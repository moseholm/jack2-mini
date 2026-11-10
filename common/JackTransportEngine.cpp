// Copyright (C) 2001 Paul Davis
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

#include "JackTransportEngine.h"
#include "JackClientControl.h"
#include "JackClientInterface.h"
#include "JackEngineControl.h"
#include "JackError.h"
#include "JackGlobals.h"
#include "JackTime.h"
#include <cstdint>
#include <assert.h>
#include <math.h>
#include <stdlib.h>

using namespace std;

namespace jackmini {

TransportEngine::TransportEngine()
    : AtomicArrayState<PositionC>()
{
    static_assert(offsetof(TransportEngine, m_writeCounter) % sizeof(m_writeCounter) == 0,
        "fWriteCounter must be first member of JackTransportEngine to ensure its alignment");
    m_transportState = kTransportStopped;
    m_transportCmd = m_previousCmd = kTransportCommandStop;
    m_syncTimeout = 10000000;  // 10 seconds default
    m_syncTimeLeft = 0;
    m_timeBaseMaster = -1;
    m_writeCounter = 0;
    m_conditionnal = false;
    m_pendingPos = false;
    m_networkSync = false;
}

// compute the number of cycle for timeout
void TransportEngine::syncTimeout(NframesC frameRate, NframesC bufferSize)
{
    std::int64_t bufUsecs = (std::int64_t)((bufferSize * (TimeC)1000000) / frameRate);
    m_syncTimeLeft = m_syncTimeout / bufUsecs;
    jack_log("SyncTimeout fSyncTimeout = %ld fSyncTimeLeft = %ld", (std::int64_t)m_syncTimeout, (std::int64_t)m_syncTimeLeft);
}

// Server
Result<void> TransportEngine::resetTimebase(std::int32_t refnum)
{
    if (m_timeBaseMaster == refnum) {
        PositionC* request = writeNextStateStart(2); // To check
        request->m_valid = (PositionBitsC)0;
        writeNextStateStop(2);
        m_timeBaseMaster = -1;
        return { };
    } else {
        return std::unexpected(Error::kInvalidState);
    }
}

// Server
Result<void> TransportEngine::setTimebaseMaster(std::int32_t refnum, bool conditionnal)
{
    if (conditionnal && m_timeBaseMaster > 0) {
        if (refnum != m_timeBaseMaster) {
            jack_log("conditional timebase for ref = %ld failed: %ld is already the master", refnum, m_timeBaseMaster);
            return std::unexpected(Error::kInvalidState);
        } else {
            jack_log("ref = %ld was already timebase master", refnum);
            return { };
        }
    } else {
        m_timeBaseMaster = refnum;
        m_conditionnal = conditionnal;
        jack_log("new timebase master: ref = %ld", refnum);
        return { };
    }
}

// RT
bool TransportEngine::checkAllRolling(std::atomic<ClientInterface*>* table)
{
    for (std::int32_t i = getEngineControl()->m_driverNum; i < kClientNum; i++) {
        ClientInterface* client = table[i];
        if (client && client->getClientControl()->m_transportState != kTransportRolling) {
            jack_log("CheckAllRolling ref = %ld is not rolling", i);
            return false;
        }
    }
    jack_log("CheckAllRolling");
    return true;
}

// RT
void TransportEngine::makeAllStartingLocating(std::atomic<ClientInterface*>* table)
{
    for (std::int32_t i = getEngineControl()->m_driverNum; i < kClientNum; i++) {
        ClientInterface* client = table[i];
        if (client) {
            ClientControl* control = client->getClientControl();
            // Inactive clients don't have their process function called at all, so they must appear as already "rolling" for the transport....
            control->m_transportState = (control->m_active && control->m_callback[kRealTimeCallback]) ? kTransportStarting : kTransportRolling;
            control->m_transportSync = true;
            control->m_transportTimebase = true;
            jack_log("MakeAllStartingLocating ref = %ld", i);
        }
    }
}

// RT
void TransportEngine::makeAllStopping(std::atomic<ClientInterface*>* table)
{
    for (std::int32_t i = getEngineControl()->m_driverNum; i < kClientNum; i++) {
        ClientInterface* client = table[i];
        if (client) {
            ClientControl* control = client->getClientControl();
            control->m_transportState = kTransportStopped;
            control->m_transportSync = false;
            control->m_transportTimebase = false;
            jack_log("MakeAllStopping ref = %ld", i);
        }
    }
}

// RT
void TransportEngine::makeAllLocating(std::atomic<ClientInterface*>* table)
{
    for (std::int32_t i = getEngineControl()->m_driverNum; i < kClientNum; i++) {
        ClientInterface* client = table[i];
        if (client) {
            ClientControl* control = client->getClientControl();
            control->m_transportState = kTransportStopped;
            control->m_transportSync = true;
            control->m_transportTimebase = true;
            jack_log("MakeAllLocating ref = %ld", i);
        }
    }
}

// RT
void TransportEngine::cycleBegin(NframesC frameRate, TimeC time)
{
    PositionC* pending = writeNextStateStart(1); // Update "pending" state
    pending->m_usecs = time;
    pending->m_frameRate = frameRate;
    writeNextStateStop(1);
}

// RT
void TransportEngine::cycleEnd(std::atomic<ClientInterface*>* table, NframesC frameRate, NframesC bufferSize)
{
    trySwitchState(1); // Switch from "pending" to "current", it always works since there is always a pending state

    // Handle any new transport command from the last cycle.
    TransportCommand cmd = m_transportCmd;
    if (cmd != m_previousCmd) {
        m_previousCmd = cmd;
        jack_log("transport command: %s", (cmd == kTransportCommandStart ? "Transport start" : "Transport stop"));
    } else {
        cmd = kTransportCommandNone;
    }

    // state transition switch
    switch (m_transportState) {

    case kTransportStopped:
        // Set a JackTransportStarting for the current cycle, if all clients are ready (no slow_sync) ==> JackTransportRolling next state
        if (cmd == kTransportCommandStart) {
            jack_log("transport stopped ==> starting frame = %d", readCurrentState()->m_frame);
            m_transportState = kTransportStarting;
            makeAllStartingLocating(table);
            syncTimeout(frameRate, bufferSize);
        } else if (m_pendingPos) {
            jack_log("transport stopped ==> stopped (locating) frame = %d", readCurrentState()->m_frame);
            makeAllLocating(table);
        }
        break;

    case kTransportStarting:
        if (cmd == kTransportCommandStop) {
            jack_log("transport starting ==> stopped frame = %d", readCurrentState()->m_frame);
            m_transportState = kTransportStopped;
            makeAllStopping(table);
        } else if (m_pendingPos) {
            jack_log("transport starting ==> starting frame = %d", readCurrentState()->m_frame);
            m_transportState = kTransportStarting;
            makeAllStartingLocating(table);
            syncTimeout(frameRate, bufferSize);
        } else if (--m_syncTimeLeft == 0 || checkAllRolling(table)) { // Slow clients may still catch up
            if (m_networkSync) {
                jack_log("transport starting ==> netstarting frame = %d");
                m_transportState = kTransportNetStarting;
            } else {
                jack_log("transport starting ==> rolling fSyncTimeLeft = %ld", m_syncTimeLeft);
                m_transportState = kTransportRolling;
            }
        }
        break;

    case kTransportRolling:
        if (cmd == kTransportCommandStop) {
            jack_log("transport rolling ==> stopped");
            m_transportState = kTransportStopped;
            makeAllStopping(table);
        } else if (m_pendingPos) {
            jack_log("transport rolling ==> starting");
            m_transportState = kTransportStarting;
            makeAllStartingLocating(table);
            syncTimeout(frameRate, bufferSize);
        }
        break;

    case kTransportNetStarting:
        break;

    default:
        jack_error("Invalid JACK transport state: %d", m_transportState);
    }

    // Update timebase, if needed.
    if (m_transportState == kTransportRolling) {
        PositionC* pending = writeNextStateStart(1); // Update "pending" state
        pending->m_frame += bufferSize;
        writeNextStateStop(1);
    }

    // See if an asynchronous position request arrived during the last cycle.
    PositionC* request = writeNextStateStart(2, &m_pendingPos);
    if (m_pendingPos) {
        jack_log("New pos = %ld", request->m_frame);
        PositionC* pending = writeNextStateStart(1);
        copyPosition(request, pending);
        writeNextStateStop(1);
    }
}

// Client
void TransportEngine::readCurrentPos(PositionC* pos)
{
    UInt16 nextIndex = getCurrentIndex();
    UInt16 curIndex;
    do {
        curIndex = nextIndex;
        memcpy(pos, readCurrentState(), sizeof(PositionC));
        nextIndex = getCurrentIndex();
    } while (curIndex != nextIndex); // Until a coherent state has been read
}

void TransportEngine::requestNewPos(PositionC* pos)
{
    PositionC* request = writeNextStateStart(2);
    pos->m_unique1 = pos->m_unique2 = generateUniqueID();
    copyPosition(pos, request);
    jack_log("RequestNewPos pos = %ld", pos->m_frame);
    writeNextStateStop(2);
}

TransportStateC TransportEngine::query(PositionC* pos)
{
    if (pos)
        readCurrentPos(pos);
    return getState();
}

NframesC TransportEngine::getCurrentFrame()
{
    PositionC pos;
    readCurrentPos(&pos);

    if (m_transportState == kTransportRolling) {
        float usecs = getMicroseconds() - pos.m_usecs;
        NframesC elapsed = (NframesC)floor((((float)pos.m_frameRate) / 1000000.0f) * usecs);
        return pos.m_frame + elapsed;
    } else {
        return pos.m_frame;
    }
}

// RT, client
void TransportEngine::copyPosition(PositionC* from, PositionC* to)
{
    std::int32_t tries = 0;
    std::int64_t timeout = 1000;

    do {
        // throttle the busy wait if we don't get the answer
        // very quickly. See comment above about this
        // design.
        if (tries > 10) {
            sleepMicroseconds(20);
            tries = 0;

            // debug code to avoid system hangs...
            if (--timeout == 0) {
                jack_error("hung in loop copying position B");
                abort();
            }
        }
        *to = *from;
        tries++;

    } while (to->m_unique1 != to->m_unique2);
}


} // namespace jackmini
