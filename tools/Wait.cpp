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

// jackmini_wait: checks whether a server is running, or waits until it starts or quits.  It does what jack_wait did,
// with the same messages.  Options: -w waits until the server runs ("server is available"), -q waits until it quits
// ("server is gone"), -c checks once ("running" or "not running"; the default), -t sets a timeout in seconds, -n names
// the client, -s chooses the server.  It never starts a server.  jack_wait's -n and -s are the same here.

#include "ToolCommon.hpp"

#include <getopt.h>
#include <array>
#include <cstdint>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

namespace {

constexpr const char* kTool = "jackmini_wait";

void showUsage(FILE* out)
{
    std::fprintf(out, "\nUsage: %s [options]\n", kTool);
    std::fprintf(out, "Check for jack existence, or wait, until it either quits, or gets started\n");
    std::fprintf(out, "options:\n");
    std::fprintf(out, "        -s, --server <name>   Connect to the jack server named <name>\n");
    std::fprintf(out, "        -n, --name <name>     Set client name to <name>\n");
    std::fprintf(out, "        -w, --wait            Wait for server to become available\n");
    std::fprintf(out, "        -q, --quit            Wait until server is quit\n");
    std::fprintf(out, "        -c, --check           Check whether server is running (the default)\n");
    std::fprintf(out, "        -t, --timeout         Wait timeout in seconds\n");
    std::fprintf(out, "        -v, --version         Output version information and exit\n");
    std::fprintf(out, "        -h, --help            Display this help message\n");
}

} // namespace

int main(std::int32_t argc, char** argv)
{
    std::string server;
    std::string name = kTool;
    bool waitForStart = false;
    bool waitForQuit = false;
    std::int64_t timeout = 0;

    static constexpr std::string_view kShortOptions = "s:n:wqct:hv";
    static std::array<const struct option, 9> kLongOptions = { {
        { "server", required_argument, nullptr, 's' },
        { "name", required_argument, nullptr, 'n' },
        { "wait", no_argument, nullptr, 'w' },
        { "quit", no_argument, nullptr, 'q' },
        { "check", no_argument, nullptr, 'c' },
        { "timeout", required_argument, nullptr, 't' },
        { "help", no_argument, nullptr, 'h' },
        { "version", no_argument, nullptr, 'v' },
        { nullptr, 0, nullptr, 0 },
    } };

    std::int32_t c;
    while ((c = getopt_long(argc, argv, kShortOptions.data(), kLongOptions.data(), nullptr)) != -1) {
        switch (c) {
        case 's':
            server = optarg;
            break;
        case 'n':
            name = optarg;
            break;
        case 'w':
            waitForStart = true;
            break;
        case 'q':
            waitForQuit = true;
            break;
        case 'c':
            break;
        case 't':
            timeout = std::strtol(optarg, nullptr, 10);
            break;
        case 'v':
            std::printf("%s " JACK_VERSION "\n", kTool);
            return 0;
        case 'h':
            showUsage(stderr);
            return 1;
        default:
            showUsage(stderr);
            return 1;
        }
    }
    if (optind < argc) {
        showUsage(stderr);
        return 1;
    }

    const auto start = std::chrono::steady_clock::now();
    for (;;) {
        bool running = false;
        {
            // The probe client is closed at the end of this block, before the answer is given.
            auto opened = jackmini::tools::openClient(name, server);
            if (opened) {
                running = true;
            } else if (opened.error() != jackmini::Error::kNoServer) {
                return jackmini::tools::fail(kTool, opened.error());
            }
        }

        if (running) {
            if (waitForStart) {
                std::printf("server is available\n");
                return 0;
            }
            if (!waitForQuit) {
                std::printf("running\n");
                return 0;
            }
        } else {
            if (waitForQuit) {
                std::printf("server is gone\n");
                return 0;
            }
            if (!waitForStart) {
                std::printf("not running\n");
                return 0;
            }
        }

        if (timeout > 0 && std::chrono::steady_clock::now() - start > std::chrono::seconds(timeout)) {
            std::printf("timeout\n");
            return 1;
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}
