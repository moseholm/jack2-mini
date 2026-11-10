// Copyright (C) 1999-2000 Paul Davis
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

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string.h>

#include "types.h"


typedef enum {
    kNone,
    kRectangular,
    kTriangular,
    kShaped
} DitherAlgorithm;

inline constexpr std::int32_t kDitherBufSize = 8;
inline constexpr std::int32_t kDitherBufMask = 7;

typedef struct {
    std::uint32_t m_depth;
    float m_rm1;
    std::uint32_t m_idx;
    std::array<float, kDitherBufSize> m_e;
} DitherState;

// float functions
void sampleMoveFloatLESSs(DefaultAudioSampleC* dst, std::byte* src, std::uint64_t nsamples, std::uint64_t dstSkip);
void sampleMoveDSFloatLE(std::byte* dst, DefaultAudioSampleC* src, std::uint64_t nsamples, std::uint64_t dstSkip, DitherState* state);

// integer functions
void sampleMoveD32SSs(std::byte* dst, DefaultAudioSampleC* src, std::uint64_t nsamples, std::uint64_t dstSkip, DitherState* state);
void sampleMoveD32SS(std::byte* dst, DefaultAudioSampleC* src, std::uint64_t nsamples, std::uint64_t dstSkip, DitherState* state);
void sampleMoveD32u24SSs(std::byte* dst, DefaultAudioSampleC* src, std::uint64_t nsamples, std::uint64_t dstSkip, DitherState* state);
void sampleMoveD32u24SS(std::byte* dst, DefaultAudioSampleC* src, std::uint64_t nsamples, std::uint64_t dstSkip, DitherState* state);
void sampleMoveD32l24SSs(std::byte* dst, DefaultAudioSampleC* src, std::uint64_t nsamples, std::uint64_t dstSkip, DitherState* state);
void sampleMoveD32l24SS(std::byte* dst, DefaultAudioSampleC* src, std::uint64_t nsamples, std::uint64_t dstSkip, DitherState* state);
void sampleMoveD24SSs(std::byte* dst, DefaultAudioSampleC* src, std::uint64_t nsamples, std::uint64_t dstSkip, DitherState* state);
void sampleMoveD24SS(std::byte* dst, DefaultAudioSampleC* src, std::uint64_t nsamples, std::uint64_t dstSkip, DitherState* state);
void sampleMoveD16SSs(std::byte* dst, DefaultAudioSampleC* src, std::uint64_t nsamples, std::uint64_t dstSkip, DitherState* state);
void sampleMoveD16SS(std::byte* dst, DefaultAudioSampleC* src, std::uint64_t nsamples, std::uint64_t dstSkip, DitherState* state);

void sampleMoveDitherRectD16SSs(std::byte* dst, DefaultAudioSampleC* src, std::uint64_t nsamples, std::uint64_t dstSkip, DitherState* state);
void sampleMoveDitherRectD16SS(std::byte* dst, DefaultAudioSampleC* src, std::uint64_t nsamples, std::uint64_t dstSkip, DitherState* state);
void sampleMoveDitherTriD16SSs(std::byte* dst, DefaultAudioSampleC* src, std::uint64_t nsamples, std::uint64_t dstSkip, DitherState* state);
void sampleMoveDitherTriD16SS(std::byte* dst, DefaultAudioSampleC* src, std::uint64_t nsamples, std::uint64_t dstSkip, DitherState* state);
void sampleMoveDitherShapedD16SSs(std::byte* dst, DefaultAudioSampleC* src, std::uint64_t nsamples, std::uint64_t dstSkip, DitherState* state);
void sampleMoveDitherShapedD16SS(std::byte* dst, DefaultAudioSampleC* src, std::uint64_t nsamples, std::uint64_t dstSkip, DitherState* state);

void sampleMoveDSS32s(DefaultAudioSampleC* dst, std::byte* src, std::uint64_t nsamples, std::uint64_t srcSkip);
void sampleMoveDSS32(DefaultAudioSampleC* dst, std::byte* src, std::uint64_t nsamples, std::uint64_t srcSkip);
void sampleMoveDSS32u24s(DefaultAudioSampleC* dst, std::byte* src, std::uint64_t nsamples, std::uint64_t srcSkip);
void sampleMoveDSS32u24(DefaultAudioSampleC* dst, std::byte* src, std::uint64_t nsamples, std::uint64_t srcSkip);
void sampleMoveDSS32l24s(DefaultAudioSampleC* dst, std::byte* src, std::uint64_t nsamples, std::uint64_t srcSkip);
void sampleMoveDSS32l24(DefaultAudioSampleC* dst, std::byte* src, std::uint64_t nsamples, std::uint64_t srcSkip);
void sampleMoveDSS24s(DefaultAudioSampleC* dst, std::byte* src, std::uint64_t nsamples, std::uint64_t srcSkip);
void sampleMoveDSS24(DefaultAudioSampleC* dst, std::byte* src, std::uint64_t nsamples, std::uint64_t srcSkip);
void sampleMoveDSS16s(DefaultAudioSampleC* dst, std::byte* src, std::uint64_t nsamples, std::uint64_t srcSkip);
void sampleMoveDSS16(DefaultAudioSampleC* dst, std::byte* src, std::uint64_t nsamples, std::uint64_t srcSkip);


static __inline__ void
sampleMerge(DefaultAudioSampleC* dst, DefaultAudioSampleC* src, std::uint64_t cnt)
{
    while (cnt--) {
        *dst += *src;
        dst++;
        src++;
    }
}

static __inline__ void
sampleMemcpy(DefaultAudioSampleC* dst, DefaultAudioSampleC* src, std::uint64_t cnt)
{
    memcpy(dst, src, cnt * sizeof(DefaultAudioSampleC));
}

void memsetInterleave(std::byte* dst, std::byte val, std::uint64_t bytes, std::uint64_t unitBytes, std::uint64_t skipBytes);
void memcpyFake(std::byte* dst, std::byte* src, std::uint64_t srcBytes, std::uint64_t foo, std::uint64_t bar);

void memcpyInterleaveD16S16(std::byte* dst, std::byte* src, std::uint64_t srcBytes, std::uint64_t dstSkipBytes, std::uint64_t srcSkipBytes);
void memcpyInterleaveD24S24(std::byte* dst, std::byte* src, std::uint64_t srcBytes, std::uint64_t dstSkipBytes, std::uint64_t srcSkipBytes);
void memcpyInterleaveD32S32(std::byte* dst, std::byte* src, std::uint64_t srcBytes, std::uint64_t dstSkipBytes, std::uint64_t srcSkipBytes);
