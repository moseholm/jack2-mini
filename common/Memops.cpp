// Copyright (C) 2000 Paul Davis
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

#define _ISOC9X_SOURCE 1
#define _ISOC99_SOURCE 1

#define __USE_ISOC9X 1
#define __USE_ISOC99 1

#include "Memops.h"
#include <endian.h>
#include <limits.h>
#include <math.h>
#include <memory.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <array>
#include <cstddef>
#include <cstdint>

#if defined(__SSE2__)
#include <emmintrin.h>
#ifdef __SSE4_1__
#include <smmintrin.h>
#endif
#endif

#if defined(__ARM_NEON__) || defined(__ARM_NEON)
#include <arm_neon.h>
#endif

// Notes about these *_SCALING values.
//
// the MAX_<N>BIT values are floating point. when multiplied by
// a full-scale normalized floating point sample value (-1.0..+1.0)
// they should give the maximum value representable with an integer
// sample type of N bits. Note that this is asymmetric. Sample ranges
// for signed integer, 2's complement values are -(2^(N-1) to +(2^(N-1)-1)
//
// Complications
// -------------
// If we use +2^(N-1) for the scaling factors, we run into a problem:
//
// if we start with a normalized float value of -1.0, scaling
// to 24 bits would give -8388608 (-2^23), which is ideal.
// But with +1.0, we get +8388608, which is technically out of range.
//
// We never multiply a full range normalized value by this constant,
// but we could multiply it by a positive value that is close enough to +1.0
// to produce a value > +(2^(N-1)-1.
//
// There is no way around this paradox without wasting CPU cycles to determine
// which scaling factor to use (i.e. determine if its negative or not,
// use the right factor).
//
// So, for now (October 2008) we use 2^(N-1)-1 as the scaling factor.

static constexpr double kSample32BitScaling = 2147483647.0;
static constexpr float kSample24BitScaling = 8388607.0f;
static constexpr float kSample16BitScaling = 32767.0f;

// these are just values to use if the floating point value was out of range
//
// advice from Fons Adriaensen: make the limits symmetrical

static constexpr std::int32_t kSample32BitMax = 2147483647;
static constexpr std::int32_t kSample32BitMin = -2147483647;
static constexpr double kSample32BitMaxD = 2147483647.0;
static constexpr double kSample32BitMinD = -2147483647.0;

static constexpr std::int32_t kSample24BitMax = 8388607;
static constexpr std::int32_t kSample24BitMin = -8388607;
static constexpr float kSample24BitMaxF = 8388607.0f;
static constexpr float kSample24BitMinF = -8388607.0f;

static constexpr std::int32_t kSample16BitMax = 32767;
static constexpr std::int32_t kSample16BitMin = -32767;
static constexpr float kSample16BitMaxF = 32767.0f;
static constexpr float kSample16BitMinF = -32767.0f;

// these mark the outer edges of the range considered "within" range
// for a floating point sample value. values outside (and on the boundaries)
// of this range will be clipped before conversion; values within this
// range will be scaled to appropriate values for the target sample
// type.

static constexpr float kNormalizedFloatMin = -1.0f;
static constexpr float kNormalizedFloatMax = 1.0f;

// define this in case we end up on a platform that is missing
// the real lrintf functions

inline std::int64_t fRound(float f)
{
    return lrintf(f);
}

inline std::int64_t dRound(double d)
{
    return lrint(d);
}

template <typename S, typename D>
inline void float16(S s, D& d)
{
    if (s <= kNormalizedFloatMin) {
        d = kSample16BitMin;
    } else if (s >= kNormalizedFloatMax) {
        d = kSample16BitMax;
    } else {
        d = fRound(s * kSample16BitScaling);
    }
}

// call this when "s" has already been scaled (e.g. when dithering)

template <typename S, typename D>
inline void float16Scaled(S s, D& d)
{
    if (s <= kSample16BitMinF) {
        d = kSample16BitMinF;
    } else if (s >= kSample16BitMaxF) {
        d = kSample16BitMax;
    } else {
        d = fRound(s);
    }
}

template <typename S, typename D>
inline void float24u32(S s, D& d)
{
    if (s <= kNormalizedFloatMin) {
        d = kSample24BitMin << 8;
    } else if (s >= kNormalizedFloatMax) {
        d = kSample24BitMax << 8;
    } else {
        d = fRound(s * kSample24BitScaling) << 8;
    }
}

template <typename S, typename D>
inline void float24l32(S s, D& d)
{
    if (s <= kNormalizedFloatMin) {
        d = kSample24BitMin;
    } else if (s >= kNormalizedFloatMax) {
        d = kSample24BitMax;
    } else {
        d = fRound(s * kSample24BitScaling);
    }
}

template <typename S, typename D>
inline void float32(S s, D& d)
{
    double clipped = fmin(kNormalizedFloatMax, fmax((double)s, kNormalizedFloatMin));
    double scaled = clipped * kSample32BitMaxD;
    d = dRound(scaled);
}

// call this when "s" has already been scaled (e.g. when dithering)

template <typename S, typename D>
inline void float24u32Scaled(S s, D& d)
{
    if (s <= kSample24BitMinF) {
        d = kSample24BitMin << 8;
    } else if (s >= kSample24BitMaxF) {
        d = kSample24BitMax << 8;
    } else {
        d = fRound(s) << 8;
    }
}

template <typename S, typename D>
inline void float24(S s, D& d)
{
    if (s <= kNormalizedFloatMin) {
        d = kSample24BitMin;
    } else if (s >= kNormalizedFloatMax) {
        d = kSample24BitMax;
    } else {
        d = fRound(s * kSample24BitScaling);
    }
}

// call this when "s" has already been scaled (e.g. when dithering)

template <typename S, typename D>
inline void float24Scaled(S s, D& d)
{
    if (s <= kSample24BitMinF) {
        d = kSample24BitMin;
    } else if (s >= kSample24BitMaxF) {
        d = kSample24BitMax;
    } else {
        d = fRound(s);
    }
}

#if defined(__SSE2__)

// generates same as _mm_set_ps(1.f, 1.f, 1f., 1f) but faster
static inline __m128 genOne(void)
{
    volatile __m128i x = { 0 }; // shut up, GCC
    __m128i ones = _mm_cmpeq_epi32(x, x);
    return (__m128)_mm_slli_epi32(_mm_srli_epi32(ones, 25), 23);
}

static inline __m128 clip(__m128 s, __m128 min, __m128 max)
{
    return _mm_min_ps(max, _mm_max_ps(s, min));
}

static inline __m128d clipDouble(__m128d s, __m128d min, __m128d max)
{
    return _mm_min_pd(max, _mm_max_pd(s, min));
}

static inline __m128i float24Sse(__m128 s)
{
    const __m128 upperBound = genOne(); // NORMALIZED_FLOAT_MAX
    const __m128 lowerBound = _mm_sub_ps(_mm_setzero_ps(), upperBound);

    __m128 clipped = clip(s, lowerBound, upperBound);
    __m128 scaled = _mm_mul_ps(clipped, _mm_set1_ps(kSample24BitScaling));
    return _mm_cvtps_epi32(scaled);
}
#endif


#if defined(__ARM_NEON__) || defined(__ARM_NEON)

static inline float32x4_t clip(float32x4_t s, float32x4_t min, float32x4_t max)
{
    return vminq_f32(max, vmaxq_f32(s, min));
}

// Rounds to the nearest integer, ties to even, as lrintf does in the scalar code and as the SSE conversion does.
// AArch64 has that conversion; the truncating one would differ at every value that is not exact.  32-bit NEON has no
// rounding conversion, so there it truncates.
static inline int32x4_t round_to_int_neon(float32x4_t scaled)
{
#if defined(__aarch64__)
    return vcvtnq_s32_f32(scaled);
#else
    return vcvtq_s32_f32(scaled);
#endif
}

