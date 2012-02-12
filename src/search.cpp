#include "qrep.hpp"

#include "fileutil.hpp"
#include "workqueue.hpp"

#include <fstream>
#include <vector>
#include <algorithm>

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
	ProcessChunk(Regex* re, char* data, unsigned int fileCount, unsigned int options): re(re), data(data), fileCount(fileCount), options(options)
	{
	}
	
	void operator()()
	{
		processChunk(re, data, fileCount, options);
		free(data);
	}
	
	Regex* re;
	char* data;
	unsigned int fileCount;
	unsigned int options;
};

struct DecompressChunk
{
	DecompressChunk(char* compressed, char* data, unsigned int dataSize): compressed(compressed), data(data), dataSize(dataSize)
	{
	}
	
	void operator()()
	{
		LZ4_uncompress(compressed, data, dataSize);
		free(compressed);
	}
	
	char* compressed;
	char* data;
	unsigned int dataSize;
};

template <typename L, typename R> struct Chain
{
	Chain(const L& l, const R& r): l(l), r(r)
	{
	}
	
	void operator()()
	{
		l();
		wqQueue(r);
	}
	
	L l;
	R r;
};

void searchProject(const char* file, const char* string, unsigned int options)
{
	RE2::Options opts;
	opts.set_case_sensitive((options & SO_IGNORECASE) == 0);
	
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
	
	wqBegin(16);
	
	while (read(in, chunk))
	{
		char* compressed = static_cast<char*>(malloc(chunk.compressedSize));
		char* data = static_cast<char*>(malloc(chunk.uncompressedSize));
		
		if (!compressed || !data || !read(in, compressed, chunk.compressedSize))
			fatal("Error reading data file %s: malformed chunk\n", dataPath.c_str());
			
		DecompressChunk dc(compressed, data, chunk.uncompressedSize);
		ProcessChunk pc(&regex, data, chunk.fileCount, options);
		
		if (chunk.compressedSize + chunk.uncompressedSize > 16*1024*1024)
		{
			// Huge chunk; to preserve memory process it synchronously
			dc();
			pc();
		}
		else
		{
			// Queue chunk processing
			wqQueue(Chain<DecompressChunk, ProcessChunk>(dc, pc));
		}
	}
	
	wqEnd();
}