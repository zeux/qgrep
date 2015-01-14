#include "common.hpp"
#include "filter.hpp"

#include "output.hpp"
#include "search.hpp"
#include "regex.hpp"
#include "stringutil.hpp"
#include "highlight.hpp"
#include "fuzzymatch.hpp"

#include <memory>
#include <algorithm>
#include <limits.h>

struct FilterOutput
{
	FilterOutput(Output* output, unsigned int options, unsigned int limit): output(output), options(options), limit(limit)
	{
	}

	Output* output;
	unsigned int options;
	unsigned int limit;
};

struct FilterHighlightBuffer
{
	std::vector<int> posbuf;
	std::vector<HighlightRange> ranges;
	std::string result;
};

static void processMatch(const char* path, size_t pathLength, FilterOutput* output)
{
	if (output->options & SO_VISUALSTUDIO)
	{
		char* buffer = static_cast<char*>(alloca(pathLength));
		
		std::transform(path, path + pathLength, buffer, BackSlashTransformer());
		path = buffer;
	}

	output->output->rawprint(path, pathLength);
	output->output->rawprint("\n", 1);
}

static void processMatch(const FilterEntry& entry, const char* buffer, FilterOutput* output)
{
	processMatch(buffer + entry.offset, entry.length, output);
}

static unsigned int dumpEntries(const FilterEntries& entries, FilterOutput* output)
{
	unsigned int count = std::min(output->limit, entries.entryCount);

	for (unsigned int i = 0; i < count; ++i)
		processMatch(entries.entries[i], entries.buffer, output);

	return count;
}

template <typename Pred> static void filterRegex(const FilterEntries& entries, Regex* re, unsigned int limit, Pred pred)
{
	const char* range = re->rangePrepare(entries.buffer, entries.bufferSize);

	const char* begin = range;
	const char* end = begin + entries.bufferSize;

	unsigned int matches = 0;

	while (RegexMatch match = re->rangeSearch(begin, end - begin))
	{
		size_t matchOffset = match.data - range;

		// find first file entry with offset > matchOffset
		const FilterEntry* entry =
			std::upper_bound(entries.entries, entries.entries + entries.entryCount, matchOffset,
                [](size_t l, const FilterEntry& r) { return l < r.offset; });

		// find last file entry with offset <= matchOffset
		assert(entry > entries.entries);
		entry--;

		// print match
		pred(entry - entries.entries);

		// move to next line
		const char* lend = findLineEnd(match.data + match.size, end);
		if (lend == end) break;
		begin = lend + 1;
		matches++;

		if (matches >= limit) break;
	}

	re->rangeFinalize(range);
}

static void processMatchHighlightRegex(Regex* re, FilterHighlightBuffer& hlbuf, const FilterEntry& entry, size_t offset, const char* buffer, FilterOutput* output)
{
    const char* data = entry.offset + buffer;

	hlbuf.ranges.clear();
	highlightRegex(hlbuf.ranges, re, data, entry.length, nullptr, offset);

	hlbuf.result.clear();
	highlight(hlbuf.result, data, entry.length, hlbuf.ranges.empty() ? nullptr : &hlbuf.ranges[0], hlbuf.ranges.size(), kHighlightMatch);

	processMatch(hlbuf.result.c_str(), hlbuf.result.size(), output);
}

static unsigned int filterRegex(const FilterEntries& entries, const FilterEntries& matchEntries, const char* string, FilterOutput* output)
{
	unsigned int result = 0;

	std::unique_ptr<Regex> re(createRegex(string, getRegexOptions(output->options)));

	FilterHighlightBuffer hlbuf;

	filterRegex(matchEntries, re.get(), output->limit,
		[&](unsigned int i) {
            const FilterEntry& e = entries.entries[i];

			if (output->options & SO_HIGHLIGHT_MATCHES)
				processMatchHighlightRegex(re.get(), hlbuf, e, e.length - matchEntries.entries[i].length, entries.buffer, output);
			else
				processMatch(e, entries.buffer, output);

			result++;
		});

	return result;
}

struct VisualAssistFragment
{
    std::string text;
    std::unique_ptr<Regex> re;
    bool ispath;
};

