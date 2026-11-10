// Copyright (C) 2005 Samuel TRACOL
// Copyright (C) 2008 Grame
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

// @file jack_cpu.cpp
//
// @brief This client test the capacity for jackd to kick out a to heavy cpu client.
//
// The times given on the command line are converted to cycles of the server, so the run is the same with the server in
// freewheel mode (jackd -F) as in real time.

#include "JackminiWait.hpp"
#include "jackmini/Jackmini.hpp"

#include <getopt.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

using namespace jackmini;

namespace {

Port gInputPort;
Port gOutputPort;
Client* gClient = nullptr;
std::int32_t gPercentCpu = 0;
std::int32_t gTimeToRun = 0;
std::int32_t gTimeBeforeRun = 0;

// The load is applied while set; the main thread sets it once the delay has passed.
std::atomic<bool> gLoadOn { false };
std::atomic<std::uint64_t> gCycles { 0 };

void usage()
{
    std::fprintf(stderr, "\n"
                         "usage: jack_cpu \n"
                         "              [ --name OR -n client_name ]\n"
                         "              [ --time OR -t time_to_run (in seconds) ]\n"
                         "              [ --delay OR -d delay_before_cpuload__is_applied (in seconds) ]\n"
                         "                --cpu OR -c percent_cpu_load (1-99)\n");
}

// The process callback runs once for each audio cycle.  The load spins for a share of the buffer period, the share
// being percent_cpu of the buffer.
bool process(const Cycle& cycle)
{
    const FrameCount nframes = cycle.m_frames;
    const auto start = std::chrono::steady_clock::now();
    auto out = gClient->audioBuffer(gOutputPort);
    std::fill(out.begin(), out.end(), 0.0f);

    gCycles++;
    if (gLoadOn.load(std::memory_order_relaxed)) {
        const auto sampleRate = gClient->sampleRate();
        const auto idle = std::chrono::nanoseconds(static_cast<std::int64_t>(nframes) * gPercentCpu * 1000000000LL / (100LL * sampleRate));
        while (std::chrono::steady_clock::now() - start < idle) {
        }
    }
    return true;
}

} // namespace

int main(std::int32_t argc, char* argv[])
{
    bool gotTime = false;
    std::int32_t opt;
    const char* options = "t:t:d:c:";
    const char* clientName = "jack-cpu";
    std::array<struct option, 5> longOptions = { {
        { "time", 1, nullptr, 't' },
        { "name", 1, nullptr, 'n' },
        { "delay", 1, nullptr, 'd' },
        { "cpu", 1, nullptr, 'c' },
        { nullptr, 0, nullptr, 0 },
    } };

    while ((opt = getopt_long(argc, argv, options, longOptions.data(), nullptr)) != -1) {
        switch (opt) {
        case 'n':
            clientName = optarg;
            break;
        case 't':
            gTimeToRun = std::atoi(optarg);
            break;
        case 'd':
            gTimeBeforeRun = std::atoi(optarg);
            break;
        case 'c':
            gPercentCpu = std::atoi(optarg);
            gotTime = true;
            break;
        default:
            std::fprintf(stderr, "unknown option %c\n", opt);
            usage();
        }
    }

    if (!gotTime) {
        std::fprintf(stderr, "CPU load not specified ! See usage as following :\n");
        usage();
        return -1;
    }

    if (gTimeToRun != 0) {
        std::printf("Running jack-cpu for %d seconds...\n", gTimeToRun);
    }

    OpenOptions openOptions;
    openOptions.m_startServer = false;
    auto opened = Client::open(clientName, openOptions);
    if (!opened) {
        std::fprintf(stderr, "jack_client_open() failed : is jack server running ?\n");
        return 1;
    }
    Client jack = std::move(*opened);
    gClient = &jack;

    const FrameCount bufferSize = jack.bufferSize();
    const FrameCount sampleRate = jack.sampleRate();
    std::printf("engine buffer size = %u \n", bufferSize);
    std::printf("engine sample rate: %u Hz\n", sampleRate);
    std::printf("CPU load applies as %u sample delay.\n", static_cast<FrameCount>(bufferSize * gPercentCpu / 100));

    if (!jack.onProcess(process) || !jack.onShutdown([] {
                                             std::fprintf(stderr, "JACK shut down, exiting ...\n");
                                             std::exit(1);
                                         })
            .has_value()) {
        std::fprintf(stderr, "cannot set the callbacks\n");
        return 1;
    }

    auto in = jack.registerPort("input", PortType::kAudio, PortFlags::kInput);
    auto out = jack.registerPort("output", PortType::kAudio, PortFlags::kOutput);
    std::printf("registering ports...\n");
    if (!in || !out) {
        std::fprintf(stderr, "no more JACK ports available\n");
        return 1;
    }
    gInputPort = *in;
    gOutputPort = *out;

    // Tell the JACK server that we are ready to roll.  The process callback starts running now.
    std::printf("Activating as jackd client...\n");
    if (!jack.activate().has_value()) {
        std::fprintf(stderr, "cannot activate client\n");
        return 1;
    }

    // Connect the ports.  Connections are made after activation, because the server connects only the ports of active
    // clients.  Capture ports are outputs of the hardware, playback ports its inputs.
    const auto capture = jack.portNames(PortType::kAudio, PortFlags::kPhysical | PortFlags::kOutput);
    if (capture.empty()) {
        std::fprintf(stderr, "no physical capture ports\n");
        return 1;
    }
    auto source = jack.findPort(capture[0]);
    if (!source || !jack.connect(*source, gInputPort).has_value()) {
        std::fprintf(stderr, "cannot connect input ports\n");
    }

    const auto playback = jack.portNames(PortType::kAudio, PortFlags::kPhysical | PortFlags::kInput);
    if (playback.empty()) {
        std::fprintf(stderr, "no physical playback ports\n");
        return 1;
    }
    auto sink = jack.findPort(playback[0]);
    if (!sink || !jack.connect(gOutputPort, *sink).has_value()) {
        std::fprintf(stderr, "cannot connect output ports\n");
    }

    const std::uint64_t cyclesPerSecond = std::max<std::uint64_t>(1, sampleRate / bufferSize);
    const std::uint64_t beforeRun = static_cast<std::uint64_t>(gTimeBeforeRun) * cyclesPerSecond;

    if (beforeRun > 0) {
        waitUntil([&] { return gCycles.load() >= beforeRun; });
    }
    gLoadOn = true;
    std::printf("Activating cpu load...\n");

    if (gTimeToRun != 0) {
        const std::uint64_t exitAt = beforeRun + static_cast<std::uint64_t>(gTimeToRun) * cyclesPerSecond;
        waitUntil([&] { return gCycles.load() >= exitAt; });
        jack.close();
        std::printf("Exiting after a %d seconds run.\n", gTimeToRun);
        return 0;
    }

    // Without a time the load runs until the program is stopped.
    std::atomic<bool> forever { false };
    forever.wait(false);
    return 0;
}
