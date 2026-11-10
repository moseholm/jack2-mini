// Copyright (C) 2001 Paul Davis
// Copyright (C) 2004-2008 Grame.
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// (at your option) any later version.
//
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

#include "AudioDriver.h"
#include "CString.h"
#include "EngineControl.h"
#include "Error.h"
#include "Exception.h"
#include "GraphManager.h"
#include "LockedEngine.h"
#include "ServerPort.h"
#include "SystemDeps.h"
#include "Time.h"
#include <assert.h>
#include <array>
#include <cstdint>

using namespace std;

namespace jackmini {

AudioDriver::AudioDriver(const char* name, const char* alias, LockedEngine* engine, Synchro* table)
    : Driver(name, alias, engine, table)
{
}

AudioDriver::~AudioDriver()
{
}

std::int32_t AudioDriver::setBufferSize(NframesC bufferSize)
{
    // Update engine and graph manager state
    m_engineControl->m_bufferSize = bufferSize;
    m_graphManager->setBufferSize(bufferSize);

    m_engineControl->updateTimeOut();
    updateLatencies();

    // Redirected on slaves drivers...
    return Driver::setBufferSize(bufferSize);
}

std::int32_t AudioDriver::setSampleRate(NframesC sampleRate)
{
    m_engineControl->m_sampleRate = sampleRate;
    m_engineControl->updateTimeOut();

    // Redirected on slaves drivers...
    return Driver::setSampleRate(sampleRate);
}

std::int32_t AudioDriver::open(NframesC bufferSize,
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
    m_captureChannels = inchannels;
    m_playbackChannels = outchannels;
    m_withMonitorPorts = monitor;
    memset(m_capturePortList.data(), 0, sizeof(PortIdC) * kDriverPortNum);
    memset(m_playbackPortList.data(), 0, sizeof(PortIdC) * kDriverPortNum);
    memset(m_monitorPortList.data(), 0, sizeof(PortIdC) * kDriverPortNum);
    return Driver::open(bufferSize, samplerate, capturing, playing, inchannels, outchannels,
        monitor, captureDriverName, playbackDriverName, captureLatency, playbackLatency);
}

void AudioDriver::updateLatencies()
{
    LatencyRangeC inputRange;
    LatencyRangeC outputRange;
    LatencyRangeC monitorRange;

    for (std::int32_t i = 0; i < m_captureChannels; i++) {
        inputRange.m_max = inputRange.m_min = m_engineControl->m_bufferSize + m_captureLatency;
        m_graphManager->getPort(m_capturePortList[i])->setLatencyRange(kCaptureLatency, &inputRange);
    }

    for (std::int32_t i = 0; i < m_playbackChannels; i++) {
        outputRange.m_max = outputRange.m_min = m_playbackLatency;
        if (m_engineControl->m_syncMode) {
            outputRange.m_max = outputRange.m_min += m_engineControl->m_bufferSize;
        } else {
            outputRange.m_max = outputRange.m_min += m_engineControl->m_bufferSize * 2;
        }
        m_graphManager->getPort(m_playbackPortList[i])->setLatencyRange(kPlaybackLatency, &outputRange);
        if (m_withMonitorPorts) {
            monitorRange.m_min = monitorRange.m_max = m_engineControl->m_bufferSize;
            m_graphManager->getPort(m_monitorPortList[i])->setLatencyRange(kCaptureLatency, &monitorRange);
        }
    }
}

std::int32_t AudioDriver::attach()
{
    ServerPort* port;
    PortIdC portIndex;
    std::array<char, kRealJackPortNameSize + 1> name { };
    std::array<char, kRealJackPortNameSize + 1> alias { };
    std::int32_t i;

    jack_log("JackAudioDriver::Attach fBufferSize = %ld fSampleRate = %ld", m_engineControl->m_bufferSize, m_engineControl->m_sampleRate);

    for (i = 0; i < m_captureChannels; i++) {
        jackmini::formatCString(alias, "{}:{}:out{}", m_aliasName.data(), m_captureDriverName.data(), i + 1);
        jackmini::formatCString(name, "{}:capture_{}", m_clientControl.m_name.data(), i + 1);
        if (!m_engine->portRegister(m_clientControl.m_refNum, name.data(), kDefaultAudioType, kCaptureDriverFlags, m_engineControl->m_bufferSize, &portIndex)) {
            jack_error("driver: cannot register port for %s", name.data());
            return -1;
        }
        port = m_graphManager->getPort(portIndex);
        port->setAlias(alias.data());
        m_capturePortList[i] = portIndex;
        jack_log("JackAudioDriver::Attach fCapturePortList[i] port_index = %ld", portIndex);
    }

    for (i = 0; i < m_playbackChannels; i++) {
        jackmini::formatCString(alias, "{}:{}:in{}", m_aliasName.data(), m_playbackDriverName.data(), i + 1);
        jackmini::formatCString(name, "{}:playback_{}", m_clientControl.m_name.data(), i + 1);
        if (!m_engine->portRegister(m_clientControl.m_refNum, name.data(), kDefaultAudioType, kPlaybackDriverFlags, m_engineControl->m_bufferSize, &portIndex)) {
            jack_error("driver: cannot register port for %s", name.data());
            return -1;
        }
        port = m_graphManager->getPort(portIndex);
        port->setAlias(alias.data());
        m_playbackPortList[i] = portIndex;
        jack_log("JackAudioDriver::Attach fPlaybackPortList[i] port_index = %ld", portIndex);

        // Monitor ports
        if (m_withMonitorPorts) {
            jack_log("Create monitor port");
            jackmini::formatCString(name, "{}:monitor_{}", m_clientControl.m_name.data(), i + 1);
            if (!m_engine->portRegister(m_clientControl.m_refNum, name.data(), kDefaultAudioType, kPortIsOutput, m_engineControl->m_bufferSize, &portIndex)) {
                jack_error("Cannot register monitor port for %s", name.data());
                return -1;
            } else {
                m_monitorPortList[i] = portIndex;
            }
        }
    }

    updateLatencies();
    return 0;
}

std::int32_t AudioDriver::detach()
{
    std::int32_t i;
    jack_log("JackAudioDriver::Detach");

    for (i = 0; i < m_captureChannels; i++) {
        m_engine->portUnRegister(m_clientControl.m_refNum, m_capturePortList[i]);
    }

    for (i = 0; i < m_playbackChannels; i++) {
        m_engine->portUnRegister(m_clientControl.m_refNum, m_playbackPortList[i]);
        if (m_withMonitorPorts) {
            m_engine->portUnRegister(m_clientControl.m_refNum, m_monitorPortList[i]);
        }
    }

    return 0;
}

std::int32_t AudioDriver::write()
{
    for (std::int32_t i = 0; i < m_playbackChannels; i++) {
        if (m_graphManager->getConnectionsNum(m_playbackPortList[i]) > 0) {
            DefaultAudioSampleC* buffer = getOutputBuffer(i);
            std::int32_t size = sizeof(DefaultAudioSampleC) * m_engineControl->m_bufferSize;
            // Monitor ports
            if (m_withMonitorPorts && m_graphManager->getConnectionsNum(m_monitorPortList[i]) > 0) {
                memcpy(getMonitorBuffer(i), buffer, size);
            }
        }
    }
    return 0;
}

std::int32_t AudioDriver::process()
{
    return (m_engineControl->m_syncMode) ? processSync() : processAsync();
}

// The driver "asynchronous" mode: output buffers computed at the *previous cycle* are used, the server does not
// synchronize to the end of client graph execution.

std::int32_t AudioDriver::processAsync()
{
    // Read input buffers for the current cycle
    if (read() < 0) {
        jack_error("JackAudioDriver::ProcessAsync: read error, stopping...");
        return -1;
    }

    // Write output buffers from the previous cycle
    if (write() < 0) {
        jack_error("JackAudioDriver::ProcessAsync: write error, stopping...");
        return -1;
    }

    // Process graph
    processGraphAsync();

    // Keep end cycle time
    Driver::cycleTakeEndTime();
    return 0;
}

void AudioDriver::processGraphAsync()
{
    // Process graph
    if (m_isMaster) {
        processGraphAsyncMaster();
    } else {
        processGraphAsyncSlave();
    }
}

// Used when the driver works in master mode.

void AudioDriver::processGraphAsyncMaster()
{
    // fBeginDateUst is set in the "low level" layer, fEndDateUst is from previous cycle
    if (!m_engine->process(m_beginDateUst, m_endDateUst)) {
        jack_error("JackAudioDriver::ProcessGraphAsyncMaster: Process error");
    }

    if (!resumeRefNum()) {
        jack_error("JackAudioDriver::ProcessGraphAsyncMaster: ResumeRefNum error");
    }

    if (processReadSlaves() < 0) {
        jack_error("JackAudioDriver::ProcessGraphAsyncMaster: ProcessReadSlaves error");
    }

    if (processWriteSlaves() < 0) {
        jack_error("JackAudioDriver::ProcessGraphAsyncMaster: ProcessWriteSlaves error");
    }

    // Does not wait on graph execution end
}

// Used when the driver works in slave mode.

void AudioDriver::processGraphAsyncSlave()
{
    if (!resumeRefNum()) {
        jack_error("JackAudioDriver::ProcessGraphAsyncSlave: ResumeRefNum error");
    }
}

// The driver "synchronous" mode: the server does synchronize to the end of client graph execution,
// if graph process succeed, output buffers computed at the *current cycle* are used.

std::int32_t AudioDriver::processSync()
{
    // Read input buffers for the current cycle
    if (read() < 0) {
        jack_error("JackAudioDriver::ProcessSync: read error, stopping...");
        return -1;
    }

    // Process graph
    processGraphSync();

    // Write output buffers from the current cycle
    if (write() < 0) {
        jack_error("JackAudioDriver::ProcessSync: write error, stopping...");
        return -1;
    }

    // Keep end cycle time
    Driver::cycleTakeEndTime();
    return 0;
}

void AudioDriver::processGraphSync()
{
    // Process graph
    if (m_isMaster) {
        processGraphSyncMaster();
    } else {
        processGraphSyncSlave();
    }
}

// Used when the driver works in master mode.

void AudioDriver::processGraphSyncMaster()
{
    // fBeginDateUst is set in the "low level" layer, fEndDateUst is from previous cycle
    if (m_engine->process(m_beginDateUst, m_endDateUst)) {

        if (!resumeRefNum()) {
            jack_error("JackAudioDriver::ProcessGraphSyncMaster: ResumeRefNum error");
        }

        if (processReadSlaves() < 0) {
            jack_error("JackAudioDriver::ProcessGraphSync: ProcessReadSlaves error, engine may now behave abnormally!!");
        }

        if (processWriteSlaves() < 0) {
            jack_error("JackAudioDriver::ProcessGraphSync: ProcessWriteSlaves error, engine may now behave abnormally!!");
        }

        // Waits for graph execution end
        if (!suspendRefNum()) {
            jack_error("JackAudioDriver::ProcessGraphSync: SuspendRefNum error, engine may now behave abnormally!!");
        }

    } else { // Graph not finished: do not activate it
        jack_error("JackAudioDriver::ProcessGraphSync: Process error");
    }
}

// Used when the driver works in slave mode.

void AudioDriver::processGraphSyncSlave()
{
    if (!resumeRefNum()) {
        jack_error("JackAudioDriver::ProcessGraphSyncSlave: ResumeRefNum error");
    }
}

DefaultAudioSampleC* AudioDriver::getInputBuffer(std::int32_t portIndex)
{
    return m_capturePortList[portIndex]
        ? (DefaultAudioSampleC*)m_graphManager->getBuffer(m_capturePortList[portIndex], m_engineControl->m_bufferSize)
        : nullptr;
}

DefaultAudioSampleC* AudioDriver::getOutputBuffer(std::int32_t portIndex)
{
    return m_playbackPortList[portIndex]
        ? (DefaultAudioSampleC*)m_graphManager->getBuffer(m_playbackPortList[portIndex], m_engineControl->m_bufferSize)
        : nullptr;
}

DefaultAudioSampleC* AudioDriver::getMonitorBuffer(std::int32_t portIndex)
{
    return m_monitorPortList[portIndex]
        ? (DefaultAudioSampleC*)m_graphManager->getBuffer(m_monitorPortList[portIndex], m_engineControl->m_bufferSize)
        : nullptr;
}

std::int32_t AudioDriver::clientNotify(std::int32_t refnum, const char* name, std::int32_t notify, std::int32_t sync, const char* message, std::int32_t value1, std::int32_t value2)
{
    switch (notify) {

    case kLatencyCallback:
        handleLatencyCallback(value1);
        break;

    default:
        Driver::clientNotify(refnum, name, notify, sync, message, value1, value2);
        break;
    }

    return 0;
}

void AudioDriver::handleLatencyCallback(std::int32_t status)
{
    LatencyCallbackModeC mode = (status == 0) ? kCaptureLatency : kPlaybackLatency;

    for (std::int32_t i = 0; i < m_captureChannels; i++) {
        if (mode == kPlaybackLatency) {
            m_graphManager->recalculateLatency(m_capturePortList[i], mode);
        }
    }

    for (std::int32_t i = 0; i < m_playbackChannels; i++) {
        if (mode == kCaptureLatency) {
            m_graphManager->recalculateLatency(m_playbackPortList[i], mode);
        }
    }
}

} // namespace jackmini
