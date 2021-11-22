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

inline simd16 simd_cmpeq(simd16 a, simd16 b)
{
	return _mm_cmpeq_epi8(a, b);
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

inline int simd_movemask(simd16 v)
{
	return _mm_movemask_epi8(v);
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

inline simd16 simd_cmpeq(simd16 a, simd16 b)
{
	return vceqq_s8(a, b);
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

inline int simd_movemask(simd16 v)
{
	static const uint8_t maskv[16] = {1, 2, 4, 8, 16, 32, 64, 128, 1, 2, 4, 8, 16, 32, 64, 128};

	uint8x16_t mask = vld1q_u8(maskv);
	uint8x16_t masked = vandq_u8(mask, vreinterpretq_u8_s8(v));

#ifdef __aarch64__
	// aarch64 has horizontal sums; MSVC doesn't expose this via arm64_neon.h so this path is exclusive to clang/gcc
	int mask0 = vaddv_u8(vget_low_u8(masked));
	int mask1 = vaddv_u8(vget_high_u8(masked));
#else
	// we need horizontal sums of each half of masked, which can be done in 3 steps (yielding sums of sizes 2, 4, 8)
	uint8x8_t sum1 = vpadd_u8(vget_low_u8(masked), vget_high_u8(masked));
	uint8x8_t sum2 = vpadd_u8(sum1, sum1);
	uint8x8_t sum3 = vpadd_u8(sum2, sum2);

	int mask0 = vget_lane_u8(sum3, 0);
	int mask1 = vget_lane_u8(sum3, 1);
#endif

	return mask0 | (mask1 << 8);
}
#endif