// The 32-bit kernels' SSE path truncates (_mm_cvttps_epi32), so the NEON path of d32u24 does too.
static inline int32x4_t float_24_trunc_neon(float32x4_t s)
{
    const float32x4_t upper_bound = vdupq_n_f32(kNormalizedFloatMax);
    const float32x4_t lower_bound = vdupq_n_f32(kNormalizedFloatMin);

    float32x4_t clipped = clip(s, lower_bound, upper_bound);
    float32x4_t scaled = vmulq_f32(clipped, vdupq_n_f32(kSample24BitScaling));
    return vcvtq_s32_f32(scaled);
}

static inline int32x4_t float_24_neon(float32x4_t s)
{
    const float32x4_t upper_bound = vdupq_n_f32(kNormalizedFloatMax);
    const float32x4_t lower_bound = vdupq_n_f32(kNormalizedFloatMin);

    float32x4_t clipped = clip(s, lower_bound, upper_bound);
    float32x4_t scaled = vmulq_f32(clipped, vdupq_n_f32(kSample24BitScaling));
    return round_to_int_neon(scaled);
}

static inline int16x4_t float_16_neon(float32x4_t s)
{
    const float32x4_t upper_bound = vdupq_n_f32(kNormalizedFloatMax);
    const float32x4_t lower_bound = vdupq_n_f32(kNormalizedFloatMin);

    float32x4_t clipped = clip(s, lower_bound, upper_bound);
    float32x4_t scaled = vmulq_f32(clipped, vdupq_n_f32(kSample16BitScaling));
    return vmovn_s32(round_to_int_neon(scaled));
}
#endif

// Linear Congruential noise generator. From the music-dsp list
// less random than rand(), but good enough and 10x faster
static std::uint32_t gSeed = 22222;

static inline std::uint32_t fastRand()
{
    gSeed = (gSeed * 196314165) + 907633515;
    return gSeed;
}

// functions for native float sample data

void sampleMoveFloatLESSs(DefaultAudioSampleC* dst, std::byte* src, std::uint64_t nsamples, std::uint64_t srcSkip)
{
    while (nsamples--) {
        *dst = *((float*)src);
        dst++;
        src += srcSkip;
    }
}

void sampleMoveDSFloatLE(std::byte* dst, DefaultAudioSampleC* src, std::uint64_t nsamples, std::uint64_t dstSkip, DitherState* state)
{
    while (nsamples--) {
        *((float*)dst) = *src;
        dst += dstSkip;
        src++;
    }
}

// NOTES on function naming:
//
// foo_bar_d<TYPE>_s<TYPE>
//
// the "d<TYPE>" component defines the destination type for the operation
// the "s<TYPE>" component defines the source type for the operation
//
// TYPE can be one of:
//
// S      - sample is a jack_default_audio_sample_t, currently (October 2008) a 32 bit floating point value
// Ss     - like S but reverse endian from the host CPU
// 32     - sample is a signed 32 bit integer value
// 32u24  - sample is a signed 32 bit integer value, but data is in upper 24 bits only
// 32u24s - like 32u24 but reverse endian from the host CPU
// 32l24  - sample is a signed 32 bit integer value, but data is in lower 24 bits only
// 32l24s - like 32l24 but reverse endian from the host CPU
// 24     - sample is a signed 24 bit integer value
// 24s    - like 24 but reverse endian from the host CPU
// 16     - sample is a signed 16 bit integer value
// 16s    - like 16 but reverse endian from the host CPU
//
// For obvious reasons, the reverse endian versions only show as source types.
//
// This covers all known sample formats at 16 bits or larger.

// functions for native integer sample data

void sampleMoveD32SSs(std::byte* dst, DefaultAudioSampleC* src, std::uint64_t nsamples, std::uint64_t dstSkip, DitherState* state)
{
    while (nsamples--) {
        std::int32_t z;
        float32(*src, z);
#if __BYTE_ORDER == __LITTLE_ENDIAN
        dst[0] = static_cast<std::byte>(z >> 24);
        dst[1] = static_cast<std::byte>(z >> 16);
        dst[2] = static_cast<std::byte>(z >> 8);
        dst[3] = static_cast<std::byte>(z);
#elif __BYTE_ORDER == __BIG_ENDIAN
        dst[0] = static_cast<std::byte>(z);
        dst[1] = static_cast<std::byte>(z >> 8);
        dst[2] = static_cast<std::byte>(z >> 16);
        dst[3] = static_cast<std::byte>(z >> 24);
#endif
        dst += dstSkip;
        src++;
    }
}

void sampleMoveD32SS(std::byte* dst, DefaultAudioSampleC* src, std::uint64_t nsamples, std::uint64_t dstSkip, DitherState* state)
{
    while (nsamples--) {
        float32(*src, *(std::int32_t*)dst);
        dst += dstSkip;
        src++;
    }
}

void sampleMoveD32u24SSs(std::byte* dst, DefaultAudioSampleC* src, std::uint64_t nsamples, std::uint64_t dstSkip, DitherState* state)
{
#if defined(__ARM_NEON__) || defined(__ARM_NEON)
    std::uint64_t unrolled = nsamples / 4;
    nsamples = nsamples & 3;

    while (unrolled--) {
        float32x4_t samples = vld1q_f32(src);
        int32x4_t converted = float_24_neon(samples);
        int32x4_t shifted = vshlq_n_s32(converted, 8);
        shifted = vreinterpretq_s32_u8(vrev32q_u8(vreinterpretq_u8_s32(shifted)));

        switch (dstSkip) {
        case 4:
            vst1q_s32((std::int32_t*)dst, shifted);
            break;
        default:
            vst1q_lane_s32((std::int32_t*)(dst), shifted, 0);
            vst1q_lane_s32((std::int32_t*)(dst + dstSkip), shifted, 1);
            vst1q_lane_s32((std::int32_t*)(dst + 2 * dstSkip), shifted, 2);
            vst1q_lane_s32((std::int32_t*)(dst + 3 * dstSkip), shifted, 3);
            break;
        }
        dst += 4 * dstSkip;
        src += 4;
    }
#endif

    std::int32_t z;

    while (nsamples--) {

        float24u32(*src, z);

#if __BYTE_ORDER == __LITTLE_ENDIAN
        dst[0] = static_cast<std::byte>(z >> 24);
        dst[1] = static_cast<std::byte>(z >> 16);
        dst[2] = static_cast<std::byte>(z >> 8);
        dst[3] = static_cast<std::byte>(z);
#elif __BYTE_ORDER == __BIG_ENDIAN
        dst[0] = static_cast<std::byte>(z);
        dst[1] = static_cast<std::byte>(z >> 8);
        dst[2] = static_cast<std::byte>(z >> 16);
        dst[3] = static_cast<std::byte>(z >> 24);
#endif
        dst += dstSkip;
        src++;
    }
}

