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

#include <atomic>
#include <csignal>
#include <memory>
#include <mutex>

#include "JackConstants.h"
#include "JackEngineControl.h"
#include "JackGraphManager.h"
#include "JackShmMem.h"
#include "JackSynchro.h"

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

    // The context of the process, or NULL when no client is open. Readable from any thread.
    static ProcessContext* current() noexcept;

    ~ProcessContext();
    ProcessContext(const ProcessContext&) = delete;
    ProcessContext& operator=(const ProcessContext&) = delete;

    JackShmReadWritePtr<JackEngineControl> engine;
    JackShmReadWritePtr<JackGraphManager> graph;
    JackSynchro synchro[CLIENT_NUM];
    std::mutex synchro_mutex; // guards the connection of the synchro entries

private:
    ProcessContext();

    sigset_t saved_signals_;

    static std::mutex registry_mutex_;
    static std::unique_ptr<ProcessContext> instance_;
    static std::atomic<ProcessContext*> current_;
    static int count_;
};

} // namespace jackmini::client
