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

#include "JackAlsaDriftController.h"

namespace jackmini
{

static double Clamp(double v, double limit)
{
    if (v > limit) {
        return limit;
    }
    if (v < -limit) {
        return -limit;
    }
    return v;
}

JackAlsaDriftController::JackAlsaDriftController(double kp, double ki,
                                                 double max_correction, double slew)
    : fKp(kp), fKi(ki), fMaxCorrection(max_correction), fSlew(slew),
      fIntegral(0.0), fApplied(0.0)
{}

void JackAlsaDriftController::Reset()
{
    fIntegral = 0.0;
    fApplied = 0.0;
}

double JackAlsaDriftController::Update(double fill, double target, double period)
{
    if (period <= 0.0) {
        return 1.0 + fApplied;
    }

    double error = (fill - target) / period;

    fIntegral = Clamp(fIntegral + fKi * error, fMaxCorrection);
    double wanted = Clamp(-(fKp * error + fIntegral), fMaxCorrection);

    double step = Clamp(wanted - fApplied, fSlew);
    fApplied = Clamp(fApplied + step, fMaxCorrection);

    return 1.0 + fApplied;
}

} // end of namespace
