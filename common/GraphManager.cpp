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

#include "GraphManager.h"
#include "Constants.h"
#include "Error.h"
#include "jack/uuid.h"
#include <assert.h>
#include <regex.h>
#include <stdlib.h>
#include <algorithm>
#include <array>
#include <cstdint>

namespace jackmini {

static void assertBufferSize(NframesC bufferSize)
{
    if (bufferSize > kBufferSizeMax) {
        jack_log("JackGraphManager::AssertBufferSize frames = %ld", bufferSize);
        assert(buffer_size <= kBufferSizeMax);
    }
}

void GraphManager::assertPort(PortIdC portIndex)
{
    if (portIndex >= m_portMax) {
        jack_log("JackGraphManager::AssertPort port_index = %ld", portIndex);
        assert(port_index < fPortMax);
    }
}

GraphManager* GraphManager::allocate(std::int32_t portMax)
{
    // Using "Placement" new
    void* sharedPtr = ShmMem::operator new(sizeof(GraphManager) + portMax * sizeof(ServerPort));
    return new (sharedPtr) GraphManager(portMax);
}

void GraphManager::destroy(GraphManager* manager)
{
    // "Placement" new was used
    manager->~GraphManager();
    ShmMem::operator delete(manager);
}

GraphManager::GraphManager(std::int32_t portMax)
{
    assert(port_max <= kPortNumMax);

    for (std::int32_t i = 0; i < portMax; i++) {
        m_portArray[i].release();
    }

    m_portMax = portMax;
    for (std::int32_t i = 0; i < kClientNum; i++) {
        m_activation[i].store(0);
        m_clientUUID[i] = 0;
    }
}

void GraphManager::setClientUUID(std::int32_t refnum, UuidC uuid)
{
    m_clientUUID[refnum] = uuid;
}

ServerPort* GraphManager::getPort(PortIdC portIndex)
{
    assertPort(portIndex);
    return &m_portArray[portIndex];
}

void GraphManager::getOwnedPorts(std::int32_t refnum, std::vector<PortIdC>& ports)
{
    ports.clear();
    for (std::uint32_t i = kFirstAvailablePort; i < m_portMax; i++) {
        const ServerPort& port = m_portArray[i];
        if (port.isUsed() && port.getRefNum() == refnum) {
            ports.push_back(i);
        }
    }
}

DefaultAudioSampleC* GraphManager::getBuffer(PortIdC portIndex)
{
    return m_portArray[portIndex].getBuffer();
}

// Server
void GraphManager::initRefNum(std::int32_t refnum)
{
    ConnectionManager* manager = writeNextStateStart();
    manager->initRefNum(refnum);
    writeNextStateStop();
}

// RT
void GraphManager::runCurrentGraph()
{
    ConnectionManager* manager = readCurrentState();
    manager->resetGraph(m_clientTiming.data(), m_activation.data());
}

// RT
bool GraphManager::runNextGraph()
{
    bool res;
    ConnectionManager* manager = trySwitchState(&res);
    manager->resetGraph(m_clientTiming.data(), m_activation.data());
    return res;
}

// RT
bool GraphManager::isFinishedGraph()
{
    return (m_activation[kFreewheelDriverRefnum].load() == 0);
}

// RT
Result<void> GraphManager::resumeRefNum(ClientControl* control, Synchro* table)
{
    ConnectionManager* manager = readCurrentState();
    return manager->resumeRefNum(control, table, m_clientTiming.data(), m_activation.data());
}

// RT
Result<void> GraphManager::suspendRefNum(ClientControl* control, Synchro* table, std::int64_t usec)
{
    ConnectionManager* manager = readCurrentState();
    return manager->suspendRefNum(control, table, m_clientTiming.data(), usec);
}

void GraphManager::topologicalSort(std::vector<Int>& sorted)
{
    UInt16 curIndex;
    UInt16 nextIndex;

    do {
        curIndex = getCurrentIndex();
        sorted.clear();
        readCurrentState()->topologicalSort(sorted);
        nextIndex = getCurrentIndex();
    } while (curIndex != nextIndex); // Until a coherent state has been read
}

// Server
void GraphManager::directConnect(std::int32_t ref1, std::int32_t ref2)
{
    ConnectionManager* manager = writeNextStateStart();
    manager->directConnect(ref1, ref2);
    jack_log("JackGraphManager::ConnectRefNum cur_index = %ld ref1 = %ld ref2 = %ld", curIndexOf(loadCounter(m_counter)), ref1, ref2);
    writeNextStateStop();
}

// Server
void GraphManager::directDisconnect(std::int32_t ref1, std::int32_t ref2)
{
    ConnectionManager* manager = writeNextStateStart();
    manager->directDisconnect(ref1, ref2);
    jack_log("JackGraphManager::DisconnectRefNum cur_index = %ld ref1 = %ld ref2 = %ld", curIndexOf(loadCounter(m_counter)), ref1, ref2);
    writeNextStateStop();
}

// Server
bool GraphManager::isDirectConnection(std::int32_t ref1, std::int32_t ref2)
{
    ConnectionManager* manager = readCurrentState();
    return manager->isDirectConnection(ref1, ref2);
}

// RT
void* GraphManager::getBuffer(PortIdC portIndex, NframesC bufferSize)
{
    assertPort(portIndex);
    assertBufferSize(bufferSize);

    ConnectionManager* manager = readCurrentState();
    ServerPort* port = getPort(portIndex);

    // This happens when a port has just been unregistered and is still used by the RT code
    if (!port->isUsed()) {
        jack_log("JackGraphManager::GetBuffer : port = %ld is released state", portIndex);
        return getBuffer(0); // port_index 0 is not used
    }

    // Output port
    if (port->m_flags & kPortIsOutput) {
        return (port->m_tied != kNoPort) ? getBuffer(port->m_tied, bufferSize) : getBuffer(portIndex);
    }

    // The input of a deactivated client is silent; the outputs of deactivated clients are not mixed in.
    std::array<PortIdC, kConnectionNumForPort> active;
    Int len = 0;
    if (!manager->isInactive(port->getRefNum())) {
        const Int connections = manager->connections(portIndex);
        for (Int i = 0; i < connections; i++) {
            const PortIdC srcIndex = manager->getPort(portIndex, i);
            if (!manager->isInactive(getPort(srcIndex)->getRefNum())) {
                active[len++] = srcIndex;
            }
        }
    }

    // No connections : return a zero-filled buffer
    if (len == 0) {
        port->clearBuffer(bufferSize);
        return port->getBuffer();

        // One connection
    } else if (len == 1) {
        PortIdC srcIndex = active[0];

        // Ports in same client : copy the buffer
        if (getPort(srcIndex)->getRefNum() == port->getRefNum()) {
            std::array<void*, 1> buffers;
            buffers[0] = getBuffer(srcIndex, bufferSize);
            port->mixBuffers(buffers.data(), 1, bufferSize);
            return port->getBuffer();
            // Otherwise, use zero-copy mode, just pass the buffer of the connected (output) port.
        } else {
            return getBuffer(srcIndex, bufferSize);
        }

        // Multiple connections : mix all buffers
    } else {
        std::array<void*, kConnectionNumForPort> buffers;
        for (Int i = 0; i < len; i++) {
            buffers[i] = getBuffer(active[i], bufferSize);
        }
        port->mixBuffers(buffers.data(), len, bufferSize);
        return port->getBuffer();
    }
}

// Server
void GraphManager::requestMonitor(PortIdC portIndex, bool onoff) // Client
{
    assertPort(portIndex);
    ServerPort* port = getPort(portIndex);

    // jackd.h
    // * If @ref JackPortCanMonitor is set for this @a port, turn input
    // * monitoring on or off. Otherwise, do nothing.
    //
    // if (!(fFlags & JackPortCanMonitor))
    // return -1;

    port->requestMonitor(onoff);

    const Int* connections = readCurrentState()->getConnections(portIndex);
    if ((port->m_flags & kPortIsOutput) == 0) { // ?? Taken from jack, why not (port->fFlags  & JackPortIsInput) ?
        PortIdC srcIndex;
        for (std::int32_t i = 0; (i < kConnectionNumForPort) && ((srcIndex = connections[i]) != kEmpty); i++) {
            // XXX much worse things will happen if there is a feedback loop !!!
            requestMonitor(srcIndex, onoff);
        }
    }
}

// Client
NframesC GraphManager::computeTotalLatencyAux(PortIdC portIndex, PortIdC srcPortIndex, ConnectionManager* manager, std::int32_t hopCount)
{
    const Int* connections = readCurrentState()->getConnections(portIndex);
    NframesC maxLatency = 0;
    PortIdC dstIndex;

    if (hopCount > 8)
        return getPort(portIndex)->getLatency();

    for (std::int32_t i = 0; (i < kConnectionNumForPort) && ((dstIndex = connections[i]) != kEmpty); i++) {
        if (srcPortIndex != dstIndex) {
            assertPort(dstIndex);
            ServerPort* dstPort = getPort(dstIndex);
            NframesC thisLatency = (dstPort->m_flags & kPortIsTerminal)
                ? dstPort->getLatency()
                : computeTotalLatencyAux(dstIndex, portIndex, manager, hopCount + 1);
            maxLatency = ((maxLatency > thisLatency) ? maxLatency : thisLatency);
        }
    }

    return maxLatency + getPort(portIndex)->getLatency();
}

// Client
void GraphManager::computeTotalLatency(PortIdC portIndex)
{
    UInt16 curIndex;
    UInt16 nextIndex;
    ServerPort* port = getPort(portIndex);
    assertPort(portIndex);

    do {
        curIndex = getCurrentIndex();
        port->m_totalLatency = computeTotalLatencyAux(portIndex, portIndex, readCurrentState(), 0);
        nextIndex = getCurrentIndex();
    } while (curIndex != nextIndex); // Until a coherent state has been read

    jack_log("JackGraphManager::GetTotalLatency port_index = %ld total latency = %ld", portIndex, port->m_totalLatency);
}

// Client
void GraphManager::computeTotalLatencies()
{
    PortIdC portIndex;
    for (portIndex = kFirstAvailablePort; portIndex < m_portMax; portIndex++) {
        ServerPort* port = getPort(portIndex);
        if (port->isUsed()) {
            computeTotalLatency(portIndex);
        }
    }
}

void GraphManager::recalculateLatencyAux(PortIdC portIndex, LatencyCallbackModeC mode)
{
    const Int* connections = readCurrentState()->getConnections(portIndex);
    ServerPort* port = getPort(portIndex);
    LatencyRangeC latency = { UINT32_MAX, 0 };
    PortIdC dstIndex;

    for (std::int32_t i = 0; (i < kConnectionNumForPort) && ((dstIndex = connections[i]) != kEmpty); i++) {
        assertPort(dstIndex);
        ServerPort* dstPort = getPort(dstIndex);
        LatencyRangeC otherLatency;

        dstPort->getLatencyRange(mode, &otherLatency);

        if (otherLatency.m_max > latency.m_max) {
            latency.m_max = otherLatency.m_max;
        }
        if (otherLatency.m_min < latency.m_min) {
            latency.m_min = otherLatency.m_min;
        }
    }

    if (latency.m_min == UINT32_MAX) {
        latency.m_min = 0;
    }

    port->setLatencyRange(mode, &latency);
}

void GraphManager::recalculateLatency(PortIdC portIndex, LatencyCallbackModeC mode)
{
    UInt16 curIndex;
    UInt16 nextIndex;

    do {
        curIndex = getCurrentIndex();
        recalculateLatencyAux(portIndex, mode);
        nextIndex = getCurrentIndex();
    } while (curIndex != nextIndex); // Until a coherent state has been read

    // jack_log("JackGraphManager::RecalculateLatency port_index = %ld", port_index);
}

// Server
void GraphManager::setBufferSize(NframesC bufferSize)
{
    jack_log("JackGraphManager::SetBufferSize size = %ld", bufferSize);

    PortIdC portIndex;
    for (portIndex = kFirstAvailablePort; portIndex < m_portMax; portIndex++) {
        ServerPort* port = getPort(portIndex);
        if (port->isUsed()) {
            port->clearBuffer(bufferSize);
        }
    }
}

// Server
PortIdC GraphManager::allocatePortAux(std::int32_t refnum, const char* portName, const char* portType, PortFlagsC flags)
{
    PortIdC portIndex;

    // Available ports start at FIRST_AVAILABLE_PORT (= 1), otherwise a port_index of 0 is "seen" as a nullptr port by the external API...
    for (portIndex = kFirstAvailablePort; portIndex < m_portMax; portIndex++) {
        ServerPort* port = getPort(portIndex);
        if (!port->isUsed()) {
            jack_log("JackGraphManager::AllocatePortAux port_index = %ld name = %s type = %s", portIndex, portName, portType);
            if (!port->allocate(refnum, portName, portType, flags)) {
                return kNoPort;
            }
            port->m_uUID = jack_port_uuid_generate(portIndex);
            break;
        }
    }

    return (portIndex < m_portMax) ? portIndex : kNoPort;
}

// Server
PortIdC GraphManager::allocatePort(std::int32_t refnum, const char* portName, const char* portType, PortFlagsC flags, NframesC bufferSize)
{
    ConnectionManager* manager = writeNextStateStart();
    PortIdC portIndex = allocatePortAux(refnum, portName, portType, flags);

    if (portIndex != kNoPort) {
        ServerPort* port = getPort(portIndex);
        assert(port);
        port->clearBuffer(bufferSize);

        Result<void> res;
        if (flags & kPortIsOutput) {
            res = manager->addOutputPort(refnum, portIndex);
        } else {
            res = manager->addInputPort(refnum, portIndex);
        }
        // Insertion failure
        if (!res) {
            port->release();
            portIndex = kNoPort;
        }
    }

    writeNextStateStop();
    return portIndex;
}

// Server
Result<void> GraphManager::releasePort(std::int32_t refnum, PortIdC portIndex)
{
    ConnectionManager* manager = writeNextStateStart();
    ServerPort* port = getPort(portIndex);
    Result<void> res;

    if (port->m_flags & kPortIsOutput) {
        disconnectAllOutput(portIndex);
        res = manager->removeOutputPort(refnum, portIndex);
    } else {
        disconnectAllInput(portIndex);
        res = manager->removeInputPort(refnum, portIndex);
    }

    port->release();
    writeNextStateStop();
    return res;
}

void GraphManager::getInputPorts(std::int32_t refnum, Int* res)
{
    ConnectionManager* manager = writeNextStateStart();
    const Int* input = manager->getInputPorts(refnum);
    memcpy(res, input, sizeof(Int) * kPortNumForClient);
    writeNextStateStop();
}

void GraphManager::getOutputPorts(std::int32_t refnum, Int* res)
{
    ConnectionManager* manager = writeNextStateStart();
    const Int* output = manager->getOutputPorts(refnum);
    memcpy(res, output, sizeof(Int) * kPortNumForClient);
    writeNextStateStop();
}

// Server
void GraphManager::removeAllPorts(std::int32_t refnum)
{
    jack_log("JackGraphManager::RemoveAllPorts ref = %ld", refnum);
    ConnectionManager* manager = writeNextStateStart();
    PortIdC portIndex;

    // Warning : ReleasePort shift port to left, thus we always remove the first port until the "input" table is empty
    const Int* input = manager->getInputPorts(refnum);
    while ((portIndex = input[0]) != kEmpty) {
        if (!releasePort(refnum, portIndex)) {
            jack_error("JackGraphManager::RemoveAllPorts failure ref = %ld port_index = %ld", refnum, portIndex);
            assert(true);
            break;
        }
    }

    // Warning : ReleasePort shift port to left, thus we always remove the first port until the "output" table is empty
    const Int* output = manager->getOutputPorts(refnum);
    while ((portIndex = output[0]) != kEmpty) {
        if (!releasePort(refnum, portIndex)) {
            jack_error("JackGraphManager::RemoveAllPorts failure ref = %ld port_index = %ld", refnum, portIndex);
            assert(true);
            break;
        }
    }

    writeNextStateStop();
}

// Server
void GraphManager::disconnectAllPorts(std::int32_t refnum)
{
    std::int32_t i;
    jack_log("JackGraphManager::DisconnectAllPorts ref = %ld", refnum);
    ConnectionManager* manager = writeNextStateStart();

    const Int* input = manager->getInputPorts(refnum);
    for (i = 0; i < kPortNumForClient && input[i] != kEmpty; i++) {
        disconnectAllInput(input[i]);
    }

    const Int* output = manager->getOutputPorts(refnum);
    for (i = 0; i < kPortNumForClient && output[i] != kEmpty; i++) {
        disconnectAllOutput(output[i]);
    }

    writeNextStateStop();
}

// Server
void GraphManager::disconnectAllInput(PortIdC portIndex)
{
    jack_log("JackGraphManager::DisconnectAllInput port_index = %ld", portIndex);
    ConnectionManager* manager = writeNextStateStart();

    for (std::uint32_t i = 0; i < m_portMax; i++) {
        if (manager->isConnected(i, portIndex)) {
            jack_log("JackGraphManager::Disconnect i = %ld  port_index = %ld", i, portIndex);
            disconnect(i, portIndex);
        }
    }
    writeNextStateStop();
}

// Server
void GraphManager::disconnectAllOutput(PortIdC portIndex)
{
    jack_log("JackGraphManager::DisconnectAllOutput port_index = %ld ", portIndex);
    ConnectionManager* manager = writeNextStateStart();

    const Int* connections = manager->getConnections(portIndex);
    while (connections[0] != kEmpty) {
        disconnect(portIndex, connections[0]); // Warning : Disconnect shift port to left
    }
    writeNextStateStop();
}

// Server
void GraphManager::disconnectAll(PortIdC portIndex)
{
    assertPort(portIndex);

    ServerPort* port = getPort(portIndex);
    if (port->m_flags & kPortIsOutput) {
        disconnectAllOutput(portIndex);
    } else {
        disconnectAllInput(portIndex);
    }
}

// Server
void GraphManager::getConnections(PortIdC portIndex, Int* res)
{
    ConnectionManager* manager = writeNextStateStart();
    const Int* connections = manager->getConnections(portIndex);
    memcpy(res, connections, sizeof(Int) * kConnectionNumForPort);
    writeNextStateStop();
}

// Server
void GraphManager::activate(std::int32_t refnum)
{
    ConnectionManager* manager = writeNextStateStart();
    manager->setInactive(refnum, false);
    manager->directConnect(kFreewheelDriverRefnum, refnum);
    manager->directConnect(refnum, kFreewheelDriverRefnum);
    writeNextStateStop();
}

// Disconnection from the FW must be done in last otherwise an intermediate "unconnected"
// (thus unactivated) state may happen where the client is still checked for its end.

// Server
void GraphManager::deactivate(std::int32_t refnum)
{
    // The activation edges go while the client still counts; then it is inactive. Disconnection from the FW must be done
    // last, as in Activate, so that no intermediate state is seen where the client is unactivated and still checked.
    ConnectionManager* manager = writeNextStateStart();
    if (manager->isDirectConnection(refnum, kFreewheelDriverRefnum)) {
        manager->directDisconnect(refnum, kFreewheelDriverRefnum);
    } else {
        jack_log("JackServer::Deactivate client = %ld was not activated", refnum);
    }
    if (manager->isDirectConnection(kFreewheelDriverRefnum, refnum)) {
        manager->directDisconnect(kFreewheelDriverRefnum, refnum);
    } else {
        jack_log("JackServer::Deactivate client = %ld was not activated", refnum);
    }
    manager->setInactive(refnum, true);
    writeNextStateStop();
}

// Server
std::int32_t GraphManager::getInputRefNum(PortIdC portIndex)
{
    assertPort(portIndex);
    ConnectionManager* manager = writeNextStateStart();
    std::int32_t res = manager->getInputRefNum(portIndex);
    writeNextStateStop();
    return res;
}

// Server
std::int32_t GraphManager::getOutputRefNum(PortIdC portIndex)
{
    assertPort(portIndex);
    ConnectionManager* manager = writeNextStateStart();
    std::int32_t res = manager->getOutputRefNum(portIndex);
    writeNextStateStop();
    return res;
}

Result<void> GraphManager::connect(PortIdC portSrc, PortIdC portDst)
{
    ConnectionManager* manager = writeNextStateStart();
    jack_log("JackGraphManager::Connect port_src = %ld port_dst = %ld", portSrc, portDst);
    ServerPort* src = getPort(portSrc);
    ServerPort* dst = getPort(portDst);
    Result<void> res;

    if (!src->m_inUse || !dst->m_inUse) {
        if (!src->m_inUse)
            jack_error("JackGraphManager::Connect port_src = %ld not used name = %s", portSrc, getPort(portSrc)->m_name);
        if (!dst->m_inUse)
            jack_error("JackGraphManager::Connect port_dst = %ld not used name = %s", portDst, getPort(portDst)->m_name);
        res = std::unexpected(Error::kNotFound);
        goto end;
    }
    if (src->m_typeId != dst->m_typeId) {
        jack_error("JackGraphManager::Connect different port types port_src = %ld port_dst = %ld", portSrc, portDst);
        res = std::unexpected(Error::kInvalidArgument);
        goto end;
    }
    if (manager->isConnected(portSrc, portDst)) {
        jack_error("JackGraphManager::Connect already connected port_src = %ld port_dst = %ld", portSrc, portDst);
        res = std::unexpected(Error::kInvalidState);
        goto end;
    }

    res = manager->connect(portSrc, portDst);
    if (!res) {
        jack_error("JackGraphManager::Connect failed port_src = %ld port_dst = %ld", portSrc, portDst);
        goto end;
    }
    res = manager->connect(portDst, portSrc);
    if (!res) {
        jack_error("JackGraphManager::Connect failed port_dst = %ld port_src = %ld", portDst, portSrc);
        goto end;
    }

    if (manager->isLoopPath(portSrc, portDst)) {
        jack_log("JackGraphManager::Connect: LOOP detected");
        manager->incFeedbackConnection(portSrc, portDst);
    } else {
        manager->incDirectConnection(portSrc, portDst);
    }

end:
    writeNextStateStop();
    return res;
}

// Server
Result<void> GraphManager::disconnect(PortIdC portSrc, PortIdC portDst)
{
    ConnectionManager* manager = writeNextStateStart();
    jack_log("JackGraphManager::Disconnect port_src = %ld port_dst = %ld", portSrc, portDst);
    bool inUseSrc = getPort(portSrc)->m_inUse;
    bool inUseDst = getPort(portDst)->m_inUse;
    Result<void> res;

    if (!inUseSrc || !inUseDst) {
        if (!inUseSrc)
            jack_error("JackGraphManager::Disconnect: port_src = %ld not used name = %s", portSrc, getPort(portSrc)->m_name);
        if (!inUseDst)
            jack_error("JackGraphManager::Disconnect: port_src = %ld not used name = %s", portDst, getPort(portDst)->m_name);
        res = std::unexpected(Error::kNotFound);
        goto end;
    }
    if (!manager->isConnected(portSrc, portDst)) {
        jack_error("JackGraphManager::Disconnect not connected port_src = %ld port_dst = %ld", portSrc, portDst);
        res = std::unexpected(Error::kNotFound);
        goto end;
    }

    res = manager->disconnect(portSrc, portDst);
    if (!res) {
        jack_error("JackGraphManager::Disconnect failed port_src = %ld port_dst = %ld", portSrc, portDst);
        goto end;
    }
    res = manager->disconnect(portDst, portSrc);
    if (!res) {
        jack_error("JackGraphManager::Disconnect failed port_dst = %ld port_src = %ld", portDst, portSrc);
        goto end;
    }

    if (manager->isFeedbackConnection(portSrc, portDst)) {
        jack_log("JackGraphManager::Disconnect: FEEDBACK removed");
        manager->decFeedbackConnection(portSrc, portDst);
    } else {
        manager->decDirectConnection(portSrc, portDst);
    }

end:
    writeNextStateStop();
    return res;
}

// Client
bool GraphManager::isConnected(PortIdC portSrc, PortIdC portDst)
{
    ConnectionManager* manager = readCurrentState();
    return manager->isConnected(portSrc, portDst);
}

// Server
Result<void> GraphManager::checkPorts(PortIdC portSrc, PortIdC portDst)
{
    ServerPort* src = getPort(portSrc);
    ServerPort* dst = getPort(portDst);

    if ((dst->m_flags & kPortIsInput) == 0) {
        jack_error("Destination port in attempted (dis)connection of %s and %s is not an input port", src->m_name, dst->m_name);
        return std::unexpected(Error::kInvalidArgument);
    }

    if ((src->m_flags & kPortIsOutput) == 0) {
        jack_error("Source port in attempted (dis)connection of %s and %s is not an output port", src->m_name, dst->m_name);
        return std::unexpected(Error::kInvalidArgument);
    }

    return { };
}

Result<void> GraphManager::getTwoPorts(const char* srcName, const char* dstName, PortIdC* portSrc, PortIdC* portDst)
{
    jack_log("JackGraphManager::CheckConnect src_name = %s dst_name = %s", srcName, dstName);

    if ((*portSrc = getPort(srcName)) == kNoPort) {
        jack_error("Unknown source port in attempted (dis)connection src_name [%s] dst_name [%s]", srcName, dstName);
        return std::unexpected(Error::kNotFound);
    }

    if ((*portDst = getPort(dstName)) == kNoPort) {
        jack_error("Unknown destination port in attempted (dis)connection src_name [%s] dst_name [%s]", srcName, dstName);
        return std::unexpected(Error::kNotFound);
    }

    return { };
}

// Client : port array
PortIdC GraphManager::getPort(const char* name)
{
    for (std::uint32_t i = 0; i < m_portMax; i++) {
        ServerPort* port = getPort(i);
        if (port->isUsed() && port->nameEquals(name)) {
            return i;
        }
    }
    return kNoPort;
}

// \brief Get the connection port name array.

// Client
void GraphManager::getConnectionsAux(ConnectionManager* manager, const char** res, PortIdC portIndex)
{
    const Int* connections = manager->getConnections(portIndex);
    Int index;
    std::int32_t i;

    // Cleanup connection array
    memset(res, 0, sizeof(char*) * kConnectionNumForPort);

    for (i = 0; (i < kConnectionNumForPort) && ((index = connections[i]) != kEmpty); i++) {
        ServerPort* port = getPort(index);
        res[i] = port->m_name.data();
    }

    res[i] = nullptr;
}

// Use the state returned by ReadCurrentState and check that the state was not changed during the read operation.
// The operation is lock-free since there is no intermediate state in the write operation that could cause the
// read to loop forever.

// Client
const char** GraphManager::getConnections(PortIdC portIndex)
{
    const char** res = (const char**)malloc(sizeof(char*) * kConnectionNumForPort);
    UInt16 curIndex, nextIndex;

    if (!res)
        return nullptr;

    do {
        curIndex = getCurrentIndex();
        getConnectionsAux(readCurrentState(), res, portIndex);
        nextIndex = getCurrentIndex();
    } while (curIndex != nextIndex); // Until a coherent state has been read

    if (res[0]) { // At least one connection
        return res;
    } else { // Empty array, should return nullptr
        free(res);
        return nullptr;
    }
}

// Client
void GraphManager::getPortsAux(const char** matchingPorts, const char* portNamePattern, const char* typeNamePattern, std::uint64_t flags)
{
    // Cleanup port array
    memset(matchingPorts, 0, sizeof(char*) * m_portMax);

    std::int32_t matchCnt = 0;
    regex_t portRegex, typeRegex;

    if (portNamePattern && portNamePattern[0]) {
        if (regcomp(&portRegex, portNamePattern, REG_EXTENDED | REG_NOSUB) != 0) {
            jack_log("JackGraphManager::GetPortsAux could not compile regex for port_name_pattern '%s'", portNamePattern);
            return;
        }
    }
    if (typeNamePattern && typeNamePattern[0]) {
        if (regcomp(&typeRegex, typeNamePattern, REG_EXTENDED | REG_NOSUB) != 0) {
            jack_log("JackGraphManager::GetPortsAux could not compile regex for type_name_pattern '%s'", typeNamePattern);
            return;
        }
    }

    for (std::uint32_t i = 0; i < m_portMax; i++) {
        bool matching = true;
        ServerPort* port = getPort(i);

        if (port->isUsed()) {

            if (flags) {
                if ((port->m_flags & flags) != flags) {
                    matching = false;
                }
            }

            if (matching && portNamePattern && portNamePattern[0]) {
                if (regexec(&portRegex, port->getName(), 0, nullptr, 0)) {
                    matching = false;
                }
            }
            if (matching && typeNamePattern && typeNamePattern[0]) {
                if (regexec(&typeRegex, port->getType(), 0, nullptr, 0)) {
                    matching = false;
                }
            }

            if (matching) {
                matchingPorts[matchCnt++] = port->m_name.data();
            }
        }
    }

    matchingPorts[matchCnt] = 0;

    if (portNamePattern && portNamePattern[0]) {
        regfree(&portRegex);
    }
    if (typeNamePattern && typeNamePattern[0]) {
        regfree(&typeRegex);
    }
}

// Client
// Check that the state was not changed during the read operation.
// The operation is lock-free since there is no intermediate state in the write operation that could cause the
// read to loop forever.
const char** GraphManager::getPorts(const char* portNamePattern, const char* typeNamePattern, std::uint64_t flags)
{
    const char** res = (const char**)malloc(sizeof(char*) * m_portMax);
    UInt16 curIndex, nextIndex;

    if (!res)
        return nullptr;

    do {
        curIndex = getCurrentIndex();
        getPortsAux(res, portNamePattern, typeNamePattern, flags);
        nextIndex = getCurrentIndex();
    } while (curIndex != nextIndex); // Until a coherent state has been read

    if (res[0]) { // At least one port
        return res;
    } else {
        free(res); // Empty array, should return nullptr
        return nullptr;
    }
}

// Server
void GraphManager::save(ConnectionManager* dst)
{
    ConnectionManager* manager = writeNextStateStart();
    memcpy(dst, manager, sizeof(ConnectionManager));
    writeNextStateStop();
}

// Server
void GraphManager::restore(ConnectionManager* src)
{
    ConnectionManager* manager = writeNextStateStart();
    memcpy(manager, src, sizeof(ConnectionManager));
    writeNextStateStop();
}

} // namespace jackmini
