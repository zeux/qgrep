#include "common.hpp"
#include "filterutil.hpp"

#include "filter.hpp"
#include "output.hpp"
#include "search.hpp"

#include <vector>

#include <stdio.h>

#include "re2/util/stringops.h"

unsigned int filterBuffer(Output* output, const char* string, unsigned int options, unsigned int limit, const char* buffer, size_t bufferSize)
{
	if (bufferSize == 0) return 0;

	std::vector<FilterEntry> data;

	for (size_t i = 0; i < bufferSize; )
	{
		const void* nextptr = re2::memchr(buffer + i, '\n', bufferSize - i);
		size_t next = nextptr ? static_cast<const char*>(nextptr) - buffer : bufferSize;

		if (i < next)
		{
			FilterEntry e = {i, next - i};
			data.push_back(e);
		}

		i = next + 1;
	}

	FilterEntries entries;

	entries.buffer = buffer;
	entries.bufferSize = bufferSize;

	entries.entries = data.empty() ? nullptr : &data[0];
	entries.entryCount = data.size();

	return filter(output, string, options, limit, entries);
}

static std::pair<unsigned int, size_t> filterBufferPartial(Output* output, const char* string, unsigned int options, unsigned int limit,
	const char* buffer, size_t bufferSize, size_t bufferOffset)
{
	// search for last full line; limit the search to bufferOffset to avoid quadratic behaviour on repeated calls for extremely long lines
	size_t end = bufferSize;
	while (end > bufferOffset && buffer[end - 1] != '\n') end--;

	if (end == bufferOffset) return std::make_pair(0, 0);

	unsigned int result = filterBuffer(output, string, options, limit, buffer, end);

	return std::make_pair(result, end);
}

unsigned int filterStdin(Output* output, const char* string, unsigned int options, unsigned int limit)
{
	const size_t chunkSize = 1048576;
	bool allowPartialInput = (options & SO_FILE_COMMANDT_RANKED) == 0;

	std::vector<char> buffer;

	unsigned int matches = 0;

	while (true)
	{
		size_t offset = buffer.size();
		buffer.resize(offset + chunkSize);

		size_t readsize = fread(&buffer[offset], 1, chunkSize, stdin);
		buffer.resize(offset + readsize);

		if (readsize == 0)
		{
			// end-of-input
			matches += filterBuffer(output, string, options, limit, buffer.empty() ? nullptr : &buffer[0], buffer.size());
			break;
		}
		else if (allowPartialInput)
		{
			auto p = filterBufferPartial(output, string, options, limit, buffer.empty() ? nullptr : &buffer[0], buffer.size(), offset);

			assert(p.first <= limit);
			limit -= p.first;
			matches += p.first;

			assert(p.second <= buffer.size());
			buffer.erase(buffer.begin(), buffer.begin() + p.second);
		}
	}

	return matches;
}