void sampleMoveD32u24SS(std::byte* dst, DefaultAudioSampleC* src, std::uint64_t nsamples, std::uint64_t dstSkip, DitherState* state)
{
#if defined(__SSE2__)
    __m128 intMax = _mm_set1_ps(kSample24BitMaxF);
    __m128 intMin = _mm_sub_ps(_mm_setzero_ps(), intMax);
    __m128 factor = intMax;

    std::uint64_t unrolled = nsamples / 4;
    nsamples = nsamples & 3;

    while (unrolled--) {
        __m128 in = _mm_load_ps(src);
        __m128 scaled = _mm_mul_ps(in, factor);
        __m128 clipped = clip(scaled, intMin, intMax);

        __m128i y = _mm_cvttps_epi32(clipped);
        __m128i shifted = _mm_slli_epi32(y, 8);

#ifdef __SSE4_1__
        *(std::int32_t*)dst = _mm_extract_epi32(shifted, 0);
        *(std::int32_t*)(dst + dstSkip) = _mm_extract_epi32(shifted, 1);
        *(std::int32_t*)(dst + 2 * dstSkip) = _mm_extract_epi32(shifted, 2);
        *(std::int32_t*)(dst + 3 * dstSkip) = _mm_extract_epi32(shifted, 3);
#else
        __m128i shuffled1 = _mm_shuffle_epi32(shifted, _MM_SHUFFLE(0, 3, 2, 1));
        __m128i shuffled2 = _mm_shuffle_epi32(shifted, _MM_SHUFFLE(1, 0, 3, 2));
        __m128i shuffled3 = _mm_shuffle_epi32(shifted, _MM_SHUFFLE(2, 1, 0, 3));

        _mm_store_ss((float*)dst, (__m128)shifted);

        _mm_store_ss((float*)(dst + dstSkip), (__m128)shuffled1);
        _mm_store_ss((float*)(dst + 2 * dstSkip), (__m128)shuffled2);
        _mm_store_ss((float*)(dst + 3 * dstSkip), (__m128)shuffled3);
#endif
        dst += 4 * dstSkip;

        src += 4;
    }

    while (nsamples--) {
        __m128 in = _mm_load_ss(src);
        __m128 scaled = _mm_mul_ss(in, factor);
        __m128 clipped = _mm_min_ss(intMax, _mm_max_ss(scaled, intMin));

        std::int32_t y = _mm_cvttss_si32(clipped);
        *((std::int32_t*)dst) = y << 8;

        dst += dstSkip;
        src++;
    }

#elif defined(__ARM_NEON__) || defined(__ARM_NEON)
    std::uint64_t unrolled = nsamples / 4;
    nsamples = nsamples & 3;

    while (unrolled--) {
        float32x4_t samples = vld1q_f32(src);
        int32x4_t converted = float_24_trunc_neon(samples);
        int32x4_t shifted = vshlq_n_s32(converted, 8);

        switch (dstSkip) {
        case 4:
            vst1q_s32((std::int32_t*)dst, shifted);
            break;
        default:
            vst1q_lane_s32((std::int32_t*)(dst), shifted, 0);
            vst1q_lane_s32((std::int32_t*)(dst + dstSkip), shifted, 1);
            vst1q_lane_s32((std::int32_t*)(dst + 2 * dstSkip), shifted, 2);
            vst1q_lane_s32((std::int32_t*)(dst + 3 * dstSkip), shifted, 3);
            break;
        }
        dst += 4 * dstSkip;

        src += 4;
    }
#endif

#if defined(__ARM_NEON__) || defined(__ARM_NEON)
    // The same clip and truncation as the SSE tail above, so the tail agrees with the vector part.
    while (nsamples--) {
        const float scaled = fminf(kSample24BitMaxF, fmaxf(*src * kSample24BitMaxF, -kSample24BitMaxF));
        *((std::int32_t*)dst) = ((std::int32_t)scaled) << 8;
        dst += dstSkip;
        src++;
    }
#elif !defined(__SSE2__)
    while (nsamples--) {
        float24u32(*src, *((std::int32_t*)dst));
        dst += dstSkip;
        src++;
    }
#endif
}

void sampleMoveDSS32u24s(DefaultAudioSampleC* dst, std::byte* src, std::uint64_t nsamples, std::uint64_t srcSkip)
{
#if defined(__ARM_NEON__) || defined(__ARM_NEON)
    float32x4_t factor = vdupq_n_f32(1.0 / kSample24BitScaling);
    std::uint64_t unrolled = nsamples / 4;
    while (unrolled--) {
        int32x4_t src128;
        switch (srcSkip) {
        case 4:
            src128 = vld1q_s32((std::int32_t*)src);
            break;
        case 8:
            src128 = vld2q_s32((std::int32_t*)src).val[0];
            break;
        default:
            src128 = vld1q_lane_s32((std::int32_t*)src, src128, 0);
            src128 = vld1q_lane_s32((std::int32_t*)(src + srcSkip), src128, 1);
            src128 = vld1q_lane_s32((std::int32_t*)(src + 2 * srcSkip), src128, 2);
            src128 = vld1q_lane_s32((std::int32_t*)(src + 3 * srcSkip), src128, 3);
            break;
        }
        src128 = vreinterpretq_s32_u8(vrev32q_u8(vreinterpretq_u8_s32(src128)));
        int32x4_t shifted = vshrq_n_s32(src128, 8);
        float32x4_t as_float = vcvtq_f32_s32(shifted);
        float32x4_t divided = vmulq_f32(as_float, factor);
        vst1q_f32(dst, divided);

        src += 4 * srcSkip;
        dst += 4;
    }
    nsamples = nsamples & 3;
#endif

    // ALERT: signed sign-extension portability !!!

    const DefaultAudioSampleC scaling = 1.0 / kSample24BitScaling;

    while (nsamples--) {
        std::int32_t x;
#if __BYTE_ORDER == __LITTLE_ENDIAN
        x = std::to_integer<std::uint8_t>(src[0]);
        x <<= 8;
        x |= std::to_integer<std::uint8_t>(src[1]);
        x <<= 8;
        x |= std::to_integer<std::uint8_t>(src[2]);
        x <<= 8;
        x |= std::to_integer<std::uint8_t>(src[3]);
#elif __BYTE_ORDER == __BIG_ENDIAN
        x = std::to_integer<std::uint8_t>(src[3]);
        x <<= 8;
        x |= std::to_integer<std::uint8_t>(src[2]);
        x <<= 8;
        x |= std::to_integer<std::uint8_t>(src[1]);
        x <<= 8;
        x |= std::to_integer<std::uint8_t>(src[0]);
#endif
        *dst = (x >> 8) * scaling;
        dst++;
        src += srcSkip;
    }
}

void sampleMoveDSS32u24(DefaultAudioSampleC* dst, std::byte* src, std::uint64_t nsamples, std::uint64_t srcSkip)
{
#if defined(__SSE2__)
    std::uint64_t unrolled = nsamples / 4;
    static float invSampleMax24bit = 1.0 / kSample24BitScaling;
    __m128 factor = _mm_set1_ps(invSampleMax24bit);
    while (unrolled--) {
        std::int32_t i1 = *((std::int32_t*)src);
        src += srcSkip;
        std::int32_t i2 = *((std::int32_t*)src);
        src += srcSkip;
        std::int32_t i3 = *((std::int32_t*)src);
        src += srcSkip;
        std::int32_t i4 = *((std::int32_t*)src);
        src += srcSkip;

        __m128i src = _mm_set_epi32(i4, i3, i2, i1);
        __m128i shifted = _mm_srai_epi32(src, 8);

        __m128 asFloat = _mm_cvtepi32_ps(shifted);
        __m128 divided = _mm_mul_ps(asFloat, factor);

        _mm_storeu_ps(dst, divided);

        dst += 4;
    }
    nsamples = nsamples & 3;
#elif defined(__ARM_NEON__) || defined(__ARM_NEON)
    std::uint64_t unrolled = nsamples / 4;
    float32x4_t factor = vdupq_n_f32(1.0 / kSample24BitScaling);
    while (unrolled--) {
        int32x4_t src128;
        switch (srcSkip) {
        case 4:
            src128 = vld1q_s32((std::int32_t*)src);
            break;
        case 8:
            src128 = vld2q_s32((std::int32_t*)src).val[0];
            break;
        default:
            src128 = vld1q_lane_s32((std::int32_t*)src, src128, 0);
            src128 = vld1q_lane_s32((std::int32_t*)(src + srcSkip), src128, 1);
            src128 = vld1q_lane_s32((std::int32_t*)(src + 2 * srcSkip), src128, 2);
            src128 = vld1q_lane_s32((std::int32_t*)(src + 3 * srcSkip), src128, 3);
            break;
        }
        int32x4_t shifted = vshrq_n_s32(src128, 8);
        float32x4_t as_float = vcvtq_f32_s32(shifted);
        float32x4_t divided = vmulq_f32(as_float, factor);
        vst1q_f32(dst, divided);

        src += 4 * srcSkip;
        dst += 4;
    }
    nsamples = nsamples & 3;
#endif

    // ALERT: signed sign-extension portability !!!

    const DefaultAudioSampleC scaling = 1.0 / kSample24BitScaling;
    while (nsamples--) {
        *dst = (*((std::int32_t*)src) >> 8) * scaling;
        dst++;
        src += srcSkip;
    }
}

