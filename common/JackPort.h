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

inline constexpr std::int32_t kAllPorts = 0xFFFF;

// \brief Base class for port.


class SERVER_EXPORT ServerPort {

    friend class GraphManager;

private:
    std::int32_t m_typeId;
    enum PortFlagsC m_flags;
    std::array<char, kRealJackPortNameSize + 1> m_name;
    std::array<char, kRealJackPortNameSize + 1> m_alias1;
    std::array<char, kRealJackPortNameSize + 1> m_alias2;
    std::int32_t m_refNum;
    UuidC m_uUID; // set when the port is allocated; the UUID is reported by the client tools

    NframesC m_latency;
    NframesC m_totalLatency;
    LatencyRangeC m_playbackLatency;
    LatencyRangeC m_captureLatency;
    std::uint8_t m_monitorRequests;

    bool m_inUse;
    PortIdC m_tied; // Locally tied source port
    std::array<DefaultAudioSampleC, kBufferSizeMax + 8> m_buffer;

    bool isUsed() const
    {
        return m_inUse;
    }

    // RT
    void clearBuffer(NframesC frames);
    void mixBuffers(void** srcBuffers, std::int32_t srcCount, NframesC frames);

public:
    ServerPort();

    bool allocate(std::int32_t refnum, const char* portName, const char* portType, PortFlagsC flags);
    void release();
    const char* getName() const;
    const char* getShortName() const;
    void setName(const char* name);

    std::int32_t getAliases(char* const aliases[2]);
    std::int32_t setAlias(const char* alias);
    std::int32_t unsetAlias(const char* alias);
    bool nameEquals(std::string_view target);

    std::int32_t getFlags() const;
    const char* getType() const;

    std::int32_t tie(PortIdC portIndex);
    std::int32_t unTie();

    NframesC getLatency() const;
    void setLatency(NframesC latency);

    void setLatencyRange(LatencyCallbackModeC mode, LatencyRangeC* range);
    void getLatencyRange(LatencyCallbackModeC mode, LatencyRangeC* range) const;

    NframesC getTotalLatency() const;

    std::int32_t requestMonitor(bool onoff);
    std::int32_t ensureMonitor(bool onoff);
    bool monitoringInput()
    {
        return (m_monitorRequests > 0);
    }

    // Since we are in shared memory, the resulting pointer cannot be cached, so align it here...
    DefaultAudioSampleC* getBuffer()
    {
        return (DefaultAudioSampleC*)((std::uintptr_t)m_buffer.data() & ~31L) + 8;
    }

    std::int32_t getRefNum() const;
    UuidC getUUID() const
    {
        return m_uUID;
    }
    bool inUse() const
    {
        return m_inUse;
    }
};

} // namespace jackmini
