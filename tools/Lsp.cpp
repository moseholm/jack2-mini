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

// jackmini_lsp: lists the ports of a running server, and optionally what each port has.  It does what jack_lsp did, with
// the same options and the same output, so that scripts that read it keep working.  It never starts a server.

#include "ToolCommon.hpp"

#include <getopt.h>
#include <array>
#include <cstdint>

#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace {

constexpr const char* kTool = "jackmini_lsp";

struct Options {
    std::string m_server;
    bool m_aliases = false;
    bool m_connections = false;
    bool m_portLatency = false;
    bool m_totalLatency = false;
    bool m_properties = false;
    bool m_type = false;
    bool m_uuid = false;
    bool m_portUuid = false;
};

void showUsage(FILE* out)
{
    std::fprintf(out, "\nUsage: %s [options] [filter string]\n", kTool);
    std::fprintf(out, "List active Jack ports, and optionally display extra information.\n");
    std::fprintf(out, "Optionally filter ports which match ALL strings provided after any options.\n\n");
    std::fprintf(out, "Display options:\n");
    std::fprintf(out, "        -s, --server <name>   Connect to the jack server named <name>\n");
    std::fprintf(out, "        -A, --aliases         List aliases for each port (none are set yet)\n");
    std::fprintf(out, "        -c, --connections     List connections to/from each port\n");
    std::fprintf(out, "        -l, --port-latency    Display per-port latency in frames at each port\n");
    std::fprintf(out, "        -L, --total-latency   Display total latency in frames at each port\n");
    std::fprintf(out, "        -p, --properties      Display port properties. Output may include:\n"
                      "                              input, output, can-monitor, physical, terminal\n");
    std::fprintf(out, "        -t, --type            Display port type\n");
    std::fprintf(out, "        -u, --uuid            Display uuid instead of client name\n");
    std::fprintf(out, "        -U, --port-uuid       Display port uuid\n");
    std::fprintf(out, "        -h, --help            Display this help message\n");
    std::fprintf(out, "        -v, --version         Output version information and exit\n\n");
}

// The UUID as the server writes it: the decimal of the 64-bit value (jack_uuid_unparse).
std::string uuidText(jackmini::Uuid uuid)
{
    return std::to_string(uuid.value());
}

// The name with its client part replaced by the UUID of that client (-u), when the client is known.
std::string withOwnerUuid(const std::string& name, const std::map<std::string, jackmini::Uuid>& owners)
{
    const auto colon = name.find(':');
    if (colon == std::string::npos) {
        return name;
    }
    const auto owner = owners.find(name.substr(0, colon));
    if (owner == owners.end()) {
        return name;
    }
    return uuidText(owner->second) + name.substr(colon);
}

// Every filter string must occur in the port name (a substring, as jack_lsp matched them).
bool matchesAll(const std::string& name, const std::vector<char*>& filters)
{
    for (const char* filter : filters) {
        if (name.find(filter) == std::string::npos) {
            return false;
        }
    }
    return true;
}

void printProperties(jackmini::PortFlags flags)
{
    std::fputs("\tproperties: ", stdout);
    if (jackmini::hasFlag(flags, jackmini::PortFlags::kInput)) {
        std::fputs("input,", stdout);
    }
    if (jackmini::hasFlag(flags, jackmini::PortFlags::kOutput)) {
        std::fputs("output,", stdout);
    }
    if (jackmini::hasFlag(flags, jackmini::PortFlags::kCanMonitor)) {
        std::fputs("can-monitor,", stdout);
    }
    if (jackmini::hasFlag(flags, jackmini::PortFlags::kPhysical)) {
        std::fputs("physical,", stdout);
    }
    if (jackmini::hasFlag(flags, jackmini::PortFlags::kTerminal)) {
        std::fputs("terminal,", stdout);
    }
    std::putc('\n', stdout);
}

} // namespace

int main(std::int32_t argc, char** argv)
{
    Options options;
    static constexpr std::string_view kShortOptions = "s:AclLpthvtuU";
    static std::array<const struct option, 12> kLongOptions = { {
        { "server", required_argument, nullptr, 's' },
        { "aliases", no_argument, nullptr, 'A' },
        { "connections", no_argument, nullptr, 'c' },
        { "port-latency", no_argument, nullptr, 'l' },
        { "total-latency", no_argument, nullptr, 'L' },
        { "properties", no_argument, nullptr, 'p' },
        { "type", no_argument, nullptr, 't' },
        { "uuid", no_argument, nullptr, 'u' },
        { "port-uuid", no_argument, nullptr, 'U' },
        { "help", no_argument, nullptr, 'h' },
        { "version", no_argument, nullptr, 'v' },
        { nullptr, 0, nullptr, 0 },
    } };

    std::int32_t c;
    while ((c = getopt_long(argc, argv, kShortOptions.data(), kLongOptions.data(), nullptr)) != -1) {
        switch (c) {
        case 's':
            options.m_server = optarg;
            break;
        case 'A':
            options.m_aliases = true;
            break;
        case 'c':
            options.m_connections = true;
            break;
        case 'l':
            options.m_portLatency = true;
            break;
        case 'L':
            options.m_totalLatency = true;
            break;
        case 'p':
            options.m_properties = true;
            break;
        case 't':
            options.m_type = true;
            break;
        case 'u':
            options.m_uuid = true;
            break;
        case 'U':
            options.m_portUuid = true;
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

    std::vector<char*> filters(argv + optind, argv + argc);

    auto opened = jackmini::tools::openClient("jackmini_lsp", options.m_server);
    if (!opened) {
        if (opened.error() == jackmini::Error::kNoServer) {
            std::fprintf(stderr, "Error: cannot connect to JACK, server is not running.\n");
            return 1;
        }
        return jackmini::tools::fail(kTool, opened.error());
    }
    jackmini::Client& client = *opened;

    const std::vector<jackmini::PortInfo> ports = client.ports();

    // The UUID of each client, from the ports it owns: -u prints them for the owners of the ports and of the connections.
    std::map<std::string, jackmini::Uuid> owners;
    for (const jackmini::PortInfo& port : ports) {
        owners.emplace(port.m_owner, port.m_ownerUuid);
    }

    for (const jackmini::PortInfo& port : ports) {
        if (!matchesAll(port.m_name, filters)) {
            continue;
        }

        std::printf("%s\n", options.m_uuid ? withOwnerUuid(port.m_name, owners).c_str() : port.m_name.c_str());

        if (options.m_portUuid) {
            std::printf("\tuuid: %s\n", uuidText(port.m_portUuid).c_str());
        }

        if (options.m_aliases) {
            for (const std::string& alias : port.m_aliases) {
                std::printf("   %s\n", alias.c_str());
            }
        }

        if (options.m_connections) {
            for (const std::string& connected : port.m_connections) {
                std::printf("   %s\n", (options.m_uuid ? withOwnerUuid(connected, owners) : connected).c_str());
            }
        }

        if (options.m_portLatency) {
            std::printf("\tport playback latency = [ %u %u ] frames\n",
                port.m_playbackLatency.m_min, port.m_playbackLatency.m_max);
            std::printf("\tport capture latency = [ %u %u ] frames\n",
                port.m_captureLatency.m_min, port.m_captureLatency.m_max);
        }

        if (options.m_totalLatency) {
            std::printf("\ttotal latency = %u frames\n", port.m_totalLatency);
        }

        if (options.m_properties) {
            printProperties(port.m_flags);
        }

        if (options.m_type) {
            std::printf("\t%s\n", port.m_type.c_str());
        }
    }

    return 0;
}
