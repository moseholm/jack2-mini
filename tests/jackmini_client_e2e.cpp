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

// The client lifecycle of the jackmini API (client-api.md) with a running server: open, the callbacks, activate and
// deactivate, the rules for setting callbacks, identity, and the error reports.  Run it with a server started
// (for example `jackd -d dummy`); it is not part of ctest, which has no server.

#include "jackmini/jackmini.hpp"
#include "jackmini_wait.hpp"

#include <array>
#include <cstdint>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <string>
#include <thread>

using namespace jackmini;

namespace {

std::int32_t gFailures = 0;

void check(const char* what, bool ok)
{
    std::printf("%-56s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) {
        gFailures++;
    }
}

} // namespace

int main(std::int32_t argc, char** argv)
{
    OpenOptions options;
    options.m_startServer = false;
    if (argc > 1) {
        options.m_serverName = argv[1];
    }

    // No server under this name: the error says so, when the server is not to be started.
    OpenOptions missing;
    missing.m_startServer = false;
    missing.m_serverName = "jackmini_no_such_server";
    auto none = Client::open("jackmini_missing", missing);
    check("open with no server: NoServer", !none && none.error() == Error::kNoServer);

    auto opened = Client::open("jackmini_lifecycle", options);
    if (!opened) {
        std::printf("cannot open a client: %s\n", std::string(toString(opened.error())).c_str());
        return 1;
    }
    Client client = std::move(*opened);

    check("opened: not active", !client.isActive());
    check("opened: name", client.name() == "jackmini_lifecycle");
    check("opened: uuid is set", !client.uuid().empty());
    check("opened: sample rate is set", client.sampleRate() != 0);
    check("opened: buffer size is set", client.bufferSize() != 0);

    std::atomic<std::uint64_t> cycles { 0 };
    std::atomic<bool> xrunCalled { false };
    check("on_process while opened", client.onProcess([&](const Cycle&) {
                                               cycles++;
                                               return true;
                                           })
                                         .has_value());
    check("on_xrun while opened", client.onXrun([&] { xrunCalled = true; }).has_value());

    check("activate", client.activate().has_value());
    check("active: is_active", client.isActive());
    check("active: a second activate is refused", client.activate().error() == Error::kInvalidState);
    check("active: callbacks cannot be set", client.onShutdown([] { }).error() == Error::kInvalidState);

    // A clock client counts the cycles of the server; the checks below wait for cycles of the clock, not for time.
    auto clockOpened = Client::open("jackmini_clock", options);
    check("clock client opens", clockOpened.has_value());
    if (!clockOpened) {
        return 1;
    }
    Client clock = std::move(*clockOpened);
    std::atomic<std::uint64_t> clockCycles { 0 };
    check("clock: on_process", clock.onProcess([&](const Cycle&) {
                                        clockCycles++;
                                        return true;
                                    })
                                   .has_value());
    check("clock activates", clock.activate().has_value());

    check("active: the process callback runs", waitCycles(cycles, 10));

    check("deactivate", client.deactivate().has_value());
    check("inactive: is_active is false", !client.isActive());
    check("inactive: a second deactivate is refused", client.deactivate().error() == Error::kInvalidState);

    const std::uint64_t after = cycles.load();
    check("inactive: the clock runs on", waitCycles(clockCycles, 200));
    check("inactive: the process callback no longer runs", cycles.load() == after);

    check("inactive: callbacks can be set again", client.onBufferSize([](FrameCount) { return true; }).has_value());
    check("reactivate", client.activate().has_value());
    check("reactivated: the process callback runs again", waitCycles(cycles, 10));
    check("clock deactivates", clock.deactivate().has_value());
    clock.close();

    // Ports and connections.  A second client supplies a port of its own, so that find_port and the connections see
    // another client's port too.
    auto peerOpened = Client::open("jackmini_peer", options);
    check("peer client opens", peerOpened.has_value());
    Client peer = std::move(*peerOpened);
    // The server connects only the ports of active clients, so the peer is activated for the connections.
    check("peer client activates", peer.activate().has_value());

    auto out = client.registerPort("out", PortType::kAudio, PortFlags::kOutput);
    check("register an audio output", out.has_value());
    check("the port's name is client:name", out && client.portName(*out) == "jackmini_lifecycle:out");
    check("the port's type and flags", out && client.portType(*out) == PortType::kAudio && hasFlag(client.portFlags(*out), PortFlags::kOutput));
    check("a duplicate port name: NameTaken", out && client.registerPort("out", PortType::kAudio, PortFlags::kOutput).error() == Error::kNameTaken);
    check("a name with a colon: InvalidArgument", client.registerPort("a:b", PortType::kAudio, PortFlags::kOutput).error() == Error::kInvalidArgument);

    auto peerIn = peer.registerPort("in", PortType::kAudio, PortFlags::kInput);
    auto peerMidi = peer.registerPort("midi_in", PortType::kMidi, PortFlags::kInput);
    check("the peer registers an input and a MIDI input", peerIn.has_value() && peerMidi.has_value());

    // A handle belongs to the client that asked for it: this client gets the peer's ports with find_port.
    auto in = client.findPort("jackmini_peer:in");
    auto midiIn = client.findPort("jackmini_peer:midi_in");
    check("the peer's port is a port of another client", in && client.portName(*in) == "jackmini_peer:in");

    auto listed = client.portNames(PortType::kAudio, PortFlags::kInput);
    bool found = false;
    for (const auto& name : listed) {
        found = found || name == "jackmini_peer:in";
    }
    check("port_names lists the audio inputs", found);

    auto byName = client.findPort("jackmini_peer:in");
    check("find_port returns the same handle again", byName.has_value() && in && *byName == *in);
    check("find_port of an unknown port: NotFound", client.findPort("no:such").error() == Error::kNotFound);

    if (out && in) {
        check("connect output to input", client.connect(*out, *in).has_value());
        check("is_connected after connect", client.isConnected(*out, *in));
        check("connections lists the input", client.connections(*out).size() == 1 && client.connections(*out)[0] == "jackmini_peer:in");
        check("connecting twice: InvalidState", client.connect(*out, *in).error() == Error::kInvalidState);
        check("disconnect", client.disconnect(*out, *in).has_value());
        check("is_connected after disconnect is false", !client.isConnected(*out, *in));
        check("disconnecting twice: NotFound", client.disconnect(*out, *in).error() == Error::kNotFound);
    }
    if (out && midiIn) {
        check("audio to MIDI: InvalidArgument", client.connect(*out, *midiIn).error() == Error::kInvalidArgument);
    }
    if (out) {
        check("unregister the output", client.unregisterPort(*out).has_value());
        check("an unregistered port has no name", client.portName(*out).empty());
        check("unregistering twice: NotFound", client.unregisterPort(*out).error() == Error::kNotFound);
    }
    peer.close();

    // Audio and MIDI through the realtime buffers, between two clients.  The source writes a constant and a MIDI event
    // in each cycle; the destination reads them in its process callback.
    auto srcOpened = Client::open("jackmini_src", options);
    auto dstOpened = Client::open("jackmini_dst", options);
    check("audio clients open", srcOpened.has_value() && dstOpened.has_value());
    if (srcOpened && dstOpened) {
        Client src = std::move(*srcOpened);
        Client dst = std::move(*dstOpened);
        auto audioOut = src.registerPort("aout", PortType::kAudio, PortFlags::kOutput);
        auto midiOut = src.registerPort("mout", PortType::kMidi, PortFlags::kOutput);
        auto audioIn = dst.registerPort("ain", PortType::kAudio, PortFlags::kInput);
        auto midiIn = dst.registerPort("min", PortType::kMidi, PortFlags::kInput);
        check("audio and MIDI ports register", audioOut && midiOut && audioIn && midiIn);

        std::atomic<std::uint64_t> dstCycles { 0 };
        std::atomic<float> lastSample { -1.0f };
        std::atomic<std::size_t> midiSeen { 0 };
        std::atomic<std::uint32_t> firstByte { 0 };
        std::atomic<std::uint32_t> eventSize { 0 };
        if (audioOut && midiOut && audioIn && midiIn) {
            Port aout = *audioOut;
            Port mout = *midiOut;
            Port ain = *audioIn;
            Port min = *midiIn;
            check("source process: set", src.onProcess([&src, aout, mout](const Cycle&) {
                                                for (auto& sample : src.audioBuffer(aout)) {
                                                    sample = 0.25f;
                                                }
                                                auto midi = src.midiBuffer(mout);
                                                midi.clear();
                                                std::array<const std::byte, 3> note = { std::byte { 0x90 }, std::byte { 0x40 }, std::byte { 0x7f } };
                                                midi.write(5, note);
                                                return true;
                                            })
                                             .has_value());
            check("destination process: set", dst.onProcess([&dst, ain, min, &dstCycles, &lastSample, &midiSeen, &firstByte, &eventSize](const Cycle&) {
                                                     dstCycles++;
                                                     auto samples = dst.audioBuffer(ain);
                                                     if (!samples.empty()) {
                                                         lastSample = samples.back();
                                                     }
                                                     auto midi = dst.midiBuffer(min);
                                                     midiSeen = midi.count();
                                                     if (midi.count() > 0) {
                                                         const auto event = midi.event(0);
                                                         firstByte = static_cast<std::uint32_t>(event.m_data.empty() ? 0 : std::to_integer<std::uint32_t>(event.m_data[0]));
                                                         eventSize = static_cast<std::uint32_t>(event.m_data.size());
                                                     }
                                                     return true;
                                                 })
                                                  .has_value());
        }

        std::atomic<std::uint32_t> latencyCalls { 0 };
        check("latency callback: set while inactive", dst.onLatency([&](LatencyMode) { latencyCalls++; }).has_value());
        check("audio clients activate", src.activate().has_value() && dst.activate().has_value());
        if (audioOut) {
            check("latency range: set and read back", src.setPortLatency(*audioOut, LatencyMode::kPlayback, LatencyRange { 10, 20 }).has_value() && src.portLatency(*audioOut, LatencyMode::kPlayback) == (LatencyRange { 10, 20 }));
        }
        auto aoutAgain = src.findPort("jackmini_src:aout");
        auto ainAgain = src.findPort("jackmini_dst:ain");
        auto moutAgain = src.findPort("jackmini_src:mout");
        auto minAgain = src.findPort("jackmini_dst:min");
        if (aoutAgain && ainAgain && moutAgain && minAgain) {
            check("connect audio and MIDI", src.connect(*aoutAgain, *ainAgain).has_value() && src.connect(*moutAgain, *minAgain).has_value());
        }
        // Two cycles after the connections: the destination has run with them in place.
        check("the destination reads the source's audio", waitCycles(dstCycles, 2) && std::fabs(lastSample.load() - 0.25f) < 1e-6f);
        check("recompute the latencies", src.recomputeLatencies().has_value());
        check("the server asks for the latency (callback runs)", waitUntil([&] { return latencyCalls.load() > 0; }));
        check("the destination reads the source's MIDI event", midiSeen.load() == 1 && firstByte.load() == 0x90 && eventSize.load() == 3);

        check("transport start", src.transportStart().has_value());
        check("transport is rolling", waitUntil([&] { return src.transportState() != TransportState::kStopped; }));
        check("transport stop", src.transportStop().has_value());
        check("transport is stopped", waitUntil([&] { return src.transportState() == TransportState::kStopped; }));
        check("transport locate", src.transportLocate(1000).has_value());

        src.close();
        dst.close();
    }

    check("close", client.close().has_value());
    check("closed: name is empty", client.name().empty());
    check("closed: a second close is harmless", client.close().has_value());

    // A name taken, with the name kept exactly: the second open is refused.
    OpenOptions exact = options;
    exact.m_exactName = true;
    auto first = Client::open("jackmini_taken", exact);
    auto second = Client::open("jackmini_taken", exact);
    check("exact name taken: NameTaken", first.has_value() && !second && second.error() == Error::kNameTaken);

    std::printf("%s\n", gFailures == 0 ? "jackmini client lifecycle checks passed" : "jackmini client lifecycle checks FAILED");
    return gFailures == 0 ? 0 : 1;
}
