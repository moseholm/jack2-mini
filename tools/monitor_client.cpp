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

// jackmini_monitor_client: copies the audio inputs of a client to monitor outputs of its own.  Input i of the client is
// copied to the output monitor_<i>, every cycle, in the process callback.  The outputs are connected where the user wants
// them (jackmini_connect).  It runs until it is stopped with SIGINT or SIGTERM.
//
// This is the function listed in the plan.  The jack_monitor_client of the old tools asked the server to monitor the
// named port for 30 seconds; the server has no audio path for that request, so this tool does the copy itself instead.

#include "tool_common.hpp"

#include <cstdint>
#include <getopt.h>

#include <atomic>
#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr const char* kTool = "jackmini_monitor_client";

std::atomic<bool> gStop { false };

void on_signal(std::int32_t)
{
    gStop.store(true);
}

void show_usage(FILE* out)
{
    std::fprintf(out, "\nUsage: %s [options] <client>\n", kTool);
    std::fprintf(out, "Copies the audio inputs of <client> to the outputs monitor_<n> of this client.\n\n");
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
            show_usage(stdout);
            return 0;
        default:
            show_usage(stderr);
            return 1;
        }
    }
    if (argc - optind != 1) {
        show_usage(stderr);
        return 1;
    }
    const std::string target = argv[optind];

    // The input ports of the target: their handles are looked up through this client, which is how a port is reached.
    auto opened = jackmini::tools::open_client(kTool, server);
    if (!opened) {
        return jackmini::tools::fail(kTool, opened.error());
    }
    jackmini::Client& client = *opened;

    std::vector<jackmini::Port> inputs;
    for (const jackmini::PortInfo& port : client.ports()) {
        if (port.owner != target || port.type != "32 bit float mono audio" || !jackmini::has_flag(port.flags, jackmini::PortFlags::Input)) {
            continue;
        }
        if (auto found = client.find_port(port.name); found) {
            inputs.push_back(*found);
        }
    }
    if (inputs.empty()) {
        std::fprintf(stderr, "%s: client '%s' has no audio input ports\n", kTool, target.c_str());
        return 1;
    }

    // The outputs are registered before the client is activated; the process callback only copies.
    std::vector<jackmini::Port> outputs;
    for (std::size_t i = 0; i < inputs.size(); i++) {
        auto port = client.register_port("monitor_" + std::to_string(i + 1), jackmini::PortType::Audio, jackmini::PortFlags::Output);
        if (!port) {
            return jackmini::tools::fail(kTool, port.error());
        }
        outputs.push_back(*port);
    }

    if (auto set = client.on_process([&client, &inputs, &outputs](const jackmini::Cycle&) {
            for (std::size_t i = 0; i < inputs.size(); i++) {
                const std::span<float> in = client.audio_buffer(inputs[i]);
                const std::span<float> out = client.audio_buffer(outputs[i]);
                std::copy_n(in.begin(), std::min(in.size(), out.size()), out.begin());
            }
            return true;
        }); !set) {
        return jackmini::tools::fail(kTool, set.error());
    }

    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);
    if (auto activated = client.activate(); !activated) {
        return jackmini::tools::fail(kTool, activated.error());
    }
    while (!gStop.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    client.deactivate();
    return 0;
}
