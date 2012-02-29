#include "common.hpp"

#include "format.hpp"
#include "fileutil.hpp"
#include "workqueue.hpp"
#include "mutex.hpp"
#include "regex.hpp"

#include <fstream>
#include <vector>
#include <map>
#include <algorithm>

#include <assert.h>
#include <stdarg.h>

#include "lz4/lz4.h"

const size_t kMaxChunksInFlight = 16;
const size_t kMaxChunkSizeAsync = 32 * 1024*1024;

struct BackSlashTransformer
{
	char operator()(char ch) const
	{
		return (ch == '/') ? '\\' : ch;
	}
};

static void strprintf(std::string& result, const char* format, ...)
{
	va_list l;
	va_start(l, format);

	int count = _vsnprintf_c(0, 0, format, l);
	assert(count >= 0);

	if (count > 0)
	{
		size_t offset = result.size();
		result.resize(offset + count);
		_vsnprintf(&result[offset], count, format, l);
	}

	va_end(l);
}

class SearchOutput
{
public:
	struct Chunk
	{
		bool ready;
		std::string result;
	};

	SearchOutput(unsigned int options): options(options), current(0)
	{
	}

	Chunk* begin(unsigned int id)
	{
		MutexLock lock(mutex);

		Chunk& chunk = chunks[id];
		
		chunk.ready = false;

		return &chunk;
	}

	void match(Chunk* chunk, const char* path, size_t pathLength, unsigned int line, const char* match, size_t matchLength)
	{
		assert(!chunk->ready);
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
		
		strprintf(chunk->result, "%.*s%s%d%s %.*s\n", static_cast<unsigned>(pathLength), path, lineBefore, line, lineAfter, static_cast<unsigned>(matchLength), match);
	}

	void end(Chunk* chunk)
	{
		chunk->ready = true;

		MutexLock lock(mutex);

		while (!chunks.empty() && chunks.begin()->first == current && chunks.begin()->second.ready)
		{
			Chunk& chunk = chunks.begin()->second;
			if (!chunk.result.empty()) printf("%s", chunk.result.c_str());
			chunks.erase(chunks.begin());
			current++;
		}
	}

private:
	unsigned int options;

	Mutex mutex;
	unsigned int current;
	std::map<unsigned int, Chunk> chunks;
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

static void processFile(Regex* re, SearchOutput* output, SearchOutput::Chunk* chunk, const char* path, size_t pathLength, const char* data, size_t size)
{
	const char* range = re->rangePrepare(data, size);

	const char* begin = range;
	const char* end = begin + size;

	unsigned int line = 0;

	while (const char* match = re->rangeSearch(begin, end - begin))
	{
		// update line counter
		line += 1 + countLines(begin, match);
		
		// print match
		const char* lbeg = findLineStart(begin, match);
		const char* lend = findLineEnd(match, end);
		output->match(chunk, path, pathLength, line, (lbeg - range) + data, lend - lbeg);
		
		// move to next line
		if (lend == end) break;
		begin = lend + 1;
	}

	re->rangeFinalize(range);
}

static void processChunk(Regex* re, SearchOutput* output, unsigned int chunkIndex, const char* data, size_t fileCount)
{
	const ChunkFileHeader* files = reinterpret_cast<const ChunkFileHeader*>(data);

	SearchOutput::Chunk* chunk = output->begin(chunkIndex);
	
	for (size_t i = 0; i < fileCount; ++i)
	{
		const ChunkFileHeader& f = files[i];
		
		processFile(re, output, chunk, data + f.nameOffset, f.nameLength, data + f.dataOffset, f.dataSize);
	}

	output->end(chunk);
}

struct ProcessChunk
{
	ProcessChunk(Regex* re, SearchOutput* output, char* compressed, char* data, const ChunkHeader& chunk, unsigned int chunkIndex):
		re(re), output(output), compressed(compressed), data(data), chunk(chunk), chunkIndex(chunkIndex)
	{
	}
	
	void operator()()
	{
		LZ4_uncompress(compressed, data, chunk.uncompressedSize);
		free(compressed);

		processChunk(re, output, chunkIndex, data, chunk.fileCount);
		free(data);
	}
	
	Regex* re;
	SearchOutput* output;
	char* compressed;
	char* data;
	ChunkHeader chunk;
	unsigned int chunkIndex;
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
	SearchOutput output(options);
	std::auto_ptr<Regex> regex(createRegex(string, options));
	
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
	unsigned int chunkIndex = 0;
	
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
			
		ProcessChunk job(regex.get(), &output, compressed, data, chunk, chunkIndex++);
		
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
