// SPDX-License-Identifier: GPL-2.0-or-later
// Experimental, exact-result SIMD accelerator for PS4 OrbShdr metadata search.
// Branch: perf/tlou-cpu-shader-scan-neon-20261108; production remains unchanged.
// Important: never cache the result pointer or guest code bytes.
#pragma once
#include <bit>
#include <cstdint>
#if defined(__aarch64__)
#include <arm_neon.h>
#elif defined(__SSE2__)
#include <emmintrin.h>
#include <xmmintrin.h>
#endif

namespace AmdGpu::Detail {
template <typename FullSignatureValid>
[[nodiscard]] inline const std::uint32_t* FindShaderSignature32(
    const std::uint32_t* p, const std::uint32_t* end, std::uint32_t signature_prefix,
    FullSignatureValid&& valid) {
#if defined(__aarch64__)
    const uint32x4_t target = vdupq_n_u32(signature_prefix);
    // Four independent NEON loads inspect 16 DWORD-aligned metadata
    // candidates per branch. The uncommon matching block is then checked
    // in ORIGINAL ascending order; no stale shader bytes or saved hit.
    for (; end - p >= 16; p += 16) {
        const uint32x4_t m0 = vceqq_u32(vld1q_u32(p +  0), target);
        const uint32x4_t m1 = vceqq_u32(vld1q_u32(p +  4), target);
        const uint32x4_t m2 = vceqq_u32(vld1q_u32(p +  8), target);
        const uint32x4_t m3 = vceqq_u32(vld1q_u32(p + 12), target);
        const uint32x4_t any = vorrq_u32(vorrq_u32(m0,m1),vorrq_u32(m2,m3));
        if (vmaxvq_u32(any) == 0) continue;
        for (unsigned lane = 0; lane < 16; ++lane) {
            if (p[lane] == signature_prefix && valid(p + lane)) return p + lane;
        }
    }
    for (; end - p >= 4; p += 4) {
        const uint32x4_t eq = vceqq_u32(vld1q_u32(p), target);
        if (vmaxvq_u32(eq) == 0) continue;
        // Preserve exactly the *first* valid signature's address, including
        // when a shorter decoy is present in one of the earlier lanes.
        if (vgetq_lane_u32(eq, 0) && valid(p + 0)) return p + 0;
        if (vgetq_lane_u32(eq, 1) && valid(p + 1)) return p + 1;
        if (vgetq_lane_u32(eq, 2) && valid(p + 2)) return p + 2;
        if (vgetq_lane_u32(eq, 3) && valid(p + 3)) return p + 3;
    }
#elif defined(__SSE2__)
    const __m128i target = _mm_set1_epi32(static_cast<int>(signature_prefix));
    for (; end - p >= 16; p += 16) {
        const __m128i m0 = _mm_cmpeq_epi32(_mm_loadu_si128(
            reinterpret_cast<const __m128i*>(p +  0)), target);
        const __m128i m1 = _mm_cmpeq_epi32(_mm_loadu_si128(
            reinterpret_cast<const __m128i*>(p +  4)), target);
        const __m128i m2 = _mm_cmpeq_epi32(_mm_loadu_si128(
            reinterpret_cast<const __m128i*>(p +  8)), target);
        const __m128i m3 = _mm_cmpeq_epi32(_mm_loadu_si128(
            reinterpret_cast<const __m128i*>(p + 12)), target);
        const __m128i any = _mm_or_si128(_mm_or_si128(m0,m1),
                                        _mm_or_si128(m2,m3));
        if (_mm_movemask_ps(_mm_castsi128_ps(any)) == 0) continue;
        for (unsigned lane = 0; lane < 16; ++lane) {
            if (p[lane] == signature_prefix && valid(p + lane)) return p + lane;
        }
    }
    for (; end - p >= 4; p += 4) {
        const __m128i lanes =
            _mm_loadu_si128(reinterpret_cast<const __m128i*>(p));
        unsigned hits = static_cast<unsigned>(_mm_movemask_ps(
            _mm_castsi128_ps(_mm_cmpeq_epi32(lanes, target))));
        while (hits) {
            const unsigned lane = std::countr_zero(hits);
            if (valid(p + lane)) return p + lane;
            hits &= hits - 1;
        }
    }
#endif
    // Tail and non-SIMD platforms use the exact original candidate order.
    for (; p < end; ++p) {
        if (*p == signature_prefix && valid(p)) return p;
    }
    return nullptr;
}
} // namespace AmdGpu::Detail
