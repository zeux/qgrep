#include "files.hpp"

#include "common.hpp"
#include "project.hpp"
#include "fileutil.hpp"
#include "format.hpp"

#include "lz4/lz4.h"
#include "lz4/lz4hc.h"

#include <fstream>
#include <cassert>

static std::vector<char> compressData(const std::vector<char>& data)
{
	std::vector<char> cdata(LZ4_compressBound(data.size()));
	
	int csize = LZ4_compressHC(const_cast<char*>(&data[0]), &cdata[0], data.size());
	assert(csize >= 0 && static_cast<size_t>(csize) <= cdata.size());

	cdata.resize(csize);

	return cdata;
}

static std::string getNameBuffer(const char** files, unsigned int count)
{
	std::string result;

	for (unsigned int i = 0; i < count; ++i)
	{
		const char* file = files[i];
		const char* slash = strrchr(file, '/');
		const char* name = slash ? slash + 1 : file;

		result += name;
		result += '\n';
	}

	return result;
}

static std::string getPathBuffer(const char** files, unsigned int count)
{
	std::string result;

	for (unsigned int i = 0; i < count; ++i)
	{
		result += files[i];
		result += '\n';
	}

	return result;
}

void buildFiles(const char* path, const char** files, unsigned int count)
{
	printf("Building file table...\r");

	std::string targetPath = replaceExtension(path, ".qgf");
	std::string tempPath = targetPath + "_";

	{
		std::ofstream out(tempPath.c_str(), std::ios::out | std::ios::binary);
		if (!out)
		{
			error("Error saving data file %s\n", tempPath.c_str());
			return;
		}

		std::string nameBuffer = getNameBuffer(files, count);
		std::string pathBuffer = getPathBuffer(files, count);

		std::vector<char> dataBuffer;
		dataBuffer.insert(dataBuffer.end(), nameBuffer.begin(), nameBuffer.end());
		dataBuffer.push_back(0);
		dataBuffer.insert(dataBuffer.end(), pathBuffer.begin(), pathBuffer.end());
		dataBuffer.push_back(0);

		std::vector<char> compressed = compressData(dataBuffer);

		FileFileHeader header;
		memcpy(header.magic, kDataFileHeaderMagic, sizeof(header.magic));

		header.fileCount = count;
		header.compressedSize = compressed.size();
		header.uncompressedSize = dataBuffer.size();

		header.nameBufferOffset = 0;
		header.pathBufferOffset = nameBuffer.size() + 1;

		out.write(reinterpret_cast<char*>(&header), sizeof(header));
		if (!compressed.empty()) out.write(&compressed[0], compressed.size());
	}

	if (!renameFile(tempPath.c_str(), targetPath.c_str()))
	{
		error("Error saving data file %s\n", targetPath.c_str());
		return;
	}
}

void buildFiles(const char* path, const std::vector<std::string>& files)
{
	std::vector<const char*> filesc(files.size());
	for (size_t i = 0; i < files.size(); ++i) filesc[i] = files[i].c_str();

	buildFiles(path, filesc.empty() ? NULL : &filesc[0], filesc.size());
}

void buildFiles(const char* path)
{
    printf("Building file table for %s:\n", path);
	printf("Scanning project...\r");

	std::vector<std::string> files;
	if (!getProjectFiles(path, files))
	{
		return;
	}

	buildFiles(path, files);
}

void searchFiles(const char* file, const char* string, unsigned int options)
{
}
