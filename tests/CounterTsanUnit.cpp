// Copyright (C) 2026 JACK contributors
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

// The activation counters (protocol.md section 4.5) under concurrency: the realtime thread runs the cycle (switch the
// graph, reset the counters, signal the connected clients) while another thread changes connections, which copies the
// configuration to the next state.  Run under TSAN, the test fails on any race between the copy and the live counters.

#include "ClientControl.h"
#include "GraphManager.h"
#include "Synchro.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <thread>

using namespace jackmini;

namespace {

constexpr std::int32_t kInputRef = 2;
constexpr std::int32_t kOutputRef = 3;
constexpr std::int32_t kRounds = 20000;

} // namespace

int main()
{
    // The cycle reads the time, through a clock the server selects at startup.
    setClockSource(kTimerSystemClock);

    GraphManager* graph = GraphManager::allocate(16);
    Synchro* table = new Synchro[kClientNum];

    std::array<char, 64> name { };
    for (std::int32_t ref : { kInputRef, kOutputRef }) {
        std::snprintf(name.data(), name.size(), "counter_test_%d", ref);
        if (!table[ref].allocate(name.data(), "counter_test", 0)) {
            std::printf("cannot allocate the synchro of ref %d\n", ref);
            return 1;
        }
        graph->initRefNum(ref);
    }

    // The output client feeds the input client: the input waits for it.
    graph->directConnect(kOutputRef, kInputRef);
    graph->runNextGraph();

    ClientControl output(name.data(), 0, kOutputRef, 0);
    std::atomic<std::int32_t> failures { 0 };

    // The realtime thread: one cycle per round.
    std::thread rt([&] {
        for (std::int32_t i = 0; i < kRounds; i++) {
            graph->runNextGraph();
            if (!graph->resumeRefNum(&output, table)) {
                failures++;
            }
        }
    });

    // The control thread: connections change while the cycles run.  Each change copies the configuration.
    std::thread control([&] {
        for (std::int32_t i = 0; i < kRounds; i++) {
            graph->directDisconnect(kOutputRef, kInputRef);
            graph->directConnect(kOutputRef, kInputRef);
        }
    });

    rt.join();
    control.join();

    std::printf("counter test: %d failed cycles\n", failures.load());
    const std::int32_t result = failures.load() == 0 ? 0 : 1;
    for (std::int32_t ref : { kInputRef, kOutputRef }) {
        table[ref].destroy();
    }
    delete[] table;
    GraphManager::destroy(graph);
    return result;
}
