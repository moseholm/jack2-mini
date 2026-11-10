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

// The byte ringbuffer (jack_ringbuffer_*). The size is rounded up to a power of two, at least 2. One slot is always
// kept empty, so a buffer of size n holds n - 1 bytes. The test checks the sizes, the wrap-around, the two-segment
// vectors, and a transfer between two threads.

#include <array>
#include <string_view>
#include <cstddef>
#include <cstdint>
#include <jack/ringbuffer.h>

#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

namespace {

std::int32_t failures = 0;

#define CHECK(cond)                                              \
    do {                                                         \
        if (!(cond)) {                                           \
            std::fprintf(stderr, "FAILED %s:%d: %s\n", __FILE__, \
                __LINE__, #cond);                                \
            ++failures;                                          \
        }                                                        \
    } while (0)

void check_sizes()
{
    for (size_t requested : { size_t(0), size_t(1), size_t(2) }) {
        jack_ringbuffer_t* rb = jack_ringbuffer_create(requested);
        CHECK(rb != nullptr);
        CHECK(jack_ringbuffer_write_space(rb) == 1);
        jack_ringbuffer_free(rb);
    }

    jack_ringbuffer_t* rb = jack_ringbuffer_create(5);
    CHECK(rb != nullptr);
    CHECK(jack_ringbuffer_write_space(rb) == 7);
    jack_ringbuffer_free(rb);

    rb = jack_ringbuffer_create(8);
    CHECK(rb != nullptr);
    CHECK(jack_ringbuffer_write_space(rb) == 7);
    CHECK(jack_ringbuffer_mlock(rb) == 0);
    jack_ringbuffer_free(rb);
}

void check_round_trip_and_wrap()
{
    jack_ringbuffer_t* rb = jack_ringbuffer_create(8);
    CHECK(rb != nullptr);

    // Fill 5 of the 7 usable bytes, read 3, then write 6: the write wraps past the end.
    const std::string_view first = "abcde";
    CHECK(jack_ringbuffer_write(rb, first.data(), 5) == 5);
    std::array<char, 16> out = { };
    CHECK(jack_ringbuffer_read(rb, out.data(), 3) == 3);
    CHECK(std::memcmp(out.data(), "abc", 3) == 0);
    CHECK(jack_ringbuffer_read_space(rb) == 2);

    // Five bytes are free, so the write stops there and wraps past the end of the buffer.
    const std::string_view second = "fghijk";
    CHECK(jack_ringbuffer_write(rb, second.data(), 6) == 5);
    CHECK(jack_ringbuffer_read_space(rb) == 7);

    std::memset(out.data(), 0, sizeof out);
    CHECK(jack_ringbuffer_read(rb, out.data(), 16) == 7);
    CHECK(std::memcmp(out.data(), "defghij", 7) == 0);
    CHECK(jack_ringbuffer_read_space(rb) == 0);
    CHECK(jack_ringbuffer_read(rb, out.data(), 1) == 0);

    jack_ringbuffer_free(rb);
}

void check_peek_and_advance()
{
    jack_ringbuffer_t* rb = jack_ringbuffer_create(16);
    CHECK(jack_ringbuffer_write(rb, "0123456789", 10) == 10);

    std::array<char, 16> out = { };
    CHECK(jack_ringbuffer_peek(rb, out.data(), 4) == 4);
    CHECK(std::memcmp(out.data(), "0123", 4) == 0);
    CHECK(jack_ringbuffer_read_space(rb) == 10);

    jack_ringbuffer_read_advance(rb, 4);
    CHECK(jack_ringbuffer_read_space(rb) == 6);
    CHECK(jack_ringbuffer_peek(rb, out.data(), 2) == 2);
    CHECK(std::memcmp(out.data(), "45", 2) == 0);

    jack_ringbuffer_write_advance(rb, 0);
    jack_ringbuffer_reset(rb);
    CHECK(jack_ringbuffer_read_space(rb) == 0);
    CHECK(jack_ringbuffer_write_space(rb) == 15);
    jack_ringbuffer_free(rb);
}

void check_vectors()
{
    jack_ringbuffer_t* rb = jack_ringbuffer_create(8);
    std::array<jack_ringbuffer_data_t, 2> vec;

    // Write 6 bytes and read 5. The free space now starts at index 6, so the writer's vector wraps: 2 bytes at the
    // end of the buffer and 4 at the start.
    jack_ringbuffer_get_write_vector(rb, vec.data());
    CHECK(vec[0].len == 7);
    CHECK(jack_ringbuffer_write(rb, "uvwxyz", 6) == 6);
    std::array<char, 8> out = { };
    CHECK(jack_ringbuffer_read(rb, out.data(), 5) == 5);

    jack_ringbuffer_get_write_vector(rb, vec.data());
    CHECK(vec[0].len == 2);
    CHECK(vec[1].len == 4);

    jack_ringbuffer_get_read_vector(rb, vec.data());
    CHECK(vec[0].len == 1);
    CHECK(vec[0].buf[0] == 'z');
    CHECK(vec[1].len == 0);

    // The reader's view wraps too: "78" at the end of the buffer, then "9" at the start.
    jack_ringbuffer_reset(rb);
    CHECK(jack_ringbuffer_write(rb, "1234567", 7) == 7);
    jack_ringbuffer_read_advance(rb, 6);
    CHECK(jack_ringbuffer_write(rb, "89", 2) == 2);
    jack_ringbuffer_get_read_vector(rb, vec.data());
    CHECK(vec[0].len == 2);
    CHECK(std::memcmp(vec[0].buf, "78", 2) == 0);
    CHECK(vec[1].len == 1);
    CHECK(vec[1].buf[0] == '9');

    jack_ringbuffer_free(rb);
}

void check_reset_size()
{
    jack_ringbuffer_t* rb = jack_ringbuffer_create(16);
    CHECK(jack_ringbuffer_write(rb, "abcdef", 6) == 6);
    jack_ringbuffer_reset_size(rb, 4);
    CHECK(jack_ringbuffer_read_space(rb) == 0);
    CHECK(jack_ringbuffer_write_space(rb) == 3);
    jack_ringbuffer_free(rb);
}

// One thread writes a known sequence and the other reads it back. The bytes must come out in the same order, with
// nothing lost or repeated.
void check_two_threads()
{
    constexpr size_t kTotal = 1 << 22;
    jack_ringbuffer_t* rb = jack_ringbuffer_create(4096);
    bool ordered = true;

    std::thread writer([rb] {
        std::vector<std::byte> chunk(37);
        size_t sent = 0;
        std::uint8_t value = 0;
        while (sent < kTotal) {
            const size_t want = (kTotal - sent) < chunk.size() ? kTotal - sent : chunk.size();
            for (size_t i = 0; i < want; ++i) {
                chunk[i] = static_cast<std::byte>(value++);
            }
            size_t done = 0;
            while (done < want) {
                done += jack_ringbuffer_write(rb, reinterpret_cast<const char*>(chunk.data() + done), want - done);
            }
            sent += want;
        }
    });

    std::vector<std::byte> chunk(53);
    size_t received = 0;
    std::uint8_t expected = 0;
    while (received < kTotal) {
        const size_t got = jack_ringbuffer_read(rb, reinterpret_cast<char*>(chunk.data()), chunk.size());
        for (size_t i = 0; i < got; ++i) {
            if (std::to_integer<std::uint8_t>(chunk[i]) != expected++) {
                ordered = false;
            }
        }
        received += got;
    }

    writer.join();
    CHECK(ordered);
    CHECK(received == kTotal);
    CHECK(jack_ringbuffer_read_space(rb) == 0);
    jack_ringbuffer_free(rb);
}

} // end of anonymous namespace

int main()
{
    check_sizes();
    check_round_trip_and_wrap();
    check_peek_and_advance();
    check_vectors();
    check_reset_size();
    check_two_threads();

    if (failures) {
        std::fprintf(stderr, "ringbuffer_unit: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("ringbuffer_unit: all checks passed\n");
    return 0;
}