void sampleMoveD32l24SSs(std::byte* dst, DefaultAudioSampleC* src, std::uint64_t nsamples, std::uint64_t dstSkip, DitherState* state)
{
#if defined(__ARM_NEON__) || defined(__ARM_NEON)
    std::uint64_t unrolled = nsamples / 4;
    nsamples = nsamples & 3;

    while (unrolled--) {
        float32x4_t samples = vld1q_f32(src);
        int32x4_t converted = float_24_neon(samples);
        converted = vreinterpretq_s32_u8(vrev32q_u8(vreinterpretq_u8_s32(converted)));

        switch (dstSkip) {
        case 4:
            vst1q_s32((std::int32_t*)dst, converted);
            break;
        default:
            vst1q_lane_s32((std::int32_t*)(dst), converted, 0);
            vst1q_lane_s32((std::int32_t*)(dst + dstSkip), converted, 1);
            vst1q_lane_s32((std::int32_t*)(dst + 2 * dstSkip), converted, 2);
            vst1q_lane_s32((std::int32_t*)(dst + 3 * dstSkip), converted, 3);
            break;
        }
        dst += 4 * dstSkip;
        src += 4;
    }
#endif

    std::int32_t z;

    while (nsamples--) {

        float24l32(*src, z);

#if __BYTE_ORDER == __LITTLE_ENDIAN
        dst[0] = static_cast<std::byte>(z >> 24);
        dst[1] = static_cast<std::byte>(z >> 16);
        dst[2] = static_cast<std::byte>(z >> 8);
        dst[3] = static_cast<std::byte>(z);
#elif __BYTE_ORDER == __BIG_ENDIAN
        dst[0] = static_cast<std::byte>(z);
        dst[1] = static_cast<std::byte>(z >> 8);
        dst[2] = static_cast<std::byte>(z >> 16);
        dst[3] = static_cast<std::byte>(z >> 24);
#endif
        dst += dstSkip;
        src++;
    }
}

void sampleMoveD32l24SS(std::byte* dst, DefaultAudioSampleC* src, std::uint64_t nsamples, std::uint64_t dstSkip, DitherState* state)
{
#if defined(__SSE2__)
    __m128 intMax = _mm_set1_ps(kSample24BitMaxF);
    __m128 intMin = _mm_sub_ps(_mm_setzero_ps(), intMax);
    __m128 factor = intMax;

    std::uint64_t unrolled = nsamples / 4;
    nsamples = nsamples & 3;

    while (unrolled--) {
        __m128 in = _mm_load_ps(src);
        __m128 scaled = _mm_mul_ps(in, factor);
        __m128 clipped = clip(scaled, intMin, intMax);

        __m128i shifted = _mm_cvttps_epi32(clipped);

#ifdef __SSE4_1__
        *(std::int32_t*)dst = _mm_extract_epi32(shifted, 0);
        *(std::int32_t*)(dst + dstSkip) = _mm_extract_epi32(shifted, 1);
        *(std::int32_t*)(dst + 2 * dstSkip) = _mm_extract_epi32(shifted, 2);
        *(std::int32_t*)(dst + 3 * dstSkip) = _mm_extract_epi32(shifted, 3);
#else
        __m128i shuffled1 = _mm_shuffle_epi32(shifted, _MM_SHUFFLE(0, 3, 2, 1));
        __m128i shuffled2 = _mm_shuffle_epi32(shifted, _MM_SHUFFLE(1, 0, 3, 2));
        __m128i shuffled3 = _mm_shuffle_epi32(shifted, _MM_SHUFFLE(2, 1, 0, 3));

        _mm_store_ss((float*)dst, (__m128)shifted);

        _mm_store_ss((float*)(dst + dstSkip), (__m128)shuffled1);
        _mm_store_ss((float*)(dst + 2 * dstSkip), (__m128)shuffled2);
        _mm_store_ss((float*)(dst + 3 * dstSkip), (__m128)shuffled3);
#endif
        dst += 4 * dstSkip;

        src += 4;
    }

    while (nsamples--) {
        __m128 in = _mm_load_ss(src);
        __m128 scaled = _mm_mul_ss(in, factor);
        __m128 clipped = _mm_min_ss(intMax, _mm_max_ss(scaled, intMin));

        std::int32_t y = _mm_cvttss_si32(clipped);
        *((std::int32_t*)dst) = y << 8;

        dst += dstSkip;
        src++;
    }
#elif defined(__ARM_NEON__) || defined(__ARM_NEON)
    std::uint64_t unrolled = nsamples / 4;
    nsamples = nsamples & 3;

    while (unrolled--) {
        float32x4_t samples = vld1q_f32(src);
        int32x4_t converted = float_24_neon(samples);

        switch (dstSkip) {
        case 4:
            vst1q_s32((std::int32_t*)dst, converted);
            break;
        default:
            vst1q_lane_s32((std::int32_t*)(dst), converted, 0);
            vst1q_lane_s32((std::int32_t*)(dst + dstSkip), converted, 1);
            vst1q_lane_s32((std::int32_t*)(dst + 2 * dstSkip), converted, 2);
            vst1q_lane_s32((std::int32_t*)(dst + 3 * dstSkip), converted, 3);
            break;
        }
        dst += 4 * dstSkip;

        src += 4;
    }
#endif

#if !defined(__SSE2__)
    while (nsamples--) {
        float24l32(*src, *((std::int32_t*)dst));
        dst += dstSkip;
        src++;
    }
#endif
}

void sampleMoveDSS32s(DefaultAudioSampleC* dst, std::byte* src, std::uint64_t nsamples, std::uint64_t srcSkip)
{
    const DefaultAudioSampleC scaling = 1.0 / kSample32BitScaling;
    while (nsamples--) {
        std::int32_t x;
#if __BYTE_ORDER == __LITTLE_ENDIAN
        x = std::to_integer<std::uint8_t>(src[0]);
        x <<= 8;
        x |= std::to_integer<std::uint8_t>(src[1]);
        x <<= 8;
        x |= std::to_integer<std::uint8_t>(src[2]);
        x <<= 8;
        x |= std::to_integer<std::uint8_t>(src[3]);
#elif __BYTE_ORDER == __BIG_ENDIAN
        x = std::to_integer<std::uint8_t>(src[3]);
        x <<= 8;
        x |= std::to_integer<std::uint8_t>(src[2]);
        x <<= 8;
        x |= std::to_integer<std::uint8_t>(src[1]);
        x <<= 8;
        x |= std::to_integer<std::uint8_t>(src[0]);
#endif
        double extended = x * scaling;
        *dst = (float)extended;
        dst++;
        src += srcSkip;
    }
}

