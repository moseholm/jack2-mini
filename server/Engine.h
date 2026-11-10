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

#include "Channel.h"
#include "Constants.h"
#include "GraphManager.h"
#include "Mutex.h"
#include "PlatformPlug.h"
#include "Request.h"
#include "Synchro.h"
#include "TransportEngine.h"
#include "jackmini/Error.hpp"
#include <array>
#include <atomic>
#include <cstdint>
#include <map>

namespace jackmini {

class ClientInterface;
struct EngineControl;
class ExternalClient;

// \brief Engine description.

class SERVER_EXPORT Engine : public LockAble {
    friend class LockedEngine;

private:
    GraphManager* m_graphManager;
    EngineControl* m_engineControl;
    char m_selfConnectMode;
    std::array<std::atomic<ClientInterface*>, kClientNum> m_clientTable;
    // Set by client open and close, cleared by the engine thread, which owns the rolling CPU statistics
    std::atomic<bool> m_rollingResetPending;
    Synchro* m_synchroTable;
    ServerNotifyChannel m_channel; // To communicate between the RT thread and server
    ProcessSync m_signal;
    // Number of graph switches so far.  Changed by the RT thread (ProcessNext) only.
    std::atomic<std::uint64_t> m_switchCount;
    // Written by the RT thread at each switch, and by the server thread when a client is deactivated.
    std::atomic<TimeC> m_lastSwitchUsecs;

    std::map<std::int32_t, std::string> m_reservationMap;

    Result<void> clientCloseAux(std::int32_t refnum, bool wait);
    void checkXRun(TimeC callbackUsecs);

    Result<void> notifyAddClient(ClientInterface* newClient, const char* newName, std::int32_t refnum);
    void notifyRemoveClient(const char* name, std::int32_t refnum);

    void processNext(TimeC callbackUsecs);
    void processCurrent(TimeC callbackUsecs);

    // Waits until fSwitchCount reaches count.  A change written to the next state is in effect after the second
    // switch that begins after the write (the first may have begun before it), so a waiter asks for two more switches.
    bool waitForSwitches(std::uint64_t count);

    bool clientCheckName(const char* name);
    bool generateUniqueName(char* name);

    Result<std::int32_t> allocateRefnum();
    void releaseRefnum(std::int32_t refnum);

    Result<void> clientNotify(ClientInterface* client, std::int32_t refnum, const char* name, std::int32_t notify, std::int32_t sync, const char* message, std::int32_t value1, std::int32_t value2);

    void notifyClient(std::int32_t refnum, std::int32_t event, std::int32_t sync, const char* message, std::int32_t value1, std::int32_t value2);
    void notifyClients(std::int32_t event, std::int32_t sync, const char* message, std::int32_t value1, std::int32_t value2);

    void notifyPortRegistation(PortIdC portIndex, bool onoff);
    void notifyPortConnect(PortIdC src, PortIdC dst, bool onoff);
    void notifyPortRename(PortIdC src, const char* oldName);
    void notifyActivate(std::int32_t refnum);

    void ensureUUID(UuidC uuid);

    bool checkClient(std::int32_t refnum)
    {
        return (refnum >= 0 && refnum < kClientNum && m_clientTable[refnum] != nullptr);
    }

    Result<bool> checkPortsConnect(std::int32_t refnum, PortIdC src, PortIdC dst);

public:
    Engine(GraphManager* manager, Synchro* table, EngineControl* controler, char selfConnectMode);
    ~Engine();

    Result<void> open();
    Result<void> close();

    // Client management
    Result<void> clientCheck(const char* name, UuidC uuid, char* nameRes, std::int32_t protocol, std::int32_t options, std::int32_t* status);

    Result<void> clientExternalOpen(const char* name, std::int32_t pid, UuidC uuid, std::int32_t* ref, ShmSegment* sharedEngine, ShmSegment* sharedClient, ShmSegment* sharedGraphManager);
    Result<void> registerDriver(const char* name, std::int32_t* ref, EngineControl** sharedEngine, GraphManager** sharedManager, ClientInterface* client, bool wait);

    Result<void> clientExternalClose(std::int32_t refnum);
    Result<void> unregisterDriver(std::int32_t refnum, bool wait);

    Result<void> clientActivate(std::int32_t refnum, bool isRealTime, bool wait);
    Result<void> clientDeactivate(std::int32_t refnum, bool wait);

    void clientKill(std::int32_t refnum);

    Result<std::int32_t> getClientPID(const char* name);
    Result<std::int32_t> getClientRefNum(const char* name);


    // Port management
    Result<void> portRegister(std::int32_t refnum, const char* name, const char* type, std::uint32_t flags, std::uint32_t bufferSize, PortIdC* port);
    Result<void> portUnRegister(std::int32_t refnum, PortIdC port);

    Result<void> portConnect(std::int32_t refnum, const char* src, const char* dst);
    Result<void> portDisconnect(std::int32_t refnum, const char* src, const char* dst);

    Result<void> portConnect(std::int32_t refnum, PortIdC src, PortIdC dst);
    Result<void> portDisconnect(std::int32_t refnum, PortIdC src, PortIdC dst);

    Result<void> portRename(std::int32_t refnum, PortIdC port, const char* name);


    Result<void> computeTotalLatencies();

    // Graph
    bool process(TimeC curCycleBegin, TimeC prevCycleEnd);

    // Notifications
    void notifyDriverXRun();
    void notifyClientXRun(std::int32_t refnum);
    void notifyFailure(std::int32_t code, const char* reason);
    void notifyGraphReorder();
    void notifyBufferSize(NframesC bufferSize);
    void notifySampleRate(NframesC sampleRate);
    void notifySecondarySampleRate(const char* card, NframesC sampleRate, std::int32_t direction);
    void notifyFreewheel(bool onoff);
    void notifyQuit();

    Result<void> getUUIDForClientName(const char* clientName, char* uuidRes);
    Result<void> getClientNameForUUID(const char* uuid, char* nameRes);
    Result<void> reserveClientName(const char* name, const char* uuid);
};


} // namespace jackmini
