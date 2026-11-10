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

class LockedEngine;
class GraphManager;
struct EngineControl;
class SlaveDriverInterface;

// \brief The base interface for drivers.

class SERVER_EXPORT DriverInterface {

public:
    DriverInterface()
    {
    }
    virtual ~DriverInterface()
    {
    }

    virtual std::int32_t open() = 0;

    virtual std::int32_t open(NframesC bufferSize,
        NframesC samplerate,
        bool capturing,
        bool playing,
        std::int32_t inchannels,
        std::int32_t outchannels,
        bool monitor,
        const char* captureDriverName,
        const char* playbackDriverName,
        NframesC captureLatency,
        NframesC playbackLatency) = 0;

    virtual std::int32_t attach() = 0;
    virtual std::int32_t detach() = 0;

    virtual std::int32_t read() = 0;
    virtual std::int32_t write() = 0;

    virtual std::int32_t start() = 0;
    virtual std::int32_t stop() = 0;

    virtual bool isFixedBufferSize() = 0;
    virtual std::int32_t setBufferSize(NframesC bufferSize) = 0;
    virtual std::int32_t setSampleRate(NframesC sampleRate) = 0;

    virtual std::int32_t process() = 0;

    virtual void setMaster(bool onoff) = 0;
    virtual bool getMaster() = 0;

    virtual void addSlave(DriverInterface* slave) = 0;
    virtual void removeSlave(DriverInterface* slave) = 0;

    virtual std::list<DriverInterface*> getSlaves() = 0;

    // For "master" driver
    virtual std::int32_t processReadSlaves() = 0;
    virtual std::int32_t processWriteSlaves() = 0;

    // For "slave" driver
    virtual std::int32_t processRead() = 0;
    virtual std::int32_t processWrite() = 0;

    // For "slave" driver in "synchronous" mode
    virtual std::int32_t processReadSync() = 0;
    virtual std::int32_t processWriteSync() = 0;

    // For "slave" driver in "asynchronous" mode
    virtual std::int32_t processReadAsync() = 0;
    virtual std::int32_t processWriteAsync() = 0;

    virtual bool isRealTime() const = 0;
    virtual bool isRunning() const = 0;
};

// \brief The base interface for drivers clients.

class SERVER_EXPORT DriverClientInterface : public DriverInterface, public ClientInterface { };

// \brief The base class for drivers.

inline constexpr PortFlagsC kCaptureDriverFlags = static_cast<PortFlagsC>(kPortIsOutput | kPortIsPhysical | kPortIsTerminal);
inline constexpr PortFlagsC kPlaybackDriverFlags = static_cast<PortFlagsC>(kPortIsInput | kPortIsPhysical | kPortIsTerminal);
inline constexpr PortFlagsC kMonitorDriverFlags = static_cast<PortFlagsC>(kPortIsOutput);

typedef std::list<std::pair<std::string, std::pair<std::string, std::string>>> DriverConnectionsList; // [type : (src, dst)]

class SERVER_EXPORT Driver : public DriverClientInterface {

protected:
    std::array<char, kClientNameSize + 1> m_captureDriverName { };
    std::array<char, kClientNameSize + 1> m_playbackDriverName { };
    std::array<char, kClientNameSize + 1> m_aliasName { };

    NframesC m_captureLatency;
    NframesC m_playbackLatency;

    std::int32_t m_captureChannels;
    std::int32_t m_playbackChannels;

    TimeC m_beginDateUst;
    TimeC m_endDateUst;
    float m_delayedUsecs;

    // Pointers to engine state
    LockedEngine* m_engine;
    GraphManager* m_graphManager;
    Synchro* m_synchroTable;
    EngineControl* m_engineControl;
    ClientControl m_clientControl;

    std::list<DriverInterface*> m_slaveList;

    bool m_isMaster;
    bool m_isRunning;
    bool m_withMonitorPorts;

    // Static tables since the actual number of ports may be changed by the real driver
    // thus dynamic allocation is more difficult to handle
    std::array<PortIdC, kDriverPortNum> m_capturePortList;
    std::array<PortIdC, kDriverPortNum> m_playbackPortList;
    std::array<PortIdC, kDriverPortNum> m_monitorPortList;

    DriverConnectionsList m_connections; // Connections list

    void cycleIncTime();
    void cycleTakeBeginTime();
    void cycleTakeEndTime();

    void setupDriverSync(std::int32_t ref, bool freewheel);

    void notifyXRun(TimeC callbackUsecs, float delayedUsecs); // XRun notification sent by the driver
    void notifyBufferSize(NframesC bufferSize); // BufferSize notification sent by the driver
    void notifySampleRate(NframesC sampleRate); // SampleRate notification sent by the driver
    void notifySecondarySampleRate(const char* card, NframesC sampleRate, std::int32_t direction); // secondary card rate change
    void notifyFailure(std::int32_t code, const char* reason); // Failure notification sent by the driver

    virtual void saveConnections(std::int32_t alias);
    virtual void loadConnections(std::int32_t alias, bool fullName = true);
    std::string matchPortName(const char* name, const char** ports, std::int32_t alias, const std::string& type);

    virtual std::int32_t startSlaves();
    virtual std::int32_t stopSlaves();

    virtual Result<void> resumeRefNum();
    virtual Result<void> suspendRefNum();

public:
    Driver(const char* name, const char* alias, LockedEngine* engine, Synchro* table);
    virtual ~Driver();

    void setMaster(bool onoff);
    bool getMaster();

    void addSlave(DriverInterface* slave);
    void removeSlave(DriverInterface* slave);

    std::list<DriverInterface*> getSlaves()
    {
        return m_slaveList;
    }

    virtual std::int32_t open();

    virtual std::int32_t open(NframesC bufferSize,
        NframesC samplerate,
        bool capturing,
        bool playing,
        std::int32_t inchannels,
        std::int32_t outchannels,
        bool monitor,
        const char* captureDriverName,
        const char* playbackDriverName,
        NframesC captureLatency,
        NframesC playbackLatency);

    virtual std::int32_t close();

    virtual std::int32_t process();

    virtual std::int32_t attach();
    virtual std::int32_t detach();

    virtual std::int32_t read();
    virtual std::int32_t write();

    virtual std::int32_t start();
    virtual std::int32_t stop();

    // For "master" driver
    std::int32_t processReadSlaves();
    std::int32_t processWriteSlaves();

    // For "slave" driver with typically decompose a given cycle in separated Read and Write parts.
    virtual std::int32_t processRead();
    virtual std::int32_t processWrite();

    // For "slave" driver in "synchronous" mode
    virtual std::int32_t processReadSync();
    virtual std::int32_t processWriteSync();

    // For "slave" driver in "asynchronous" mode
    virtual std::int32_t processReadAsync();
    virtual std::int32_t processWriteAsync();

    virtual bool isFixedBufferSize();
    virtual std::int32_t setBufferSize(NframesC bufferSize);
    virtual std::int32_t setSampleRate(NframesC sampleRate);

    virtual std::int32_t clientNotify(std::int32_t refnum, const char* name, std::int32_t notify, std::int32_t sync, const char* message, std::int32_t value1, std::int32_t value2);
    virtual ClientControl* getClientControl() const;

    virtual bool isRealTime() const;
    virtual bool isRunning() const { return m_isRunning; }
    virtual bool initialize(); // To be called by the wrapping thread Init method when the driver is a "blocking" one
};

} // namespace jackmini
