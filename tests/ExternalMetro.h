// Copyright (C) 2001 Paul Davis
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

#include <cstdint>

#include "jackmini/Jackmini.hpp"

// \brief A class to test external clients

class ExternalMetro {

public:
    jackmini::Client m_client;
    jackmini::Port m_inputPort;
    jackmini::Port m_outputPort;

    std::uint64_t m_sr;
    std::int32_t m_freq;
    std::int32_t m_bpm;
    jackmini::FrameCount m_toneLength, m_waveLength;
    float* m_wave;
    double* m_amp;
    std::int64_t m_offset;

    ExternalMetro(std::int32_t freq, double maxAmp, std::int32_t durArg, std::int32_t bpm, const char* clientName = "metro");
    virtual ~ExternalMetro();

private:
    bool processAudio(jackmini::FrameCount nframes);
};
