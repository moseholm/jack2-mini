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

#include "JackActivationCount.h"
#include "JackCompilerDeps.h"
#include "JackConstants.h"
#include "JackError.h"
#include "jackmini/error.hpp"
#include <cstdint>
#include <assert.h>
#include <vector>

namespace jackmini {

struct JackClientControl;

// \brief Utility class.


template <std::int32_t SIZE>
class JackFixedArray {

private:
    jack_int_t fTable[SIZE];
    std::uint32_t fCounter;

public:
    JackFixedArray()
    {
        Init();
    }

    void Init()
    {
        for (std::int32_t i = 0; i < SIZE; i++)
            fTable[i] = EMPTY;
        fCounter = 0;
    }

    bool AddItem(jack_int_t index)
    {
        for (std::int32_t i = 0; i < SIZE; i++) {
            if (fTable[i] == EMPTY) {
                fTable[i] = index;
                fCounter++;
                return true;
            }
        }
        return false;
    }

    bool RemoveItem(jack_int_t index)
    {
        for (std::int32_t i = 0; i < SIZE; i++) {
            if (fTable[i] == index) {
                fCounter--;
                // Shift all indexes
                if (i == SIZE - 1) {
                    fTable[i] = EMPTY;
                } else {
                    std::int32_t j;
                    for (j = i; j <= SIZE - 2 && fTable[j] != EMPTY; j++) {
                        fTable[j] = fTable[j + 1];
                    }
                    fTable[j] = EMPTY;
                }
                return true;
            }
        }
        return false;
    }

    jack_int_t GetItem(jack_int_t index) const
    {
        return (index < SIZE) ? fTable[index] : EMPTY;
    }

    const jack_int_t* GetItems() const
    {
        return fTable;
    }

    bool CheckItem(jack_int_t index) const
    {
        for (std::int32_t i = 0; i < SIZE && fTable[i] != EMPTY; i++) {
            if (fTable[i] == index)
                return true;
        }
        return false;
    }

    std::uint32_t GetItemCount() const
    {
        return fCounter;
    }
};

// \brief Utility class.


template <std::int32_t SIZE>
class JackFixedArray1 : public JackFixedArray<SIZE> {
private:
    bool fUsed;

public:
    JackFixedArray1()
    {
        Init();
    }

    void Init()
    {
        JackFixedArray<SIZE>::Init();
        fUsed = false;
    }

    bool IsAvailable()
    {
        if (fUsed) {
            return false;
        } else {
            fUsed = true;
            return true;
        }
    }
};

// \brief Utility class.


template <std::int32_t SIZE>
class JackFixedMatrix {
private:
    jack_int_t fTable[SIZE][SIZE];

public:
    JackFixedMatrix()
    {
    }

    void Init(jack_int_t index)
    {
        for (std::int32_t i = 0; i < SIZE; i++) {
            fTable[index][i] = 0;
            fTable[i][index] = 0;
        }
    }

    const jack_int_t* GetItems(jack_int_t index) const
    {
        return fTable[index];
    }

    jack_int_t IncItem(jack_int_t index1, jack_int_t index2)
    {
        fTable[index1][index2]++;
        return fTable[index1][index2];
    }

    jack_int_t DecItem(jack_int_t index1, jack_int_t index2)
    {
        fTable[index1][index2]--;
        return fTable[index1][index2];
    }

    jack_int_t GetItemCount(jack_int_t index1, jack_int_t index2) const
    {
        return fTable[index1][index2];
    }

    void ClearItem(jack_int_t index1, jack_int_t index2)
    {
        fTable[index1][index2] = 0;
    }

    // \brief Get the output indexes of a given index.
    void GetOutputTable(jack_int_t index, jack_int_t* output) const
    {
        std::int32_t i, j;

        for (i = 0; i < SIZE; i++)
            output[i] = EMPTY;

        for (i = 0, j = 0; i < SIZE; i++) {
            if (fTable[index][i] > 0) {
                output[j] = i;
                j++;
            }
        }
    }

    void GetOutputTable1(jack_int_t index, jack_int_t* output) const
    {
        for (std::int32_t i = 0; i < SIZE; i++) {
            output[i] = fTable[i][index];
        }
    }

    bool IsInsideTable(jack_int_t index, jack_int_t* output) const
    {
        for (std::int32_t i = 0; i < SIZE && output[i] != EMPTY; i++) {
            if (output[i] == index)
                return true;
        }
        return false;
    }

