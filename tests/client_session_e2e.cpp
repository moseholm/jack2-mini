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
