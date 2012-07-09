#include "common.hpp"
#include "files.hpp"

#include "output.hpp"
#include "project.hpp"
#include "fileutil.hpp"
#include "format.hpp"
#include "search.hpp"
#include "regex.hpp"
#include "stringutil.hpp"
#include "streamutil.hpp"
#include "highlight.hpp"
#include "fuzzymatch.hpp"

#include "lz4/lz4.h"
#include "lz4/lz4hc.h"

#include <fstream>
#include <memory>
#include <algorithm>
#include <limits.h>

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

	return result;
}

typedef std::pair<unsigned int, unsigned int> BufferOffsetLength;

static std::pair<std::vector<char>, std::pair<BufferOffsetLength, BufferOffsetLength>> prepareFileData(const char** files, unsigned int count)
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

	assert(nameOffset == entrySize + nameSize && pathOffset == totalSize);

	return std::make_pair(data, std::make_pair(BufferOffsetLength(entrySize, nameSize), BufferOffsetLength(entrySize + nameSize, pathSize)));
}

void buildFiles(Output* output, const char* path, const char** files, unsigned int count)
{
	output->print("Building file table...\r");

	std::string targetPath = replaceExtension(path, ".qgf");
	std::string tempPath = targetPath + "_";

	{
		createPathForFile(tempPath.c_str());

		std::ofstream out(tempPath.c_str(), std::ios::out | std::ios::binary);
		if (!out)
		{
			output->error("Error saving data file %s\n", tempPath.c_str());
			return;
		}

		std::pair<std::vector<char>, std::pair<BufferOffsetLength, BufferOffsetLength>> data = prepareFileData(files, count);
		std::vector<char> compressed = compressData(data.first);

		FileFileHeader header;
		memcpy(header.magic, kFileFileHeaderMagic, sizeof(header.magic));

		header.fileCount = count;
		header.compressedSize = compressed.size();
		header.uncompressedSize = data.first.size();

		header.nameBufferOffset = data.second.first.first;
		header.nameBufferLength = data.second.first.second;
		header.pathBufferOffset = data.second.second.first;
		header.pathBufferLength = data.second.second.second;

		out.write(reinterpret_cast<char*>(&header), sizeof(header));
		if (!compressed.empty()) out.write(&compressed[0], compressed.size());
	}

	if (!renameFile(tempPath.c_str(), targetPath.c_str()))
	{
		output->error("Error saving data file %s\n", targetPath.c_str());
		return;
	}
}

void buildFiles(Output* output, const char* path, const std::vector<FileInfo>& files)
{
	std::vector<const char*> filesc(files.size());
	for (size_t i = 0; i < files.size(); ++i) filesc[i] = files[i].path.c_str();

	buildFiles(output, path, filesc.empty() ? NULL : &filesc[0], filesc.size());
}

void buildFiles(Output* output, const char* path)
{
    output->print("Building file table for %s:\n", path);
	output->print("Scanning project...\r");

	std::vector<FileInfo> files;
	if (!getProjectFiles(output, path, files))
	{
		return;
	}

	buildFiles(output, path, files);
}

struct FilesOutput
{
	FilesOutput(Output* output, unsigned int options, unsigned int limit): output(output), options(options), limit(limit)
	{
	}

	Output* output;
	unsigned int options;
	unsigned int limit;
};

static void processMatch(const char* path, size_t pathLength, FilesOutput* output)
{
	if (output->options & SO_VISUALSTUDIO)
	{
		char* buffer = static_cast<char*>(alloca(pathLength));
		
		std::transform(path, path + pathLength, buffer, BackSlashTransformer());
		path = buffer;
	}

	output->output->print("%.*s\n", static_cast<unsigned>(pathLength), path);
}

static void processMatch(const FileFileEntry& entry, const char* data, FilesOutput* output)
{
	const char* path = entry.pathOffset + data;

	const char* pathEnd = strchr(path, '\n');
	assert(pathEnd);

	processMatch(path, pathEnd - path, output);
}

static unsigned int dumpFiles(const FileFileHeader& header, const char* data, FilesOutput* output)
{
	const FileFileEntry* entries = reinterpret_cast<const FileFileEntry*>(data);

	unsigned int count = std::min(output->limit, header.fileCount);

	for (unsigned int i = 0; i < count; ++i)
		processMatch(entries[i], data, output);

	return count;
}

