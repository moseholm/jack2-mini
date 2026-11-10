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

#pragma once

#include "JackAtomicArrayState.h"
#include "JackCompilerDeps.h"
#include "jackmini/error.hpp"
#include "types.h"
#include <cstdint>
#include <atomic>

namespace jackmini {

typedef enum {
    kTransportCommandNone = 0,
    kTransportCommandStart = 1,
    kTransportCommandStop = 2,
} TransportCommand;

// \brief The client transport structure.
//
// We have:
//
// - a "current" position
// - a "pending" position prepared by the server at each cycle
// - a "request" position wanted by a client
//
// At the beginning of a cycle the server needs to select a new current position. When a request and a pending position are available,
// the request takes precedence on the pending one. The server atomically switches to the new position.
// The current position can be read by clients.
//
// We use a JackAtomicArrayState pattern that allows to manage several "next" states independently.
//
// In jack1 implementation, transport code (jack_transport_cycle_end) was not called if the graph could not be locked (see jack_run_one_cycle).
// Here transport cycle (CycleBegin, CycleEnd) has to run in the RT thread concurrently with code executed from the "command" thread.
//
// Each client maintains a state in it's shared memory area defined by:
//
// - it's current transport state
// - a boolean that is "true" when slow-sync cb has to be called
// - a boolean that is "true" when timebase cb is called with new_pos on
//
// Several operations set the "slow-sync cb" flag to true:
//
// - setting a new cb (client)
// - activate (client)
// - transport start (server)
// - new pos (server)
//
// Slow-sync cb calls stops when:
//
// - the cb return true (client)
// - deactivate (client)
// - transport stop (server)
//
// Several operations set the "timebase cb" flag to true:
//
// - setting a new cb (client)
// - activate (client)
// - transport start (server) ??
// - new pos (server)
//
// Timebase cb "new_pos" argument calls stops when:
//
// - after one cb call with "new_pos" argument true (client)
// - deactivate (client)
// - release (client)
// - transport stop (server)

class ClientInterface;


class SERVER_EXPORT TransportEngine : public AtomicArrayState<PositionC> {

private:
    TransportStateC m_transportState;
    volatile TransportCommand m_transportCmd;
    TransportCommand m_previousCmd;  // previous transport_cmd
    TimeC m_syncTimeout;
    std::int32_t m_syncTimeLeft;
    std::int32_t m_timeBaseMaster;
    bool m_pendingPos;
    bool m_networkSync;
    bool m_conditionnal;
    alignas(SInt32) SInt32 m_writeCounter;

    bool checkAllRolling(std::atomic<ClientInterface*>* table);
    void makeAllStartingLocating(std::atomic<ClientInterface*>* table);
    void makeAllStopping(std::atomic<ClientInterface*>* table);
    void makeAllLocating(std::atomic<ClientInterface*>* table);

    void syncTimeout(NframesC frameRate, NframesC bufferSize);

public:
    TransportEngine();

    ~TransportEngine()
    {
    }

    void setCommand(TransportCommand state)
    {
        m_transportCmd = state;
    }

    TransportStateC getState() const
    {
        return m_transportState;
    }

    void setState(TransportStateC state)
    {
        m_transportState = state;
    }

    // \brief
    Result<void> resetTimebase(std::int32_t refnum);

    // \brief
    Result<void> setTimebaseMaster(std::int32_t refnum, bool conditionnal);

    void getTimebaseMaster(std::int32_t& refnum, bool& conditionnal)
    {
        refnum = m_timeBaseMaster;
        conditionnal = m_conditionnal;
    }

    // \brief
    void cycleBegin(NframesC frameRate, TimeC time);

    // \brief
    void cycleEnd(std::atomic<ClientInterface*>* table, NframesC frameRate, NframesC bufferSize);

    // \brief
    void setSyncTimeout(TimeC timeout)
    {
        m_syncTimeout = timeout;
    }

    void readCurrentPos(PositionC* pos);

    UniqueC generateUniqueID()
    {
        return (UniqueC)INCATOMIC(&m_writeCounter);
    }

    void requestNewPos(PositionC* pos);

    TransportStateC query(PositionC* pos);

    NframesC getCurrentFrame();

    static void copyPosition(PositionC* from, PositionC* to);

    bool getNetworkSync() const
    {
        return m_networkSync;
    }

    void setNetworkSync(bool sync)
    {
        m_networkSync = sync;
    }
};

} // namespace jackmini
