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

#include <atomic>
#include <chrono>
#include <cstdio>
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

    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    Check("active: the process callback runs each cycle", cycles.load() > 10);

    Check("deactivate", client.deactivate().has_value());
    Check("inactive: is_active is false", !client.is_active());
    Check("inactive: a second deactivate is refused", client.deactivate().error() == Error::InvalidState);

    const unsigned long after = cycles.load();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    Check("inactive: the process callback no longer runs", cycles.load() == after);

    Check("inactive: callbacks can be set again", client.on_buffer_size([](FrameCount) { return true; }).has_value());
    Check("reactivate", client.activate().has_value());
    const unsigned long resumed = cycles.load();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    Check("reactivated: the process callback runs again", cycles.load() > resumed);

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