void sampleMoveDSS32l24s(DefaultAudioSampleC* dst, std::byte* src, std::uint64_t nsamples, std::uint64_t srcSkip)
{
#if defined(__ARM_NEON__) || defined(__ARM_NEON)
    float32x4_t factor = vdupq_n_f32(1.0 / kSample24BitScaling);
    std::uint64_t unrolled = nsamples / 4;
    while (unrolled--) {
        uint32x4_t src128;
        switch (srcSkip) {
        case 4:
            src128 = vld1q_u32((std::uint32_t*)src);
            break;
        case 8:
            src128 = vld2q_u32((std::uint32_t*)src).val[0];
            break;
        default:
            src128 = vld1q_lane_u32((std::uint32_t*)src, src128, 0);
            src128 = vld1q_lane_u32((std::uint32_t*)(src + srcSkip), src128, 1);
            src128 = vld1q_lane_u32((std::uint32_t*)(src + 2 * srcSkip), src128, 2);
            src128 = vld1q_lane_u32((std::uint32_t*)(src + 3 * srcSkip), src128, 3);
            break;
        }
        src128 = vreinterpretq_u32_u8(vrev32q_u8(vreinterpretq_u8_u32(src128)));
        uint32x4_t toupper = vshlq_n_u32(src128, 8);
        int32x4_t shifted = vshrq_n_s32((int32x4_t)toupper, 8);
        float32x4_t as_float = vcvtq_f32_s32(shifted);
        float32x4_t divided = vmulq_f32(as_float, factor);
        vst1q_f32(dst, divided);

        src += 4 * srcSkip;
        dst += 4;
    }
    nsamples = nsamples & 3;
#endif

    // ALERT: signed sign-extension portability !!!

    const DefaultAudioSampleC scaling = 1.0 / kSample24BitScaling;

    while (nsamples--) {
        std::int32_t x;
#if __BYTE_ORDER == __LITTLE_ENDIAN
        x = std::to_integer<std::uint8_t>(src[0]);
        x <<= 8;
        x |= std::to_integer<std::uint8_t>(src[1]);
        x <<= 8;
        x |= std::to_integer<std::uint8_t>(src[2]);
        x <<= 8;
        x |= std::to_integer<std::uint8_t>(src[3]);
#elif __BYTE_ORDER == __BIG_ENDIAN
        x = std::to_integer<std::uint8_t>(src[3]);
        x <<= 8;
        x |= std::to_integer<std::uint8_t>(src[2]);
        x <<= 8;
        x |= std::to_integer<std::uint8_t>(src[1]);
        x <<= 8;
        x |= std::to_integer<std::uint8_t>(src[0]);
#endif
        *dst = (x >> 0) * scaling;
        dst++;
        src += srcSkip;
    }
}

void sampleMoveDSS32(DefaultAudioSampleC* dst, std::byte* src, std::uint64_t nsamples, std::uint64_t srcSkip)
{
    const double scaling = 1.0 / kSample32BitScaling;
    while (nsamples--) {
        std::int32_t val = (*((std::int32_t*)src));
        double extended = val * scaling;
        *dst = (float)extended;
        dst++;
        src += srcSkip;
    }
}

void sampleMoveDSS32l24(DefaultAudioSampleC* dst, std::byte* src, std::uint64_t nsamples, std::uint64_t srcSkip)
{
#if defined(__SSE2__)
    std::uint64_t unrolled = nsamples / 4;
    static float invSampleMax24bit = 1.0 / kSample24BitScaling;
    __m128 factor = _mm_set1_ps(invSampleMax24bit);
    while (unrolled--) {
        std::int32_t i1 = *((std::int32_t*)src);
        src += srcSkip;
        std::int32_t i2 = *((std::int32_t*)src);
        src += srcSkip;
        std::int32_t i3 = *((std::int32_t*)src);
        src += srcSkip;
        std::int32_t i4 = *((std::int32_t*)src);
        src += srcSkip;

        __m128i shifted = _mm_set_epi32(i4, i3, i2, i1);

        __m128 asFloat = _mm_cvtepi32_ps(shifted);
        __m128 divided = _mm_mul_ps(asFloat, factor);

        _mm_storeu_ps(dst, divided);

        dst += 4;
    }
    nsamples = nsamples & 3;
#elif defined(__ARM_NEON__) || defined(__ARM_NEON)
    std::uint64_t unrolled = nsamples / 4;
    float32x4_t factor = vdupq_n_f32(1.0 / kSample24BitScaling);
    while (unrolled--) {
        uint32x4_t src128;
        switch (srcSkip) {
        case 4:
            src128 = vld1q_u32((std::uint32_t*)src);
            break;
        case 8:
            src128 = vld2q_u32((std::uint32_t*)src).val[0];
            break;
        default:
            src128 = vld1q_lane_u32((std::uint32_t*)src, src128, 0);
            src128 = vld1q_lane_u32((std::uint32_t*)(src + srcSkip), src128, 1);
            src128 = vld1q_lane_u32((std::uint32_t*)(src + 2 * srcSkip), src128, 2);
            src128 = vld1q_lane_u32((std::uint32_t*)(src + 3 * srcSkip), src128, 3);
            break;
        }
        // Sign extension by moving to upper as unsigned, then down
        uint32x4_t toupper = vshlq_n_u32(src128, 8);
        int32x4_t shifted = vshrq_n_s32((int32x4_t)toupper, 8);
        float32x4_t as_float = vcvtq_f32_s32(shifted);
        float32x4_t divided = vmulq_f32(as_float, factor);
        vst1q_f32(dst, divided);

        src += 4 * srcSkip;
        dst += 4;
    }
    nsamples = nsamples & 3;
#endif

    // ALERT: signed sign-extension portability !!!

    const DefaultAudioSampleC scaling = 1.0 / kSample24BitScaling;
    while (nsamples--) {
        std::uint32_t val = (*((std::uint32_t*)src));
        if (val & 0x800000u)
            val |= 0xFF000000u;
        *dst = (*((std::int32_t*)&val)) * scaling;
        dst++;
        src += srcSkip;
    }
}

void sampleMoveD24SSs(std::byte* dst, DefaultAudioSampleC* src, std::uint64_t nsamples, std::uint64_t dstSkip, DitherState* state)
{
#if defined(__ARM_NEON__) || defined(__ARM_NEON)
    std::uint64_t unrolled = nsamples / 4;
    while (unrolled--) {
        std::int32_t i;
        std::array<std::int32_t, 4> z;
        float32x4_t samples = vld1q_f32(src);
        int32x4_t converted = float_24_neon(samples);
        converted = vreinterpretq_s32_u8(vrev32q_u8(vreinterpretq_u8_s32(converted)));
        vst1q_s32(z.data(), converted);

        for (i = 0; i != 4; ++i) {
            memcpy(dst, ((std::byte*)(z.data() + i)) + 1, 3);
            dst += dstSkip;
        }
        src += 4;
    }
    nsamples = nsamples & 3;
#endif

    std::int32_t z;

    while (nsamples--) {
        float24(*src, z);
#if __BYTE_ORDER == __LITTLE_ENDIAN
        dst[0] = static_cast<std::byte>(z >> 16);
        dst[1] = static_cast<std::byte>(z >> 8);
        dst[2] = static_cast<std::byte>(z);
#elif __BYTE_ORDER == __BIG_ENDIAN
        dst[0] = static_cast<std::byte>(z);
        dst[1] = static_cast<std::byte>(z >> 8);
        dst[2] = static_cast<std::byte>(z >> 16);
#endif
        dst += dstSkip;
        src++;
    }
}

