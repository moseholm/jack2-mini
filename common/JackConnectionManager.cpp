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

#include "JackConnectionManager.h"
#include "JackClientControl.h"
#include "JackEngineControl.h"
#include "JackError.h"
#include "JackGlobals.h"
#include <array>
#include <cstdint>
#include <assert.h>
#include <iostream>
#include <set>

namespace jackmini {

ConnectionManager::ConnectionManager()
{
    std::int32_t i;
    static_assert(offsetof(ConnectionManager, m_inputCounter) % sizeof(UInt32) == 0,
        "fInputCounter must be aligned within JackConnectionManager");

    jack_log("JackConnectionManager::InitConnections size = %ld ", sizeof(ConnectionManager));

    for (i = 0; i < kPortNumMax; i++) {
        m_connection[i].init();
    }

    m_loopFeedback.init();

    jack_log("JackConnectionManager::InitClients");
    for (i = 0; i < kClientNum; i++) {
        initRefNum(i);
    }
}

ConnectionManager::~ConnectionManager()
{
}

//--------------
// Internal API
//--------------

bool ConnectionManager::isLoopPathAux(std::int32_t ref1, std::int32_t ref2) const
{
    jack_log("JackConnectionManager::IsLoopPathAux ref1 = %ld ref2 = %ld", ref1, ref2);

    if (ref1 < getEngineControl()->m_driverNum || ref2 < getEngineControl()->m_driverNum) {
        return false;
    } else if (ref1 == ref2) { // Same refnum
        return true;
    } else {
        std::array<Int, kClientNum> output;
        m_connectionRef.getOutputTable(ref1, output.data());

        if (m_connectionRef.isInsideTable(ref2, output.data())) { // If ref2 is contained in the outputs of ref1
            return true;
        } else {
            for (std::int32_t i = 0; i < kClientNum && output[i] != kEmpty; i++) { // Otherwise recurse for all ref1 outputs
                if (isLoopPathAux(output[i], ref2)) {
                    return true; // Stop when a path is found
                }
            }
            return false;
        }
    }
}

//--------------
// External API
//--------------

// \brief Connect port_src to port_dst.
Result<void> ConnectionManager::connect(PortIdC portSrc, PortIdC portDst)
{
    jack_log("JackConnectionManager::Connect port_src = %ld port_dst = %ld", portSrc, portDst);

    if (m_connection[portSrc].addItem(portDst)) {
        return { };
    } else {
        jack_error("Connection table is full !!");
        return std::unexpected(Error::kServerFailure);
    }
}

// \brief Disconnect port_src from port_dst.
Result<void> ConnectionManager::disconnect(PortIdC portSrc, PortIdC portDst)
{
    jack_log("JackConnectionManager::Disconnect port_src = %ld port_dst = %ld", portSrc, portDst);

    if (m_connection[portSrc].removeItem(portDst)) {
        return { };
    } else {
        jack_error("Connection not found !!");
        return std::unexpected(Error::kNotFound);
    }
}

// \brief Check if port_src and port_dst are connected.
bool ConnectionManager::isConnected(PortIdC portSrc, PortIdC portDst) const
{
    return m_connection[portSrc].checkItem(portDst);
}

// \brief Get the connection port array.
const Int* ConnectionManager::getConnections(PortIdC portIndex) const
{
    return m_connection[portIndex].getItems();
}

//------------------------
// Client port management
//------------------------

// \brief Add an input port to a client.
Result<void> ConnectionManager::addInputPort(std::int32_t refnum, PortIdC portIndex)
{
    if (m_inputPort[refnum].addItem(portIndex)) {
        jack_log("JackConnectionManager::AddInputPort ref = %ld port = %ld", refnum, portIndex);
        return { };
    } else {
        jack_error("Maximum number of input ports is reached for application ref = %ld", refnum);
        return std::unexpected(Error::kServerFailure);
    }
}

// \brief Add an output port to a client.
Result<void> ConnectionManager::addOutputPort(std::int32_t refnum, PortIdC portIndex)
{
    if (m_outputPort[refnum].addItem(portIndex)) {
        jack_log("JackConnectionManager::AddOutputPort ref = %ld port = %ld", refnum, portIndex);
        return { };
    } else {
        jack_error("Maximum number of output ports is reached for application ref = %ld", refnum);
        return std::unexpected(Error::kServerFailure);
    }
}

// \brief Remove an input port from a client.
Result<void> ConnectionManager::removeInputPort(std::int32_t refnum, PortIdC portIndex)
{
    jack_log("JackConnectionManager::RemoveInputPort ref = %ld port_index = %ld ", refnum, portIndex);

    if (m_inputPort[refnum].removeItem(portIndex)) {
        return { };
    } else {
        jack_error("Input port index = %ld not found for application ref = %ld", portIndex, refnum);
        return std::unexpected(Error::kNotFound);
    }
}

// \brief Remove an output port from a client.
Result<void> ConnectionManager::removeOutputPort(std::int32_t refnum, PortIdC portIndex)
{
    jack_log("JackConnectionManager::RemoveOutputPort ref = %ld port_index = %ld ", refnum, portIndex);

    if (m_outputPort[refnum].removeItem(portIndex)) {
        return { };
    } else {
        jack_error("Output port index = %ld not found for application ref = %ld", portIndex, refnum);
        return std::unexpected(Error::kNotFound);
    }
}

// \brief Get the input port array of a given refnum.
const Int* ConnectionManager::getInputPorts(std::int32_t refnum)
{
    return m_inputPort[refnum].getItems();
}

// \brief Get the output port array of a given refnum.
const Int* ConnectionManager::getOutputPorts(std::int32_t refnum)
{
    return m_outputPort[refnum].getItems();
}

// \brief Init the refnum.
void ConnectionManager::initRefNum(std::int32_t refnum)
{
    m_inputPort[refnum].init();
    m_outputPort[refnum].init();
    m_connectionRef.init(refnum);
    m_inputCounter[refnum].setValue(0);
    m_inactive[refnum] = 0;
}

// \brief Reset all clients activation.
void ConnectionManager::resetGraph(ClientTiming* timing, std::atomic<SInt32>* live)
{
    // Reset activation counter : must be done *before* starting to resume clients
    for (std::int32_t i = 0; i < kClientNum; i++) {
        live[i].store(m_inputCounter[i].getCount());
        timing[i].m_status = kNotTriggered;
    }
}

// \brief Wait on the input synchro.
Result<void> ConnectionManager::suspendRefNum(ClientControl* control, Synchro* table, ClientTiming* timing, std::int64_t timeOutUsec)
{
    bool res;
    if ((res = table[control->m_refNum].timedWait(timeOutUsec))) {
        timing[control->m_refNum].m_status = kRunning;
        timing[control->m_refNum].m_awakeAt = getMicroseconds();
    }
    if (!res) {
        return std::unexpected(Error::kServerFailure);
    }
    return { };
}

// \brief Signal clients connected to the given client.
Result<void> ConnectionManager::resumeRefNum(ClientControl* control, Synchro* table, ClientTiming* timing, std::atomic<SInt32>* live)
{
    TimeC currentDate = getMicroseconds();
    const Int* outputRef = m_connectionRef.getItems(control->m_refNum);
    Result<void> res;

    // Update state and timestamp of current client
    timing[control->m_refNum].m_status = kFinished;
    timing[control->m_refNum].m_finishedAt = currentDate;

    for (std::int32_t i = 0; i < kClientNum; i++) {

        // Signal connected clients or drivers
        if (outputRef[i] > 0) {

            // Update state and timestamp of destination clients
            timing[i].m_status = kTriggered;
            timing[i].m_signaledAt = currentDate;

            if (!ActivationCount::signal(live[i], table + i, control)) {
                jack_log("JackConnectionManager::ResumeRefNum error: ref = %ld output = %ld ", control->m_refNum, i);
                res = std::unexpected(Error::kServerFailure);
            }
        }
    }

    return res;
}

static bool hasNoConnection(Int* table)
{
    for (std::int32_t ref = 0; ref < kClientNum; ref++) {
        if (table[ref] > 0)
            return false;
    }
    return true;
}

// Using http://en.wikipedia.org/wiki/Topological_sorting

void ConnectionManager::topologicalSort(std::vector<Int>& sorted)
{
    FixedMatrix<kClientNum>* tmp = new FixedMatrix<kClientNum>;
    std::set<Int> level;

    m_connectionRef.copy(*tmp);

    // Inputs of the graph
    level.insert(kAudioDriverRefnum);
    level.insert(kFreewheelDriverRefnum);

    while (level.size() > 0) {
        Int refnum = *level.begin();
        sorted.push_back(refnum);
        level.erase(level.begin());
        const Int* outputRef1 = tmp->getItems(refnum);
        for (std::int32_t dst = 0; dst < kClientNum; dst++) {
            if (outputRef1[dst] > 0) {
                tmp->clearItem(refnum, dst);
                Int outputRef2[kClientNum];
                tmp->getOutputTable1(dst, outputRef2);
                if (hasNoConnection(outputRef2)) {
                    level.insert(dst);
                }
            }
        }
    }

    delete tmp;
}

// \brief Increment the number of ports between 2 clients, if the 2 clients become connected, then the Activation counter is updated.
void ConnectionManager::incDirectConnection(PortIdC portSrc, PortIdC portDst)
{
    std::int32_t ref1 = getOutputRefNum(portSrc);
    std::int32_t ref2 = getInputRefNum(portDst);

    assert(ref1 >= 0 && ref2 >= 0);

    directConnect(ref1, ref2);
    jack_log("JackConnectionManager::IncConnectionRef: ref1 = %ld ref2 = %ld", ref1, ref2);
}

// \brief Decrement the number of ports between 2 clients, if the 2 clients become disconnected, then the Activation counter is updated.
void ConnectionManager::decDirectConnection(PortIdC portSrc, PortIdC portDst)
{
    std::int32_t ref1 = getOutputRefNum(portSrc);
    std::int32_t ref2 = getInputRefNum(portDst);

    assert(ref1 >= 0 && ref2 >= 0);

    directDisconnect(ref1, ref2);
    jack_log("JackConnectionManager::DecConnectionRef: ref1 = %ld ref2 = %ld", ref1, ref2);
}

// \brief Directly connect 2 reference numbers.
void ConnectionManager::directConnect(std::int32_t ref1, std::int32_t ref2)
{
    assert(ref1 >= 0 && ref2 >= 0);

    if (m_connectionRef.incItem(ref1, ref2) == 1) { // First connection between client ref1 and client ref2
        jack_log("JackConnectionManager::DirectConnect first: ref1 = %ld ref2 = %ld", ref1, ref2);
        if (!isInactive(ref1)) {
            m_inputCounter[ref2].incValue();
        }
    }
}

// \brief Directly disconnect 2 reference numbers.
void ConnectionManager::directDisconnect(std::int32_t ref1, std::int32_t ref2)
{
    assert(ref1 >= 0 && ref2 >= 0);

    if (m_connectionRef.decItem(ref1, ref2) == 0) { // Last connection between client ref1 and client ref2
        jack_log("JackConnectionManager::DirectDisconnect last: ref1 = %ld ref2 = %ld", ref1, ref2);
        if (!isInactive(ref1)) {
            m_inputCounter[ref2].decValue();
        }
    }
}

// \brief Deactivates or activates a client. Its connections are kept; the activation count of each client it feeds
// follows: they stop counting while the client is inactive.
void ConnectionManager::setInactive(std::int32_t refnum, bool inactive)
{
    assert(refnum >= 0 && refnum < kClientNum);
    if (isInactive(refnum) == inactive) {
        return;
    }
    const Int* outputs = m_connectionRef.getItems(refnum);
    for (std::int32_t ref = 0; ref < kClientNum; ref++) {
        if (outputs[ref] > 0) {
            if (inactive) {
                m_inputCounter[ref].decValue();
            } else {
                m_inputCounter[ref].incValue();
            }
        }
    }
    m_inactive[refnum] = inactive ? 1 : 0;
}

// \brief Returns the connections state between 2 refnum.
bool ConnectionManager::isDirectConnection(std::int32_t ref1, std::int32_t ref2) const
{
    assert(ref1 >= 0 && ref2 >= 0);
    return (m_connectionRef.getItemCount(ref1, ref2) > 0);
}

// \brief Get the client refnum of a given input port.
std::int32_t ConnectionManager::getInputRefNum(PortIdC portIndex) const
{
    for (std::int32_t i = 0; i < kClientNum; i++) {
        if (m_inputPort[i].checkItem(portIndex)) {
            return i;
        }
    }

    return -1;
}

// \brief Get the client refnum of a given output port.
std::int32_t ConnectionManager::getOutputRefNum(PortIdC portIndex) const
{
    for (std::int32_t i = 0; i < kClientNum; i++) {
        if (m_outputPort[i].checkItem(portIndex)) {
            return i;
        }
    }

    return -1;
}

// \brief Test is a connection path exists between port_src and port_dst.
bool ConnectionManager::isLoopPath(PortIdC portSrc, PortIdC portDst) const
{
    return isLoopPathAux(getInputRefNum(portDst), getOutputRefNum(portSrc));
}

bool ConnectionManager::isFeedbackConnection(PortIdC portSrc, PortIdC portDst) const
{
    return (m_loopFeedback.getConnectionIndex(getOutputRefNum(portSrc), getInputRefNum(portDst)) >= 0);
}

bool ConnectionManager::incFeedbackConnection(PortIdC portSrc, PortIdC portDst)
{
    std::int32_t ref1 = getOutputRefNum(portSrc);
    std::int32_t ref2 = getInputRefNum(portDst);

    // Add an activation connection in the other direction
    jack_log("JackConnectionManager::IncFeedbackConnection ref1 = %ld ref2 = %ld", ref1, ref2);
    assert(ref1 >= 0 && ref2 >= 0);

    if (ref1 != ref2) {
        directConnect(ref2, ref1);
    }

    return m_loopFeedback.incConnection(ref1, ref2); // Add the feedback connection
}

bool ConnectionManager::decFeedbackConnection(PortIdC portSrc, PortIdC portDst)
{
    std::int32_t ref1 = getOutputRefNum(portSrc);
    std::int32_t ref2 = getInputRefNum(portDst);

    // Remove an activation connection in the other direction
    jack_log("JackConnectionManager::DecFeedbackConnection ref1 = %ld ref2 = %ld", ref1, ref2);
    assert(ref1 >= 0 && ref2 >= 0);

    if (ref1 != ref2) {
        directDisconnect(ref2, ref1);
    }

    return m_loopFeedback.decConnection(ref1, ref2); // Remove the feedback connection
}

} // namespace jackmini
