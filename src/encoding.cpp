// This file is part of qgrep and is distributed under the MIT license, see LICENSE.md
#include "common.hpp"
#include "encoding.hpp"

#include <string.h>

inline uint16_t endianSwap(uint16_t value)
{
	return static_cast<uint16_t>(((value & 0xff) << 8) | (value >> 8));
}

inline uint32_t endianSwap(uint32_t value)
{
	return ((value & 0xff) << 24) | ((value & 0xff00) << 8) | ((value & 0xff0000) >> 8) | (value >> 24);
}

struct UTF8Counter
{
	size_t operator()(size_t result, uint32_t ch)
	{
		// U+0000..U+007F
		if (ch < 0x80) return result + 1;
		// U+0080..U+07FF
		else if (ch < 0x800) return result + 2;
		// U+0800..U+FFFF
		else if (ch < 0x10000) return result + 3;
		// U+10000..U+10FFFF
		else return result + 4;
	}
};

struct UTF8Writer
{
	uint8_t* operator()(uint8_t* result, uint32_t ch)
	{
		// U+0000..U+007F
		if (ch < 0x80)
		{
			*result = static_cast<uint8_t>(ch);
			return result + 1;
		}
		// U+0080..U+07FF
		else if (ch < 0x800)
		{
			result[0] = static_cast<uint8_t>(0xC0 | (ch >> 6));
			result[1] = static_cast<uint8_t>(0x80 | (ch & 0x3F));
			return result + 2;
		}
		// U+0800..U+FFFF
		else if (ch < 0x10000)
		{
			result[0] = static_cast<uint8_t>(0xE0 | (ch >> 12));
			result[1] = static_cast<uint8_t>(0x80 | ((ch >> 6) & 0x3F));
			result[2] = static_cast<uint8_t>(0x80 | (ch & 0x3F));
			return result + 3;
		}
		else
		{
			// U+10000..U+10FFFF
			result[0] = static_cast<uint8_t>(0xF0 | (ch >> 18));
			result[1] = static_cast<uint8_t>(0x80 | ((ch >> 12) & 0x3F));
			result[2] = static_cast<uint8_t>(0x80 | ((ch >> 6) & 0x3F));
			result[3] = static_cast<uint8_t>(0x80 | (ch & 0x3F));
			return result + 4;
		}
	}
};

template <bool swap> struct UTF16Decoder
{
	typedef uint16_t Element;

	template <typename State, typename Pred> static State decode(const uint16_t* data, size_t size, State result, Pred pred)
	{
		const uint16_t* end = data + size;

		while (data < end)
		{
			uint16_t lead = swap ? endianSwap(*data) : *data;

			// U+0000..U+D7FF
			if (lead < 0xD800)
			{
				result = pred(result, lead);
				data += 1;
			}
			// U+E000..U+FFFF
			else if (static_cast<unsigned int>(lead - 0xE000) < 0x2000)
			{
				result = pred(result, lead);
				data += 1;
			}
			// surrogate pair lead
			else if (static_cast<unsigned int>(lead - 0xD800) < 0x400 && data + 1 < end)
			{
				uint16_t next = swap ? endianSwap(data[1]) : data[1];

				if (static_cast<unsigned int>(next - 0xDC00) < 0x400)
				{
					result = pred(result, 0x10000 + ((lead & 0x3ff) << 10) + (next & 0x3ff));
					data += 2;
				}
				else
				{
					data += 1;
				}
			}
			else
			{
				data += 1;
			}
		}

		return result;
	}
};

template <bool swap> struct UTF32Decoder
{
	typedef uint32_t Element;

	template <typename State, typename Pred> static State decode(const uint32_t* data, size_t size, State result, Pred pred)
	{
		const uint32_t* end = data + size;

		while (data < end)
		{
			uint32_t lead = swap ? endianSwap(*data) : *data;

			result = pred(result, lead);
			data += 1;
		}

		return result;
	}
};

template <typename Decoder> inline std::vector<char> convertToUTF8Impl(const char* data, size_t size)
{
	typedef typename Decoder::Element T;

	const T* source = reinterpret_cast<const T*>(data);
	size_t count = size / sizeof(T);

	size_t utf8Length = Decoder::decode(source, count, static_cast<size_t>(0), UTF8Counter());
	std::vector<char> result(utf8Length);

	if (utf8Length > 0)
	{
		uint8_t* beg = reinterpret_cast<uint8_t*>(&result[0]);
		uint8_t* end = Decoder::decode(source, count, beg, UTF8Writer());
		assert(beg + utf8Length == end);
	}

	return result;
}

std::vector<char> convertToUTF8(std::vector<char> data)
{
	const char* contents = data.empty() ? 0 : &data[0];
	size_t size = data.size();

	if (size >= 4 && *reinterpret_cast<const uint32_t*>(contents) == 0x0000feff) return convertToUTF8Impl<UTF32Decoder<false>>(contents + 4, size - 4);
	if (size >= 4 && *reinterpret_cast<const uint32_t*>(contents) == 0xfffe0000) return convertToUTF8Impl<UTF32Decoder<true>>(contents + 4, size - 4);
	if (size >= 2 && *reinterpret_cast<const uint16_t*>(contents) == 0xfeff) return convertToUTF8Impl<UTF16Decoder<false>>(contents + 2, size - 2);
	if (size >= 2 && *reinterpret_cast<const uint16_t*>(contents) == 0xfffe) return convertToUTF8Impl<UTF16Decoder<true>>(contents + 2, size - 2);
	if (size >= 3 && memcmp(contents, "\xef\xbb\xbf", 3) == 0) return std::vector<char>(contents + 3, contents + size);

	return data;
}
