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

#include <cstdint>
#include <string>
#include <vector>

namespace jackmini {

// \brief One PCM device (subdevice 0) of an ALSA card.

struct AlsaPcmInfo {
    std::string m_cardId; // ALSA card id, e.g. "NVidia"
    std::int32_t m_cardNum; // card index when enumerated
    std::int32_t m_device; // PCM device number
    std::string m_pcm; // stable name, "hw:CARD=<id>,DEV=<n>"
    bool m_playback; // has a playback stream
    bool m_capture; // has a capture stream
    std::int32_t m_playbackChannels; // maximum channels, 0 if no playback stream
    std::int32_t m_captureChannels; // maximum channels, 0 if no capture stream
};

// Lists every PCM device that has a stream, on every card.
//
// A device that cannot be opened right now (it is held by a stream of this or
// another application) keeps the channel counts it had in \a known, the previous
// listing, so a device in use is not taken to be gone.  A device with no known
// counts and no open is left out.  \a report turns the log lines on.
std::vector<AlsaPcmInfo> alsaEnumeratePcms(const std::vector<AlsaPcmInfo>& known,
    bool report);

} // namespace jackmini
