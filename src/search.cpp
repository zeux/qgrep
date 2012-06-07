#include "search.hpp"

#include "output.hpp"
#include "format.hpp"
#include "fileutil.hpp"
#include "workqueue.hpp"
#include "regex.hpp"
#include "orderedoutput.hpp"
#include "constants.hpp"
#include "blockpool.hpp"
#include "stringutil.hpp"
#include "bloom.hpp"
#include "casefold.hpp"

#include <fstream>
#include <algorithm>
#include <memory>

#include "lz4/lz4.h"
#include "re2/prefilter.h"
#include "re2/prefilter_tree.h"

struct SearchOutput
{
	SearchOutput(Output* output, unsigned int options): options(options), output(output, kMaxBufferedOutput, kBufferedOutputFlushThreshold)
	{
	}

	unsigned int options;
	OrderedOutput output;
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

static void processFile(Regex* re, SearchOutput* output, OrderedOutput::Chunk* chunk, const char* path, size_t pathLength, const char* data, size_t size, unsigned int startLine)
{
	const char* range = re->rangePrepare(data, size);

	const char* begin = range;
	const char* end = begin + size;

	unsigned int line = startLine;

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
	const DataChunkFileHeader* files = reinterpret_cast<const DataChunkFileHeader*>(data);

	OrderedOutput::Chunk* chunk = output->output.begin(chunkIndex);
	
	for (size_t i = 0; i < fileCount; ++i)
	{
		const DataChunkFileHeader& f = files[i];
		
		processFile(re, output, chunk, data + f.nameOffset, f.nameLength, data + f.dataOffset, f.dataSize, f.startLine);
	}

	output->output.end(chunk);
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

std::shared_ptr<char> safeAlloc(BlockPool& pool, size_t size)
{
	try
	{
		return pool.allocate(size);
	}
	catch (const std::bad_alloc&)
	{
		return std::shared_ptr<char>();
	}
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
	NgramRegex(Regex* re)
	{
		if (!re) return;

		re2::RE2* r = static_cast<RE2*>(re->getRegexObject());

		std::vector<std::string> atomstr;

		tree.Add(re2::Prefilter::FromRE2(r));
		tree.Compile(&atomstr);

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

		std::vector<int> res;
		tree.RegexpsGivenStrings(matched, &res);

		assert(res.size() <= 1);
		return !res.empty();
	}

	bool empty() const
	{
		return atoms.empty();
	}

private:
	std::vector<NgramString> atoms;
	re2::PrefilterTree tree;
};

void searchProject(Output* output_, const char* file, const char* string, unsigned int options, unsigned int limit)
{
	SearchOutput output(output_, options);
	std::unique_ptr<Regex> regex(createRegex(string, getRegexOptions(options)));
	NgramRegex ngregex((options & SO_BRUTEFORCE) ? nullptr : regex.get());
	
	std::string dataPath = replaceExtension(file, ".qgd");
	std::ifstream in(dataPath.c_str(), std::ios::in | std::ios::binary);
	if (!in)
	{
		output_->error("Error reading data file %s\n", dataPath.c_str());
		return;
	}
	
	DataFileHeader header;
	if (!read(in, header) || memcmp(header.magic, kDataFileHeaderMagic, strlen(kDataFileHeaderMagic)) != 0)
	{
		output_->error("Error reading data file %s: malformed header\n", dataPath.c_str());
		return;
	}
		
	DataChunkHeader chunk;
	unsigned int chunkIndex = 0;
	
	// Assume 50% compression ratio (it's usually much better)
	BlockPool chunkPool(kChunkSize * 3 / 2);
	std::vector<unsigned char> index;
	WorkQueue queue(WorkQueue::getIdealWorkerCount(), kMaxQueuedChunkData);
	
	while (read(in, chunk))
	{
		if (ngregex.empty() || chunk.indexSize == 0)
		{
			in.seekg(chunk.indexSize, std::ios::cur);
		}
		else
		{
			try
			{
				index.resize(chunk.indexSize);
			}
			catch (const std::bad_alloc&)
			{
				output_->error("Error reading data file %s: malformed chunk\n", dataPath.c_str());
				return;
			}

			if (chunk.indexSize && !read(in, &index[0], chunk.indexSize))
			{
				output_->error("Error reading data file %s: malformed chunk\n", dataPath.c_str());
				return;
			}

			if (!ngregex.match(index, chunk.indexHashIterations))
			{
				in.seekg(chunk.compressedSize, std::ios::cur);
				continue;
			}
		}

		std::shared_ptr<char> data = safeAlloc(chunkPool, chunk.compressedSize + chunk.uncompressedSize);
		
		if (!data || !read(in, data.get(), chunk.compressedSize))
		{
			output_->error("Error reading data file %s: malformed chunk\n", dataPath.c_str());
			return;
		}
			
		queue.push([=, &regex, &output]() {
			char* compressed = data.get();
			char* uncompressed = data.get() + chunk.compressedSize;

			LZ4_uncompress(compressed, uncompressed, chunk.uncompressedSize);
			processChunk(regex.get(), &output, chunkIndex, uncompressed, chunk.fileCount);
		}, chunk.compressedSize + chunk.uncompressedSize);

		chunkIndex++;
	}
}