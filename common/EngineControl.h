// Copyright (C) 2003 Paul Davis
// Copyright (C) 2004-2008 Grame
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

#include "Constants.h"
#include "FrameTimer.h"
#include "ShmMem.h"
#include "TransportEngine.h"
#include "types.h"
#include <stdio.h>
#include <array>
#include <atomic>
#include <cstdint>

namespace jackmini {

class ClientInterface;
class GraphManager;

inline constexpr std::int32_t kEngineRollingCount = 32;
inline constexpr std::int32_t kEngineRollingInterval = 1024;

// \brief Engine control in shared memory.


struct SERVER_EXPORT EngineControl : public ShmMem {
    // Shared state
    NframesC m_bufferSize;
    NframesC m_sampleRate;
    bool m_syncMode;
    bool m_temporary;
    TimeC m_periodUsecs;
    TimeC m_timeOutUsecs;
    float m_maxDelayedUsecs;
    float m_xrunDelayedUsecs;
    bool m_timeOut;
    bool m_realTime;
    bool m_savedRealTime; // RT state saved and restored during Freewheel mode
    std::atomic<bool> m_freewheel; // Freewheel mode is on: clients opened now must use the normal (not flushed) driver synchro
    std::int32_t m_serverPriority;
    std::int32_t m_clientPriority;
    std::int32_t m_maxClientPriority;
    std::array<char, kServerNameSize + 1> m_serverName;
    alignas(UInt32) alignas(TransportEngine) TransportEngine m_transport;
    TimerType m_clockSource;
    std::int32_t m_driverNum;
    bool m_verbose;

    // CPU Load
    TimeC m_prevCycleTime;
    TimeC m_curCycleTime;
    TimeC m_spareUsecs;
    TimeC m_maxUsecs;
    std::array<TimeC, kEngineRollingCount> m_rollingClientUsecs;
    std::uint32_t m_rollingClientUsecsCnt;
    std::uint32_t m_rollingClientUsecsIndex;
    std::uint32_t m_rollingInterval;
    float m_cPULoad;
    float m_maxCPULoad;

    // Thread timing
    UInt64 m_period;
    UInt64 m_computation;
    UInt64 m_constraint;

    // Timer
    alignas(UInt32) alignas(FrameTimer) FrameTimer m_frameTimer;

    EngineControl(bool sync, bool temporary, std::int64_t timeout, bool rt, std::int64_t priority, bool verbose, TimerType clock, const char* serverName)
    {
        static_assert(offsetof(EngineControl, m_transport) % sizeof(UInt32) == 0,
            "fTransport must be aligned within JackEngineControl");
        static_assert(offsetof(EngineControl, m_frameTimer) % sizeof(UInt32) == 0,
            "fFrameTimer must be aligned within JackEngineControl");
        m_bufferSize = 512;
        m_sampleRate = 48000;
        m_periodUsecs = TimeC(1000000.f / m_sampleRate * m_bufferSize);
        m_syncMode = sync;
        m_temporary = temporary;
        m_timeOut = (timeout > 0);
        m_timeOutUsecs = timeout * 1000;
        m_realTime = rt;
        m_savedRealTime = false;
        m_freewheel = false;
        m_serverPriority = priority;

        m_clientPriority = (rt) ? priority - 5 : 0;
        m_maxClientPriority = (rt) ? priority - 1 : 0;
        m_verbose = verbose;
        m_prevCycleTime = 0;
        m_curCycleTime = 0;
        m_spareUsecs = 0;
        m_maxUsecs = 0;
        resetRollingUsecs();
        strncpy(m_serverName.data(), serverName, sizeof(m_serverName));
        m_serverName[sizeof(m_serverName) - 1] = 0;
        m_cPULoad = 0.f;
        m_maxCPULoad = 0.f;
        m_period = 0;
        m_computation = 0;
        m_constraint = 0;
        m_maxDelayedUsecs = 0.f;
        m_xrunDelayedUsecs = 0.f;
        m_clockSource = clock;
        m_driverNum = 0;
    }

    ~EngineControl()
    {
    }

    void updateTimeOut()
    {
        m_periodUsecs = TimeC(1000000.f / m_sampleRate * m_bufferSize); // In microsec
        if (!(m_timeOut && m_timeOutUsecs > 2 * m_periodUsecs)) {
            m_timeOutUsecs = 2 * m_periodUsecs;
        }
    }

    // Cycle
    void cycleIncTime(TimeC callbackUsecs)
    {
        // Timer
        m_frameTimer.incFrameTime(m_bufferSize, callbackUsecs, m_periodUsecs);
    }

    void cycleBegin(std::atomic<ClientInterface*>* table, GraphManager* manager, TimeC curCycleBegin, TimeC prevCycleEnd)
    {
        m_transport.cycleBegin(m_sampleRate, curCycleBegin);
        calcCPULoad(table, manager, curCycleBegin, prevCycleEnd);
    }

    void cycleEnd(std::atomic<ClientInterface*>* table)
    {
        m_transport.cycleEnd(table, m_sampleRate, m_bufferSize);
    }

    // Timer
    void initFrameTime()
    {
        m_frameTimer.initFrameTime();
    }

    void resetFrameTime(TimeC callbackUsecs)
    {
        m_frameTimer.resetFrameTime(callbackUsecs);
    }

    void readFrameTime(Timer* timer)
    {
        m_frameTimer.readFrameTime(timer);
    }

    // XRun
    void notifyXRun(TimeC callbackUsecs, float delayedUsecs);
    void resetXRun()
    {
        m_maxDelayedUsecs = 0.f;
    }

    // Private
    void calcCPULoad(std::atomic<ClientInterface*>* table, GraphManager* manager, TimeC curCycleBegin, TimeC prevCycleEnd);
    void resetRollingUsecs();
};

} // namespace jackmini
