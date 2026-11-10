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

#include "JackSocket.h"
#include "jackmini/error.hpp"

namespace jackmini {

// \brief JackServerNotifyChannel using sockets.

class SocketServerNotifyChannel {
private:
    ClientSocket m_requestSocket;

public:
    SocketServerNotifyChannel()
    {
    }

    Result<void> open(const char* serverName);
    void close();

    void notify(std::int32_t refnum, std::int32_t notify, std::int32_t value);
    void notifyQuit();
};

} // namespace jackmini
