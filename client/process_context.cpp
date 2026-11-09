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

#include "process_context.hpp"

#include "JackError.h"
#include "JackMessageBuffer.h"
#include "JackTime.h"

namespace jackmini::client
{

std::mutex ProcessContext::registry_mutex_;
std::unique_ptr<ProcessContext> ProcessContext::instance_;
int ProcessContext::count_ = 0;

ProcessContext::ProcessContext()
{
    if (!JackMessageBuffer::Create()) {
        jack_error("Cannot create message buffer");
    }

    // A client must not get SIGPIPE when the server has died: the write fails with an error instead.
    sigset_t signals;
    sigemptyset(&signals);
    sigaddset(&signals, SIGPIPE);
    sigprocmask(SIG_BLOCK, &signals, &saved_signals_);
}

ProcessContext::~ProcessContext()
{
    for (auto& entry : synchro) {
        entry.Disconnect();
    }
    JackMessageBuffer::Destroy();
    sigprocmask(SIG_SETMASK, &saved_signals_, nullptr);
}

ProcessContext& ProcessContext::acquire()
{
    std::lock_guard lock(registry_mutex_);
    if (count_++ == 0) {
        init_time();
        instance_.reset(new ProcessContext());
    }
    return *instance_;
}

void ProcessContext::release() noexcept
{
    std::lock_guard lock(registry_mutex_);
    if (count_ > 0 && --count_ == 0) {
        instance_.reset();
        end_time();
    }
}

} // end of namespace
