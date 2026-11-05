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

#ifndef __JackAlsaSecondaryConfig__
#define __JackAlsaSecondaryConfig__

#include <string>
#include <vector>

namespace Jack
{

enum JackAlsaSecondaryMode {
    SecondaryModeDuplex = 0,
    SecondaryModeCapture,
    SecondaryModePlayback
};

/*!
\brief Description of one secondary ALSA card.

A card may be capture-only, playback-only or duplex.  The name is used as
the card component of the JACK port names (system:capture_<name>_<n>).
*/

struct JackAlsaSecondaryConfig {

    std::string name;
    std::string capture_device;
    std::string playback_device;
    JackAlsaSecondaryMode mode;

    int capture_channels;
    int playback_channels;

    // 0 means "use the primary value".
    int rate;
    int nperiods;

    bool optional;
    bool follow_hw_rate;

    // Optional native ALSA control element names.  Empty means auto-probe.
    std::string capture_rate_ctl;
    std::string playback_rate_ctl;

    // Optional mapping for enumerated native controls: "44100:0,48000:1".
    std::string rate_map;

    JackAlsaSecondaryConfig();
};

/*!
\brief Parse one --secondary specification.

The syntax is a semicolon separated list of key=value pairs:

    name=hw1;device=hw:1;mode=duplex;channels=2;rate=48000

A bare value without '=' is treated as device=<value>.  Values may be quoted.
Returns false and fills error on failure.
*/
bool JackAlsaSecondaryConfigParse(const std::string& spec,
                                  JackAlsaSecondaryConfig* config,
                                  std::string* error);

/*!
\brief Load a list of secondary cards from a file.

One card per line, using the same key=value syntax.  Blank lines and lines
whose first non-space character is '#' are ignored.
*/
bool JackAlsaSecondaryConfigLoadFile(const std::string& path,
                                     std::vector<JackAlsaSecondaryConfig>* configs,
                                     std::string* error);

/*!
\brief Turn an arbitrary string into a valid JACK port name component.
*/
std::string JackAlsaSecondarySanitizeName(const std::string& name);

} // end of namespace

#endif
