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

#include "Memops.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

std::int32_t gFailures = 0;

#define CHECK(cond)                                              \
    do {                                                         \
        if (!(cond)) {                                           \
            std::fprintf(stderr, "FAILED %s:%d: %s\n", __FILE__, \
                __LINE__, #cond);                                \
            ++failures;                                          \
        }                                                        \
    } while (0)

template <std::size_t N>
void checkBytes(const char* what, const void* got, const std::array<const std::byte, N>& want)
{
    if (std::memcmp(got, want.data(), N) != 0) {
        std::fprintf(stderr, "FAILED %s: bytes differ\n", what);
        ++gFailures;
    }
}

std::array<const std::byte, 16> gKD16 = {
    std::byte { 0x00 },
    std::byte { 0x00 },
    std::byte { 0xff },
    std::byte { 0x7f },
    std::byte { 0x01 },
    std::byte { 0x80 },
    std::byte { 0x00 },
    std::byte { 0x40 },
    std::byte { 0x00 },
    std::byte { 0xe0 },
    std::byte { 0xff },
    std::byte { 0x7f },
    std::byte { 0x01 },
    std::byte { 0x80 },
    std::byte { 0xaa },
    std::byte { 0x2a },
};

std::array<const std::byte, 24> gKD24 = {
    std::byte { 0x00 },
    std::byte { 0x00 },
    std::byte { 0x00 },
    std::byte { 0xff },
    std::byte { 0xff },
    std::byte { 0x7f },
    std::byte { 0x01 },
    std::byte { 0x00 },
    std::byte { 0x80 },
    std::byte { 0x00 },
    std::byte { 0x00 },
    std::byte { 0x40 },
    std::byte { 0x00 },
    std::byte { 0x00 },
    std::byte { 0xe0 },
    std::byte { 0xff },
    std::byte { 0xff },
    std::byte { 0x7f },
    std::byte { 0x01 },
    std::byte { 0x00 },
    std::byte { 0x80 },
    std::byte { 0xaa },
    std::byte { 0xaa },
    std::byte { 0x2a },
};

std::array<const std::byte, 32> gKD32 = {
    std::byte { 0x00 },
    std::byte { 0x00 },
    std::byte { 0x00 },
    std::byte { 0x00 },
    std::byte { 0xff },
    std::byte { 0xff },
    std::byte { 0xff },
    std::byte { 0x7f },
    std::byte { 0x01 },
    std::byte { 0x00 },
    std::byte { 0x00 },
    std::byte { 0x80 },
    std::byte { 0x00 },
    std::byte { 0x00 },
    std::byte { 0x00 },
    std::byte { 0x40 },
    std::byte { 0x00 },
    std::byte { 0x00 },
    std::byte { 0x00 },
    std::byte { 0xe0 },
    std::byte { 0xff },
    std::byte { 0xff },
    std::byte { 0xff },
    std::byte { 0x7f },
    std::byte { 0x01 },
    std::byte { 0x00 },
    std::byte { 0x00 },
    std::byte { 0x80 },
    std::byte { 0x80 },
    std::byte { 0xaa },
    std::byte { 0xaa },
    std::byte { 0x2a },
};

std::array<const std::byte, 32> gKD32u24 = {
    std::byte { 0x00 },
    std::byte { 0x00 },
    std::byte { 0x00 },
    std::byte { 0x00 },
    std::byte { 0x00 },
    std::byte { 0xff },
    std::byte { 0xff },
    std::byte { 0x7f },
    std::byte { 0x00 },
    std::byte { 0x01 },
    std::byte { 0x00 },
    std::byte { 0x80 },
    std::byte { 0x00 },
    std::byte { 0xff },
    std::byte { 0xff },
    std::byte { 0x3f },
    std::byte { 0x00 },
    std::byte { 0x01 },
    std::byte { 0x00 },
    std::byte { 0xe0 },
    std::byte { 0x00 },
    std::byte { 0xff },
    std::byte { 0xff },
    std::byte { 0x7f },
    std::byte { 0x00 },
    std::byte { 0x01 },
    std::byte { 0x00 },
    std::byte { 0x80 },
    std::byte { 0x00 },
    std::byte { 0xaa },
    std::byte { 0xaa },
    std::byte { 0x2a },
};

std::array<const std::byte, 24> gKS16Float = {
    std::byte { 0x00 },
    std::byte { 0x00 },
    std::byte { 0x00 },
    std::byte { 0x00 },
    std::byte { 0x00 },
    std::byte { 0x00 },
    std::byte { 0x80 },
    std::byte { 0x3f },
    std::byte { 0x00 },
    std::byte { 0x00 },
    std::byte { 0x80 },
    std::byte { 0xbf },
    std::byte { 0x00 },
    std::byte { 0x01 },
    std::byte { 0x00 },
    std::byte { 0x3f },
    std::byte { 0x00 },
    std::byte { 0x01 },
    std::byte { 0x80 },
    std::byte { 0xbe },
    std::byte { 0x00 },
    std::byte { 0x01 },
    std::byte { 0x00 },
    std::byte { 0x38 },
};

std::array<const std::byte, 20> gKS32Float = {
    std::byte { 0x00 },
    std::byte { 0x00 },
    std::byte { 0x00 },
    std::byte { 0x00 },
    std::byte { 0x00 },
    std::byte { 0x00 },
    std::byte { 0x80 },
    std::byte { 0x3f },
    std::byte { 0x00 },
    std::byte { 0x00 },
    std::byte { 0x80 },
    std::byte { 0xbf },
    std::byte { 0x00 },
    std::byte { 0x00 },
    std::byte { 0x00 },
    std::byte { 0x3f },
    std::byte { 0x00 },
    std::byte { 0x00 },
    std::byte { 0x80 },
    std::byte { 0xbe },
};

std::array<const std::byte, 24> gKInterleave16 = {
    std::byte { 0x01 },
    std::byte { 0x02 },
    std::byte { 0x55 },
    std::byte { 0x55 },
    std::byte { 0x03 },
    std::byte { 0x04 },
    std::byte { 0x55 },
    std::byte { 0x55 },
    std::byte { 0x05 },
    std::byte { 0x06 },
    std::byte { 0x55 },
    std::byte { 0x55 },
    std::byte { 0x55 },
    std::byte { 0x55 },
    std::byte { 0x55 },
    std::byte { 0x55 },
    std::byte { 0x55 },
    std::byte { 0x55 },
    std::byte { 0x55 },
    std::byte { 0x55 },
    std::byte { 0x55 },
    std::byte { 0x55 },
    std::byte { 0x55 },
    std::byte { 0x55 },
};

std::array<const std::byte, 24> gKMemsetUnit2 = {
    std::byte { 0x7f },
    std::byte { 0x00 },
    std::byte { 0x55 },
    std::byte { 0x55 },
    std::byte { 0x7f },
    std::byte { 0x00 },
    std::byte { 0x55 },
    std::byte { 0x55 },
    std::byte { 0x7f },
    std::byte { 0x00 },
    std::byte { 0x55 },
    std::byte { 0x55 },
    std::byte { 0x55 },
    std::byte { 0x55 },
    std::byte { 0x55 },
    std::byte { 0x55 },
    std::byte { 0x55 },
    std::byte { 0x55 },
    std::byte { 0x55 },
    std::byte { 0x55 },
    std::byte { 0x55 },
    std::byte { 0x55 },
    std::byte { 0x55 },
    std::byte { 0x55 },
};

std::array<const std::byte, 12> gKMemcpyFake = {
    std::byte { 0x01 },
    std::byte { 0x02 },
    std::byte { 0x03 },
    std::byte { 0x04 },
    std::byte { 0x05 },
    std::byte { 0x06 },
    std::byte { 0x07 },
    std::byte { 0x08 },
    std::byte { 0x09 },
    std::byte { 0x0a },
    std::byte { 0x0b },
    std::byte { 0x0c },
};

void checkFloatsToSamples()
{
    std::array<const float, 8> in = { 0.0f, 1.0f, -1.0f, 0.5f, -0.25f, 2.0f, -2.0f, 0.3333333f };
    const std::uint64_t n = sizeof in / sizeof in[0];
    DitherState st { };

    std::array<std::byte, n * 2> d16 = { };
    sampleMoveD16SS(d16.data(), const_cast<float*>(in.data()), n, 2, &st);
    checkBytes("d16", d16.data(), gKD16);

    std::array<std::byte, n * 3> d24 = { };
    sampleMoveD24SS(d24.data(), const_cast<float*>(in.data()), n, 3, &st);
    checkBytes("d24", d24.data(), gKD24);

    std::array<std::byte, n * 4> d32 = { };
    sampleMoveD32SS(d32.data(), const_cast<float*>(in.data()), n, 4, &st);
    checkBytes("d32", d32.data(), gKD32);

    std::array<std::byte, n * 4> d32u24 = { };
    sampleMoveD32u24SS(d32u24.data(), const_cast<float*>(in.data()), n, 4, &st);
    checkBytes("d32u24", d32u24.data(), gKD32u24);
}

void checkSamplesToFloats()
{
    std::array<const std::int16_t, 6> s16 = { 0, 32767, -32767, 16384, -8192, 1 };
    std::array<float, 6> back = { };
    sampleMoveDSS16(back.data(), reinterpret_cast<std::byte*>(const_cast<std::int16_t*>(s16.data())), 6, 2);
    checkBytes("s16 to float", back.data(), gKS16Float);

    std::array<const std::int32_t, 5> s32 = { 0, 2147483647, -2147483647, 1073741824, -536870912 };
    std::array<float, 5> backs = { };
    sampleMoveDSS32(backs.data(), reinterpret_cast<std::byte*>(const_cast<std::int32_t*>(s32.data())), 5, 4);
    checkBytes("s32 to float", backs.data(), gKS32Float);
}

void checkCopies()
{
    std::array<std::byte, 12> src = { std::byte { 1 }, std::byte { 2 }, std::byte { 3 }, std::byte { 4 }, std::byte { 5 }, std::byte { 6 }, std::byte { 7 }, std::byte { 8 }, std::byte { 9 }, std::byte { 10 }, std::byte { 11 }, std::byte { 12 } };
    std::array<std::byte, 24> dst;

    std::memset(dst.data(), 0x55, sizeof dst);
    memcpyInterleaveD16S16(dst.data(), src.data(), 6, 4, 2);
    checkBytes("interleave 16", dst.data(), gKInterleave16);

    std::memset(dst.data(), 0x55, sizeof dst);
    memsetInterleave(dst.data(), std::byte { 0x7f }, 6, 2, 4);
    checkBytes("memset interleave", dst.data(), gKMemsetUnit2);

    std::memset(dst.data(), 0x55, sizeof dst);
    memcpyFake(dst.data(), src.data(), 12, 0, 0);
    checkBytes("memcpy", dst.data(), gKMemcpyFake);
}

} // end of anonymous namespace

int main()
{
    checkFloatsToSamples();
    checkSamplesToFloats();
    checkCopies();

    if (gFailures) {
        std::fprintf(stderr, "memops_unit: %d failure(s)\n", gFailures);
        return 1;
    }
    std::printf("memops_unit: all checks passed\n");
    return 0;
}
