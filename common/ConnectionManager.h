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

#include "ActivationCount.h"
#include "CompilerDeps.h"
#include "Constants.h"
#include "Error.h"
#include "jackmini/Error.hpp"
#include <assert.h>
#include <array>
#include <cstdint>
#include <vector>

namespace jackmini {

struct ClientControl;

// \brief Utility class.


template <std::int32_t SIZE>
class FixedArray {

private:
    Int m_table[SIZE];
    std::uint32_t m_counter;

public:
    FixedArray()
    {
        init();
    }

    void init()
    {
        for (std::int32_t i = 0; i < SIZE; i++)
            m_table[i] = kEmpty;
        m_counter = 0;
    }

    bool addItem(Int index)
    {
        for (std::int32_t i = 0; i < SIZE; i++) {
            if (m_table[i] == kEmpty) {
                m_table[i] = index;
                m_counter++;
                return true;
            }
        }
        return false;
    }

    bool removeItem(Int index)
    {
        for (std::int32_t i = 0; i < SIZE; i++) {
            if (m_table[i] == index) {
                m_counter--;
                // Shift all indexes
                if (i == SIZE - 1) {
                    m_table[i] = kEmpty;
                } else {
                    std::int32_t j;
                    for (j = i; j <= SIZE - 2 && m_table[j] != kEmpty; j++) {
                        m_table[j] = m_table[j + 1];
                    }
                    m_table[j] = kEmpty;
                }
                return true;
            }
        }
        return false;
    }

    Int getItem(Int index) const
    {
        return (index < SIZE) ? m_table[index] : kEmpty;
    }

    const Int* getItems() const
    {
        return m_table;
    }

    bool checkItem(Int index) const
    {
        for (std::int32_t i = 0; i < SIZE && m_table[i] != kEmpty; i++) {
            if (m_table[i] == index)
                return true;
        }
        return false;
    }

    std::uint32_t getItemCount() const
    {
        return m_counter;
    }
};

// \brief Utility class.


template <std::int32_t SIZE>
class FixedArray1 : public FixedArray<SIZE> {
private:
    bool m_used;

public:
    FixedArray1()
    {
        init();
    }

    void init()
    {
        FixedArray<SIZE>::init();
        m_used = false;
    }

    bool isAvailable()
    {
        if (m_used) {
            return false;
        } else {
            m_used = true;
            return true;
        }
    }
};

// \brief Utility class.


template <std::int32_t SIZE>
class FixedMatrix {
private:
    Int m_table[SIZE][SIZE];

public:
    FixedMatrix()
    {
    }

    void init(Int index)
    {
        for (std::int32_t i = 0; i < SIZE; i++) {
            m_table[index][i] = 0;
            m_table[i][index] = 0;
        }
    }

    const Int* getItems(Int index) const
    {
        return m_table[index];
    }

    Int incItem(Int index1, Int index2)
    {
        m_table[index1][index2]++;
        return m_table[index1][index2];
    }

    Int decItem(Int index1, Int index2)
    {
        m_table[index1][index2]--;
        return m_table[index1][index2];
    }

    Int getItemCount(Int index1, Int index2) const
    {
        return m_table[index1][index2];
    }

    void clearItem(Int index1, Int index2)
    {
        m_table[index1][index2] = 0;
    }

    // \brief Get the output indexes of a given index.
    void getOutputTable(Int index, Int* output) const
    {
        std::int32_t i, j;

        for (i = 0; i < SIZE; i++)
            output[i] = kEmpty;

        for (i = 0, j = 0; i < SIZE; i++) {
            if (m_table[index][i] > 0) {
                output[j] = i;
                j++;
            }
        }
    }

    void getOutputTable1(Int index, Int* output) const
    {
        for (std::int32_t i = 0; i < SIZE; i++) {
            output[i] = m_table[i][index];
        }
    }

    bool isInsideTable(Int index, Int* output) const
    {
        for (std::int32_t i = 0; i < SIZE && output[i] != kEmpty; i++) {
            if (output[i] == index)
                return true;
        }
        return false;
    }

