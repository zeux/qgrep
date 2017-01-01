#include "common.hpp"
#include "build.hpp"

#include "output.hpp"
#include "format.hpp"
#include "fileutil.hpp"
#include "filestream.hpp"
#include "constants.hpp"
#include "project.hpp"
#include "encoding.hpp"
#include "files.hpp"
#include "bloom.hpp"
#include "casefold.hpp"
#include "compression.hpp"
#include "workqueue.hpp"
#include "blockingqueue.hpp"

#include <vector>
#include <list>
#include <numeric>
#include <string>
#include <memory>
#include <map>
#include <unordered_set>

#include <string.h>

class Builder::BuilderImpl
{
public:
	struct Statistics
	{
		size_t chunkCount;
		size_t fileCount;
		uint64_t fileSize;
		uint64_t resultSize;
	};

	BuilderImpl()
	: pendingSize(0), statistics(), chunkOrder(0)
	, prepareChunkQueue(std::max(WorkQueue::getIdealWorkerCount(), 2u) - 1, kMaxQueuedChunkData)
	, writeChunkThread(std::bind(writeChunkThreadFun, std::ref(outData), std::ref(statistics), std::ref(writeChunkQueue)))
	{
	}

	~BuilderImpl()
	{
		finish();
	}

	bool start(const char* path)
	{
        createPathForFile(path);

		outData.open(path, "wb");
		if (!outData) return false;

		DataFileHeader header;
		memcpy(header.magic, kDataFileHeaderMagic, sizeof(header.magic));

		outData.write(&header, sizeof(header));

		return true;
	}

	void appendFilePart(const char* path, unsigned int startLine, const char* data, size_t dataSize, uint64_t lastWriteTime, uint64_t fileSize)
	{
		if (!pendingFiles.empty() && pendingFiles.back().name == path)
		{
			File& file = pendingFiles.back();

			assert(file.startLine < startLine);
			assert(file.timeStamp == lastWriteTime && file.fileSize == fileSize);
			assert(file.contents.offset + file.contents.count == file.contents.storage->size());

			file.contents.storage->insert(file.contents.storage->end(), data, data + dataSize);
			file.contents.count += dataSize;

			pendingSize += dataSize;
		}
		else
		{
			File file;

			file.name = path;
			file.startLine = startLine;
			file.timeStamp = lastWriteTime;
			file.fileSize = fileSize;
			file.contents = std::vector<char>(data, data + dataSize);

			pendingFiles.emplace_back(file);
			pendingSize += dataSize;
		}

		flushIfNeeded();
	}

	bool appendFile(const char* path, uint64_t lastWriteTime, uint64_t fileSize)
	{
		FileStream in(path, "rb");
		if (!in) return false;

		try
		{
			std::vector<char> contents = convertToUTF8(readFile(in));

			appendFilePart(path, 0, contents.empty() ? 0 : &contents[0], contents.size(), lastWriteTime, fileSize);

			return true;
		}
		catch (const std::bad_alloc&)
		{
			return false;
		}
	}

	bool appendChunk(const DataChunkHeader& header, std::unique_ptr<char[]>& compressedData, std::unique_ptr<char[]>& index, bool firstFileIsSuffix)
	{
		flushIfNeeded();

		// Because of the logic in flushIfNeeded, we should now have kChunkSize * m pending data, where m is in [0..2)
		// Moreover, usually m is in [1..2), since flushIfNeeded leaves kChunkSize worth of data. We can try to either
		// flush the entire pending set as one chunk, or do it in two chunks. Let's pick m=1.5 as a split decision point,
		// with m=0.75 (1.5/2) as a point when we refuse to append the chunk.
		const size_t kChunkMaxSize = kChunkSize * 3 / 2;
		const size_t kChunkMinSize = kChunkMaxSize / 2;

		if (pendingSize > 0)
		{
			// Assumptions above are invalid for some reason, bail out
			if (pendingSize > kChunkSize * 2) return false;

			// Never leave chunks that are too small
			if (pendingSize < kChunkMinSize) return false;
			
			// Never make chunks that are too big
			if (pendingSize > kChunkMaxSize) flushChunk(pendingSize / 2);

			assert(pendingSize < kChunkMaxSize);
			flushChunk(pendingSize);
		}

		// We should be good to go now
		assert(pendingSize == 0 && pendingFiles.empty());

		unsigned int order = chunkOrder++;
		writeChunk(order, header, std::move(compressedData), std::move(index), firstFileIsSuffix);

		return true;
	}

