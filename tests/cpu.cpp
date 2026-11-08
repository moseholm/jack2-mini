/*
    Copyright (C) 2005 Samuel TRACOL
    Copyright (C) 2008 Grame

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

*/

/** @file jack_cpu.cpp
 *
 * @brief This client test the capacity for jackd to kick out a to heavy cpu client.
 *
 * The times given on the command line are converted to cycles of the server, so the run is the same with the server in
 * freewheel mode (jackd -F) as in real time.
 */

#include "jackmini/jackmini.hpp"
#include "jackmini_wait.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <getopt.h>

using namespace jackmini;

namespace
{

Port input_port;
Port output_port;
Client* client = nullptr;
int percent_cpu = 0;
int time_to_run = 0;
int time_before_run = 0;

// The load is applied while set; the main thread sets it once the delay has passed.
std::atomic<bool> load_on{ false };
std::atomic<unsigned long> cycles{ 0 };

void usage()
{
    std::fprintf(stderr, "\n"
                         "usage: jack_cpu \n"
                         "              [ --name OR -n client_name ]\n"
                         "              [ --time OR -t time_to_run (in seconds) ]\n"
                         "              [ --delay OR -d delay_before_cpuload__is_applied (in seconds) ]\n"
                         "                --cpu OR -c percent_cpu_load (1-99)\n");
}

/**
 * The process callback runs once for each audio cycle.  The load spins for a share of the buffer period, the share
 * being percent_cpu of the buffer.
 */
bool process(FrameCount nframes)
{
    const auto start = std::chrono::steady_clock::now();
    auto out = client->audio_buffer(output_port);
    std::fill(out.begin(), out.end(), 0.0f);

    cycles++;
    if (load_on.load(std::memory_order_relaxed)) {
        const auto sample_rate = client->sample_rate();
        const auto idle = std::chrono::nanoseconds(static_cast<long long>(nframes) * percent_cpu * 1000000000LL /
                                                   (100LL * sample_rate));
        while (std::chrono::steady_clock::now() - start < idle) {
        }
    }
    return true;
}

} // namespace

int main(int argc, char* argv[])
{
    bool got_time = false;
    int opt;
    const char* options = "t:t:d:c:";
    const char* client_name = "jack-cpu";
    struct option long_options[] = {
        { "time", 1, nullptr, 't' },
        { "name", 1, nullptr, 'n' },
        { "delay", 1, nullptr, 'd' },
        { "cpu", 1, nullptr, 'c' },
        { nullptr, 0, nullptr, 0 },
    };

    while ((opt = getopt_long(argc, argv, options, long_options, nullptr)) != -1) {
        switch (opt) {
            case 'n':
                client_name = optarg;
                break;
            case 't':
                time_to_run = std::atoi(optarg);
                break;
            case 'd':
                time_before_run = std::atoi(optarg);
                break;
            case 'c':
                percent_cpu = std::atoi(optarg);
                got_time = true;
                break;
            default:
                std::fprintf(stderr, "unknown option %c\n", opt);
                usage();
        }
    }

    if (!got_time) {
        std::fprintf(stderr, "CPU load not specified ! See usage as following :\n");
        usage();
        return -1;
    }

    if (time_to_run != 0) {
        std::printf("Running jack-cpu for %d seconds...\n", time_to_run);
    }

    OpenOptions open_options;
    open_options.start_server = false;
    auto opened = Client::open(client_name, open_options);
    if (!opened) {
        std::fprintf(stderr, "jack_client_open() failed : is jack server running ?\n");
        return 1;
    }
    Client jack = std::move(*opened);
    client = &jack;

    const FrameCount buffer_size = jack.buffer_size();
    const FrameCount sample_rate = jack.sample_rate();
    std::printf("engine buffer size = %u \n", buffer_size);
    std::printf("engine sample rate: %u Hz\n", sample_rate);
    std::printf("CPU load applies as %u sample delay.\n", static_cast<FrameCount>(buffer_size * percent_cpu / 100));

    if (!jack.on_process(process) || !jack.on_shutdown([] {
            std::fprintf(stderr, "JACK shut down, exiting ...\n");
            std::exit(1);
        }).has_value()) {
        std::fprintf(stderr, "cannot set the callbacks\n");
        return 1;
    }

    auto in = jack.register_port("input", PortType::Audio, PortFlags::Input);
    auto out = jack.register_port("output", PortType::Audio, PortFlags::Output);
    std::printf("registering ports...\n");
    if (!in || !out) {
        std::fprintf(stderr, "no more JACK ports available\n");
        return 1;
    }
    input_port = *in;
    output_port = *out;

    // Tell the JACK server that we are ready to roll.  The process callback starts running now.
    std::printf("Activating as jackd client...\n");
    if (!jack.activate().has_value()) {
        std::fprintf(stderr, "cannot activate client\n");
        return 1;
    }

    // Connect the ports.  Connections are made after activation, because the server connects only the ports of active
    // clients.  Capture ports are outputs of the hardware, playback ports its inputs.
    const auto capture = jack.port_names(PortType::Audio, PortFlags::Physical | PortFlags::Output);
    if (capture.empty()) {
        std::fprintf(stderr, "no physical capture ports\n");
        return 1;
    }
    auto source = jack.find_port(capture[0]);
    if (!source || !jack.connect(*source, input_port).has_value()) {
        std::fprintf(stderr, "cannot connect input ports\n");
    }

    const auto playback = jack.port_names(PortType::Audio, PortFlags::Physical | PortFlags::Input);
    if (playback.empty()) {
        std::fprintf(stderr, "no physical playback ports\n");
        return 1;
    }
    auto sink = jack.find_port(playback[0]);
    if (!sink || !jack.connect(output_port, *sink).has_value()) {
        std::fprintf(stderr, "cannot connect output ports\n");
    }

    const unsigned long cycles_per_second = std::max<unsigned long>(1, sample_rate / buffer_size);
    const unsigned long before_run = static_cast<unsigned long>(time_before_run) * cycles_per_second;

    if (before_run > 0) {
        WaitUntil([&] { return cycles.load() >= before_run; });
    }
    load_on = true;
    std::printf("Activating cpu load...\n");

    if (time_to_run != 0) {
        const unsigned long exit_at = before_run + static_cast<unsigned long>(time_to_run) * cycles_per_second;
        WaitUntil([&] { return cycles.load() >= exit_at; });
        jack.close();
        std::printf("Exiting after a %d seconds run.\n", time_to_run);
        return 0;
    }

    // Without a time the load runs until the program is stopped.
    std::atomic<bool> forever{ false };
    forever.wait(false);
    return 0;
}
