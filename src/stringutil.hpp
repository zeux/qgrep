// This file is part of qgrep and is distributed under the MIT license, see LICENSE.md
#pragma once

#include <vector>
#include <string>

#include <string.h>
#include <stdarg.h>

struct BackSlashTransformer
{
	char operator()(char ch) const
	{
		return (ch == '/') ? '\\' : ch;
	}
};

inline const char* findLineStart(const char* begin, const char* pos)
{
	for (const char* s = pos; s > begin; --s)
		if (s[-1] == '\n')
			return s;

	return begin;
}

inline const char* findLineEnd(const char* pos, const char* end)
{
	for (const char* s = pos; s != end; ++s)
		if (*s == '\n')
			return s;

	return end;
}

inline unsigned int countLines(const char* begin, const char* end)
{
	unsigned int res = 0;
	
	for (const char* s = begin; s != end; ++s)
		res += (*s == '\n');
		
	return res;
}

template <typename Pred> inline std::vector<std::string> split(const char* str, Pred sep)
{
	std::vector<std::string> result;

	const char* last = str;
	const char* end = str + strlen(str);

	for (const char* i = str; i != end; ++i)
	{
		if (sep(*i))
		{
			if (last != i)
				result.push_back(std::string(last, i));

			last = i + 1;
		}
	}

	if (last != end)
		result.push_back(std::string(last, end));

	return result;
}

void strprintf(std::string& result, const char* format, va_list args);
