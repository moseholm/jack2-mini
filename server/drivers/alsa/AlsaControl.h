// Copyright (C) 2026 JACK contributors
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

#include <alsa/asoundlib.h>
#include <cstdint>
#include <string>

namespace jackmini {

enum AlsaRateDirection {
    kRateCapture = 0,
    kRatePlayback = 1
};

// \brief An integer ALSA control element that carries a secondary card's rate.
//
// Elements are addressed by numid, which is what ALSA events report.

struct AlsaRateElement {
    std::uint32_t m_numid;
    std::uint32_t m_iface;
    std::uint32_t m_index;
    std::string m_name;
    bool m_user;
    bool m_writable;

    AlsaRateElement()
        : m_numid(0)
        , m_iface(0)
        , m_index(0)
        , m_user(false)
        , m_writable(false)
    {
    }
};

// Control device for a PCM name, e.g. "hw:1,0" -> "hw:1".
std::string alsaControlDeviceName(const std::string& pcm);

// Exclusive per-card lock, held for the life of a JACK server.  Returns the
// lock fd (>= 0) or -1 when the card is already held by another server or by
// another device of this server.  The kernel releases the lock if the process
// dies, so a crashed server never leaves the card locked.
std::int32_t alsaControlLockCard(const std::string& ctlName);
void alsaControlUnlockCard(std::int32_t fd);

// Element name JACK creates for a direction, e.g. "JACK Secondary Capture Rate".
// With a device number, the name carries it, e.g. "JACK Secondary Capture Rate d3".
std::string alsaRateElementName(AlsaRateDirection dir, std::int32_t device);

// Find an existing native rate element.  A configured name is used exclusively;
// otherwise common names are probed in order.  Returns 0 when an element was
// selected, 1 when none is suitable (the caller then creates a JACK element), and
// -1 with *error set when a configured element cannot be used.
std::int32_t alsaControlResolveRate(snd_ctl_t* ctl, AlsaRateDirection dir,
    const std::string& configured,
    AlsaRateElement* out, std::string* error);

// Create the JACK user element for a direction with the given initial rate.
// A stale element with no owner is replaced; one held by a live client is an error.
std::int32_t alsaControlCreateRate(snd_ctl_t* ctl, AlsaRateDirection dir, std::int32_t device, std::int64_t initial,
    AlsaRateElement* out);

std::int32_t alsaControlRemoveRate(snd_ctl_t* ctl, const AlsaRateElement& elem);
std::int32_t alsaControlReadRate(snd_ctl_t* ctl, const AlsaRateElement& elem, std::int64_t* value);
std::int32_t alsaControlWriteRate(snd_ctl_t* ctl, const AlsaRateElement& elem, std::int64_t value);

} // namespace jackmini
