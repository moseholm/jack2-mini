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

// Unit tests for the realtime message buffer.  They run the real singleton
// and its flush thread, with several producers, and check that every message
// is either delivered in per-producer order or counted as an overrun.

#include "Error.h"
#include "MessageBuffer.h"

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

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

std::mutex gLock;
std::vector<std::string> gReceived;

void captureError(const char* desc)
{
    std::lock_guard<std::mutex> guard(gLock);
    gReceived.emplace_back(desc);
}

std::vector<std::string> snapshot()
{
    std::lock_guard<std::mutex> guard(gLock);
    return gReceived;
}

// Producer messages look like "t<producer>:<sequence>".  Anything else is
// the buffer's own report and is not counted.
bool isProducerMessage(const std::string& text)
{
    return text.size() > 1 && text[0] == 't' && std::isdigit(static_cast<unsigned char>(text[1]));
}

void testConcurrentProducers()
{
    constexpr std::int32_t kProducers = 4;
    constexpr std::int64_t kPerProducer = 20000;

    CHECK(jackmini::MessageBuffer::create());
    gReceived.clear();

    std::vector<std::thread> threads;
    for (std::int32_t id = 0; id < kProducers; id++) {
        threads.emplace_back([id] {
            for (std::int64_t i = 0; i < kPerProducer; i++) {
                const std::string text = "t" + std::to_string(id) + ":" + std::to_string(i);
                jackmini::messageBufferAdd(kLogLevelError, text.c_str());
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }

    // Read the overrun count before Destroy() drains the ring and logs it.
    const std::uint64_t dropped = jackmini::MessageBuffer::s_instance->overruns();
    CHECK(jackmini::MessageBuffer::destroy());

    std::vector<std::int64_t> last(kProducers, -1);
    std::uint64_t delivered = 0;
    bool ordered = true;
    for (const auto& text : snapshot()) {
        if (!isProducerMessage(text)) {
            continue;
        }
        const auto colon = text.find(':');
        const std::int32_t id = std::atoi(text.c_str() + 1);
        const std::int64_t seq = std::strtol(text.c_str() + colon + 1, nullptr, 10);
        if (id < 0 || id >= kProducers || seq <= last[id] || text != "t" + std::to_string(id) + ":" + std::to_string(seq)) {
            ordered = false;
            break;
        }
        last[id] = seq;
        delivered++;
    }

    CHECK(ordered);
    // Every message was either delivered or counted, and nothing else was.
    CHECK(delivered + dropped == static_cast<std::uint64_t>(kProducers) * kPerProducer);
    CHECK(delivered > 0);
}

void testTruncation()
{
    CHECK(jackmini::MessageBuffer::create());
    gReceived.clear();

    const std::string longMessage = "L:" + std::string(400, 'x');
    jackmini::messageBufferAdd(kLogLevelError, longMessage.c_str());
    CHECK(jackmini::MessageBuffer::destroy());

    const auto received = snapshot();
    CHECK(received.size() == 1);
    if (received.size() == 1) {
        // 256 bytes including the terminator, so 255 characters are kept.
        CHECK(received[0].size() == 255);
        CHECK(received[0].compare(0, 2, "L:") == 0);
    }
}

} // namespace

int main()
{
    gJackErrorCallback = captureError;

    testConcurrentProducers();
    testTruncation();

    std::printf("%d checks, %d failed\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
