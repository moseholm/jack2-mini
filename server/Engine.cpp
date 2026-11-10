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

#include <assert.h>
#include <ctype.h>
#include <array>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <set>

#include "CString.h"
#include "Channel.h"
#include "ClientControl.h"
#include "EngineControl.h"
#include "Error.h"
#include "ExternalClient.h"
#include "Globals.h"
#include "LockedEngine.h"
#include "ServerCore.h"
#include "ServerGlobals.h"
#include "SystemDeps.h"
#include "jack/uuid.h"

namespace jackmini {

Engine::Engine(GraphManager* manager,
    Synchro* table,
    EngineControl* control,
    char selfConnectMode)
    : LockAble(control->m_serverName.data())
    , m_signal(control->m_serverName.data())
{
    m_graphManager = manager;
    m_synchroTable = table;
    m_engineControl = control;
    m_selfConnectMode = selfConnectMode;
    for (std::int32_t i = 0; i < kClientNum; i++) {
        m_clientTable[i] = nullptr;
    }
    m_rollingResetPending = false;
    m_lastSwitchUsecs = 0;
    m_switchCount = 0;
}

Engine::~Engine()
{
}

Result<void> Engine::open()
{
    jack_log("JackEngine::Open");

    // Open audio thread => request thread communication channel
    return m_channel.open(m_engineControl->m_serverName.data());
}

Result<void> Engine::close()
{
    jack_log("JackEngine::Close");
    m_channel.close();

    // Close remaining clients (RT is stopped)
    for (std::int32_t i = m_engineControl->m_driverNum; i < kClientNum; i++) {
        ClientInterface* client = m_clientTable[i];
        if (ExternalClient* externalClient = dynamic_cast<ExternalClient*>(client)) {
            jack_log("JackEngine::Close external client = %s", externalClient->getClientControl()->m_name);
            externalClient->close();
            m_clientTable[i] = nullptr;
            delete externalClient;
        }
    }

    return { };
}

void Engine::notifyQuit()
{
    m_channel.notifyQuit();
}


//-----------------------------
// Client resource management
//-----------------------------

Result<std::int32_t> Engine::allocateRefnum()
{
    for (std::int32_t i = 0; i < kClientNum; i++) {
        if (!m_clientTable[i]) {
            jack_log("JackEngine::AllocateRefNum ref = %ld", i);
            return i;
        }
    }
    return std::unexpected(Error::kServerFailure);
}

void Engine::releaseRefnum(std::int32_t refnum)
{
    m_clientTable[refnum] = nullptr;

    if (m_engineControl->m_temporary) {
        std::int32_t i;
        for (i = m_engineControl->m_driverNum; i < kClientNum; i++) {
            if (m_clientTable[i]) {
                break;
            }
        }
        if (i == kClientNum) {
            // Last client and temporary case: quit the server
            jack_log("JackEngine::ReleaseRefnum server quit");
            m_engineControl->m_temporary = false;
            throw TemporaryException();
        }
    }
}

//------------------
// Graph management
//------------------

void Engine::processNext(TimeC curCycleBegin)
{
    m_lastSwitchUsecs = curCycleBegin;
    if (m_graphManager->runNextGraph()) { // True if the graph actually switched to a new state
        m_channel.notify(kAllClients, kGraphOrderCallback, 0);
    }
    m_switchCount.fetch_add(1);
    m_signal.signal(); // Signal for threads waiting for a switch
}

bool Engine::waitForSwitches(std::uint64_t count)
{
    // A switch is signalled without the lock (RT path); a missed signal costs at most the wait for the next switch.
    m_signal.lock();
    bool res = true;
    while (m_switchCount.load() < count) {
        if (!m_signal.timedWait(kGraphSwitchGuard * 1000000)) {
            res = false;
            break;
        }
    }
    m_signal.unlock();
    return res;
}

void Engine::processCurrent(TimeC curCycleBegin)
{
    if (curCycleBegin < m_lastSwitchUsecs + 2 * m_engineControl->m_periodUsecs) { // Signal XRun only for the first failing cycle
        checkXRun(curCycleBegin);
    }
    m_graphManager->runCurrentGraph();
}

bool Engine::process(TimeC curCycleBegin, TimeC prevCycleEnd)
{
    bool res = true;

    // Cycle  begin
    if (m_rollingResetPending.exchange(false)) {
        m_engineControl->resetRollingUsecs();
    }
    m_engineControl->cycleBegin(m_clientTable.data(), m_graphManager, curCycleBegin, prevCycleEnd);

    // Graph
    if (m_graphManager->isFinishedGraph()) {
        processNext(curCycleBegin);
        res = true;
    } else {
        jack_log("Process: graph not finished!");
        if (curCycleBegin > m_lastSwitchUsecs + m_engineControl->m_timeOutUsecs) {
            jack_log("Process: switch to next state delta = %ld", std::int64_t(curCycleBegin - m_lastSwitchUsecs));
            processNext(curCycleBegin);
            res = true;
        } else {
            jack_log("Process: waiting to switch delta = %ld", std::int64_t(curCycleBegin - m_lastSwitchUsecs));
            processCurrent(curCycleBegin);
            res = false;
        }
    }

    // Cycle end
    m_engineControl->cycleEnd(m_clientTable.data());
    return res;
}

// Client that finish *after* the callback date are considered late even if their output buffers may have been
// correctly mixed in the time window: callbackUsecs <==> Read <==> Write.

static const char* state2String(ClientState state)
{
    switch (state) {
    case kNotTriggered:
        return "NotTriggered";
    case kTriggered:
        return "Triggered";
    case kRunning:
        return "Running";
    case kFinished:
        return "Finished";
    default:
        return "";
    }
}

void Engine::checkXRun(TimeC callbackUsecs) // REVOIR les conditions de fin
{
    for (std::int32_t i = m_engineControl->m_driverNum; i < kClientNum; i++) {
        ClientInterface* client = m_clientTable[i];
        if (client && client->getClientControl()->m_active) {
            ClientTiming* timing = m_graphManager->getClientTiming(i);
            ClientState status = timing->m_status;
            TimeC finishedDate = timing->m_finishedAt;

            if (status != kNotTriggered && status != kFinished) {
                jack_error("JackEngine::XRun: client = %s was not finished, state = %s", client->getClientControl()->m_name, state2String(status));
                m_channel.notify(kAllClients, kXRunCallback, 0); // Notify all clients
            }

            if (status == kFinished && (std::int64_t)(finishedDate - callbackUsecs) > 0) {
                jack_error("JackEngine::XRun: client %s finished after current callback", client->getClientControl()->m_name);
                m_channel.notify(kAllClients, kXRunCallback, 0); // Notify all clients
            }
        }
    }
}

Result<void> Engine::computeTotalLatencies()
{
    std::vector<Int> sorted;
    std::vector<Int>::iterator it;
    std::vector<Int>::reverse_iterator rit;

    m_graphManager->topologicalSort(sorted);

    // iterate over all clients in graph order, and emit
    // capture latency callback.

    for (it = sorted.begin(); it != sorted.end(); it++) {
        notifyClient(*it, kLatencyCallback, true, "", 0, 0);
    }

    // now issue playback latency callbacks in reverse graph order.
    for (rit = sorted.rbegin(); rit != sorted.rend(); rit++) {
        notifyClient(*rit, kLatencyCallback, true, "", 1, 0);
    }

    return { };
}

//--------------
// Metadata API
//--------------

//---------------
// Notifications
//---------------

Result<void> Engine::clientNotify(ClientInterface* client, std::int32_t refnum, const char* name, std::int32_t notify, std::int32_t sync, const char* message, std::int32_t value1, std::int32_t value2)
{
    // Check if notification is needed
    if (!client->getClientControl()->m_callback[notify]) {
        jack_log("JackEngine::ClientNotify: no callback for notification = %ld", notify);
        return { };
    }

    std::int32_t res1;

    // External client
    if (dynamic_cast<ExternalClient*>(client)) {
        res1 = client->clientNotify(refnum, name, notify, sync, message, value1, value2);
        // Important for internal client : unlock before calling the notification callbacks
    } else {
        bool res2 = unlock();
        res1 = client->clientNotify(refnum, name, notify, sync, message, value1, value2);
        if (res2) {
            lock();
        }
    }

    if (res1 < 0) {
        jack_error("ClientNotify fails name = %s notification = %ld val1 = %ld val2 = %ld", name, notify, value1, value2);
        return std::unexpected(Error::kServerFailure);
    }
    return { };
}

void Engine::notifyClient(std::int32_t refnum, std::int32_t event, std::int32_t sync, const char* message, std::int32_t value1, std::int32_t value2)
{
    ClientInterface* client = m_clientTable[refnum];
    if (client) {
        clientNotify(client, refnum, client->getClientControl()->m_name.data(), event, sync, message, value1, value2);
    }
}

void Engine::notifyClients(std::int32_t event, std::int32_t sync, const char* message, std::int32_t value1, std::int32_t value2)
{
    for (std::int32_t i = 0; i < kClientNum; i++) {
        notifyClient(i, event, sync, message, value1, value2);
    }
}

Result<void> Engine::notifyAddClient(ClientInterface* newClient, const char* newName, std::int32_t refnum)
{
    jack_log("JackEngine::NotifyAddClient: name = %s", newName);

    // Notify existing clients of the new client and new client of existing clients.
    for (std::int32_t i = 0; i < kClientNum; i++) {
        ClientInterface* oldClient = m_clientTable[i];
        if (oldClient && oldClient != newClient) {
            char* oldName = oldClient->getClientControl()->m_name.data();
            if (!clientNotify(oldClient, refnum, newName, kAddClient, false, "", 0, 0)) {
                jack_error("NotifyAddClient old_client fails name = %s", oldName);
                // Not considered as a failure...
            }
            if (!clientNotify(newClient, i, oldName, kAddClient, true, "", 0, 0)) {
                jack_error("NotifyAddClient new_client fails name = %s", newName);
                return std::unexpected(Error::kServerFailure);
            }
        }
    }

    return { };
}

void Engine::notifyRemoveClient(const char* name, std::int32_t refnum)
{
    // Notify existing clients (including the one being suppressed) of the removed client
    for (std::int32_t i = 0; i < kClientNum; i++) {
        ClientInterface* client = m_clientTable[i];
        if (client) {
            clientNotify(client, refnum, name, kRemoveClient, false, "", 0, 0);
        }
    }
}

// Coming from the driver
void Engine::notifyDriverXRun()
{
    // Use the audio thread => request thread communication channel
    m_channel.notify(kAllClients, kXRunCallback, 0);
}

void Engine::notifyClientXRun(std::int32_t refnum)
{
    if (refnum == kAllClients) {
        notifyClients(kXRunCallback, false, "", 0, 0);
    } else {
        notifyClient(refnum, kXRunCallback, false, "", 0, 0);
    }
}

void Engine::notifyGraphReorder()
{
    computeTotalLatencies();
    notifyClients(kGraphOrderCallback, false, "", 0, 0);
}

void Engine::notifySecondarySampleRate(const char* card, NframesC sampleRate, std::int32_t direction)
{
    notifyClients(kSecondarySampleRateCallback, false, card, sampleRate, direction);
}

void Engine::notifyBufferSize(NframesC bufferSize)
{
    notifyClients(kBufferSizeCallback, true, "", bufferSize, 0);
}

void Engine::notifySampleRate(NframesC sampleRate)
{
    notifyClients(kSampleRateCallback, true, "", sampleRate, 0);
}

void Engine::notifyFailure(std::int32_t code, const char* reason)
{
    notifyClients(kShutDownCallback, false, reason, code, 0);
}

void Engine::notifyFreewheel(bool onoff)
{
    m_engineControl->m_freewheel = onoff;
    if (onoff) {
        // Save RT state
        m_engineControl->m_savedRealTime = m_engineControl->m_realTime;
        m_engineControl->m_realTime = false;
    } else {
        // Restore RT state
        m_engineControl->m_realTime = m_engineControl->m_savedRealTime;
        m_engineControl->m_savedRealTime = false;
    }
    notifyClients((onoff ? kStartFreewheelCallback : kStopFreewheelCallback), true, "", 0, 0);
}

void Engine::notifyPortRegistation(PortIdC portIndex, bool onoff)
{
    notifyClients((onoff ? kPortRegistrationOnCallback : kPortRegistrationOffCallback), false, "", portIndex, 0);
}

void Engine::notifyPortRename(PortIdC port, const char* oldName)
{
    notifyClients(kPortRenameCallback, false, oldName, port, 0);
}

void Engine::notifyPortConnect(PortIdC src, PortIdC dst, bool onoff)
{
    notifyClients((onoff ? kPortConnectCallback : kPortDisconnectCallback), false, "", src, dst);
}

void Engine::notifyActivate(std::int32_t refnum)
{
    notifyClient(refnum, kActivateClient, true, "", 0, 0);
}

//-------------------
// Client management
//-------------------

Result<void> Engine::clientCheck(const char* name, UuidC uuid, char* nameRes, std::int32_t protocol, std::int32_t options, std::int32_t* status)
{
    // Clear status
    *status = 0;
    strcpy(nameRes, name);

    jack_log("Check protocol client = %ld server = %ld", protocol, kProtocolVersion);

    if (protocol != kProtocolVersion) {
        *status |= (kFailure | kVersionError);
        jack_error("JACK protocol mismatch (%d vs %d)", protocol, kProtocolVersion);
        return std::unexpected(Error::kProtocol);
    }

    std::map<std::int32_t, std::string>::iterator res = m_reservationMap.find(uuid);

    if (res != m_reservationMap.end()) {
        strncpy(nameRes, res->second.c_str(), kClientNameSize);
    } else if (clientCheckName(name)) {

        *status |= kNameNotUnique;

        if (options & kUseExactName) {
            jack_error("cannot create new client; %s already exists", name);
            *status |= kFailure;
            return std::unexpected(Error::kNameTaken);
        }

        if (generateUniqueName(nameRes)) {
            *status |= kFailure;
            return std::unexpected(Error::kNameTaken);
        }
    }

    return { };
}

bool Engine::generateUniqueName(char* name)
{
    std::int32_t tens, ones;
    std::int32_t length = strlen(name);

    if (length > kClientNameSize - 4) {
        jack_error("%s exists and is too long to make unique", name);
        return true; // failure
    }

    // generate a unique name by appending "-01".."-99"
    name[length++] = '-';
    tens = length++;
    ones = length++;
    name[tens] = '0';
    name[ones] = '1';
    name[length] = '\0';

    while (clientCheckName(name)) {
        if (name[ones] == '9') {
            if (name[tens] == '9') {
                jack_error("client %s has 99 extra instances already", name);
                return true; // give up
            }
            name[tens]++;
            name[ones] = '0';
        } else {
            name[ones]++;
        }
    }
    return false;
}

bool Engine::clientCheckName(const char* name)
{
    for (std::int32_t i = 0; i < kClientNum; i++) {
        ClientInterface* client = m_clientTable[i];
        if (client && (strcmp(client->getClientControl()->m_name.data(), name) == 0)) {
            return true;
        }
    }

    for (std::map<std::int32_t, std::string>::iterator i = m_reservationMap.begin(); i != m_reservationMap.end(); i++) {
        if (i->second == name) {
            return true;
        }
    }

    return false;
}

void Engine::ensureUUID(UuidC uuid)
{
    if (jack_uuid_empty(uuid))
        return;

    for (std::int32_t i = 0; i < kClientNum; i++) {
        ClientInterface* client = m_clientTable[i];
        if (client && jack_uuid_compare(client->getClientControl()->m_sessionID, uuid) == 0) {
            // FIXME? this code does nothing, but jack1 has it like this too..
            jack_uuid_clear(&uuid);
            // client->GetClientControl()->fSessionID = jack_client_uuid_generate();
        }
    }
}

Result<std::int32_t> Engine::getClientPID(const char* name)
{
    for (std::int32_t i = 0; i < kClientNum; i++) {
        ClientInterface* client = m_clientTable[i];
        if (client && (strcmp(client->getClientControl()->m_name.data(), name) == 0)) {
            return client->getClientControl()->m_pID;
        }
    }

    return std::unexpected(Error::kNotFound);
}

Result<std::int32_t> Engine::getClientRefNum(const char* name)
{
    for (std::int32_t i = 0; i < kClientNum; i++) {
        ClientInterface* client = m_clientTable[i];
        if (client && (strcmp(client->getClientControl()->m_name.data(), name) == 0)) {
            return client->getClientControl()->m_refNum;
        }
    }

    return std::unexpected(Error::kNotFound);
}

// Used for external clients
Result<void> Engine::clientExternalOpen(const char* name, std::int32_t pid, UuidC uuid, std::int32_t* ref, ShmSegment* sharedEngine, ShmSegment* sharedClient, ShmSegment* sharedGraphManager)
{
    std::array<char, kClientNameSize + 1> realName { };

    if (jack_uuid_empty(uuid)) {
        uuid = jack_client_uuid_generate();
        strncpy(realName.data(), name, kClientNameSize);
    } else {
        std::map<std::int32_t, std::string>::iterator res = m_reservationMap.find(uuid);
        if (res != m_reservationMap.end()) {
            strncpy(realName.data(), res->second.c_str(), kClientNameSize);
            m_reservationMap.erase(uuid);
        } else {
            strncpy(realName.data(), name, kClientNameSize);
        }
        ensureUUID(uuid);
    }

    jack_log("JackEngine::ClientExternalOpen: uuid = %d, name = %s", uuid, realName.data());

    auto allocated = allocateRefnum();
    if (!allocated) {
        jack_error("No more refnum available");
        return std::unexpected(Error::kServerFailure);
    }
    std::int32_t refnum = *allocated;

    ExternalClient* client = new ExternalClient();

    if (!m_synchroTable[refnum].allocate(realName.data(), m_engineControl->m_serverName.data(), 0)) {
        jack_error("Cannot allocate synchro");
        goto error;
    }

    if (!client->openClient(realName.data(), pid, refnum, uuid, sharedClient)) {
        jack_error("Cannot open client");
        goto error;
    }

    if (!waitForSwitches(m_switchCount.load() + 1)) {
        // Failure if RT thread is not running (problem with the driver...)
        jack_error("Driver is not running");
        goto error;
    }

    // Only now that the driver runs: the channel starts a writer thread, and a refused open must not create one.
    if (!client->openChannel(realName.data())) {
        jack_error("Cannot open client channel");
        goto error;
    }

    m_clientTable[refnum] = client;

    if (!notifyAddClient(client, realName.data(), refnum)) {
        jack_error("Cannot notify add client");
        goto error;
    }

    m_graphManager->initRefNum(refnum);
    m_graphManager->setClientUUID(refnum, client->getClientControl()->m_sessionID);
    m_rollingResetPending = true;
    *sharedEngine = ShmSegment { m_engineControl->getShmFd(), m_engineControl->getShmSize() };
    *sharedGraphManager = ShmSegment { m_graphManager->getShmFd(), m_graphManager->getShmSize() };
    *ref = refnum;
    return { };

error:
    // Cleanup...
    m_synchroTable[refnum].destroy();
    m_clientTable[refnum] = 0;
    client->close();
    delete client;
    return std::unexpected(Error::kServerFailure);
}

// Used for server driver clients
Result<void> Engine::registerDriver(const char* name, std::int32_t* ref, EngineControl** sharedEngine, GraphManager** sharedManager, ClientInterface* client, bool wait)
{
    jack_log("JackEngine::RegisterDriver: name = %s", name);

    auto allocated = allocateRefnum();
    if (!allocated) {
        jack_error("No more refnum available");
        return std::unexpected(Error::kServerFailure);
    }
    std::int32_t refnum = *allocated;

    if (!m_synchroTable[refnum].allocate(name, m_engineControl->m_serverName.data(), 0)) {
        jack_error("Cannot allocate synchro");
        goto error;
    }

    if (wait && !waitForSwitches(m_switchCount.load() + 1)) {
        // Failure if RT thread is not running (problem with the driver...)
        jack_error("Driver is not running");
        goto error;
    }

    m_clientTable[refnum] = client;

    if (!notifyAddClient(client, name, refnum)) {
        jack_error("Cannot notify add client");
        goto error;
    }

    m_graphManager->initRefNum(refnum);
    m_graphManager->setClientUUID(refnum, client->getClientControl()->m_sessionID);
    m_rollingResetPending = true;
    *sharedEngine = m_engineControl;
    *sharedManager = m_graphManager;
    *ref = refnum;
    return { };

error:
    // Cleanup...
    m_synchroTable[refnum].destroy();
    m_clientTable[refnum] = 0;
    return std::unexpected(Error::kServerFailure);
}

// Used for external clients
Result<void> Engine::clientExternalClose(std::int32_t refnum)
{
    jack_log("JackEngine::ClientExternalClose ref = %ld", refnum);
    ClientInterface* client = m_clientTable[refnum];
    assert(client);
    auto res = clientCloseAux(refnum, true);
    client->close();
    delete client;
    return res;
}

// Used for server internal clients or drivers when the RT thread is stopped
Result<void> Engine::unregisterDriver(std::int32_t refnum, bool wait)
{
    jack_log("JackEngine::UnregisterDriver ref = %ld", refnum);
    return clientCloseAux(refnum, wait);
}

Result<void> Engine::clientCloseAux(std::int32_t refnum, bool wait)
{
    jack_log("JackEngine::ClientCloseAux ref = %ld", refnum);

    ClientInterface* client = m_clientTable[refnum];
    m_engineControl->m_transport.resetTimebase(refnum);

    UuidC uuid = kUuidEmptyInitializer;
    jack_uuid_copy(&uuid, client->getClientControl()->m_sessionID);

    // Unregister all ports ==> notifications are sent
    std::array<Int, kPortNumForClient> ports;
    std::int32_t i;

    m_graphManager->getInputPorts(refnum, ports.data());
    for (i = 0; (i < kPortNumForClient) && (ports[i] != kEmpty); i++) {
        portUnRegister(refnum, ports[i]);
    }

    m_graphManager->getOutputPorts(refnum, ports.data());
    for (i = 0; (i < kPortNumForClient) && (ports[i] != kEmpty); i++) {
        portUnRegister(refnum, ports[i]);
    }

    // Remove the client from the table
    releaseRefnum(refnum);

    // Remove all ports
    m_graphManager->removeAllPorts(refnum);

    // Wait until next cycle to be sure client is not used anymore
    if (wait) {
        if (!waitForSwitches(m_switchCount.load() + 2)) {
            jack_error("JackEngine::ClientCloseAux wait error ref = %ld", refnum);
        }
    }

    // Notify running clients
    notifyRemoveClient(client->getClientControl()->m_name.data(), refnum);

    // Cleanup...
    m_synchroTable[refnum].destroy();
    m_rollingResetPending = true;
    return { };
}

Result<void> Engine::clientActivate(std::int32_t refnum, bool isRealTime, bool wait)
{
    ClientInterface* client = m_clientTable[refnum];
    jack_log("JackEngine::ClientActivate ref = %ld name = %s", refnum, client->getClientControl()->m_name);

    if (isRealTime) {
        m_graphManager->activate(refnum);
    }

    // Wait for graph state change to be effective
    if (wait && !waitForSwitches(m_switchCount.load() + 2)) {
        jack_error("JackEngine::ClientActivate wait error ref = %ld name = %s", refnum, client->getClientControl()->m_name);
        return std::unexpected(Error::kServerFailure);
    } else {
        std::array<Int, kPortNumForClient> inputPorts;
        std::array<Int, kPortNumForClient> outputPorts;
        m_graphManager->getInputPorts(refnum, inputPorts.data());
        m_graphManager->getOutputPorts(refnum, outputPorts.data());

        // Notify client
        notifyActivate(refnum);

        // Then issue port registration notification
        for (std::int32_t i = 0; (i < kPortNumForClient) && (inputPorts[i] != kEmpty); i++) {
            notifyPortRegistation(inputPorts[i], true);
        }
        for (std::int32_t i = 0; (i < kPortNumForClient) && (outputPorts[i] != kEmpty); i++) {
            notifyPortRegistation(outputPorts[i], true);
        }

        return { };
    }
}

// May be called without client
Result<void> Engine::clientDeactivate(std::int32_t refnum, bool wait)
{
    ClientInterface* client = m_clientTable[refnum];
    jack_log("JackEngine::ClientDeactivate ref = %ld name = %s", refnum, client->getClientControl()->m_name);

    // The ports and their connections are kept: the graph skips this client until it is activated again.
    m_graphManager->deactivate(refnum);
    m_lastSwitchUsecs = 0; // Force switch to occur next cycle, even when called with "dead" clients

    // In freewheel the driver waits for every client of the graph (see JackFreewheelDriver), and a removed client never
    // signals it.  Wake the driver, so that it reaches Process and switches to the graph without the client.  If the
    // driver was not waiting, Process finds the graph unfinished and retries, so the wake does no harm.
    if (m_engineControl->m_freewheel && !m_graphManager->isFinishedGraph()) {
        m_synchroTable[kFreewheelDriverRefnum].signal();
    }

    // Wait for graph state change to be effective
    if (wait && !waitForSwitches(m_switchCount.load() + 2)) {
        jack_error("JackEngine::ClientDeactivate wait error ref = %ld name = %s", refnum, client->getClientControl()->m_name);
        return std::unexpected(Error::kServerFailure);
    } else {
        return { };
    }
}

void Engine::clientKill(std::int32_t refnum)
{
    jack_log("JackEngine::ClientKill ref = %ld", refnum);
    if (!clientDeactivate(refnum, true)) {
        jack_error("JackEngine::ClientKill ref = %ld cannot be removed from the graph !!", refnum);
    }
    if (!clientExternalClose(refnum)) {
        jack_error("JackEngine::ClientKill ref = %ld cannot be closed", refnum);
    }
}

//-----------------
// Port management
//-----------------

Result<void> Engine::portRegister(std::int32_t refnum, const char* name, const char* type, std::uint32_t flags, std::uint32_t bufferSize, PortIdC* portIndex)
{
    jack_log("JackEngine::PortRegister ref = %ld name = %s type = %s flags = %d buffer_size = %d", refnum, name, type, flags, bufferSize);
    ClientInterface* client = m_clientTable[refnum];

    // Check if port name already exists
    if (m_graphManager->getPort(name) != kNoPort) {
        jack_error("port_name \"%s\" already exists", name);
        return std::unexpected(Error::kNameTaken);
    }

    // buffer_size is actually ignored...
    *portIndex = m_graphManager->allocatePort(refnum, name, type, (PortFlagsC)flags, m_engineControl->m_bufferSize);
    if (*portIndex != kNoPort) {
        if (client->getClientControl()->m_active) {
            notifyPortRegistation(*portIndex, true);
        }
        return { };
    } else {
        return std::unexpected(Error::kServerFailure);
    }
}

Result<void> Engine::portUnRegister(std::int32_t refnum, PortIdC portIndex)
{
    jack_log("JackEngine::PortUnRegister ref = %ld port_index = %ld", refnum, portIndex);
    ClientInterface* client = m_clientTable[refnum];
    assert(client);

    // Disconnect port ==> notification is sent
    portDisconnect(-1, portIndex, kAllPorts);

    if (m_graphManager->releasePort(refnum, portIndex)) {
        if (client->getClientControl()->m_active) {
            notifyPortRegistation(portIndex, false);
        }
        return { };
    } else {
        return std::unexpected(Error::kServerFailure);
    }
}

// this check is to prevent apps to self connect to other apps
// TODO: make this work with multiple clients per app
Result<bool> Engine::checkPortsConnect(std::int32_t refnum, PortIdC src, PortIdC dst)
{
    if (m_selfConnectMode == ' ')
        return true;

    ServerPort* srcPort = m_graphManager->getPort(src);
    ServerPort* dstPort = m_graphManager->getPort(dst);

    jack_log("JackEngine::CheckPortsConnect(ref = %d, src = %d, dst = %d)", refnum, srcPort->getRefNum(), dstPort->getRefNum());

    // jack_log("%s -> %s", src_port->GetName(), dst_port->GetName());
    // jack_log("mode = '%c'", fSelfConnectMode);

    std::int32_t srcSelf = srcPort->getRefNum() == refnum ? 1 : 0;
    std::int32_t dstSelf = dstPort->getRefNum() == refnum ? 1 : 0;

    // jack_log("src_self is %s", src_self ? "true" : "false");
    // jack_log("dst_self is %s", dst_self ? "true" : "false");

    // 0 means client is connecting other client ports (control app patchbay functionality)
    // 1 means client is connecting its own port to port of other client (e.g. self connecting into "system" client)
    // 2 means client is connecting its own ports (for app internal functionality)
    std::int32_t sum = srcSelf + dstSelf;
    // jack_log("sum = %d", sum);
    if (sum == 0)
        return true;
    char lmode = tolower(m_selfConnectMode);
    // jack_log("lmode = '%c'", lmode);
    if (sum == 2 && lmode == 'e')
        return true;
    bool fail = lmode != m_selfConnectMode; // fail modes are upper case
    // jack_log("fail = %d", (int)fail);

    jack_info(
        "%s port self connect request%s (%s -> %s)",
        fail ? "rejecting" : "ignoring",
        sum == 1 ? " to external port" : "",
        srcPort->getName(),
        dstPort->getName());

    if (fail) {
        return std::unexpected(Error::kInvalidArgument);
    }
    return false;
}

Result<void> Engine::portConnect(std::int32_t refnum, const char* src, const char* dst)
{
    jack_log("JackEngine::PortConnect ref = %d src = %s dst = %s", refnum, src, dst);
    PortIdC portSrc, portDst;

    if (!m_graphManager->getTwoPorts(src, dst, &portSrc, &portDst)) {
        return std::unexpected(Error::kNotFound);
    }
    return portConnect(refnum, portSrc, portDst);
}

Result<void> Engine::portConnect(std::int32_t refnum, PortIdC src, PortIdC dst)
{
    jack_log("JackEngine::PortConnect ref = %d src = %d dst = %d", refnum, src, dst);
    ClientInterface* client;
    std::int32_t ref;

    if (!m_graphManager->checkPorts(src, dst)) {
        return std::unexpected(Error::kInvalidArgument);
    }

    ref = m_graphManager->getOutputRefNum(src);
    assert(ref >= 0);
    client = m_clientTable[ref];
    assert(client);
    if (!client->getClientControl()->m_active) {
        jack_error("Cannot connect ports owned by inactive clients:"
                   " \"%s\" is not active",
            client->getClientControl()->m_name);
        return std::unexpected(Error::kInvalidState);
    }

    ref = m_graphManager->getInputRefNum(dst);
    assert(ref >= 0);
    client = m_clientTable[ref];
    assert(client);
    if (!client->getClientControl()->m_active) {
        jack_error("Cannot connect ports owned by inactive clients:"
                   " \"%s\" is not active",
            client->getClientControl()->m_name);
        return std::unexpected(Error::kInvalidState);
    }

    auto allowed = checkPortsConnect(refnum, src, dst);
    if (!allowed) {
        return std::unexpected(allowed.error());
    }
    if (!*allowed) {
        return { };
    }

    auto connected = m_graphManager->connect(src, dst);
    if (!connected) {
        return connected;
    }
    notifyPortConnect(src, dst, true);
    return { };
}

Result<void> Engine::portDisconnect(std::int32_t refnum, const char* src, const char* dst)
{
    jack_log("JackEngine::PortDisconnect ref = %d src = %s dst = %s", refnum, src, dst);
    PortIdC portSrc, portDst;

    if (!m_graphManager->getTwoPorts(src, dst, &portSrc, &portDst)) {
        return std::unexpected(Error::kNotFound);
    }
    return portDisconnect(refnum, portSrc, portDst);
}

Result<void> Engine::portDisconnect(std::int32_t refnum, PortIdC src, PortIdC dst)
{
    jack_log("JackEngine::PortDisconnect ref = %d src = %d dst = %d", refnum, src, dst);

    if (dst == kAllPorts) {

        std::array<Int, kConnectionNumForPort> connections;
        m_graphManager->getConnections(src, connections.data());

        ServerPort* port = m_graphManager->getPort(src);
        bool failed = false;
        if (port->getFlags() & kPortIsOutput) {
            for (std::int32_t i = 0; (i < kConnectionNumForPort) && (connections[i] != kEmpty); i++) {
                if (!portDisconnect(refnum, src, connections[i])) {
                    failed = true;
                }
            }
        } else {
            for (std::int32_t i = 0; (i < kConnectionNumForPort) && (connections[i] != kEmpty); i++) {
                if (!portDisconnect(refnum, connections[i], src)) {
                    failed = true;
                }
            }
        }

        if (failed) {
            return std::unexpected(Error::kServerFailure);
        }
        return { };
    }

    if (!m_graphManager->checkPorts(src, dst)) {
        return std::unexpected(Error::kInvalidArgument);
    }

    auto allowed = checkPortsConnect(refnum, src, dst);
    if (!allowed) {
        return std::unexpected(allowed.error());
    }
    if (!*allowed) {
        return { };
    }

    auto disconnected = m_graphManager->disconnect(src, dst);
    if (!disconnected) {
        return disconnected;
    }
    notifyPortConnect(src, dst, false);
    return { };
}

Result<void> Engine::portRename(std::int32_t refnum, PortIdC port, const char* name)
{
    std::array<char, kRealJackPortNameSize + 1> oldName { };
    strcpy(oldName.data(), m_graphManager->getPort(port)->getName());
    m_graphManager->getPort(port)->setName(name);
    notifyPortRename(port, oldName.data());
    return { };
}

//--------------------
//--------------------

Result<void> Engine::getUUIDForClientName(const char* clientName, char* uuidRes)
{
    for (std::int32_t i = 0; i < kClientNum; i++) {
        ClientInterface* client = m_clientTable[i];

        if (client && (strcmp(clientName, client->getClientControl()->m_name.data()) == 0)) {
            jack_uuid_unparse(client->getClientControl()->m_sessionID, uuidRes);
            return { };
        }
    }
    // Did not find name.
    return std::unexpected(Error::kNotFound);
}

Result<void> Engine::getClientNameForUUID(const char* uuidBuf, char* nameRes)
{
    UuidC uuid;
    if (jack_uuid_parse(uuidBuf, &uuid) != 0)
        return std::unexpected(Error::kInvalidArgument);

    for (std::int32_t i = 0; i < kClientNum; i++) {
        ClientInterface* client = m_clientTable[i];

        if (!client) {
            continue;
        }

        if (jack_uuid_compare(client->getClientControl()->m_sessionID, uuid) == 0) {
            strncpy(nameRes, client->getClientControl()->m_name.data(), kClientNameSize);
            return { };
        }
    }
    // Did not find uuid.
    return std::unexpected(Error::kNotFound);
}

Result<void> Engine::reserveClientName(const char* name, const char* uuidstr)
{
    jack_log("JackEngine::ReserveClientName ( name = %s, uuid = %s )", name, uuidstr);

    if (clientCheckName(name)) {
        jack_log("name already taken");
        return std::unexpected(Error::kNameTaken);
    }

    UuidC uuid;
    if (jack_uuid_parse(uuidstr, &uuid) != 0) {
        jack_error("JackEngine::ReserveClientName invalid uuid %s", uuidstr);
        return std::unexpected(Error::kInvalidArgument);
    }

    ensureUUID(uuid);
    m_reservationMap[uuid] = name;
    return { };
}

} // namespace jackmini
