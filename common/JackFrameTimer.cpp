// Copyright (C) 2001 Paul Davis
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

#include "JackFrameTimer.h"
#include "JackError.h"
#include <cstdint>
#include <math.h>
#include <stdio.h>

namespace jackmini {

Timer::Timer()
{
    m_initialized = false;
    m_frames = 0;
    m_currentWakeup = 0;
    m_currentCallback = 0;
    m_nextWakeUp = 0;
    m_periodUsecs = 0.0f;
    m_filterOmega = 0.0f;  // Initialised later
}

NframesC Timer::time2Frames(TimeC usecs, NframesC bufferSize)
{
    if (m_initialized) {
        // Make sure we have signed differences. It would make a lot of sense
        // to use the standard signed intNN_t types everywhere  instead of e.g.
        // jack_nframes_t and jack_time_t. This would at least ensure that the
        // types used below are the correct ones. There is no way to get a type
        // that would be 'a signed version of jack_time_t' for example - the
        // types below are inherently fragile and there is no automatic way to
        // check they are the correct ones. The only way is to check manually
        // against jack/types.h.  FA - 16/02/2012
        std::int64_t du = usecs - m_currentWakeup;
        std::int64_t dp = m_nextWakeUp - m_currentWakeup;
        return m_frames + (std::int32_t)rint((double)du / (double)dp * bufferSize);
    } else {
        return 0;
    }
}

TimeC Timer::frames2Time(NframesC frames, NframesC bufferSize)
{
    if (m_initialized) {
        // Make sure we have signed differences. It would make a lot of sense
        // to use the standard signed intNN_t types everywhere  instead of e.g.
        // jack_nframes_t and jack_time_t. This would at least ensure that the
        // types used below are the correct ones. There is no way to get a type
        // that would be 'a signed version of jack_time_t' for example - the
        // types below are inherently fragile and there is no automatic way to
        // check they are the correct ones. The only way is to check manually
        // against jack/types.h.  FA - 16/02/2012
        std::int32_t df = frames - m_frames;
        std::int64_t dp = m_nextWakeUp - m_currentWakeup;
        return m_currentWakeup + (std::int64_t)rint((double)df * (double)dp / bufferSize);
    } else {
        return 0;
    }
}

std::int32_t Timer::getCycleTimes(NframesC* currentFrames, TimeC* currentUsecs, TimeC* nextUsecs, float* periodUsecs)
{
    if (m_initialized) {
        *currentFrames = m_frames;
        *currentUsecs = m_currentWakeup;
        *nextUsecs = m_nextWakeUp;
        *periodUsecs = m_periodUsecs;
        return 0;
    } else {
        return -1;
    }
}

NframesC Timer::framesSinceCycleStart(TimeC curTime, NframesC framesRate)
{
    return (NframesC)floor((((float)framesRate) / 1000000.0f) * (curTime - m_currentCallback));
}

void FrameTimer::initFrameTime()
{
    m_firstWakeUp = true;
}

void FrameTimer::incFrameTime(NframesC bufferSize, TimeC callbackUsecs, TimeC periodUsecs)
{
    if (m_firstWakeUp) {
        initFrameTimeAux(callbackUsecs, periodUsecs);
        m_firstWakeUp = false;
    }

    incFrameTimeAux(bufferSize, callbackUsecs, periodUsecs);
}

void FrameTimer::resetFrameTime(TimeC callbackUsecs)
{
    if (!m_firstWakeUp) { // ResetFrameTime may be called by a xrun/delayed wakeup on the first cycle
        Timer* timer = writeNextStateStart();
        timer->m_currentWakeup = callbackUsecs;
        timer->m_currentCallback = callbackUsecs;
        writeNextStateStop();
        trySwitchState(); // always succeed since there is only one writer
    }
}

// Use the state returned by ReadCurrentState and check that the state was not changed during the read operation.
// The operation is lock-free since there is no intermediate state in the write operation that could cause the
// read to loop forever.
void FrameTimer::readFrameTime(Timer* timer)
{
    UInt16 nextIndex = getCurrentIndex();
    UInt16 curIndex;
    do {
        curIndex = nextIndex;
        memcpy(timer, readCurrentState(), sizeof(Timer));
        nextIndex = getCurrentIndex();
    } while (curIndex != nextIndex); // Until a coherent state has been read
}

// Internal

void FrameTimer::initFrameTimeAux(TimeC callbackUsecs, TimeC periodUsecs)
{
    // the first wakeup or post-freewheeling or post-xrun

    // There seems to be no significant difference between
    // the two conditions OR-ed above. Incrementing the
    // frame_time after an xrun shouldn't harm, as there
    // will be a discontinuity anyway. So the two are
    // combined in this version.
    // FA 16/03/2012
    // Since the DLL *will* be run, next_wakeup should be the
    // current wakeup time *without* adding the period time, as
    // if it were computed in the previous period.
    // FA 16/03/2012
    // Added initialisation of timer->period_usecs, required
    // due to the modified implementation of the DLL itself.
    // OTOH, this should maybe not be repeated after e.g.
    // freewheeling or an xrun, as the current value would be
    // more accurate than the nominal one. But it doesn't really
    // harm either. Implementing this would require a new flag
    // in the engine structure, to be used after freewheeling
    // or an xrun instead of first_wakeup. I don't know if this
    // can be done without breaking compatibility, so I did not
    // add this
    // FA 13/02/2012
    // Added initialisation of timer->filter_omega. This makes
    // the DLL bandwidth independent of the actual period time.
    // The bandwidth is now 1/8 Hz in all cases. The value of
    // timer->filter_omega is 2 * pi * BW * Tperiod.
    // FA 13/02/2012

    Timer* timer = writeNextStateStart();
    timer->m_periodUsecs = (float)periodUsecs;
    timer->m_currentCallback = callbackUsecs;
    timer->m_nextWakeUp = callbackUsecs;
    timer->m_filterOmega = periodUsecs * 7.854e-7f;
    writeNextStateStop();
    trySwitchState(); // always succeed since there is only one writer
}

void FrameTimer::incFrameTimeAux(NframesC bufferSize, TimeC callbackUsecs, TimeC periodUsecs)
{
    Timer* timer = writeNextStateStart();

    // Modified implementation (the actual result is the same).
    //
    // 'fSecondOrderIntegrator' is renamed to 'fPeriodUsecs'
    // and now represents the DLL's best estimate of the
    // period time in microseconds (before it was a scaled
    // version of the difference w.r.t. the nominal value).
    // This allows this value to be made available to clients
    // that are interested in it (see jack_get_cycle_times).
    // This change also means that 'fPeriodUsecs' must be
    // initialised to the nominal period time instead of zero.
    // This is done in the first cycle in jack_run_cycle().
    //
    // 'fFilterCoefficient' is renamed to 'fFilterOmega'. It
    // is now equal to the 'omega' value as defined in the
    // 'Using a DLL to filter time' paper (before it was a
    // scaled version of this value). It is computed once in
    // jack_run_cycle() rather than set to a fixed value. This
    // makes the DLL bandwidth independent of the period time.
    //
    // FA 13/02/2012

    float delta = (float)((std::int64_t)callbackUsecs - (std::int64_t)timer->m_nextWakeUp);
    delta *= timer->m_filterOmega;
    timer->m_currentWakeup = timer->m_nextWakeUp;
    timer->m_currentCallback = callbackUsecs;
    timer->m_frames += bufferSize;
    timer->m_periodUsecs += timer->m_filterOmega * delta;
    timer->m_nextWakeUp += (std::int64_t)floorf(timer->m_periodUsecs + 1.41f * delta + 0.5f);
    timer->m_initialized = true;

    writeNextStateStop();
    trySwitchState(); // always succeed since there is only one writer
}

} // namespace jackmini
