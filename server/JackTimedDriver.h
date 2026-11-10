// Copyright (C) 2001 Paul Davis
// Copyright (C) 2004-2008 Grame
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

#include <cstdint>

#include "JackAudioDriver.h"

namespace jackmini {

// \brief The timed driver.

class SERVER_EXPORT TimedDriver : public AudioDriver {
protected:
    std::int32_t m_cycleCount;
    TimeC m_anchorTimeUsec;

    std::int32_t firstCycle(TimeC curTime);
    std::int32_t currentCycle(TimeC curTime);

    void processWait();

public:
    TimedDriver(const char* name, const char* alias, LockedEngine* engine, Synchro* table)
        : AudioDriver(name, alias, engine, table)
        , m_cycleCount(0)
        , m_anchorTimeUsec(0)
    {
    }
    virtual ~TimedDriver()
    {
    }

    // BufferSize can be changed
    bool isFixedBufferSize()
    {
        return false;
    }

    std::int32_t start();
};

} // namespace jackmini
