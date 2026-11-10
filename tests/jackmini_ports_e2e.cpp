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

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>
#include <vector>

using namespace jackmini;

namespace {

std::int32_t gFailures = 0;

void check(const char* what, bool ok)
{
    std::printf("%-72s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) {
        gFailures++;
    }
}

Client openOrDie(const char* name)
{
    OpenOptions options;
    options.m_startServer = false;
    auto opened = Client::open(name, options);
    if (!opened) {
        std::printf("cannot open %s: %s\n", name, std::string(toString(opened.error())).c_str());
        std::exit(1);
    }
    return std::move(*opened);
}

const PortInfo* find(const std::vector<PortInfo>& ports, const std::string& name)
{
    for (const PortInfo& port : ports) {
        if (port.m_name == name) {
            return &port;
        }
    }
    return nullptr;
}

} // namespace

int main()
{
    Client a = openOrDie("ports_a");
    Client b = openOrDie("ports_b");

    auto aOut = a.registerPort("out", PortType::kAudio, PortFlags::kOutput);
    auto aMid = a.registerPort("mid", PortType::kMidi, PortFlags::kOutput);
    auto bIn = b.registerPort("in", PortType::kAudio, PortFlags::kInput);
    check("ports register", aOut && aMid && bIn);
    if (!aOut || !aMid || !bIn) {
        std::printf("%d failure(s)\n", gFailures);
        return 1;
    }

    const std::vector<PortInfo> before = a.ports();
    const PortInfo* out = find(before, "ports_a:out");
    const PortInfo* mid = find(before, "ports_a:mid");
    const PortInfo* in = find(before, "ports_b:in");
    check("ports lists the registered ports", out && mid && in);
    if (out && mid && in) {
        check("audio type is the stored type string", out->m_type == "32 bit float mono audio");
        check("midi type is the stored type string", mid->m_type == "8 bit raw midi");
        check("output flags", hasFlag(out->m_flags, PortFlags::kOutput) && !hasFlag(out->m_flags, PortFlags::kInput));
        check("input flags", hasFlag(in->m_flags, PortFlags::kInput) && !hasFlag(in->m_flags, PortFlags::kOutput));
        check("owner is the client part of the name", out->m_owner == "ports_a" && in->m_owner == "ports_b");
        check("owner UUID is the client's UUID", out->m_ownerUuid == a.uuid() && in->m_ownerUuid == b.uuid());
        check("owner UUID is not empty", !out->m_ownerUuid.empty());
        check("port UUIDs are not empty", !out->m_portUuid.empty() && !in->m_portUuid.empty());
        check("port UUIDs differ", out->m_portUuid != in->m_portUuid && out->m_portUuid != mid->m_portUuid);
        check("no aliases are set", out->m_aliases.empty());
        check("nothing is connected yet", out->m_connections.empty() && in->m_connections.empty());
    }

    // Latency: a range set on a port is listed, and the total latency is what the server last computed.
    if (auto set = a.setPortLatency(*aOut, LatencyMode::kPlayback, LatencyRange { 3, 3 }); !set) {
        check("set the playback latency", false);
    }
    a.recomputeLatencies();
    const std::vector<PortInfo> latency = a.ports();
    const PortInfo* outL = find(latency, "ports_a:out");
    check("playback latency is listed", outL && outL->m_playbackLatency == LatencyRange { 3, 3 });
    check("total latency of the port matches the listing",
          outL && a.totalLatency(*aOut) == outL->m_totalLatency);

    // Connections: the audio ports are connected in both lists.
    // The engine connects only the ports of active clients.
    check("activate both clients", a.activate().has_value() && b.activate().has_value());

    // A handle belongs to the client that obtained it, so ports_a reaches ports_b:in through its own find_port.
    auto bInForA = a.findPort("ports_b:in");
    auto connected = bInForA ? a.connect(*aOut, *bInForA) : std::unexpected(Error::kNotFound);
    check("connect ports_a:out to ports_b:in", connected.has_value());
    const std::vector<PortInfo> linked = a.ports();
    const PortInfo* outC = find(linked, "ports_a:out");
    const PortInfo* inC = find(linked, "ports_b:in");
    check("the output lists the input as a connection",
          outC && outC->m_connections.size() == 1 && outC->m_connections[0] == "ports_b:in");
    check("the input lists the output as a connection",
          inC && inC->m_connections.size() == 1 && inC->m_connections[0] == "ports_a:out");

    // Unregistering removes the port from the listing.
    auto gone = a.unregisterPort(*aMid);
    check("unregister the midi port", gone.has_value());
    const std::vector<PortInfo> after = a.ports();
    check("the unregistered port is no longer listed", find(after, "ports_a:mid") == nullptr);

    // Every listed port has a distinct port UUID, and the list is in port-number order without repeats.
    std::set<std::uint64_t> uuids;
    bool distinct = true;
    for (const PortInfo& port : after) {
        distinct = distinct && uuids.insert(port.m_portUuid.value()).second;
    }
    check("all listed port UUIDs are distinct", distinct);

    std::printf("%d failure(s)\n", gFailures);
    return gFailures == 0 ? 0 : 1;
}
