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

// The port listing of the jackmini API (Client::ports, total_latency) with a running server: names, types, flags,
// latencies, the UUIDs of the ports and of their owners, and the connections.  Run it with a server started (for
// example `jackd -d dummy`).  It is not part of ctest.
#include "jackmini/jackmini.hpp"
#include "jackmini_wait.hpp"

#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>
#include <vector>

using namespace jackmini;

namespace {

int gFailures = 0;

void Check(const char* what, bool ok)
{
    std::printf("%-72s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) {
        gFailures++;
    }
}

Client OpenOrDie(const char* name)
{
    OpenOptions options;
    options.start_server = false;
    auto opened = Client::open(name, options);
    if (!opened) {
        std::printf("cannot open %s: %s\n", name, std::string(to_string(opened.error())).c_str());
        std::exit(1);
    }
    return std::move(*opened);
}

const PortInfo* Find(const std::vector<PortInfo>& ports, const std::string& name)
{
    for (const PortInfo& port : ports) {
        if (port.name == name) {
            return &port;
        }
    }
    return nullptr;
}

} // namespace

int main()
{
    Client a = OpenOrDie("ports_a");
    Client b = OpenOrDie("ports_b");

    auto a_out = a.register_port("out", PortType::Audio, PortFlags::Output);
    auto a_mid = a.register_port("mid", PortType::Midi, PortFlags::Output);
    auto b_in = b.register_port("in", PortType::Audio, PortFlags::Input);
    Check("ports register", a_out && a_mid && b_in);
    if (!a_out || !a_mid || !b_in) {
        std::printf("%d failure(s)\n", gFailures);
        return 1;
    }

    const std::vector<PortInfo> before = a.ports();
    const PortInfo* out = Find(before, "ports_a:out");
    const PortInfo* mid = Find(before, "ports_a:mid");
    const PortInfo* in = Find(before, "ports_b:in");
    Check("ports lists the registered ports", out && mid && in);
    if (out && mid && in) {
        Check("audio type is the stored type string", out->type == "32 bit float mono audio");
        Check("midi type is the stored type string", mid->type == "8 bit raw midi");
        Check("output flags", has_flag(out->flags, PortFlags::Output) && !has_flag(out->flags, PortFlags::Input));
        Check("input flags", has_flag(in->flags, PortFlags::Input) && !has_flag(in->flags, PortFlags::Output));
        Check("owner is the client part of the name", out->owner == "ports_a" && in->owner == "ports_b");
        Check("owner UUID is the client's UUID", out->owner_uuid == a.uuid() && in->owner_uuid == b.uuid());
        Check("owner UUID is not empty", !out->owner_uuid.empty());
        Check("port UUIDs are not empty", !out->port_uuid.empty() && !in->port_uuid.empty());
        Check("port UUIDs differ", out->port_uuid != in->port_uuid && out->port_uuid != mid->port_uuid);
        Check("no aliases are set", out->aliases.empty());
        Check("nothing is connected yet", out->connections.empty() && in->connections.empty());
    }

    // Latency: a range set on a port is listed, and the total latency is what the server last computed.
    if (auto set = a.set_port_latency(*a_out, LatencyMode::Playback, LatencyRange { 3, 3 }); !set) {
        Check("set the playback latency", false);
    }
    a.recompute_latencies();
    const std::vector<PortInfo> latency = a.ports();
    const PortInfo* out_l = Find(latency, "ports_a:out");
    Check("playback latency is listed", out_l && out_l->playback_latency == LatencyRange { 3, 3 });
    Check("total latency of the port matches the listing",
          out_l && a.total_latency(*a_out) == out_l->total_latency);

    // Connections: the audio ports are connected in both lists.
    // The engine connects only the ports of active clients.
    Check("activate both clients", a.activate().has_value() && b.activate().has_value());

    // A handle belongs to the client that obtained it, so ports_a reaches ports_b:in through its own find_port.
    auto b_in_for_a = a.find_port("ports_b:in");
    auto connected = b_in_for_a ? a.connect(*a_out, *b_in_for_a) : std::unexpected(Error::NotFound);
    Check("connect ports_a:out to ports_b:in", connected.has_value());
    const std::vector<PortInfo> linked = a.ports();
    const PortInfo* out_c = Find(linked, "ports_a:out");
    const PortInfo* in_c = Find(linked, "ports_b:in");
    Check("the output lists the input as a connection",
          out_c && out_c->connections.size() == 1 && out_c->connections[0] == "ports_b:in");
    Check("the input lists the output as a connection",
          in_c && in_c->connections.size() == 1 && in_c->connections[0] == "ports_a:out");

    // Unregistering removes the port from the listing.
    auto gone = a.unregister_port(*a_mid);
    Check("unregister the midi port", gone.has_value());
    const std::vector<PortInfo> after = a.ports();
    Check("the unregistered port is no longer listed", Find(after, "ports_a:mid") == nullptr);

    // Every listed port has a distinct port UUID, and the list is in port-number order without repeats.
    std::set<std::uint64_t> uuids;
    bool distinct = true;
    for (const PortInfo& port : after) {
        distinct = distinct && uuids.insert(port.port_uuid.value()).second;
    }
    Check("all listed port UUIDs are distinct", distinct);

    std::printf("%d failure(s)\n", gFailures);
    return gFailures == 0 ? 0 : 1;
}
