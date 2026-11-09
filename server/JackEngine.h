/*
Copyright (C) 2004-2008 Grame

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

*/

#ifndef __JackEngine__
#define __JackEngine__

#include "JackChannel.h"
#include "JackConstants.h"
#include "JackGraphManager.h"
#include "JackMutex.h"
#include "JackPlatformPlug.h"
#include "JackRequest.h"
#include "JackSynchro.h"
#include "JackTransportEngine.h"
#include "jackmini/error.hpp"
#include <atomic>
#include <map>

namespace jackmini {

class JackClientInterface;
struct JackEngineControl;
class JackExternalClient;

/*!
\brief Engine description.
*/

class SERVER_EXPORT JackEngine : public JackLockAble {
    friend class JackLockedEngine;

private:
    JackGraphManager* fGraphManager;
    JackEngineControl* fEngineControl;
    char fSelfConnectMode;
    std::atomic<JackClientInterface*> fClientTable[CLIENT_NUM];
    // Set by client open and close, cleared by the engine thread, which owns the rolling CPU statistics
    std::atomic<bool> fRollingResetPending;
    JackSynchro* fSynchroTable;
    JackServerNotifyChannel fChannel; /*! To communicate between the RT thread and server */
    JackProcessSync fSignal;
    // Number of graph switches so far.  Changed by the RT thread (ProcessNext) only.
    std::atomic<unsigned long> fSwitchCount;
    // Written by the RT thread at each switch, and by the server thread when a client is deactivated.
    std::atomic<jack_time_t> fLastSwitchUsecs;

    std::map<int, std::string> fReservationMap;

    Result<void> ClientCloseAux(int refnum, bool wait);
    void CheckXRun(jack_time_t callback_usecs);

    Result<void> NotifyAddClient(JackClientInterface* new_client, const char* new_name, int refnum);
    void NotifyRemoveClient(const char* name, int refnum);

    void ProcessNext(jack_time_t callback_usecs);
    void ProcessCurrent(jack_time_t callback_usecs);

    // Waits until fSwitchCount reaches count.  A change written to the next state is in effect after the second
    // switch that begins after the write (the first may have begun before it), so a waiter asks for two more switches.
    bool WaitForSwitches(unsigned long count);

    bool ClientCheckName(const char* name);
    bool GenerateUniqueName(char* name);

    Result<int> AllocateRefnum();
    void ReleaseRefnum(int refnum);

    Result<void> ClientNotify(JackClientInterface* client, int refnum, const char* name, int notify, int sync, const char* message, int value1, int value2);

    void NotifyClient(int refnum, int event, int sync, const char* message, int value1, int value2);
    void NotifyClients(int event, int sync, const char* message, int value1, int value2);

    void NotifyPortRegistation(jack_port_id_t port_index, bool onoff);
    void NotifyPortConnect(jack_port_id_t src, jack_port_id_t dst, bool onoff);
    void NotifyPortRename(jack_port_id_t src, const char* old_name);
    void NotifyActivate(int refnum);

    void EnsureUUID(jack_uuid_t uuid);

    bool CheckClient(int refnum)
    {
        return (refnum >= 0 && refnum < CLIENT_NUM && fClientTable[refnum] != NULL);
    }

    Result<bool> CheckPortsConnect(int refnum, jack_port_id_t src, jack_port_id_t dst);

public:
    JackEngine(JackGraphManager* manager, JackSynchro* table, JackEngineControl* controler, char self_connect_mode);
    ~JackEngine();

    Result<void> Open();
    Result<void> Close();

    // Client management
    Result<void> ClientCheck(const char* name, jack_uuid_t uuid, char* name_res, int protocol, int options, int* status);

    Result<void> ClientExternalOpen(const char* name, int pid, jack_uuid_t uuid, int* ref, JackShmSegment* shared_engine, JackShmSegment* shared_client, JackShmSegment* shared_graph_manager);
    Result<void> RegisterDriver(const char* name, int* ref, JackEngineControl** shared_engine, JackGraphManager** shared_manager, JackClientInterface* client, bool wait);

    Result<void> ClientExternalClose(int refnum);
    Result<void> UnregisterDriver(int refnum, bool wait);

    Result<void> ClientActivate(int refnum, bool is_real_time, bool wait);
    Result<void> ClientDeactivate(int refnum, bool wait);

    void ClientKill(int refnum);

    Result<int> GetClientPID(const char* name);
    Result<int> GetClientRefNum(const char* name);


    // Port management
    Result<void> PortRegister(int refnum, const char* name, const char* type, unsigned int flags, unsigned int buffer_size, jack_port_id_t* port);
    Result<void> PortUnRegister(int refnum, jack_port_id_t port);

    Result<void> PortConnect(int refnum, const char* src, const char* dst);
    Result<void> PortDisconnect(int refnum, const char* src, const char* dst);

    Result<void> PortConnect(int refnum, jack_port_id_t src, jack_port_id_t dst);
    Result<void> PortDisconnect(int refnum, jack_port_id_t src, jack_port_id_t dst);

    Result<void> PortRename(int refnum, jack_port_id_t port, const char* name);


    Result<void> ComputeTotalLatencies();

    // Graph
    bool Process(jack_time_t cur_cycle_begin, jack_time_t prev_cycle_end);

    // Notifications
    void NotifyDriverXRun();
    void NotifyClientXRun(int refnum);
    void NotifyFailure(int code, const char* reason);
    void NotifyGraphReorder();
    void NotifyBufferSize(jack_nframes_t buffer_size);
    void NotifySampleRate(jack_nframes_t sample_rate);
    void NotifySecondarySampleRate(const char* card, jack_nframes_t sample_rate, int direction);
    void NotifyFreewheel(bool onoff);
    void NotifyQuit();

    Result<void> GetUUIDForClientName(const char* client_name, char* uuid_res);
    Result<void> GetClientNameForUUID(const char* uuid, char* name_res);
    Result<void> ReserveClientName(const char* name, const char* uuid);
};


} // namespace jackmini

#endif