    void copy(FixedMatrix& copy)
    {
        for (std::int32_t i = 0; i < SIZE; i++) {
            memcpy(copy.m_table[i], m_table[i], sizeof(Int) * SIZE);
        }
    }
};

// \brief Utility class.


template <std::int32_t SIZE>
class LoopFeedback {
private:
    std::int32_t m_table[SIZE][3];

    // \brief Add a feedback connection between 2 refnum.
    bool addConnectionAux(std::int32_t ref1, std::int32_t ref2)
    {
        for (std::int32_t i = 0; i < SIZE; i++) {
            if (m_table[i][0] == kEmpty) {
                m_table[i][0] = ref1;
                m_table[i][1] = ref2;
                m_table[i][2] = 1;
                jack_log("JackLoopFeedback::AddConnectionAux ref1 = %ld ref2 = %ld", ref1, ref2);
                return true;
            }
        }
        jack_error("Feedback table is full !!\n");
        return false;
    }

    // \brief Remove a feedback connection between 2 refnum.
    bool removeConnectionAux(std::int32_t ref1, std::int32_t ref2)
    {
        for (std::int32_t i = 0; i < SIZE; i++) {
            if (m_table[i][0] == ref1 && m_table[i][1] == ref2) {
                m_table[i][0] = kEmpty;
                m_table[i][1] = kEmpty;
                m_table[i][2] = 0;
                jack_log("JackLoopFeedback::RemoveConnectionAux ref1 = %ld ref2 = %ld", ref1, ref2);
                return true;
            }
        }
        jack_error("Feedback connection not found\n");
        return false;
    }

    std::int32_t incConnection(std::int32_t index)
    {
        m_table[index][2]++;
        return m_table[index][2];
    }

    std::int32_t decConnection(std::int32_t index)
    {
        m_table[index][2]--;
        return m_table[index][2];
    }

public:
    LoopFeedback()
    {
        init();
    }

    void init()
    {
        for (std::int32_t i = 0; i < SIZE; i++) {
            m_table[i][0] = kEmpty;
            m_table[i][1] = kEmpty;
            m_table[i][2] = 0;
        }
    }

    bool incConnection(std::int32_t ref1, std::int32_t ref2)
    {
        std::int32_t index = getConnectionIndex(ref1, ref2);

        if (index >= 0) { // Feedback connection is already added, increment counter
            incConnection(index);
            return true;
        } else {
            return addConnectionAux(ref1, ref2); // Add the feedback connection
        }
    }

    bool decConnection(std::int32_t ref1, std::int32_t ref2)
    {
        std::int32_t index = getConnectionIndex(ref1, ref2);

        if (index >= 0) {
            jack_log("JackLoopFeedback::DecConnection ref1 = %ld ref2 = %ld index = %ld", ref1, ref2, index);
            return (decConnection(index) == 0) ? removeConnectionAux(ref1, ref2) : true;
        } else {
            return false;
        }
    }

    // \brief Test if a connection between 2 refnum is a feedback connection.
    std::int32_t getConnectionIndex(std::int32_t ref1, std::int32_t ref2) const
    {
        for (std::int32_t i = 0; i < SIZE; i++) {
            if (m_table[i][0] == ref1 && m_table[i][1] == ref2)
                return i;
        }
        return -1;
    }
};

// \brief For client timing measurements.


struct ClientTiming {
    TimeC m_signaledAt;
    TimeC m_awakeAt;
    TimeC m_finishedAt;
    ClientState m_status;

    ClientTiming()
    {
        init();
    }
    ~ClientTiming()
    {
    }

