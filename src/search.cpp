#include "common.hpp"

#include "format.hpp"
#include "fileutil.hpp"
#include "workqueue.hpp"

#include <fstream>
#include <vector>
#include <algorithm>

#include "re2/re2.h"
#include "lz4/lz4.h"

const size_t kMaxChunksInFlight = 16;
const size_t kMaxChunkSizeAsync = 32 * 1024*1024;

class Regex
{
public:
	Regex(const char* string, unsigned int options): re(0), lowercase(false)
	{
		RE2::Options opts;
		opts.set_literal((options & SO_LITERAL) != 0);
		
		std::string pattern;
		if ((options & SO_IGNORECASE) && transformRegexLower(string, pattern, (options & SO_LITERAL) != 0))
		{
			lowercase = true;
		}
		else
		{
			pattern = string;
			opts.set_case_sensitive((options & SO_IGNORECASE) == 0);
		}
		
		re = new RE2(pattern, opts);
		if (!re->ok()) fatal("Error parsing regular expression %s\n", string);
		
		if (lowercase)
		{
			for (size_t i = 0; i < sizeof(lower); ++i)
			{
				lower[i] = tolower(i);
			}
		}
	}
	
	~Regex()
	{
		delete re;
	}

	const char* prepareRange(const char* data, size_t size)
	{
		if (lowercase)
		{
			char* temp = (char*)malloc(size);
			transformRangeLower(temp, data, data + size);
			return temp;
		}

		return data;
	}

	void finalizeRange(const char* data)
	{
		if (lowercase)
		{
			free(const_cast<char*>(data));
		}
	}
	
	const char* search(const char* begin, const char* end)
	{
		re2::StringPiece p(begin, end - begin);
		
		return RE2::FindAndConsume(&p, *re) ? p.data() : 0;
	}
	
private:
	static bool transformRegexLower(const char* pattern, std::string& res, bool literal)
	{
		res.clear();
		
		// Simple lexer intended to separate literals from non-literals; does not handle Unicode character classes
		// properly, so bail out if we have them
		for (const char* p = pattern; *p; ++p)
		{
			if (*p == '\\' && !literal)
			{
				if (p[1] == 'p' || p[1] == 'P') return false;
				res.push_back(*p);
				p++;
				res.push_back(*p);
			}
			else
			{
				res.push_back(tolower(*p));
			}
		}
		
		return true;
	}
	
	void transformRangeLower(char* dest, const char* begin, const char* end)
	{
		for (const char* i = begin; i != end; ++i)
			*dest++ = lower[static_cast<unsigned char>(*i)];
	}
	
	RE2* re;
	bool lowercase;
	char lower[256];
};

static const char* findLineStart(const char* begin, const char* pos)
{
	for (const char* s = pos; s > begin; --s)
		if (s[-1] == '\n')
			return s;

	return begin;
}

static const char* findLineEnd(const char* pos, const char* end)
{
	for (const char* s = pos; s != end; ++s)
		if (*s == '\n')
			return s;

	return end;
}

static unsigned int countLines(const char* begin, const char* end)
{
	unsigned int res = 0;
	
	for (const char* s = begin; s != end; ++s)
		res += (*s == '\n');
		
	return res;
}

struct BackSlashTransformer
{
	char operator()(char ch) const
	{
		return (ch == '/') ? '\\' : ch;
	}
};

static void processMatch(const char* path, size_t pathLength, unsigned int line, const char* match, size_t matchLength, unsigned int options)
{
	if (matchLength > 0 && match[matchLength - 1] == '\r') matchLength--;
	
	const char* lineBefore = ":";
	const char* lineAfter = ":";
	
	if (options & SO_VISUALSTUDIO)
	{
		char* buffer = static_cast<char*>(alloca(pathLength));
		
		std::transform(path, path + pathLength, buffer, BackSlashTransformer());
		path = buffer;
		
		lineBefore = "(";
		lineAfter = "):";
	}
	
	printf("%.*s%s%d%s %.*s\n", static_cast<unsigned>(pathLength), path, lineBefore, line, lineAfter, static_cast<unsigned>(matchLength), match);
}

static void processFile(Regex* re, const char* path, size_t pathLength, const char* data, size_t size, unsigned int options)
{
	const char* rdata = re->prepareRange(data, size);

	const char* begin = rdata;
	const char* end = begin + size;

	unsigned int line = 0;

	while (const char* match = re->search(begin, end))
	{
		// update line counter
		line += 1 + countLines(begin, match);
		
		// print match
		const char* lbeg = findLineStart(begin, match);
		const char* lend = findLineEnd(match, end);
		processMatch(path, pathLength, line, (lbeg - rdata) + data, lend - lbeg, options);
		
		// move to next line
		if (lend == end) break;
		begin = lend + 1;
	}

	re->finalizeRange(rdata);
}

static void processChunk(Regex* re, const char* data, size_t fileCount, unsigned int options)
{
	const ChunkFileHeader* files = reinterpret_cast<const ChunkFileHeader*>(data);
	
	for (size_t i = 0; i < fileCount; ++i)
	{
		const ChunkFileHeader& f = files[i];
		
		processFile(re, data + f.nameOffset, f.nameLength, data + f.dataOffset, f.dataSize, options);
	}
}

struct ProcessChunk
{
	ProcessChunk(Regex* re, char* compressed, char* data, const ChunkHeader& chunk, unsigned int options): re(re), compressed(compressed), data(data), chunk(chunk), options(options)
	{
	}
	
	void operator()()
	{
		LZ4_uncompress(compressed, data, chunk.uncompressedSize);
		free(compressed);

		processChunk(re, data, chunk.fileCount, options);
		free(data);
	}
	
	Regex* re;
	char* compressed;
	char* data;
	ChunkHeader chunk;
	unsigned int options;
};

bool read(std::istream& in, void* data, size_t size)
{
	in.read(static_cast<char*>(data), size);
	return in.gcount() == size;
}

template <typename T> bool read(std::istream& in, T& value)
{
	return read(in, &value, sizeof(T));
}

void searchProject(const char* file, const char* string, unsigned int options)
{
	Regex regex(string, options);
	
	std::string dataPath = replaceExtension(file, ".qgd");
	std::ifstream in(dataPath.c_str(), std::ios::in | std::ios::binary);
	if (!in)
	{
		error("Error reading data file %s\n", dataPath.c_str());
		return;
	}
	
	FileHeader header;
	if (!read(in, header) || memcmp(header.magic, kFileHeaderMagic, strlen(kFileHeaderMagic)) != 0)
	{
		error("Error reading data file %s: malformed header\n", dataPath.c_str());
		return;
	}
		
	ChunkHeader chunk;
	
	wqBegin(kMaxChunksInFlight);
	
	while (read(in, chunk))
	{
		char* compressed = static_cast<char*>(malloc(chunk.compressedSize));
		char* data = static_cast<char*>(malloc(chunk.uncompressedSize));
		
		if (!compressed || !data || !read(in, compressed, chunk.compressedSize))
		{
			free(compressed);
			free(data);
			error("Error reading data file %s: malformed chunk\n", dataPath.c_str());
			return;
		}
			
		ProcessChunk job(&regex, compressed, data, chunk, options);
		
		if (chunk.compressedSize + chunk.uncompressedSize > kMaxChunkSizeAsync)
		{
			// Huge chunk; to preserve memory process it synchronously
			job();
		}
		else
		{
			// Queue chunk processing
			wqQueue(job);
		}
	}
	
	wqEnd();
}