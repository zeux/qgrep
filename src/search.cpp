#include "search.hpp"

#include "common.hpp"
#include "format.hpp"
#include "fileutil.hpp"
#include "workqueue.hpp"
#include "regex.hpp"
#include "orderedoutput.hpp"
#include "constants.hpp"

#include <fstream>
#include <algorithm>
#include <memory>

#include "lz4/lz4.h"

struct SearchOutput
{
	SearchOutput(unsigned int options): options(options), output(kMaxBufferedOutput, kBufferedOutputFlushThreshold)
	{
	}

	unsigned int options;
	OrderedOutput output;
};

struct BackSlashTransformer
{
	char operator()(char ch) const
	{
		return (ch == '/') ? '\\' : ch;
	}
};

static void processMatch(SearchOutput* output, OrderedOutput::Chunk* chunk, const char* path, size_t pathLength, unsigned int line, unsigned int column, const char* match, size_t matchLength)
{
	const char* lineBefore = ":";
	const char* lineAfter = ":";
	
	if (output->options & SO_VISUALSTUDIO)
	{
		char* buffer = static_cast<char*>(alloca(pathLength));
		
		std::transform(path, path + pathLength, buffer, BackSlashTransformer());
		path = buffer;
		
		lineBefore = "(";
		lineAfter = "):";
	}

	char colnumber[16] = "";

	if (output->options & SO_COLUMNNUMBER)
	{
		sprintf(colnumber, "%c%d", (output->options & SO_VISUALSTUDIO) ? ',' : ':', column);
	}
	
	output->output.write(chunk, "%.*s%s%d%s%s %.*s\n", static_cast<unsigned>(pathLength), path, lineBefore, line, colnumber, lineAfter, static_cast<unsigned>(matchLength), match);
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

static void processFile(Regex* re, SearchOutput* output, OrderedOutput::Chunk* chunk, const char* path, size_t pathLength, const char* data, size_t size)
{
	const char* range = re->rangePrepare(data, size);

	const char* begin = range;
	const char* end = begin + size;

	unsigned int line = 0;

	while (RegexMatch match = re->rangeSearch(begin, end - begin))
	{
		// update line counter
		line += 1 + countLines(begin, match.data);
		
		// print match
		const char* lbeg = findLineStart(begin, match.data);
		const char* lend = findLineEnd(match.data + match.size, end);
		processMatch(output, chunk, path, pathLength, line, (match.data - lbeg) + 1, (lbeg - range) + data, lend - lbeg);
		
		// move to next line
		if (lend == end) break;
		begin = lend + 1;
	}

	re->rangeFinalize(range);
}

static void processChunk(Regex* re, SearchOutput* output, unsigned int chunkIndex, const char* data, size_t fileCount)
{
	const ChunkFileHeader* files = reinterpret_cast<const ChunkFileHeader*>(data);

	OrderedOutput::Chunk* chunk = output->output.begin(chunkIndex);
	
	for (size_t i = 0; i < fileCount; ++i)
	{
		const ChunkFileHeader& f = files[i];
		
		processFile(re, output, chunk, data + f.nameOffset, f.nameLength, data + f.dataOffset, f.dataSize);
	}

	output->output.end(chunk);
}

bool read(std::istream& in, void* data, size_t size)
{
	in.read(static_cast<char*>(data), size);
	return in.gcount() == size;
}

template <typename T> bool read(std::istream& in, T& value)
{
	return read(in, &value, sizeof(T));
}

char* safeAlloc(size_t size)
{
	try
	{
		return new char[size];
	}
	catch (const std::bad_alloc&)
	{
		return nullptr;
	}
}

unsigned int getRegexOptions(unsigned int options)
{
	return
		(options & SO_IGNORECASE ? RO_IGNORECASE : 0) |
		(options & SO_LITERAL ? RO_LITERAL : 0);
}

void searchProject(const char* file, const char* string, unsigned int options)
{
	SearchOutput output(options);
	std::unique_ptr<Regex> regex(createRegex(string, getRegexOptions(options)));
	
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
	
	WorkQueue queue(WorkQueue::getIdealWorkerCount(), kMaxQueuedChunkData);
	
	while (read(in, chunk))
	{
		char* compressed = safeAlloc(chunk.compressedSize);
		char* data = safeAlloc(chunk.uncompressedSize);
		
		if (!compressed || !data || !read(in, compressed, chunk.compressedSize))
		{
			delete[] compressed;
			delete[] data;
			error("Error reading data file %s: malformed chunk\n", dataPath.c_str());
			return;
		}
			
		auto job = [=, &regex, &output]() {
			LZ4_uncompress(compressed, data, chunk.uncompressedSize);
			delete[] compressed;

			processChunk(regex.get(), &output, chunkIndex, data, chunk.fileCount);
			delete[] data;
		};

        size_t jobSize = chunk.compressedSize + chunk.uncompressedSize;

		if (jobSize > kMaxChunkSizeAsync)
		{
			// Huge chunk; to preserve memory process it synchronously
			job();
		}
		else
		{
			// Queue chunk processing
			queue.push(job, jobSize);
		}

		chunkIndex++;
	}
}
