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

#include "Channel.h"
#include "Socket.h"

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>

namespace jackmini {

struct ClientNotification;

// \brief JackNotifyChannel using sockets.

class SocketNotifyChannel {

private:
    // A notification waiting for the writer.  A synchronous one is also waited for by the caller of ClientNotify.
    struct Pending {
        std::shared_ptr<ClientNotification> m_event;
        std::uint32_t m_request = 0;
        bool m_done = false;
        std::int32_t m_result = -1;
    };

    ClientSocket m_notifySocket; // Socket to communicate with the server : from server to client

    std::mutex m_mutex;
    std::condition_variable m_condition;
    std::deque<std::shared_ptr<Pending>> m_queue;
    std::uint32_t m_dropped = 0;
    bool m_closing = false;
    std::thread m_writer;

    void writer();
    std::int32_t send(Pending& pending, std::uint32_t dropped);
    std::int32_t start();

public:
    // Notifications queued for one client.  When it is full, the oldest is dropped and counted; the client is told
    // with a kDropped notification (protocol.md section 3.2).  The size is a first guess, to be measured.
    static constexpr std::size_t kQueueSize = 256;

    SocketNotifyChannel()
    {
    }

    std::int32_t open(const char* name); // Open the Server/Client connection
    // Uses a connected socket instead of connecting one: the tests attach one end of a socket pair.
    std::int32_t attach(std::int32_t fd);
    void close(); // Close the Server/Client connection

    void clientNotify(std::int32_t refnum, const char* name, std::int32_t notify, std::int32_t sync, const char* message, std::int32_t value1, std::int32_t value2, std::int32_t* result);
};

} // namespace jackmini
