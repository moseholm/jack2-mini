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

#include "JackSocketNotifyChannel.h"
#include "JackConstants.h"
#include "JackError.h"
#include "JackNotification.h"
#include "JackRequest.h"

namespace jackmini {

// Server to client
std::int32_t SocketNotifyChannel::open(const char* name)
{
    jack_log("JackSocketNotifyChannel::Open name = %s", name);

    // Connect to client listen socket
    if (m_notifySocket.connect(kClientDir, name, 0) < 0) {
        jack_error("Cannot connect client socket");
        return -1;
    }
    return start();
}

std::int32_t SocketNotifyChannel::attach(std::int32_t fd)
{
    m_notifySocket = ClientSocket(fd);
    return start();
}

std::int32_t SocketNotifyChannel::start()
{
    // Use a time out for notifications
    m_notifySocket.setReadTimeOut(kSocketTimeOut);

    {
        std::lock_guard lock(m_mutex);
        m_closing = false;
    }
    m_writer = std::thread([this] { writer(); });
    return 0;
}

void SocketNotifyChannel::close()
{
    jack_log("JackSocketNotifyChannel::Close");
    {
        std::lock_guard lock(m_mutex);
        m_closing = true;
    }
    m_condition.notify_all();
    if (m_writer.joinable()) {
        m_writer.join();
    }
    m_notifySocket.close();
}

// Called from the server's threads.  It does not wait for the client, except for a synchronous notification, whose
// result the client sends back.
void SocketNotifyChannel::clientNotify(std::int32_t refnum, const char* name, std::int32_t notify, std::int32_t sync, const char* message, std::int32_t value1, std::int32_t value2, std::int32_t* result)
{
    auto pending = std::make_shared<Pending>();
    pending->m_event = std::make_shared<ClientNotification>(name, refnum, notify, sync, message, value1, value2);
    pending->m_request = sync ? nextJackRequestId() : 0;

    std::unique_lock lock(m_mutex);
    if (m_closing) {
        *result = -1;
        return;
    }

    if (m_queue.size() >= kQueueSize) {
        const auto oldest = m_queue.front();
        m_queue.pop_front();
        m_dropped++;
        if (oldest->m_event->m_sync) {
            oldest->m_result = -1;
            oldest->m_done = true;
        }
        jack_log("JackSocketNotifyChannel::ClientNotify queue full, dropped notification %d", oldest->m_event->m_notify);
    }
    m_queue.push_back(pending);
    m_condition.notify_all();

    if (!sync) {
        *result = 0;
        return;
    }
    m_condition.wait(lock, [&] { return pending->m_done; });
    *result = pending->m_result;
}

// The only thread that writes to the client's socket, so the notifications arrive in the order they were queued.
void SocketNotifyChannel::writer()
{
    std::unique_lock lock(m_mutex);
    while (true) {
        m_condition.wait(lock, [&] { return m_closing || !m_queue.empty(); });
        if (m_closing) {
            break;
        }

        const auto pending = m_queue.front();
        m_queue.pop_front();
        const std::uint32_t dropped = std::exchange(m_dropped, 0);

        lock.unlock();
        const std::int32_t result = send(*pending, dropped);
        lock.lock();

        pending->m_result = result;
        pending->m_done = true;
        m_condition.notify_all();
    }

    // Closing: the callers still waiting for a synchronous notification are released with an error.
    for (const auto& pending : m_queue) {
        if (pending->m_event->m_sync) {
            pending->m_result = -1;
            pending->m_done = true;
        }
    }
    m_queue.clear();
    m_condition.notify_all();
}

std::int32_t SocketNotifyChannel::send(Pending& pending, std::uint32_t dropped)
{
    if (dropped != 0) {
        ClientNotification notice("", pending.m_event->m_refNum, kDropped, 0, nullptr, static_cast<std::int32_t>(dropped), 0);
        if (notice.write(&m_notifySocket, 0) < 0) {
            jack_error("Could not write the dropped notification count");
            return -1;
        }
    }

    if (pending.m_event->write(&m_notifySocket, pending.m_request) < 0) {
        jack_error("Could not write notification");
        return -1;
    }
    if (!pending.m_event->m_sync) {
        return 0;
    }

    Reply res;
    if (readJackReply(&m_notifySocket, pending.m_event->m_notify, pending.m_request, res) < 0) {
        jack_error("Could not read notification result");
        return -1;
    }
    return res.m_result;
}

} // namespace jackmini
