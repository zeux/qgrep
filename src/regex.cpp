#include "common.hpp"
#include "regex.hpp"

#include "casefold.hpp"

#include "re2/re2.h"
#include "re2/prefilter.h"
#include "re2/prefilter_tree.h"

#include <memory>
#include <stdexcept>

#include <string.h>

#ifdef USE_SSE2
#include <emmintrin.h>

#   ifdef _MSC_VER
#       include <intrin.h>
#       pragma intrinsic(_BitScanForward)
#   endif
#endif

static bool transformRegexCasefold(const char* pattern, std::string& res, bool literal)
{
	res.clear();
	
	// Simple lexer intended to separate literals from non-literals; does not handle Unicode character classes
	// properly, so bail out if we have them
	for (const char* p = pattern; *p; ++p)
	{
		if (*p == '\\' && !literal)
		{
			if (p[1] == 0) return false;
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

class LiteralMatcher
{
public:
	virtual ~LiteralMatcher() {}

	virtual size_t match(const char* data, size_t size) = 0;
};

#ifdef USE_SSE2
inline int countTrailingZeros(int value)
{
#ifdef _MSC_VER
	unsigned long r;
	_BitScanForward(&r, value);
	return r;
#else
	return __builtin_ctz(value);
#endif
}

class LiteralMatcher1: public LiteralMatcher
{
public:
	LiteralMatcher1(const char* string): first(string[0])
	{
	}

	virtual size_t match(const char* data, size_t size)
	{
		__m128i pattern = _mm_set1_epi8(first);

		size_t offset = 0;

		while (offset + 16 <= size)
		{
			__m128i val = _mm_loadu_si128(reinterpret_cast<const __m128i*>(data + offset));
			__m128i maskv = _mm_cmpeq_epi8(val, pattern);
			int mask = _mm_movemask_epi8(maskv);

			if (mask == 0)
				;
			else
				return offset + countTrailingZeros(mask);

			offset += 16;
		}

		while (offset < size && data[offset] != first)
			offset++;

		return offset;
	}

private:
	char first;
};

class LiteralMatcher16: public LiteralMatcher
{
public:
	LiteralMatcher16(const char* string)
	{
		size_t length = strlen(string);

		size_t firstPos = getLeastFrequentLetter(string, length);
		size_t dataOffset = firstPos < 16 ? 0 : firstPos - 16;

		for (size_t i = 0; i < 16; ++i)
		{
			firstLetter[i] = string[firstPos];

			patternData[i] = (dataOffset + i < length) ? string[dataOffset + i] : 0;
			patternMask[i] = (dataOffset + i < length) ? 0 : 0xff;
		}

		firstLetterPos = firstPos;
		firstLetterOffset = firstPos - dataOffset;

		pattern = string;
	}

	virtual size_t match(const char* data, size_t size)
	{
		__m128i firstLetter = _mm_loadu_si128(reinterpret_cast<const __m128i*>(this->firstLetter));
		__m128i patternData = _mm_loadu_si128(reinterpret_cast<const __m128i*>(this->patternData));
		__m128i patternMask = _mm_loadu_si128(reinterpret_cast<const __m128i*>(this->patternMask));

		size_t offset = firstLetterPos;

		while (offset + 32 <= size)
		{
			__m128i value = _mm_loadu_si128(reinterpret_cast<const __m128i*>(data + offset));
			unsigned int mask = _mm_movemask_epi8(_mm_cmpeq_epi8(value, firstLetter));

			// advance offset regardless of match results to reduce number of live values
			offset += 16;

			while (mask != 0)
			{
				unsigned int pos = countTrailingZeros(mask);
				size_t dataOffset = offset - 16 + pos - firstLetterOffset;

				mask &= ~(1 << pos);

				// check if we have a match
				__m128i patternMatch = _mm_loadu_si128(reinterpret_cast<const __m128i*>(data + dataOffset));
				__m128i matchMask = _mm_or_si128(patternMask, _mm_cmpeq_epi8(patternMatch, patternData));

				if (_mm_movemask_epi8(matchMask) == 0xffff)
				{
					size_t matchOffset = dataOffset + firstLetterOffset - firstLetterPos;

					// final check for full pattern
					if (matchOffset + pattern.size() < size && memcmp(data + matchOffset, pattern.c_str(), pattern.size()) == 0)
					{
						return matchOffset;
					}
				}
			}
		}

		return findMatch(pattern.c_str(), pattern.size(), data, size, offset - firstLetterPos);
	}

private:
	unsigned char firstLetter[16];
	unsigned char patternData[16];
	unsigned char patternMask[16];
	size_t firstLetterPos;
	size_t firstLetterOffset;

	std::string pattern;

	static size_t findMatch(const char* x, size_t m, const char* y, size_t n, size_t start)
	{
		for (size_t j = start; j + m <= n; ++j)
		{
			size_t i = 0;
			while (i < m && x[i] == y[i + j]) ++i;

			if (i == m) return j;
		}

		return n;
	}

	static size_t getLeastFrequentLetter(const char* string, size_t length)
	{
		static const int kFrequencyTable[256] =
		{
			0, 1, 0, 0, 0, 0, 0, 0, 0, 10602590, 15871966, 0, 115, 15871967, 0, 0, 
			0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 20, 0, 0, 0, 0, 
			137776326, 380531, 2050160, 1390430, 55652, 156711, 622798, 377541, 6347309, 6349017, 15625360, 795950, 19415821, 7560692, 4462299, 7677639, 
			28564723, 4287685, 3463240, 3246751, 2524164, 2032662, 2395948, 1767308, 2015273, 1511489, 3656040, 5020957, 952959, 3703051, 1709838, 58757, 
			33589, 4925784, 2109034, 4461995, 3250571, 5025264, 3924677, 1936150, 1023430, 3832789, 193034, 437171, 2790600, 2535084, 2734912, 2410167, 
			2863507, 180258, 3341306, 4837122, 4450499, 1455744, 1216353, 703917, 784740, 538485, 229684, 898206, 1013666, 897273, 16601, 7723320, 
			4063, 19048491, 4008087, 10148475, 9126676, 33071235, 7304196, 4732808, 5355028, 17956702, 429890, 1506676, 11310158, 8442069, 17783591, 16421409, 
			7587849, 310591, 18113867, 15601492, 26260892, 8333809, 2934125, 2055891, 16325891, 3402971, 743219, 1688043, 180102, 1687646, 39011, 0, 
			195, 316, 835, 1262, 32, 60, 69, 39, 75, 79, 97, 112, 98, 114, 64, 92, 
			119, 87, 475, 193, 184, 109, 521, 5121, 9, 20, 24, 11, 23, 39, 48, 19, 
			39, 44, 57, 59, 81, 455, 51, 30, 31, 1126, 86, 54, 39, 93, 39, 29, 
			110, 38, 46, 104, 69, 193, 85, 8353, 132, 68, 99, 64, 87, 138, 143, 97, 
			16, 11, 334, 13773, 126, 117, 14, 9, 22, 70, 44, 10, 66, 24, 12, 27, 
			1254, 50889, 79, 77, 62, 140, 14, 11, 9, 4, 7, 11, 21, 8, 0, 9, 
			11, 11, 84, 627, 43, 113, 31, 58, 81, 69, 36, 4, 9, 27, 9, 18, 
			61, 7, 5, 12, 6, 8, 7, 6, 8, 9, 20, 4, 18, 5, 4, 2, 
		};

		size_t result = 0;

		for (size_t i = 1; i < length; ++i)
			if (kFrequencyTable[static_cast<unsigned char>(string[i])] < kFrequencyTable[static_cast<unsigned char>(string[result])])
				result = i;
			
		return result;
	}
};
#endif

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
		opts.set_never_nl(true);
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

		std::string prefix = getPrefix(re.get(), 128);

	#ifdef USE_SSE2
		if (prefix.length() == 1)
			matcher.reset(new LiteralMatcher1(prefix.c_str()));
		else if (prefix.length() > 1)
			matcher.reset(new LiteralMatcher16(prefix.c_str()));
	#endif
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
		size_t offset = 0;

		if (matcher)
		{
			offset = matcher->match(data, size);
			assert(offset <= size);

			if (offset == size) return RegexMatch();
		}

		re2::StringPiece p(data, size);
		re2::StringPiece match;

		if (re->Match(p, offset, size, re2::RE2::UNANCHORED, &match, 1))
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

	virtual std::vector<std::string> prefilterPrepare()
	{
		std::unique_ptr<re2::Prefilter> prf(re2::Prefilter::FromRE2(re.get()));

		if (prf && prf->op() != re2::Prefilter::NONE)
		{
			prefilter.reset(new re2::PrefilterTree());
			prefilter->Add(prf.release());

			std::vector<std::string> result;
			prefilter->Compile(&result);

			return result;
		}
		else
		{
			prefilter.reset();

			return {};
		}
	}

	virtual bool prefilterMatch(const std::vector<int>& matches)
	{
		if (!prefilter)
			return true;

		std::vector<int> result;
		prefilter->RegexpsGivenStrings(matches, &result);

		assert(result.size() <= 1);
		return !result.empty();
	}
	
private:
	std::unique_ptr<RE2> re;
	bool casefold;

	std::unique_ptr<LiteralMatcher> matcher;

	std::unique_ptr<re2::PrefilterTree> prefilter;

	static std::string getPrefix(RE2* re, size_t maxlen)
	{
		std::string min, max;

		if (!re->PossibleMatchRange(&min, &max, maxlen))
			return "";

		size_t offset = 0;

		while (offset < min.size() && offset < max.size() && min[offset] == max[offset])
			offset++;

		return min.substr(0, offset);
	}
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
