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

// Deactivation keeps the connections (protocol.md section 6): an inactive client's input is silent, its output is not
// mixed into any input, and the connection is still there when it is activated again.  The graph is the real server
// code.  The graph's Connect checks for feedback loops through the running server, which this test does not start, so
// the test makes its (loop-free) connection with the same steps Connect takes after that check.  The client API test,
// with a running server, is in the end-to-end checks.

#include "JackGraphManager.h"
#include "JackClientControl.h"
#include "JackSynchro.h"
#include "JackConnectionManager.h"

#include <cstdio>

using namespace jackmini;

namespace
{

constexpr int kSourceRef = 2;
constexpr int kSinkRef = 3;
constexpr jack_nframes_t kFrames = 64;

int gFailures = 0;

// The same object as a JackGraphManager, with the connection steps of JackGraphManager::Connect for a loop-free
// connection.  It adds no members, so the layout is the same.
struct GraphTestAccess : public JackGraphManager {
    void ConnectDirect(jack_port_id_t src, jack_port_id_t dst)
    {
        JackConnectionManager* manager = WriteNextStateStart();
        manager->Connect(src, dst);
        manager->Connect(dst, src);
        manager->IncDirectConnection(src, dst);
        WriteNextStateStop();
    }
    // Readers see the changes once the graph has switched to the next state, as in the cycle.
    void Switch()
    {
        RunNextGraph();
    }
};

void Check(const char* what, bool ok)
{
    std::printf("%-56s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) {
        gFailures++;
    }
}

bool AllEqual(const float* buffer, float value)
{
    for (jack_nframes_t i = 0; i < kFrames; i++) {
        if (buffer[i] != value) {
            return false;
        }
    }
    return true;
}

} // namespace

int main()
{
    SetClockSource(JACK_TIMER_SYSTEM_CLOCK);

    JackGraphManager* graph = JackGraphManager::Allocate(16);
    graph->InitRefNum(kSourceRef);
    graph->InitRefNum(kSinkRef);
    graph->Activate(kSourceRef);
    graph->Activate(kSinkRef);
    static_cast<GraphTestAccess*>(graph)->Switch();

    const jack_port_id_t out = graph->AllocatePort(kSourceRef, "src:out", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, kFrames);
    const jack_port_id_t in = graph->AllocatePort(kSinkRef, "sink:in", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, kFrames);
    Check("ports allocated", out != NO_PORT && in != NO_PORT);
    static_cast<GraphTestAccess*>(graph)->ConnectDirect(out, in);
    static_cast<GraphTestAccess*>(graph)->Switch();
    Check("connected", graph->IsConnected(out, in) == 1);

    float* source = static_cast<float*>(graph->GetBuffer(out, kFrames));
    for (jack_nframes_t i = 0; i < kFrames; i++) {
        source[i] = 0.5f;
    }
    Check("active: the input receives the output", AllEqual(static_cast<float*>(graph->GetBuffer(in, kFrames)), 0.5f));

    graph->Deactivate(kSourceRef);
    static_cast<GraphTestAccess*>(graph)->Switch();
    Check("deactivated: the connection is kept", graph->IsConnected(out, in) == 1);
    Check("deactivated: the input is silent", AllEqual(static_cast<float*>(graph->GetBuffer(in, kFrames)), 0.0f));

    graph->Activate(kSourceRef);
    static_cast<GraphTestAccess*>(graph)->Switch();
    Check("reactivated: the connection is still there", graph->IsConnected(out, in) == 1);
    Check("reactivated: the input receives the output again", AllEqual(static_cast<float*>(graph->GetBuffer(in, kFrames)), 0.5f));

    std::printf("%s\n", gFailures == 0 ? "deactivate graph checks passed" : "deactivate graph checks FAILED");
    return gFailures == 0 ? 0 : 1;
}
