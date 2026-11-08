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

// The client lifecycle of the jackmini API (client-api.md) with a running server: open, the callbacks, activate and
// deactivate, the rules for setting callbacks, identity, and the error reports.  Run it with a server started
// (for example `jackd -d dummy`); it is not part of ctest, which has no server.

#include "jackmini/jackmini.hpp"
#include "jackmini_wait.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstddef>
#include <string>
#include <thread>

using namespace jackmini;

namespace
{

int gFailures = 0;

void Check(const char* what, bool ok)
{
    std::printf("%-56s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) {
        gFailures++;
    }
}

} // namespace

int main(int argc, char** argv)
{
    OpenOptions options;
    options.start_server = false;
    if (argc > 1) {
        options.server_name = argv[1];
    }

    // No server under this name: the error says so, when the server is not to be started.
    OpenOptions missing;
    missing.start_server = false;
    missing.server_name = "jackmini_no_such_server";
    auto none = Client::open("jackmini_missing", missing);
    Check("open with no server: NoServer", !none && none.error() == Error::NoServer);

    auto opened = Client::open("jackmini_lifecycle", options);
    if (!opened) {
        std::printf("cannot open a client: %s\n", std::string(to_string(opened.error())).c_str());
        return 1;
    }
    Client client = std::move(*opened);

    Check("opened: not active", !client.is_active());
    Check("opened: name", client.name() == "jackmini_lifecycle");
    Check("opened: uuid is set", !client.uuid().empty());
    Check("opened: sample rate is set", client.sample_rate() != 0);
    Check("opened: buffer size is set", client.buffer_size() != 0);

    std::atomic<unsigned long> cycles{ 0 };
    std::atomic<bool> xrun_called{ false };
    Check("on_process while opened", client.on_process([&](FrameCount) {
        cycles++;
        return true;
    }).has_value());
    Check("on_xrun while opened", client.on_xrun([&] { xrun_called = true; }).has_value());

    Check("activate", client.activate().has_value());
    Check("active: is_active", client.is_active());
    Check("active: a second activate is refused", client.activate().error() == Error::InvalidState);
    Check("active: callbacks cannot be set", client.on_shutdown([] {}).error() == Error::InvalidState);

    // A clock client counts the cycles of the server; the checks below wait for cycles of the clock, not for time.
    auto clock_opened = Client::open("jackmini_clock", options);
    Check("clock client opens", clock_opened.has_value());
    if (!clock_opened) {
        return 1;
    }
    Client clock = std::move(*clock_opened);
    std::atomic<unsigned long> clock_cycles{ 0 };
    Check("clock: on_process", clock.on_process([&](FrameCount) {
        clock_cycles++;
        return true;
    }).has_value());
    Check("clock activates", clock.activate().has_value());

    Check("active: the process callback runs", WaitCycles(cycles, 10));

    Check("deactivate", client.deactivate().has_value());
    Check("inactive: is_active is false", !client.is_active());
    Check("inactive: a second deactivate is refused", client.deactivate().error() == Error::InvalidState);

    const unsigned long after = cycles.load();
    Check("inactive: the clock runs on", WaitCycles(clock_cycles, 200));
    Check("inactive: the process callback no longer runs", cycles.load() == after);

    Check("inactive: callbacks can be set again", client.on_buffer_size([](FrameCount) { return true; }).has_value());
    Check("reactivate", client.activate().has_value());
    Check("reactivated: the process callback runs again", WaitCycles(cycles, 10));
    Check("clock deactivates", clock.deactivate().has_value());
    clock.close();

    // Ports and connections.  A second client supplies a port of its own, so that find_port and the connections see
    // another client's port too.
    auto peer_opened = Client::open("jackmini_peer", options);
    Check("peer client opens", peer_opened.has_value());
    Client peer = std::move(*peer_opened);
    // The server connects only the ports of active clients, so the peer is activated for the connections.
    Check("peer client activates", peer.activate().has_value());

    auto out = client.register_port("out", PortType::Audio, PortFlags::Output);
    Check("register an audio output", out.has_value());
    Check("the port's name is client:name", out && client.port_name(*out) == "jackmini_lifecycle:out");
    Check("the port's type and flags", out && client.port_type(*out) == PortType::Audio &&
                                       has_flag(client.port_flags(*out), PortFlags::Output));
    Check("a duplicate port name: NameTaken", out && client.register_port("out", PortType::Audio, PortFlags::Output).error() == Error::NameTaken);
    Check("a name with a colon: InvalidArgument", client.register_port("a:b", PortType::Audio, PortFlags::Output).error() == Error::InvalidArgument);

    auto peer_in = peer.register_port("in", PortType::Audio, PortFlags::Input);
    auto peer_midi = peer.register_port("midi_in", PortType::Midi, PortFlags::Input);
    Check("the peer registers an input and a MIDI input", peer_in.has_value() && peer_midi.has_value());

    // A handle belongs to the client that asked for it: this client gets the peer's ports with find_port.
    auto in = client.find_port("jackmini_peer:in");
    auto midi_in = client.find_port("jackmini_peer:midi_in");
    Check("the peer's port is a port of another client", in && client.port_name(*in) == "jackmini_peer:in");

    auto listed = client.port_names(PortType::Audio, PortFlags::Input);
    bool found = false;
    for (const auto& name : listed) {
        found = found || name == "jackmini_peer:in";
    }
    Check("port_names lists the audio inputs", found);

    auto by_name = client.find_port("jackmini_peer:in");
    Check("find_port returns the same handle again", by_name.has_value() && in && *by_name == *in);
    Check("find_port of an unknown port: NotFound", client.find_port("no:such").error() == Error::NotFound);

    if (out && in) {
        Check("connect output to input", client.connect(*out, *in).has_value());
        Check("is_connected after connect", client.is_connected(*out, *in));
        Check("connections lists the input", client.connections(*out).size() == 1 &&
                                             client.connections(*out)[0] == "jackmini_peer:in");
        Check("connecting twice: InvalidState", client.connect(*out, *in).error() == Error::InvalidState);
        Check("disconnect", client.disconnect(*out, *in).has_value());
        Check("is_connected after disconnect is false", !client.is_connected(*out, *in));
        Check("disconnecting twice: NotFound", client.disconnect(*out, *in).error() == Error::NotFound);
    }
    if (out && midi_in) {
        Check("audio to MIDI: InvalidArgument", client.connect(*out, *midi_in).error() == Error::InvalidArgument);
    }
    if (out) {
        Check("unregister the output", client.unregister_port(*out).has_value());
        Check("an unregistered port has no name", client.port_name(*out).empty());
        Check("unregistering twice: NotFound", client.unregister_port(*out).error() == Error::NotFound);
    }
    peer.close();

    // Audio and MIDI through the realtime buffers, between two clients.  The source writes a constant and a MIDI event
    // in each cycle; the destination reads them in its process callback.
    auto src_opened = Client::open("jackmini_src", options);
    auto dst_opened = Client::open("jackmini_dst", options);
    Check("audio clients open", src_opened.has_value() && dst_opened.has_value());
    if (src_opened && dst_opened) {
        Client src = std::move(*src_opened);
        Client dst = std::move(*dst_opened);
        auto audio_out = src.register_port("aout", PortType::Audio, PortFlags::Output);
        auto midi_out = src.register_port("mout", PortType::Midi, PortFlags::Output);
        auto audio_in = dst.register_port("ain", PortType::Audio, PortFlags::Input);
        auto midi_in = dst.register_port("min", PortType::Midi, PortFlags::Input);
        Check("audio and MIDI ports register", audio_out && midi_out && audio_in && midi_in);

        std::atomic<unsigned long> dst_cycles{ 0 };
        std::atomic<float> last_sample{ -1.0f };
        std::atomic<std::size_t> midi_seen{ 0 };
        std::atomic<unsigned> first_byte{ 0 };
        std::atomic<unsigned> event_size{ 0 };
        if (audio_out && midi_out && audio_in && midi_in) {
            Port aout = *audio_out;
            Port mout = *midi_out;
            Port ain = *audio_in;
            Port min = *midi_in;
            Check("source process: set", src.on_process([&src, aout, mout](FrameCount) {
                for (auto& sample : src.audio_buffer(aout)) {
                    sample = 0.25f;
                }
                auto midi = src.midi_buffer(mout);
                midi.clear();
                const std::byte note[3] = { std::byte{ 0x90 }, std::byte{ 0x40 }, std::byte{ 0x7f } };
                midi.write(5, note);
                return true;
            }).has_value());
            Check("destination process: set", dst.on_process([&dst, ain, min, &dst_cycles, &last_sample, &midi_seen, &first_byte, &event_size](FrameCount) {
                dst_cycles++;
                auto samples = dst.audio_buffer(ain);
                if (!samples.empty()) {
                    last_sample = samples.back();
                }
                auto midi = dst.midi_buffer(min);
                midi_seen = midi.count();
                if (midi.count() > 0) {
                    const auto event = midi.event(0);
                    first_byte = static_cast<unsigned>(event.data.empty() ? 0 : std::to_integer<unsigned>(event.data[0]));
                    event_size = static_cast<unsigned>(event.data.size());
                }
                return true;
            }).has_value());
        }

        std::atomic<unsigned> latency_calls{ 0 };
        Check("latency callback: set while inactive", dst.on_latency([&](LatencyMode) { latency_calls++; }).has_value());
        Check("audio clients activate", src.activate().has_value() && dst.activate().has_value());
        if (audio_out) {
            Check("latency range: set and read back", src.set_port_latency(*audio_out, LatencyMode::Playback, LatencyRange{ 10, 20 }).has_value() &&
                                                       src.port_latency(*audio_out, LatencyMode::Playback) == (LatencyRange{ 10, 20 }));
        }
        auto aout_again = src.find_port("jackmini_src:aout");
        auto ain_again = src.find_port("jackmini_dst:ain");
        auto mout_again = src.find_port("jackmini_src:mout");
        auto min_again = src.find_port("jackmini_dst:min");
        if (aout_again && ain_again && mout_again && min_again) {
            Check("connect audio and MIDI", src.connect(*aout_again, *ain_again).has_value() &&
                                             src.connect(*mout_again, *min_again).has_value());
        }
        // Two cycles after the connections: the destination has run with them in place.
        Check("the destination reads the source's audio", WaitCycles(dst_cycles, 2) && std::fabs(last_sample.load() - 0.25f) < 1e-6f);
        Check("recompute the latencies", src.recompute_latencies().has_value());
        Check("the server asks for the latency (callback runs)", WaitUntil([&] { return latency_calls.load() > 0; }));
        Check("the destination reads the source's MIDI event", midi_seen.load() == 1 && first_byte.load() == 0x90 && event_size.load() == 3);

        Check("transport start", src.transport_start().has_value());
        Check("transport is rolling", WaitUntil([&] { return src.transport_state() != TransportState::Stopped; }));
        Check("transport stop", src.transport_stop().has_value());
        Check("transport is stopped", WaitUntil([&] { return src.transport_state() == TransportState::Stopped; }));
        Check("transport locate", src.transport_locate(1000).has_value());

        src.close();
        dst.close();
    }

    Check("close", client.close().has_value());
    Check("closed: name is empty", client.name().empty());
    Check("closed: a second close is harmless", client.close().has_value());

    // A name taken, with the name kept exactly: the second open is refused.
    OpenOptions exact = options;
    exact.exact_name = true;
    auto first = Client::open("jackmini_taken", exact);
    auto second = Client::open("jackmini_taken", exact);
    Check("exact name taken: NameTaken", first.has_value() && !second && second.error() == Error::NameTaken);

    std::printf("%s\n", gFailures == 0 ? "jackmini client lifecycle checks passed" : "jackmini client lifecycle checks FAILED");
    return gFailures == 0 ? 0 : 1;
}
