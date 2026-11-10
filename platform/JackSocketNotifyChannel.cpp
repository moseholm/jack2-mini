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
int JackSocketNotifyChannel::Open(const char* name)
{
    jack_log("JackSocketNotifyChannel::Open name = %s", name);

    // Connect to client listen socket
    if (fNotifySocket.Connect(jack_client_dir, name, 0) < 0) {
        jack_error("Cannot connect client socket");
        return -1;
    }
    return Start();
}

int JackSocketNotifyChannel::Attach(int fd)
{
    fNotifySocket = JackClientSocket(fd);
    return Start();
}

int JackSocketNotifyChannel::Start()
{
    // Use a time out for notifications
    fNotifySocket.SetReadTimeOut(SOCKET_TIME_OUT);

    {
        std::lock_guard lock(fMutex);
        fClosing = false;
    }
    fWriter = std::thread([this] { Writer(); });
    return 0;
}

void JackSocketNotifyChannel::Close()
{
    jack_log("JackSocketNotifyChannel::Close");
    {
        std::lock_guard lock(fMutex);
        fClosing = true;
    }
    fCondition.notify_all();
    if (fWriter.joinable()) {
        fWriter.join();
    }
    fNotifySocket.Close();
}

// Called from the server's threads.  It does not wait for the client, except for a synchronous notification, whose
// result the client sends back.
void JackSocketNotifyChannel::ClientNotify(int refnum, const char* name, int notify, int sync, const char* message, int value1, int value2, int* result)
{
    auto pending = std::make_shared<Pending>();
    pending->event = std::make_shared<JackClientNotification>(name, refnum, notify, sync, message, value1, value2);
    pending->request = sync ? NextJackRequestId() : 0;

    std::unique_lock lock(fMutex);
    if (fClosing) {
        *result = -1;
        return;
    }

    if (fQueue.size() >= kQueueSize) {
        const auto oldest = fQueue.front();
        fQueue.pop_front();
        fDropped++;
        if (oldest->event->fSync) {
            oldest->result = -1;
            oldest->done = true;
        }
        jack_log("JackSocketNotifyChannel::ClientNotify queue full, dropped notification %d", oldest->event->fNotify);
    }
    fQueue.push_back(pending);
    fCondition.notify_all();

    if (!sync) {
        *result = 0;
        return;
    }
    fCondition.wait(lock, [&] { return pending->done; });
    *result = pending->result;
}

// The only thread that writes to the client's socket, so the notifications arrive in the order they were queued.
void JackSocketNotifyChannel::Writer()
{
    std::unique_lock lock(fMutex);
    while (true) {
        fCondition.wait(lock, [&] { return fClosing || !fQueue.empty(); });
        if (fClosing) {
            break;
        }

        const auto pending = fQueue.front();
        fQueue.pop_front();
        const std::uint32_t dropped = std::exchange(fDropped, 0);

        lock.unlock();
        const int result = Send(*pending, dropped);
        lock.lock();

        pending->result = result;
        pending->done = true;
        fCondition.notify_all();
    }

    // Closing: the callers still waiting for a synchronous notification are released with an error.
    for (const auto& pending : fQueue) {
        if (pending->event->fSync) {
            pending->result = -1;
            pending->done = true;
        }
    }
    fQueue.clear();
    fCondition.notify_all();
}

int JackSocketNotifyChannel::Send(Pending& pending, std::uint32_t dropped)
{
    if (dropped != 0) {
        JackClientNotification notice("", pending.event->fRefNum, kDropped, 0, nullptr, static_cast<int>(dropped), 0);
        if (notice.Write(&fNotifySocket, 0) < 0) {
            jack_error("Could not write the dropped notification count");
            return -1;
        }
    }

    if (pending.event->Write(&fNotifySocket, pending.request) < 0) {
        jack_error("Could not write notification");
        return -1;
    }
    if (!pending.event->fSync) {
        return 0;
    }

    JackResult res;
    if (ReadJackReply(&fNotifySocket, pending.event->fNotify, pending.request, res) < 0) {
        jack_error("Could not read notification result");
        return -1;
    }
    return res.fResult;
}

} // namespace jackmini
