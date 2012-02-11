#include "qrep.hpp"

#include "fileutil.hpp"

#include <fstream>
#include <vector>

#include "re2/re2.h"
#include "lz4/lz4.h"

struct Regex
{
	virtual ~Regex() {}
	
	virtual const char* search(const char* begin, const char* end) = 0;
};

struct RE2Regex: Regex
{
	RE2Regex(RE2* re): re(re)
	{
	}
	
	virtual const char* search(const char* begin, const char* end)
	{
		re2::StringPiece p(begin, end - begin);
		
		return RE2::FindAndConsume(&p, *re) ? p.data() : 0;
	}
	
	RE2* re;
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

static void processMatch(const char* pathBegin, const char* pathEnd, unsigned int line, const char* begin, const char* end)
{
	printf("%.*s:%d: %.*s\n", pathEnd - pathBegin, pathBegin, line, end - begin, begin);
}

static void processFile(Regex* re, const char* pathBegin, const char* pathEnd, const char* begin, const char* end)
{
	unsigned int line = 1;
	const char* match;
	
	while ((match = re->search(begin, end)) != 0)
	{
		// update line counter
		line += countLines(begin, match);
		
		// print match
		const char* lbeg = findLineStart(begin, match);
		const char* lend = findLineEnd(match, end);
		processMatch(pathBegin, pathEnd, line, lbeg, lend);
		
		// move to next line
		if (lend == end) return;
		begin = lend + 1;
	}
}

static void processChunk(Regex* re, const char* data, size_t fileCount)
{
	const ChunkFileHeader* files = reinterpret_cast<const ChunkFileHeader*>(data);
	
	for (size_t i = 0; i < fileCount; ++i)
	{
		const ChunkFileHeader& f = files[i];
		
		processFile(re, data + f.nameOffset, data + f.nameOffset + f.nameLength, data + f.dataOffset, data + f.dataOffset + f.dataSize);
	}
}

void searchProject(const char* file, const char* string)
{
	RE2::Options opts;
	RE2 re(string, opts);
	if (!re.ok()) fatal("Error parsing regular expression %s\n", string);
	RE2Regex regex(&re);
	
	std::string dataPath = replaceExtension(file, ".qgd");
	std::ifstream in(dataPath.c_str(), std::ios::in | std::ios::binary);
	if (!in)
		fatal("Error reading data file %s\n", dataPath.c_str());
	
	FileHeader header;
	if (!read(in, header) || memcmp(header.magic, kFileHeaderMagic, strlen(kFileHeaderMagic)) != 0)
		fatal("Error reading data file %s: malformed header\n", dataPath.c_str());
		
	ChunkHeader chunk;
	
	while (read(in, chunk))
	{
		char* compressed = static_cast<char*>(malloc(chunk.compressedSize));
		char* data = static_cast<char*>(malloc(chunk.uncompressedSize));
		
		if (!compressed || !data || !read(in, compressed, chunk.compressedSize))
			fatal("Error reading data file %s: malformed chunk\n", dataPath.c_str());
			
		LZ4_uncompress(compressed, data, chunk.uncompressedSize);
		free(compressed);
		
		processChunk(&regex, data, chunk.fileCount);
		free(data);
	}
}