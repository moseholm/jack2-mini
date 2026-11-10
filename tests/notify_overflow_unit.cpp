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

#include "JackMessage.h"
#include "JackNotification.h"
#include "JackRequest.h"
#include "JackSocket.h"
#include "JackSocketNotifyChannel.h"

#include <array>
#include <cstdint>
#include <sys/socket.h>
#include <unistd.h>
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
    std::vector<std::int32_t> delivered; // value1 of the notifications that came through, in order
    std::uint64_t dropped = 0; // the sum of the kDropped counts
};

Received ReadAll(JackClientSocket& socket, std::int32_t last)
{
    Received got;
    while (true) {
        JackMsgHeader header { };
        if (ReadJackMsgHeader(&socket, header) < 0) {
            break;
        }
        JackClientNotification event;
        JackPayloadReader payload(&socket, header.length, { });
        if (event.Read(&payload) < 0 || !payload.Done()) {
            break;
        }
        if (event.fNotify == kDropped) {
            got.dropped += static_cast<std::uint64_t>(event.fValue1);
            continue;
        }
        got.delivered.push_back(event.fValue1);
        if (event.fValue1 == last) {
            break;
        }
    }
    return got;
}


// Sends sent notifications while the client does not read, then reads them all.  Returns what came through.
Received Run(std::int32_t sent)
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

    JackSocketNotifyChannel channel;
    CHECK(channel.Attach(fd[0]) == 0);

    for (std::int32_t i = 0; i < sent; i++) {
        std::int32_t result = -1;
        channel.ClientNotify(1, "", kNotify, 0, nullptr, i, 0, &result);
        CHECK(result == 0);
    }

    Received got;
    JackClientSocket client_end(fd[1]);
    std::thread reader([&] { got = ReadAll(client_end, sent - 1); });
    reader.join();
    channel.Close();
    close(fd[1]);
    return got;
}

void CheckOrdered(const Received& got)
{
    bool ordered = true;
    for (std::size_t i = 1; i < got.delivered.size(); i++) {
        ordered = ordered && got.delivered[i - 1] < got.delivered[i];
    }
    CHECK(ordered);
}

} // namespace

int main()
{
    // Over the queue: the oldest are dropped and counted, the newest is delivered.
    {
        constexpr std::int32_t sent = 1000;
        const Received got = Run(sent);
        const std::uint64_t delivered = got.delivered.size();
        CHECK(delivered + got.dropped == static_cast<std::uint64_t>(sent));
        CHECK(got.dropped > 0);
        CHECK(!got.delivered.empty() && got.delivered.back() == sent - 1);
        CheckOrdered(got);
        // At most the queue and the few notifications that fit in the small socket buffer.
        CHECK(delivered <= JackSocketNotifyChannel::kQueueSize + 64);
        std::printf("sent %d, delivered %lu, dropped %lu\n", sent, delivered, got.dropped);
    }

    // Under the queue: nothing is dropped and every notification comes through in order.
    {
        constexpr std::int32_t sent = 100;
        const Received got = Run(sent);
        CHECK(got.dropped == 0);
        CHECK(got.delivered.size() == static_cast<std::size_t>(sent));
        CheckOrdered(got);
        std::printf("sent %d, delivered %zu, dropped %lu\n", sent, got.delivered.size(), got.dropped);
    }

    std::printf("%s\n", gFailures == 0 ? "notification overflow checks passed" : "notification overflow checks FAILED");
    return gFailures == 0 ? 0 : 1;
}
