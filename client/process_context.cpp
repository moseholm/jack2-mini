#include <cstdint>

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

#include "process_context.hpp"

#include "JackError.h"
#include "JackGlobals.h"
#include "JackMessageBuffer.h"
#include "JackTime.h"

namespace jackmini::client {

std::mutex ProcessContext::s_registryMutex;
std::unique_ptr<ProcessContext> ProcessContext::s_instance;
std::atomic<ProcessContext*> ProcessContext::s_current { nullptr };
std::int32_t ProcessContext::s_count = 0;

ProcessContext::ProcessContext()
{
    if (!MessageBuffer::create()) {
        jack_error("Cannot create message buffer");
    }

    // A client must not get SIGPIPE when the server has died: the write fails with an error instead.
    sigset_t signals;
    sigemptyset(&signals);
    sigaddset(&signals, SIGPIPE);
    sigprocmask(SIG_BLOCK, &signals, &m_savedSignals);
}

ProcessContext::~ProcessContext()
{
    for (auto& entry : m_synchro) {
        entry.disconnect();
    }
    MessageBuffer::destroy();
    sigprocmask(SIG_SETMASK, &m_savedSignals, nullptr);
}

ProcessContext& ProcessContext::acquire()
{
    std::lock_guard lock(s_registryMutex);
    if (s_count++ == 0) {
        initTime();
        s_instance.reset(new ProcessContext());
        s_current = s_instance.get();
    }
    return *s_instance;
}

ProcessContext* ProcessContext::current() noexcept
{
    return s_current.load();
}

void ProcessContext::release() noexcept
{
    std::lock_guard lock(s_registryMutex);
    if (s_count > 0 && --s_count == 0) {
        s_current = nullptr;
        s_instance.reset();
        endTime();
    }
}

} // end of namespace jackmini::client

namespace jackmini {

// The accessors of the shared state for the code that is common to the server and the client library. The server
// defines them over its own state; here they are the state of the process's clients, and nullptr when there is none.
SERVER_EXPORT GraphManager* getGraphManager()
{
    auto* context = jackmini::client::ProcessContext::current();
    return context ? context->m_graph.getShmAddress() : nullptr;
}

SERVER_EXPORT EngineControl* getEngineControl()
{
    auto* context = jackmini::client::ProcessContext::current();
    return context ? context->m_engine.getShmAddress() : nullptr;
}

SERVER_EXPORT Synchro* getSynchroTable()
{
    auto* context = jackmini::client::ProcessContext::current();
    return context ? context->m_synchro.data() : nullptr;
}

} // end of namespace jackmini
