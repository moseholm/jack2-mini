// Copyright (C) 2026 JACK contributors
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

// Tests for descriptor passing and anonymous segments (P2 step 2).

#include "FdSegment.h"

#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace {

std::int32_t gFailures = 0;
std::int32_t gChecks = 0;

#define CHECK(cond)                                                                       \
    do {                                                                                  \
        gChecks++;                                                                        \
        if (!(cond)) {                                                                    \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            gFailures++;                                                                  \
        }                                                                                 \
    } while (0)

using namespace jackmini;

void socketPair(std::array<std::int32_t, 2>& pair)
{
    const std::int32_t rc = socketpair(AF_UNIX, SOCK_SEQPACKET, 0, pair.data());
    if (rc != 0) {
        std::perror("socketpair");
        std::exit(1);
    }
}

void testSegmentIsSharedAcrossMappings()
{
    constexpr std::size_t kSize = 4096;
    auto segment = createSegment(kSize, "jack-test");
    CHECK(segment.has_value());
    if (!segment)
        return;

    auto first = mapSegment(segment->get(), kSize);
    auto second = mapSegment(segment->get(), kSize);
    CHECK(first.has_value() && second.has_value());
    if (!first || !second)
        return;

    auto* a = static_cast<std::uint32_t*>(*first);
    auto* b = static_cast<std::uint32_t*>(*second);
    a[7] = 0xdeadbeef;
    CHECK(b[7] == 0xdeadbeef);

    unmapSegment(*first, kSize);
    unmapSegment(*second, kSize);
}

void testDescriptorArrivesAndMapsToSameMemory()
{
    constexpr std::size_t kSize = 8192;
    auto segment = createSegment(kSize, "jack-test-pass");
    CHECK(segment.has_value());
    if (!segment)
        return;

    auto mine = mapSegment(segment->get(), kSize);
    CHECK(mine.has_value());
    if (!mine)
        return;
    auto* words = static_cast<std::uint32_t*>(*mine);
    for (std::uint32_t i = 0; i < 16; i++) {
        words[i] = 0x1000 + i;
    }

    std::array<std::int32_t, 2> pair;
    socketPair(pair);

    const std::array<std::byte, 5> message { std::byte { 'h' }, std::byte { 'e' }, std::byte { 'l' }, std::byte { 'l' }, std::byte { 'o' } };
    const std::array<std::int32_t, 1> fds { segment->get() };
    CHECK(sendWithFds(pair[0], message, fds).has_value());

    std::array<std::byte, 16> got { };
    std::vector<UniqueFd> received;
    auto n = recvWithFds(pair[1], got, received);
    CHECK(n.has_value() && *n == message.size());
    CHECK(std::memcmp(got.data(), message.data(), message.size()) == 0);
    CHECK(received.size() == 1);
    if (n && received.size() == 1) {
        CHECK(received[0].valid());
        CHECK(fcntl(received[0].get(), F_GETFD) & FD_CLOEXEC);

        auto theirs = mapSegment(received[0].get(), kSize);
        CHECK(theirs.has_value());
        if (theirs) {
            auto* view = static_cast<std::uint32_t*>(*theirs);
            CHECK(view[0] == 0x1000 && view[15] == 0x100f);
            view[3] = 42;
            CHECK(words[3] == 42);
            unmapSegment(*theirs, kSize);
        }
    }

    unmapSegment(*mine, kSize);
    close(pair[0]);
    close(pair[1]);
}

void testClosedPeerReadsZero()
{
    std::array<std::int32_t, 2> pair;
    socketPair(pair);
    close(pair[0]);
    std::array<std::byte, 8> buf { };
    std::vector<UniqueFd> none;
    auto n = recvWithFds(pair[1], buf, none);
    CHECK(n.has_value() && *n == 0);
    close(pair[1]);
}

void testTooManyDescriptorsRejected()
{
    std::array<std::int32_t, 2> pair;
    socketPair(pair);
    std::array<std::int32_t, 9> fds { };
    for (auto& fd : fds)
        fd = pair[0];
    const std::array<std::byte, 1> one { std::byte { 1 } };
    auto sent = sendWithFds(pair[0], one, fds);
    CHECK(!sent.has_value());
    close(pair[0]);
    close(pair[1]);
}

void testUniqueFdClosesOnDestruction()
{
    std::array<std::int32_t, 2> pair;
    socketPair(pair);
    std::int32_t raw = pair[0];
    {
        UniqueFd owned(raw);
        CHECK(owned.valid());
    }
    CHECK(fcntl(raw, F_GETFD) == -1);
    close(pair[1]);
}

} // namespace

int main()
{
    testSegmentIsSharedAcrossMappings();
    testDescriptorArrivesAndMapsToSameMemory();
    testClosedPeerReadsZero();
    testTooManyDescriptorsRejected();
    testUniqueFdClosesOnDestruction();

    std::printf("%d checks, %d failed\n", gChecks, gFailures);
    return gFailures == 0 ? 0 : 1;
}