static void processMatchHighlightVisualAssist(const std::vector<VisualAssistFragment>& fragments,
	FilterHighlightBuffer& hlbuf, const FilterEntry& entry, const char* entryBuffer, const FilterEntry& nameEntry, const char* nameBuffer, FilterOutput* output)
{
	const char* data = entryBuffer + entry.offset;

	hlbuf.ranges.clear();

	for (auto& f: fragments)
		highlightRegex(hlbuf.ranges, f.re.get(), data, entry.length, nullptr, f.ispath ? 0 : entry.length - nameEntry.length);

	hlbuf.result.clear();
	highlight(hlbuf.result, data, entry.length, hlbuf.ranges.empty() ? nullptr : &hlbuf.ranges[0], hlbuf.ranges.size(), kHighlightMatch);

	processMatch(hlbuf.result.c_str(), hlbuf.result.size(), output);
}

static unsigned int filterVisualAssist(const FilterEntries& entries, const FilterEntries& names, const char* string, FilterOutput* output)
{
    std::vector<VisualAssistFragment> fragments;

    for (auto& s: split(string, isspace))
    {
		fragments.emplace_back();
        VisualAssistFragment& f = fragments.back();

        f.text = s;
		f.re.reset(createRegex(s.c_str(), getRegexOptions(output->options | SO_LITERAL)));
        f.ispath = s.find_first_of("/\\") != std::string::npos;
    }

	if (fragments.empty()) return dumpEntries(entries, output);

	// sort name components first, path components last, larger components first
	std::sort(fragments.begin(), fragments.end(),
        [](const VisualAssistFragment& lhs, const VisualAssistFragment& rhs) {
            return (lhs.ispath != rhs.ispath) ? lhs.ispath < rhs.ispath : lhs.text.length() > rhs.text.length();
        });

	// gather files by first component
	std::vector<unsigned int> results;

	filterRegex(fragments[0].ispath ? entries : names, fragments[0].re.get(),
		(fragments.size() == 1) ? output->limit : ~0u, [&](unsigned int i) { results.push_back(i); });

	// filter results by subsequent components
	for (size_t i = 1; i < fragments.size(); ++i)
	{
        const VisualAssistFragment& f = fragments[i];

		results.erase(std::remove_if(results.begin(), results.end(), [&](unsigned int i) -> bool {
            const FilterEntries& matchEntries = f.ispath ? entries : names;
            const FilterEntry& me = matchEntries.entries[i];

			return f.re->search(matchEntries.buffer + me.offset, me.length).size == 0; }), results.end());
	}

	// trim results according to limit
	if (results.size() > output->limit)
		results.resize(output->limit);

	// output results
	FilterHighlightBuffer hlbuf;

	for (auto& i: results)
	{
        const FilterEntry& e = entries.entries[i];
        
		if (output->options & SO_HIGHLIGHT_MATCHES)
			processMatchHighlightVisualAssist(fragments, hlbuf, e, entries.buffer, names.entries[i], names.buffer, output);
		else
			processMatch(e, entries.buffer, output);
	}

	return results.size();
}

static void processMatchHighlightFuzzy(FuzzyMatcher& matcher, bool ranked, FilterHighlightBuffer& hlbuf, const FilterEntry& entry, const char* buffer, FilterOutput* output)
{
    const char* data = buffer + entry.offset;

	assert(matcher.size() > 0);
	hlbuf.posbuf.resize(matcher.size());

	if (ranked)
		matcher.rank(data, entry.length, &hlbuf.posbuf[0]);
	else
		matcher.match(data, entry.length, &hlbuf.posbuf[0]);

	hlbuf.ranges.resize(hlbuf.posbuf.size());
	for (size_t i = 0; i < hlbuf.posbuf.size(); ++i) hlbuf.ranges[i] = std::make_pair(hlbuf.posbuf[i], 1);

	hlbuf.result.clear();
	highlight(hlbuf.result, data, entry.length, hlbuf.ranges.empty() ? nullptr : &hlbuf.ranges[0], hlbuf.ranges.size(), kHighlightMatch);

	processMatch(hlbuf.result.c_str(), hlbuf.result.size(), output);
}