	void flushIfNeeded()
	{
		while (pendingSize >= kChunkSize * 2)
		{
			flushChunk(kChunkSize);
		}
	}

	void flush()
	{
		while (pendingSize > 0)
		{
			flushChunk(kChunkSize);
		}
	}

	void finish()
	{
		if (writeChunkThread.joinable())
		{
			flush();

			ChunkFileData chunkDummy = { chunkOrder };
			writeChunkQueue.push(std::move(chunkDummy));
			writeChunkThread.join();
		}
	}

	Statistics getStatistics() const
	{
		return statistics;
	}

private:
	struct Blob
	{
		size_t offset;
		size_t count;
		std::shared_ptr<std::vector<char>> storage;

		Blob(): offset(0), count(0)
		{
		}

		Blob(std::vector<char> storage): offset(0), count(storage.size()), storage(new std::vector<char>(std::move(storage)))
		{
		}

		const char* data() const
		{
			assert(offset + count <= storage->size());
			return storage->empty() ? nullptr : &(*storage)[0] + offset;
		}

		size_t size() const
		{
			return count;
		}
	};

	struct File
	{
		std::string name;
		Blob contents;

		uint32_t startLine;
		uint64_t fileSize;
		uint64_t timeStamp;
	};

	struct Chunk
	{
		std::vector<File> files;
		size_t totalSize;

		Chunk(): totalSize(0)
		{
		}
	};

	struct ChunkData
	{
		std::unique_ptr<char[]> data;
		size_t size;

		size_t dataOffset;
		size_t dataSize;
	};

	struct ChunkIndex
	{
		std::unique_ptr<char[]> data;
		size_t size;
		unsigned int iterations;

		ChunkIndex(): size(0), iterations(0)
		{
		}
	};

	struct ChunkFileData
	{
		unsigned int order;

		DataChunkHeader header;
		std::unique_ptr<char[]> compressedData;
		std::unique_ptr<char[]> index;
		bool firstFileIsSuffix;
	};

	std::list<File> pendingFiles;
	size_t pendingSize;
	
	FileStream outData;
	Statistics statistics;

	unsigned int chunkOrder;
	WorkQueue prepareChunkQueue;
	BlockingQueue<ChunkFileData> writeChunkQueue;
	std::thread writeChunkThread;

    static size_t normalizeEOL(char* data, size_t size)
    {
        // replace \r\n with \n, replace stray \r with \n
        size_t result = 0;

        for (size_t i = 0; i < size; ++i)
        {
            if (data[i] == '\r')
            {
                data[result++] = '\n';
                if (i + 1 < size && data[i + 1] == '\n') i++;
            }
            else
                data[result++] = data[i];
        }

		return result;
    }

	static std::vector<char> readFile(FileStream& in)
	{
		std::vector<char> result;

        // read file as is
		char buffer[65536];
		size_t readsize;

		while ((readsize = in.read(buffer, sizeof(buffer))) > 0)
		{
			result.insert(result.end(), buffer, buffer + readsize);
		}

        // normalize new lines in a cross-platform way (don't rely on text-mode file I/O)
        if (!result.empty())
        {
            size_t size = normalizeEOL(&result[0], result.size());
            assert(size <= result.size());
            result.resize(size);
        }

		return result;
	}

	static std::pair<size_t, unsigned int> skipByLines(const char* data, size_t dataSize)
	{
		auto result = std::make_pair(0, 0);

		for (size_t i = 0; i < dataSize; ++i)
			if (data[i] == '\n')
			{
				result.first = i + 1;
				result.second++;
			}

		return result;
	}

	static size_t skipOneLine(const char* data, size_t dataSize)
	{
		for (size_t i = 0; i < dataSize; ++i)
			if (data[i] == '\n')
				return i + 1;

		return dataSize;
	}

	static File splitPrefix(File& file, size_t size)
	{
		File result = file;

		assert(size <= file.contents.size());
		result.contents.count = size;
		file.contents.offset += size;
		file.contents.count -= size;

		return result;
	}

