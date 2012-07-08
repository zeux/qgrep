#include "common.hpp"
#include "highlight.hpp"

#include <algorithm>

static bool isSequenceSorted(const HighlightRange* data, size_t count)
{
    for (size_t i = 1; i < count; ++i)
        if (data[i].first < data[i-1].first)
            return false;

    return true;
}

static size_t mergeSortedRanges(HighlightRange* data, size_t count)
{
    size_t result = 0;

    for (size_t i = 0; i < count; ++i)
    {
		const HighlightRange& cur = data[i];
		HighlightRange& last = result > 0 ? data[result - 1] : data[0];

        if (result > 0 && last.first + last.second >= cur.first)
            last.second = std::max(last.second, cur.first + cur.second - last.first);
        else
            data[result++] = cur;
    }

	return result;
}

void highlight(std::string& result, const char* data, size_t dataSize, HighlightRange* ranges, size_t rangeCount, const char* groupBegin, const char* groupEnd)
{
    if (!isSequenceSorted(ranges, rangeCount))
        std::sort(ranges, ranges + rangeCount, [](const HighlightRange& lhs, const HighlightRange& rhs) { return lhs.first < rhs.first; });

    size_t rangesMerged = mergeSortedRanges(ranges, rangeCount);

    size_t last = 0;

    result.clear();

    for (size_t i = 0; i < rangesMerged; ++i)
    {
        const HighlightRange& r = ranges[i];

        assert(last <= r.first);
        assert(r.first + r.second <= dataSize);

        result.insert(result.end(), data + last, data + r.first);
        result += groupBegin;
        result.insert(result.end(), data + r.first, data + r.first + r.second);
        result += groupEnd;

        last = r.first + r.second;
    }

    result.insert(result.end(), data + last, data + dataSize);
}