void sampleMoveD24SS(std::byte* dst, DefaultAudioSampleC* src, std::uint64_t nsamples, std::uint64_t dstSkip, DitherState* state)
{
#if defined(__SSE2__)
    _MM_SET_ROUNDING_MODE(_MM_ROUND_NEAREST);
    while (nsamples >= 4) {
        std::int32_t i;
        std::array<std::int32_t, 4> z;
        __m128 samples = _mm_loadu_ps(src);
        __m128i converted = float24Sse(samples);

#ifdef __SSE4_1__
        z[0] = _mm_extract_epi32(converted, 0);
        z[1] = _mm_extract_epi32(converted, 1);
        z[2] = _mm_extract_epi32(converted, 2);
        z[3] = _mm_extract_epi32(converted, 3);
#else
        __m128i shuffled1 = _mm_shuffle_epi32(converted, _MM_SHUFFLE(0, 3, 2, 1));
        __m128i shuffled2 = _mm_shuffle_epi32(converted, _MM_SHUFFLE(1, 0, 3, 2));
        __m128i shuffled3 = _mm_shuffle_epi32(converted, _MM_SHUFFLE(2, 1, 0, 3));

        _mm_store_ss((float*)z.data(), (__m128)converted);
        _mm_store_ss((float*)z.data() + 1, (__m128)shuffled1);
        _mm_store_ss((float*)z.data() + 2, (__m128)shuffled2);
        _mm_store_ss((float*)z.data() + 3, (__m128)shuffled3);
#endif

        for (i = 0; i != 4; ++i) {
            memcpy(dst, z.data() + i, 3);
            dst += dstSkip;
        }

        nsamples -= 4;
        src += 4;
    }
#elif defined(__ARM_NEON__) || defined(__ARM_NEON)
    std::uint64_t unrolled = nsamples / 4;
    while (unrolled--) {
        std::int32_t i;
        std::array<std::int32_t, 4> z;
        float32x4_t samples = vld1q_f32(src);
        int32x4_t converted = float_24_neon(samples);
        vst1q_s32(z.data(), converted);

        for (i = 0; i != 4; ++i) {
            memcpy(dst, z.data() + i, 3);
            dst += dstSkip;
        }
        src += 4;
    }
    nsamples = nsamples & 3;
#endif

    std::int32_t z;

    while (nsamples--) {
        float24(*src, z);
#if __BYTE_ORDER == __LITTLE_ENDIAN
        memcpy(dst, &z, 3);
#elif __BYTE_ORDER == __BIG_ENDIAN
        memcpy(dst, (std::byte*)&z + 1, 3);
#endif
        dst += dstSkip;
        src++;
    }
}

void sampleMoveDSS24s(DefaultAudioSampleC* dst, std::byte* src, std::uint64_t nsamples, std::uint64_t srcSkip)
{
    const DefaultAudioSampleC scaling = 1.0 / kSample24BitScaling;

#if defined(__ARM_NEON__) || defined(__ARM_NEON)
    // we shift 8 to the right by dividing by 256.0 -> no sign extra handling
    const float32x4_t vscaling = vdupq_n_f32(scaling / 256.0);
    std::array<std::int32_t, 4> x;
    memset(x.data(), 0, sizeof(x));
    std::uint64_t unrolled = nsamples / 4;
    while (unrolled--) {
#if __BYTE_ORDER == __BIG_ENDIAN // ARM big endian??
        // right aligned / inverse sequence below -> *256
        memcpy(((std::byte*)&x[0]) + 1, src, 3);
        memcpy(((std::byte*)&x[1]) + 1, src + srcSkip, 3);
        memcpy(((std::byte*)&x[2]) + 1, src + 2 * srcSkip, 3);
        memcpy(((std::byte*)&x[3]) + 1, src + 3 * srcSkip, 3);
#else
        memcpy(&x[0], src, 3);
        memcpy(&x[1], src + srcSkip, 3);
        memcpy(&x[2], src + 2 * srcSkip, 3);
        memcpy(&x[3], src + 3 * srcSkip, 3);
#endif
        src += 4 * srcSkip;

        int32x4_t source = vld1q_s32(x.data());
        source = vreinterpretq_s32_u8(vrev32q_u8(vreinterpretq_u8_s32(source)));
        float32x4_t converted = vcvtq_f32_s32(source);
        float32x4_t scaled = vmulq_f32(converted, vscaling);
        vst1q_f32(dst, scaled);
        dst += 4;
    }
    nsamples = nsamples & 3;
#endif

    // ALERT: signed sign-extension portability !!!

    while (nsamples--) {
        std::int32_t x;
#if __BYTE_ORDER == __LITTLE_ENDIAN
        x = std::to_integer<std::uint8_t>(src[0]);
        x <<= 8;
        x |= std::to_integer<std::uint8_t>(src[1]);
        x <<= 8;
        x |= std::to_integer<std::uint8_t>(src[2]);
        // correct sign bit and the rest of the top byte
        if (std::to_integer<std::uint8_t>(src[0]) & 0x80) {
            x |= 0xff << 24;
        }
#elif __BYTE_ORDER == __BIG_ENDIAN
        x = std::to_integer<std::uint8_t>(src[2]);
        x <<= 8;
        x |= std::to_integer<std::uint8_t>(src[1]);
        x <<= 8;
        x |= std::to_integer<std::uint8_t>(src[0]);
        // correct sign bit and the rest of the top byte
        if (src[2] & 0x80) {
            x |= 0xff << 24;
        }
#endif
        *dst = x * scaling;
        dst++;
        src += srcSkip;
    }
}

void sampleMoveDSS24(DefaultAudioSampleC* dst, std::byte* src, std::uint64_t nsamples, std::uint64_t srcSkip)
{
    const DefaultAudioSampleC scaling = 1.f / kSample24BitScaling;

#if defined(__SSE2__)
    const __m128 scalingBlock = _mm_set_ps1(scaling);
    while (nsamples >= 4) {
        std::int32_t x0, x1, x2, x3;

        memcpy((std::byte*)&x0 + 1, src, 3);
        memcpy((std::byte*)&x1 + 1, src + srcSkip, 3);
        memcpy((std::byte*)&x2 + 1, src + 2 * srcSkip, 3);
        memcpy((std::byte*)&x3 + 1, src + 3 * srcSkip, 3);
        src += 4 * srcSkip;

        const __m128i blockI = _mm_set_epi32(x3, x2, x1, x0);
        const __m128i shifted = _mm_srai_epi32(blockI, 8);
        const __m128 converted = _mm_cvtepi32_ps(shifted);
        const __m128 scaled = _mm_mul_ps(converted, scalingBlock);
        _mm_storeu_ps(dst, scaled);
        dst += 4;
        nsamples -= 4;
    }
#elif defined(__ARM_NEON__) || defined(__ARM_NEON)
    // we shift 8 to the right by dividing by 256.0 -> no sign extra handling
    const float32x4_t vscaling = vdupq_n_f32(scaling / 256.0);
    std::array<std::int32_t, 4> x;
    memset(x.data(), 0, sizeof(x));
    std::uint64_t unrolled = nsamples / 4;
    while (unrolled--) {
#if __BYTE_ORDER == __BIG_ENDIAN // ARM big endian??
        // left aligned -> *256
        memcpy(&x[0], src, 3);
        memcpy(&x[1], src + srcSkip, 3);
        memcpy(&x[2], src + 2 * srcSkip, 3);
        memcpy(&x[3], src + 3 * srcSkip, 3);
#else
        memcpy(((std::byte*)&x[0]) + 1, src, 3);
        memcpy(((std::byte*)&x[1]) + 1, src + srcSkip, 3);
        memcpy(((std::byte*)&x[2]) + 1, src + 2 * srcSkip, 3);
        memcpy(((std::byte*)&x[3]) + 1, src + 3 * srcSkip, 3);
#endif
        src += 4 * srcSkip;

        int32x4_t source = vld1q_s32(x.data());
        float32x4_t converted = vcvtq_f32_s32(source);
        float32x4_t scaled = vmulq_f32(converted, vscaling);
        vst1q_f32(dst, scaled);
        dst += 4;
    }
    nsamples = nsamples & 3;
#endif

    while (nsamples--) {
        std::int32_t x;
#if __BYTE_ORDER == __LITTLE_ENDIAN
        memcpy((std::byte*)&x + 1, src, 3);
#elif __BYTE_ORDER == __BIG_ENDIAN
        memcpy(&x, src, 3);
#endif
        x >>= 8;
        *dst = x * scaling;
        dst++;
        src += srcSkip;
    }
}


