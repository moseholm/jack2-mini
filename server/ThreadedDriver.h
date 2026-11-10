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

#include <cstdint>

#include "Driver.h"
#include "PlatformPlug.h"

namespace jackmini {

// \brief The base class for threaded drivers using a "decorator" pattern. Threaded drivers are used with blocking devices.

class SERVER_EXPORT ThreadedDriver : public DriverClientInterface, public RunnableInterface {

protected:
    Thread m_thread;
    Driver* m_driver;

    void setRealTime();

public:
    ThreadedDriver(Driver* driver);
    virtual ~ThreadedDriver();

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

    virtual bool isFixedBufferSize();
    virtual std::int32_t setBufferSize(NframesC bufferSize);
    virtual std::int32_t setSampleRate(NframesC sampleRate);

    virtual void setMaster(bool onoff);
    virtual bool getMaster();

    virtual void addSlave(DriverInterface* slave);
    virtual void removeSlave(DriverInterface* slave);

    virtual std::list<DriverInterface*> getSlaves();

    virtual std::int32_t processReadSlaves();
    virtual std::int32_t processWriteSlaves();

    virtual std::int32_t processRead();
    virtual std::int32_t processWrite();

    virtual std::int32_t processReadSync();
    virtual std::int32_t processWriteSync();

    virtual std::int32_t processReadAsync();
    virtual std::int32_t processWriteAsync();

    virtual std::int32_t clientNotify(std::int32_t refnum, const char* name, std::int32_t notify, std::int32_t sync, const char* message, std::int32_t value1, std::int32_t value2);
    virtual ClientControl* getClientControl() const;
    virtual bool isRealTime() const;
    virtual bool isRunning() const;

    // JackRunnableInterface interface
    virtual bool execute();
    virtual bool init();
};

} // namespace jackmini
