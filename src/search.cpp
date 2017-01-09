#include "common.hpp"
#include "search.hpp"

#include "output.hpp"
#include "format.hpp"
#include "fileutil.hpp"
#include "filestream.hpp"
#include "workqueue.hpp"
#include "regex.hpp"
#include "orderedoutput.hpp"
#include "constants.hpp"
#include "blockpool.hpp"
#include "stringutil.hpp"
#include "bloom.hpp"
#include "casefold.hpp"
#include "highlight.hpp"
#include "compression.hpp"

#include <algorithm>
#include <memory>
#include <fstream>

struct SearchOutput
{
	SearchOutput(Output* output, unsigned int options, unsigned int limit): options(options), limit(limit), output(output, kMaxBufferedOutput, kBufferedOutputFlushThreshold, limit)
	{
	}

	bool isLimitReached(OrderedOutput::Chunk* chunk = nullptr) const
	{
		return (chunk && chunk->lines >= limit) || output.getLineCount() >= limit;
	}

	unsigned int options;
	unsigned int limit;
	OrderedOutput output;
};

struct HighlightBuffer
{
	std::vector<HighlightRange> ranges;
};

static char* printString(char* dest, const char* src)
{
	while (*src) *dest++ = *src++;
	return dest;
}

static char* printNumber(char* dest, unsigned int value)
{
	char buf[32];

	char* end = buf + sizeof(buf);
	*--end = 0;

	do
	{
		*--end = '0' + (value % 10);
		value /= 10;
	}
	while (value > 0);

	return printString(dest, end);
}

static size_t printMatchLineColumn(unsigned int line, unsigned int column, unsigned int options, char (&buf)[256])
{
	char* pos = buf;

	const char* sepbeg = (options & SO_VISUALSTUDIO) ? "(" : ":";
	const char* sepmid = (options & SO_VISUALSTUDIO) ? "," : ":";
	const char* sepend = (options & SO_VISUALSTUDIO) ? "):" : ":";

	if (options & SO_HIGHLIGHT) pos = printString(pos, kHighlightSeparator);
	pos = printString(pos, sepbeg);

	if (options & SO_HIGHLIGHT) pos = printString(pos, kHighlightNumber);
	pos = printNumber(pos, line);

	if (options & SO_COLUMNNUMBER)
	{
		if (options & SO_HIGHLIGHT) pos = printString(pos, kHighlightSeparator);
		pos = printString(pos, sepmid);

		if (options & SO_HIGHLIGHT) pos = printString(pos, kHighlightNumber);
		pos = printNumber(pos, column);
	}

	if (options & SO_HIGHLIGHT) pos = printString(pos, kHighlightSeparator);
	pos = printString(pos, sepend);

	assert(pos <= buf + sizeof(buf));
	return pos - buf;
}

static void printHighlightMatch(std::string& result, Regex* re, HighlightBuffer& hlbuf, const char* line, size_t lineLength, const char* preparedRange, size_t matchOffset, size_t matchLength)
{
	hlbuf.ranges.clear();
	hlbuf.ranges.push_back(HighlightRange(matchOffset, matchLength));
	highlightRegex(hlbuf.ranges, re, line, lineLength, preparedRange, matchOffset + matchLength);

	highlight(result, line, lineLength, hlbuf.ranges.empty() ? nullptr : &hlbuf.ranges[0], hlbuf.ranges.size(), kHighlightMatch);
}

static void processMatch(Regex* re, SearchOutput* output, OrderedOutput::Chunk* chunk, HighlightBuffer& hlbuf,
	const char* path, size_t pathLength, const char* line, size_t lineLength, unsigned int lineNumber,
	const char* preparedRange, size_t matchOffset, size_t matchLength)
{
	if (output->options & SO_VISUALSTUDIO)
	{
		char* buffer = static_cast<char*>(alloca(pathLength));
		
		std::transform(path, path + pathLength, buffer, BackSlashTransformer());
		path = buffer;
	}

	char linecolumn[256];
	size_t linecolumnsize = printMatchLineColumn(lineNumber, matchOffset + 1, output->options, linecolumn);

	if (output->options & SO_HIGHLIGHT) chunk->result += kHighlightPath;
	chunk->result.append(path, pathLength);
	chunk->result.append(linecolumn, linecolumnsize);
	if (output->options & SO_HIGHLIGHT) chunk->result += kHighlightEnd;

	if (output->options & SO_HIGHLIGHT_MATCHES)
		printHighlightMatch(chunk->result, re, hlbuf, line, lineLength, preparedRange, matchOffset, matchLength);
	else
		chunk->result.append(line, lineLength);

	chunk->result += '\n';

	output->output.write(chunk);
}

