// This file is part of qgrep and is distributed under the MIT license, see LICENSE.md
#pragma once

#include <stdint.h>

#ifdef USE_SSE2
#include <emmintrin.h>

typedef __m128i simd16;

inline simd16 simd_dup(char v)
{
	return _mm_set1_epi8(v);
}

inline simd16 simd_load(const void* p)
{
	return _mm_loadu_si128(static_cast<const __m128i*>(p));
}

inline void simd_store(void* p, simd16 v)
{
	_mm_storeu_si128(static_cast<__m128i*>(p), v);
}

inline simd16 simd_cmpgt(simd16 a, simd16 b)
{
	return _mm_cmpgt_epi8(a, b);
}

inline simd16 simd_add(simd16 a, simd16 b)
{
	return _mm_add_epi8(a, b);
}

inline simd16 simd_and(simd16 a, simd16 b)
{
	return _mm_and_si128(a, b);
}

inline simd16 simd_or(simd16 a, simd16 b)
{
	return _mm_or_si128(a, b);
}
#endif

#ifdef USE_NEON
#include <arm_neon.h>

typedef uint8x16_t simd16;

inline simd16 simd_dup(char v)
{
	return vdupq_n_s8(v);
}

inline simd16 simd_load(const void* p)
{
	return vld1q_s8(static_cast<const int8_t*>(p));
}

inline void simd_store(void* p, simd16 v)
{
	vst1q_s8(static_cast<int8_t*>(p), v);
}

inline simd16 simd_cmpgt(simd16 a, simd16 b)
{
	return vcgtq_s8(a, b);
}

inline simd16 simd_add(simd16 a, simd16 b)
{
	return vaddq_s8(a, b);
}

inline simd16 simd_and(simd16 a, simd16 b)
{
	return vandq_s8(a, b);
}

inline simd16 simd_or(simd16 a, simd16 b)
{
	return vorrq_s8(a, b);
}
#endif
