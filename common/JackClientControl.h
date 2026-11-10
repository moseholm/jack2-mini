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

#include "JackNotification.h"
#include "JackPort.h"
#include "JackShmMem.h"
#include "JackSynchro.h"
#include <array>
#include <cstdint>
#include <atomic>

namespace jackmini {

// \brief Client control possibly in shared memory.


struct ClientControl : public ShmMemAble {
    std::array<char, kClientNameSize + 1> m_name;
    std::array<bool, kMaxNotification> m_callback;
    volatile TransportStateC m_transportState;
    std::atomic<bool> m_transportSync;  // Will be true when slow-sync cb has to be called
    std::atomic<bool> m_transportTimebase;  // Will be true when timebase cb is called with new_pos on
    std::int32_t m_refNum;
    std::int32_t m_pID;
    bool m_active;

    UuidC m_sessionID;

    ClientControl(const char* name, std::int32_t pid, std::int32_t refnum, UuidC uuid)
    {
        init(name, pid, refnum, uuid);
    }

    ClientControl(const char* name, UuidC uuid)
    {
        init(name, 0, -1, uuid);
    }

    ClientControl()
    {
        init("", 0, -1, kUuidEmptyInitializer);
    }

    void init(const char* name, std::int32_t pid, std::int32_t refnum, UuidC uuid)
    {
        strcpy(m_name.data(), name);
        for (std::int32_t i = 0; i < kMaxNotification; i++) {
            m_callback[i] = false;
        }
        // Always activated
        m_callback[kAddClient] = true;
        m_callback[kRemoveClient] = true;
        m_callback[kActivateClient] = true;
        m_callback[kLatencyCallback] = true;
        // So that driver synchro are correctly setup in "flush" or "normal" mode
        m_callback[kStartFreewheelCallback] = true;
        m_callback[kStopFreewheelCallback] = true;
        m_refNum = refnum;
        m_pID = pid;
        m_transportState = kTransportStopped;
        m_transportSync = false;
        m_transportTimebase = false;
        m_active = false;

        m_sessionID = uuid;
    }
};

} // namespace jackmini
