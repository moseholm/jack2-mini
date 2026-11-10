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

#include "JackThreadedDriver.h"
#include "JackEngineControl.h"
#include "JackError.h"
#include "JackGlobals.h"
#include "JackSystemDeps.h"
#include "JackTools.h"

namespace jackmini {

ThreadedDriver::ThreadedDriver(Driver* driver)
    : m_thread(this)
    , m_driver(driver)
{
}

ThreadedDriver::~ThreadedDriver()
{
    delete m_driver;
}

std::int32_t ThreadedDriver::open()
{
    return m_driver->open();
}

std::int32_t ThreadedDriver::open(NframesC bufferSize,
    NframesC samplerate,
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
    return m_driver->open(bufferSize,
        samplerate, capturing,
        playing, inchannels,
        outchannels, monitor,
        captureDriverName, playbackDriverName,
        captureLatency, playbackLatency);
}

std::int32_t ThreadedDriver::close()
{
    return m_driver->close();
}

std::int32_t ThreadedDriver::process()
{
    return m_driver->process();
}

std::int32_t ThreadedDriver::attach()
{
    return m_driver->attach();
}

std::int32_t ThreadedDriver::detach()
{
    return m_driver->detach();
}

std::int32_t ThreadedDriver::read()
{
    return m_driver->read();
}

std::int32_t ThreadedDriver::write()
{
    return m_driver->write();
}

bool ThreadedDriver::isFixedBufferSize()
{
    return m_driver->isFixedBufferSize();
}

std::int32_t ThreadedDriver::setBufferSize(NframesC bufferSize)
{
    return m_driver->setBufferSize(bufferSize);
}

std::int32_t ThreadedDriver::setSampleRate(NframesC sampleRate)
{
    return m_driver->setSampleRate(sampleRate);
}

void ThreadedDriver::setMaster(bool onoff)
{
    m_driver->setMaster(onoff);
}

bool ThreadedDriver::getMaster()
{
    return m_driver->getMaster();
}

void ThreadedDriver::addSlave(DriverInterface* slave)
{
    m_driver->addSlave(slave);
}

void ThreadedDriver::removeSlave(DriverInterface* slave)
{
    m_driver->removeSlave(slave);
}

std::int32_t ThreadedDriver::processReadSlaves()
{
    return m_driver->processReadSlaves();
}

std::int32_t ThreadedDriver::processWriteSlaves()
{
    return m_driver->processWriteSlaves();
}

std::int32_t ThreadedDriver::processRead()
{
    return m_driver->processRead();
}

std::int32_t ThreadedDriver::processWrite()
{
    return m_driver->processWrite();
}

std::int32_t ThreadedDriver::processReadSync()
{
    return m_driver->processReadSync();
}

std::int32_t ThreadedDriver::processWriteSync()
{
    return m_driver->processWriteSync();
}

std::int32_t ThreadedDriver::processReadAsync()
{
    return m_driver->processReadAsync();
}

std::int32_t ThreadedDriver::processWriteAsync()
{
    return m_driver->processWriteAsync();
}

std::list<DriverInterface*> ThreadedDriver::getSlaves()
{
    return m_driver->getSlaves();
}

std::int32_t ThreadedDriver::clientNotify(std::int32_t refnum, const char* name, std::int32_t notify, std::int32_t sync, const char* message, std::int32_t value1, std::int32_t value2)
{
    return m_driver->clientNotify(refnum, name, notify, sync, message, value1, value2);
}

ClientControl* ThreadedDriver::getClientControl() const
{
    return m_driver->getClientControl();
}

bool ThreadedDriver::isRealTime() const
{
    return m_driver->isRealTime();
}

bool ThreadedDriver::isRunning() const
{
    return m_driver->isRunning();
}

std::int32_t ThreadedDriver::start()
{
    jack_log("JackThreadedDriver::Start");

    if (m_driver->start() < 0) {
        jack_error("Cannot start driver");
        return -1;
    }
    if (m_thread.startSync() < 0) {
        jack_error("Cannot start thread");
        return -1;
    }

    return 0;
}

std::int32_t ThreadedDriver::stop()
{
    jack_log("JackThreadedDriver::Stop");

    switch (m_thread.getStatus()) {

    // Kill the thread in Init phase
    case Thread::kStarting:
    case Thread::kIniting:
        if (m_thread.kill() < 0) {
            jack_error("Cannot kill thread");
        }
        break;

    // Stop when the thread cycle is finished
    case Thread::kRunning:
        if (m_thread.stop() < 0) {
            jack_error("Cannot stop thread");
        }
        break;

    default:
        break;
    }

    if (m_driver->stop() < 0) {
        jack_error("Cannot stop driver");
        return -1;
    }
    return 0;
}

bool ThreadedDriver::execute()
{
    return (process() == 0);
}

bool ThreadedDriver::init()
{
    if (m_driver->initialize()) {
        setRealTime();
        return true;
    } else {
        return false;
    }
}

void ThreadedDriver::setRealTime()
{
    if (m_driver->isRealTime()) {
        jack_log("JackThreadedDriver::Init real-time");
        // Will do "something" on OSX only...
        getEngineControl()->m_period = getEngineControl()->m_constraint = getEngineControl()->m_periodUsecs * 1000;
        getEngineControl()->m_computation = Tools::computationMicroSec(getEngineControl()->m_bufferSize) * 1000;
        m_thread.setParams(getEngineControl()->m_period, getEngineControl()->m_computation, getEngineControl()->m_constraint);
        if (m_thread.acquireSelfRealTime(getEngineControl()->m_serverPriority) < 0) {
            jack_error("AcquireSelfRealTime error");
        } else {
            setThreadedLogFunction();
        }
    } else {
        jack_log("JackThreadedDriver::Init non-realtime");
    }
}


} // namespace jackmini
