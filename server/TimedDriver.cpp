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

#include "TimedDriver.h"
#include "CompilerDeps.h"
#include "EngineControl.h"
#include "Time.h"
#include <math.h>
#include <unistd.h>
#include <cstdint>
#include <iostream>

namespace jackmini {

std::int32_t TimedDriver::firstCycle(TimeC curTimeUsec)
{
    m_anchorTimeUsec = curTimeUsec;
    return std::int32_t((double(m_engineControl->m_bufferSize) * 1000000) / double(m_engineControl->m_sampleRate));
}

std::int32_t TimedDriver::currentCycle(TimeC curTimeUsec)
{
    return std::int32_t(((double(m_cycleCount) * double(m_engineControl->m_bufferSize) * 1000000.) / double(m_engineControl->m_sampleRate)) - (curTimeUsec - m_anchorTimeUsec));
}

std::int32_t TimedDriver::start()
{
    m_cycleCount = 0;
    return AudioDriver::start();
}

void TimedDriver::processWait()
{
    TimeC curTimeUsec = getMicroseconds();
    std::int32_t waitTimeUsec;

    if (m_cycleCount++ == 0) {
        waitTimeUsec = firstCycle(curTimeUsec);
    } else {
        waitTimeUsec = currentCycle(curTimeUsec);
    }

    if (waitTimeUsec < 0) {
        notifyXRun(curTimeUsec, float(curTimeUsec - m_beginDateUst));
        m_cycleCount = 0;
        waitTimeUsec = 0;
        jack_error("JackTimedDriver::Process XRun = %ld usec", (curTimeUsec - m_beginDateUst));
    }

    // jack_log("JackTimedDriver::Process wait_time = %d", wait_time_usec);
    sleepMicroseconds(waitTimeUsec);
}

} // namespace jackmini
