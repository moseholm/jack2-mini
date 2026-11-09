/*
Copyright (C) 2003 Paul Davis
Copyright (C) 2004-2008 Grame

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

#pragma once

#include "JackNotification.h"
#include "JackPort.h"
#include "JackShmMem.h"
#include "JackSynchro.h"
#include <atomic>

namespace jackmini {

/*!
\brief Client control possibly in shared memory.
*/


struct JackClientControl : public JackShmMemAble {
    char fName[JACK_CLIENT_NAME_SIZE + 1];
    bool fCallback[kMaxNotification];
    volatile jack_transport_state_t fTransportState;
    std::atomic<bool> fTransportSync; /* Will be true when slow-sync cb has to be called */
    std::atomic<bool> fTransportTimebase; /* Will be true when timebase cb is called with new_pos on */
    int fRefNum;
    int fPID;
    bool fActive;

    jack_uuid_t fSessionID;

    JackClientControl(const char* name, int pid, int refnum, jack_uuid_t uuid)
    {
        Init(name, pid, refnum, uuid);
    }

    JackClientControl(const char* name, jack_uuid_t uuid)
    {
        Init(name, 0, -1, uuid);
    }

    JackClientControl()
    {
        Init("", 0, -1, JACK_UUID_EMPTY_INITIALIZER);
    }

    void Init(const char* name, int pid, int refnum, jack_uuid_t uuid)
    {
        strcpy(fName, name);
        for (int i = 0; i < kMaxNotification; i++) {
            fCallback[i] = false;
        }
        // Always activated
        fCallback[kAddClient] = true;
        fCallback[kRemoveClient] = true;
        fCallback[kActivateClient] = true;
        fCallback[kLatencyCallback] = true;
        // So that driver synchro are correctly setup in "flush" or "normal" mode
        fCallback[kStartFreewheelCallback] = true;
        fCallback[kStopFreewheelCallback] = true;
        fRefNum = refnum;
        fPID = pid;
        fTransportState = JackTransportStopped;
        fTransportSync = false;
        fTransportTimebase = false;
        fActive = false;

        fSessionID = uuid;
    }
};

} // namespace jackmini