static void processFile(Regex* re, SearchOutput* output, OrderedOutput::Chunk* chunk, HighlightBuffer& hlbuf,
	const char* path, size_t pathLength, const char* data, size_t size, unsigned int startLine)
{
	const char* range = re->rangePrepare(data, size);

	const char* begin = range;
	const char* end = begin + size;

	unsigned int line = startLine;

	while (RegexMatch match = re->rangeSearch(begin, end - begin))
	{
		// discard zero-length matches at the end (.* results in an extra line for every file part otherwise)
		if (match.data == end) break;

		// update line counter
		line += 1 + countLines(begin, match.data);
		
		// print match
		const char* lbeg = findLineStart(begin, match.data);
		const char* lend = findLineEnd(match.data + match.size, end);
		processMatch(re, output, chunk, hlbuf, path, pathLength, (lbeg - range) + data, lend - lbeg, line, lbeg, match.data - lbeg, match.size);
		
		// early-out for big matches
		if (output->isLimitReached(chunk)) break;

		// move to next line
		if (lend == end) break;
		begin = lend + 1;
	}

	re->rangeFinalize(range);
}

static void processChangedFile(Regex* re, SearchOutput* output, OrderedOutput::Chunk* chunk, HighlightBuffer& hlbuf, const std::string& path, Regex* includeRe, Regex* excludeRe)
{
	if (includeRe && !includeRe->search(path.c_str(), path.size()))
		return;

	if (excludeRe && excludeRe->search(path.c_str(), path.size()))
		return;

	std::unique_ptr<FILE, int(*)(FILE*)> file(openFile(path.c_str(), "rb"), fclose);
	if (!file)
		return;

	fseek(file.get(), 0, SEEK_END);
	size_t length = ftell(file.get());
	fseek(file.get(), 0, SEEK_SET);

	std::unique_ptr<char[]> data(new (std::nothrow) char[length]);
	if (!data)
		return;

	if (fread(data.get(), 1, length, file.get()) != length)
		return;

	if (ferror(file.get()) != 0)
		return;

	processFile(re, output, chunk, hlbuf, path.c_str(), path.size(), data.get(), length, 0);
}

static void processChunk(Regex* re, SearchOutput* output, unsigned int chunkIndex, const char* data, size_t fileCount, Regex* includeRe, Regex* excludeRe, const std::string* changes, size_t changeCount)
{
	const DataChunkFileHeader* files = reinterpret_cast<const DataChunkFileHeader*>(data);

	OrderedOutput::Chunk* chunk = output->output.begin(chunkIndex);

	HighlightBuffer hlbuf;

	size_t changeIndex = 0;

	for (size_t i = 0; i < fileCount; ++i)
	{
		// early-out for big matches
		if (output->isLimitReached(chunk))
			break;

		const DataChunkFileHeader& f = files[i];

		while (changeIndex < changeCount && changes[changeIndex].compare(0, changes[changeIndex].size(), data + f.nameOffset, f.nameLength) < 0)
		{
			processChangedFile(re, output, chunk, hlbuf, changes[changeIndex], includeRe, excludeRe);
			changeIndex++;
		}

		if (changeIndex < changeCount && changes[changeIndex].compare(0, changes[changeIndex].size(), data + f.nameOffset, f.nameLength) == 0)
		{
			processChangedFile(re, output, chunk, hlbuf, changes[changeIndex], includeRe, excludeRe);
			changeIndex++;
		}
		else
		{
			if (includeRe && !includeRe->search(data + f.nameOffset, f.nameLength))
				continue;

			if (excludeRe && excludeRe->search(data + f.nameOffset, f.nameLength))
				continue;

			processFile(re, output, chunk, hlbuf, data + f.nameOffset, f.nameLength, data + f.dataOffset, f.dataSize, f.startLine);
		}
	}

	while (changeIndex < changeCount)
	{
		processChangedFile(re, output, chunk, hlbuf, changes[changeIndex], includeRe, excludeRe);
		changeIndex++;
	}

	output->output.end(chunk);
}

unsigned int getRegexOptions(unsigned int options)
{
	return
		(options & SO_IGNORECASE ? RO_IGNORECASE : 0) |
		(options & SO_LITERAL ? RO_LITERAL : 0);
}

typedef std::vector<unsigned int> NgramString;

NgramString ngramExtract(const std::string& string)
{
	NgramString result;

	for (size_t i = 3; i < string.length(); ++i)
	{
		char a = string[i - 3], b = string[i - 2], c = string[i - 1], d = string[i];
		unsigned int n = ngram(casefold(a), casefold(b), casefold(c), casefold(d));
		result.push_back(n);
	}

	return result;
}

bool ngramExists(const std::vector<unsigned char>& index, unsigned int iterations, const NgramString& search)
{
	for (size_t i = 0; i < search.size(); ++i)
		if (!bloomFilterExists(&index[0], index.size(), search[i], iterations))
			return false;

	return true;
}

class NgramRegex
{
public:
	NgramRegex(Regex* re): re(re)
	{
		if (!re) return;

		std::vector<std::string> atomstr = re->prefilterPrepare();

		for (size_t i = 0; i < atomstr.size(); ++i)
			atoms.push_back(ngramExtract(atomstr[i]));
	}