static unsigned int filterFuzzy(const FilterEntries& entries, const char* string, FilterOutput* output)
{
	FuzzyMatcher matcher(string);

	typedef std::pair<int, const FilterEntry*> Match;

	std::vector<Match> matches;
	unsigned int perfectMatches = 0;

	for (size_t i = 0; i < entries.entryCount; ++i)
	{
		const FilterEntry& e = entries.entries[i];
		const char* data = entries.buffer + e.offset;

		if (matcher.match(data, e.length))
		{
            int score = matcher.rank(data, e.length);
			assert(score != INT_MAX);

			matches.push_back(std::make_pair(score, &e));

			if (score == 0)
			{
				perfectMatches++;
				if (perfectMatches >= output->limit) break;
			}
		}
	}

	auto compareMatches = [](const Match& l, const Match& r) { return l.first == r.first ? l.second < r.second : l.first < r.first; };

	if (matches.size() <= output->limit)
		std::sort(matches.begin(), matches.end(), compareMatches);
	else
	{
		std::partial_sort(matches.begin(), matches.begin() + output->limit, matches.end(), compareMatches);
		matches.resize(output->limit);
	}

	FilterHighlightBuffer hlbuf;

	for (auto& m: matches)
	{
		const FilterEntry& e = *m.second;

		if (output->options & SO_HIGHLIGHT_MATCHES)
			processMatchHighlightFuzzy(matcher, /* ranked= */ true, hlbuf, e, entries.buffer, output);
		else
			processMatch(e, entries.buffer, output);
	}

	return matches.size();
}

static void buildNameBuffer(FilterEntries& names, const FilterEntries& entries, std::unique_ptr<FilterEntry[]>& entryptr, std::unique_ptr<char[]>& bufferptr)
{
    // fill entries
    entryptr.reset(new FilterEntry[entries.entryCount]);

    names.entries = entryptr.get();
    names.entryCount = entries.entryCount;

    size_t offset = 0;

    for (unsigned int i = 0; i < entries.entryCount; ++i)
    {
        const FilterEntry& e = entries.entries[i];

        const char* path = entries.buffer + e.offset;
        const char* name = path + e.length;

        while (name > path && name[-1] != '/' && name[-1] != '\\') name--;

        FilterEntry& n = names.entries[i];

        n.offset = offset;
        n.length = path + e.length - name;

        offset += n.length + 1;
    }

    // fill name buffer
    bufferptr.reset(new char[offset]);

    names.buffer = bufferptr.get();
    names.bufferSize = offset;

	char* buffer = bufferptr.get();

    for (unsigned int i = 0; i < entries.entryCount; ++i)
    {
        const FilterEntry& e = entries.entries[i];
        const FilterEntry& n = names.entries[i];

        memcpy(buffer + n.offset, entries.buffer + e.offset + e.length - n.length, n.length);
        buffer[n.offset + n.length] = '\n';
    }
}

static const FilterEntries& getNameBuffer(const FilterEntries* namesOpt, FilterEntries& names, const FilterEntries& entries,
    std::unique_ptr<FilterEntry[]>& entryptr, std::unique_ptr<char[]>& bufferptr)
{
    if (namesOpt) return *namesOpt;

    buildNameBuffer(names, entries, entryptr, bufferptr);
    return names;
}

unsigned int filter(Output* output_, const char* string, unsigned int options, unsigned int limit, const FilterEntries& entries, const FilterEntries* namesOpt)
{
    assert(!namesOpt || namesOpt->entryCount == entries.entryCount);

    FilterEntries names = {};
    std::unique_ptr<FilterEntry[]> nameEntries;
    std::unique_ptr<char[]> nameBuffer;

	FilterOutput output(output_, options, limit);

	if (*string == 0)
		return dumpEntries(entries, &output);
	else if (options & SO_FILE_NAMEREGEX)
		return filterRegex(entries, getNameBuffer(namesOpt, names, entries, nameEntries, nameBuffer), string, &output);
	else if (options & SO_FILE_PATHREGEX)
		return filterRegex(entries, entries, string, &output);
	else if (options & SO_FILE_VISUALASSIST)
		return filterVisualAssist(entries, getNameBuffer(namesOpt, names, entries, nameEntries, nameBuffer), string, &output);
	else if (options & SO_FILE_FUZZY)
		return filterFuzzy(entries, string, &output);
	else
	{
		output_->error("Unknown file search type\n");
		return 0;
	}
}
