// Copyright (C) 2001 Paul Davis
// Copyright (C) 2004-2008 Grame
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation; either version 2.1 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.

#pragma once

#include "JackCompilerDeps.h"
#include "JackConstants.h"
#include "types.h"
#include <array>
#include <cstdint>
#include <string_view>

namespace jackmini {

#define ALL_PORTS 0xFFFF
#define NO_PORT 0xFFFE

// \brief Base class for port.


class SERVER_EXPORT JackPort {

    friend class JackGraphManager;

private:
    std::int32_t fTypeId;
    enum JackPortFlags fFlags;
    std::array<char, REAL_JACK_PORT_NAME_SIZE + 1> fName;
    std::array<char, REAL_JACK_PORT_NAME_SIZE + 1> fAlias1;
    std::array<char, REAL_JACK_PORT_NAME_SIZE + 1> fAlias2;
    std::int32_t fRefNum;
    jack_uuid_t fUUID; // set when the port is allocated; the UUID is reported by the client tools

    jack_nframes_t fLatency;
    jack_nframes_t fTotalLatency;
    jack_latency_range_t fPlaybackLatency;
    jack_latency_range_t fCaptureLatency;
    std::uint8_t fMonitorRequests;

    bool fInUse;
    jack_port_id_t fTied; // Locally tied source port
    std::array<jack_default_audio_sample_t, BUFFER_SIZE_MAX + 8> fBuffer;

    bool IsUsed() const
    {
        return fInUse;
    }

    // RT
    void ClearBuffer(jack_nframes_t frames);
    void MixBuffers(void** src_buffers, std::int32_t src_count, jack_nframes_t frames);

public:
    JackPort();

    bool Allocate(std::int32_t refnum, const char* port_name, const char* port_type, JackPortFlags flags);
    void Release();
    const char* GetName() const;
    const char* GetShortName() const;
    void SetName(const char* name);

    std::int32_t GetAliases(char* const aliases[2]);
    std::int32_t SetAlias(const char* alias);
    std::int32_t UnsetAlias(const char* alias);
    bool NameEquals(std::string_view target);

    std::int32_t GetFlags() const;
    const char* GetType() const;

    std::int32_t Tie(jack_port_id_t port_index);
    std::int32_t UnTie();

    jack_nframes_t GetLatency() const;
    void SetLatency(jack_nframes_t latency);

    void SetLatencyRange(jack_latency_callback_mode_t mode, jack_latency_range_t* range);
    void GetLatencyRange(jack_latency_callback_mode_t mode, jack_latency_range_t* range) const;

    jack_nframes_t GetTotalLatency() const;

    std::int32_t RequestMonitor(bool onoff);
    std::int32_t EnsureMonitor(bool onoff);
    bool MonitoringInput()
    {
        return (fMonitorRequests > 0);
    }

    // Since we are in shared memory, the resulting pointer cannot be cached, so align it here...
    jack_default_audio_sample_t* GetBuffer()
    {
        return (jack_default_audio_sample_t*)((std::uintptr_t)fBuffer.data() & ~31L) + 8;
    }

    std::int32_t GetRefNum() const;
    jack_uuid_t GetUUID() const
    {
        return fUUID;
    }
    bool InUse() const
    {
        return fInUse;
    }
};

} // namespace jackmini
