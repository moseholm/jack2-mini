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

#include "tool_common.hpp"

#include <getopt.h>

#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace {

constexpr const char* kTool = "jackmini_lsp";

struct Options {
    std::string server;
    bool aliases = false;
    bool connections = false;
    bool port_latency = false;
    bool total_latency = false;
    bool properties = false;
    bool type = false;
    bool uuid = false;
    bool port_uuid = false;
};

void show_usage(FILE* out)
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
std::string uuid_text(jackmini::Uuid uuid)
{
    return std::to_string(uuid.value());
}

// The name with its client part replaced by the UUID of that client (-u), when the client is known.
std::string with_owner_uuid(const std::string& name, const std::map<std::string, jackmini::Uuid>& owners)
{
    const auto colon = name.find(':');
    if (colon == std::string::npos) {
        return name;
    }
    const auto owner = owners.find(name.substr(0, colon));
    if (owner == owners.end()) {
        return name;
    }
    return uuid_text(owner->second) + name.substr(colon);
}

// Every filter string must occur in the port name (a substring, as jack_lsp matched them).
bool matches_all(const std::string& name, const std::vector<char*>& filters)
{
    for (const char* filter : filters) {
        if (name.find(filter) == std::string::npos) {
            return false;
        }
    }
    return true;
}

void print_properties(jackmini::PortFlags flags)
{
    std::fputs("\tproperties: ", stdout);
    if (jackmini::has_flag(flags, jackmini::PortFlags::Input)) {
        std::fputs("input,", stdout);
    }
    if (jackmini::has_flag(flags, jackmini::PortFlags::Output)) {
        std::fputs("output,", stdout);
    }
    if (jackmini::has_flag(flags, jackmini::PortFlags::CanMonitor)) {
        std::fputs("can-monitor,", stdout);
    }
    if (jackmini::has_flag(flags, jackmini::PortFlags::Physical)) {
        std::fputs("physical,", stdout);
    }
    if (jackmini::has_flag(flags, jackmini::PortFlags::Terminal)) {
        std::fputs("terminal,", stdout);
    }
    std::putc('\n', stdout);
}

} // namespace

int main(int argc, char** argv)
{
    Options options;
    static const char kShortOptions[] = "s:AclLpthvtuU";
    static const struct option kLongOptions[] = {
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
    };

    int c;
    while ((c = getopt_long(argc, argv, kShortOptions, kLongOptions, nullptr)) != -1) {
        switch (c) {
        case 's':
            options.server = optarg;
            break;
        case 'A':
            options.aliases = true;
            break;
        case 'c':
            options.connections = true;
            break;
        case 'l':
            options.port_latency = true;
            break;
        case 'L':
            options.total_latency = true;
            break;
        case 'p':
            options.properties = true;
            break;
        case 't':
            options.type = true;
            break;
        case 'u':
            options.uuid = true;
            break;
        case 'U':
            options.port_uuid = true;
            break;
        case 'h':
            show_usage(stderr);
            return 1;
        case 'v':
            std::printf("%s " JACK_VERSION "\n", kTool);
            return 0;
        default:
            show_usage(stderr);
            return 1;
        }
    }

    std::vector<char*> filters(argv + optind, argv + argc);

    auto opened = jackmini::tools::open_client("jackmini_lsp", options.server);
    if (!opened) {
        if (opened.error() == jackmini::Error::NoServer) {
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
        owners.emplace(port.owner, port.owner_uuid);
    }

    for (const jackmini::PortInfo& port : ports) {
        if (!matches_all(port.name, filters)) {
            continue;
        }

        std::printf("%s\n", options.uuid ? with_owner_uuid(port.name, owners).c_str() : port.name.c_str());

        if (options.port_uuid) {
            std::printf("\tuuid: %s\n", uuid_text(port.port_uuid).c_str());
        }

        if (options.aliases) {
            for (const std::string& alias : port.aliases) {
                std::printf("   %s\n", alias.c_str());
            }
        }

        if (options.connections) {
            for (const std::string& connected : port.connections) {
                std::printf("   %s\n", (options.uuid ? with_owner_uuid(connected, owners) : connected).c_str());
            }
        }

        if (options.port_latency) {
            std::printf("\tport playback latency = [ %u %u ] frames\n",
                        port.playback_latency.min, port.playback_latency.max);
            std::printf("\tport capture latency = [ %u %u ] frames\n",
                        port.capture_latency.min, port.capture_latency.max);
        }

        if (options.total_latency) {
            std::printf("\ttotal latency = %u frames\n", port.total_latency);
        }

        if (options.properties) {
            print_properties(port.flags);
        }

        if (options.type) {
            std::printf("\t%s\n", port.type.c_str());
        }
    }

    return 0;
}