	static void appendChunkFile(Chunk& chunk, File&& file)
	{
		chunk.totalSize += file.contents.size();
		chunk.files.emplace_back(std::move(file));
	}

	static void appendChunkFilePrefix(Chunk& chunk, File& file, size_t remainingSize)
	{
		const char* data = file.contents.data();
		size_t dataSize = file.contents.size();

		assert(remainingSize < dataSize);
		std::pair<size_t, unsigned int> skip = skipByLines(data, remainingSize);

		// add file even if we could not split the (very large) line if it'll be the only file in chunk
		if (skip.first > 0 || chunk.files.empty())
		{
			size_t skipSize = (skip.first > 0) ? skip.first : skipOneLine(data, dataSize);
			unsigned int skipLines = (skip.first > 0) ? skip.second : 1;

			chunk.totalSize += skipSize;
			chunk.files.push_back(splitPrefix(file, skipSize));

			file.startLine += skipLines;
		}
	}

	void flushChunk(size_t size)
	{
		Chunk chunk;

		// grab pending files one by one and add it to current chunk
		while (chunk.totalSize < size && !pendingFiles.empty())
		{
			File file = std::move(pendingFiles.front());
			pendingFiles.pop_front();

			size_t remainingSize = size - chunk.totalSize;

			if (file.contents.size() <= remainingSize)
			{
				// no need to split the file, just add it
				appendChunkFile(chunk, std::move(file));
			}
			else
			{
				// last file does not fit completely, store some part of it and put the remaining lines back into pending list
				appendChunkFilePrefix(chunk, file, remainingSize);
				pendingFiles.emplace_front(file);

				// it's impossible to add any more files to this chunk without making it larger than requested
				break;
			}
		}

		// update pending size
		assert(chunk.totalSize <= pendingSize);
		pendingSize -= chunk.totalSize;

		// store resulting chunk
		flushChunk(chunk);
	}

	void flushChunk(const Chunk& chunk)
	{
		if (chunk.files.empty()) return;

		ChunkData data = prepareChunkData(chunk);
		unsigned int order = chunkOrder++;

		size_t fileCount = chunk.files.size();
		bool firstFileIsSuffix = !chunk.files.empty() && chunk.files[0].startLine != 0;

		// workaround for lack of generalized capture
		std::shared_ptr<ChunkData> sdata(new ChunkData(std::move(data)));

		prepareChunkQueue.push([this, sdata, order, fileCount, firstFileIsSuffix] {
			ChunkIndex index = prepareChunkIndex(sdata->data.get() + sdata->dataOffset, sdata->dataSize);

			std::pair<std::unique_ptr<char[]>, size_t> cdata = compress(sdata->data.get(), sdata->size, /* compressionLevel= */ 9);

			DataChunkHeader header = {};
			header.fileCount = fileCount;
			header.fileTableSize = sdata->dataOffset;
			header.compressedSize = cdata.second;
			header.uncompressedSize = sdata->size;
			header.indexSize = index.size;
			header.indexHashIterations = index.iterations;

			writeChunk(order, header, std::move(cdata.first), std::move(index.data), firstFileIsSuffix);
		}, sdata->size);
	}

	size_t getChunkNameTotalSize(const Chunk& chunk)
	{
		size_t result = 0;

		for (size_t i = 0; i < chunk.files.size(); ++i)
			result += chunk.files[i].name.size();

		return result;
	}

	size_t getChunkDataTotalSize(const Chunk& chunk)
	{
		size_t result = 0;

		for (size_t i = 0; i < chunk.files.size(); ++i)
			result += chunk.files[i].contents.size();

		return result;
	}

