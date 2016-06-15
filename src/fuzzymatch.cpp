#include "common.hpp"
#include "fuzzymatch.hpp"

#include "casefold.hpp"

#include <string.h>
#include <limits.h>

struct RankContext
{
    const RankPathElement* path;
    size_t pathLength;
    const char* pattern;
    size_t patternLength;
    int* cache;
    int* cachepos;
};

template <bool fillPosition>
static int rankRecursive(const RankContext& c, size_t pathOffset, int lastMatch, size_t patternOffset)
{
    const RankPathElement* path = c.path;
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
        if (casefold(path[i].character) == pattern[patternOffset])
        {
            int distance = path[i].position - lastMatch;

            int charScore = (patternOffset == 0) ? path[i].leftScore : 0;

            if (distance > 1 && lastMatch >= 0)
            {
                assert(static_cast<unsigned int>(lastMatch) == path[pathOffset - 1].position);

                charScore += path[pathOffset - 1].rightScore;
                charScore += 10 + (distance - 2);
                charScore += path[i].leftScore;
            }

            int restScore =
                (patternOffset + 1 < patternLength)
                ? rankRecursive<fillPosition>(c, i + 1, path[i].position, patternOffset + 1)
                : path[i].rightScore;

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

static void fillPositions(int* positions, const RankPathElement* path, size_t pathLength, size_t patternLength, int* cachepos)
{
    size_t pathOffset = 0;

    for (size_t i = 0; i < patternLength; ++i)
    {
        assert(pathOffset < pathLength);

        int pos = cachepos[pathOffset * patternLength + i];
        assert(pos >= 0 && pos < (int)pathLength);

        positions[i] = path[pos].position;

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
        unsigned char ch = static_cast<unsigned char>(casefold(char(i)));

        table[i] = table[ch];
    }
}

bool FuzzyMatcher::match(const char* data, size_t size, int* positions)
{
    const char* pattern = cfquery.c_str();

    const char* begin = data;
    const char* end = data + size;

    while (*pattern)
    {
        while (begin != end && casefold(*begin) != *pattern) begin++;

        if (begin == end) return false;

		if (positions) *positions++ = begin - data;
        begin++;
        pattern++;
    }

    return true;
}

static int rankPair(char first, char second)
{
    // boundary character has priority
    if (second == 0)
        return 0;

    // if the main character is alphabetical, weigh boundary character
    if (static_cast<unsigned>((first | ' ') - 'a') <= 26)
    {
        // path components
        if (second == '/' || second == '.')
            return 1;

        // word components
        if (second == '_' || second == '-')
            return 2;
    }

    return 4;
}

int FuzzyMatcher::rank(const char* data, size_t size, int* positions)
{
    size_t begin = 0;
    size_t end = size;

    while (begin < size && casefold(data[begin]) != cfquery.front()) begin++;
    while (begin < end && casefold(data[end - 1]) != cfquery.back()) end--;

    if (begin + cfquery.size() > end) return INT_MAX;

    if (buf.size() < (end - begin) + 1) buf.resize((end - begin) + 1);

    RankPathElement* bufp = &buf[0];
    size_t bufsize = 0;

    for (size_t i = begin; i < end; ++i)
    {
        unsigned char ch = static_cast<unsigned char>(data[i]);

        if (table[ch])
        {
            int leftScore = rankPair(data[i], i > 0 ? data[i - 1] : 0);
            int rightScore = rankPair(data[i], i + 1 < size ? data[i + 1] : 0);

            bufp[bufsize] = RankPathElement(i, data[i], leftScore, rightScore);
            bufsize++;
        }
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
