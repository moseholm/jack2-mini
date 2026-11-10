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

namespace jackmini {

// \brief The base class for audio drivers: drivers with audio ports.
//
// A concrete derived class will have to be defined with a real audio driver API,
// either callback based ones or blocking ones (like ALSA).
//
// Most of the generic audio handing code is part of this class :
// - concrete callback basedd derived subclasses typically have to Open/Close the underlying audio API,
// setup the audio callback and implement the Read/Write methods
// - concrete blocking based derived subclasses typically have to Open/Close the underlying audio API,
// implement the Read/Write methods and "wraps" the driver with the JackThreadDriver class.

class SERVER_EXPORT AudioDriver : public Driver {

protected:
    DefaultAudioSampleC* getInputBuffer(std::int32_t portIndex);
    DefaultAudioSampleC* getOutputBuffer(std::int32_t portIndex);
    DefaultAudioSampleC* getMonitorBuffer(std::int32_t portIndex);

    void handleLatencyCallback(std::int32_t status);
    virtual void updateLatencies();

    std::int32_t processAsync();
    void processGraphAsync();
    void processGraphAsyncMaster();
    void processGraphAsyncSlave();

    std::int32_t processSync();
    void processGraphSync();
    void processGraphSyncMaster();
    void processGraphSyncSlave();

public:
    AudioDriver(const char* name, const char* alias, LockedEngine* engine, Synchro* table);
    virtual ~AudioDriver();

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

    // To be called by the underlying driver audio callback, or possibly by a RT thread (using JackThreadedDriver decorator)
    // when a blocking read/write underlying API is used (like ALSA)
    virtual std::int32_t process();

    virtual std::int32_t attach();
    virtual std::int32_t detach();

    virtual std::int32_t write();

    virtual std::int32_t setBufferSize(NframesC bufferSize);
    virtual std::int32_t setSampleRate(NframesC sampleRate);

    virtual std::int32_t clientNotify(std::int32_t refnum, const char* name, std::int32_t notify, std::int32_t sync, const char* message, std::int32_t value1, std::int32_t value2);
};

} // namespace jackmini
