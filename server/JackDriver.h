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

#pragma once

#include "JackClientControl.h"
#include "JackClientInterface.h"
#include "JackConstants.h"
#include "JackPlatformPlug.h"
#include "jackmini/error.hpp"
#include "types.h"
#include <cstdint>
#include <array>
#include <list>

namespace jackmini {

class JackLockedEngine;
class JackGraphManager;
struct JackEngineControl;
class JackSlaveDriverInterface;

// \brief The base interface for drivers.

class SERVER_EXPORT JackDriverInterface {

public:
    JackDriverInterface()
    {
    }
    virtual ~JackDriverInterface()
    {
    }

    virtual std::int32_t Open() = 0;

    virtual std::int32_t Open(jack_nframes_t buffer_size,
        jack_nframes_t samplerate,
        bool capturing,
        bool playing,
        std::int32_t inchannels,
        std::int32_t outchannels,
        bool monitor,
        const char* capture_driver_name,
        const char* playback_driver_name,
        jack_nframes_t capture_latency,
        jack_nframes_t playback_latency) = 0;

    virtual std::int32_t Attach() = 0;
    virtual std::int32_t Detach() = 0;

    virtual std::int32_t Read() = 0;
    virtual std::int32_t Write() = 0;

    virtual std::int32_t Start() = 0;
    virtual std::int32_t Stop() = 0;

    virtual bool IsFixedBufferSize() = 0;
    virtual std::int32_t SetBufferSize(jack_nframes_t buffer_size) = 0;
    virtual std::int32_t SetSampleRate(jack_nframes_t sample_rate) = 0;

    virtual std::int32_t Process() = 0;

    virtual void SetMaster(bool onoff) = 0;
    virtual bool GetMaster() = 0;

    virtual void AddSlave(JackDriverInterface* slave) = 0;
    virtual void RemoveSlave(JackDriverInterface* slave) = 0;

    virtual std::list<JackDriverInterface*> GetSlaves() = 0;

    // For "master" driver
    virtual std::int32_t ProcessReadSlaves() = 0;
    virtual std::int32_t ProcessWriteSlaves() = 0;

    // For "slave" driver
    virtual std::int32_t ProcessRead() = 0;
    virtual std::int32_t ProcessWrite() = 0;

    // For "slave" driver in "synchronous" mode
    virtual std::int32_t ProcessReadSync() = 0;
    virtual std::int32_t ProcessWriteSync() = 0;

    // For "slave" driver in "asynchronous" mode
    virtual std::int32_t ProcessReadAsync() = 0;
    virtual std::int32_t ProcessWriteAsync() = 0;

    virtual bool IsRealTime() const = 0;
    virtual bool IsRunning() const = 0;
};

// \brief The base interface for drivers clients.

class SERVER_EXPORT JackDriverClientInterface : public JackDriverInterface, public JackClientInterface { };

// \brief The base class for drivers.

#define CaptureDriverFlags static_cast<JackPortFlags>(JackPortIsOutput | JackPortIsPhysical | JackPortIsTerminal)
#define PlaybackDriverFlags static_cast<JackPortFlags>(JackPortIsInput | JackPortIsPhysical | JackPortIsTerminal)
#define MonitorDriverFlags static_cast<JackPortFlags>(JackPortIsOutput)

typedef std::list<std::pair<std::string, std::pair<std::string, std::string>>> driver_connections_list_t; // [type : (src, dst)]

class SERVER_EXPORT JackDriver : public JackDriverClientInterface {

protected:
    std::array<char, JACK_CLIENT_NAME_SIZE + 1> fCaptureDriverName { };
    std::array<char, JACK_CLIENT_NAME_SIZE + 1> fPlaybackDriverName { };
    std::array<char, JACK_CLIENT_NAME_SIZE + 1> fAliasName { };

    jack_nframes_t fCaptureLatency;
    jack_nframes_t fPlaybackLatency;

    std::int32_t fCaptureChannels;
    std::int32_t fPlaybackChannels;

    jack_time_t fBeginDateUst;
    jack_time_t fEndDateUst;
    float fDelayedUsecs;

    // Pointers to engine state
    JackLockedEngine* fEngine;
    JackGraphManager* fGraphManager;
    JackSynchro* fSynchroTable;
    JackEngineControl* fEngineControl;
    JackClientControl fClientControl;

