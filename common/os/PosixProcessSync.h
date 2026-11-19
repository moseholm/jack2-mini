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

#include "common/Exception.h"
#include "common/os/PosixMutex.h"
#include <cstdint>
#include <string_view>

namespace jackmini {

// \brief A synchronization primitive built using a condition variable.

class PosixProcessSync : public BasePosixMutex {

private:
    pthread_cond_t m_cond; // Condition variable

public:
    PosixProcessSync(std::string_view /*name*/ = {})
        : BasePosixMutex()
    {
        std::int32_t res = pthread_cond_init(&m_cond, nullptr);
        ThrowIf(res != 0, Exception(std::string_view("JackBasePosixMutex: could not init the cond variable")));
    }

    virtual ~PosixProcessSync()
    {
        pthread_cond_destroy(&m_cond);
    }

    bool timedWait(std::int64_t usec);
    bool lockedTimedWait(std::int64_t usec);

    void wait();
    void lockedWait();

    void signal();
    void lockedSignal();

    void signalAll();
    void lockedSignalAll();
};

} // namespace jackmini
