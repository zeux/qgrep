#include "common.hpp"
#include "build.hpp"

#include "output.hpp"
#include "format.hpp"
#include "fileutil.hpp"
#include "constants.hpp"
#include "project.hpp"
#include "encoding.hpp"
#include "files.hpp"
#include "bloom.hpp"
#include "casefold.hpp"

#include <fstream>
#include <vector>
#include <list>
#include <numeric>
#include <string>
#include <memory>
#include <unordered_set>

#include <string.h>

#include "lz4/lz4.h"
#include "lz4/lz4hc.h"

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
	{
		pendingSize = 0;
		statistics = Statistics();
	}

	~BuilderImpl()
	{
		flush();
	}

	bool start(const char* path)
	{
        createPathForFile(path);

		outData.open(path, std::ios::out | std::ios::binary);
		if (!outData) return false;

		DataFileHeader header;
		memcpy(header.magic, kDataFileHeaderMagic, sizeof(header.magic));

		outData.write(reinterpret_cast<char*>(&header), sizeof(header));

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

	static std::vector<char> readFile(std::ifstream& in)
	{
		std::vector<char> result;

        // read file as is
		while (!in.eof())
		{
			char buffer[65536];
			in.read(buffer, sizeof(buffer));

			result.insert(result.end(), buffer, buffer + in.gcount());
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

	bool appendFile(const char* path, uint64_t lastWriteTime, uint64_t fileSize)
	{
		std::ifstream in(path, std::ios::in | std::ios::binary);
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

	bool appendChunk(const DataChunkHeader& header, const char* compressedData, const char* index, bool firstFileIsSuffix)
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
		writeChunk(header, compressedData, index, firstFileIsSuffix);

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

	const Statistics& getStatistics() const
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

	std::list<File> pendingFiles;
	size_t pendingSize;
	
	std::ofstream outData;
	Statistics statistics;

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

	static std::vector<char> compressData(const std::vector<char>& data)
	{
		std::vector<char> cdata(LZ4_compressBound(data.size()));
		
		int csize = LZ4_compressHC(const_cast<char*>(&data[0]), &cdata[0], data.size());
		assert(csize >= 0 && static_cast<size_t>(csize) <= cdata.size());

		cdata.resize(csize);

		return cdata;
	}

	void flushChunk(const Chunk& chunk)
	{
		if (chunk.files.empty()) return;

		std::vector<char> data = prepareChunkData(chunk);
		std::pair<std::vector<char>, unsigned int> index = prepareChunkIndex(chunk);

		writeChunk(chunk, index, data);
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

	std::vector<char> prepareChunkData(const Chunk& chunk)
	{
		size_t headerSize = sizeof(DataChunkFileHeader) * chunk.files.size();
		size_t nameSize = getChunkNameTotalSize(chunk);
		size_t dataSize = getChunkDataTotalSize(chunk);
		size_t totalSize = headerSize + nameSize + dataSize;

		std::vector<char> data(totalSize);

		size_t nameOffset = headerSize;
		size_t dataOffset = headerSize + nameSize;

		for (size_t i = 0; i < chunk.files.size(); ++i)
		{
			const File& f = chunk.files[i];

			std::copy(f.name.begin(), f.name.end(), data.begin() + nameOffset);
			std::copy(f.contents.data(), f.contents.data() + f.contents.size(), data.begin() + dataOffset);

			DataChunkFileHeader& h = reinterpret_cast<DataChunkFileHeader*>(&data[0])[i];

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

		return data;
	}

	size_t getChunkIndexSize(const Chunk& chunk)
	{
		size_t dataSize = getChunkDataTotalSize(chunk);

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

	std::pair<std::vector<char>, unsigned int> prepareChunkIndex(const Chunk& chunk)
	{
		// estimate index size
		size_t indexSize = getChunkIndexSize(chunk);

		if (indexSize == 0) return std::make_pair(std::vector<char>(), 0);

		// collect ngram data
		IntSet ngrams; // std::unordered_set<unsigned int> ngrams;

		for (size_t i = 0; i < chunk.files.size(); ++i)
		{
			const File& file = chunk.files[i];
			const char* filedata = file.contents.data();

			for (size_t j = 3; j < file.contents.size(); ++j)
			{
				char a = filedata[j - 3], b = filedata[j - 2], c = filedata[j - 1], d = filedata[j];

				// don't waste bits on ngrams that cross lines
				if (a != '\n' && b != '\n' && c != '\n' && d != '\n')
				{
					unsigned int n = ngram(casefold(a), casefold(b), casefold(c), casefold(d));
					if (n != 0) ngrams.insert(n);
				}
			}
		}

		// estimate iteration count
		unsigned int iterations = getIndexHashIterations(indexSize, ngrams.size);

		// fill bloom filter
		std::vector<char> result(indexSize);

		unsigned char* data = reinterpret_cast<unsigned char*>(&result[0]);

		for (auto n: ngrams.data)
			if (n != 0)
				bloomFilterUpdate(data, indexSize, n, iterations);

		return std::make_pair(result, iterations);
	}

	void writeChunk(const Chunk& chunk, const std::pair<std::vector<char>, unsigned int>& index, const std::vector<char>& data)
	{
		std::vector<char> cdata = compressData(data);

		DataChunkHeader header = {};
		header.fileCount = chunk.files.size();
		header.uncompressedSize = data.size();
		header.compressedSize = cdata.size();
		header.indexSize = index.first.size();
		header.indexHashIterations = index.second;

		outData.write(reinterpret_cast<const char*>(&header), sizeof(header));
		if (!index.first.empty()) outData.write(&index.first[0], index.first.size());
		outData.write(&cdata[0], cdata.size());

		statistics.chunkCount++;

		for (size_t i = 0; i < chunk.files.size(); ++i)
			if (chunk.files[i].startLine == 0)
				statistics.fileCount++;

		statistics.fileSize += data.size();
		statistics.resultSize += cdata.size();
	}

	void writeChunk(const DataChunkHeader& header, const char* compressedData, const char* index, bool firstFileIsSuffix)
	{
		outData.write(reinterpret_cast<const char*>(&header), sizeof(header));
		outData.write(index, header.indexSize);
		outData.write(compressedData, header.compressedSize);

		statistics.chunkCount++;
		statistics.fileCount += header.fileCount - firstFileIsSuffix;
		statistics.fileSize += header.uncompressedSize;
		statistics.resultSize += header.compressedSize;
	}
};

Builder::Builder(Output* output, BuilderImpl* impl, unsigned int fileCount): impl(impl), output(output), fileCount(fileCount), lastResultSize(0)
{
}

Builder::~Builder()
{
	impl->flush();
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

bool Builder::appendChunk(const DataChunkHeader& header, const void* compressedData, const void* index, bool firstFileIsSuffix)
{
	if (impl->appendChunk(header, static_cast<const char*>(compressedData), static_cast<const char*>(index), firstFileIsSuffix))
	{
		printStatistics();
		return true;
	}

	return false;
}

unsigned int Builder::flush()
{
	impl->flush();

	return impl->getStatistics().chunkCount;
}

void Builder::printStatistics()
{
	const BuilderImpl::Statistics& s = impl->getStatistics();

	if (fileCount == 0 || lastResultSize == s.resultSize) return;

	lastResultSize = s.resultSize;
	
	int percent = s.fileCount * 100 / fileCount;

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
