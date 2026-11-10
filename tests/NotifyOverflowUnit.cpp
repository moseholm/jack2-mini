// Copyright (C) 2026 JACK contributors
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

// The notification queue of a client when the client does not read (protocol.md section 3.2 and 4).  The server queues
// up to kQueueSize notifications for a client and drops the oldest when it is full, counting the drops.  The client
// is told of the drops with a kDropped notification.  The test does not depend on the socket buffers of the kernel: what
// must hold is that every notification is either delivered or counted as dropped, that the newest one is delivered,
// and that the order is kept.

#include "Message.h"
#include "Notification.h"
#include "Request.h"
#include "Socket.h"
#include "SocketNotifyChannel.h"

#include <sys/socket.h>
#include <unistd.h>
#include <array>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <vector>

using namespace jackmini;

namespace {

std::int32_t gFailures = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            gFailures++;                                                \
        }                                                               \
    } while (0)

// Sent in order: value1 is the index.  The kind is a registration notification, nothing special for the client.
constexpr std::int32_t kNotify = kPortRegistrationOnCallback;

// The reader: parses the notifications from the client's end of the socket pair, as the client does.
struct Received {
    std::vector<std::int32_t> m_delivered; // value1 of the notifications that came through, in order
    std::uint64_t m_dropped = 0; // the sum of the kDropped counts
};

Received readAll(ClientSocket& socket, std::int32_t last)
{
    Received got;
    while (true) {
        MsgHeader header { };
        if (readJackMsgHeader(&socket, header) < 0) {
            break;
        }
        ClientNotification event;
        PayloadReader payload(&socket, header.m_length, { });
        if (event.read(&payload) < 0 || !payload.done()) {
            break;
        }
        if (event.m_notify == kDropped) {
            got.m_dropped += static_cast<std::uint64_t>(event.m_value1);
            continue;
        }
        got.m_delivered.push_back(event.m_value1);
        if (event.m_value1 == last) {
            break;
        }
    }
    return got;
}


// Sends sent notifications while the client does not read, then reads them all.  Returns what came through.
Received run(std::int32_t sent)
{
    std::array<std::int32_t, 2> fd;
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fd.data()) != 0) {
        std::printf("socketpair failed\n");
        return { };
    }

    // A small send buffer, so that the writer stops early: the rest waits in the queue.  The count of what fits is
    // not checked, only the invariants.
    std::int32_t small = 4096;
    setsockopt(fd[0], SOL_SOCKET, SO_SNDBUF, &small, sizeof(small));

    SocketNotifyChannel channel;
    CHECK(channel.attach(fd[0]) == 0);

    for (std::int32_t i = 0; i < sent; i++) {
        std::int32_t result = -1;
        channel.clientNotify(1, "", kNotify, 0, nullptr, i, 0, &result);
        CHECK(result == 0);
    }

    Received got;
    ClientSocket clientEnd(fd[1]);
    std::thread reader([&] { got = readAll(clientEnd, sent - 1); });
    reader.join();
    channel.close();
    close(fd[1]);
    return got;
}

void checkOrdered(const Received& got)
{
    bool ordered = true;
    for (std::size_t i = 1; i < got.m_delivered.size(); i++) {
        ordered = ordered && got.m_delivered[i - 1] < got.m_delivered[i];
    }
    CHECK(ordered);
}

} // namespace

int main()
{
    // Over the queue: the oldest are dropped and counted, the newest is delivered.
    {
        constexpr std::int32_t kSent = 1000;
        const Received got = run(kSent);
        const std::uint64_t delivered = got.m_delivered.size();
        CHECK(delivered + got.m_dropped == static_cast<std::uint64_t>(kSent));
        CHECK(got.m_dropped > 0);
        CHECK(!got.m_delivered.empty() && got.m_delivered.back() == kSent - 1);
        checkOrdered(got);
        // At most the queue and the few notifications that fit in the small socket buffer.
        CHECK(delivered <= SocketNotifyChannel::kQueueSize + 64);
        std::printf("sent %d, delivered %lu, dropped %lu\n", kSent, delivered, got.m_dropped);
    }

    // Under the queue: nothing is dropped and every notification comes through in order.
    {
        constexpr std::int32_t kSent = 100;
        const Received got = run(kSent);
        CHECK(got.m_dropped == 0);
        CHECK(got.m_delivered.size() == static_cast<std::size_t>(kSent));
        checkOrdered(got);
        std::printf("sent %d, delivered %zu, dropped %lu\n", kSent, got.m_delivered.size(), got.m_dropped);
    }

    std::printf("%s\n", gFailures == 0 ? "notification overflow checks passed" : "notification overflow checks FAILED");
    return gFailures == 0 ? 0 : 1;
}
