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

// Unit tests for the parts of jackmini that do not need a server: the
// ring buffer, Uuid, and the error text.

#include "jackmini/jackmini.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
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

using jackmini::RingBuffer;

void TestRingCapacity()
{
    CHECK(RingBuffer<int>(0).capacity() == 2);
    CHECK(RingBuffer<int>(5).capacity() == 8);
    CHECK(RingBuffer<int>(8).capacity() == 8);
}

void TestRingSingleThread()
{
    RingBuffer<int> ring(4);
    CHECK(ring.read_space() == 0);
    CHECK(ring.write_space() == 4);

    const std::array<int, 3> in{1, 2, 3};
    CHECK(ring.write(in) == 3);
    CHECK(ring.read_space() == 3);

    // Only one slot is left, so a write of three moves one item.
    CHECK(ring.write(in) == 1);
    CHECK(ring.write_space() == 0);

    std::array<int, 8> out{};
    CHECK(ring.read(out) == 4);
    CHECK(out[0] == 1 && out[1] == 2 && out[2] == 3 && out[3] == 1);
    CHECK(ring.read(out) == 0);

    // Indices wrap around the storage many times.
    for (int round = 0; round < 1000; round++) {
        const std::array<int, 2> pair{round, round + 1};
        CHECK(ring.write(pair) == 2);
        std::array<int, 2> got{};
        CHECK(ring.read(got) == 2);
        CHECK(got == pair);
    }

    ring.reset();
    CHECK(ring.read_space() == 0 && ring.write_space() == 4);
}

// One producer and one consumer, each on its own thread.  Every value must
// arrive once and in order, and a full ring must not lose data.
void TestRingProducerConsumer()
{
    constexpr std::uint32_t total = 2000000;
    RingBuffer<std::uint32_t> ring(256);
    bool ordered = true;

    std::thread producer([&] {
        std::uint32_t next = 0;
        std::array<std::uint32_t, 17> chunk{};
        while (next < total) {
            const std::size_t n = std::min<std::size_t>(chunk.size(), total - next);
            for (std::size_t i = 0; i < n; i++) {
                chunk[i] = next + static_cast<std::uint32_t>(i);
            }
            std::span<const std::uint32_t> pending(chunk.data(), n);
            while (!pending.empty()) {
                const std::size_t moved = ring.write(pending);
                pending = pending.subspan(moved);
                if (moved == 0) {
                    std::this_thread::yield();
                }
            }
            next += static_cast<std::uint32_t>(n);
        }
    });

    std::thread consumer([&] {
        std::uint32_t expected = 0;
        std::array<std::uint32_t, 13> chunk{};
        while (expected < total) {
            const std::size_t n = ring.read(chunk);
            for (std::size_t i = 0; i < n; i++) {
                if (chunk[i] != expected) {
                    ordered = false;
                }
                expected++;
            }
            if (n == 0) {
                std::this_thread::yield();
            }
        }
    });

    producer.join();
    consumer.join();
    CHECK(ordered);
    CHECK(ring.read_space() == 0);
}

void TestUuid()
{
    const jackmini::Uuid id{0x0123456789abcdefULL};
    CHECK(id.to_string() == "0123456789abcdef");
    const auto parsed = jackmini::Uuid::parse(id.to_string());
    CHECK(parsed.has_value() && *parsed == id);
    CHECK(!jackmini::Uuid::parse("0123").has_value());
    CHECK(!jackmini::Uuid::parse("0123456789abcdeg").has_value());
    CHECK(jackmini::Uuid{}.empty());
    CHECK(jackmini::Uuid{1} < jackmini::Uuid{2});
}

void TestErrorText()
{
    CHECK(!jackmini::to_string(jackmini::Error::InvalidState).empty());
    CHECK(jackmini::to_string(jackmini::Error::NoServer) != jackmini::to_string(jackmini::Error::Io));
}

} // namespace

int main()
{
    TestRingCapacity();
    TestRingSingleThread();
    TestRingProducerConsumer();
    TestUuid();
    TestErrorText();

    std::printf("%d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
