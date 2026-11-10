#include <cstdint>

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

#include "Constants.h"
#include "Error.h"
#include "Notification.h"
#include "Request.h"
#include "SocketServerNotifyChannel.h"

namespace jackmini {

Result<void> SocketServerNotifyChannel::open(const char* serverName)
{
    if (m_requestSocket.connect(kServerDir, serverName, 0) < 0) {
        jack_error("Cannot connect to server socket");
        return std::unexpected(Error::kServerFailure);
    } else {
        m_requestSocket.setNonBlocking(true);
        return { };
    }
}

void SocketServerNotifyChannel::close()
{
    m_requestSocket.close();
}

// The requirement is that the Notification from RT thread can be delivered... not sure using a socket is adequate here...
// Can the write operation block?
// A non blocking write operation should be used : check if write can succeed, and ignore the notification otherwise
// (since its mainly used for XRun, ignoring a notification is OK, successive XRun will come...)
void SocketServerNotifyChannel::notify(std::int32_t refnum, std::int32_t notify, std::int32_t value)
{
    ClientNotificationRequest req(refnum, notify, value);
    if (req.write(&m_requestSocket) < 0) {
        jack_error("Could not write notification ref = %d notify = %d", refnum, notify);
    }
}

void SocketServerNotifyChannel::notifyQuit()
{
    notify(-1, kQUIT, 0);
}

} // namespace jackmini