	ChunkData prepareChunkData(const Chunk& chunk)
	{
		size_t headerSize = sizeof(DataChunkFileHeader) * chunk.files.size();
		size_t nameSize = getChunkNameTotalSize(chunk);
		size_t dataSize = getChunkDataTotalSize(chunk);
		size_t totalSize = headerSize + nameSize + dataSize;

		ChunkData result;
		result.data.reset(new char[totalSize]);
		result.size = totalSize;
		result.dataOffset = headerSize + nameSize;
		result.dataSize = dataSize;

		size_t nameOffset = headerSize;
		size_t dataOffset = headerSize + nameSize;

		for (size_t i = 0; i < chunk.files.size(); ++i)
		{
			const File& f = chunk.files[i];

			memcpy(result.data.get() + nameOffset, f.name.c_str(), f.name.length());
			memcpy(result.data.get() + dataOffset, f.contents.data(), f.contents.size());

			DataChunkFileHeader& h = reinterpret_cast<DataChunkFileHeader*>(result.data.get())[i];

			h.nameOffset = nameOffset;
			h.nameLength = f.name.size();
			h.dataOffset = dataOffset;
			h.dataSize = f.contents.size();

			h.startLine = f.startLine;
			h.reserved = 0;

			h.fileSize = f.fileSize;
			h.timeStamp = f.timeStamp;

			nameOffset += f.name.size();
			dataOffset += f.contents.size();
		}

		assert(nameOffset == headerSize + nameSize && dataOffset == totalSize);

		return result;
	}

	size_t getChunkIndexSize(size_t dataSize)
	{
		// data compression ratio is ~5x
		// we want the index to be ~10% of the compressed data
		// so index is ~50x smaller than the original data
		size_t indexSize = dataSize / 50;

		// don't bother storing tiny indices
		return indexSize < 1024 ? 0 : indexSize;
	}

	// http://pages.cs.wisc.edu/~cao/papers/summary-cache/node8.html 
	unsigned int getIndexHashIterations(unsigned int indexSize, unsigned int itemCount)
	{
		unsigned int m = indexSize * 8;
		unsigned int n = itemCount;
		double k = n == 0 ? 1.0 : 0.693147181 * static_cast<double>(m) / static_cast<double>(n);

		return (k < 1) ? 1 : (k > 16) ? 16 : static_cast<unsigned int>(k);
	}

	struct IntSet
	{
		std::vector<unsigned int> data;
		unsigned int size;

		IntSet(size_t capacity = 16): data(capacity), size(0)
		{
		}

		void grow()
		{
			IntSet res(data.size() * 2);

			for (size_t i = 0; i < data.size(); ++i)
				if (data[i])
					res.insert(data[i]);

			data.swap(res.data);
		}

		void insert(unsigned int key)
		{
			assert(key != 0);

			if (size * 2 > data.size()) grow();

			unsigned int h = bloomHash2(key) & (data.size() - 1);

			while (data[h] != key)
			{
				if (data[h] == 0)
				{
					data[h] = key;
					size++;
					break;
				}

				h = (h + 7) & (data.size() - 1);
			}
		}
	};

	ChunkIndex prepareChunkIndex(const char* data, size_t size)
	{
		// estimate index size
		size_t indexSize = getChunkIndexSize(size);

		if (indexSize == 0) return ChunkIndex();

		// collect ngram data
		IntSet ngrams;

		for (size_t i = 3; i < size; ++i)
		{
			char a = data[i - 3], b = data[i - 2], c = data[i - 1], d = data[i];

			// don't waste bits on ngrams that cross lines
			if (a != '\n' && b != '\n' && c != '\n' && d != '\n')
			{
				unsigned int n = ngram(casefold(a), casefold(b), casefold(c), casefold(d));
				if (n != 0) ngrams.insert(n);
			}
		}

		// estimate iteration count
		unsigned int iterations = getIndexHashIterations(indexSize, ngrams.size);

		// fill bloom filter
		ChunkIndex result;
		result.data.reset(new char[indexSize]);
		result.size = indexSize;
		result.iterations = iterations;

		unsigned char* index = reinterpret_cast<unsigned char*>(result.data.get());

		memset(index, 0, indexSize);

		for (auto n: ngrams.data)
			if (n != 0)
				bloomFilterUpdate(index, indexSize, n, iterations);

		return result;
	}

	void writeChunk(unsigned int order, const DataChunkHeader& header, std::unique_ptr<char[]> compressedData, std::unique_ptr<char[]> index, bool firstFileIsSuffix)
	{
		assert(compressedData);
		ChunkFileData chunk = { order, header, std::move(compressedData), std::move(index), firstFileIsSuffix };

		writeChunkQueue.push(std::move(chunk));
	}

