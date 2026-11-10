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

// A MIDI source for the tests of jackmini_midi_dump: its output port "midi_writer:out" writes a note on (C4, velocity 64)
// in every cycle until it is stopped with SIGINT or SIGTERM.  Connect the output to the dump's input to see the events.
// It is not a tool.

#include "jackmini/Jackmini.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <thread>

namespace {

std::atomic<bool> gStop { false };

void onSignal(std::int32_t)
{
    gStop.store(true);
}

} // namespace

int main(std::int32_t argc, char** argv)
{
    std::string server;
    if (argc == 3 && std::string(argv[1]) == "-s") {
        server = argv[2];
    }

    jackmini::OpenOptions options;
    options.m_startServer = false;
    options.m_serverName = server;
    auto opened = jackmini::Client::open("midi_writer", options);
    if (!opened) {
        std::fprintf(stderr, "midi_writer: cannot open a client: %s\n",
            std::string(jackmini::toString(opened.error())).c_str());
        return 1;
    }
    jackmini::Client& client = *opened;

    auto port = client.registerPort("out", jackmini::PortType::kMidi, jackmini::PortFlags::kOutput);
    if (!port) {
        std::fprintf(stderr, "midi_writer: cannot register the port\n");
        return 1;
    }
    const jackmini::Port output = *port;

    static constexpr std::array<std::byte, 3> kNoteOn { std::byte { 0x90 }, std::byte { 0x3c }, std::byte { 0x40 } };
    if (auto set = client.onProcess([&](const jackmini::Cycle&) {
            jackmini::midi::Buffer events = client.midiBuffer(output);
            events.clear();
            events.write(0, kNoteOn);
            return true;
        });
        !set) {
        std::fprintf(stderr, "midi_writer: cannot set the process callback\n");
        return 1;
    }

    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);
    if (auto activated = client.activate(); !activated) {
        std::fprintf(stderr, "midi_writer: cannot activate\n");
        return 1;
    }
    while (!gStop.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    client.deactivate();
    return 0;
}
