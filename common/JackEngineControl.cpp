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

void EngineControl::calcCPULoad(std::atomic<ClientInterface*>* table,
    GraphManager* manager,
    TimeC curCycleBegin,
    TimeC prevCycleEnd)
{
    m_prevCycleTime = m_curCycleTime;
    m_curCycleTime = curCycleBegin;
    TimeC lastCycleEnd = prevCycleEnd;

    // In Asynchronous mode, last cycle end is the max of client end dates
    if (!m_syncMode) {
        for (std::int32_t i = m_driverNum; i < kClientNum; i++) {
            ClientInterface* client = table[i];
            ClientTiming* timing = manager->getClientTiming(i);
            if (client && client->getClientControl()->m_active && timing->m_status == kFinished) {
                lastCycleEnd = std::max(lastCycleEnd, timing->m_finishedAt);
            }
        }
    }

    // Store the execution time for later averaging
    if (lastCycleEnd > 0) {
        m_rollingClientUsecs[m_rollingClientUsecsIndex++] = lastCycleEnd - m_prevCycleTime;
    }
    if (m_rollingClientUsecsIndex >= kEngineRollingCount) {
        m_rollingClientUsecsIndex = 0;
    }

    // Each time we have a full set of iterations, recompute the current
    // usage from the latest JACK_ENGINE_ROLLING_COUNT client entries.
    if (m_rollingClientUsecsCnt && (m_rollingClientUsecsIndex == 0 || m_rollingClientUsecsCnt == m_rollingInterval)) {
        TimeC avgUsecs = 0;
        TimeC maxUsecs = 0;

        for (std::int32_t i = 0; i < kEngineRollingCount; i++) {
            avgUsecs += m_rollingClientUsecs[i]; // This is really a running total to be averaged later
            maxUsecs = std::max(m_rollingClientUsecs[i], maxUsecs);
        }

        m_maxUsecs = std::max(m_maxUsecs, maxUsecs);

        if (m_rollingClientUsecsCnt == m_rollingInterval) {
            m_maxUsecs = maxUsecs;
            m_rollingClientUsecsCnt = 0;
        }

        if (maxUsecs < ((m_periodUsecs * 95) / 100)) {
            // Average the values from our JACK_ENGINE_ROLLING_COUNT array
            m_spareUsecs = (TimeC)(m_periodUsecs - (avgUsecs / kEngineRollingCount));
        } else {
            // Use the 'worst case' value (or zero if we exceeded 'fPeriodUsecs')
            m_spareUsecs = TimeC((maxUsecs < m_periodUsecs) ? m_periodUsecs - maxUsecs : 0);
        }

        m_cPULoad = ((1.f - (float(m_spareUsecs) / float(m_periodUsecs))) * 50.f + (m_cPULoad * 0.5f));
        m_maxCPULoad = (1.f - float(m_maxUsecs < m_periodUsecs ? m_periodUsecs - m_maxUsecs : 0) / float(m_periodUsecs)) * 100.f;
    }

    m_rollingClientUsecsCnt++;
}

void EngineControl::resetRollingUsecs()
{
    memset(m_rollingClientUsecs.data(), 0, sizeof(m_rollingClientUsecs));
    m_rollingClientUsecsIndex = 0;
    m_rollingClientUsecsCnt = 0;
    m_spareUsecs = 0;
    m_rollingInterval = std::int32_t(floor((kEngineRollingInterval * 1000.f) / m_periodUsecs));
}

void EngineControl::notifyXRun(TimeC callbackUsecs, float delayedUsecs)
{
    resetFrameTime(callbackUsecs);
    m_xrunDelayedUsecs = delayedUsecs;
    if (delayedUsecs > m_maxDelayedUsecs) {
        m_maxDelayedUsecs = delayedUsecs;
    }
}

} // namespace jackmini
