// Copyright (C) 2026 JACK contributors
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation; either version 2.1 of the License, or
// (at your option) any later version.
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
// You should have received a copy of the GNU Lesser General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.

// jackmini_midi_dump: prints the MIDI events that reach an input port, one per line, with their time.  It does what
// jack_midi_dump did, with the same output.  The realtime callback only copies the events into a ring buffer; the main
// thread prints them.  Options: -a absolute times (frames since the start of this program), -r times relative to the
// previous event, -s the server, -v the version, -h the usage.  The client is named midi-monitor unless a name is given.

#include "tool_common.hpp"

#include <getopt.h>

#include <atomic>
#include <array>
#include <chrono>
#include <csignal>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <string>
#include <thread>

namespace {

constexpr const char* kTool = "jackmini_midi_dump";
constexpr std::size_t kMaxBytes = 4096;   // longer events are counted and skipped, as before
constexpr std::size_t kRingMessages = 100;

// One event, as the realtime thread hands it to the printer.  Plain data: copying it does not allocate.
struct Message {
    std::uint32_t m_time = 0;                  // the frame within the cycle
    std::uint32_t m_size = 0;
    std::uint64_t m_frame = 0;                 // the frame since this program started (the cycle's start plus the time)
    std::array<std::uint8_t, kMaxBytes> m_bytes { };
};

std::atomic<bool> gStop { false };
std::atomic<std::uint64_t> gTooLarge { 0 };
std::atomic<std::uint64_t> gRingFull { 0 };

void onSignal(std::int32_t)
{
    gStop.store(true);
}

void showUsage(FILE* out)
{
    std::fprintf(out, "jack_midi_dump - JACK MIDI Monitor.\n\n");
    std::fprintf(out, "Usage: %s [ OPTIONS ] [CLIENT-NAME]\n\n", kTool);
    std::fprintf(out, "Options:\n");
    std::fprintf(out, "  -a        use absolute timestamps relative to application start\n");
    std::fprintf(out, "  -h        display this help and exit\n");
    std::fprintf(out, "  -r        use relative timestamps to previous MIDI event\n");
    std::fprintf(out, "  -s        connect to the jack server named <name>\n");
    std::fprintf(out, "  -v        display the version and exit\n\n");
    std::fprintf(out, "This tool listens for MIDI events on a JACK MIDI port and prints\n");
    std::fprintf(out, "the message to stdout.\n\n");
    std::fprintf(out, "If no client name is given it defaults to 'midi-monitor'.\n\n");
}

// The message as the description the old tool printed: note on, note off and control change, with their channel.
void describe(const Message& m)
{
    if (m.m_size == 0) {
        return;
    }
    const std::uint8_t type = m.m_bytes[0] & 0xf0;
    const std::uint8_t channel = m.m_bytes[0] & 0xf;
    if (m.m_size != 3) {
        return;
    }
    switch (type) {
    case 0x90:
        std::printf(" note on  (channel %2d): pitch %3d, velocity %3d", channel, m.m_bytes[1], m.m_bytes[2]);
        break;
    case 0x80:
        std::printf(" note off (channel %2d): pitch %3d, velocity %3d", channel, m.m_bytes[1], m.m_bytes[2]);
        break;
    case 0xb0:
        std::printf(" control change (channel %2d): controller %3d, value %3d", channel, m.m_bytes[1], m.m_bytes[2]);
        break;
    default:
        break;
    }
}

} // namespace