    void Copy(JackFixedMatrix& copy)
    {
        for (std::int32_t i = 0; i < SIZE; i++) {
            memcpy(copy.fTable[i], fTable[i], sizeof(jack_int_t) * SIZE);
        }
    }
};

// \brief Utility class.


template <std::int32_t SIZE>
class JackLoopFeedback {
private:
    std::int32_t fTable[SIZE][3];

    // \brief Add a feedback connection between 2 refnum.
    bool AddConnectionAux(std::int32_t ref1, std::int32_t ref2)
    {
        for (std::int32_t i = 0; i < SIZE; i++) {
            if (fTable[i][0] == EMPTY) {
                fTable[i][0] = ref1;
                fTable[i][1] = ref2;
                fTable[i][2] = 1;
                jack_log("JackLoopFeedback::AddConnectionAux ref1 = %ld ref2 = %ld", ref1, ref2);
                return true;
            }
        }
        jack_error("Feedback table is full !!\n");
        return false;
    }

    // \brief Remove a feedback connection between 2 refnum.
    bool RemoveConnectionAux(std::int32_t ref1, std::int32_t ref2)
    {
        for (std::int32_t i = 0; i < SIZE; i++) {
            if (fTable[i][0] == ref1 && fTable[i][1] == ref2) {
                fTable[i][0] = EMPTY;
                fTable[i][1] = EMPTY;
                fTable[i][2] = 0;
                jack_log("JackLoopFeedback::RemoveConnectionAux ref1 = %ld ref2 = %ld", ref1, ref2);
                return true;
            }
        }
        jack_error("Feedback connection not found\n");
        return false;
    }

    std::int32_t IncConnection(std::int32_t index)
    {
        fTable[index][2]++;
        return fTable[index][2];
    }

    std::int32_t DecConnection(std::int32_t index)
    {
        fTable[index][2]--;
        return fTable[index][2];
    }

public:
    JackLoopFeedback()
    {
        Init();
    }

    void Init()
    {
        for (std::int32_t i = 0; i < SIZE; i++) {
            fTable[i][0] = EMPTY;
            fTable[i][1] = EMPTY;
            fTable[i][2] = 0;
        }
    }

    bool IncConnection(std::int32_t ref1, std::int32_t ref2)
    {
        std::int32_t index = GetConnectionIndex(ref1, ref2);

        if (index >= 0) { // Feedback connection is already added, increment counter
            IncConnection(index);
            return true;
        } else {
            return AddConnectionAux(ref1, ref2); // Add the feedback connection
        }
    }

    bool DecConnection(std::int32_t ref1, std::int32_t ref2)
    {
        std::int32_t index = GetConnectionIndex(ref1, ref2);

        if (index >= 0) {
            jack_log("JackLoopFeedback::DecConnection ref1 = %ld ref2 = %ld index = %ld", ref1, ref2, index);
            return (DecConnection(index) == 0) ? RemoveConnectionAux(ref1, ref2) : true;
        } else {
            return false;
        }
    }

    // \brief Test if a connection between 2 refnum is a feedback connection.
    std::int32_t GetConnectionIndex(std::int32_t ref1, std::int32_t ref2) const
    {
        for (std::int32_t i = 0; i < SIZE; i++) {
            if (fTable[i][0] == ref1 && fTable[i][1] == ref2)
                return i;
        }
        return -1;
    }
};

// \brief For client timing measurements.


struct JackClientTiming {
    jack_time_t fSignaledAt;
    jack_time_t fAwakeAt;
    jack_time_t fFinishedAt;
    jack_client_state_t fStatus;

    JackClientTiming()
    {
        Init();
    }
    ~JackClientTiming()
    {
    }

