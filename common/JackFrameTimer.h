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

#pragma once

#include <cstdint>

#include "JackAtomicState.h"
#include "JackCompilerDeps.h"
#include "types.h"

namespace jackmini {

// \brief A structure used for time management.


class SERVER_EXPORT Timer {

    friend class FrameTimer;

private:
    NframesC m_frames;
    TimeC m_currentWakeup;
    TimeC m_currentCallback;
    TimeC m_nextWakeUp;
    float m_periodUsecs;
    float m_filterOmega;  // set once, never altered
    bool m_initialized;

public:
    Timer();
    ~Timer()
    {
    }

    NframesC time2Frames(TimeC time, NframesC bufferSize);
    TimeC frames2Time(NframesC frames, NframesC bufferSize);
    NframesC framesSinceCycleStart(TimeC curTime, NframesC framesRate);
    std::int32_t getCycleTimes(NframesC* currentFrames, TimeC* currentUsecs, TimeC* nextUsecs, float* periodUsecs);

    NframesC curFrame()
    {
        return m_frames;
    }

    TimeC curTime()
    {
        return m_currentWakeup;
    }
};

// \brief A class using the JackAtomicState to manage jack time.


class SERVER_EXPORT FrameTimer : public AtomicState<Timer> {

private:
    bool m_firstWakeUp;
    void incFrameTimeAux(NframesC bufferSize, TimeC callbackUsecs, TimeC periodUsecs);
    void initFrameTimeAux(TimeC callbackUsecs, TimeC periodUsecs);

public:
    FrameTimer()
        : m_firstWakeUp(true)
    {
    }
    ~FrameTimer()
    {
    }

    void initFrameTime();
    void resetFrameTime(TimeC callbackUsecs);
    void incFrameTime(NframesC bufferSize, TimeC callbackUsecs, TimeC periodUsecs);
    void readFrameTime(Timer* timer);
};


} // namespace jackmini