int main(std::int32_t argc, char** argv)
{
    std::string server;
    std::int32_t timeFormat = 0; // 0: frame in the cycle, 1: absolute, 2: relative
    static constexpr std::string_view kShortOptions = "arhs:v";
    static std::array<const struct option, 4> kLongOptions = {{
        { "server", required_argument, nullptr, 's' },
        { "help", no_argument, nullptr, 'h' },
        { "version", no_argument, nullptr, 'v' },
        { nullptr, 0, nullptr, 0 },
    }};

    std::int32_t c;
    while ((c = getopt_long(argc, argv, kShortOptions.data(), kLongOptions.data(), nullptr)) != -1) {
        switch (c) {
        case 'a':
            timeFormat = 1;
            break;
        case 'r':
            timeFormat = 2;
            break;
        case 's':
            server = optarg;
            break;
        case 'v':
            std::printf("%s " JACK_VERSION "\n", kTool);
            return 0;
        case 'h':
            showUsage(stdout);
            return 0;
        default:
            showUsage(stderr);
            return 1;
        }
    }
    const std::string clientName = optind < argc ? argv[optind] : "midi-monitor";

    auto opened = jackmini::tools::openClient(clientName, server);
    if (!opened) {
        return jackmini::tools::fail(kTool, opened.error());
    }
    jackmini::Client& client = *opened;

    auto port = client.registerPort("input", jackmini::PortType::kMidi, jackmini::PortFlags::kInput);
    if (!port) {
        return jackmini::tools::fail(kTool, port.error());
    }
    const jackmini::Port input = *port;

    // The realtime thread's view: the frames since the start of the program, and the ring of events for the printer.
    jackmini::RingBuffer<Message> ring(kRingMessages);
    std::uint64_t framesBeforeCycle = 0;   // only the realtime thread writes it
    if (auto set = client.onProcess([&](const jackmini::Cycle& cycle) {
            const jackmini::midi::Buffer events = client.midiBuffer(input);
            for (std::size_t i = 0; i < events.count(); i++) {
                const jackmini::midi::Event event = events.event(i);
                if (event.m_data.size() > kMaxBytes) {
                    gTooLarge.fetch_add(1, std::memory_order_relaxed);
                    continue;
                }
                Message message;
                message.m_time = event.m_time;
                message.m_size = static_cast<std::uint32_t>(event.m_data.size());
                message.m_frame = framesBeforeCycle + event.m_time;
                for (std::size_t j = 0; j < event.m_data.size(); j++) {
                    message.m_bytes[j] = static_cast<std::uint8_t>(event.m_data[j]);
                }
                if (ring.write(std::span<const Message>(&message, 1)) == 0) {
                    gRingFull.fetch_add(1, std::memory_order_relaxed);
                }
            }
            framesBeforeCycle += cycle.m_frames;
            return true;
        }); !set) {
        return jackmini::tools::fail(kTool, set.error());
    }

    std::signal(SIGHUP, onSignal);
    std::signal(SIGINT, onSignal);
    if (auto activated = client.activate(); !activated) {
        return jackmini::tools::fail(kTool, activated.error());
    }

    std::uint64_t prevEvent = 0;
    std::uint64_t reportedTooLarge = 0;
    std::uint64_t reportedFull = 0;
    Message message;
    while (!gStop.load()) {
        while (ring.read(std::span<Message>(&message, 1)) == 1) {
            switch (timeFormat) {
            case 1:
                std::printf("%7" PRId64 ":", static_cast<std::int64_t>(message.m_frame));
                break;
            case 2:
                std::printf("%+6" PRId64 ":", static_cast<std::int64_t>(message.m_frame - prevEvent));
                break;
            default:
                std::printf("%4u:", message.m_time);
                break;
            }
            for (std::uint32_t j = 0; j < message.m_size; j++) {
                std::printf(" %02x", message.m_bytes[j]);
            }
            describe(message);
            std::printf("\n");
            prevEvent = message.m_frame;
        }
        // The counters of the realtime thread are reported here, not printed there.
        const std::uint64_t tooLarge = gTooLarge.load();
        if (tooLarge != reportedTooLarge) {
            std::fprintf(stderr, "Error: MIDI message was too large, skipping event. Max. allowed size: %zu bytes\n", kMaxBytes);
            reportedTooLarge = tooLarge;
        }
        const std::uint64_t full = gRingFull.load();
        if (full != reportedFull) {
            std::fprintf(stderr, "Error: ringbuffer was full, skipping event.\n");
            reportedFull = full;
        }
        std::fflush(stdout);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    std::fprintf(stderr, "Shutting down\n");
    client.deactivate();
    return 0;
}
