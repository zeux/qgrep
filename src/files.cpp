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
#include "casefold.hpp"
#include "colors.hpp"

#include "lz4/lz4.h"
#include "lz4/lz4hc.h"

#include <fstream>
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
static void searchFilesRegex(const FileFileHeader& header, const char* data, const char* buffer, unsigned int bufferSize, const char* string,
	unsigned int options, unsigned int limit, ExtractOffset extractOffset, ProcessMatch processMatch)
{
	std::unique_ptr<Regex> re(createRegex(string, getRegexOptions(options)));

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
static void searchFilesRegex(const FileFileHeader& header, const char* data, const char* string, bool matchPaths,
	unsigned int options, unsigned int limit, ProcessMatch processMatch)
{
	if (matchPaths)
		searchFilesRegex(header, data, data + header.pathBufferOffset, header.pathBufferLength, string, options, limit,
			[](const FileFileEntry& e) { return e.pathOffset; }, processMatch);
	else
		searchFilesRegex(header, data, data + header.nameBufferOffset, header.nameBufferLength, string, options, limit,
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

	// force literal searches
	unsigned int options = output->options | SO_LITERAL;

	// gather files by first component
	std::vector<const FileFileEntry*> entries;

	searchFilesRegex(header, data, fragments[0].c_str(), isPathComponent(fragments[0].c_str()),
		options, (fragments.size() == 1) ? output->limit : ~0u, [&](const FileFileEntry& e) { entries.push_back(&e); });

	// filter results by subsequent components
	for (size_t i = 1; i < fragments.size(); ++i)
	{
		const char* query = fragments[i].c_str();
		bool queryPath = isPathComponent(query);

		std::unique_ptr<Regex> re(createRegex(query, getRegexOptions(output->options)));

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

class RankMatcherCommandT
{
public:
	RankMatcherCommandT(const char* query)
	{
		// initialize casefolded query
		cfquery.resize(strlen(query));

		for (size_t i = 0; i < cfquery.size(); ++i)
			cfquery[i] = casefold(query[i]);

		// fill table
		memset(table, 0, sizeof(table));

		for (size_t i = 0; i < cfquery.size(); ++i)
		{
			unsigned char ch = static_cast<unsigned char>(cfquery[i]);

			table[ch] = true;
		}

		// add inverse casefolded letters
		for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); ++i)
			table[i] = table[casefold(i)];
	}

	bool match(const char* data, size_t size)
	{
		const char* pattern = cfquery.c_str();

		const char* begin = data;
		const char* end = data + size;

		while (*pattern)
		{
			while (begin != end && casefold(*begin) != *pattern) begin++;

			if (begin == end) return false;

			begin++;
			pattern++;
		}

		return true;
	}

    int rank(const char* data, size_t size, int* positions = nullptr)
    {
		size_t offset = 0;

		while (offset < size && casefold(data[offset]) != cfquery.front()) offset++;
		while (offset < size && casefold(data[size - 1]) != cfquery.back()) size--;

		if (offset + cfquery.size() > size) return INT_MAX;

		if (buf.size() < size + 1) buf.resize(size + 1);

		std::pair<int, char>* bufp = &buf[0];
		size_t bufsize = 0;

		for (size_t i = offset; i < size; ++i)
		{
			unsigned char ch = static_cast<unsigned char>(data[i]);

			bufp[bufsize] = std::make_pair(i, data[i]);
			bufsize += table[ch];
		}

        cache.clear();
        cache.resize(bufsize * cfquery.size(), INT_MIN);

        RankContext c = {&buf[0], bufsize, cfquery.c_str(), cfquery.size(), &cache[0], nullptr};

		if (positions)
		{
			cachepos.clear();
			cachepos.resize(bufsize * cfquery.size(), -1);
			c.cachepos = &cachepos[0];

			int score = rankRecursive<true>(c, 0, -1, 0);

			if (score != INT_MAX) fillPositions(positions, &buf[0], bufsize, cfquery.size(), &cachepos[0]);

			return score;
		}
		else
            return rankRecursive<false>(c, 0, -1, 0);
	}

	size_t size() const
	{
		return cfquery.size();
	}

private:
	bool table[256];

	std::string cfquery;
    std::vector<std::pair<int, char>> buf;
    std::vector<int> cache;
    std::vector<int> cachepos;

	struct RankContext
	{
        const std::pair<int, char>* path;
		size_t pathLength;
		const char* pattern;
		size_t patternLength;
		int* cache;
		int* cachepos;
	};
	
	template <bool fillPosition>
    static int rankRecursive(const RankContext& c, size_t pathOffset, int lastMatch, size_t patternOffset)
    {
        const std::pair<int, char>* path = c.path;
		size_t pathLength = c.pathLength;
		const char* pattern = c.pattern;
		size_t patternLength = c.patternLength;
		int* cache = c.cache;

		if (pathOffset == pathLength) return 0;

		int& cv = cache[pathOffset * patternLength + patternOffset];

		if (cv != INT_MIN) return cv;

        int bestScore = INT_MAX;
		int bestPos = -1;

		size_t patternRest = patternLength - patternOffset - 1;

        for (size_t i = pathOffset; i + patternRest < pathLength; ++i)
            if (casefold(path[i].second) == pattern[patternOffset])
            {
				int distance = path[i].first - lastMatch;

				int charScore = 0;

				if (distance > 1 && lastMatch >= 0)
				{
					charScore += 10 + (distance - 2);
				}

                int restScore =
                    (patternOffset + 1 < patternLength)
                    ? rankRecursive<fillPosition>(c, i + 1, path[i].first, patternOffset + 1)
                    : 0;

                if (restScore != INT_MAX)
                {
                    int score = charScore + restScore;

                    if (bestScore > score)
					{
						bestScore = score;
						bestPos = i;
					}
                }

                if (patternOffset + 1 < patternLength)
                    ;
                else
					break;
            }

		if (fillPosition) c.cachepos[pathOffset * patternLength + patternOffset] = bestPos;

        return cv = bestScore;
    }

    void fillPositions(int* positions, const std::pair<int, char>* path, size_t pathLength, size_t patternLength, int* cachepos)
	{
		size_t pathOffset = 0;

		for (size_t i = 0; i < patternLength; ++i)
		{
			assert(pathOffset < pathLength);

			int pos = cachepos[pathOffset * patternLength + i];
			assert(pos >= 0 && pos < (int)pathLength);

			positions[i] = path[pos].first;

			pathOffset = pos + 1;
		}
	}
};

static unsigned int searchFilesCommandT(const FileFileHeader& header, const char* data, const char* string, FilesOutput* output)
{
	RankMatcherCommandT matcher(string);

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

static void processMatchHighlight(RankMatcherCommandT& matcher, const FileFileEntry& entry, const char* data, FilesOutput* output)
{
	const char* path = entry.pathOffset + data;

	const char* pathEnd = strchr(path, '\n');
	assert(pathEnd);

	static std::vector<int> posbuf;

	posbuf.resize(matcher.size());
	matcher.rank(path, pathEnd - path, &posbuf[0]);

	static std::string result;
	result.clear();

	size_t posi = 0;

	for (size_t i = 0; path + i != pathEnd; ++i)
	{
		if (posi < posbuf.size() && posbuf[posi] == i)
		{
			posi++;

			result += kColorMatch;
			result += path[i];
			result += kColorEnd;
		}
		else
			result += path[i];
	}

	processMatch(result.c_str(), result.size(), output);
}

static unsigned int searchFilesCommandTRanked(const FileFileHeader& header, const char* data, const char* string, FilesOutput* output)
{
	RankMatcherCommandT matcher(string);

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

		if (output->options & SO_HIGHLIGHT)
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

		searchFilesRegex(header, data, string, (options & SO_FILE_PATHREGEX) != 0,
			output.options, output.limit, [&](const FileFileEntry& e) { processMatch(e, data, &output); result++; });

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
