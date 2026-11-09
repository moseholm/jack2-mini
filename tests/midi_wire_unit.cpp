/*
Copyright (C) 2026 JACK contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

// Unit tests for the MIDI byte codec of the raw MIDI backend: running status on output, and message parsing on input.

#include "midi_wire.hpp"

#include <cstdio>
#include <vector>

using namespace jackmini::alsa::midi;

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond) do { \
        g_checks++; \
        if (!(cond)) { \
            g_failures++; \
            std::printf("  FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        } \
    } while (0)

namespace
{

using Message = std::vector<std::uint8_t>;

// Parses a sequence of device bytes, in chunks of the given sizes, and returns the messages in order.
std::vector<Message> Parse(Unpacker& unpacker, const Message& bytes, std::size_t chunk = 0)
{
    std::vector<Message> out;
    auto emit = [&out](Bytes message) { out.emplace_back(message.begin(), message.end()); };
    const std::size_t step = chunk == 0 ? bytes.size() : chunk;
    for (std::size_t at = 0; at < bytes.size(); at += step) {
        const std::size_t n = std::min(step, bytes.size() - at);
        unpacker.feed(Bytes(bytes.data() + at, n), emit);
    }
    return out;
}

void TestPackerRunningStatus()
{
    std::printf("midi: output drops a repeated voice status\n");
    Packer packer;
    const std::uint8_t on1[] = {0x90, 0x40, 0x7f};
    const std::uint8_t on2[] = {0x90, 0x41, 0x7f};
    const std::uint8_t off[] = {0x80, 0x40, 0x00};

    Bytes first = packer.pack(on1);
    CHECK(first.size() == 3 && first[0] == 0x90);
    Bytes second = packer.pack(on2);
    CHECK(second.size() == 2 && second[0] == 0x41 && second[1] == 0x7f);
    Bytes third = packer.pack(off);
    CHECK(third.size() == 3 && third[0] == 0x80);
}

void TestPackerResets()
{
    std::printf("midi: data and system common end the running status, realtime does not\n");
    Packer packer;
    const std::uint8_t on[] = {0x90, 0x40, 0x7f};
    const std::uint8_t realtime[] = {0xF8};
    const std::uint8_t common[] = {0xF1, 0x20};

    packer.pack(on);
    CHECK(packer.pack(realtime).size() == 1);
    CHECK(packer.pack(on).size() == 2);  // still in running status after the realtime byte

    packer.pack(common);
    CHECK(packer.pack(on).size() == 3);  // the common message ended it
}

void TestUnpackerMessages()
{
    std::printf("midi: input messages and running status\n");
    Unpacker unpacker;
    const Message bytes = {0x90, 0x40, 0x7f, 0x41, 0x7f, 0xB0, 0x07, 0x64};
    const std::vector<Message> messages = Parse(unpacker, bytes);
    CHECK(messages.size() == 3);
    CHECK(messages.size() > 0 && messages[0] == Message({0x90, 0x40, 0x7f}));
    CHECK(messages.size() > 1 && messages[1] == Message({0x90, 0x41, 0x7f}));
    CHECK(messages.size() > 2 && messages[2] == Message({0xB0, 0x07, 0x64}));
}

void TestUnpackerNoteOnZeroIsNoteOff()
{
    std::printf("midi: a Note On with velocity 0 is a Note Off\n");
    Unpacker unpacker;
    const std::vector<Message> messages = Parse(unpacker, {0x93, 0x3c, 0x00});
    CHECK(messages.size() == 1);
    CHECK(messages.size() > 0 && messages[0] == Message({0x83, 0x3c, 0x40}));
}

void TestUnpackerSplitReads()
{
    std::printf("midi: a message split over reads is kept\n");
    Unpacker unpacker;
    const Message bytes = {0xE0, 0x00, 0x40, 0xC1, 0x05};
    for (std::size_t chunk = 1; chunk <= bytes.size(); ++chunk) {
        unpacker.reset();
        const std::vector<Message> messages = Parse(unpacker, bytes, chunk);
        CHECK(messages.size() == 2);
        CHECK(messages.size() > 0 && messages[0] == Message({0xE0, 0x00, 0x40}));
        CHECK(messages.size() > 1 && messages[1] == Message({0xC1, 0x05}));
    }
}

void TestUnpackerRealtimeBetweenBytes()
{
    std::printf("midi: a realtime byte is passed on inside a message\n");
    Unpacker unpacker;
    const std::vector<Message> messages = Parse(unpacker, {0x90, 0x40, 0xF8, 0x7f});
    CHECK(messages.size() == 2);
    CHECK(messages.size() > 0 && messages[0] == Message({0xF8}));
    CHECK(messages.size() > 1 && messages[1] == Message({0x90, 0x40, 0x7f}));
}

void TestUnpackerSystem()
{
    std::printf("midi: system exclusive, common and realtime messages\n");
    Unpacker unpacker;
    const std::vector<Message> messages = Parse(unpacker, {0xF0, 0x01, 0x02, 0xF7, 0xF1, 0x20, 0xF6});
    CHECK(messages.size() == 3);
    CHECK(messages.size() > 0 && messages[0] == Message({0xF0, 0x01, 0x02, 0xF7}));
    CHECK(messages.size() > 1 && messages[1] == Message({0xF1, 0x20}));
    CHECK(messages.size() > 2 && messages[2] == Message({0xF6}));
}

void TestUnpackerOversizedSysex()
{
    std::printf("midi: a system exclusive message longer than the buffer is dropped\n");
    Unpacker unpacker;
    Message bytes = {0xF0};
    bytes.insert(bytes.end(), Unpacker::kMaxMessage * 2, 0x01);
    bytes.push_back(0xF7);
    bytes.push_back(0x90);
    bytes.push_back(0x40);
    bytes.push_back(0x7f);
    const std::vector<Message> messages = Parse(unpacker, bytes);
    CHECK(messages.size() == 1);
    CHECK(messages.size() > 0 && messages[0] == Message({0x90, 0x40, 0x7f}));
}

} // namespace

int main()
{
    TestPackerRunningStatus();
    TestPackerResets();
    TestUnpackerMessages();
    TestUnpackerNoteOnZeroIsNoteOff();
    TestUnpackerSplitReads();
    TestUnpackerRealtimeBetweenBytes();
    TestUnpackerSystem();
    TestUnpackerOversizedSysex();

    std::printf("%d checks, %d failed\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
