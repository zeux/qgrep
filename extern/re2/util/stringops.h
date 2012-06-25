#ifndef RE2_UTIL_STRINGOPS_H_
#define RE2_UTIL_STRINGOPS_H_

#define STRINGOPS_SSE 1

#include <string.h>

#if STRINGOPS_SSE
#include <emmintrin.h>
#endif

#ifdef _MSC_VER
#include <intrin.h>
#pragma intrinsic(_BitScanForward)

inline int __builtin_ctz (unsigned int x)
{
	unsigned long r;
	_BitScanForward(&r, x);
	return r;
}
#endif

namespace re2 {

#if STRINGOPS_SSE
inline const void * memchr ( const void * ptr, int value, size_t num )
{
	const unsigned char* data = static_cast<const unsigned char*>(ptr);

	__m128i pattern = _mm_set1_epi8(value);

	while (num >= 16)
	{
		__m128i val = _mm_loadu_si128(reinterpret_cast<const __m128i*>(data));
		__m128i maskv = _mm_cmpeq_epi8(val, pattern);
		int mask = _mm_movemask_epi8(maskv);

		if (mask == 0)
			;
		else
			return data + __builtin_ctz(mask);

		data += 16;
		num -= 16;
	}

	num+=0;

	while (num && *data != value)
	{
		data++;
		num--;
	}

	return num ? data : 0;
}
#else
inline const void * memchr ( const void * ptr, int value, size_t num )
{
    return ::memchr(ptr, value, num);
}
#endif

}

#endif
