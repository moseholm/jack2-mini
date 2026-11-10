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

namespace jackmini {

// \brief PI controller that keeps a secondary card's ring near its target fill.
//
// The input is the ring fill measured on the primary RT thread.  The output is
// a resampler rate (1.0 is nominal).  When the ring holds more than its target,
// the card is running faster than the primary clock, so the resampler consumes
// input a little faster.  Corrections are limited to +/- max_correction and move
// by at most slew per cycle, so the converter never jumps.

class JackAlsaDriftController {

private:
    double fKp;
    double fKi;
    double fMaxCorrection;
    double fSlew;

    double fIntegral;
    double fApplied;

public:
    // kp and ki are per period of fill error.  The defaults were chosen by
    // simulating the capture ring with the real converter: they settle to
    // the drift without oscillating.
    JackAlsaDriftController(double kp = 0.02, double ki = 2e-4,
        double max_correction = 1000e-6, double slew = 20e-6);

    void Reset();

    // fill and target are in frames, period in frames per cycle.
    // Returns the resampler rate to apply, 1.0 being nominal.
    double Update(double fill, double target, double period);

    // Correction currently applied, as a signed fraction (1e-6 == 1 ppm).
    double Correction() const { return fApplied; }
};

} // namespace jackmini
