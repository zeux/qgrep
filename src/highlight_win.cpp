#ifdef _WIN32

#include "common.hpp"
#include "highlight.hpp"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

static const int kColorTable[] =
{
	0,
	FOREGROUND_RED,
	FOREGROUND_GREEN,
	FOREGROUND_RED | FOREGROUND_GREEN,
	FOREGROUND_BLUE,
	FOREGROUND_RED | FOREGROUND_BLUE,
	FOREGROUND_GREEN | FOREGROUND_BLUE,
	FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE,
};

// This is far from a full-featured escape sequence parser; it only parses color settings (and probably not exactly right at that)
static std::pair<int, const char*> parseEscapeSequence(const char* data, size_t size)
{
	if (size > 2 && data[0] == '\033' && data[1] == '[')
	{
		int color = 0;

		for (size_t i = 2; i < size; ++i)
		{
			switch (data[i])
			{
			case ';':
				break;

			case 'm':
				return std::make_pair(color, data + i + 1);

			case '0':
				color = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
				break;

			case '1':
				color |= FOREGROUND_INTENSITY;
				break;

			case '3':
				if (i + 1 < size && data[i + 1] >= '0' && data[i + 1] <= '7')
				{
					color = (color & FOREGROUND_INTENSITY) | kColorTable[data[i + 1] - '0'];
					i++;
					break;
				}

				// fallthrough to error

			default:
				return std::make_pair(0, nullptr);
			}
		}
	}

	return std::make_pair(0, nullptr);
}

void printEscapeCodedStringToConsole(const char* data, size_t size)
{
	HANDLE handle = GetStdHandle(STD_OUTPUT_HANDLE);

	const char* begin = data;
	const char* end = data + size;

	while (const char* pos = static_cast<const char*>(memchr(begin, '\033', end - begin)))
	{
		if (begin != pos)
			WriteConsoleA(handle, begin, pos - begin, NULL, NULL);

		auto p = parseEscapeSequence(pos, end - pos);

		if (p.second)
		{
			SetConsoleTextAttribute(handle, p.first);
			begin = p.second;
		}
		else
		{
			// print unrecognized escape sequences as is
			WriteConsoleA(handle, pos, 1, NULL, NULL);
			begin = pos + 1;
		}
	}

	if (begin != end)
		WriteConsoleA(handle, begin, end - begin, NULL, NULL);
}

#endif