    std::list<JackDriverInterface*> fSlaveList;

    bool fIsMaster;
    bool fIsRunning;
    bool fWithMonitorPorts;

    // Static tables since the actual number of ports may be changed by the real driver
    // thus dynamic allocation is more difficult to handle
    std::array<jack_port_id_t, DRIVER_PORT_NUM> fCapturePortList;
    std::array<jack_port_id_t, DRIVER_PORT_NUM> fPlaybackPortList;
    std::array<jack_port_id_t, DRIVER_PORT_NUM> fMonitorPortList;

    driver_connections_list_t fConnections; // Connections list

    void CycleIncTime();
    void CycleTakeBeginTime();
    void CycleTakeEndTime();

    void SetupDriverSync(std::int32_t ref, bool freewheel);

    void NotifyXRun(jack_time_t callback_usecs, float delayed_usecs); // XRun notification sent by the driver
    void NotifyBufferSize(jack_nframes_t buffer_size); // BufferSize notification sent by the driver
    void NotifySampleRate(jack_nframes_t sample_rate); // SampleRate notification sent by the driver
    void NotifySecondarySampleRate(const char* card, jack_nframes_t sample_rate, std::int32_t direction); // secondary card rate change
    void NotifyFailure(std::int32_t code, const char* reason); // Failure notification sent by the driver

    virtual void SaveConnections(std::int32_t alias);
    virtual void LoadConnections(std::int32_t alias, bool full_name = true);
    std::string MatchPortName(const char* name, const char** ports, std::int32_t alias, const std::string& type);

    virtual std::int32_t StartSlaves();
    virtual std::int32_t StopSlaves();

    virtual Result<void> ResumeRefNum();
    virtual Result<void> SuspendRefNum();

public:
    JackDriver(const char* name, const char* alias, JackLockedEngine* engine, JackSynchro* table);
    virtual ~JackDriver();

    void SetMaster(bool onoff);
    bool GetMaster();

    void AddSlave(JackDriverInterface* slave);
    void RemoveSlave(JackDriverInterface* slave);

    std::list<JackDriverInterface*> GetSlaves()
    {
        return fSlaveList;
    }

    virtual std::int32_t Open();

    virtual std::int32_t Open(jack_nframes_t buffer_size,
        jack_nframes_t samplerate,
        bool capturing,
        bool playing,
        std::int32_t inchannels,
        std::int32_t outchannels,
        bool monitor,
        const char* capture_driver_name,
        const char* playback_driver_name,
        jack_nframes_t capture_latency,
        jack_nframes_t playback_latency);

    virtual std::int32_t Close();

    virtual std::int32_t Process();

    virtual std::int32_t Attach();
    virtual std::int32_t Detach();

    virtual std::int32_t Read();
    virtual std::int32_t Write();

    virtual std::int32_t Start();
    virtual std::int32_t Stop();

    // For "master" driver
    std::int32_t ProcessReadSlaves();
    std::int32_t ProcessWriteSlaves();

    // For "slave" driver with typically decompose a given cycle in separated Read and Write parts.
    virtual std::int32_t ProcessRead();
    virtual std::int32_t ProcessWrite();

    // For "slave" driver in "synchronous" mode
    virtual std::int32_t ProcessReadSync();
    virtual std::int32_t ProcessWriteSync();

    // For "slave" driver in "asynchronous" mode
    virtual std::int32_t ProcessReadAsync();
    virtual std::int32_t ProcessWriteAsync();

    virtual bool IsFixedBufferSize();
    virtual std::int32_t SetBufferSize(jack_nframes_t buffer_size);
    virtual std::int32_t SetSampleRate(jack_nframes_t sample_rate);

    virtual std::int32_t ClientNotify(std::int32_t refnum, const char* name, std::int32_t notify, std::int32_t sync, const char* message, std::int32_t value1, std::int32_t value2);
    virtual JackClientControl* GetClientControl() const;

    virtual bool IsRealTime() const;
    virtual bool IsRunning() const { return fIsRunning; }
    virtual bool Initialize(); // To be called by the wrapping thread Init method when the driver is a "blocking" one
};

} // namespace jackmini
