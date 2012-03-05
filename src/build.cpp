#include "build.hpp"

#include "common.hpp"
#include "format.hpp"
#include "fileutil.hpp"
#include "constants.hpp"
#include "project.hpp"

#include <fstream>
#include <vector>
#include <numeric>
#include <cassert>
#include <string>
#include <memory>

#include "lz4/lz4.h"
#include "lz4hc/lz4hc.h"
#include "re2/re2.h"

class Builder::BuilderImpl
{
public:
	struct Statistics
	{
		size_t fileCount;
		uint64_t fileSize;
		uint64_t resultSize;
	};

	BuilderImpl()
	{
		statistics = Statistics();
	}

	~BuilderImpl()
	{
		flush();
	}

	bool start(const char* path)
	{
		outData.open(path, std::ios::out | std::ios::binary);
		if (!outData) return false;

		FileHeader header;
		memcpy(header.magic, kFileHeaderMagic, sizeof(header.magic));

		outData.write(reinterpret_cast<char*>(&header), sizeof(header));

		return true;
	}

	bool appendFile(const char* path)
	{
		if (currentChunk.totalSize > kChunkSize) flushChunk();

		std::ifstream in(path);
		if (!in) return false;

		File file = {path};

		if (!getFileAttributes(path, &file.timeStamp, &file.fileSize)) return false;

		while (!in.eof())
		{
			char buffer[65536];
			in.read(buffer, sizeof(buffer));

			file.contents.insert(file.contents.end(), buffer, buffer + in.gcount());
		}

		currentChunk.files.push_back(file);
		currentChunk.totalSize += file.contents.size();

		return true;
	}

	void flush()
	{
		flushChunk();
	}

	const Statistics& getStatistics() const
	{
		return statistics;
	}

private:
	struct File
	{
		std::string name;
		std::vector<char> contents;

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

	Chunk currentChunk;
	std::ofstream outData;
	Statistics statistics;

	static std::vector<char> compressData(const std::vector<char>& data)
	{
		std::vector<char> cdata(LZ4_compressBound(data.size()));
		
		int csize = LZ4_compressHC(const_cast<char*>(&data[0]), &cdata[0], data.size());
		assert(csize >= 0 && static_cast<size_t>(csize) <= cdata.size());

		cdata.resize(csize);

		return cdata;
	}

	void flushChunk()
	{
		if (currentChunk.files.empty()) return;

		std::vector<char> data = prepareChunkData(currentChunk);
		writeChunk(currentChunk, data);

		currentChunk = Chunk();
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
		size_t headerSize = sizeof(ChunkFileHeader) * chunk.files.size();
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
			std::copy(f.contents.begin(), f.contents.end(), data.begin() + dataOffset);

			ChunkFileHeader& h = reinterpret_cast<ChunkFileHeader*>(&data[0])[i];

			h.nameOffset = nameOffset;
			h.nameLength = f.name.size();
			h.dataOffset = dataOffset;
			h.dataSize = f.contents.size();

			h.fileSize = f.fileSize;
			h.timeStamp = f.timeStamp;

			nameOffset += f.name.size();
			dataOffset += f.contents.size();
		}

		assert(nameOffset == headerSize + nameSize && dataOffset == totalSize);

		return data;
	}

	void writeChunk(const Chunk& chunk, const std::vector<char>& data)
	{
		std::vector<char> cdata = compressData(data);

		ChunkHeader header = {};
		header.fileCount = chunk.files.size();
		header.uncompressedSize = data.size();
		header.compressedSize = cdata.size();

		outData.write(reinterpret_cast<char*>(&header), sizeof(header));
		outData.write(&cdata[0], cdata.size());

		statistics.fileCount += chunk.files.size();
		statistics.fileSize += data.size();
		statistics.resultSize += cdata.size();
	}
};

Builder::Builder(BuilderImpl* impl, unsigned int fileCount): impl(impl), fileCount(fileCount), lastResultSize(0)
{
}

Builder::~Builder()
{
	impl->flush();
	printStatistics();

	delete impl;
}

void Builder::appendFile(const char* path)
{
	if (!impl->appendFile(path))
		error("Error reading file %s\n", path);

	printStatistics();
}

void Builder::printStatistics()
{
	const BuilderImpl::Statistics& s = impl->getStatistics();

	if (fileCount == 0 || lastResultSize == s.resultSize) return;

	lastResultSize = s.resultSize;
	
	int percent = s.fileCount * 100 / fileCount;

	printf("\r[%3d%%] %d files, %d Mb in, %d Mb out\r", percent, s.fileCount, (int)(s.fileSize / 1024 / 1024), (int)(s.resultSize / 1024 / 1024));
	fflush(stdout);
}

Builder* createBuilder(const char* path, unsigned int fileCount)
{
	std::unique_ptr<Builder::BuilderImpl> impl(new Builder::BuilderImpl);

	if (!impl->start(path))
	{
		error("Error opening data file %s for writing\n", path);
		return 0;
	}

	return new Builder(impl.release(), fileCount);
}

void buildProject(const char* path)
{
    printf("Building %s:\n", path);
	printf("Scanning folder for files...\r");

	std::vector<std::string> files;
	if (!getProjectFiles(path, files))
	{
		return;
	}
	
	std::string targetPath = replaceExtension(path, ".qgd");
	std::string tempPath = targetPath + "_";

	{
		std::unique_ptr<Builder> builder(createBuilder(tempPath.c_str()));
		if (!builder) return;

		for (size_t i = 0; i < files.size(); ++i)
		{
			const char* path = files[i].c_str();

			builder->appendFile(path);
		}
	}

	printf("\n");
	
	if (!renameFile(tempPath.c_str(), targetPath.c_str()))
	{
		error("Error saving data file %s\n", targetPath.c_str());
		return;
	}
}
