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

// jackmini_connect and jackmini_disconnect: connect or disconnect two ports, given by name.  One source file: the
// disconnect program is built with JACKMINI_DISCONNECT defined.  They do what jack_connect and jack_disconnect did,
// with the same arguments: -s server, -u (the ports are given as <client UUID>:<port>), -h, -v, and two port names, one
// output and one input, in either order.

#include "tool_common.hpp"

#include <array>
#include <cstdint>
#include <getopt.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <thread>
#include <vector>

#ifdef JACKMINI_DISCONNECT
constexpr bool kDisconnect = true;
constexpr const char* kTool = "jackmini_disconnect";
#else
constexpr bool kDisconnect = false;
constexpr const char* kTool = "jackmini_connect";
#endif

namespace {

void showUsage(FILE* out)
{
    std::fprintf(out, "\nusage: %s [options] port1 port2\n", kTool);
    std::fprintf(out, kDisconnect ? "Disconnects two JACK ports.\n\n" : "Connects two JACK ports together.\n\n");
    std::fprintf(out, "        -s, --server <name>   Connect to the jack server named <name>\n");
    std::fprintf(out, "        -u, --uuid            The ports are given as <client UUID>:<port>\n");
    std::fprintf(out, "        -v, --version         Output version information and exit\n");
    std::fprintf(out, "        -h, --help            Display this help message\n\n");
}

// A port name given as <client UUID>:<port>: the client part is replaced by the client's name, when it is known.
std::string nameFromUuid(const std::string& text, const std::map<std::string, std::string>& names)
{
    const auto colon = text.find(':');
    if (colon == std::string::npos) {
        return text;
    }
    const auto client = names.find(text.substr(0, colon));
    if (client == names.end()) {
        return text;
    }
    return client->second + text.substr(colon);
}

// Waits, a bounded number of times, until the connection is in the state the tool asked for.  The server changes the
// graph in its next cycle; the tool waits for that, as jack_connect waited for the connect callback.
bool waitFor(const jackmini::Client& client, jackmini::Port source, jackmini::Port destination, bool connected)
{
    for (std::int32_t i = 0; i < 500; i++) {
        if (client.isConnected(source, destination) == connected) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

} // namespace

int main(std::int32_t argc, char** argv)
{
    std::string server;
    bool useUuid = false;
    static constexpr std::string_view kShortOptions = "s:uhv";
    static std::array<const struct option, 5> kLongOptions = {{
        { "server", required_argument, nullptr, 's' },
        { "uuid", no_argument, nullptr, 'u' },
        { "help", no_argument, nullptr, 'h' },
        { "version", no_argument, nullptr, 'v' },
        { nullptr, 0, nullptr, 0 },
    }};

    std::int32_t c;
    while ((c = getopt_long(argc, argv, kShortOptions.data(), kLongOptions.data(), nullptr)) != -1) {
        switch (c) {
        case 's':
            server = optarg;
            break;
        case 'u':
            useUuid = true;
            break;
        case 'h':
            showUsage(stderr);
            return 1;
        case 'v':
            std::printf("%s " JACK_VERSION "\n", kTool);
            return 0;
        default:
            showUsage(stderr);
            return 1;
        }
    }

    if (argc - optind < 2) {
        showUsage(stderr);
        return 1;
    }
    // As jack_connect did: the two last arguments are the ports.
    std::string first = argv[argc - 1];
    std::string second = argv[argc - 2];

    auto opened = jackmini::tools::openClient(kTool, server);
    if (!opened) {
        return jackmini::tools::fail(kTool, opened.error());
    }
    jackmini::Client& client = *opened;

    if (useUuid) {
        std::map<std::string, std::string> names;
        for (const jackmini::PortInfo& port : client.ports()) {
            names.emplace(std::to_string(port.m_ownerUuid.value()), port.m_owner);
        }
        first = nameFromUuid(first, names);
        second = nameFromUuid(second, names);
    }

    // The engine connects only the ports of active clients, as jack_connect required.
    if (auto activated = client.activate(); !activated) {
        return jackmini::tools::fail(kTool, activated.error());
    }

    auto port1 = client.findPort(first);
    if (!port1) {
        std::fprintf(stderr, "ERROR %s not a valid port\n", first.c_str());
        return 1;
    }
    auto port2 = client.findPort(second);
    if (!port2) {
        std::fprintf(stderr, "ERROR %s not a valid port\n", second.c_str());
        return 1;
    }

    // One input and one output: the output is the source, whichever order the arguments have.
    const jackmini::PortFlags flags1 = client.portFlags(*port1);
    const jackmini::PortFlags flags2 = client.portFlags(*port2);
    jackmini::Port source;
    jackmini::Port destination;
    bool found = false;
    if (jackmini::hasFlag(flags1, jackmini::PortFlags::kInput) && jackmini::hasFlag(flags2, jackmini::PortFlags::kOutput)) {
        source = *port2;
        destination = *port1;
        found = true;
    } else if (jackmini::hasFlag(flags1, jackmini::PortFlags::kOutput) && jackmini::hasFlag(flags2, jackmini::PortFlags::kInput)) {
        source = *port1;
        destination = *port2;
        found = true;
    }
    if (!found) {
        std::fprintf(stderr, "arguments must include 1 input port and 1 output port\n");
        return 1;
    }

    const std::string sourceName(client.portName(source));
    const std::string destinationName(client.portName(destination));
    if (kDisconnect) {
        if (auto done = client.disconnect(source, destination); !done) {
            const bool absent = done.error() == jackmini::Error::kNotFound;
            std::fprintf(stderr, "cannot disconnect %s from %s: %s\n", sourceName.c_str(), destinationName.c_str(),
                         absent ? "already disconnected" : std::string(jackmini::toString(done.error())).c_str());
            return 1;
        }
        return waitFor(client, source, destination, false) ? 0 : 1;
    }

    if (auto done = client.connect(source, destination); !done) {
        const bool linked = done.error() == jackmini::Error::kInvalidState;
        std::fprintf(stderr, "cannot connect %s to %s: %s\n", sourceName.c_str(), destinationName.c_str(),
                     linked ? "already connected" : std::string(jackmini::toString(done.error())).c_str());
        return 1;
    }
    return waitFor(client, source, destination, true) ? 0 : 1;
}
