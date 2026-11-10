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
#include <string_view>
#include <vector>

namespace jackmini {

enum AlsaSecondaryMode {
    kSecondaryModeDuplex = 0,
    kSecondaryModeCapture,
    kSecondaryModePlayback
};

// Sample format for a secondary card.  Auto lets the ALSA core choose, which
// prefers 32-bit float.
enum AlsaSampleFormat {
    kSampleFormatAuto = 0,
    kSampleFormatFloat = 1,
    kSampleFormatS32 = 2,
    kSampleFormatS16 = 3
};

// Clock handling for a secondary card.  Async (default) runs a drift
// controller that keeps the card's ring near its target fill.  Sync holds a
// fixed ratio, which is only correct when the card's clock is locked to the
// primary.
enum AlsaSecondaryClock {
    kSecondaryClockAsync = 0,
    kSecondaryClockSync
};

// \brief Description of one secondary ALSA card.
//
// A card may be capture-only, playback-only or duplex.  The name is used as
// the card component of the JACK port names (system:capture_<name>_<n>).

struct AlsaSecondaryConfig {

    std::string m_name;
    std::string m_captureDevice;
    std::string m_playbackDevice;
    AlsaSecondaryMode m_mode;

    std::int32_t m_captureChannels;
    std::int32_t m_playbackChannels;

    // 0 means "use the primary value".
    std::int32_t m_rate;
    std::int32_t m_nperiods;

    bool m_optional;
    bool m_followHwRate;
    AlsaSecondaryClock m_clock;
    AlsaSampleFormat m_sampleFormat;

    // Resampler by name. "pipewire" is the only one.
    std::string m_resampler;

    // Added by discovery rather than configured.  A failure to open such a card is
    // logged and the card skipped, instead of failing the server.
    bool m_automatic;

    // Optional native ALSA control element names.  Empty means auto-probe.
    std::string m_captureRateCtl;
    std::string m_playbackRateCtl;

    // Optional mapping for enumerated native controls: "44100:0,48000:1".
    std::string m_rateMap;

    AlsaSecondaryConfig();
};

// \brief Parse one --secondary specification.
//
// The syntax is a semicolon separated list of key=value pairs:
//
// name=hw1;device=hw:1;mode=duplex;channels=2;rate=48000
//
// A bare value without '=' is treated as device=<value>.  Values may be quoted.
// Returns false and fills error on failure.
bool alsaSecondaryConfigParse(std::string_view spec,
    AlsaSecondaryConfig* config,
    std::string* error);

// \brief Load a list of secondary cards from a file.
//
// One card per line, using the same key=value syntax.  Blank lines and lines
// whose first non-space character is '#' are ignored.
bool alsaSecondaryConfigLoadFile(const std::string& path,
    std::vector<AlsaSecondaryConfig>* configs,
    std::string* error);

// \brief Turn an arbitrary string into a valid JACK port name component.
// Device number of a PCM name: "hw:2,3" -> 3, "hw:CARD=X,DEV=7" -> 7, "hw:X" -> 0.
std::int32_t alsaPcmDeviceNumber(std::string_view pcm);

// True if a PCM name names the card by its ALSA id: "hw:CARD=PCH", "hw:PCH,0",
// "plughw:CARD=PCH,DEV=0".  Numeric forms ("hw:0", "hw:2,0") are false.
bool alsaPcmNameIsById(std::string_view pcm);

// True if an ALSA card id is in the --exclude-card list.  Ids are compared without case.
bool alsaCardExcluded(std::string_view cardId, const std::vector<std::string>& excluded);

std::string alsaSecondarySanitizeName(std::string_view name);

} // namespace jackmini
