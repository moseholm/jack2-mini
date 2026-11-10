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

#pragma once

#include <array>
#include <atomic>
#include <csignal>
#include <cstdint>
#include <memory>
#include <mutex>

#include "Constants.h"
#include "EngineControl.h"
#include "GraphManager.h"
#include "ShmMem.h"
#include "Synchro.h"

namespace jackmini::client {

// The state that the clients of a process share: the engine control and the graph, mapped from the server's shared
// memory, and the synchro table that wakes the clients of the graph. The first client to open creates it, and the last
// one to close releases it.
class ProcessContext {
public:
    // Takes a reference, creating the context if there is none.
    static ProcessContext& acquire();

    // Drops a reference. The last one destroys the context.
    static void release() noexcept;

    // The context of the process, or nullptr when no client is open. Readable from any thread.
    static ProcessContext* current() noexcept;

    ~ProcessContext();
    ProcessContext(const ProcessContext&) = delete;
    ProcessContext& operator=(const ProcessContext&) = delete;

    ShmReadWritePtr<EngineControl> m_engine;
    ShmReadWritePtr<GraphManager> m_graph;
    std::array<Synchro, kClientNum> m_synchro;
    std::mutex m_synchroMutex; // guards the connection of the synchro entries

private:
    ProcessContext();

    sigset_t m_savedSignals;

    static std::mutex s_registryMutex;
    static std::unique_ptr<ProcessContext> s_instance;
    static std::atomic<ProcessContext*> s_current;
    static std::int32_t s_count;
};

} // namespace jackmini::client
