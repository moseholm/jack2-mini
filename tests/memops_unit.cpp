// Copyright (C) 2026 JACK contributors
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

// The sample conversions and the interleave copies (memops.h). The expected bytes were produced by the C
// implementation that memops.cpp replaced, on these same inputs; a change in the kernels shows here.

#include "memops.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

int failures = 0;

#define CHECK(cond)                                              \
    do {                                                         \
        if (!(cond)) {                                           \
            std::fprintf(stderr, "FAILED %s:%d: %s\n", __FILE__, \
                __LINE__, #cond);                                \
            ++failures;                                          \
        }                                                        \
    } while (0)

template <std::size_t N>
void check_bytes(const char* what, const void* got, const unsigned char (&want)[N])
{
    if (std::memcmp(got, want, N) != 0) {
        std::fprintf(stderr, "FAILED %s: bytes differ\n", what);
        ++failures;
    }
}

const unsigned char kD16[] = {
    0x00,
    0x00,
    0xff,
    0x7f,
    0x01,
    0x80,
    0x00,
    0x40,
    0x00,
    0xe0,
    0xff,
    0x7f,
    0x01,
    0x80,
    0xaa,
    0x2a,
};

const unsigned char kD24[] = {
    0x00,
    0x00,
    0x00,
    0xff,
    0xff,
    0x7f,
    0x01,
    0x00,
    0x80,
    0x00,
    0x00,
    0x40,
    0x00,
    0x00,
    0xe0,
    0xff,
    0xff,
    0x7f,
    0x01,
    0x00,
    0x80,
    0xaa,
    0xaa,
    0x2a,
};

const unsigned char kD32[] = {
    0x00,
    0x00,
    0x00,
    0x00,
    0xff,
    0xff,
    0xff,
    0x7f,
    0x01,
    0x00,
    0x00,
    0x80,
    0x00,
    0x00,
    0x00,
    0x40,
    0x00,
    0x00,
    0x00,
    0xe0,
    0xff,
    0xff,
    0xff,
    0x7f,
    0x01,
    0x00,
    0x00,
    0x80,
    0x80,
    0xaa,
    0xaa,
    0x2a,
};

const unsigned char kD32u24[] = {
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0xff,
    0xff,
    0x7f,
    0x00,
    0x01,
    0x00,
    0x80,
    0x00,
    0xff,
    0xff,
    0x3f,
    0x00,
    0x01,
    0x00,
    0xe0,
    0x00,
    0xff,
    0xff,
    0x7f,
    0x00,
    0x01,
    0x00,
    0x80,
    0x00,
    0xaa,
    0xaa,
    0x2a,
};

const unsigned char kS16Float[] = {
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x80,
    0x3f,
    0x00,
    0x00,
    0x80,
    0xbf,
    0x00,
    0x01,
    0x00,
    0x3f,
    0x00,
    0x01,
    0x80,
    0xbe,
    0x00,
    0x01,
    0x00,
    0x38,
};

const unsigned char kS32Float[] = {
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x00,
    0x80,
    0x3f,
    0x00,
    0x00,
    0x80,
    0xbf,
    0x00,
    0x00,
    0x00,
    0x3f,
    0x00,
    0x00,
    0x80,
    0xbe,
};

const unsigned char kInterleave16[] = {
    0x01,
    0x02,
    0x55,
    0x55,
    0x03,
    0x04,
    0x55,
    0x55,
    0x05,
    0x06,
    0x55,
    0x55,
    0x55,
    0x55,
    0x55,
    0x55,
    0x55,
    0x55,
    0x55,
    0x55,
    0x55,
    0x55,
    0x55,
    0x55,
};

const unsigned char kMemsetUnit2[] = {
    0x7f,
    0x00,
    0x55,
    0x55,
    0x7f,
    0x00,
    0x55,
    0x55,
    0x7f,
    0x00,
    0x55,
    0x55,
    0x55,
    0x55,
    0x55,
    0x55,
    0x55,
    0x55,
    0x55,
    0x55,
    0x55,
    0x55,
    0x55,
    0x55,
};

const unsigned char kMemcpyFake[] = {
    0x01,
    0x02,
    0x03,
    0x04,
    0x05,
    0x06,
    0x07,
    0x08,
    0x09,
    0x0a,
    0x0b,
    0x0c,
};

void check_floats_to_samples()
{
    const float in[] = { 0.0f, 1.0f, -1.0f, 0.5f, -0.25f, 2.0f, -2.0f, 0.3333333f };
    const unsigned long n = sizeof in / sizeof in[0];
    dither_state_t st { };

    char d16[n * 2] = { };
    sample_move_d16_sS(d16, const_cast<float*>(in), n, 2, &st);
    check_bytes("d16", d16, kD16);

    char d24[n * 3] = { };
    sample_move_d24_sS(d24, const_cast<float*>(in), n, 3, &st);
    check_bytes("d24", d24, kD24);

    char d32[n * 4] = { };
    sample_move_d32_sS(d32, const_cast<float*>(in), n, 4, &st);
    check_bytes("d32", d32, kD32);

    char d32u24[n * 4] = { };
    sample_move_d32u24_sS(d32u24, const_cast<float*>(in), n, 4, &st);
    check_bytes("d32u24", d32u24, kD32u24);
}

void check_samples_to_floats()
{
    const short s16[] = { 0, 32767, -32767, 16384, -8192, 1 };
    float back[6] = { };
    sample_move_dS_s16(back, reinterpret_cast<char*>(const_cast<short*>(s16)), 6, 2);
    check_bytes("s16 to float", back, kS16Float);

    const int32_t s32[] = { 0, 2147483647, -2147483647, 1073741824, -536870912 };
    float backs[5] = { };
    sample_move_dS_s32(backs, reinterpret_cast<char*>(const_cast<int32_t*>(s32)), 5, 4);
    check_bytes("s32 to float", backs, kS32Float);
}

void check_copies()
{
    char src[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12 };
    char dst[24];

    std::memset(dst, 0x55, sizeof dst);
    memcpy_interleave_d16_s16(dst, src, 6, 4, 2);
    check_bytes("interleave 16", dst, kInterleave16);

    std::memset(dst, 0x55, sizeof dst);
    memset_interleave(dst, 0x7f, 6, 2, 4);
    check_bytes("memset interleave", dst, kMemsetUnit2);

    std::memset(dst, 0x55, sizeof dst);
    memcpy_fake(dst, src, 12, 0, 0);
    check_bytes("memcpy", dst, kMemcpyFake);
}

} // end of anonymous namespace

int main()
{
    check_floats_to_samples();
    check_samples_to_floats();
    check_copies();

    if (failures) {
        std::fprintf(stderr, "memops_unit: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("memops_unit: all checks passed\n");
    return 0;
}
