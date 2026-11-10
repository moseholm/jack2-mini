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

#include "jackmini/jackmini.hpp"

// \brief A class to test external clients

class ExternalMetro {

public:
    jackmini::Client client;
    jackmini::Port input_port;
    jackmini::Port output_port;

    unsigned long sr;
    int freq;
    int bpm;
    jackmini::FrameCount tone_length, wave_length;
    float* wave;
    double* amp;
    long offset;

    ExternalMetro(int freq, double max_amp, int dur_arg, int bpm, const char* client_name = "metro");
    virtual ~ExternalMetro();

private:
    bool process_audio(jackmini::FrameCount nframes);
};
