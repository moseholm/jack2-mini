// Copyright (C) 2003 Paul Davis
// Copyright (C) 2004-2008 Grame
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

#include "JackEngineControl.h"
#include "JackClientControl.h"
#include "JackClientInterface.h"
#include "JackGraphManager.h"
#include <cstdint>
#include <math.h>
#include <algorithm>

namespace jackmini {

void JackEngineControl::CalcCPULoad(std::atomic<JackClientInterface*>* table,
    JackGraphManager* manager,
    jack_time_t cur_cycle_begin,
    jack_time_t prev_cycle_end)
{
    fPrevCycleTime = fCurCycleTime;
    fCurCycleTime = cur_cycle_begin;
    jack_time_t last_cycle_end = prev_cycle_end;

    // In Asynchronous mode, last cycle end is the max of client end dates
    if (!fSyncMode) {
        for (std::int32_t i = fDriverNum; i < kClientNum; i++) {
            JackClientInterface* client = table[i];
            JackClientTiming* timing = manager->GetClientTiming(i);
            if (client && client->GetClientControl()->fActive && timing->fStatus == Finished) {
                last_cycle_end = std::max(last_cycle_end, timing->fFinishedAt);
            }
        }
    }

    // Store the execution time for later averaging
    if (last_cycle_end > 0) {
        fRollingClientUsecs[fRollingClientUsecsIndex++] = last_cycle_end - fPrevCycleTime;
    }
    if (fRollingClientUsecsIndex >= kEngineRollingCount) {
        fRollingClientUsecsIndex = 0;
    }

    // Each time we have a full set of iterations, recompute the current
    // usage from the latest JACK_ENGINE_ROLLING_COUNT client entries.
    if (fRollingClientUsecsCnt && (fRollingClientUsecsIndex == 0 || fRollingClientUsecsCnt == fRollingInterval)) {
        jack_time_t avg_usecs = 0;
        jack_time_t max_usecs = 0;

        for (std::int32_t i = 0; i < kEngineRollingCount; i++) {
            avg_usecs += fRollingClientUsecs[i]; // This is really a running total to be averaged later
            max_usecs = std::max(fRollingClientUsecs[i], max_usecs);
        }

        fMaxUsecs = std::max(fMaxUsecs, max_usecs);

        if (fRollingClientUsecsCnt == fRollingInterval) {
            fMaxUsecs = max_usecs;
            fRollingClientUsecsCnt = 0;
        }

        if (max_usecs < ((fPeriodUsecs * 95) / 100)) {
            // Average the values from our JACK_ENGINE_ROLLING_COUNT array
            fSpareUsecs = (jack_time_t)(fPeriodUsecs - (avg_usecs / kEngineRollingCount));
        } else {
            // Use the 'worst case' value (or zero if we exceeded 'fPeriodUsecs')
            fSpareUsecs = jack_time_t((max_usecs < fPeriodUsecs) ? fPeriodUsecs - max_usecs : 0);
        }

        fCPULoad = ((1.f - (float(fSpareUsecs) / float(fPeriodUsecs))) * 50.f + (fCPULoad * 0.5f));
        fMaxCPULoad = (1.f - float(fMaxUsecs < fPeriodUsecs ? fPeriodUsecs - fMaxUsecs : 0) / float(fPeriodUsecs)) * 100.f;
    }

    fRollingClientUsecsCnt++;
}

void JackEngineControl::ResetRollingUsecs()
{
    memset(fRollingClientUsecs.data(), 0, sizeof(fRollingClientUsecs));
    fRollingClientUsecsIndex = 0;
    fRollingClientUsecsCnt = 0;
    fSpareUsecs = 0;
    fRollingInterval = std::int32_t(floor((kEngineRollingInterval * 1000.f) / fPeriodUsecs));
}

void JackEngineControl::NotifyXRun(jack_time_t callback_usecs, float delayed_usecs)
{
    ResetFrameTime(callback_usecs);
    fXrunDelayedUsecs = delayed_usecs;
    if (delayed_usecs > fMaxDelayedUsecs) {
        fMaxDelayedUsecs = delayed_usecs;
    }
}

} // namespace jackmini
