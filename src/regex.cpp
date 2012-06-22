#include "common.hpp"
#include "regex.hpp"

#include "casefold.hpp"

#include "re2/re2.h"

#include <memory>
#include <stdexcept>

static bool transformRegexCasefold(const char* pattern, std::string& res, bool literal)
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
			res.push_back(casefold(*p));
		}
	}
	
	return true;
}
	

class RE2Regex: public Regex
{
public:
	RE2Regex(const char* string, unsigned int options): casefold(false)
	{
		RE2::Options opts;
		opts.set_posix_syntax(true);
		opts.set_perl_classes(true);
		opts.set_word_boundary(true);
		opts.set_one_line(false);
		opts.set_literal((options & RO_LITERAL) != 0);
		opts.set_log_errors(false);
		
		std::string pattern;
		if ((options & RO_IGNORECASE) && transformRegexCasefold(string, pattern, (options & RO_LITERAL) != 0))
		{
			casefold = true;
		}
		else
		{
			pattern = string;
			opts.set_case_sensitive((options & RO_IGNORECASE) == 0);
		}
		
		re.reset(new RE2(pattern, opts));
		if (!re->ok())
			throw std::runtime_error("Error parsing regular expression " + (string + (": " + re->error())));
	}
	
	virtual const char* rangePrepare(const char* data, size_t size)
	{
		if (casefold)
		{
			char* temp = new char[size];
			casefoldRange(temp, data, data + size);
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
		if (casefold)
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

	virtual void* getRegexObject()
	{
		return re.get();
	}
	
private:
	std::unique_ptr<RE2> re;
	bool casefold;
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
