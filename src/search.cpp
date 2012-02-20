#include "common.hpp"

#include "format.hpp"
#include "fileutil.hpp"
#include "workqueue.hpp"

#include <fstream>
#include <vector>
#include <algorithm>

#include "re2/re2.h"
#include "lz4/lz4.h"

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
	
	const char* search(const char* begin, const char* end)
	{
		if (lowercase && begin != end)
		{
			size_t size = end - begin;
			char* temp = (char*)malloc(size);
			
			transformRangeLower(temp, begin, end);
			
			const char* result = searchRaw(temp, temp + size);
			
			free(temp);
			
			return result ? (result - temp) + begin : 0;
		}
		else
			return searchRaw(begin, end);
	}
	
private:
	const char* searchRaw(const char* begin, const char* end)
	{
		re2::StringPiece p(begin, end - begin);
		
		return RE2::FindAndConsume(&p, *re) ? p.data() : 0;
	}
	
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

static void processMatch(const char* pathBegin, const char* pathEnd, unsigned int line, const char* begin, const char* end, unsigned int options)
{
	if (begin < end && end[-1] == '\r') --end;
	
	const char* lineBefore = ":";
	const char* lineAfter = ":";
	
	if (options & SO_VISUALSTUDIO)
	{
		char* pathBuffer = static_cast<char*>(alloca(pathEnd - pathBegin));
		
		pathEnd = std::transform(pathBegin, pathEnd, pathBuffer, BackSlashTransformer());
		pathBegin = pathBuffer;
		
		lineBefore = "(";
		lineAfter = "):";
	}
	
	printf("%.*s%s%d%s %.*s\n", pathEnd - pathBegin, pathBegin, lineBefore, line, lineAfter, end - begin, begin);
}

static void processFile(Regex* re, const char* pathBegin, const char* pathEnd, const char* begin, const char* end, unsigned int options)
{
	unsigned int line = 0;
	const char* match;
	
	while ((match = re->search(begin, end)) != 0)
	{
		// update line counter
		line += 1 + countLines(begin, match);
		
		// print match
		const char* lbeg = findLineStart(begin, match);
		const char* lend = findLineEnd(match, end);
		processMatch(pathBegin, pathEnd, line, lbeg, lend, options);
		
		// move to next line
		if (lend == end) return;
		begin = lend + 1;
	}
}

static void processChunk(Regex* re, const char* data, size_t fileCount, unsigned int options)
{
	const ChunkFileHeader* files = reinterpret_cast<const ChunkFileHeader*>(data);
	
	for (size_t i = 0; i < fileCount; ++i)
	{
		const ChunkFileHeader& f = files[i];
		
		processFile(re, data + f.nameOffset, data + f.nameOffset + f.nameLength, data + f.dataOffset, data + f.dataOffset + f.dataSize, options);
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
		fatal("Error reading data file %s: malformed header\n", dataPath.c_str());
		
	ChunkHeader chunk;
	
	wqBegin(16);
	
	while (read(in, chunk))
	{
		char* compressed = static_cast<char*>(malloc(chunk.compressedSize));
		char* data = static_cast<char*>(malloc(chunk.uncompressedSize));
		
		if (!compressed || !data || !read(in, compressed, chunk.compressedSize))
			fatal("Error reading data file %s: malformed chunk\n", dataPath.c_str());
			
		ProcessChunk job(&regex, compressed, data, chunk, options);
		
		if (chunk.compressedSize + chunk.uncompressedSize > 16*1024*1024)
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