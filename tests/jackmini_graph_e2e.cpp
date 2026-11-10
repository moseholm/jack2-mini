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

// The graph of the jackmini API with a running server: a chain of clients whose data is checked sample by sample, port
// registration at scale, connection rules, naming, and several clients running together.  It replaces the parts of the
// C functional test (test.cpp) that have a jackmini equivalent.  Run it with a server started (for example
// `jackd -d dummy`, or `jackd -F -d dummy` for freewheel, where the checks count cycles).  It is not part of ctest.

#include "jackmini/jackmini.hpp"
#include "jackmini_wait.hpp"

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

using namespace jackmini;

namespace {

std::int32_t gFailures = 0;

void Check(const char* what, bool ok)
{
    std::printf("%-64s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) {
        gFailures++;
    }
}

bool StartsWith(const std::string& s, const std::string& prefix)
{
    return s.compare(0, prefix.size(), prefix) == 0;
}

// Opens a client for the test.  The server is expected to run.
Client OpenOrDie(const char* name)
{
    OpenOptions options;
    options.start_server = false;
    auto opened = Client::open(name, options);
    if (!opened) {
        std::printf("cannot open %s: %s\n", name, std::string(to_string(opened.error())).c_str());
        std::exit(1);
    }
    return std::move(*opened);
}

} // namespace

