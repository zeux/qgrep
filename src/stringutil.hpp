#pragma once

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
