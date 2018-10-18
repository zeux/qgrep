#include "common.hpp"
#include "files.hpp"

#include "output.hpp"
#include "project.hpp"
#include "fileutil.hpp"
#include "filestream.hpp"
#include "format.hpp"
#include "compression.hpp"
#include "filter.hpp"
#include "constants.hpp"

#include <memory>

#include <string.h>

static std::vector<const char*> getFileNames(const char** files, unsigned int count)
{
	std::vector<const char*> result(count);

	for (unsigned int i = 0; i < count; ++i)
	{
		const char* file = files[i];
		const char* slash = strrchr(file, '/');
		const char* name = slash ? slash + 1 : file;

		result[i] = name;
	}

	return result;
}

static size_t getStringBufferSize(const std::vector<const char*>& strings)
{
	size_t result = 0;

	for (size_t i = 0; i < strings.size(); ++i)
		result += strlen(strings[i]) + 1;

	return result;
}

typedef std::pair<unsigned int, unsigned int> BufferOffsetLength;

static std::pair<std::vector<char>, std::pair<BufferOffsetLength, BufferOffsetLength>> prepareFileData(const std::vector<FileInfo>& files)
{
	size_t count = files.size();

	std::vector<const char*> paths(count);
	for (size_t i = 0; i < count; ++i)
		paths[i] = files[i].path.c_str();

	std::vector<const char*> names = getFileNames(paths.data(), count);

	size_t entrySize = sizeof(FileFileEntry) * count;
	size_t nameSize = getStringBufferSize(names);
	size_t pathSize = getStringBufferSize(paths);
	size_t totalSize = entrySize + nameSize + pathSize;

	std::vector<char> data(totalSize);

	size_t nameOffset = entrySize;
	size_t pathOffset = entrySize + nameSize;
	
	for (unsigned int i = 0; i < count; ++i)
	{
		size_t nameLength = strlen(names[i]);
		size_t pathLength = strlen(paths[i]);

		std::copy(names[i], names[i] + nameLength, data.begin() + nameOffset);
		data[nameOffset + nameLength] = '\n';

		std::copy(paths[i], paths[i] + pathLength, data.begin() + pathOffset);
		data[pathOffset + pathLength] = '\n';

		FileFileEntry& e = reinterpret_cast<FileFileEntry*>(&data[0])[i];

		e.nameOffset = nameOffset;
		e.pathOffset = pathOffset;

		nameOffset += nameLength + 1;
		pathOffset += pathLength + 1;
	}

	assert(nameOffset == entrySize + nameSize && pathOffset == totalSize);

	return std::make_pair(data, std::make_pair(BufferOffsetLength(entrySize, nameSize), BufferOffsetLength(entrySize + nameSize, pathSize)));
}

bool buildFiles(Output* output, const char* path, const std::vector<FileInfo>& files)
{
	std::string targetPath = replaceExtension(path, ".qgf");
	std::string tempPath = targetPath + "_";

	{
		createPathForFile(tempPath.c_str());

		FileStream out(tempPath.c_str(), "wb");
		if (!out)
		{
			output->error("Error saving data file %s\n", tempPath.c_str());
			return false;
		}

		std::pair<std::vector<char>, std::pair<BufferOffsetLength, BufferOffsetLength>> data = prepareFileData(files);
		std::pair<std::unique_ptr<char[]>, size_t> compressed = compress(data.first.data(), data.first.size(), kFileListCompressionLevel);

		FileFileHeader header;
		memcpy(header.magic, kFileFileHeaderMagic, sizeof(header.magic));

		header.fileCount = files.size();
		header.compressedSize = compressed.second;
		header.uncompressedSize = data.first.size();

		header.nameBufferOffset = data.second.first.first;
		header.nameBufferLength = data.second.first.second;
		header.pathBufferOffset = data.second.second.first;
		header.pathBufferLength = data.second.second.second;

		out.write(&header, sizeof(header));
		if (compressed.first) out.write(compressed.first.get(), compressed.second);
	}

	if (!renameFile(tempPath.c_str(), targetPath.c_str()))
	{
		output->error("Error saving data file %s\n", targetPath.c_str());
		return false;
	}

	return true;
}

template <typename ExtractOffset>
static void buildFilterEntries(FilterEntries& result, std::unique_ptr<FilterEntry[]>& entryptr,
    const FileFileHeader& header, const char* data, unsigned int bufferOffset, unsigned int bufferLength, ExtractOffset extractOffset)
{
    const FileFileEntry* entries = reinterpret_cast<const FileFileEntry*>(data);

    entryptr.reset(new FilterEntry[header.fileCount]);

    result.buffer = data + bufferOffset;
    result.bufferSize = bufferLength;

    result.entries = entryptr.get();
    result.entryCount = header.fileCount;

    for (size_t i = 0; i < header.fileCount; ++i)
    {
        FilterEntry& e = result.entries[i];

        size_t offset = extractOffset(entries[i]);

        e.offset = offset - bufferOffset;
        e.length = (i + 1 < header.fileCount ? extractOffset(entries[i + 1]) : bufferOffset + bufferLength) - offset - 1;
    }
}

unsigned int searchFiles(Output* output, const char* file, const char* string, unsigned int options, unsigned int limit, const char* include, const char* exclude)
{
	std::string dataPath = replaceExtension(file, ".qgf");
	FileStream in(dataPath.c_str(), "rb");
	if (!in)
	{
		output->error("Error reading data file %s\n", dataPath.c_str());
		return 0;
	}
	
	FileFileHeader header;
	if (!read(in, header) || memcmp(header.magic, kFileFileHeaderMagic, strlen(kFileFileHeaderMagic)) != 0)
	{
		output->error("Error reading data file %s: malformed header\n", dataPath.c_str());
		return 0;
	}

	std::unique_ptr<char[]> buffer(new (std::nothrow) char[header.compressedSize + header.uncompressedSize]);

	if (!buffer || !read(in, buffer.get(), header.compressedSize))
	{
		output->error("Error reading data file %s: malformed header\n", dataPath.c_str());
		return 0;
	}

	char* data = buffer.get() + header.compressedSize;
	decompress(data, header.uncompressedSize, buffer.get(), header.compressedSize);

    FilterEntries paths;
    std::unique_ptr<FilterEntry[]> pathEntries;
    buildFilterEntries(paths, pathEntries, header, data, header.pathBufferOffset, header.pathBufferLength, [](const FileFileEntry& e) { return e.pathOffset; });

    FilterEntries names;
    std::unique_ptr<FilterEntry[]> nameEntries;
    buildFilterEntries(names, nameEntries, header, data, header.nameBufferOffset, header.nameBufferLength, [](const FileFileEntry& e) { return e.nameOffset; });

    return filter(output, string, options, limit, paths, &names);
}
