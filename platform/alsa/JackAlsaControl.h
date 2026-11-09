/*
Copyright (C) 2026 JACK contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#pragma once

#include <alsa/asoundlib.h>
#include <string>

namespace jackmini {

enum JackAlsaRateDirection {
    RateCapture = 0,
    RatePlayback = 1
};

/*!
\brief An integer ALSA control element that carries a secondary card's rate.

Elements are addressed by numid, which is what ALSA events report.
*/

struct JackAlsaRateElement {
    unsigned int numid;
    unsigned int iface;
    unsigned int index;
    std::string name;
    bool user;
    bool writable;

    JackAlsaRateElement()
        : numid(0)
        , iface(0)
        , index(0)
        , user(false)
        , writable(false)
    {
    }
};

// Control device for a PCM name, e.g. "hw:1,0" -> "hw:1".
std::string JackAlsaControlDeviceName(const std::string& pcm);

/*
Exclusive per-card lock, held for the life of a JACK server.  Returns the
lock fd (>= 0) or -1 when the card is already held by another server or by
another device of this server.  The kernel releases the lock if the process
dies, so a crashed server never leaves the card locked.
*/
int JackAlsaControlLockCard(const std::string& ctl_name);
void JackAlsaControlUnlockCard(int fd);

// Element name JACK creates for a direction, e.g. "JACK Secondary Capture Rate".
// With a device number, the name carries it, e.g. "JACK Secondary Capture Rate d3".
std::string JackAlsaRateElementName(JackAlsaRateDirection dir, int device);

/*
Find an existing native rate element.  A configured name is used exclusively;
otherwise common names are probed in order.  Returns 0 when an element was
selected, 1 when none is suitable (the caller then creates a JACK element), and
-1 with *error set when a configured element cannot be used.
*/
int JackAlsaControlResolveRate(snd_ctl_t* ctl, JackAlsaRateDirection dir,
    const std::string& configured,
    JackAlsaRateElement* out, std::string* error);

// Create the JACK user element for a direction with the given initial rate.
// A stale element with no owner is replaced; one held by a live client is an error.
int JackAlsaControlCreateRate(snd_ctl_t* ctl, JackAlsaRateDirection dir, int device, long initial,
    JackAlsaRateElement* out);

int JackAlsaControlRemoveRate(snd_ctl_t* ctl, const JackAlsaRateElement& elem);
int JackAlsaControlReadRate(snd_ctl_t* ctl, const JackAlsaRateElement& elem, long* value);
int JackAlsaControlWriteRate(snd_ctl_t* ctl, const JackAlsaRateElement& elem, long value);

} // namespace jackmini
