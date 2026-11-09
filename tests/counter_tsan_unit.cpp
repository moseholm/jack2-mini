/*
Copyright (C) 2026 JACK contributors

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

// The activation counters (protocol.md section 4.5) under concurrency: the realtime thread runs the cycle (switch the
// graph, reset the counters, signal the connected clients) while another thread changes connections, which copies the
// configuration to the next state.  Run under TSAN, the test fails on any race between the copy and the live counters.

#include "JackGraphManager.h"
#include "JackClientControl.h"
#include "JackSynchro.h"

#include <array>
#include <atomic>
#include <cstdio>
#include <thread>

using namespace jackmini;

namespace
{

constexpr int kInputRef = 2;
constexpr int kOutputRef = 3;
constexpr int kRounds = 20000;

} // namespace

int main()
{
    // The cycle reads the time, through a clock the server selects at startup.
    set_clock_source(JACK_TIMER_SYSTEM_CLOCK);

    JackGraphManager* graph = JackGraphManager::Allocate(16);
    JackSynchro* table = new JackSynchro[CLIENT_NUM];

    std::array<char, 64> name{};
    for (int ref : { kInputRef, kOutputRef }) {
        std::snprintf(name.data(), name.size(), "counter_test_%d", ref);
        if (!table[ref].Allocate(name.data(), "counter_test", 0)) {
            std::printf("cannot allocate the synchro of ref %d\n", ref);
            return 1;
        }
        graph->InitRefNum(ref);
    }

    // The output client feeds the input client: the input waits for it.
    graph->DirectConnect(kOutputRef, kInputRef);
    graph->RunNextGraph();

    JackClientControl output(name.data(), 0, kOutputRef, 0);
    std::atomic<int> failures{ 0 };

    // The realtime thread: one cycle per round.
    std::thread rt([&] {
        for (int i = 0; i < kRounds; i++) {
            graph->RunNextGraph();
            if (!graph->ResumeRefNum(&output, table)) {
                failures++;
            }
        }
    });

    // The control thread: connections change while the cycles run.  Each change copies the configuration.
    std::thread control([&] {
        for (int i = 0; i < kRounds; i++) {
            graph->DirectDisconnect(kOutputRef, kInputRef);
            graph->DirectConnect(kOutputRef, kInputRef);
        }
    });

    rt.join();
    control.join();

    std::printf("counter test: %d failed cycles\n", failures.load());
    return failures.load() == 0 ? 0 : 1;
}
