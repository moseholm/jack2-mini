// Copyright (C) 2026 JACK contributors
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation; either version 2.1 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.

// The client session (client/client_session.hpp) against a running server: open, name handling, and close. Not a
// ctest: it needs a server, for example `jackd -d dummy`.

#include "client_session.hpp"

#include "jackmini/client.hpp"

#include <cstdint>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

namespace {

std::int32_t gFailures = 0;

#define CHECK(cond)                                              \
    do {                                                         \
        if (!(cond)) {                                           \
            std::fprintf(stderr, "FAILED %s:%d: %s\n", __FILE__, \
                __LINE__, #cond);                                \
            ++gFailures;                                          \
        }                                                        \
    } while (0)

} // end of anonymous namespace

namespace {

// Waits until the condition holds, or for about ten seconds. The cycles are counted by the realtime thread, so the
// test polls the count; it does not depend on how long a cycle takes.
template <class F>
bool waitFor(F condition)
{
    for (std::int32_t i = 0; i < 10000; ++i) {
        if (condition()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return condition();
}

// Activation, the cycles, deactivation, reactivation, and a process callback that fails.
void checkActivation(const jackmini::OpenOptions& exact)
{
    using jackmini::client::ClientSession;

    auto client = ClientSession::open("session_rt", exact);
    CHECK(client.has_value());
    if (!client) {
        return;
    }

    std::atomic<std::int32_t> cycles { 0 };
    std::atomic<bool> badFrames { false };
    (*client)->setProcess([&](const jackmini::Cycle& cycle) {
        if (cycle.m_frames == 0) {
            badFrames = true;
        }
        cycles.fetch_add(1);
        return true;
    });

    CHECK((*client)->activate().has_value());
    CHECK((*client)->isActive());
    CHECK(waitFor([&] { return cycles.load() >= 20; }));

    CHECK((*client)->deactivate().has_value());
    CHECK(!(*client)->isActive());
    // The thread is joined by deactivate, so the count does not change after it returns.
    const std::int32_t afterDeactivate = cycles.load();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    CHECK(cycles.load() == afterDeactivate);

    CHECK((*client)->activate().has_value());
    CHECK(waitFor([&] { return cycles.load() >= afterDeactivate + 5; }));
    CHECK((*client)->deactivate().has_value());
    CHECK(!badFrames.load());

    // Closing an active client deactivates it first.
    CHECK((*client)->activate().has_value());
    CHECK(waitFor([&] { return cycles.load() > afterDeactivate + 5; }));
    CHECK((*client)->close().has_value());

    // A process callback that fails stops the client: the server takes it out of the graph.
    auto failing = ClientSession::open("session_fail", exact);
    CHECK(failing.has_value());
    if (failing) {
        std::atomic<std::int32_t> calls { 0 };
        (*failing)->setProcess([&](const jackmini::Cycle&) {
            return calls.fetch_add(1) < 3;
        });
        CHECK((*failing)->activate().has_value());
        CHECK(waitFor([&] { return !(*failing)->isActive(); }));
        CHECK((*failing)->deactivate().has_value());
        CHECK(calls.load() == 4);
        CHECK((*failing)->close().has_value());
    }
}

// Ports and connections between two sessions, and the reasons they are refused.
void checkPorts(const jackmini::OpenOptions& exact)
{
    using jackmini::client::ClientSession;
    constexpr const char* kAudio = "32 bit float mono audio";
    constexpr std::uint64_t kOutput = 0x2; // JackPortIsOutput
    constexpr std::uint64_t kInput = 0x1; // JackPortIsInput
    constexpr PortIdC kNone = 0;

    auto source = ClientSession::open("ports_src", exact);
    auto target = ClientSession::open("ports_dst", exact);
    CHECK(source.has_value() && target.has_value());
    if (!source || !target) {
        return;
    }
    ClientSession& src = **source;
    ClientSession& dst = **target;

    auto out = src.registerPort("out", kAudio, kOutput, 0);
    CHECK(out.has_value());
    auto in = dst.registerPort("in", kAudio, kInput, 0);
    CHECK(in.has_value());
    if (!out || !in) {
        return;
    }
    CHECK(*out != kNone && *in != kNone);
    CHECK(src.portName(*out) == "ports_src:out");
    CHECK(std::string(src.portType(*out)) == kAudio);
    CHECK(src.portFlags(*out) == kOutput);
    CHECK(src.findPort("ports_src:out") == std::optional<PortIdC>(*out));
    CHECK(src.owns(*out));
    CHECK(!dst.owns(*out)); // another client's port is not ours

    const auto outputs = src.portNames(kAudio, kOutput);
    CHECK(std::find(outputs.begin(), outputs.end(), "ports_src:out") != outputs.end());

    // A name with no client part and an empty name are refused; a port of another client cannot be unregistered.
    CHECK(!src.registerPort("", kAudio, kOutput, 0).has_value());
    CHECK(!dst.unregisterPort(*out).has_value());
    CHECK(!src.registerPort("out", kAudio, kOutput, 0).has_value()); // the name is taken

    // The server connects only the ports of active clients. Without a process callback no thread runs.
    CHECK(src.activate().has_value() && dst.activate().has_value());
    CHECK(src.connectPorts("ports_src:out", "ports_dst:in").has_value());
    CHECK(src.isConnected(*out, *in));
    const auto links = src.connections(*out);
    CHECK(std::find(links.begin(), links.end(), "ports_dst:in") != links.end());
    CHECK(src.disconnectPorts("ports_src:out", "ports_dst:in").has_value());
    CHECK(!src.isConnected(*out, *in));

    CHECK(src.unregisterPort(*out).has_value());
    CHECK(!src.findPort("ports_src:out").has_value());
    CHECK(dst.unregisterPort(*in).has_value());
    CHECK(src.deactivate().has_value() && dst.deactivate().has_value());

    CHECK((*target)->close().has_value());
    CHECK((*source)->close().has_value());
}

// The buffers: a value written by one client's process callback reaches the input buffer of a connected client in the
// same graph, through the realtime threads.
void checkBuffers(const jackmini::OpenOptions& exact)
{
    using jackmini::client::ClientSession;
    constexpr const char* kAudio = "32 bit float mono audio";

    auto writer = ClientSession::open("buf_out", exact);
    auto reader = ClientSession::open("buf_in", exact);
    CHECK(writer.has_value() && reader.has_value());
    if (!writer || !reader) {
        return;
    }
    ClientSession& outSession = **writer;
    ClientSession& inSession = **reader;

    auto out = outSession.registerPort("out", kAudio, 0x2, 0);
    auto in = inSession.registerPort("in", kAudio, 0x1, 0);
    CHECK(out.has_value() && in.has_value());
    if (!out || !in) {
        return;
    }
    const PortIdC outPort = *out;
    const PortIdC inPort = *in;

    std::atomic<std::int32_t> sentFrames { 0 };
    std::atomic<std::int32_t> receivedFrames { 0 };
    std::atomic<float> received { 0.0f };
    std::atomic<std::int32_t> cycles { 0 };
    outSession.setProcess([&](const jackmini::Cycle& cycle) {
        auto* buffer = static_cast<float*>(outSession.portBuffer(outPort, cycle.m_frames));
        for (jackmini::FrameCount i = 0; buffer && i < cycle.m_frames; ++i) {
            buffer[i] = 0.5f;
        }
        sentFrames = static_cast<std::int32_t>(cycle.m_frames);
        return true;
    });
    inSession.setProcess([&](const jackmini::Cycle& cycle) {
        auto* buffer = static_cast<float*>(inSession.portBuffer(inPort, cycle.m_frames));
        if (buffer) {
            received = buffer[cycle.m_frames - 1];
            receivedFrames = static_cast<std::int32_t>(cycle.m_frames);
        }
        cycles.fetch_add(1);
        return true;
    });

    CHECK(outSession.activate().has_value() && inSession.activate().has_value());
    CHECK(outSession.connectPorts("buf_out:out", "buf_in:in").has_value());
    // In freewheel the cycles run faster than the connection takes effect, so the test waits for the value rather than
    // for a number of cycles.
    CHECK(waitFor([&] { return received.load() == 0.5f; }));
    CHECK(cycles.load() > 0);
    CHECK(sentFrames.load() > 0 && receivedFrames.load() == sentFrames.load());

    CHECK(outSession.deactivate().has_value() && inSession.deactivate().has_value());
    CHECK(outSession.unregisterPort(outPort).has_value());
    CHECK(inSession.unregisterPort(inPort).has_value());
    CHECK((*writer)->close().has_value());
    CHECK((*reader)->close().has_value());
}

// Latency: a range set by hand, the default that the server's latency request applies, and the handler that replaces
// the default.
void checkLatency(const jackmini::OpenOptions& exact)
{
    using jackmini::client::ClientSession;
    constexpr const char* kAudio = "32 bit float mono audio";
    constexpr std::uint64_t kOutput = 0x2;
    constexpr std::uint64_t kInput = 0x1;

    auto client = ClientSession::open("lat_test", exact);
    CHECK(client.has_value());
    if (!client) {
        return;
    }
    ClientSession& session = **client;
    auto outA = session.registerPort("out_a", kAudio, kOutput, 0);
    auto outB = session.registerPort("out_b", kAudio, kOutput, 0);
    auto in = session.registerPort("in", kAudio, kInput, 0);
    CHECK(outA.has_value() && outB.has_value() && in.has_value());
    if (!outA || !outB || !in) {
        return;
    }
    CHECK(session.activate().has_value());

    // The server asks only the clients that the driver reaches through the graph, so the input is connected to the
    // driver's capture port.
    CHECK(session.connectPorts("system:capture_1", "lat_test:in").has_value());

    constexpr auto kPlayback = kPlaybackLatency;
    session.setPortLatencyRange(*outA, kPlayback, LatencyRangeC { 5, 15 });
    CHECK(session.portLatencyRange(*outA, kPlayback).m_min == 5);
    CHECK(session.portLatencyRange(*outA, kPlayback).m_max == 15);

    // The default recalculates the output ports from their connections (none here, so {0, 0}), then sets the input
    // ports to the widest of the outputs. A value set by hand on the input is replaced.
    session.setPortLatencyRange(*in, kPlayback, LatencyRangeC { 9, 9 });
    CHECK(session.recomputeLatencies().has_value());
    CHECK(waitFor([&] {
        const auto range = session.portLatencyRange(*in, kPlayback);
        return range.m_min == 0 && range.m_max == 0;
    }));
    CHECK(session.portLatencyRange(*outA, kPlayback).m_max == 0);

    // A handler replaces the default: it is called for the mode the server asks about.
    std::atomic<std::int32_t> handlerCalls { 0 };
    session.setLatencyHandler([&](jackmini::LatencyMode) { handlerCalls.fetch_add(1); });
    CHECK(session.recomputeLatencies().has_value());
    CHECK(waitFor([&] { return handlerCalls.load() >= 1; }));

    CHECK(session.disconnectPorts("system:capture_1", "lat_test:in").has_value());
    CHECK(session.deactivate().has_value());
    CHECK(session.unregisterPort(*outA).has_value());
    CHECK(session.unregisterPort(*outB).has_value());
    CHECK(session.unregisterPort(*in).has_value());
    CHECK((*client)->close().has_value());
}

// Transport: start and stop reach the server, which moves the state; a locate sets the position.
void checkTransport(const jackmini::OpenOptions& exact)
{
    using jackmini::client::ClientSession;

    auto client = ClientSession::open("transport_t", exact);
    CHECK(client.has_value());
    if (!client) {
        return;
    }
    ClientSession& session = **client;
    CHECK(session.activate().has_value());

    PositionC position { };
    CHECK(session.transportQuery(&position) == kTransportStopped);

    session.transportLocate(4800);
    CHECK(waitFor([&] {
        PositionC now { };
        session.transportQuery(&now);
        return now.m_frame >= 4800;
    }));

    session.transportStart();
    CHECK(waitFor([&] { return session.transportQuery(&position) != kTransportStopped; }));
    session.transportStop();
    CHECK(waitFor([&] { return session.transportQuery(&position) == kTransportStopped; }));

    CHECK(session.deactivate().has_value());
    CHECK((*client)->close().has_value());
}

// The callbacks of a running client: the buffer size is reported when the realtime thread starts.
void checkCallbacks(const jackmini::OpenOptions& exact)
{
    using jackmini::client::ClientSession;

    auto client = ClientSession::open("cb_test", exact);
    CHECK(client.has_value());
    if (!client) {
        return;
    }
    ClientSession& session = **client;
    std::atomic<std::int32_t> sizeCalls { 0 };
    std::atomic<std::int32_t> reportedSize { 0 };
    session.setBufferSizeHandler([&](jackmini::FrameCount frames) {
        reportedSize = static_cast<std::int32_t>(frames);
        sizeCalls.fetch_add(1);
        return true;
    });
    session.setProcess([](const jackmini::Cycle&) { return true; });
    CHECK(session.activate().has_value());
    CHECK(waitFor([&] { return sizeCalls.load() >= 1; }));
    CHECK(reportedSize.load() > 0);
    CHECK(session.deactivate().has_value());
    CHECK((*client)->close().has_value());
}

} // end of anonymous namespace

int main()
{
    using jackmini::OpenOptions;
    using jackmini::client::ClientSession;

    OpenOptions exact;
    exact.m_exactName = true;

    auto first = ClientSession::open("session_e2e", exact);
    CHECK(first.has_value());
    if (!first) {
        std::fprintf(stderr, "no server? error %d\n", static_cast<std::int32_t>(first.error()));
        return 1;
    }
    CHECK((*first)->ref() >= 0);
    CHECK((*first)->name() == "session_e2e");
    CHECK((*first)->serverAlive());

    // The exact name is taken: the open fails with NameTaken.
    auto taken = ClientSession::open("session_e2e", exact);
    CHECK(!taken.has_value());
    CHECK(!taken.has_value() && taken.error() == jackmini::Error::kNameTaken);

    // Without exact_name the server gives the client another name.
    auto renamed = ClientSession::open("session_e2e", OpenOptions { });
    CHECK(renamed.has_value());
    if (renamed) {
        CHECK((*renamed)->name() != "session_e2e");
        CHECK((*renamed)->ref() != (*first)->ref());
        CHECK((*renamed)->close().has_value());
        CHECK((*renamed)->close().has_value()); // closing twice does nothing
    }

    checkActivation(exact);
    checkPorts(exact);
    checkBuffers(exact);
    checkLatency(exact);
    checkTransport(exact);
    checkCallbacks(exact);

    CHECK((*first)->close().has_value());
    auto again = ClientSession::open("session_e2e", exact);
    CHECK(again.has_value());
    if (again) {
        CHECK((*again)->close().has_value());
    }

    if (gFailures) {
        std::fprintf(stderr, "client session e2e: %d failure(s)\n", gFailures);
        return 1;
    }
    std::printf("client session e2e checks passed\n");
    return 0;
}
