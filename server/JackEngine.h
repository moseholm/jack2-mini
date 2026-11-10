// Copyright (C) 2004-2008 Grame
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

#pragma once

#include "JackChannel.h"
#include "JackConstants.h"
#include "JackGraphManager.h"
#include "JackMutex.h"
#include "JackPlatformPlug.h"
#include "JackRequest.h"
#include "JackSynchro.h"
#include "JackTransportEngine.h"
#include "jackmini/error.hpp"
#include <array>
#include <cstdint>
#include <atomic>
#include <map>

namespace jackmini {

class JackClientInterface;
struct JackEngineControl;
class JackExternalClient;

// \brief Engine description.

class SERVER_EXPORT JackEngine : public JackLockAble {
    friend class JackLockedEngine;

private:
    JackGraphManager* fGraphManager;
    JackEngineControl* fEngineControl;
    char fSelfConnectMode;
    std::array<std::atomic<JackClientInterface*>, kClientNum> fClientTable;
    // Set by client open and close, cleared by the engine thread, which owns the rolling CPU statistics
    std::atomic<bool> fRollingResetPending;
    JackSynchro* fSynchroTable;
    JackServerNotifyChannel fChannel;  // To communicate between the RT thread and server
    JackProcessSync fSignal;
    // Number of graph switches so far.  Changed by the RT thread (ProcessNext) only.
    std::atomic<std::uint64_t> fSwitchCount;
    // Written by the RT thread at each switch, and by the server thread when a client is deactivated.
    std::atomic<jack_time_t> fLastSwitchUsecs;

    std::map<std::int32_t, std::string> fReservationMap;

    Result<void> ClientCloseAux(std::int32_t refnum, bool wait);
    void CheckXRun(jack_time_t callback_usecs);

    Result<void> NotifyAddClient(JackClientInterface* new_client, const char* new_name, std::int32_t refnum);
    void NotifyRemoveClient(const char* name, std::int32_t refnum);

    void ProcessNext(jack_time_t callback_usecs);
    void ProcessCurrent(jack_time_t callback_usecs);

    // Waits until fSwitchCount reaches count.  A change written to the next state is in effect after the second
    // switch that begins after the write (the first may have begun before it), so a waiter asks for two more switches.
    bool WaitForSwitches(std::uint64_t count);

    bool ClientCheckName(const char* name);
    bool GenerateUniqueName(char* name);

    Result<std::int32_t> AllocateRefnum();
    void ReleaseRefnum(std::int32_t refnum);

    Result<void> ClientNotify(JackClientInterface* client, std::int32_t refnum, const char* name, std::int32_t notify, std::int32_t sync, const char* message, std::int32_t value1, std::int32_t value2);

    void NotifyClient(std::int32_t refnum, std::int32_t event, std::int32_t sync, const char* message, std::int32_t value1, std::int32_t value2);
    void NotifyClients(std::int32_t event, std::int32_t sync, const char* message, std::int32_t value1, std::int32_t value2);

    void NotifyPortRegistation(jack_port_id_t port_index, bool onoff);
    void NotifyPortConnect(jack_port_id_t src, jack_port_id_t dst, bool onoff);
    void NotifyPortRename(jack_port_id_t src, const char* old_name);
    void NotifyActivate(std::int32_t refnum);

    void EnsureUUID(jack_uuid_t uuid);

    bool CheckClient(std::int32_t refnum)
    {
        return (refnum >= 0 && refnum < kClientNum && fClientTable[refnum] != nullptr);
    }

    Result<bool> CheckPortsConnect(std::int32_t refnum, jack_port_id_t src, jack_port_id_t dst);

public:
    JackEngine(JackGraphManager* manager, JackSynchro* table, JackEngineControl* controler, char self_connect_mode);
    ~JackEngine();

    Result<void> Open();
    Result<void> Close();

    // Client management
    Result<void> ClientCheck(const char* name, jack_uuid_t uuid, char* name_res, std::int32_t protocol, std::int32_t options, std::int32_t* status);

    Result<void> ClientExternalOpen(const char* name, std::int32_t pid, jack_uuid_t uuid, std::int32_t* ref, JackShmSegment* shared_engine, JackShmSegment* shared_client, JackShmSegment* shared_graph_manager);
    Result<void> RegisterDriver(const char* name, std::int32_t* ref, JackEngineControl** shared_engine, JackGraphManager** shared_manager, JackClientInterface* client, bool wait);

    Result<void> ClientExternalClose(std::int32_t refnum);
    Result<void> UnregisterDriver(std::int32_t refnum, bool wait);

    Result<void> ClientActivate(std::int32_t refnum, bool is_real_time, bool wait);
    Result<void> ClientDeactivate(std::int32_t refnum, bool wait);

    void ClientKill(std::int32_t refnum);

    Result<std::int32_t> GetClientPID(const char* name);
    Result<std::int32_t> GetClientRefNum(const char* name);


    // Port management
    Result<void> PortRegister(std::int32_t refnum, const char* name, const char* type, std::uint32_t flags, std::uint32_t buffer_size, jack_port_id_t* port);
    Result<void> PortUnRegister(std::int32_t refnum, jack_port_id_t port);

    Result<void> PortConnect(std::int32_t refnum, const char* src, const char* dst);
    Result<void> PortDisconnect(std::int32_t refnum, const char* src, const char* dst);

    Result<void> PortConnect(std::int32_t refnum, jack_port_id_t src, jack_port_id_t dst);
    Result<void> PortDisconnect(std::int32_t refnum, jack_port_id_t src, jack_port_id_t dst);

    Result<void> PortRename(std::int32_t refnum, jack_port_id_t port, const char* name);


    Result<void> ComputeTotalLatencies();

    // Graph
    bool Process(jack_time_t cur_cycle_begin, jack_time_t prev_cycle_end);

    // Notifications
    void NotifyDriverXRun();
    void NotifyClientXRun(std::int32_t refnum);
    void NotifyFailure(std::int32_t code, const char* reason);
    void NotifyGraphReorder();
    void NotifyBufferSize(jack_nframes_t buffer_size);
    void NotifySampleRate(jack_nframes_t sample_rate);
    void NotifySecondarySampleRate(const char* card, jack_nframes_t sample_rate, std::int32_t direction);
    void NotifyFreewheel(bool onoff);
    void NotifyQuit();

    Result<void> GetUUIDForClientName(const char* client_name, char* uuid_res);
    Result<void> GetClientNameForUUID(const char* uuid, char* name_res);
    Result<void> ReserveClientName(const char* name, const char* uuid);
};


} // namespace jackmini
