#include "files.hpp"

#include "common.hpp"
#include "project.hpp"
#include "fileutil.hpp"
#include "format.hpp"
#include "search.hpp"
#include "regex.hpp"

#include "lz4/lz4.h"
#include "lz4/lz4hc.h"

#include <fstream>
#include <cassert>
#include <memory>
#include <algorithm>

static std::vector<char> compressData(const std::vector<char>& data)
{
	std::vector<char> cdata(LZ4_compressBound(data.size()));
	
	int csize = LZ4_compressHC(const_cast<char*>(&data[0]), &cdata[0], data.size());
	assert(csize >= 0 && static_cast<size_t>(csize) <= cdata.size());

	cdata.resize(csize);

	return cdata;
}

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

	return result + 1;
}

static std::pair<std::vector<char>, std::pair<unsigned int, unsigned int>> prepareFileData(const char** files, unsigned int count)
{
	std::vector<const char*> paths(files, files + count);
	std::vector<const char*> names = getFileNames(files, count);

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

	assert(nameOffset + 1 == entrySize + nameSize && pathOffset + 1 == totalSize);

	return std::make_pair(data, std::make_pair(entrySize, entrySize + nameSize));
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

		std::pair<std::vector<char>, std::pair<unsigned int, unsigned int>> data = prepareFileData(files, count);
		std::vector<char> compressed = compressData(data.first);

		FileFileHeader header;
		memcpy(header.magic, kFileFileHeaderMagic, sizeof(header.magic));

		header.fileCount = count;
		header.compressedSize = compressed.size();
		header.uncompressedSize = data.first.size();

		header.nameBufferOffset = data.second.first;
		header.pathBufferOffset = data.second.second;

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

inline bool read(std::istream& in, void* data, size_t size)
{
	in.read(static_cast<char*>(data), size);
	return in.gcount() == size;
}

template <typename T> inline bool read(std::istream& in, T& value)
{
	return read(in, &value, sizeof(T));
}

std::unique_ptr<char[]> safeAlloc(size_t size)
{
	try
	{
		return std::unique_ptr<char[]>(new char[size]);
	}
	catch (const std::bad_alloc&)
	{
		return std::unique_ptr<char[]>();
	}
}

static const char* findLineEnd(const char* pos, const char* end)
{
	for (const char* s = pos; s != end; ++s)
		if (*s == '\n')
			return s;

	return end;
}

struct BackSlashTransformer
{
	char operator()(char ch) const
	{
		return (ch == '/') ? '\\' : ch;
	}
};

static void processMatch(const FileFileEntry& entry, const char* data, unsigned int options)
{
	const char* path = entry.pathOffset + data;

	const char* pathEnd = strchr(path, '\n');
	assert(pathEnd);

	size_t pathLength = pathEnd - path;

	if (options & SO_VISUALSTUDIO)
	{
		char* buffer = static_cast<char*>(alloca(pathLength));
		
		std::transform(path, path + pathLength, buffer, BackSlashTransformer());
		path = buffer;
	}

	printf("%.*s\n", static_cast<unsigned>(pathLength), path);
}

template <typename ExtractOffset>
static void searchFilesRegex(const FileFileHeader& header, const char* data, const char* buffer, const char* string, unsigned int options, ExtractOffset extractOffset)
{
	std::unique_ptr<Regex> re(createRegex(string, getRegexOptions(options)));

	const FileFileEntry* entries = reinterpret_cast<const FileFileEntry*>(data);

	size_t size = strlen(buffer);

	const char* range = re->rangePrepare(buffer, size);

	const char* begin = range;
	const char* end = begin + size;

	while (RegexMatch match = re->rangeSearch(begin, end - begin))
	{
		// find file index
		size_t matchOffset = (match.data - begin) + (buffer - data);
		const FileFileEntry* entry =
			std::lower_bound(entries, entries + header.fileCount, matchOffset, [=](const FileFileEntry& e, size_t o) { return extractOffset(e) < o; });
		assert(entry < entries + header.fileCount);

		// print match
		processMatch(*entry, data, options);

		// move to next line
		const char* lend = findLineEnd(match.data + match.size, end);
		if (lend == end) break;
		begin = lend + 1;
	}

	re->rangeFinalize(range);
}

static void searchFilesSolution(const FileFileHeader& header, const char* data, const char* string, unsigned int options)
{
}

static void searchFiles(const FileFileHeader& header, const char* data, const char* string, unsigned int options)
{
	if (options & SO_FILE_NAMEREGEX)
		searchFilesRegex(header, data, data + header.nameBufferOffset, string, options, [](const FileFileEntry& e) { return e.nameOffset; });
	else if (options & SO_FILE_PATHREGEX)
		searchFilesRegex(header, data, data + header.pathBufferOffset, string, options, [](const FileFileEntry& e) { return e.pathOffset; });
	else
		searchFilesSolution(header, data, string, options);
}

void searchFiles(const char* file, const char* string, unsigned int options)
{
	std::string dataPath = replaceExtension(file, ".qgf");
	std::ifstream in(dataPath.c_str(), std::ios::in | std::ios::binary);
	if (!in)
	{
		error("Error reading data file %s\n", dataPath.c_str());
		return;
	}
	
	FileFileHeader header;
	if (!read(in, header) || memcmp(header.magic, kFileFileHeaderMagic, strlen(kFileFileHeaderMagic)) != 0)
	{
		error("Error reading data file %s: malformed header\n", dataPath.c_str());
		return;
	}

	std::unique_ptr<char[]> buffer = safeAlloc(header.compressedSize + header.uncompressedSize);

	if (!buffer || !read(in, buffer.get(), header.compressedSize))
	{
		error("Error reading data file %s: malformed header\n", dataPath.c_str());
		return;
	}

	char* data = buffer.get() + header.compressedSize;
	LZ4_uncompress(buffer.get(), data, header.uncompressedSize);

	searchFiles(header, data, string, options);
}
