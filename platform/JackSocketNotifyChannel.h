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

#include "JackChannel.h"
#include "JackSocket.h"

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>

namespace jackmini {

struct JackClientNotification;

// \brief JackNotifyChannel using sockets.

class JackSocketNotifyChannel {

private:
    // A notification waiting for the writer.  A synchronous one is also waited for by the caller of ClientNotify.
    struct Pending {
        std::shared_ptr<JackClientNotification> event;
        std::uint32_t request = 0;
        bool done = false;
        std::int32_t result = -1;
    };

    JackClientSocket fNotifySocket; // Socket to communicate with the server : from server to client

    std::mutex fMutex;
    std::condition_variable fCondition;
    std::deque<std::shared_ptr<Pending>> fQueue;
    std::uint32_t fDropped = 0;
    bool fClosing = false;
    std::thread fWriter;

    void Writer();
    std::int32_t Send(Pending& pending, std::uint32_t dropped);
    std::int32_t Start();

public:
    // Notifications queued for one client.  When it is full, the oldest is dropped and counted; the client is told
    // with a kDropped notification (protocol.md section 3.2).  The size is a first guess, to be measured.
    static constexpr std::size_t kQueueSize = 256;

    JackSocketNotifyChannel()
    {
    }

    std::int32_t Open(const char* name); // Open the Server/Client connection
    // Uses a connected socket instead of connecting one: the tests attach one end of a socket pair.
    std::int32_t Attach(std::int32_t fd);
    void Close(); // Close the Server/Client connection

    void ClientNotify(std::int32_t refnum, const char* name, std::int32_t notify, std::int32_t sync, const char* message, std::int32_t value1, std::int32_t value2, std::int32_t* result);
};

} // namespace jackmini
