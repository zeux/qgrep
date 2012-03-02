#include "regex.hpp"
#include "common.hpp"

#include "re2/re2.h"

#include <memory>

static bool transformRegexLower(const char* pattern, std::string& res, bool literal)
{
	res.clear();
	
	// Simple lexer intended to separate literals from non-literals; does not handle Unicode character classes
	// properly, so bail out if we have them
	for (const char* p = pattern; *p; ++p)
	{
		if (*p == '\\' && !literal)
		{
			if (p[1] == 'p' || p[1] == 'P') return false;
			res.push_back(*p);
			p++;
			res.push_back(*p);
		}
		else
		{
			res.push_back(tolower(*p));
		}
	}
	
	return true;
}
	

class RE2Regex: public Regex
{
public:
	RE2Regex(const char* string, unsigned int options): lowercase(false)
	{
		RE2::Options opts;
		opts.set_literal((options & SO_LITERAL) != 0);
		
		std::string pattern;
		if ((options & SO_IGNORECASE) && transformRegexLower(string, pattern, (options & SO_LITERAL) != 0))
		{
			lowercase = true;
		}
		else
		{
			pattern = string;
			opts.set_case_sensitive((options & SO_IGNORECASE) == 0);
		}
		
		re.reset(new RE2(pattern, opts));
		if (!re->ok()) fatal("Error parsing regular expression %s: %s\n", string, re->error().c_str());
		
		if (lowercase)
		{
			for (size_t i = 0; i < sizeof(lower); ++i)
			{
				lower[i] = tolower(i);
			}
		}
	}
	
	virtual const char* rangePrepare(const char* data, size_t size)
	{
		if (lowercase)
		{
			char* temp = new char[size];
			transformRangeLower(temp, data, data + size);
			return temp;
		}

		return data;
	}

	virtual RegexMatch rangeSearch(const char* data, size_t size)
	{
		re2::StringPiece p(data, size);
		re2::StringPiece match;

		if (re->Match(re2::StringPiece(data, size), 0, size, re2::RE2::UNANCHORED, &match, 1))
			return RegexMatch(match.data(), match.size());

		return RegexMatch();
	}
	
	virtual void rangeFinalize(const char* data)
	{
		if (lowercase)
		{
			delete[] data;
		}
	}

	virtual RegexMatch search(const char* data, size_t size)
	{
		const char* range = rangePrepare(data, size);
		RegexMatch result = rangeSearch(range, size);
		rangeFinalize(range);

		return result ? RegexMatch(result.data - range + data, result.size) : RegexMatch();
	}
	
private:
	void transformRangeLower(char* dest, const char* begin, const char* end)
	{
		for (const char* i = begin; i != end; ++i)
			*dest++ = lower[static_cast<unsigned char>(*i)];
	}
	
	std::unique_ptr<RE2> re;
	bool lowercase;
	char lower[256];
};

RegexMatch::RegexMatch(): data(0), size(0)
{
}

RegexMatch::RegexMatch(const char* data, size_t size): data(data), size(size)
{
}

RegexMatch::operator bool() const
{
	return data != 0;
}

Regex* createRegex(const char* pattern, unsigned int options)
{
	return new RE2Regex(pattern, options);
}