template <typename ExtractOffset, typename ProcessMatch>
static void searchFilesRegex(const FileFileHeader& header, const char* data, const char* buffer, unsigned int bufferSize,
	Regex* re, unsigned int limit, ExtractOffset extractOffset, ProcessMatch processMatch)
{
	const FileFileEntry* entries = reinterpret_cast<const FileFileEntry*>(data);

	const char* range = re->rangePrepare(buffer, bufferSize);

	const char* begin = range;
	const char* end = begin + bufferSize;

	unsigned int matches = 0;

	while (RegexMatch match = re->rangeSearch(begin, end - begin))
	{
		size_t matchOffset = (match.data - range) + (buffer - data);

		// find first file entry with offset > matchOffset
		const FileFileEntry* entry =
			std::upper_bound(entries, entries + header.fileCount, matchOffset, [=](size_t l, const FileFileEntry& r) { return l < extractOffset(r); });

		// find last file entry with offset <= matchOffset
		assert(entry > entries);
		entry--;

		// print match
		processMatch(*entry);

		// move to next line
		const char* lend = findLineEnd(match.data + match.size, end);
		if (lend == end) break;
		begin = lend + 1;
		matches++;

		if (matches >= limit) break;
	}

	re->rangeFinalize(range);
}

template <typename ProcessMatch>
static void searchFilesRegex(const FileFileHeader& header, const char* data, bool matchPaths, Regex* re, unsigned int limit, ProcessMatch processMatch)
{
	if (matchPaths)
		searchFilesRegex(header, data, data + header.pathBufferOffset, header.pathBufferLength, re, limit,
			[](const FileFileEntry& e) { return e.pathOffset; }, processMatch);
	else
		searchFilesRegex(header, data, data + header.nameBufferOffset, header.nameBufferLength, re, limit,
			[](const FileFileEntry& e) { return e.nameOffset; }, processMatch);
}

static bool isPathComponent(const char* str)
{
	return strchr(str, '/') != 0;
}

static unsigned int searchFilesVisualAssist(const FileFileHeader& header, const char* data, const char* string, FilesOutput* output)
{
	std::vector<std::string> fragments = split(string, isspace);

	if (fragments.empty()) return dumpFiles(header, data, output);

	// sort name components first, path components last, larger components first
	std::sort(fragments.begin(), fragments.end(),
		[](const std::string& lhs, const std::string& rhs) -> bool {
			bool lpath = isPathComponent(lhs.c_str());
			bool rpath = isPathComponent(rhs.c_str());
			return (lpath != rpath) ? lpath < rpath : lhs.length() > rhs.length();
		});

	// compile regular expressions
	std::vector<std::unique_ptr<Regex>> res;

	for (auto& f: fragments)
		res.emplace_back(createRegex(f.c_str(), getRegexOptions(output->options | SO_LITERAL)));

	// gather files by first component
	std::vector<const FileFileEntry*> entries;

	searchFilesRegex(header, data, isPathComponent(fragments[0].c_str()), res[0].get(),
		(fragments.size() == 1) ? output->limit : ~0u, [&](const FileFileEntry& e) { entries.push_back(&e); });

	// filter results by subsequent components
	for (size_t i = 1; i < fragments.size(); ++i)
	{
		const char* query = fragments[i].c_str();
		bool queryPath = isPathComponent(query);
		Regex* re = res[i].get();

		entries.erase(std::remove_if(entries.begin(), entries.end(), [&](const FileFileEntry* e) -> bool {
			const char* begin = data + (queryPath ? e->pathOffset : e->nameOffset);
			const char* end = strchr(begin, '\n');
			assert(end);

			return re->search(begin, end - begin).size == 0; }), entries.end());
	}

	// trim results according to limit
	if (entries.size() > output->limit)
		entries.resize(output->limit);

	// output results
	for (auto& e: entries)
		processMatch(*e, data, output);

	return entries.size();
}

static unsigned int searchFilesCommandT(const FileFileHeader& header, const char* data, const char* string, FilesOutput* output)
{
	FuzzyMatcher matcher(string);

	const FileFileEntry* entries = reinterpret_cast<const FileFileEntry*>(data);

	unsigned int matches = 0;

	for (size_t i = 0; i < header.fileCount; ++i)
	{
		const FileFileEntry& e = entries[i];

		const char* path = data + e.pathOffset;
		const char* pathe = strchr(path, '\n');

		if (matcher.match(path, pathe - path))
		{
			matches++;
			processMatch(e, data, output);

			if (matches >= output->limit) break;
		}
	}

	return matches;
}

