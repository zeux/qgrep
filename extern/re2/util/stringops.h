#ifndef RE2_UTIL_STRINGOPS_H_
#define RE2_UTIL_STRINGOPS_H_

#include <string.h>

#ifdef USE_SSE2
#include <emmintrin.h>

#   ifdef _MSC_VER
#       include <intrin.h>
#       pragma intrinsic(_BitScanForward)
#   endif
#endif

namespace re2 {

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
			return data + countTrailingZeros(mask);

		data += 16;
		num -= 16;
	}

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