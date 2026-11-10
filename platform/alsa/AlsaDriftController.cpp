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

#include "AlsaDriftController.h"

namespace jackmini {

static double clamp(double v, double limit)
{
    if (v > limit) {
        return limit;
    }
    if (v < -limit) {
        return -limit;
    }
    return v;
}

AlsaDriftController::AlsaDriftController(double kp, double ki,
    double maxCorrection, double slew)
    : m_kp(kp)
    , m_ki(ki)
    , m_maxCorrection(maxCorrection)
    , m_slew(slew)
    , m_integral(0.0)
    , m_applied(0.0)
{
}

void AlsaDriftController::reset()
{
    m_integral = 0.0;
    m_applied = 0.0;
}

double AlsaDriftController::update(double fill, double target, double period)
{
    if (period <= 0.0) {
        return 1.0 + m_applied;
    }

    double error = (fill - target) / period;

    m_integral = clamp(m_integral + m_ki * error, m_maxCorrection);
    double wanted = clamp(-(m_kp * error + m_integral), m_maxCorrection);

    double step = clamp(wanted - m_applied, m_slew);
    m_applied = clamp(m_applied + step, m_maxCorrection);

    return 1.0 + m_applied;
}

} // namespace jackmini
