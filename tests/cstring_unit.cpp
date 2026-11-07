/*
Copyright (C) 2026 JACK contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License as published by
the Free Software Foundation; either version 2.1 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
*/

// Unit tests for CopyCString(): truncation, termination and zero fill.

#include "JackCString.h"

#include <cstdio>
#include <cstring>
#include <span>

namespace
{

int g_failures = 0;
int g_checks = 0;

#define CHECK(cond) do { \
    g_checks++; \
    if (!(cond)) { \
        std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
        g_failures++; \
    } \
} while (0)

void TestFits()
{
    char buf[8];
    Jack::CopyCString(buf, "abc");
    CHECK(std::strcmp(buf, "abc") == 0);
    for (int i = 4; i < 8; i++) {
        CHECK(buf[i] == '\0');
    }
}

void TestExactFit()
{
    char buf[4];
    Jack::CopyCString(buf, "abc");
    CHECK(std::strncmp(buf, "abc", 4) == 0);
    CHECK(buf[3] == '\0');
}

void TestTruncates()
{
    char buf[4];
    Jack::CopyCString(buf, "abcdef");
    CHECK(std::strncmp(buf, "abc", 4) == 0);
    CHECK(buf[3] == '\0');
}

void TestZeroFillsStaleBytes()
{
    char buf[8];
    std::memset(buf, 'x', sizeof(buf));
    Jack::CopyCString(buf, "ab");
    CHECK(buf[0] == 'a' && buf[1] == 'b');
    for (int i = 2; i < 8; i++) {
        CHECK(buf[i] == '\0');
    }
}

void TestEmptyBuffer()
{
    Jack::CopyCString(std::span<char>{}, "abc");
    CHECK(true);
}

}

int main()
{
    TestFits();
    TestExactFit();
    TestTruncates();
    TestZeroFillsStaleBytes();
    TestEmptyBuffer();

    std::printf("%d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