	bool match(const std::vector<unsigned char>& index, unsigned int iterations) const
	{
		if (atoms.empty()) return true;

		std::vector<int> matched;

		for (size_t i = 0; i < atoms.size(); ++i)
			if (ngramExists(index, iterations, atoms[i]))
				matched.push_back(i);

		return re->prefilterMatch(matched);
	}

	bool empty() const
	{
		return atoms.empty();
	}

private:
	std::vector<NgramString> atoms;
	Regex* re;
};

std::vector<std::string> readChanges(const char* path)
{
	std::string filePath = replaceExtension(path, ".qgc");

	std::vector<std::string> result;
	std::string line;

	std::ifstream in(filePath.c_str(), std::ios::in | std::ios::binary);

	while (std::getline(in, line))
		result.push_back(line);

	return result;
}

size_t scanChanges(const std::vector<std::string>& changes, size_t changeIt, const char* data, size_t size)
{
	while (changeIt < changes.size() && changes[changeIt].compare(0, changes[changeIt].size(), data, size) <= 0)
		changeIt++;

	return changeIt;
}

template <typename T> static bool readVector(FileStream& in, std::vector<T>& data, size_t size)
{
	try
	{
		data.resize(size);
	}
	catch (const std::bad_alloc&)
	{
		return false;
	}

	if (size && !read(in, &data[0], size * sizeof(T)))
	{
		return false;
	}

	return true;
}

unsigned int searchProject(Output* output_, const char* file, const char* string, unsigned int options, unsigned int limit, const char* include, const char* exclude)
{
	SearchOutput output(output_, options, limit);
	std::unique_ptr<Regex> regex(createRegex(string, getRegexOptions(options)));
	std::unique_ptr<Regex> includeRe(include ? createRegex(include, RO_IGNORECASE) : 0);
	std::unique_ptr<Regex> excludeRe(exclude ? createRegex(exclude, RO_IGNORECASE) : 0);
	NgramRegex ngregex((options & SO_BRUTEFORCE) ? nullptr : regex.get());

	std::vector<std::string> changes = readChanges(file);
	size_t changeIt = 0;
	
	std::string dataPath = replaceExtension(file, ".qgd");
	FileStream in(dataPath.c_str(), "rb");
	if (!in)
	{
		output_->error("Error reading data file %s\n", dataPath.c_str());
		return 0;
	}
	
	DataFileHeader header;
	if (!read(in, header) || memcmp(header.magic, kDataFileHeaderMagic, strlen(kDataFileHeaderMagic)) != 0)
	{
		output_->error("Error reading data file %s: file format is out of date, update the project to fix\n", dataPath.c_str());
		return 0;
	}

	{
		unsigned int chunkIndex = 0;

		// Assume 50% compression ratio (it's usually much better)
		BlockPool chunkPool(kChunkSize * 3 / 2);

		std::vector<char> extra;
		std::vector<unsigned char> index;
		DataChunkHeader chunk;

		WorkQueue queue(WorkQueue::getIdealWorkerCount(), kMaxQueuedChunkData);

		while (!output.isLimitReached() && read(in, chunk))
		{
			if (!readVector(in, extra, chunk.extraSize))
			{
				output_->error("Error reading data file %s: malformed chunk\n", dataPath.c_str());
				return 0;
			}

			if (ngregex.empty() || chunk.indexSize == 0)
			{
				in.skip(chunk.indexSize);
			}
			else
			{
				if (!readVector(in, index, chunk.indexSize))
				{
					output_->error("Error reading data file %s: malformed chunk\n", dataPath.c_str());
					return 0;
				}

				if (!ngregex.match(index, chunk.indexHashIterations))
				{
					in.skip(chunk.compressedSize);
					continue;
				}
			}

			std::shared_ptr<char> data = chunkPool.allocate(chunk.compressedSize + chunk.uncompressedSize, std::nothrow);

			if (!data || !read(in, data.get(), chunk.compressedSize))
			{
				output_->error("Error reading data file %s: malformed chunk\n", dataPath.c_str());
				return 0;
			}

			size_t changeNext = scanChanges(changes, changeIt, extra.data(), extra.size());

			queue.push([=, &regex, &output, &includeRe, &excludeRe]() {
				char* compressed = data.get();
				char* uncompressed = data.get() + chunk.compressedSize;

				decompress(uncompressed, chunk.uncompressedSize, compressed, chunk.compressedSize);
				processChunk(regex.get(), &output, chunkIndex, uncompressed, chunk.fileCount, includeRe.get(), excludeRe.get(), changes.data() + changeIt, changeNext - changeIt);
			}, chunk.compressedSize + chunk.uncompressedSize);

			chunkIndex++;
			changeIt = changeNext;
		}

		if (changeIt < changes.size())
		{
			OrderedOutput::Chunk* chunk = output.output.begin(chunkIndex);

			HighlightBuffer hlbuf;

			while (changeIt < changes.size())
			{
				processChangedFile(regex.get(), &output, chunk, hlbuf, changes[changeIt], includeRe.get(), excludeRe.get());
				changeIt++;
			}

			output.output.end(chunk);
		}
	}

	return output.output.getLineCount();
}
