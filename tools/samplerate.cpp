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

// jackmini_samplerate: prints the sample rate of a running server and exits.  It does what jack_samplerate did: the
// rate on one line.  It never starts a server.

#include "tool_common.hpp"

#include <cstdint>
#include <getopt.h>

#include <cstdio>
#include <string>

namespace {

constexpr const char* kTool = "jackmini_samplerate";

void show_usage(FILE* out)
{
    std::fprintf(out, "\nusage: %s [options]\n", kTool);
    std::fprintf(out, "Prints the sample rate of the JACK server, and exits.\n\n");
    std::fprintf(out, "        -s, --server <name>   Connect to the jack server named <name>\n");
    std::fprintf(out, "        -v, --version         Output version information and exit\n");
    std::fprintf(out, "        -h, --help            Display this help message\n\n");
}

} // namespace

int main(std::int32_t argc, char** argv)
{
    std::string server;
    static const char kShortOptions[] = "s:hv";
    static const struct option kLongOptions[] = {
        { "server", required_argument, nullptr, 's' },
        { "help", no_argument, nullptr, 'h' },
        { "version", no_argument, nullptr, 'v' },
        { nullptr, 0, nullptr, 0 },
    };

    std::int32_t c;
    while ((c = getopt_long(argc, argv, kShortOptions, kLongOptions, nullptr)) != -1) {
        switch (c) {
        case 's':
            server = optarg;
            break;
        case 'v':
            std::printf("%s " JACK_VERSION "\n", kTool);
            return 0;
        case 'h':
            show_usage(stderr);
            return 1;
        default:
            show_usage(stderr);
            return 1;
        }
    }
    if (optind < argc) {
        show_usage(stderr);
        return 1;
    }

    auto opened = jackmini::tools::open_client(kTool, server);
    if (!opened) {
        return jackmini::tools::fail(kTool, opened.error());
    }
    std::printf("%u\n", static_cast<std::uint32_t>(opened->sample_rate()));
    return 0;
}
