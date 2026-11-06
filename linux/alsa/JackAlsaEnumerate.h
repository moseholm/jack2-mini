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

#ifndef __JackAlsaEnumerate__
#define __JackAlsaEnumerate__

#include <string>
#include <vector>

namespace Jack
{

/*!
\brief One PCM device (subdevice 0) of an ALSA card.
*/

struct JackAlsaPcmInfo {
    std::string card_id;       // ALSA card id, e.g. "NVidia"
    int card_num;              // card index when enumerated
    int device;                // PCM device number
    std::string pcm;           // stable name, "hw:CARD=<id>,DEV=<n>"
    bool playback;             // has a playback stream
    bool capture;              // has a capture stream
    int playback_channels;     // maximum channels, 0 if no playback stream
    int capture_channels;      // maximum channels, 0 if no capture stream
};

/*
Lists every PCM device that has a stream, on every card.  Devices that cannot
be opened right now (for example an HDMI output with a stream busy elsewhere)
are left out, with a log line.
*/
std::vector<JackAlsaPcmInfo> JackAlsaEnumeratePcms();

} // end of namespace

#endif
