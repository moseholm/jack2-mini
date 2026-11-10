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

// Deactivation keeps the connections (protocol.md section 6): an inactive client's input is silent, its output is not
// mixed into any input, and the connection is still there when it is activated again.  The graph is the real server
// code.  The graph's Connect checks for feedback loops through the running server, which this test does not start, so
// the test makes its (loop-free) connection with the same steps Connect takes after that check.  The client API test,
// with a running server, is in the end-to-end checks.

#include "JackClientControl.h"
#include "JackConnectionManager.h"
#include "JackGraphManager.h"
#include "JackSynchro.h"

#include <cstdint>
#include <cstdio>

using namespace jackmini;

namespace {

constexpr std::int32_t kSourceRef = 2;
constexpr std::int32_t kSinkRef = 3;
constexpr NframesC kFrames = 64;

std::int32_t gFailures = 0;

// The same object as a JackGraphManager, with the connection steps of JackGraphManager::Connect for a loop-free
// connection.  It adds no members, so the layout is the same.
struct GraphTestAccess : public GraphManager {
    void connectDirect(PortIdC src, PortIdC dst)
    {
        ConnectionManager* manager = writeNextStateStart();
        manager->connect(src, dst);
        manager->connect(dst, src);
        manager->incDirectConnection(src, dst);
        writeNextStateStop();
    }
    // Readers see the changes once the graph has switched to the next state, as in the cycle.
    void switchGraph()
    {
        runNextGraph();
    }
};

void check(const char* what, bool ok)
{
    std::printf("%-56s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) {
        gFailures++;
    }
}

bool allEqual(const float* buffer, float value)
{
    for (NframesC i = 0; i < kFrames; i++) {
        if (buffer[i] != value) {
            return false;
        }
    }
    return true;
}

} // namespace

int main()
{
    setClockSource(kTimerSystemClock);

    GraphManager* graph = GraphManager::allocate(16);
    graph->initRefNum(kSourceRef);
    graph->initRefNum(kSinkRef);
    graph->activate(kSourceRef);
    graph->activate(kSinkRef);
    static_cast<GraphTestAccess*>(graph)->switchGraph();

    const PortIdC out = graph->allocatePort(kSourceRef, "src:out", kDefaultAudioType, kPortIsOutput, kFrames);
    const PortIdC in = graph->allocatePort(kSinkRef, "sink:in", kDefaultAudioType, kPortIsInput, kFrames);
    check("ports allocated", out != kNoPort && in != kNoPort);
    static_cast<GraphTestAccess*>(graph)->connectDirect(out, in);
    static_cast<GraphTestAccess*>(graph)->switchGraph();
    check("connected", graph->isConnected(out, in));

    float* source = static_cast<float*>(graph->getBuffer(out, kFrames));
    for (NframesC i = 0; i < kFrames; i++) {
        source[i] = 0.5f;
    }
    check("active: the input receives the output", allEqual(static_cast<float*>(graph->getBuffer(in, kFrames)), 0.5f));

    graph->deactivate(kSourceRef);
    static_cast<GraphTestAccess*>(graph)->switchGraph();
    check("deactivated: the connection is kept", graph->isConnected(out, in));
    check("deactivated: the input is silent", allEqual(static_cast<float*>(graph->getBuffer(in, kFrames)), 0.0f));

    graph->activate(kSourceRef);
    static_cast<GraphTestAccess*>(graph)->switchGraph();
    check("reactivated: the connection is still there", graph->isConnected(out, in));
    check("reactivated: the input receives the output again", allEqual(static_cast<float*>(graph->getBuffer(in, kFrames)), 0.5f));

    std::printf("%s\n", gFailures == 0 ? "deactivate graph checks passed" : "deactivate graph checks FAILED");
    return gFailures == 0 ? 0 : 1;
}
