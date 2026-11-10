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

#include <cstddef>
#include <cstdint>
#include <string.h>

#include "types.h"


typedef enum {
    None,
    Rectangular,
    Triangular,
    Shaped
} DitherAlgorithm;

#define DITHER_BUF_SIZE 8
#define DITHER_BUF_MASK 7

typedef struct {
    std::uint32_t depth;
    float rm1;
    std::uint32_t idx;
    float e[DITHER_BUF_SIZE];
} dither_state_t;

// float functions
void sample_move_floatLE_sSs(jack_default_audio_sample_t* dst, std::byte* src, std::uint64_t nsamples, std::uint64_t dst_skip);
void sample_move_dS_floatLE(std::byte* dst, jack_default_audio_sample_t* src, std::uint64_t nsamples, std::uint64_t dst_skip, dither_state_t* state);

// integer functions
void sample_move_d32_sSs(std::byte* dst, jack_default_audio_sample_t* src, std::uint64_t nsamples, std::uint64_t dst_skip, dither_state_t* state);
void sample_move_d32_sS(std::byte* dst, jack_default_audio_sample_t* src, std::uint64_t nsamples, std::uint64_t dst_skip, dither_state_t* state);
void sample_move_d32u24_sSs(std::byte* dst, jack_default_audio_sample_t* src, std::uint64_t nsamples, std::uint64_t dst_skip, dither_state_t* state);
void sample_move_d32u24_sS(std::byte* dst, jack_default_audio_sample_t* src, std::uint64_t nsamples, std::uint64_t dst_skip, dither_state_t* state);
void sample_move_d32l24_sSs(std::byte* dst, jack_default_audio_sample_t* src, std::uint64_t nsamples, std::uint64_t dst_skip, dither_state_t* state);
void sample_move_d32l24_sS(std::byte* dst, jack_default_audio_sample_t* src, std::uint64_t nsamples, std::uint64_t dst_skip, dither_state_t* state);
void sample_move_d24_sSs(std::byte* dst, jack_default_audio_sample_t* src, std::uint64_t nsamples, std::uint64_t dst_skip, dither_state_t* state);
void sample_move_d24_sS(std::byte* dst, jack_default_audio_sample_t* src, std::uint64_t nsamples, std::uint64_t dst_skip, dither_state_t* state);
void sample_move_d16_sSs(std::byte* dst, jack_default_audio_sample_t* src, std::uint64_t nsamples, std::uint64_t dst_skip, dither_state_t* state);
void sample_move_d16_sS(std::byte* dst, jack_default_audio_sample_t* src, std::uint64_t nsamples, std::uint64_t dst_skip, dither_state_t* state);

void sample_move_dither_rect_d16_sSs(std::byte* dst, jack_default_audio_sample_t* src, std::uint64_t nsamples, std::uint64_t dst_skip, dither_state_t* state);
void sample_move_dither_rect_d16_sS(std::byte* dst, jack_default_audio_sample_t* src, std::uint64_t nsamples, std::uint64_t dst_skip, dither_state_t* state);
void sample_move_dither_tri_d16_sSs(std::byte* dst, jack_default_audio_sample_t* src, std::uint64_t nsamples, std::uint64_t dst_skip, dither_state_t* state);
void sample_move_dither_tri_d16_sS(std::byte* dst, jack_default_audio_sample_t* src, std::uint64_t nsamples, std::uint64_t dst_skip, dither_state_t* state);
void sample_move_dither_shaped_d16_sSs(std::byte* dst, jack_default_audio_sample_t* src, std::uint64_t nsamples, std::uint64_t dst_skip, dither_state_t* state);
void sample_move_dither_shaped_d16_sS(std::byte* dst, jack_default_audio_sample_t* src, std::uint64_t nsamples, std::uint64_t dst_skip, dither_state_t* state);

void sample_move_dS_s32s(jack_default_audio_sample_t* dst, std::byte* src, std::uint64_t nsamples, std::uint64_t src_skip);
void sample_move_dS_s32(jack_default_audio_sample_t* dst, std::byte* src, std::uint64_t nsamples, std::uint64_t src_skip);
void sample_move_dS_s32u24s(jack_default_audio_sample_t* dst, std::byte* src, std::uint64_t nsamples, std::uint64_t src_skip);
void sample_move_dS_s32u24(jack_default_audio_sample_t* dst, std::byte* src, std::uint64_t nsamples, std::uint64_t src_skip);
void sample_move_dS_s32l24s(jack_default_audio_sample_t* dst, std::byte* src, std::uint64_t nsamples, std::uint64_t src_skip);
void sample_move_dS_s32l24(jack_default_audio_sample_t* dst, std::byte* src, std::uint64_t nsamples, std::uint64_t src_skip);
void sample_move_dS_s24s(jack_default_audio_sample_t* dst, std::byte* src, std::uint64_t nsamples, std::uint64_t src_skip);
void sample_move_dS_s24(jack_default_audio_sample_t* dst, std::byte* src, std::uint64_t nsamples, std::uint64_t src_skip);
void sample_move_dS_s16s(jack_default_audio_sample_t* dst, std::byte* src, std::uint64_t nsamples, std::uint64_t src_skip);
void sample_move_dS_s16(jack_default_audio_sample_t* dst, std::byte* src, std::uint64_t nsamples, std::uint64_t src_skip);


static __inline__ void
sample_merge(jack_default_audio_sample_t* dst, jack_default_audio_sample_t* src, std::uint64_t cnt)
{
    while (cnt--) {
        *dst += *src;
        dst++;
        src++;
    }
}

static __inline__ void
sample_memcpy(jack_default_audio_sample_t* dst, jack_default_audio_sample_t* src, std::uint64_t cnt)
{
    memcpy(dst, src, cnt * sizeof(jack_default_audio_sample_t));
}

void memset_interleave(std::byte* dst, std::byte val, std::uint64_t bytes, std::uint64_t unit_bytes, std::uint64_t skip_bytes);
void memcpy_fake(std::byte* dst, std::byte* src, std::uint64_t src_bytes, std::uint64_t foo, std::uint64_t bar);

void memcpy_interleave_d16_s16(std::byte* dst, std::byte* src, std::uint64_t src_bytes, std::uint64_t dst_skip_bytes, std::uint64_t src_skip_bytes);
void memcpy_interleave_d24_s24(std::byte* dst, std::byte* src, std::uint64_t src_bytes, std::uint64_t dst_skip_bytes, std::uint64_t src_skip_bytes);
void memcpy_interleave_d32_s32(std::byte* dst, std::byte* src, std::uint64_t src_bytes, std::uint64_t dst_skip_bytes, std::uint64_t src_skip_bytes);