static void processMatchHighlight(FuzzyMatcher& matcher, const FileFileEntry& entry, const char* data, FilesOutput* output)
{
	const char* path = entry.pathOffset + data;

	const char* pathEnd = strchr(path, '\n');
	assert(pathEnd);

	static std::vector<int> posbuf;

	posbuf.resize(matcher.size());
	matcher.rank(path, pathEnd - path, &posbuf[0]);

	static std::vector<HighlightRange> rbuf;

	rbuf.resize(posbuf.size());
	for (size_t i = 0; i < posbuf.size(); ++i) rbuf[i] = std::make_pair(posbuf[i], 1);

	static std::string result;
	result.clear();

	highlight(result, path, pathEnd - path, &rbuf[0], rbuf.size(), kHighlightMatch);

	processMatch(result.c_str(), result.size(), output);
}

static unsigned int searchFilesCommandTRanked(const FileFileHeader& header, const char* data, const char* string, FilesOutput* output)
{
	FuzzyMatcher matcher(string);

	const FileFileEntry* entries = reinterpret_cast<const FileFileEntry*>(data);

	typedef std::pair<int, const FileFileEntry*> Match;

	std::vector<Match> matches;
	unsigned int perfectMatches = 0;

	for (size_t i = 0; i < header.fileCount; ++i)
	{
		const FileFileEntry& e = entries[i];

		const char* path = data + e.pathOffset;
		const char* pathe = strchr(path, '\n');

		if (matcher.match(path, pathe - path))
		{
            int score = matcher.rank(path, pathe - path);
			assert(score != INT_MAX);

			matches.push_back(std::make_pair(score, &e));

			if (score == 0)
			{
				perfectMatches++;
				if (perfectMatches >= output->limit) break;
			}
		}
	}

	auto compareMatches = [](const Match& l, const Match& r) { return l.first == r.first ? l.second < r.second : l.first < r.first; };

	if (matches.size() <= output->limit)
		std::sort(matches.begin(), matches.end(), compareMatches);
	else
	{
		std::partial_sort(matches.begin(), matches.begin() + output->limit, matches.end(), compareMatches);
		matches.resize(output->limit);
	}

	for (auto& m: matches)
	{
		const FileFileEntry& e = *m.second;

		if (output->options & SO_HIGHLIGHT_MATCHES)
			processMatchHighlight(matcher, e, data, output);
		else
			processMatch(e, data, output);
	}

	return matches.size();
}

unsigned int searchFiles(Output* output_, const char* file, const char* string, unsigned int options, unsigned int limit)
{
	FilesOutput output(output_, options, limit);

	std::string dataPath = replaceExtension(file, ".qgf");
	std::ifstream in(dataPath.c_str(), std::ios::in | std::ios::binary);
	if (!in)
	{
		output_->error("Error reading data file %s\n", dataPath.c_str());
		return 0;
	}
	
	FileFileHeader header;
	if (!read(in, header) || memcmp(header.magic, kFileFileHeaderMagic, strlen(kFileFileHeaderMagic)) != 0)
	{
		output_->error("Error reading data file %s: malformed header\n", dataPath.c_str());
		return 0;
	}

	std::unique_ptr<char[]> buffer(new (std::nothrow) char[header.compressedSize + header.uncompressedSize]);

	if (!buffer || !read(in, buffer.get(), header.compressedSize))
	{
		output_->error("Error reading data file %s: malformed header\n", dataPath.c_str());
		return 0;
	}

	char* data = buffer.get() + header.compressedSize;
	LZ4_uncompress(buffer.get(), data, header.uncompressedSize);

	if (*string == 0)
		return dumpFiles(header, data, &output);
	else if (options & (SO_FILE_NAMEREGEX | SO_FILE_PATHREGEX))
	{
		unsigned int result = 0;

		std::unique_ptr<Regex> re(createRegex(string, getRegexOptions(options)));

		searchFilesRegex(header, data, (options & SO_FILE_PATHREGEX) != 0,
			re.get(), output.limit, [&](const FileFileEntry& e) { processMatch(e, data, &output); result++; });

		return result;
	}
	else if (options & SO_FILE_VISUALASSIST)
		return searchFilesVisualAssist(header, data, string, &output);
	else if (options & SO_FILE_COMMANDT)
		return searchFilesCommandT(header, data, string, &output);
	else if (options & SO_FILE_COMMANDT_RANKED)
		return searchFilesCommandTRanked(header, data, string, &output);
	else
	{
		output_->error("Unknown file search type\n");
		return 0;
	}
}
