// Copyright (C) 2004-2008 Grame
// Copyright (C) 2016 Filipe Coelho
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

#include "CompilerDeps.h"
#include "Synchro.h"
#include <stddef.h>
#include <cstdint>

namespace jackmini {

// \brief Inter process synchronization using Linux futex.
//
// Based on the JackPosixSemaphore class.
// Adapted to work with linux futex to be as light as possible and also work in multiple architectures.
//
// Adds a new 'MakePrivate' function that makes the sync happen in the local process only,
// making it even faster for internal clients.

class SERVER_EXPORT LinuxFutex : public detail::Synchro {

private:
    struct FutexData {
        std::int32_t m_futex; // futex, needs to be 1st member
        bool m_internal; // current internal state
        bool m_wasInternal; // initial internal state, only changes in allocate
        bool m_needsChange; // change state on next wait call
        std::int32_t m_externalCount; // how many external clients have connected
    };

    std::int32_t m_sharedMem;
    FutexData* m_futex;
    bool m_private;
    bool m_promiscuous;
    std::int32_t m_promiscuousGid;

protected:
    void buildName(const char* name, const char* serverName, char* res, std::int32_t size);

public:
    LinuxFutex();

    bool signal();
    bool signalAll();
    bool wait();
    bool timedWait(std::int64_t usec);

    bool allocate(const char* name, const char* serverName, std::int32_t value, bool internal = false);
    bool connect(const char* name, const char* serverName);
    bool connectInput(const char* name, const char* serverName);
    bool connectOutput(const char* name, const char* serverName);
    bool disconnect();
    void destroy();

    void makePrivate(bool priv);
};

} // namespace jackmini
