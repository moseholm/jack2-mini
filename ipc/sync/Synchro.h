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

#include "ipc/protocol/Constants.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <string_view>

namespace jackmini {

namespace detail {

// \brief An inter process synchronization primitive.

class Synchro {

protected:
    std::array<char, kSyncMaxNameSize> m_name;
    std::atomic<bool> m_flush; // If true, signal are "flushed" : used for drivers that do no consume the signal

public:
    Synchro()
        : m_flush(false)
    {
    }
    ~Synchro()
    {
    }

    bool signal()
    {
        return true;
    }
    bool signalAll()
    {
        return true;
    }
    bool wait()
    {
        return true;
    }
    bool timedWait(std::int64_t usec)
    {
        return true;
    }
    bool allocate(std::string_view name, std::string_view serverName, std::int32_t value)
    {
        return true;
    }
    bool connect(std::string_view name, std::string_view serverName)
    {
        return true;
    }
    bool connectInput(std::string_view name, std::string_view serverName)
    {
        return true;
    }
    bool connectOutput(std::string_view name, std::string_view serverName)
    {
        return true;
    }
    bool disconnect()
    {
        return true;
    }
    void destroy()
    {
    }

    void setFlush(bool mode)
    {
        m_flush = mode;
    }
};

} // namespace detail

} // namespace jackmini
