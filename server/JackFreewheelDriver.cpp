#include <cstdint>

// Copyright (C) 2001 Paul Davis
// Copyright (C) 2004-2008 Grame
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

#include "JackFreewheelDriver.h"
#include "JackEngineControl.h"
#include "JackLockedEngine.h"
#include "JackSystemDeps.h"

namespace jackmini {

// When used in "master" mode

std::int32_t FreewheelDriver::process()
{
    jack_log("JackFreewheelDriver::Process master %lld", m_engineControl->m_timeOutUsecs);
    Driver::cycleTakeBeginTime();

    if (m_engine->process(m_beginDateUst, m_endDateUst)) {

        // Resume connected clients in the graph
        if (!resumeRefNum()) {
            jack_error("JackFreewheelDriver::Process: ResumeRefNum error");
        }

        // Special "SuspendRefNum" with longer timeout
        if (!suspendRefNum()) { // Wait for all clients to finish for FREEWHEEL_DRIVER_TIMEOUT sec
            jack_error("JackFreewheelDriver::Process: SuspendRefNum error");
        }

    } else { // Graph not finished: do not activate it
        jack_error("JackFreewheelDriver::Process: Process error");
    }

    return 0;
}

// When used in "slave" mode

std::int32_t FreewheelDriver::processReadSync()
{
    // Resume connected clients in the graph
    if (!resumeRefNum()) { // Signal all clients
        jack_error("JackFreewheelDriver::ProcessReadSync: ResumeRefNum error");
        return -1;
    }
    return 0;
}

std::int32_t FreewheelDriver::processWriteSync()
{
    // Generic "SuspendRefNum" here
    if (!Driver::suspendRefNum()) {
        jack_error("JackFreewheelDriver::ProcessSync: SuspendRefNum error");
        return -1;
    }
    return 0;
}

std::int32_t FreewheelDriver::processReadAsync()
{
    // Resume connected clients in the graph
    if (!resumeRefNum()) { // Signal all clients
        jack_error("JackFreewheelDriver::ProcessReadAsync: ResumeRefNum error");
        return -1;
    }
    return 0;
}

std::int32_t FreewheelDriver::processWriteAsync()
{
    return 0;
}

Result<void> FreewheelDriver::suspendRefNum()
{
    return m_graphManager->suspendRefNum(&m_clientControl, m_synchroTable, kFreewheelDriverTimeout * 1000000);
}

} // namespace jackmini