void sampleMoveD16SSs(std::byte* dst, DefaultAudioSampleC* src, std::uint64_t nsamples, std::uint64_t dstSkip, DitherState* state)
{
#if defined(__ARM_NEON__) || defined(__ARM_NEON)
    std::uint64_t unrolled = nsamples / 4;
    nsamples = nsamples & 3;

    while (unrolled--) {
        float32x4_t samples = vld1q_f32(src);
        int16x4_t converted = float_16_neon(samples);
        converted = vreinterpret_s16_u8(vrev16_u8(vreinterpret_u8_s16(converted)));

        switch (dstSkip) {
        case 2:
            vst1_s16((std::int16_t*)dst, converted);
            break;
        default:
            vst1_lane_s16((std::int16_t*)(dst), converted, 0);
            vst1_lane_s16((std::int16_t*)(dst + dstSkip), converted, 1);
            vst1_lane_s16((std::int16_t*)(dst + 2 * dstSkip), converted, 2);
            vst1_lane_s16((std::int16_t*)(dst + 3 * dstSkip), converted, 3);
            break;
        }
        dst += 4 * dstSkip;
        src += 4;
    }
#endif
    std::int16_t tmp;

    while (nsamples--) {
        // float_16 (*src, tmp);

        if (*src <= kNormalizedFloatMin) {
            tmp = kSample16BitMin;
        } else if (*src >= kNormalizedFloatMax) {
            tmp = kSample16BitMax;
        } else {
            tmp = (std::int16_t)fRound(*src * kSample16BitScaling);
        }

#if __BYTE_ORDER == __LITTLE_ENDIAN
        dst[0] = static_cast<std::byte>(tmp >> 8);
        dst[1] = static_cast<std::byte>(tmp);
#elif __BYTE_ORDER == __BIG_ENDIAN
        dst[0] = static_cast<std::byte>(tmp);
        dst[1] = static_cast<std::byte>(tmp >> 8);
#endif
        dst += dstSkip;
        src++;
    }
}

void sampleMoveD16SS(std::byte* dst, DefaultAudioSampleC* src, std::uint64_t nsamples, std::uint64_t dstSkip, DitherState* state)
{
#if defined(__ARM_NEON__) || defined(__ARM_NEON)
    std::uint64_t unrolled = nsamples / 4;
    nsamples = nsamples & 3;

    while (unrolled--) {
        float32x4_t samples = vld1q_f32(src);
        int16x4_t converted = float_16_neon(samples);

        switch (dstSkip) {
        case 2:
            vst1_s16((std::int16_t*)dst, converted);
            break;
        default:
            vst1_lane_s16((std::int16_t*)(dst), converted, 0);
            vst1_lane_s16((std::int16_t*)(dst + dstSkip), converted, 1);
            vst1_lane_s16((std::int16_t*)(dst + 2 * dstSkip), converted, 2);
            vst1_lane_s16((std::int16_t*)(dst + 3 * dstSkip), converted, 3);
            break;
        }
        dst += 4 * dstSkip;
        src += 4;
    }
#endif
    while (nsamples--) {
        float16(*src, *((std::int16_t*)dst));
        dst += dstSkip;
        src++;
    }
}

void sampleMoveDitherRectD16SSs(std::byte* dst, DefaultAudioSampleC* src, std::uint64_t nsamples, std::uint64_t dstSkip, DitherState* state)
{
    DefaultAudioSampleC val;
    std::int16_t tmp;

    while (nsamples--) {
        val = (*src * kSample16BitScaling) + fastRand() / (float)UINT_MAX - 0.5f;
        float16Scaled(val, tmp);
#if __BYTE_ORDER == __LITTLE_ENDIAN
        dst[0] = static_cast<std::byte>(tmp >> 8);
        dst[1] = static_cast<std::byte>(tmp);
#elif __BYTE_ORDER == __BIG_ENDIAN
        dst[0] = static_cast<std::byte>(tmp);
        dst[1] = static_cast<std::byte>(tmp >> 8);
#endif
        dst += dstSkip;
        src++;
    }
}

void sampleMoveDitherRectD16SS(std::byte* dst, DefaultAudioSampleC* src, std::uint64_t nsamples, std::uint64_t dstSkip, DitherState* state)
{
    DefaultAudioSampleC val;

    while (nsamples--) {
        val = (*src * kSample16BitScaling) + fastRand() / (float)UINT_MAX - 0.5f;
        float16Scaled(val, *((std::int16_t*)dst));
        dst += dstSkip;
        src++;
    }
}

void sampleMoveDitherTriD16SSs(std::byte* dst, DefaultAudioSampleC* src, std::uint64_t nsamples, std::uint64_t dstSkip, DitherState* state)
{
    DefaultAudioSampleC val;
    std::int16_t tmp;

    while (nsamples--) {
        val = (*src * kSample16BitScaling) + ((float)fastRand() + (float)fastRand()) / (float)UINT_MAX - 1.0f;
        float16Scaled(val, tmp);

#if __BYTE_ORDER == __LITTLE_ENDIAN
        dst[0] = static_cast<std::byte>(tmp >> 8);
        dst[1] = static_cast<std::byte>(tmp);
#elif __BYTE_ORDER == __BIG_ENDIAN
        dst[0] = static_cast<std::byte>(tmp);
        dst[1] = static_cast<std::byte>(tmp >> 8);
#endif
        dst += dstSkip;
        src++;
    }
}

void sampleMoveDitherTriD16SS(std::byte* dst, DefaultAudioSampleC* src, std::uint64_t nsamples, std::uint64_t dstSkip, DitherState* state)
{
    DefaultAudioSampleC val;

    while (nsamples--) {
        val = (*src * kSample16BitScaling) + ((float)fastRand() + (float)fastRand()) / (float)UINT_MAX - 1.0f;
        float16Scaled(val, *((std::int16_t*)dst));
        dst += dstSkip;
        src++;
    }
}

void sampleMoveDitherShapedD16SSs(std::byte* dst, DefaultAudioSampleC* src, std::uint64_t nsamples, std::uint64_t dstSkip, DitherState* state)
{
    DefaultAudioSampleC x;
    DefaultAudioSampleC xe; // the innput sample - filtered error
    DefaultAudioSampleC xp; // x'
    float r;
    float rm1 = state->m_rm1;
    std::uint32_t idx = state->m_idx;
    std::int16_t tmp;

    while (nsamples--) {
        x = *src * kSample16BitScaling;
        r = ((float)fastRand() + (float)fastRand()) / (float)UINT_MAX - 1.0f;
        // Filter the error with Lipshitz's minimally audible FIR:
        // [2.033 -2.165 1.959 -1.590 0.6149]
        xe = x
            - state->m_e[idx] * 2.033f
            + state->m_e[(idx - 1) & kDitherBufMask] * 2.165f
            - state->m_e[(idx - 2) & kDitherBufMask] * 1.959f
            + state->m_e[(idx - 3) & kDitherBufMask] * 1.590f
            - state->m_e[(idx - 4) & kDitherBufMask] * 0.6149f;
        xp = xe + r - rm1;
        rm1 = r;

        float16Scaled(xp, tmp);

        // Intrinsic z^-1 delay
        idx = (idx + 1) & kDitherBufMask;
        state->m_e[idx] = xp - xe;

#if __BYTE_ORDER == __LITTLE_ENDIAN
        dst[0] = static_cast<std::byte>(tmp >> 8);
        dst[1] = static_cast<std::byte>(tmp);
#elif __BYTE_ORDER == __BIG_ENDIAN
        dst[0] = static_cast<std::byte>(tmp);
        dst[1] = static_cast<std::byte>(tmp >> 8);
#endif
        dst += dstSkip;
        src++;
    }
    state->m_rm1 = rm1;
    state->m_idx = idx;
}

