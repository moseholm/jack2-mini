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

class SERVER_EXPORT JackAudioDriver : public JackDriver {

protected:
    jack_default_audio_sample_t* GetInputBuffer(std::int32_t port_index);
    jack_default_audio_sample_t* GetOutputBuffer(std::int32_t port_index);
    jack_default_audio_sample_t* GetMonitorBuffer(std::int32_t port_index);

    void HandleLatencyCallback(std::int32_t status);
    virtual void UpdateLatencies();

    std::int32_t ProcessAsync();
    void ProcessGraphAsync();
    void ProcessGraphAsyncMaster();
    void ProcessGraphAsyncSlave();

    std::int32_t ProcessSync();
    void ProcessGraphSync();
    void ProcessGraphSyncMaster();
    void ProcessGraphSyncSlave();

public:
    JackAudioDriver(const char* name, const char* alias, JackLockedEngine* engine, JackSynchro* table);
    virtual ~JackAudioDriver();

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

    // To be called by the underlying driver audio callback, or possibly by a RT thread (using JackThreadedDriver decorator)
    // when a blocking read/write underlying API is used (like ALSA)
    virtual std::int32_t Process();

    virtual std::int32_t Attach();
    virtual std::int32_t Detach();

    virtual std::int32_t Write();

    virtual std::int32_t SetBufferSize(jack_nframes_t buffer_size);
    virtual std::int32_t SetSampleRate(jack_nframes_t sample_rate);

    virtual std::int32_t ClientNotify(std::int32_t refnum, const char* name, std::int32_t notify, std::int32_t sync, const char* message, std::int32_t value1, std::int32_t value2);
};

} // namespace jackmini
