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

#include "JackDriver.h"
#include "JackPlatformPlug.h"

namespace jackmini {

// \brief The base class for threaded drivers using a "decorator" pattern. Threaded drivers are used with blocking devices.

class SERVER_EXPORT JackThreadedDriver : public JackDriverClientInterface, public JackRunnableInterface {

protected:
    JackThread fThread;
    JackDriver* fDriver;

    void SetRealTime();

public:
    JackThreadedDriver(JackDriver* driver);
    virtual ~JackThreadedDriver();

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

    virtual bool IsFixedBufferSize();
    virtual std::int32_t SetBufferSize(jack_nframes_t buffer_size);
    virtual std::int32_t SetSampleRate(jack_nframes_t sample_rate);

    virtual void SetMaster(bool onoff);
    virtual bool GetMaster();

    virtual void AddSlave(JackDriverInterface* slave);
    virtual void RemoveSlave(JackDriverInterface* slave);

    virtual std::list<JackDriverInterface*> GetSlaves();

    virtual std::int32_t ProcessReadSlaves();
    virtual std::int32_t ProcessWriteSlaves();

    virtual std::int32_t ProcessRead();
    virtual std::int32_t ProcessWrite();

    virtual std::int32_t ProcessReadSync();
    virtual std::int32_t ProcessWriteSync();

    virtual std::int32_t ProcessReadAsync();
    virtual std::int32_t ProcessWriteAsync();

    virtual std::int32_t ClientNotify(std::int32_t refnum, const char* name, std::int32_t notify, std::int32_t sync, const char* message, std::int32_t value1, std::int32_t value2);
    virtual JackClientControl* GetClientControl() const;
    virtual bool IsRealTime() const;
    virtual bool IsRunning() const;

    // JackRunnableInterface interface
    virtual bool Execute();
    virtual bool Init();
};

} // namespace jackmini
