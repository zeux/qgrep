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

	virtual const char* rangeSearch(const char* data, size_t size)
	{
		re2::StringPiece p(data, size);
		
		return RE2::FindAndConsume(&p, *re) ? p.data() : 0;
	}
	
	virtual void rangeFinalize(const char* data)
	{
		if (lowercase)
		{
			delete[] data;
		}
	}

	virtual const char* search(const char* data, size_t size)
	{
		const char* range = rangePrepare(data, size);
		const char* result = rangeSearch(range, size);
		rangeFinalize(range);

		return result ? result - range + data : 0;
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

Regex* createRegex(const char* pattern, unsigned int options)
{
	return new RE2Regex(pattern, options);
}