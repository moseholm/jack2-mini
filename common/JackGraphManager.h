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

#include "JackAtomicState.h"
#include "JackConnectionManager.h"
#include "JackConstants.h"
#include "JackPlatformPlug.h"
#include "JackPort.h"
#include "JackShmMem.h"
#include "JackSystemDeps.h"
#include "jackmini/error.hpp"
#include <array>
#include <cstdint>
#include <atomic>

namespace jackmini {

// \brief Graph manager: contains the connection manager and the port array.


class SERVER_EXPORT GraphManager : public ShmMem, public AtomicState<ConnectionManager> {

private:
    std::uint32_t m_portMax;
    std::array<ClientTiming, kClientNum> m_clientTiming;
    std::array<std::atomic<SInt32>, kClientNum> m_activation; // live activation counters, outside the copied connection state
    std::array<UuidC, kClientNum> m_clientUUID; // the UUID of each client, for the ports it owns
    ServerPort m_portArray[0]; // The actual size depends of port_max, it will be dynamically computed and allocated using "placement" new

    void assertPort(PortIdC portIndex);
    PortIdC allocatePortAux(std::int32_t refnum, const char* portName, const char* portType, PortFlagsC flags);
    void getConnectionsAux(ConnectionManager* manager, const char** res, PortIdC portIndex);
    void getPortsAux(const char** matchingPorts, const char* portNamePattern, const char* typeNamePattern, std::uint64_t flags);
    DefaultAudioSampleC* getBuffer(PortIdC portIndex);
    void* getBufferAux(ConnectionManager* manager, PortIdC portIndex, NframesC frames);
    NframesC computeTotalLatencyAux(PortIdC portIndex, PortIdC srcPortIndex, ConnectionManager* manager, std::int32_t hopCount);
    void recalculateLatencyAux(PortIdC portIndex, LatencyCallbackModeC mode);

public:
    GraphManager(std::int32_t portMax);
    ~GraphManager()
    {
    }

    void setBufferSize(NframesC bufferSize);

    // Ports management
    PortIdC allocatePort(std::int32_t refnum, const char* portName, const char* portType, PortFlagsC flags, NframesC bufferSize);
    Result<void> releasePort(std::int32_t refnum, PortIdC portIndex);
    void getInputPorts(std::int32_t refnum, Int* res);
    void getOutputPorts(std::int32_t refnum, Int* res);
    void removeAllPorts(std::int32_t refnum);
    void disconnectAllPorts(std::int32_t refnum);

    ServerPort* getPort(PortIdC index);
    // Read-only: the ports in use that belong to refnum, found in the port table.
    void getOwnedPorts(std::int32_t refnum, std::vector<PortIdC>& ports);
    PortIdC getPort(const char* name);

    void computeTotalLatency(PortIdC portIndex);
    void computeTotalLatencies();
    void recalculateLatency(PortIdC portIndex, LatencyCallbackModeC mode);

    void requestMonitor(PortIdC portIndex, bool onoff);

    // Connections management
    Result<void> connect(PortIdC srcIndex, PortIdC dstIndex);
    Result<void> disconnect(PortIdC srcIndex, PortIdC dstIndex);
    bool isConnected(PortIdC portSrc, PortIdC portDst);

    // RT, client
    std::int32_t getConnectionsNum(PortIdC portIndex)
    {
        ConnectionManager* manager = readCurrentState();
        return manager->connections(portIndex);
    }

    const char** getConnections(PortIdC portIndex);
    void getConnections(PortIdC portIndex, Int* connections); // TODO
    const char** getPorts(const char* portNamePattern, const char* typeNamePattern, std::uint64_t flags);

    Result<void> getTwoPorts(const char* src, const char* dst, PortIdC* srcIndex, PortIdC* dstIndex);
    Result<void> checkPorts(PortIdC portSrc, PortIdC portDst);

    void disconnectAllInput(PortIdC portIndex);
    void disconnectAllOutput(PortIdC portIndex);
    void disconnectAll(PortIdC portIndex);

    bool isDirectConnection(std::int32_t ref1, std::int32_t ref2);
    void directConnect(std::int32_t ref1, std::int32_t ref2);
    void directDisconnect(std::int32_t ref1, std::int32_t ref2);

    void activate(std::int32_t refnum);
    void deactivate(std::int32_t refnum);

    std::int32_t getInputRefNum(PortIdC portIndex);
    std::int32_t getOutputRefNum(PortIdC portIndex);

    // Buffer management
    void* getBuffer(PortIdC portIndex, NframesC frames);

    // Activation management
    void runCurrentGraph();
    bool runNextGraph();
    bool isFinishedGraph();

    void initRefNum(std::int32_t refnum);
    Result<void> resumeRefNum(ClientControl* control, Synchro* table);
    Result<void> suspendRefNum(ClientControl* control, Synchro* table, std::int64_t usecs);
    void topologicalSort(std::vector<Int>& sorted);

    // Read-only, for the client tools: the port table size, and the UUID of the client that owns a port.
    std::uint32_t portMax() const
    {
        return m_portMax;
    }
    void setClientUUID(std::int32_t refnum, UuidC uuid);
    UuidC getClientUUID(std::int32_t refnum) const
    {
        return m_clientUUID[refnum];
    }

    ClientTiming* getClientTiming(std::int32_t refnum)
    {
        return &m_clientTiming[refnum];
    }

    void save(ConnectionManager* dst);
    void restore(ConnectionManager* src);

    static GraphManager* allocate(std::int32_t portMax);
    static void destroy(GraphManager* manager);
};


} // namespace jackmini
