#include <cstdint>

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

#include "AudioDriver.h"
#include "Channel.h"
#include "ClientControl.h"
#include "DriverInfo.h"
#include "DriverInterface.h"
#include "DriverLoader.h"
#include "EngineControl.h"
#include "Error.h"
#include "FreewheelDriver.h"
#include "Globals.h"
#include "GraphManager.h"
#include "LockedEngine.h"
#include "MessageBuffer.h"
#include "ServerCore.h"
#include "ServerGlobals.h"
#include "SystemDeps.h"
#include "ThreadedDriver.h"
#include "Time.h"

const char* getSelfConnectModeDescription(char mode);

namespace jackmini {

// The driver reports 0 on success and -1 on failure.
static Result<void> driverResult(std::int32_t rc)
{
    return (rc < 0) ? Result<void>(std::unexpected(Error::kServerFailure)) : Result<void> { };
}

//----------------
// Server control
//----------------
ServerCore::ServerCore(bool sync, bool temporary, std::int32_t timeout, bool rt, std::int32_t priority, std::int32_t portMax, bool verbose, TimerType clock, char selfConnectMode, const char* serverName)
{
    if (rt) {
        jack_info("JACK server starting in realtime mode with priority %ld", priority);
    } else {
        jack_info("JACK server starting in non-realtime mode");
    }

    jack_info("self-connect-mode is \"%s\"", getSelfConnectModeDescription(selfConnectMode));

    m_graphManager = GraphManager::allocate(portMax);
    m_engineControl = std::make_unique<EngineControl>(sync, temporary, timeout, rt, priority, verbose, clock, serverName);
    m_engine = std::make_unique<LockedEngine>(m_graphManager, getSynchroTable(), m_engineControl.get(), selfConnectMode);

    // A distinction is made between the threaded freewheel driver and the
    // regular freewheel driver because the freewheel driver needs to run in
    // threaded mode when freewheel mode is active and needs to run as a slave
    // when freewheel mode isn't active.
    FreewheelDriver* freewheelDriver = new FreewheelDriver(m_engine.get(), getSynchroTable());
    m_threadedFreewheelDriver = std::make_unique<ThreadedDriver>(freewheelDriver);

    m_freewheelDriver = freewheelDriver;
    m_driverInfo = std::make_unique<DriverInfo>();
    m_audioDriver = nullptr;
    m_freewheel = false;
    ServerGlobals::s_instance = this; // Unique instance
    Globals::s_verbose = verbose;
}

ServerCore::~ServerCore()
{
    // The engine uses the engine control, so it goes first.
    GraphManager::destroy(m_graphManager);
    m_driverInfo.reset();
    m_threadedFreewheelDriver.reset();
    m_engine.reset();
    m_engineControl.reset();
}

Result<void> ServerCore::open(DriverDesc* driverDesc, JSListC* driverParams)
{
    // TODO: move that in reworked JackServerGlobals::Init()
    if (!MessageBuffer::create()) {
        jack_error("Cannot create message buffer");
    }

    if ((m_audioDriver = m_driverInfo->open(driverDesc, m_engine.get(), getSynchroTable(), driverParams)) == nullptr) {
        jack_error("Cannot initialize driver");
        MessageBuffer::destroy();
        return std::unexpected(Error::kServerFailure);
    }

    if (!m_requestChannel.open(m_engineControl->m_serverName.data(), this)) {
        jack_error("Server channel open error");
        m_audioDriver->close();
        MessageBuffer::destroy();
        return std::unexpected(Error::kServerFailure);
    }

    if (!m_engine->open()) {
        jack_error("Cannot open engine");
        m_requestChannel.close();
        m_audioDriver->close();
        MessageBuffer::destroy();
        return std::unexpected(Error::kServerFailure);
    }

    if (m_freewheelDriver->open() < 0) {
        jack_error("Cannot open freewheel driver");
        m_engine->close();
        m_requestChannel.close();
        m_audioDriver->close();
        MessageBuffer::destroy();
        return std::unexpected(Error::kServerFailure);
    }

    if (m_audioDriver->attach() < 0) {
        jack_error("Cannot attach audio driver");
        m_freewheelDriver->close();
        m_engine->close();
        m_requestChannel.close();
        m_audioDriver->close();
        MessageBuffer::destroy();
        return std::unexpected(Error::kServerFailure);
    }

    m_freewheelDriver->setMaster(false);
    m_audioDriver->setMaster(true);
    m_audioDriver->addSlave(m_freewheelDriver);
    initTime();
    setClockSource(m_engineControl->m_clockSource);
    return { };
}

void ServerCore::close()
{
    jack_log("JackServer::Close");
    m_requestChannel.close();
    m_audioDriver->detach();
    m_audioDriver->close();
    m_freewheelDriver->close();
    m_engine->close();
    // TODO: move that in reworked JackServerGlobals::Destroy()
    MessageBuffer::destroy();
    endTime();
}

Result<void> ServerCore::start(bool freewheel)
{
    jack_log("JackServer::Start freewheel = %d", freewheel);
    if (freewheel) {
        // Begin in freewheel mode with the sound card never started (SetFreewheel would stop it)
        m_freewheel = true;
        m_engine->notifyFreewheel(true);
        m_audioDriver->setMaster(false);
        m_freewheelDriver->setMaster(true);
        if (m_threadedFreewheelDriver->start() < 0) {
            return std::unexpected(Error::kServerFailure);
        }
    } else if (m_audioDriver->start() < 0) {
        return std::unexpected(Error::kServerFailure);
    }
    return m_requestChannel.start();
}

Result<void> ServerCore::stop()
{
    jack_log("JackServer::Stop");
    std::int32_t res = -1;

    if (m_freewheel) {
        if (m_threadedFreewheelDriver) {
            res = m_threadedFreewheelDriver->stop();
        }
    } else {
        if (m_audioDriver) {
            res = m_audioDriver->stop();
        }
    }

    m_engine->notifyQuit();
    m_requestChannel.stop();
    m_engine->notifyFailure(kFailure | kServerError, kServerFailure);

    return driverResult(res);
}

bool ServerCore::isRunning()
{
    jack_log("JackServer::IsRunning");
    assert(fAudioDriver);
    return m_audioDriver->isRunning();
}

//---------------------------
// From request thread : API
//---------------------------

Result<void> ServerCore::setBufferSize(NframesC bufferSize)
{
    jack_log("JackServer::SetBufferSize nframes = %ld", bufferSize);
    NframesC currentBufferSize = m_engineControl->m_bufferSize;

    if (currentBufferSize == bufferSize) {
        jack_log("SetBufferSize: requirement for new buffer size equals current value");
        return { };
    }

    if (m_audioDriver->isFixedBufferSize()) {
        jack_log("SetBufferSize: driver only supports a fixed buffer size");
        return std::unexpected(Error::kInvalidState);
    }

    if (m_audioDriver->stop() != 0) {
        jack_error("Cannot stop audio driver");
        return std::unexpected(Error::kServerFailure);
    }

    if (m_audioDriver->setBufferSize(bufferSize) == 0) {
        m_engine->notifyBufferSize(bufferSize);
        return driverResult(m_audioDriver->start());
    } else { // Failure: try to restore current value
        jack_error("Cannot SetBufferSize for audio driver, restore current value %ld", currentBufferSize);
        m_audioDriver->setBufferSize(currentBufferSize);
        m_audioDriver->start();
        // SetBufferSize actually failed, so return an error...
        return std::unexpected(Error::kServerFailure);
    }
}

// Freewheel mode is implemented by switching from the (audio [slaves] + freewheel) driver to the freewheel driver only:
//
// - "global" connection state is saved
// - all audio driver and slaves ports are deconnected, thus there is no more dependencies with the audio driver and slaves
// - the freewheel driver will be synchronized with the end of graph execution : all clients are connected to the freewheel driver
// - the freewheel driver becomes the "master"
//
// Normal mode is restored with the connections state valid before freewheel mode was done. Thus one consider that
// no graph state change can be done during freewheel mode.

Result<void> ServerCore::setFreewheel(bool onoff)
{
    jack_log("JackServer::SetFreewheel is = %ld want = %ld", m_freewheel, onoff);

    if (m_freewheel) {
        if (onoff) {
            return std::unexpected(Error::kInvalidState);
        } else {
            m_freewheel = false;
            m_threadedFreewheelDriver->stop();
            m_graphManager->restore(&m_connectionState); // Restore connection state
            m_engine->notifyFreewheel(onoff);
            m_freewheelDriver->setMaster(false);
            m_audioDriver->setMaster(true);
            return driverResult(m_audioDriver->start());
        }
    } else {
        if (onoff) {
            m_freewheel = true;
            m_audioDriver->stop();
            m_graphManager->save(&m_connectionState); // Save connection state
            // Disconnect all slaves
            std::list<DriverInterface*> slaveList = m_audioDriver->getSlaves();
            std::list<DriverInterface*>::const_iterator it;
            for (it = slaveList.begin(); it != slaveList.end(); it++) {
                Driver* slave = dynamic_cast<Driver*>(*it);
                assert(slave);
                m_graphManager->disconnectAllPorts(slave->getClientControl()->m_refNum);
            }
            // Disconnect master
            m_graphManager->disconnectAllPorts(m_audioDriver->getClientControl()->m_refNum);
            m_engine->notifyFreewheel(onoff);
            m_audioDriver->setMaster(false);
            m_freewheelDriver->setMaster(true);
            return driverResult(m_threadedFreewheelDriver->start());
        } else {
            return std::unexpected(Error::kInvalidState);
        }
    }
}

//---------------------------
// Coming from the RT thread
//---------------------------

void ServerCore::notify(std::int32_t refnum, std::int32_t notify, std::int32_t value)
{
    switch (notify) {

    case kGraphOrderCallback:
        m_engine->notifyGraphReorder();
        break;

    case kXRunCallback:
        m_engine->notifyClientXRun(refnum);
        break;
    }
}

//--------------------
// Backend management
//--------------------

//----------------------
// Transport management
//----------------------

Result<void> ServerCore::releaseTimebase(std::int32_t refnum)
{
    return m_engineControl->m_transport.resetTimebase(refnum);
}

Result<void> ServerCore::setTimebaseCallback(std::int32_t refnum, std::int32_t conditional)
{
    return m_engineControl->m_transport.setTimebaseMaster(refnum, conditional);
}

LockedEngine* ServerCore::getEngine()
{
    return m_engine.get();
}

Synchro* ServerCore::getSynchroTable()
{
    return m_synchroTable.data();
}

EngineControl* ServerCore::getEngineControl()
{
    return m_engineControl.get();
}

GraphManager* ServerCore::getGraphManager()
{
    return m_graphManager;
}

} // namespace jackmini
