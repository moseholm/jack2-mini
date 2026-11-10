// Copyright (C) 2001 Paul Davis
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

#include "JackDriver.h"
#include "JackClientControl.h"
#include "JackEngineControl.h"
#include "JackError.h"
#include "JackGlobals.h"
#include "JackGraphManager.h"
#include "JackLockedEngine.h"
#include "JackPort.h"
#include "JackSystemDeps.h"
#include "JackTime.h"
#include "jack/uuid.h"
#include <cstdint>
#include <assert.h>
#include <math.h>
#include <array>

using namespace std;

namespace jackmini {

Driver::Driver(const char* name, const char* alias, LockedEngine* engine, Synchro* table)
    : m_captureChannels(0)
    , m_playbackChannels(0)
    , m_clientControl(name, jack_client_uuid_generate())
    , m_withMonitorPorts(false)
{
    assert(strlen(name) < kClientNameSize);
    m_synchroTable = table;
    strcpy(m_aliasName.data(), alias);
    m_engine = engine;
    m_graphManager = nullptr;
    m_beginDateUst = 0;
    m_endDateUst = 0;
    m_delayedUsecs = 0.f;
    m_isMaster = true;
    m_isRunning = false;
}

Driver::~Driver()
{
    jack_log("~JackDriver");
}

std::int32_t Driver::open()
{
    std::int32_t refnum = -1;

    if (!m_engine->registerDriver(m_clientControl.m_name.data(), &refnum, &m_engineControl, &m_graphManager, this, false)) {
        jack_error("Cannot allocate internal client for driver");
        return -1;
    }

    m_clientControl.m_refNum = refnum;
    m_clientControl.m_active = true;
    m_engineControl->m_driverNum++;
    m_graphManager->directConnect(m_clientControl.m_refNum, m_clientControl.m_refNum); // Connect driver to itself for "sync" mode
    setupDriverSync(m_clientControl.m_refNum, false);
    return 0;
}

std::int32_t Driver::open(NframesC bufferSize,
    NframesC sampleRate,
    bool capturing,
    bool playing,
    std::int32_t inchannels,
    std::int32_t outchannels,
    bool monitor,
    const char* captureDriverName,
    const char* playbackDriverName,
    NframesC captureLatency,
    NframesC playbackLatency)
{
    jack_log("JackDriver::Open capture_driver_name = %s", captureDriverName);
    jack_log("JackDriver::Open playback_driver_name = %s", playbackDriverName);
    std::int32_t refnum = -1;
    std::array<char, kClientNameSize + 1> nameRes { };
    std::int32_t status;

    // Check name and possibly rename
    if (!m_engine->clientCheck(m_clientControl.m_name.data(), -1, nameRes.data(), kProtocolVersion, (std::int32_t)kNullOption, (std::int32_t*)&status)) {
        jack_error("Client name = %s conflits with another running client", m_clientControl.m_name);
        return -1;
    }
    strcpy(m_clientControl.m_name.data(), nameRes.data());

    if (!m_engine->registerDriver(m_clientControl.m_name.data(), &refnum, &m_engineControl, &m_graphManager, this, false)) {
        jack_error("Cannot allocate internal client for driver");
        return -1;
    }

    m_clientControl.m_refNum = refnum;
    m_clientControl.m_active = true;
    m_engineControl->m_driverNum++;
    if (bufferSize > 0) {
        m_engineControl->m_bufferSize = bufferSize;
    }
    if (sampleRate > 0) {
        m_engineControl->m_sampleRate = sampleRate;
    }
    m_captureLatency = captureLatency;
    m_playbackLatency = playbackLatency;

    assert(strlen(capture_driver_name) < kClientNameSize);
    assert(strlen(playback_driver_name) < kClientNameSize);

    strcpy(m_captureDriverName.data(), captureDriverName);
    strcpy(m_playbackDriverName.data(), playbackDriverName);

    m_engineControl->updateTimeOut();

    m_graphManager->setBufferSize(m_engineControl->m_bufferSize);
    m_graphManager->directConnect(m_clientControl.m_refNum, m_clientControl.m_refNum); // Connect driver to itself for "sync" mode
    setupDriverSync(m_clientControl.m_refNum, false);
    return 0;
}

std::int32_t Driver::close()
{
    if (m_clientControl.m_refNum >= 0) {
        jack_log("JackDriver::Close");
        m_graphManager->directDisconnect(m_clientControl.m_refNum, m_clientControl.m_refNum); // Disconnect driver from itself for sync
        m_clientControl.m_active = false;
        m_engineControl->m_driverNum--;
        return m_engine->unregisterDriver(m_clientControl.m_refNum, false) ? 0 : -1;
    } else {
        return -1;
    }
}

// In "async" mode, the server does not synchronize itself on the output drivers, thus it would never "consume" the activations.
// The synchronization primitives for drivers are setup in "flush" mode that to not keep unneeded activations.
// Drivers synchro are setup in "flush" mode if server is "async" and NOT freewheel.
void Driver::setupDriverSync(std::int32_t ref, bool freewheel)
{
    if (!freewheel && !m_engineControl->m_syncMode) {
        jack_log("JackDriver::SetupDriverSync driver sem in flush mode");
        m_synchroTable[ref].setFlush(true);
    } else {
        jack_log("JackDriver::SetupDriverSync driver sem in normal mode");
        m_synchroTable[ref].setFlush(false);
    }
}

std::int32_t Driver::clientNotify(std::int32_t refnum, const char* name, std::int32_t notify, std::int32_t sync, const char* message, std::int32_t value1, std::int32_t value2)
{
    jack_log("JackDriver::ClientNotify ref = %ld driver = %s name = %s notify = %ld", refnum, m_clientControl.m_name.data(), name, notify);

    switch (notify) {

    case kStartFreewheelCallback:
        jack_log("JackDriver::kStartFreewheel");
        setupDriverSync(m_clientControl.m_refNum, true);
        break;

    case kStopFreewheelCallback:
        jack_log("JackDriver::kStopFreewheel");
        setupDriverSync(m_clientControl.m_refNum, false);
        break;
    }

    return 0;
}

bool Driver::isRealTime() const
{
    return m_engineControl->m_realTime;
}

void Driver::cycleIncTime()
{
    m_engineControl->cycleIncTime(m_beginDateUst);
}

void Driver::cycleTakeBeginTime()
{
    m_beginDateUst = getMicroseconds(); // Take callback date here
    m_engineControl->cycleIncTime(m_beginDateUst);
}

void Driver::cycleTakeEndTime()
{
    m_endDateUst = getMicroseconds(); // Take end date here
}

ClientControl* Driver::getClientControl() const
{
    return (ClientControl*)&m_clientControl;
}

void Driver::notifyXRun(TimeC curCycleBegin, float delayedUsecs)
{
    m_engineControl->notifyXRun(curCycleBegin, delayedUsecs);
    m_engine->notifyDriverXRun();
}

void Driver::notifyBufferSize(NframesC bufferSize)
{
    m_engine->notifyBufferSize(bufferSize);
    m_engineControl->initFrameTime();
}

void Driver::notifySampleRate(NframesC sampleRate)
{
    m_engine->notifySampleRate(sampleRate);
    m_engineControl->initFrameTime();
}

void Driver::notifySecondarySampleRate(const char* card, NframesC sampleRate, std::int32_t direction)
{
    m_engine->notifySecondarySampleRate(card, sampleRate, direction);
}

void Driver::notifyFailure(std::int32_t code, const char* reason)
{
    m_engine->notifyFailure(code, reason);
}

void Driver::setMaster(bool onoff)
{
    m_isMaster = onoff;
}

bool Driver::getMaster()
{
    return m_isMaster;
}

void Driver::addSlave(DriverInterface* slave)
{
    m_slaveList.push_back(slave);
}

void Driver::removeSlave(DriverInterface* slave)
{
    m_slaveList.remove(slave);
}

std::int32_t Driver::processReadSlaves()
{
    std::int32_t res = 0;
    list<DriverInterface*>::const_iterator it;
    for (it = m_slaveList.begin(); it != m_slaveList.end(); it++) {
        DriverInterface* slave = *it;
        if (slave->isRunning()) {
            if (slave->processRead() < 0) {
                res = -1;
            }
        }
    }
    return res;
}

std::int32_t Driver::processWriteSlaves()
{
    std::int32_t res = 0;
    list<DriverInterface*>::const_iterator it;
    for (it = m_slaveList.begin(); it != m_slaveList.end(); it++) {
        DriverInterface* slave = *it;
        if (slave->isRunning()) {
            if (slave->processWrite() < 0) {
                res = -1;
            }
        }
    }
    return res;
}

std::int32_t Driver::processRead()
{
    return (m_engineControl->m_syncMode) ? processReadSync() : processReadAsync();
}

std::int32_t Driver::processWrite()
{
    return (m_engineControl->m_syncMode) ? processWriteSync() : processWriteAsync();
}

std::int32_t Driver::processReadSync()
{
    return 0;
}

std::int32_t Driver::processWriteSync()
{
    return 0;
}

std::int32_t Driver::processReadAsync()
{
    return 0;
}

std::int32_t Driver::processWriteAsync()
{
    return 0;
}

std::int32_t Driver::process()
{
    return 0;
}

std::int32_t Driver::attach()
{
    return 0;
}

std::int32_t Driver::detach()
{
    return 0;
}

std::int32_t Driver::read()
{
    return 0;
}

std::int32_t Driver::write()
{
    return 0;
}

std::int32_t Driver::start()
{
    if (m_isMaster) {
        m_engineControl->initFrameTime();
    }
    m_isRunning = true;
    return startSlaves();
}

std::int32_t Driver::stop()
{
    m_isRunning = false;
    return stopSlaves();
}

std::int32_t Driver::startSlaves()
{
    std::int32_t res = 0;
    list<DriverInterface*>::const_iterator it;
    for (it = m_slaveList.begin(); it != m_slaveList.end(); it++) {
        DriverInterface* slave = *it;
        if (slave->start() < 0) {
            res = -1;
            // XXX: We should attempt to stop all of the slaves that we've
            // started here.
            break;
        }
    }
    return res;
}

std::int32_t Driver::stopSlaves()
{
    std::int32_t res = 0;
    list<DriverInterface*>::const_iterator it;
    for (it = m_slaveList.begin(); it != m_slaveList.end(); it++) {
        DriverInterface* slave = *it;
        if (slave->stop() < 0) {
            res = -1;
        }
    }
    return res;
}

bool Driver::isFixedBufferSize()
{
    return true;
}

std::int32_t Driver::setBufferSize(NframesC bufferSize)
{
    std::int32_t res = 0;
    list<DriverInterface*>::const_iterator it;
    for (it = m_slaveList.begin(); it != m_slaveList.end(); it++) {
        DriverInterface* slave = *it;
        if (slave->setBufferSize(bufferSize) < 0) {
            res = -1;
        }
    }
    return res;
}

std::int32_t Driver::setSampleRate(NframesC sampleRate)
{
    std::int32_t res = 0;
    list<DriverInterface*>::const_iterator it;
    for (it = m_slaveList.begin(); it != m_slaveList.end(); it++) {
        DriverInterface* slave = *it;
        if (slave->setSampleRate(sampleRate) < 0) {
            res = -1;
        }
    }
    return res;
}

bool Driver::initialize()
{
    return true;
}

static string removeLast(const string& name)
{
    return name.substr(0, name.find_last_of(':')); // Remove end of name after last ":"
}

void Driver::saveConnections(std::int32_t alias)
{
    const char** connections;
    std::array<char, kRealJackPortNameSize + 1> alias1 { };
    std::array<char, kRealJackPortNameSize + 1> alias2 { };
    std::array<char, kRealJackPortNameSize + 1> systemAlias1 { };
    std::array<char, kRealJackPortNameSize + 1> systemAlias2 { };
    std::array<char*, 2> aliases;
    std::array<char*, 2> systemAliases;

    aliases[0] = alias1.data();
    aliases[1] = alias2.data();

    systemAliases[0] = systemAlias1.data();
    systemAliases[1] = systemAlias2.data();

    m_connections.clear();

    for (std::int32_t i = 0; i < m_captureChannels; ++i) {
        if (m_capturePortList[i] && (connections = m_graphManager->getConnections(m_capturePortList[i])) != 0) {
            if (alias == 0) {
                for (std::int32_t j = 0; connections[j]; j++) {
                    ServerPort* portId = m_graphManager->getPort(m_capturePortList[i]);
                    m_connections.push_back(make_pair(portId->getType(), make_pair(portId->getName(), connections[j])));
                    jack_info("Save connection: %s %s", m_graphManager->getPort(m_capturePortList[i])->getName(), connections[j]);
                }
            } else {
                std::int32_t res1 = m_graphManager->getPort(m_capturePortList[i])->getAliases(aliases.data());
                string subSystemName;
                if (res1 >= alias) {
                    subSystemName = aliases[alias - 1];
                } else {
                    subSystemName = m_graphManager->getPort(m_capturePortList[i])->getName();
                }
                for (std::int32_t j = 0; connections[j]; j++) {
                    ServerPort* portId = m_graphManager->getPort(m_graphManager->getPort(connections[j]));
                    std::int32_t res2 = portId->getAliases(systemAliases.data());
                    string subSystem;
                    if (res2 >= alias) {
                        subSystem = systemAliases[alias - 1];
                    } else {
                        subSystem = connections[j];
                    }
                    m_connections.push_back(make_pair(portId->getType(), make_pair(subSystemName, subSystem)));
                    jack_info("Save connection: %s %s", subSystemName.c_str(), subSystem.c_str());
                }
            }
            free(connections);
        }
    }

    for (std::int32_t i = 0; i < m_playbackChannels; ++i) {
        if (m_playbackPortList[i] && (connections = m_graphManager->getConnections(m_playbackPortList[i])) != 0) {
            if (alias == 0) {
                for (std::int32_t j = 0; connections[j]; j++) {
                    ServerPort* portId = m_graphManager->getPort(m_playbackPortList[i]);
                    m_connections.push_back(make_pair(portId->getType(), make_pair(connections[j], portId->getName())));
                    jack_info("Save connection: %s %s", connections[j], m_graphManager->getPort(m_playbackPortList[i])->getName());
                }
            } else {
                std::int32_t res1 = m_graphManager->getPort(m_playbackPortList[i])->getAliases(aliases.data());
                string subSystemName;
                if (res1 >= alias) {
                    subSystemName = aliases[alias - 1];
                } else {
                    subSystemName = m_graphManager->getPort(m_playbackPortList[i])->getName();
                }
                for (std::int32_t j = 0; connections[j]; j++) {
                    ServerPort* portId = m_graphManager->getPort(m_graphManager->getPort(connections[j]));
                    std::int32_t res2 = portId->getAliases(systemAliases.data());
                    string subName;
                    if (res2 >= alias) {
                        subName = systemAliases[alias - 1];
                    } else {
                        subName = connections[j];
                    }
                    m_connections.push_back(make_pair(portId->getType(), make_pair(subName, subSystemName)));
                    jack_info("Save connection: %s %s", subName.c_str(), subSystemName.c_str());
                }
            }
            free(connections);
        }
    }
}

string Driver::matchPortName(const char* name, const char** ports, std::int32_t alias, const std::string& type)
{
    std::array<char, kRealJackPortNameSize + 1> alias1 { };
    std::array<char, kRealJackPortNameSize + 1> alias2 { };
    std::array<char*, 2> aliases;

    aliases[0] = alias1.data();
    aliases[1] = alias2.data();

    for (std::int32_t i = 0; ports && ports[i]; ++i) {

        PortIdC portId2 = m_graphManager->getPort(ports[i]);
        ServerPort* port2 = (portId2 != kNoPort) ? m_graphManager->getPort(portId2) : nullptr;

        if (port2) {
            std::int32_t res = port2->getAliases(aliases.data());
            string nameStr;
            if (res >= alias) {
                nameStr = string(aliases[alias - 1]);
            } else {
                nameStr = string(ports[i]);
            }
            string subName = removeLast(name);
            if ((nameStr.find(subName) != string::npos) && (type == string(port2->getType()))) {
                return nameStr;
            }
        }
    }

    return "";
}

void Driver::loadConnections(std::int32_t alias, bool fullName)
{
    list<pair<string, pair<string, string>>>::const_iterator it;

    if (fullName) {
        for (it = m_connections.begin(); it != m_connections.end(); it++) {
            pair<string, string> connection = (*it).second;
            jack_info("Load connection: %s %s", connection.first.c_str(), connection.second.c_str());
            m_engine->portConnect(m_clientControl.m_refNum, connection.first.c_str(), connection.second.c_str());
        }
    } else {
        const char** inputs = m_graphManager->getPorts(nullptr, nullptr, kPortIsInput);
        const char** outputs = m_graphManager->getPorts(nullptr, nullptr, kPortIsOutput);

        for (it = m_connections.begin(); it != m_connections.end(); it++) {
            pair<string, string> connection = (*it).second;
            string realInput = matchPortName(connection.first.c_str(), outputs, alias, (*it).first);
            string realOutput = matchPortName(connection.second.c_str(), inputs, alias, (*it).first);
            if ((realInput != "") && (realOutput != "")) {
                jack_info("Load connection: %s %s", realInput.c_str(), realOutput.c_str());
                m_engine->portConnect(m_clientControl.m_refNum, realInput.c_str(), realOutput.c_str());
            }
        }

        // Wait for connection change
        if (m_graphManager->isPendingChange()) {
            sleepMicroseconds(std::int32_t(m_engineControl->m_periodUsecs * 1.1f));
        }

        if (inputs) {
            free(inputs);
        }
        if (outputs) {
            free(outputs);
        }
    }
}

Result<void> Driver::resumeRefNum()
{
    return m_graphManager->resumeRefNum(&m_clientControl, m_synchroTable);
}

Result<void> Driver::suspendRefNum()
{
    return m_graphManager->suspendRefNum(&m_clientControl, m_synchroTable, kDriverTimeoutFactor * m_engineControl->m_timeOutUsecs);
}

} // namespace jackmini
