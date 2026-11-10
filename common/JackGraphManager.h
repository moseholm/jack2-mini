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


class SERVER_EXPORT JackGraphManager : public JackShmMem, public JackAtomicState<JackConnectionManager> {

private:
    std::uint32_t fPortMax;
    std::array<JackClientTiming, kClientNum> fClientTiming;
    std::array<std::atomic<SInt32>, kClientNum> fActivation; // live activation counters, outside the copied connection state
    std::array<jack_uuid_t, kClientNum> fClientUUID; // the UUID of each client, for the ports it owns
    JackPort fPortArray[0]; // The actual size depends of port_max, it will be dynamically computed and allocated using "placement" new

    void AssertPort(jack_port_id_t port_index);
    jack_port_id_t AllocatePortAux(std::int32_t refnum, const char* port_name, const char* port_type, JackPortFlags flags);
    void GetConnectionsAux(JackConnectionManager* manager, const char** res, jack_port_id_t port_index);
    void GetPortsAux(const char** matching_ports, const char* port_name_pattern, const char* type_name_pattern, std::uint64_t flags);
    jack_default_audio_sample_t* GetBuffer(jack_port_id_t port_index);
    void* GetBufferAux(JackConnectionManager* manager, jack_port_id_t port_index, jack_nframes_t frames);
    jack_nframes_t ComputeTotalLatencyAux(jack_port_id_t port_index, jack_port_id_t src_port_index, JackConnectionManager* manager, std::int32_t hop_count);
    void RecalculateLatencyAux(jack_port_id_t port_index, jack_latency_callback_mode_t mode);

public:
    JackGraphManager(std::int32_t port_max);
    ~JackGraphManager()
    {
    }

    void SetBufferSize(jack_nframes_t buffer_size);

    // Ports management
    jack_port_id_t AllocatePort(std::int32_t refnum, const char* port_name, const char* port_type, JackPortFlags flags, jack_nframes_t buffer_size);
    Result<void> ReleasePort(std::int32_t refnum, jack_port_id_t port_index);
    void GetInputPorts(std::int32_t refnum, jack_int_t* res);
    void GetOutputPorts(std::int32_t refnum, jack_int_t* res);
    void RemoveAllPorts(std::int32_t refnum);
    void DisconnectAllPorts(std::int32_t refnum);

    JackPort* GetPort(jack_port_id_t index);
    // Read-only: the ports in use that belong to refnum, found in the port table.
    void GetOwnedPorts(std::int32_t refnum, std::vector<jack_port_id_t>& ports);
    jack_port_id_t GetPort(const char* name);

    void ComputeTotalLatency(jack_port_id_t port_index);
    void ComputeTotalLatencies();
    void RecalculateLatency(jack_port_id_t port_index, jack_latency_callback_mode_t mode);

    void RequestMonitor(jack_port_id_t port_index, bool onoff);

    // Connections management
    Result<void> Connect(jack_port_id_t src_index, jack_port_id_t dst_index);
    Result<void> Disconnect(jack_port_id_t src_index, jack_port_id_t dst_index);
    bool IsConnected(jack_port_id_t port_src, jack_port_id_t port_dst);

    // RT, client
    std::int32_t GetConnectionsNum(jack_port_id_t port_index)
    {
        JackConnectionManager* manager = ReadCurrentState();
        return manager->Connections(port_index);
    }

    const char** GetConnections(jack_port_id_t port_index);
    void GetConnections(jack_port_id_t port_index, jack_int_t* connections); // TODO
    const char** GetPorts(const char* port_name_pattern, const char* type_name_pattern, std::uint64_t flags);

    Result<void> GetTwoPorts(const char* src, const char* dst, jack_port_id_t* src_index, jack_port_id_t* dst_index);
    Result<void> CheckPorts(jack_port_id_t port_src, jack_port_id_t port_dst);

    void DisconnectAllInput(jack_port_id_t port_index);
    void DisconnectAllOutput(jack_port_id_t port_index);
    void DisconnectAll(jack_port_id_t port_index);

    bool IsDirectConnection(std::int32_t ref1, std::int32_t ref2);
    void DirectConnect(std::int32_t ref1, std::int32_t ref2);
    void DirectDisconnect(std::int32_t ref1, std::int32_t ref2);

    void Activate(std::int32_t refnum);
    void Deactivate(std::int32_t refnum);

    std::int32_t GetInputRefNum(jack_port_id_t port_index);
    std::int32_t GetOutputRefNum(jack_port_id_t port_index);

    // Buffer management
    void* GetBuffer(jack_port_id_t port_index, jack_nframes_t frames);

    // Activation management
    void RunCurrentGraph();
    bool RunNextGraph();
    bool IsFinishedGraph();

    void InitRefNum(std::int32_t refnum);
    Result<void> ResumeRefNum(JackClientControl* control, JackSynchro* table);
    Result<void> SuspendRefNum(JackClientControl* control, JackSynchro* table, std::int64_t usecs);
    void TopologicalSort(std::vector<jack_int_t>& sorted);

    // Read-only, for the client tools: the port table size, and the UUID of the client that owns a port.
    std::uint32_t PortMax() const
    {
        return fPortMax;
    }
    void SetClientUUID(std::int32_t refnum, jack_uuid_t uuid);
    jack_uuid_t GetClientUUID(std::int32_t refnum) const
    {
        return fClientUUID[refnum];
    }

    JackClientTiming* GetClientTiming(std::int32_t refnum)
    {
        return &fClientTiming[refnum];
    }

    void Save(JackConnectionManager* dst);
    void Restore(JackConnectionManager* src);

    static JackGraphManager* Allocate(std::int32_t port_max);
    static void Destroy(JackGraphManager* manager);
};


} // namespace jackmini
