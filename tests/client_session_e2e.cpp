/*
Copyright (C) 2026 JACK contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License as published by
the Free Software Foundation; either version 2.1 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
*/

// The client session (client/client_session.hpp) against a running server: open, name handling, and close. Not a
// ctest: it needs a server, for example `jackd -d dummy`.

#include "client_session.hpp"

#include "jackmini/client.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

namespace
{

int failures = 0;

#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::fprintf(stderr, "FAILED %s:%d: %s\n", __FILE__,       \
                         __LINE__, #cond);                             \
            ++failures;                                                \
        }                                                              \
    } while (0)

} // end of anonymous namespace

namespace
{

// Waits until the condition holds, or for about ten seconds. The cycles are counted by the realtime thread, so the
// test polls the count; it does not depend on how long a cycle takes.
template <class F>
bool wait_for(F condition)
{
    for (int i = 0; i < 10000; ++i) {
        if (condition()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return condition();
}

// Activation, the cycles, deactivation, reactivation, and a process callback that fails.
void check_activation(const jackmini::OpenOptions& exact)
{
    using jackmini::client::ClientSession;

    auto client = ClientSession::open("session_rt", exact);
    CHECK(client.has_value());
    if (!client) {
        return;
    }

    std::atomic<int> cycles{0};
    std::atomic<bool> bad_frames{false};
    (*client)->set_process([&](const jackmini::Cycle& cycle) {
        if (cycle.frames == 0) {
            bad_frames = true;
        }
        cycles.fetch_add(1);
        return true;
    });

    CHECK((*client)->activate().has_value());
    CHECK((*client)->is_active());
    CHECK(wait_for([&] { return cycles.load() >= 20; }));

    CHECK((*client)->deactivate().has_value());
    CHECK(!(*client)->is_active());
    // The thread is joined by deactivate, so the count does not change after it returns.
    const int after_deactivate = cycles.load();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    CHECK(cycles.load() == after_deactivate);

    CHECK((*client)->activate().has_value());
    CHECK(wait_for([&] { return cycles.load() >= after_deactivate + 5; }));
    CHECK((*client)->deactivate().has_value());
    CHECK(!bad_frames.load());

    // Closing an active client deactivates it first.
    CHECK((*client)->activate().has_value());
    CHECK(wait_for([&] { return cycles.load() > after_deactivate + 5; }));
    CHECK((*client)->close().has_value());

    // A process callback that fails stops the client: the server takes it out of the graph.
    auto failing = ClientSession::open("session_fail", exact);
    CHECK(failing.has_value());
    if (failing) {
        std::atomic<int> calls{0};
        (*failing)->set_process([&](const jackmini::Cycle&) {
            return calls.fetch_add(1) < 3;
        });
        CHECK((*failing)->activate().has_value());
        CHECK(wait_for([&] { return !(*failing)->is_active(); }));
        CHECK((*failing)->deactivate().has_value());
        CHECK(calls.load() == 4);
        CHECK((*failing)->close().has_value());
    }
}

// Ports and connections between two sessions, and the reasons they are refused.
void check_ports(const jackmini::OpenOptions& exact)
{
    using jackmini::client::ClientSession;
    constexpr const char* kAudio = "32 bit float mono audio";
    constexpr unsigned long kOutput = 0x2;   // JackPortIsOutput
    constexpr unsigned long kInput = 0x1;    // JackPortIsInput
    constexpr jack_port_id_t kNone = 0;

    auto source = ClientSession::open("ports_src", exact);
    auto target = ClientSession::open("ports_dst", exact);
    CHECK(source.has_value() && target.has_value());
    if (!source || !target) {
        return;
    }
    ClientSession& src = **source;
    ClientSession& dst = **target;

    auto out = src.register_port("out", kAudio, kOutput, 0);
    CHECK(out.has_value());
    auto in = dst.register_port("in", kAudio, kInput, 0);
    CHECK(in.has_value());
    if (!out || !in) {
        return;
    }
    CHECK(*out != kNone && *in != kNone);
    CHECK(src.port_name(*out) == "ports_src:out");
    CHECK(std::string(src.port_type(*out)) == kAudio);
    CHECK(src.port_flags(*out) == kOutput);
    CHECK(src.find_port("ports_src:out") == std::optional<jack_port_id_t>(*out));
    CHECK(src.owns(*out));
    CHECK(!dst.owns(*out));   // another client's port is not ours

    const auto outputs = src.port_names(kAudio, kOutput);
    CHECK(std::find(outputs.begin(), outputs.end(), "ports_src:out") != outputs.end());

    // A name with no client part and an empty name are refused; a port of another client cannot be unregistered.
    CHECK(!src.register_port("", kAudio, kOutput, 0).has_value());
    CHECK(!dst.unregister_port(*out).has_value());
    CHECK(!src.register_port("out", kAudio, kOutput, 0).has_value());   // the name is taken

    // The server connects only the ports of active clients. Without a process callback no thread runs.
    CHECK(src.activate().has_value() && dst.activate().has_value());
    CHECK(src.connect_ports("ports_src:out", "ports_dst:in").has_value());
    CHECK(src.is_connected(*out, *in));
    const auto links = src.connections(*out);
    CHECK(std::find(links.begin(), links.end(), "ports_dst:in") != links.end());
    CHECK(src.disconnect_ports("ports_src:out", "ports_dst:in").has_value());
    CHECK(!src.is_connected(*out, *in));

    CHECK(src.unregister_port(*out).has_value());
    CHECK(!src.find_port("ports_src:out").has_value());
    CHECK(dst.unregister_port(*in).has_value());
    CHECK(src.deactivate().has_value() && dst.deactivate().has_value());

    CHECK((*target)->close().has_value());
    CHECK((*source)->close().has_value());
}

// The buffers: a value written by one client's process callback reaches the input buffer of a connected client in the
// same graph, through the realtime threads.
void check_buffers(const jackmini::OpenOptions& exact)
{
    using jackmini::client::ClientSession;
    constexpr const char* kAudio = "32 bit float mono audio";

    auto writer = ClientSession::open("buf_out", exact);
    auto reader = ClientSession::open("buf_in", exact);
    CHECK(writer.has_value() && reader.has_value());
    if (!writer || !reader) {
        return;
    }
    ClientSession& out_session = **writer;
    ClientSession& in_session = **reader;

    auto out = out_session.register_port("out", kAudio, 0x2, 0);
    auto in = in_session.register_port("in", kAudio, 0x1, 0);
    CHECK(out.has_value() && in.has_value());
    if (!out || !in) {
        return;
    }
    const jack_port_id_t out_port = *out;
    const jack_port_id_t in_port = *in;

    std::atomic<int> sent_frames{0};
    std::atomic<int> received_frames{0};
    std::atomic<float> received{0.0f};
    std::atomic<int> cycles{0};
    out_session.set_process([&](const jackmini::Cycle& cycle) {
        auto* buffer = static_cast<float*>(out_session.port_buffer(out_port, cycle.frames));
        for (jackmini::FrameCount i = 0; buffer && i < cycle.frames; ++i) {
            buffer[i] = 0.5f;
        }
        sent_frames = static_cast<int>(cycle.frames);
        return true;
    });
    in_session.set_process([&](const jackmini::Cycle& cycle) {
        auto* buffer = static_cast<float*>(in_session.port_buffer(in_port, cycle.frames));
        if (buffer) {
            received = buffer[cycle.frames - 1];
            received_frames = static_cast<int>(cycle.frames);
        }
        cycles.fetch_add(1);
        return true;
    });

    CHECK(out_session.activate().has_value() && in_session.activate().has_value());
    CHECK(out_session.connect_ports("buf_out:out", "buf_in:in").has_value());
    // In freewheel the cycles run faster than the connection takes effect, so the test waits for the value rather than
    // for a number of cycles.
    CHECK(wait_for([&] { return received.load() == 0.5f; }));
    CHECK(cycles.load() > 0);
    CHECK(sent_frames.load() > 0 && received_frames.load() == sent_frames.load());

    CHECK(out_session.deactivate().has_value() && in_session.deactivate().has_value());
    CHECK(out_session.unregister_port(out_port).has_value());
    CHECK(in_session.unregister_port(in_port).has_value());
    CHECK((*writer)->close().has_value());
    CHECK((*reader)->close().has_value());
}

// Latency: a range set by hand, the default that the server's latency request applies, and the handler that replaces
// the default.
void check_latency(const jackmini::OpenOptions& exact)
{
    using jackmini::client::ClientSession;
    constexpr const char* kAudio = "32 bit float mono audio";
    constexpr unsigned long kOutput = 0x2;
    constexpr unsigned long kInput = 0x1;

    auto client = ClientSession::open("lat_test", exact);
    CHECK(client.has_value());
    if (!client) {
        return;
    }
    ClientSession& session = **client;
    auto out_a = session.register_port("out_a", kAudio, kOutput, 0);
    auto out_b = session.register_port("out_b", kAudio, kOutput, 0);
    auto in = session.register_port("in", kAudio, kInput, 0);
    CHECK(out_a.has_value() && out_b.has_value() && in.has_value());
    if (!out_a || !out_b || !in) {
        return;
    }
    CHECK(session.activate().has_value());

    // The server asks only the clients that the driver reaches through the graph, so the input is connected to the
    // driver's capture port.
    CHECK(session.connect_ports("system:capture_1", "lat_test:in").has_value());

    constexpr auto kPlayback = JackPlaybackLatency;
    session.set_port_latency_range(*out_a, kPlayback, jack_latency_range_t{ 5, 15 });
    CHECK(session.port_latency_range(*out_a, kPlayback).min == 5);
    CHECK(session.port_latency_range(*out_a, kPlayback).max == 15);

    // The default recalculates the output ports from their connections (none here, so {0, 0}), then sets the input
    // ports to the widest of the outputs. A value set by hand on the input is replaced.
    session.set_port_latency_range(*in, kPlayback, jack_latency_range_t{ 9, 9 });
    CHECK(session.recompute_latencies().has_value());
    CHECK(wait_for([&] {
        const auto range = session.port_latency_range(*in, kPlayback);
        return range.min == 0 && range.max == 0;
    }));
    CHECK(session.port_latency_range(*out_a, kPlayback).max == 0);

    // A handler replaces the default: it is called for the mode the server asks about.
    std::atomic<int> handler_calls{0};
    session.set_latency_handler([&](jackmini::LatencyMode) { handler_calls.fetch_add(1); });
    CHECK(session.recompute_latencies().has_value());
    CHECK(wait_for([&] { return handler_calls.load() >= 1; }));

    CHECK(session.disconnect_ports("system:capture_1", "lat_test:in").has_value());
    CHECK(session.deactivate().has_value());
    CHECK(session.unregister_port(*out_a).has_value());
    CHECK(session.unregister_port(*out_b).has_value());
    CHECK(session.unregister_port(*in).has_value());
    CHECK((*client)->close().has_value());
}

// Transport: start and stop reach the server, which moves the state; a locate sets the position.
void check_transport(const jackmini::OpenOptions& exact)
{
    using jackmini::client::ClientSession;

    auto client = ClientSession::open("transport_t", exact);
    CHECK(client.has_value());
    if (!client) {
        return;
    }
    ClientSession& session = **client;
    CHECK(session.activate().has_value());

    jack_position_t position{};
    CHECK(session.transport_query(&position) == JackTransportStopped);

    session.transport_locate(4800);
    CHECK(wait_for([&] {
        jack_position_t now{};
        session.transport_query(&now);
        return now.frame >= 4800;
    }));

    session.transport_start();
    CHECK(wait_for([&] { return session.transport_query(&position) != JackTransportStopped; }));
    session.transport_stop();
    CHECK(wait_for([&] { return session.transport_query(&position) == JackTransportStopped; }));

    CHECK(session.deactivate().has_value());
    CHECK((*client)->close().has_value());
}

// The callbacks of a running client: the buffer size is reported when the realtime thread starts.
void check_callbacks(const jackmini::OpenOptions& exact)
{
    using jackmini::client::ClientSession;

    auto client = ClientSession::open("cb_test", exact);
    CHECK(client.has_value());
    if (!client) {
        return;
    }
    ClientSession& session = **client;
    std::atomic<int> size_calls{0};
    std::atomic<int> reported_size{0};
    session.set_buffer_size_handler([&](jackmini::FrameCount frames) {
        reported_size = static_cast<int>(frames);
        size_calls.fetch_add(1);
        return true;
    });
    session.set_process([](const jackmini::Cycle&) { return true; });
    CHECK(session.activate().has_value());
    CHECK(wait_for([&] { return size_calls.load() >= 1; }));
    CHECK(reported_size.load() > 0);
    CHECK(session.deactivate().has_value());
    CHECK((*client)->close().has_value());
}

} // end of anonymous namespace

int main()
{
    using jackmini::OpenOptions;
    using jackmini::client::ClientSession;

    OpenOptions exact;
    exact.exact_name = true;

    auto first = ClientSession::open("session_e2e", exact);
    CHECK(first.has_value());
    if (!first) {
        std::fprintf(stderr, "no server? error %d\n", static_cast<int>(first.error()));
        return 1;
    }
    CHECK((*first)->ref() >= 0);
    CHECK((*first)->name() == "session_e2e");
    CHECK((*first)->server_alive());

    // The exact name is taken: the open fails with NameTaken.
    auto taken = ClientSession::open("session_e2e", exact);
    CHECK(!taken.has_value());
    CHECK(!taken.has_value() && taken.error() == jackmini::Error::NameTaken);

    // Without exact_name the server gives the client another name.
    auto renamed = ClientSession::open("session_e2e", OpenOptions{});
    CHECK(renamed.has_value());
    if (renamed) {
        CHECK((*renamed)->name() != "session_e2e");
        CHECK((*renamed)->ref() != (*first)->ref());
        CHECK((*renamed)->close().has_value());
        CHECK((*renamed)->close().has_value());   // closing twice does nothing
    }

    check_activation(exact);
    check_ports(exact);
    check_buffers(exact);
    check_latency(exact);
    check_transport(exact);
    check_callbacks(exact);

    CHECK((*first)->close().has_value());
    auto again = ClientSession::open("session_e2e", exact);
    CHECK(again.has_value());
    if (again) {
        CHECK((*again)->close().has_value());
    }

    if (failures) {
        std::fprintf(stderr, "client session e2e: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("client session e2e checks passed\n");
    return 0;
}
