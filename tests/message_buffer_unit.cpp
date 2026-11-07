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
// and its flush thread, with several producers, and check that every message
// is either delivered in per-producer order or counted as an overrun.

#include "JackError.h"
#include "JackMessageBuffer.h"

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>
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

// Producer messages look like "t<producer>:<sequence>".  Anything else is
// the buffer's own report and is not counted.
bool IsProducerMessage(const std::string& text)
{
    return text.size() > 1 && text[0] == 't' && std::isdigit(static_cast<unsigned char>(text[1]));
}

void TestConcurrentProducers()
{
    constexpr int producers = 4;
    constexpr long per_producer = 20000;

    CHECK(Jack::JackMessageBuffer::Create());
    g_received.clear();

    std::vector<std::thread> threads;
    for (int id = 0; id < producers; id++) {
        threads.emplace_back([id] {
            for (long i = 0; i < per_producer; i++) {
                const std::string text = "t" + std::to_string(id) + ":" + std::to_string(i);
                Jack::JackMessageBufferAdd(LOG_LEVEL_ERROR, text.c_str());
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }

    // Read the overrun count before Destroy() drains the ring and logs it.
    const std::uint64_t dropped = Jack::JackMessageBuffer::fInstance->Overruns();
    CHECK(Jack::JackMessageBuffer::Destroy());

    std::vector<long> last(producers, -1);
    std::uint64_t delivered = 0;
    bool ordered = true;
    for (const auto& text : Snapshot()) {
        if (!IsProducerMessage(text)) {
            continue;
        }
        const auto colon = text.find(':');
        const int id = std::atoi(text.c_str() + 1);
        const long seq = std::strtol(text.c_str() + colon + 1, nullptr, 10);
        if (id < 0 || id >= producers || seq <= last[id] || text != "t" + std::to_string(id) + ":" + std::to_string(seq)) {
            ordered = false;
            break;
        }
        last[id] = seq;
        delivered++;
    }

    CHECK(ordered);
    // Every message was either delivered or counted, and nothing else was.
    CHECK(delivered + dropped == static_cast<std::uint64_t>(producers) * per_producer);
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

    TestConcurrentProducers();
    TestTruncation();

    std::printf("%d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