void sampleMoveDitherShapedD16SS(std::byte* dst, DefaultAudioSampleC* src, std::uint64_t nsamples, std::uint64_t dstSkip, DitherState* state)
{
    DefaultAudioSampleC x;
    DefaultAudioSampleC xe; // the innput sample - filtered error
    DefaultAudioSampleC xp; // x'
    float r;
    float rm1 = state->m_rm1;
    std::uint32_t idx = state->m_idx;

    while (nsamples--) {
        x = *src * kSample16BitScaling;
        r = ((float)fastRand() + (float)fastRand()) / (float)UINT_MAX - 1.0f;
        // Filter the error with Lipshitz's minimally audible FIR:
        // [2.033 -2.165 1.959 -1.590 0.6149]
        xe = x
            - state->m_e[idx] * 2.033f
            + state->m_e[(idx - 1) & kDitherBufMask] * 2.165f
            - state->m_e[(idx - 2) & kDitherBufMask] * 1.959f
            + state->m_e[(idx - 3) & kDitherBufMask] * 1.590f
            - state->m_e[(idx - 4) & kDitherBufMask] * 0.6149f;
        xp = xe + r - rm1;
        rm1 = r;

        float16Scaled(xp, *((std::int16_t*)dst));

        // Intrinsic z^-1 delay
        idx = (idx + 1) & kDitherBufMask;
        state->m_e[idx] = *((std::int16_t*)dst) - xe;

        dst += dstSkip;
        src++;
    }
    state->m_rm1 = rm1;
    state->m_idx = idx;
}

void sampleMoveDSS16s(DefaultAudioSampleC* dst, std::byte* src, std::uint64_t nsamples, std::uint64_t srcSkip)
{
    std::int16_t z;
    const DefaultAudioSampleC scaling = 1.0 / kSample16BitScaling;
#if defined(__ARM_NEON__) || defined(__ARM_NEON)
    const float32x4_t vscaling = vdupq_n_f32(scaling);
    std::uint64_t unrolled = nsamples / 4;
    while (unrolled--) {
        int16x4_t source16x4;
        switch (srcSkip) {
        case 2:
            source16x4 = vld1_s16((std::int16_t*)src);
            break;
        case 4:
            source16x4 = vld2_s16((std::int16_t*)src).val[0];
            break;
        default:
            source16x4 = vld1_lane_s16((std::int16_t*)src, source16x4, 0);
            source16x4 = vld1_lane_s16((std::int16_t*)(src + srcSkip), source16x4, 1);
            source16x4 = vld1_lane_s16((std::int16_t*)(src + 2 * srcSkip), source16x4, 2);
            source16x4 = vld1_lane_s16((std::int16_t*)(src + 3 * srcSkip), source16x4, 3);
            break;
        }
        source16x4 = vreinterpret_s16_u8(vrev16_u8(vreinterpret_u8_s16(source16x4)));
        int32x4_t source32x4 = vmovl_s16(source16x4);
        src += 4 * srcSkip;

        float32x4_t converted = vcvtq_f32_s32(source32x4);
        float32x4_t scaled = vmulq_f32(converted, vscaling);
        vst1q_f32(dst, scaled);
        dst += 4;
    }
    nsamples = nsamples & 3;
#endif

    // ALERT: signed sign-extension portability !!!
    while (nsamples--) {
#if __BYTE_ORDER == __LITTLE_ENDIAN
        z = std::to_integer<std::uint8_t>(src[0]);
        z <<= 8;
        z |= std::to_integer<std::uint8_t>(src[1]);
#elif __BYTE_ORDER == __BIG_ENDIAN
        z = std::to_integer<std::uint8_t>(src[1]);
        z <<= 8;
        z |= std::to_integer<std::uint8_t>(src[0]);
#endif
        *dst = z * scaling;
        dst++;
        src += srcSkip;
    }
}

void sampleMoveDSS16(DefaultAudioSampleC* dst, std::byte* src, std::uint64_t nsamples, std::uint64_t srcSkip)
{
    // ALERT: signed sign-extension portability !!!
    const DefaultAudioSampleC scaling = 1.0 / kSample16BitScaling;
#if defined(__ARM_NEON__) || defined(__ARM_NEON)
    const float32x4_t vscaling = vdupq_n_f32(scaling);
    std::uint64_t unrolled = nsamples / 4;
    while (unrolled--) {
        int16x4_t source16x4;
        switch (srcSkip) {
        case 2:
            source16x4 = vld1_s16((std::int16_t*)src);
            break;
        case 4:
            source16x4 = vld2_s16((std::int16_t*)src).val[0];
            break;
        default:
            source16x4 = vld1_lane_s16((std::int16_t*)src, source16x4, 0);
            source16x4 = vld1_lane_s16((std::int16_t*)(src + srcSkip), source16x4, 1);
            source16x4 = vld1_lane_s16((std::int16_t*)(src + 2 * srcSkip), source16x4, 2);
            source16x4 = vld1_lane_s16((std::int16_t*)(src + 3 * srcSkip), source16x4, 3);
            break;
        }
        int32x4_t source32x4 = vmovl_s16(source16x4);
        src += 4 * srcSkip;

        float32x4_t converted = vcvtq_f32_s32(source32x4);
        float32x4_t scaled = vmulq_f32(converted, vscaling);
        vst1q_f32(dst, scaled);
        dst += 4;
    }
    nsamples = nsamples & 3;
#endif

    while (nsamples--) {
        *dst = (*((std::int16_t*)src)) * scaling;
        dst++;
        src += srcSkip;
    }
}

void memsetInterleave(std::byte* dst, std::byte val, std::uint64_t bytes,
    std::uint64_t unitBytes,
    std::uint64_t skipBytes)
{
    switch (unitBytes) {
    case 1:
        while (bytes--) {
            *dst = val;
            dst += skipBytes;
        }
        break;
    case 2:
        while (bytes) {
            *((std::int16_t*)dst) = (std::int16_t)val;
            dst += skipBytes;
            bytes -= 2;
        }
        break;
    case 4:
        while (bytes) {
            *((std::int32_t*)dst) = (std::int32_t)val;
            dst += skipBytes;
            bytes -= 4;
        }
        break;
    default:
        while (bytes) {
            memset(dst, std::to_integer<std::int32_t>(val), unitBytes);
            dst += skipBytes;
            bytes -= unitBytes;
        }
        break;
    }
}

// COPY FUNCTIONS: used to move data from an input channel to an
// output channel. Note that we assume that the skip distance
// is the same for both channels. This is completely fine
// unless the input and output were on different audio interfaces that
// were interleaved differently. We don't try to handle that.

void memcpyFake(std::byte* dst, std::byte* src, std::uint64_t srcBytes, std::uint64_t foo, std::uint64_t bar)
{
    memcpy(dst, src, srcBytes);
}

void memcpyInterleaveD16S16(std::byte* dst, std::byte* src, std::uint64_t srcBytes,
    std::uint64_t dstSkipBytes, std::uint64_t srcSkipBytes)
{
    while (srcBytes) {
        *((std::int16_t*)dst) = *((std::int16_t*)src);
        dst += dstSkipBytes;
        src += srcSkipBytes;
        srcBytes -= 2;
    }
}

void memcpyInterleaveD24S24(std::byte* dst, std::byte* src, std::uint64_t srcBytes,
    std::uint64_t dstSkipBytes, std::uint64_t srcSkipBytes)
{
    while (srcBytes) {
        memcpy(dst, src, 3);
        dst += dstSkipBytes;
        src += srcSkipBytes;
        srcBytes -= 3;
    }
}

void memcpyInterleaveD32S32(std::byte* dst, std::byte* src, std::uint64_t srcBytes,
    std::uint64_t dstSkipBytes, std::uint64_t srcSkipBytes)
{
    while (srcBytes) {
        *((std::int32_t*)dst) = *((std::int32_t*)src);
        dst += dstSkipBytes;
        src += srcSkipBytes;
        srcBytes -= 4;
    }
}
