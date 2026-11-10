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

#include "JackCString.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <span>

namespace {

std::int32_t g_failures = 0;
std::int32_t g_checks = 0;

#define CHECK(cond)                                                                       \
    do {                                                                                  \
        g_checks++;                                                                       \
        if (!(cond)) {                                                                    \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            g_failures++;                                                                 \
        }                                                                                 \
    } while (0)

void TestFits()
{
    char buf[8];
    jackmini::CopyCString(buf, "abc");
    CHECK(std::strcmp(buf, "abc") == 0);
    for (std::int32_t i = 4; i < 8; i++) {
        CHECK(buf[i] == '\0');
    }
}

void TestExactFit()
{
    char buf[4];
    jackmini::CopyCString(buf, "abc");
    CHECK(std::strncmp(buf, "abc", 4) == 0);
    CHECK(buf[3] == '\0');
}

void TestTruncates()
{
    char buf[4];
    jackmini::CopyCString(buf, "abcdef");
    CHECK(std::strncmp(buf, "abc", 4) == 0);
    CHECK(buf[3] == '\0');
}

void TestZeroFillsStaleBytes()
{
    char buf[8];
    std::memset(buf, 'x', sizeof(buf));
    jackmini::CopyCString(buf, "ab");
    CHECK(buf[0] == 'a' && buf[1] == 'b');
    for (std::int32_t i = 2; i < 8; i++) {
        CHECK(buf[i] == '\0');
    }
}

void TestLeadingInteger()
{
    // The same answers atoi() gives for these inputs.
    CHECK(jackmini::ParseLeadingInteger<std::int32_t>("42") == 42);
    CHECK(jackmini::ParseLeadingInteger<std::int32_t>("  -7") == -7);
    CHECK(jackmini::ParseLeadingInteger<std::int32_t>("+5") == 5);
    CHECK(jackmini::ParseLeadingInteger<std::int32_t>("12abc") == 12);
    CHECK(jackmini::ParseLeadingInteger<std::int32_t>("abc") == 0);
    CHECK(jackmini::ParseLeadingInteger<std::int32_t>("") == 0);
    CHECK(jackmini::ParseLeadingInteger<std::int32_t>("+-3") == 0);
    CHECK(jackmini::ParseLeadingInteger<std::int32_t>("99999999999") == 0);
    CHECK(jackmini::ParseLeadingInteger<std::uint32_t>("300") == 300u);
}

void TestEmptyBuffer()
{
    jackmini::CopyCString(std::span<char> { }, "abc");
    CHECK(true);
}

} // namespace

int main()
{
    TestFits();
    TestExactFit();
    TestTruncates();
    TestZeroFillsStaleBytes();
    TestLeadingInteger();
    TestEmptyBuffer();

    std::printf("%d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