    void init()
    {
        m_signaledAt = 0;
        m_awakeAt = 0;
        m_finishedAt = 0;
        m_status = kNotTriggered;
    }
};

// \brief Connection manager.
//
// <UL>
// <LI>The <B>fConnection</B> array contains the list (array line) of connected ports for a given port.
// <LI>The <B>fInputPort</B> array contains the list (array line) of input connected  ports for a given client.
// <LI>The <B>fOutputPort</B> array contains the list (array line) of output connected  ports for a given client.
// <LI>The <B>fConnectionRef</B> array contains the number of ports connected between two clients.
// <LI>The <B>fInputCounter</B> array contains the number of input clients connected to a given for activation purpose.
// </UL>


class SERVER_EXPORT ConnectionManager {

private:
    std::array<FixedArray<kConnectionNumForPort>, kPortNumMax> m_connection; // Connection matrix: list of connected ports for a given port: needed to compute Mix buffer
    std::array<FixedArray1<kPortNumForClient>, kClientNum> m_inputPort; // Table of input port per refnum : to find a refnum for a given port
    std::array<FixedArray<kPortNumForClient>, kClientNum> m_outputPort; // Table of output port per refnum : to find a refnum for a given port
    FixedMatrix<kClientNum> m_connectionRef; // Table of port connections by (refnum , refnum)
    alignas(UInt32) alignas(ActivationCount) std::array<ActivationCount, kClientNum> m_inputCounter; // Activation counter per refnum
    std::array<UInt32, kClientNum> m_inactive; // 1 for a deactivated client: it keeps its connections, which do not count toward the clients they feed
    LoopFeedback<kConnectionNumForPort> m_loopFeedback; // Loop feedback connections

    bool isLoopPathAux(std::int32_t ref1, std::int32_t ref2) const;

public:
    ConnectionManager();
    ~ConnectionManager();

    // Connections management
    Result<void> connect(PortIdC portSrc, PortIdC portDst);
    Result<void> disconnect(PortIdC portSrc, PortIdC portDst);
    bool isConnected(PortIdC portSrc, PortIdC portDst) const;

    // \brief Get the connection number of a given port.
    Int connections(PortIdC portIndex) const
    {
        return m_connection[portIndex].getItemCount();
    }

    PortIdC getPort(PortIdC portIndex, std::int32_t connection) const
    {
        assert(connection < kConnectionNumForPort);
        return (PortIdC)m_connection[portIndex].getItem(connection);
    }

    const Int* getConnections(PortIdC portIndex) const;

    bool incFeedbackConnection(PortIdC portSrc, PortIdC portDst);
    bool decFeedbackConnection(PortIdC portSrc, PortIdC portDst);
    bool isFeedbackConnection(PortIdC portSrc, PortIdC portDst) const;

    bool isLoopPath(PortIdC portSrc, PortIdC portDst) const;
    void incDirectConnection(PortIdC portSrc, PortIdC portDst);
    void decDirectConnection(PortIdC portSrc, PortIdC portDst);

    // Ports management
    Result<void> addInputPort(std::int32_t refnum, PortIdC portIndex);
    Result<void> addOutputPort(std::int32_t refnum, PortIdC portIndex);

    Result<void> removeInputPort(std::int32_t refnum, PortIdC portIndex);
    Result<void> removeOutputPort(std::int32_t refnum, PortIdC portIndex);

    const Int* getInputPorts(std::int32_t refnum);
    const Int* getOutputPorts(std::int32_t refnum);

    // Client management
    void initRefNum(std::int32_t refnum);
    std::int32_t getInputRefNum(PortIdC portIndex) const;
    std::int32_t getOutputRefNum(PortIdC portIndex) const;

    // Connect/Disconnect 2 refnum "directly"
    bool isDirectConnection(std::int32_t ref1, std::int32_t ref2) const;
    void directConnect(std::int32_t ref1, std::int32_t ref2);
    void directDisconnect(std::int32_t ref1, std::int32_t ref2);

    // A deactivated client keeps its connections, but they stop counting toward the clients they feed, and its
    // ports are silent (protocol.md section 6).
    void setInactive(std::int32_t refnum, bool inactive);
    // A port with no owner has been released; the RT thread can still see it in the connections of the current
    // state, so it counts as inactive (silent) rather than indexing the table with -1.
    bool isInactive(std::int32_t refnum) const
    {
        return refnum < 0 || m_inactive[refnum] != 0;
    }


    // Graph
    // The live activation counters are passed in: they are not part of this (copied) state.
    void resetGraph(ClientTiming* timing, std::atomic<SInt32>* live);
    Result<void> resumeRefNum(ClientControl* control, Synchro* table, ClientTiming* timing, std::atomic<SInt32>* live);
    Result<void> suspendRefNum(ClientControl* control, Synchro* table, ClientTiming* timing, std::int64_t timeOutUsec);
    void topologicalSort(std::vector<Int>& sorted);
};

} // namespace jackmini