    void Init()
    {
        fSignaledAt = 0;
        fAwakeAt = 0;
        fFinishedAt = 0;
        fStatus = NotTriggered;
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


class SERVER_EXPORT JackConnectionManager {

private:
    JackFixedArray<CONNECTION_NUM_FOR_PORT> fConnection[PORT_NUM_MAX];  // Connection matrix: list of connected ports for a given port: needed to compute Mix buffer
    JackFixedArray1<PORT_NUM_FOR_CLIENT> fInputPort[CLIENT_NUM];  // Table of input port per refnum : to find a refnum for a given port
    JackFixedArray<PORT_NUM_FOR_CLIENT> fOutputPort[CLIENT_NUM];  // Table of output port per refnum : to find a refnum for a given port
    JackFixedMatrix<CLIENT_NUM> fConnectionRef;  // Table of port connections by (refnum , refnum)
    alignas(UInt32) alignas(JackActivationCount) JackActivationCount fInputCounter[CLIENT_NUM];  // Activation counter per refnum
    UInt32 fInactive[CLIENT_NUM];  // 1 for a deactivated client: it keeps its connections, which do not count toward the clients they feed
    JackLoopFeedback<CONNECTION_NUM_FOR_PORT> fLoopFeedback;  // Loop feedback connections

    bool IsLoopPathAux(std::int32_t ref1, std::int32_t ref2) const;

public:
    JackConnectionManager();
    ~JackConnectionManager();

    // Connections management
    Result<void> Connect(jack_port_id_t port_src, jack_port_id_t port_dst);
    Result<void> Disconnect(jack_port_id_t port_src, jack_port_id_t port_dst);
    bool IsConnected(jack_port_id_t port_src, jack_port_id_t port_dst) const;

    // \brief Get the connection number of a given port.
    jack_int_t Connections(jack_port_id_t port_index) const
    {
        return fConnection[port_index].GetItemCount();
    }

    jack_port_id_t GetPort(jack_port_id_t port_index, std::int32_t connection) const
    {
        assert(connection < CONNECTION_NUM_FOR_PORT);
        return (jack_port_id_t)fConnection[port_index].GetItem(connection);
    }

    const jack_int_t* GetConnections(jack_port_id_t port_index) const;

    bool IncFeedbackConnection(jack_port_id_t port_src, jack_port_id_t port_dst);
    bool DecFeedbackConnection(jack_port_id_t port_src, jack_port_id_t port_dst);
    bool IsFeedbackConnection(jack_port_id_t port_src, jack_port_id_t port_dst) const;

    bool IsLoopPath(jack_port_id_t port_src, jack_port_id_t port_dst) const;
    void IncDirectConnection(jack_port_id_t port_src, jack_port_id_t port_dst);
    void DecDirectConnection(jack_port_id_t port_src, jack_port_id_t port_dst);

    // Ports management
    Result<void> AddInputPort(std::int32_t refnum, jack_port_id_t port_index);
    Result<void> AddOutputPort(std::int32_t refnum, jack_port_id_t port_index);

    Result<void> RemoveInputPort(std::int32_t refnum, jack_port_id_t port_index);
    Result<void> RemoveOutputPort(std::int32_t refnum, jack_port_id_t port_index);

    const jack_int_t* GetInputPorts(std::int32_t refnum);
    const jack_int_t* GetOutputPorts(std::int32_t refnum);

    // Client management
    void InitRefNum(std::int32_t refnum);
    std::int32_t GetInputRefNum(jack_port_id_t port_index) const;
    std::int32_t GetOutputRefNum(jack_port_id_t port_index) const;

    // Connect/Disconnect 2 refnum "directly"
    bool IsDirectConnection(std::int32_t ref1, std::int32_t ref2) const;
    void DirectConnect(std::int32_t ref1, std::int32_t ref2);
    void DirectDisconnect(std::int32_t ref1, std::int32_t ref2);

    // A deactivated client keeps its connections, but they stop counting toward the clients they feed, and its
    // ports are silent (protocol.md section 6).
    void SetInactive(std::int32_t refnum, bool inactive);
    // A port with no owner has been released; the RT thread can still see it in the connections of the current
    // state, so it counts as inactive (silent) rather than indexing the table with -1.
    bool IsInactive(std::int32_t refnum) const
    {
        return refnum < 0 || fInactive[refnum] != 0;
    }


    // Graph
    // The live activation counters are passed in: they are not part of this (copied) state.
    void ResetGraph(JackClientTiming* timing, std::atomic<SInt32>* live);
    Result<void> ResumeRefNum(JackClientControl* control, JackSynchro* table, JackClientTiming* timing, std::atomic<SInt32>* live);
    Result<void> SuspendRefNum(JackClientControl* control, JackSynchro* table, JackClientTiming* timing, std::int64_t time_out_usec);
    void TopologicalSort(std::vector<jack_int_t>& sorted);
};

} // namespace jackmini
