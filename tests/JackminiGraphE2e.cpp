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

#include "JackminiWait.hpp"
#include "jackmini/Jackmini.hpp"

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

void check(const char* what, bool ok)
{
    std::printf("%-64s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) {
        gFailures++;
    }
}

bool startsWith(const std::string& s, const std::string& prefix)
{
    return s.compare(0, prefix.size(), prefix) == 0;
}

// Opens a client for the test.  The server is expected to run.
Client openOrDie(const char* name)
{
    OpenOptions options;
    options.m_startServer = false;
    auto opened = Client::open(name, options);
    if (!opened) {
        std::printf("cannot open %s: %s\n", name, std::string(toString(opened.error())).c_str());
        std::exit(1);
    }
    return std::move(*opened);
}

} // namespace

// The mode the server runs in is given by the caller: "freewheel" for jackd -F, otherwise real time.
int main(std::int32_t argc, char** argv)
{
    const bool expectFreewheel = argc > 1 && std::string(argv[1]) == "freewheel";

    // A chain of three clients: a source writes 0.5 each cycle, a doubler doubles it, a sink keeps the last sample.
    // The sink must see exactly 1.0 (0.5 doubled) once the chain is connected.
    {
        Client source = openOrDie("chain_src");
        Client doubler = openOrDie("chain_dbl");
        Client sink = openOrDie("chain_snk");

        auto srcOut = source.registerPort("out", PortType::kAudio, PortFlags::kOutput);
        auto dblIn = doubler.registerPort("in", PortType::kAudio, PortFlags::kInput);
        auto dblOut = doubler.registerPort("out", PortType::kAudio, PortFlags::kOutput);
        auto snkIn = sink.registerPort("in", PortType::kAudio, PortFlags::kInput);
        check("chain: ports register", srcOut && dblIn && dblOut && snkIn);

        std::atomic<std::uint64_t> sinkCycles { 0 };
        std::atomic<float> sinkLast { -1.0f };
        if (srcOut && dblIn && dblOut && snkIn) {
            const Port so = *srcOut, di = *dblIn, dO = *dblOut, si = *snkIn;
            source.onProcess([&source, so](const Cycle&) {
                for (auto& s : source.audioBuffer(so)) {
                    s = 0.5f;
                }
                return true;
            });
            doubler.onProcess([&doubler, di, dO](const Cycle&) {
                auto in = doubler.audioBuffer(di);
                auto out = doubler.audioBuffer(dO);
                for (std::size_t i = 0; i < out.size(); i++) {
                    out[i] = 2.0f * in[i];
                }
                return true;
            });
            sink.onProcess([&sink, si, &sinkCycles, &sinkLast](const Cycle&) {
                auto in = sink.audioBuffer(si);
                if (!in.empty()) {
                    sinkLast = in.back();
                }
                sinkCycles++;
                return true;
            });
        }

        check("chain: clients activate", source.activate().has_value() && doubler.activate().has_value() && sink.activate().has_value());
        if (srcOut && dblIn && dblOut && snkIn) {
            // A handle belongs to the client that obtained it: the other client's ports are reached with find_port.
            auto dblInSeen = source.findPort("chain_dbl:in");
            auto snkInSeen = doubler.findPort("chain_snk:in");
            // A handle of another client is not a port of this one: it is refused, and nothing is connected.
            check("chain: another client's handle is refused (NotFound)",
                source.connect(*srcOut, *dblIn).error() == Error::kNotFound);
            check("chain: connect source -> doubler -> sink",
                dblInSeen && snkInSeen && source.connect(*srcOut, *dblInSeen).has_value() && doubler.connect(*dblOut, *snkInSeen).has_value());
            // Two cycles after the connections: the sink has run with the whole chain in place.
            check("chain: the sink runs", waitCycles(sinkCycles, 2));
            check("chain: the sink reads the doubled source (1.0)", std::fabs(sinkLast.load() - 1.0f) < 1e-6f);
        }
        source.close();
        doubler.close();
        sink.close();
    }

    // Port registration at scale.  The ports are named in order and all of them are listed by the server; closing the
    // client removes them, which the next client sees.
    {
        Client many = openOrDie("jackmini_many");
        const std::int32_t count = 64;
        std::int32_t registered = 0;
        for (std::int32_t i = 0; i < count; i++) {
            if (many.registerPort("p" + std::to_string(i), PortType::kAudio, PortFlags::kOutput).has_value()) {
                registered++;
            }
        }
        check("many ports: 64 registered", registered == count);

        std::int32_t listed = 0;
        for (const auto& name : many.portNames(PortType::kAudio, PortFlags::kOutput)) {
            if (startsWith(name, "jackmini_many:p")) {
                listed++;
            }
        }
        check("many ports: the server lists all 64", listed == count);

        std::int32_t removed = 0;
        for (std::int32_t i = 0; i < count; i++) {
            auto handle = many.findPort("jackmini_many:p" + std::to_string(i));
            if (handle && many.unregisterPort(*handle).has_value()) {
                removed++;
            }
        }
        check("many ports: all 64 unregistered", removed == count);
        many.close();

        Client after = openOrDie("jackmini_many");
        std::int32_t left = 0;
        for (const auto& name : after.portNames(PortType::kAudio, PortFlags::kOutput)) {
            if (startsWith(name, "jackmini_many:p")) {
                left++;
            }
        }
        check("many ports: none left after close", left == 0);
        after.close();
    }

    // Connection rules: only an output may connect to an input of the same type.
    {
        Client a = openOrDie("rules_a");
        Client b = openOrDie("rules_b");
        auto outA = a.registerPort("out", PortType::kAudio, PortFlags::kOutput);
        auto outB = b.registerPort("out", PortType::kAudio, PortFlags::kOutput);
        auto inA = a.registerPort("in", PortType::kAudio, PortFlags::kInput);
        auto inB = b.registerPort("in", PortType::kAudio, PortFlags::kInput);
        check("rules: ports register", outA && outB && inA && inB);
        check("rules: both clients activate", a.activate().has_value() && b.activate().has_value());
        if (outA && outB && inA && inB) {
            auto outBSeen = a.findPort("rules_b:out");
            auto inBSeen = a.findPort("rules_b:in");
            check("rules: the other client's ports are found", outBSeen && inBSeen);
            if (outBSeen && inBSeen) {
                check("rules: output to output is refused", !a.connect(*outA, *outBSeen).has_value());
                check("rules: input to output is refused", !a.connect(*inA, *outBSeen).has_value());
                check("rules: output to input of another client is accepted",
                    a.connect(*outA, *inBSeen).has_value());
                check("rules: the connection is listed on both ports",
                    a.isConnected(*outA, *inBSeen) && b.isConnected(b.findPort("rules_a:out").value_or(Port { }), *inB));
            }
        }
        a.close();
        b.close();
    }

    // Names and identities: a name taken is made unique by the server, and every client has its own UUID.
    {
        OpenOptions loose;
        loose.m_startServer = false;
        auto first = Client::open("jackmini_dup", loose);
        auto second = Client::open("jackmini_dup", loose);
        check("naming: both clients open with the same name", first.has_value() && second.has_value());
        if (first && second) {
            check("naming: the second name is made unique", first->name() != second->name());
            check("naming: the clients have different UUIDs", first->uuid() != second->uuid());
        }
    }

    // Several clients running together.  Each runs its process callback; a transport started by one client is seen by
    // all of them.
    {
        const std::int32_t n = 8;
        std::vector<std::unique_ptr<Client>> clients;
        std::vector<std::unique_ptr<std::atomic<std::uint64_t>>> counts;
        bool allOpen = true;
        for (std::int32_t i = 0; i < n; i++) {
            OpenOptions options;
            options.m_startServer = false;
            auto opened = Client::open("jackmini_group" + std::to_string(i), options);
            if (!opened) {
                allOpen = false;
                break;
            }
            clients.push_back(std::make_unique<Client>(std::move(*opened)));
            counts.push_back(std::make_unique<std::atomic<std::uint64_t>>(0));
        }
        check("group: eight clients open", allOpen);
        if (allOpen) {
            for (std::int32_t i = 0; i < n; i++) {
                auto* counter = counts[i].get();
                clients[i]->onProcess([counter](const Cycle&) {
                    (*counter)++;
                    return true;
                });
            }
            bool allActive = true;
            for (auto& c : clients) {
                allActive = allActive && c->activate().has_value();
            }
            check("group: eight clients activate", allActive);

            bool allRun = true;
            for (std::int32_t i = 0; i < n; i++) {
                allRun = allRun && waitCycles(*counts[i], 10);
            }
            check("group: every client runs its cycles", allRun);

            check("group: transport starts from the first client", clients[0]->transportStart().has_value());
            bool allRolling = true;
            for (auto& c : clients) {
                allRolling = allRolling && waitUntil([&] { return c->transportState() != TransportState::kStopped; });
            }
            check("group: every client sees the transport rolling", allRolling);
            check("group: transport stops from the last client", clients[n - 1]->transportStop().has_value());
            bool allStopped = true;
            for (auto& c : clients) {
                allStopped = allStopped && waitUntil([&] { return c->transportState() == TransportState::kStopped; });
            }
            check("group: every client sees the transport stopped", allStopped);

            for (auto& c : clients) {
                c->close();
            }
        }
    }

    // The cycle data: the frames advance by the buffer size each cycle, the times never go back, the freewheel flag is
    // the mode the server runs in, and in real time the next cycle is one period after the current one.
    {
        OpenOptions options;
        options.m_startServer = false;
        auto opened = Client::open("jackmini_cycle", options);
        check("cycle: a client opens", opened.has_value());
        if (opened) {
            Client cycleClient = std::move(*opened);
            const FrameCount buffer = cycleClient.bufferSize();
            std::atomic<std::uint64_t> cycles { 0 };
            std::atomic<FrameCount> lastFrames { 0 };
            std::atomic<std::uint64_t> lastUsecs { 0 };
            std::atomic<bool> framesOk { true }, timesOk { true }, flagOk { true }, periodOk { true }, sizeOk { true };
            cycleClient.onProcess([&](const Cycle& cy) {
                if (cy.m_frames != buffer) {
                    sizeOk = false;
                }
                if (cy.m_freewheel != expectFreewheel) {
                    flagOk = false;
                }
                if (cycles.load() > 0) {
                    if (cy.m_currentFrames - lastFrames.load() != cy.m_frames) {
                        framesOk = false;
                    }
                    if (cy.m_currentUsecs < lastUsecs.load()) {
                        timesOk = false;
                    }
                }
                if (!cy.m_freewheel && std::fabs(static_cast<double>(cy.m_nextUsecs - cy.m_currentUsecs) - cy.m_periodUsecs) > 2.0) {
                    periodOk = false;
                }
                lastFrames = cy.m_currentFrames;
                lastUsecs = cy.m_currentUsecs;
                cycles++;
                return true;
            });
            check("cycle: activate", cycleClient.activate().has_value());
            check("cycle: the process callback runs", waitCycles(cycles, 200));
            check("cycle: the buffer size is the cycle's frames", sizeOk.load());
            check("cycle: the frames advance by the buffer size each cycle", framesOk.load());
            check("cycle: the times never go back", timesOk.load());
            check("cycle: the freewheel flag is the server's mode", flagOk.load());
            check("cycle: in real time the next cycle is one period later", periodOk.load());
            cycleClient.close();
        }
    }

    std::printf("%s\n", gFailures == 0 ? "jackmini graph checks passed" : "jackmini graph checks FAILED");
    return gFailures == 0 ? 0 : 1;
}
