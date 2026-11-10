// Copyright (C) 2001-2003 Paul Davis
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

#include "JackPort.h"
#include "JackCString.h"
#include "JackError.h"
#include "JackPortType.h"
#include <cstdint>
#include <assert.h>
#include <array>
#include <format>
#include <span>
#include <string_view>

namespace jackmini {

ServerPort::ServerPort()
{
    release();
}

bool ServerPort::allocate(std::int32_t refnum, const char* portName, const char* portType, PortFlagsC flags)
{
    PortTypeIdC id = getPortTypeId(portType);
    assert(id >= 0 && id <= PORT_TYPES_MAX);
    if (id == gPORTTYPESMAX) {
        return false;
    }
    m_typeId = id;
    m_flags = flags;
    m_refNum = refnum;
    copyCString(m_name, portName);
    m_inUse = true;
    m_latency = 0;
    m_totalLatency = 0;
    m_monitorRequests = 0;
    m_playbackLatency.m_min = m_playbackLatency.m_max = 0;
    m_captureLatency.m_min = m_captureLatency.m_max = 0;
    m_tied = kNoPort;
    m_alias1[0] = '\0';
    m_alias2[0] = '\0';
    // DB: At this point we do not know current buffer size in frames,
    // but every time buffer will be returned to any user,
    // it will be called with either ClearBuffer or MixBuffers
    // with correct current buffer size.
    // So it is safe to init with 0 here.
    clearBuffer(0);
    return true;
}

void ServerPort::release()
{
    m_typeId = 0;
    m_flags = kPortIsInput;
    m_refNum = -1;
    m_uUID = 0;
    m_inUse = false;
    m_latency = 0;
    m_totalLatency = 0;
    m_monitorRequests = 0;
    m_playbackLatency.m_min = m_playbackLatency.m_max = 0;
    m_captureLatency.m_min = m_captureLatency.m_max = 0;
    m_tied = kNoPort;
    m_alias1[0] = '\0';
    m_alias2[0] = '\0';
}

std::int32_t ServerPort::getRefNum() const
{
    return m_refNum;
}

NframesC ServerPort::getLatency() const
{
    return m_latency;
}

NframesC ServerPort::getTotalLatency() const
{
    return m_totalLatency;
}

void ServerPort::setLatency(NframesC nframes)
{
    m_latency = nframes;

    // setup the new latency values here,
    // so we don't need to change the backend codes.
    if (m_flags & kPortIsOutput) {
        m_captureLatency.m_min = nframes;
        m_captureLatency.m_max = nframes;
    }
    if (m_flags & kPortIsInput) {
        m_playbackLatency.m_min = nframes;
        m_playbackLatency.m_max = nframes;
    }
}

void ServerPort::setLatencyRange(LatencyCallbackModeC mode, LatencyRangeC* range)
{
    if (mode == kCaptureLatency) {
        m_captureLatency = *range;

        // hack to set latency up for
        // backend ports
        if ((m_flags & kPortIsOutput) && (m_flags & kPortIsPhysical)) {
            m_latency = (range->m_min + range->m_max) / 2;
        }
    } else {
        m_playbackLatency = *range;

        // hack to set latency up for
        // backend ports
        if ((m_flags & kPortIsInput) && (m_flags & kPortIsPhysical)) {
            m_latency = (range->m_min + range->m_max) / 2;
        }
    }
}

void ServerPort::getLatencyRange(LatencyCallbackModeC mode, LatencyRangeC* range) const
{
    if (mode == kCaptureLatency) {
        *range = m_captureLatency;
    } else {
        *range = m_playbackLatency;
    }
}

std::int32_t ServerPort::tie(PortIdC portIndex)
{
    m_tied = portIndex;
    return 0;
}

std::int32_t ServerPort::unTie()
{
    m_tied = kNoPort;
    return 0;
}

std::int32_t ServerPort::requestMonitor(bool onoff)
{
    // jackd.h
    // * If @ref JackPortCanMonitor is set for this @a port, turn input
    // * monitoring on or off. Otherwise, do nothing.
    //
    // if (!(fFlags & JackPortCanMonitor))
    // return -1;

    if (onoff) {
        m_monitorRequests++;
    } else if (m_monitorRequests) {
        m_monitorRequests--;
    }

    return 0;
}

std::int32_t ServerPort::ensureMonitor(bool onoff)
{
    // jackd.h
    // * If @ref JackPortCanMonitor is set for this @a port, turn input
    // * monitoring on or off. Otherwise, do nothing.
    //
    // if (!(fFlags & JackPortCanMonitor))
    // return -1;

    if (onoff) {
        if (m_monitorRequests == 0) {
            m_monitorRequests++;
        }
    } else {
        if (m_monitorRequests > 0) {
            m_monitorRequests = 0;
        }
    }

    return 0;
}

const char* ServerPort::getName() const
{
    return m_name.data();
}

const char* ServerPort::getShortName() const
{
    // we know there is always a colon, because we put
    // it there ...
    return strchr(m_name.data(), ':') + 1;
}

std::int32_t ServerPort::getFlags() const
{
    return m_flags;
}

const char* ServerPort::getType() const
{
    const PortType* type = getPortType(m_typeId);
    return type->m_name;
}

void ServerPort::setName(const char* newName)
{
    char* colon = strchr(m_name.data(), ':');
    std::int32_t len = sizeof(m_name) - ((std::int32_t)(colon - m_name.data())) - 2;
    copyCString(std::span<char>(colon + 1, len), newName);
}

bool ServerPort::nameEquals(std::string_view target)
{
    std::array<char, kRealJackPortNameSize + 1> buf { };

    // this nasty, nasty kludge is here because between 0.109.0 and 0.109.1,
    // the ALSA audio backend had the name "ALSA", whereas as before and
    // after it, it was called "alsa_pcm". this stops breakage for
    // any setups that have saved "alsa_pcm" or "ALSA" in their connection
    // state.

    if (target.starts_with("ALSA:capture") || target.starts_with("ALSA:playback")) {
        formatCString(buf, "alsa_pcm{}", target.substr(4));
        target = buf.data();
    }

    return (std::string_view(m_name) == target
        || std::string_view(m_alias1) == target
        || std::string_view(m_alias2) == target);
}

std::int32_t ServerPort::getAliases(char* const aliases[2])
{
    std::int32_t cnt = 0;

    if (m_alias1[0] != '\0') {
        copyCString(std::span<char>(aliases[0], kRealJackPortNameSize), m_alias1.data());
        cnt++;
    }

    if (m_alias2[0] != '\0') {
        copyCString(std::span<char>(aliases[1], kRealJackPortNameSize), m_alias2.data());
        cnt++;
    }

    return cnt;
}

std::int32_t ServerPort::setAlias(const char* alias)
{
    if (m_alias1[0] == '\0') {
        copyCString(m_alias1, alias);
    } else if (m_alias2[0] == '\0') {
        copyCString(m_alias2, alias);
    } else {
        return -1;
    }

    return 0;
}

std::int32_t ServerPort::unsetAlias(const char* alias)
{
    if (strcmp(m_alias1.data(), alias) == 0) {
        m_alias1[0] = '\0';
    } else if (strcmp(m_alias2.data(), alias) == 0) {
        m_alias2[0] = '\0';
    } else {
        return -1;
    }

    return 0;
}

void ServerPort::clearBuffer(NframesC frames)
{
    const PortType* type = getPortType(m_typeId);
    (type->m_init)(getBuffer(), frames * sizeof(DefaultAudioSampleC), frames);
}

void ServerPort::mixBuffers(void** srcBuffers, std::int32_t srcCount, NframesC bufferSize)
{
    const PortType* type = getPortType(m_typeId);
    (type->m_mixdown)(getBuffer(), srcBuffers, srcCount, bufferSize);
}

} // namespace jackmini
