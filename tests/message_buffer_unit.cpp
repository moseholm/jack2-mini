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

// Unit tests for the realtime message buffer.  They run the real singleton
// and its flush thread, and check ordering, truncation and shutdown.

#include "JackError.h"
#include "JackMessageBuffer.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <vector>

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

std::mutex g_lock;
std::vector<std::string> g_received;

void CaptureError(const char* desc)
{
    std::lock_guard<std::mutex> guard(g_lock);
    g_received.emplace_back(desc);
}

std::vector<std::string> Snapshot()
{
    std::lock_guard<std::mutex> guard(g_lock);
    return g_received;
}

void TestOrderedDelivery()
{
    constexpr int count = 50000;
    for (int i = 0; i < count; i++) {
        Jack::JackMessageBufferAdd(LOG_LEVEL_ERROR, std::to_string(i).c_str());
    }
    CHECK(Jack::JackMessageBuffer::Destroy());

    // Dropped messages are allowed when the flush thread falls behind.  What is
    // delivered must be whole messages in strictly increasing order.  The
    // buffer's own shutdown report ("WARNING: ... overruns") is not a message.
    const auto received = Snapshot();
    long previous = -1;
    long delivered = 0;
    bool ordered = true;
    for (const auto& text : received) {
        if (text.empty() || !std::isdigit(static_cast<unsigned char>(text[0]))) {
            continue;
        }
        const long value = std::strtol(text.c_str(), nullptr, 10);
        if (std::to_string(value) != text || value <= previous) {
            ordered = false;
            break;
        }
        previous = value;
        delivered++;
    }
    CHECK(ordered);
    CHECK(delivered > 0);
}

void TestTruncation()
{
    CHECK(Jack::JackMessageBuffer::Create());
    g_received.clear();

    const std::string long_message = "L:" + std::string(400, 'x');
    Jack::JackMessageBufferAdd(LOG_LEVEL_ERROR, long_message.c_str());
    CHECK(Jack::JackMessageBuffer::Destroy());

    const auto received = Snapshot();
    CHECK(received.size() == 1);
    if (received.size() == 1) {
        // 256 bytes including the terminator, so 255 characters are kept.
        CHECK(received[0].size() == 255);
        CHECK(received[0].compare(0, 2, "L:") == 0);
    }
}

}

int main()
{
    jack_error_callback = CaptureError;

    CHECK(Jack::JackMessageBuffer::Create());
    TestOrderedDelivery();
    TestTruncation();

    std::printf("%d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