// The mode the server runs in is given by the caller: "freewheel" for jackd -F, otherwise real time.
int main(std::int32_t argc, char** argv)
{
    const bool expect_freewheel = argc > 1 && std::string(argv[1]) == "freewheel";

    // A chain of three clients: a source writes 0.5 each cycle, a doubler doubles it, a sink keeps the last sample.
    // The sink must see exactly 1.0 (0.5 doubled) once the chain is connected.
    {
        Client source = OpenOrDie("chain_src");
        Client doubler = OpenOrDie("chain_dbl");
        Client sink = OpenOrDie("chain_snk");

        auto src_out = source.register_port("out", PortType::Audio, PortFlags::Output);
        auto dbl_in = doubler.register_port("in", PortType::Audio, PortFlags::Input);
        auto dbl_out = doubler.register_port("out", PortType::Audio, PortFlags::Output);
        auto snk_in = sink.register_port("in", PortType::Audio, PortFlags::Input);
        Check("chain: ports register", src_out && dbl_in && dbl_out && snk_in);

        std::atomic<std::uint64_t> sink_cycles { 0 };
        std::atomic<float> sink_last { -1.0f };
        if (src_out && dbl_in && dbl_out && snk_in) {
            const Port so = *src_out, di = *dbl_in, dO = *dbl_out, si = *snk_in;
            source.on_process([&source, so](const Cycle&) {
                for (auto& s : source.audio_buffer(so)) {
                    s = 0.5f;
                }
                return true;
            });
            doubler.on_process([&doubler, di, dO](const Cycle&) {
                auto in = doubler.audio_buffer(di);
                auto out = doubler.audio_buffer(dO);
                for (std::size_t i = 0; i < out.size(); i++) {
                    out[i] = 2.0f * in[i];
                }
                return true;
            });
            sink.on_process([&sink, si, &sink_cycles, &sink_last](const Cycle&) {
                auto in = sink.audio_buffer(si);
                if (!in.empty()) {
                    sink_last = in.back();
                }
                sink_cycles++;
                return true;
            });
        }

        Check("chain: clients activate", source.activate().has_value() && doubler.activate().has_value() && sink.activate().has_value());
        if (src_out && dbl_in && dbl_out && snk_in) {
            // A handle belongs to the client that obtained it: the other client's ports are reached with find_port.
            auto dbl_in_seen = source.find_port("chain_dbl:in");
            auto snk_in_seen = doubler.find_port("chain_snk:in");
            // A handle of another client is not a port of this one: it is refused, and nothing is connected.
            Check("chain: another client's handle is refused (NotFound)",
                source.connect(*src_out, *dbl_in).error() == Error::NotFound);
            Check("chain: connect source -> doubler -> sink",
                dbl_in_seen && snk_in_seen && source.connect(*src_out, *dbl_in_seen).has_value() && doubler.connect(*dbl_out, *snk_in_seen).has_value());
            // Two cycles after the connections: the sink has run with the whole chain in place.
            Check("chain: the sink runs", WaitCycles(sink_cycles, 2));
            Check("chain: the sink reads the doubled source (1.0)", std::fabs(sink_last.load() - 1.0f) < 1e-6f);
        }
        source.close();
        doubler.close();
        sink.close();
    }

    // Port registration at scale.  The ports are named in order and all of them are listed by the server; closing the
    // client removes them, which the next client sees.
    {
        Client many = OpenOrDie("jackmini_many");
        const std::int32_t count = 64;
        std::int32_t registered = 0;
        for (std::int32_t i = 0; i < count; i++) {
            if (many.register_port("p" + std::to_string(i), PortType::Audio, PortFlags::Output).has_value()) {
                registered++;
            }
        }
        Check("many ports: 64 registered", registered == count);

        std::int32_t listed = 0;
        for (const auto& name : many.port_names(PortType::Audio, PortFlags::Output)) {
            if (StartsWith(name, "jackmini_many:p")) {
                listed++;
            }
        }
        Check("many ports: the server lists all 64", listed == count);

        std::int32_t removed = 0;
        for (std::int32_t i = 0; i < count; i++) {
            auto handle = many.find_port("jackmini_many:p" + std::to_string(i));
            if (handle && many.unregister_port(*handle).has_value()) {
                removed++;
            }
        }
        Check("many ports: all 64 unregistered", removed == count);
        many.close();

        Client after = OpenOrDie("jackmini_many");
        std::int32_t left = 0;
        for (const auto& name : after.port_names(PortType::Audio, PortFlags::Output)) {
            if (StartsWith(name, "jackmini_many:p")) {
                left++;
            }
        }
        Check("many ports: none left after close", left == 0);
        after.close();
    }

    // Connection rules: only an output may connect to an input of the same type.
    {
        Client a = OpenOrDie("rules_a");
        Client b = OpenOrDie("rules_b");
        auto out_a = a.register_port("out", PortType::Audio, PortFlags::Output);
        auto out_b = b.register_port("out", PortType::Audio, PortFlags::Output);
        auto in_a = a.register_port("in", PortType::Audio, PortFlags::Input);
        auto in_b = b.register_port("in", PortType::Audio, PortFlags::Input);
        Check("rules: ports register", out_a && out_b && in_a && in_b);
        Check("rules: both clients activate", a.activate().has_value() && b.activate().has_value());
        if (out_a && out_b && in_a && in_b) {
            auto out_b_seen = a.find_port("rules_b:out");
            auto in_b_seen = a.find_port("rules_b:in");
            Check("rules: the other client's ports are found", out_b_seen && in_b_seen);
            if (out_b_seen && in_b_seen) {
                Check("rules: output to output is refused", !a.connect(*out_a, *out_b_seen).has_value());
                Check("rules: input to output is refused", !a.connect(*in_a, *out_b_seen).has_value());
                Check("rules: output to input of another client is accepted",
                    a.connect(*out_a, *in_b_seen).has_value());
                Check("rules: the connection is listed on both ports",
                    a.is_connected(*out_a, *in_b_seen) && b.is_connected(b.find_port("rules_a:out").value_or(Port { }), *in_b));
            }
        }
        a.close();
        b.close();
    }

    // Names and identities: a name taken is made unique by the server, and every client has its own UUID.
    {
        OpenOptions loose;
        loose.start_server = false;
        auto first = Client::open("jackmini_dup", loose);
        auto second = Client::open("jackmini_dup", loose);
        Check("naming: both clients open with the same name", first.has_value() && second.has_value());
        if (first && second) {
            Check("naming: the second name is made unique", first->name() != second->name());
            Check("naming: the clients have different UUIDs", first->uuid() != second->uuid());
        }
    }

    // Several clients running together.  Each runs its process callback; a transport started by one client is seen by
    // all of them.
    {
        const std::int32_t n = 8;
        std::vector<std::unique_ptr<Client>> clients;
        std::vector<std::unique_ptr<std::atomic<std::uint64_t>>> counts;
        bool all_open = true;
        for (std::int32_t i = 0; i < n; i++) {
            OpenOptions options;
            options.start_server = false;
            auto opened = Client::open("jackmini_group" + std::to_string(i), options);
            if (!opened) {
                all_open = false;
                break;
            }
            clients.push_back(std::make_unique<Client>(std::move(*opened)));
            counts.push_back(std::make_unique<std::atomic<std::uint64_t>>(0));
        }
        Check("group: eight clients open", all_open);
        if (all_open) {
            for (std::int32_t i = 0; i < n; i++) {
                auto* counter = counts[i].get();
                clients[i]->on_process([counter](const Cycle&) {
                    (*counter)++;
                    return true;
                });
            }
            bool all_active = true;
            for (auto& c : clients) {
                all_active = all_active && c->activate().has_value();
            }
            Check("group: eight clients activate", all_active);

            bool all_run = true;
            for (std::int32_t i = 0; i < n; i++) {
                all_run = all_run && WaitCycles(*counts[i], 10);
            }
            Check("group: every client runs its cycles", all_run);

            Check("group: transport starts from the first client", clients[0]->transport_start().has_value());
            bool all_rolling = true;
            for (auto& c : clients) {
                all_rolling = all_rolling && WaitUntil([&] { return c->transport_state() != TransportState::Stopped; });
            }
            Check("group: every client sees the transport rolling", all_rolling);
            Check("group: transport stops from the last client", clients[n - 1]->transport_stop().has_value());
            bool all_stopped = true;
            for (auto& c : clients) {
                all_stopped = all_stopped && WaitUntil([&] { return c->transport_state() == TransportState::Stopped; });
            }
            Check("group: every client sees the transport stopped", all_stopped);

            for (auto& c : clients) {
                c->close();
            }
        }
    }

    // The cycle data: the frames advance by the buffer size each cycle, the times never go back, the freewheel flag is
    // the mode the server runs in, and in real time the next cycle is one period after the current one.
    {
        OpenOptions options;
        options.start_server = false;
        auto opened = Client::open("jackmini_cycle", options);
        Check("cycle: a client opens", opened.has_value());
        if (opened) {
            Client cycle_client = std::move(*opened);
            const FrameCount buffer = cycle_client.buffer_size();
            std::atomic<std::uint64_t> cycles { 0 };
            std::atomic<FrameCount> last_frames { 0 };
            std::atomic<std::uint64_t> last_usecs { 0 };
            std::atomic<bool> frames_ok { true }, times_ok { true }, flag_ok { true }, period_ok { true }, size_ok { true };
            cycle_client.on_process([&](const Cycle& cy) {
                if (cy.frames != buffer) {
                    size_ok = false;
                }
                if (cy.freewheel != expect_freewheel) {
                    flag_ok = false;
                }
                if (cycles.load() > 0) {
                    if (cy.current_frames - last_frames.load() != cy.frames) {
                        frames_ok = false;
                    }
                    if (cy.current_usecs < last_usecs.load()) {
                        times_ok = false;
                    }
                }
                if (!cy.freewheel && std::fabs(static_cast<double>(cy.next_usecs - cy.current_usecs) - cy.period_usecs) > 2.0) {
                    period_ok = false;
                }
                last_frames = cy.current_frames;
                last_usecs = cy.current_usecs;
                cycles++;
                return true;
            });
            Check("cycle: activate", cycle_client.activate().has_value());
            Check("cycle: the process callback runs", WaitCycles(cycles, 200));
            Check("cycle: the buffer size is the cycle's frames", size_ok.load());
            Check("cycle: the frames advance by the buffer size each cycle", frames_ok.load());
            Check("cycle: the times never go back", times_ok.load());
            Check("cycle: the freewheel flag is the server's mode", flag_ok.load());
            Check("cycle: in real time the next cycle is one period later", period_ok.load());
            cycle_client.close();
        }
    }

    std::printf("%s\n", gFailures == 0 ? "jackmini graph checks passed" : "jackmini graph checks FAILED");
    return gFailures == 0 ? 0 : 1;
}