	static void writeChunkThreadFun(FileStream& outData, Statistics& statistics, BlockingQueue<ChunkFileData>& queue)
	{
		unsigned int order = 0;
		std::map<unsigned int, ChunkFileData> chunks;

		while (true)
		{
			ChunkFileData chunk = queue.pop();

			assert(chunks.count(chunk.order) == 0);
			chunks[chunk.order] = std::move(chunk);

			while (!chunks.empty() && chunks.begin()->first == order)
			{
				const ChunkFileData& chunk = chunks.begin()->second;
				const DataChunkHeader& header = chunk.header;

				// empty compressed data acts as a terminator flag
				if (!chunk.compressedData)
					return;

				outData.write(&header, sizeof(header));
				outData.write(chunk.index.get(), header.indexSize);
				outData.write(chunk.compressedData.get(), header.compressedSize);

				statistics.chunkCount++;
				statistics.fileCount += header.fileCount - chunk.firstFileIsSuffix;
				statistics.fileSize += header.uncompressedSize;
				statistics.resultSize += header.compressedSize;

				chunks.erase(chunks.begin());
				order++;
			}
		}
	}
};

Builder::Builder(Output* output, BuilderImpl* impl, unsigned int fileCount): impl(impl), output(output), fileCount(fileCount), lastResultSize(~0ull)
{
	printStatistics();
}

Builder::~Builder()
{
	impl->finish();

	printStatistics();

	delete impl;
}

void Builder::appendFile(const char* path, uint64_t lastWriteTime, uint64_t fileSize)
{
	if (!impl->appendFile(path, lastWriteTime, fileSize))
		output->error("Error reading file %s\n", path);

	printStatistics();
}

void Builder::appendFilePart(const char* path, unsigned int startLine, const void* data, size_t dataSize, uint64_t lastWriteTime, uint64_t fileSize)
{
	impl->appendFilePart(path, startLine, static_cast<const char*>(data), dataSize, lastWriteTime, fileSize);
	printStatistics();
}

bool Builder::appendChunk(const DataChunkHeader& header, std::unique_ptr<char[]>& compressedData, std::unique_ptr<char[]>& index, bool firstFileIsSuffix)
{
	if (impl->appendChunk(header, compressedData, index, firstFileIsSuffix))
	{
		printStatistics();
		return true;
	}

	return false;
}

unsigned int Builder::finish()
{
	impl->finish();

	return impl->getStatistics().chunkCount;
}

void Builder::printStatistics()
{
	BuilderImpl::Statistics s = impl->getStatistics();

	if (lastResultSize == s.resultSize) return;

	lastResultSize = s.resultSize;
	
	int percent = fileCount == 0 ? 100 : s.fileCount * 100 / fileCount;

	output->print("\r[%3d%%] %d files, %d Mb in, %d Mb out\r", percent, s.fileCount, (int)(s.fileSize / 1024 / 1024), (int)(s.resultSize / 1024 / 1024));
}

Builder* createBuilder(Output* output, const char* path, unsigned int fileCount)
{
	std::unique_ptr<Builder::BuilderImpl> impl(new Builder::BuilderImpl);

	if (!impl->start(path))
	{
		output->error("Error opening data file %s for writing\n", path);
		return 0;
	}

	return new Builder(output, impl.release(), fileCount);
}

void buildProject(Output* output, const char* path)
{
    output->print("Building %s:\n", path);
	output->print("Scanning project...\r");

	std::vector<FileInfo> files;
	if (!getProjectFiles(output, path, files))
	{
		return;
	}

	buildFiles(output, path, files);
	
	std::string targetPath = replaceExtension(path, ".qgd");
	std::string tempPath = targetPath + "_";

	{
		std::unique_ptr<Builder> builder(createBuilder(output, tempPath.c_str(), files.size()));
		if (!builder) return;

		for (auto& f: files)
		{
			builder->appendFile(f.path.c_str(), f.lastWriteTime, f.fileSize);
		}
	}

	output->print("\n");
	
	if (!renameFile(tempPath.c_str(), targetPath.c_str()))
	{
		output->error("Error saving data file %s\n", targetPath.c_str());
		return;
	}
}
