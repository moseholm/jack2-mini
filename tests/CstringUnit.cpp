// Copyright (C) 2026 JACK contributors
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation; either version 2.1 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.

// Unit tests for CopyCString(): truncation, termination and zero fill.

#include "CString.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>

namespace {

std::int32_t gFailures = 0;
std::int32_t gChecks = 0;

#define CHECK(cond)                                                                       \
    do {                                                                                  \
        gChecks++;                                                                        \
        if (!(cond)) {                                                                    \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            gFailures++;                                                                  \
        }                                                                                 \
    } while (0)

void testFits()
{
    std::array<char, 8> buf;
    jackmini::copyCString(buf, "abc");
    CHECK(std::strcmp(buf.data(), "abc") == 0);
    for (std::int32_t i = 4; i < 8; i++) {
        CHECK(buf[i] == '\0');
    }
}

void testExactFit()
{
    std::array<char, 4> buf;
    jackmini::copyCString(buf, "abc");
    CHECK(std::strncmp(buf.data(), "abc", 4) == 0);
    CHECK(buf[3] == '\0');
}

void testTruncates()
{
    std::array<char, 4> buf;
    jackmini::copyCString(buf, "abcdef");
    CHECK(std::strncmp(buf.data(), "abc", 4) == 0);
    CHECK(buf[3] == '\0');
}

void testZeroFillsStaleBytes()
{
    std::array<char, 8> buf;
    std::memset(buf.data(), 'x', sizeof(buf));
    jackmini::copyCString(buf, "ab");
    CHECK(buf[0] == 'a' && buf[1] == 'b');
    for (std::int32_t i = 2; i < 8; i++) {
        CHECK(buf[i] == '\0');
    }
}

void testLeadingInteger()
{
    // The same answers atoi() gives for these inputs.
    CHECK(jackmini::parseLeadingInteger<std::int32_t>("42") == 42);
    CHECK(jackmini::parseLeadingInteger<std::int32_t>("  -7") == -7);
    CHECK(jackmini::parseLeadingInteger<std::int32_t>("+5") == 5);
    CHECK(jackmini::parseLeadingInteger<std::int32_t>("12abc") == 12);
    CHECK(jackmini::parseLeadingInteger<std::int32_t>("abc") == 0);
    CHECK(jackmini::parseLeadingInteger<std::int32_t>("") == 0);
    CHECK(jackmini::parseLeadingInteger<std::int32_t>("+-3") == 0);
    CHECK(jackmini::parseLeadingInteger<std::int32_t>("99999999999") == 0);
    CHECK(jackmini::parseLeadingInteger<std::uint32_t>("300") == 300u);
}

void testEmptyBuffer()
{
    jackmini::copyCString(std::span<char> { }, "abc");
    CHECK(true);
}

} // namespace

int main()
{
    testFits();
    testExactFit();
    testTruncates();
    testZeroFillsStaleBytes();
    testLeadingInteger();
    testEmptyBuffer();

    std::printf("%d checks, %d failed\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
