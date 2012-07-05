#include "common.hpp"
#include "fuzzymatch.hpp"

#include "casefold.hpp"

#include <string.h>
#include <limits.h>

struct RankContext
{
    const std::pair<int, char>* path;
    size_t pathLength;
    const char* pattern;
    size_t patternLength;
    int* cache;
    int* cachepos;
};
	
template <bool fillPosition>
static int rankRecursive(const RankContext& c, size_t pathOffset, int lastMatch, size_t patternOffset)
{
    const std::pair<int, char>* path = c.path;
    size_t pathLength = c.pathLength;
    const char* pattern = c.pattern;
    size_t patternLength = c.patternLength;
    int* cache = c.cache;

    if (pathOffset == pathLength) return 0;

    int& cv = cache[pathOffset * patternLength + patternOffset];

    if (cv != INT_MIN) return cv;

    int bestScore = INT_MAX;
    int bestPos = -1;

    size_t patternRest = patternLength - patternOffset - 1;

    for (size_t i = pathOffset; i + patternRest < pathLength; ++i)
        if (casefold(path[i].second) == pattern[patternOffset])
        {
            int distance = path[i].first - lastMatch;

            int charScore = 0;

            if (distance > 1 && lastMatch >= 0)
            {
                charScore += 10 + (distance - 2);
            }

            int restScore =
                (patternOffset + 1 < patternLength)
                ? rankRecursive<fillPosition>(c, i + 1, path[i].first, patternOffset + 1)
                : 0;

            if (restScore != INT_MAX)
            {
                int score = charScore + restScore;

                if (bestScore > score)
                {
                    bestScore = score;
                    bestPos = i;
                }
            }

            if (patternOffset + 1 < patternLength)
                ;
            else
                break;
        }

    if (fillPosition) c.cachepos[pathOffset * patternLength + patternOffset] = bestPos;

    return cv = bestScore;
}

static void fillPositions(int* positions, const std::pair<int, char>* path, size_t pathLength, size_t patternLength, int* cachepos)
{
    size_t pathOffset = 0;

    for (size_t i = 0; i < patternLength; ++i)
    {
        assert(pathOffset < pathLength);

        int pos = cachepos[pathOffset * patternLength + i];
        assert(pos >= 0 && pos < (int)pathLength);

        positions[i] = path[pos].first;

        pathOffset = pos + 1;
    }
}

FuzzyMatcher::FuzzyMatcher(const char* query)
{
    // initialize casefolded query
    cfquery.resize(strlen(query));

    for (size_t i = 0; i < cfquery.size(); ++i)
        cfquery[i] = casefold(query[i]);

    // fill table
    memset(table, 0, sizeof(table));

    for (size_t i = 0; i < cfquery.size(); ++i)
    {
        unsigned char ch = static_cast<unsigned char>(cfquery[i]);

        table[ch] = true;
    }

    // add inverse casefolded letters
    for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); ++i)
    {
        unsigned char ch = static_cast<unsigned char>(casefold(i));

        table[i] = table[ch];
    }
}

bool FuzzyMatcher::match(const char* data, size_t size)
{
    const char* pattern = cfquery.c_str();

    const char* begin = data;
    const char* end = data + size;

    while (*pattern)
    {
        while (begin != end && casefold(*begin) != *pattern) begin++;

        if (begin == end) return false;

        begin++;
        pattern++;
    }

    return true;
}

int FuzzyMatcher::rank(const char* data, size_t size, int* positions)
{
    size_t offset = 0;

    while (offset < size && casefold(data[offset]) != cfquery.front()) offset++;
    while (offset < size && casefold(data[size - 1]) != cfquery.back()) size--;

    if (offset + cfquery.size() > size) return INT_MAX;

    if (buf.size() < size + 1) buf.resize(size + 1);

    std::pair<int, char>* bufp = &buf[0];
    size_t bufsize = 0;

    for (size_t i = offset; i < size; ++i)
    {
        unsigned char ch = static_cast<unsigned char>(data[i]);

        bufp[bufsize] = std::make_pair(i, data[i]);
        bufsize += table[ch];
    }

    cache.clear();
    cache.resize(bufsize * cfquery.size(), INT_MIN);

    RankContext c = {&buf[0], bufsize, cfquery.c_str(), cfquery.size(), &cache[0], nullptr};

    if (positions)
    {
        cachepos.clear();
        cachepos.resize(bufsize * cfquery.size(), -1);
        c.cachepos = &cachepos[0];

        int score = rankRecursive<true>(c, 0, -1, 0);

        if (score != INT_MAX) fillPositions(positions, &buf[0], bufsize, cfquery.size(), &cachepos[0]);

        return score;
    }
    else
        return rankRecursive<false>(c, 0, -1, 0);
}
