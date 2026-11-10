// Copyright (C) 2026 JACK contributors
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

// Message framing of the control socket (protocol.md section 2): the header, the exact payload length, and the reply
// writer and reader.  The transport is a byte buffer, so the test needs no server.

#include "Message.h"
#include "Notification.h"
#include "Request.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

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

// A transport over a byte buffer: writes append, reads consume.
class BufferTransport : public detail::ChannelTransactionInterface {
public:
    std::vector<std::byte> m_bytes;
    std::size_t m_pos = 0;

    std::int32_t read(void* data, std::int32_t len) override
    {
        if (m_pos + len > m_bytes.size()) {
            return -1;
        }
        std::memcpy(data, m_bytes.data() + m_pos, len);
        m_pos += len;
        return 0;
    }

    std::int32_t write(void* data, std::int32_t len) override
    {
        const auto* bytes = static_cast<const std::byte*>(data);
        m_bytes.insert(m_bytes.end(), bytes, bytes + len);
        return 0;
    }

    std::int32_t writeWithFds(void* data, std::int32_t len, std::span<const std::int32_t> fds) override
    {
        return write(data, len);
    }
};

void testHeaderLayout()
{
    CHECK(sizeof(MsgHeader) == 16);
    CHECK(kJackMsgMagic == 0x4a4d4e53);
    CHECK(kJackMsgVersion == kProtocolVersion);
}

void testRoundTrip()
{
    BufferTransport wire;
    constexpr std::string_view kPayload = "jackmini";
    constexpr std::size_t kPayloadSize = kPayload.size() + 1;
    const MsgHeader sent = makeJackMsgHeader(23, kPayloadSize, 42);
    CHECK(sendJackMessage(&wire, sent, kPayload.data(), kPayloadSize) == 0);
    CHECK(wire.m_bytes.size() == 16 + kPayloadSize);

    MsgHeader got { };
    CHECK(readJackMsgHeader(&wire, got) == 0);
    CHECK(got.m_type == 23);
    CHECK(got.m_length == kPayloadSize);
    CHECK(got.m_request == 42);
    CHECK(got.m_version == kJackMsgVersion);

    PayloadReader body(&wire, got.m_length, { });
    std::array<char, kPayloadSize> back = { };
    CHECK(body.read(back.data(), sizeof(back)) == 0);
    CHECK(std::strcmp(back.data(), kPayload.data()) == 0);
    CHECK(body.done());
}

void testOverrunIsRefused()
{
    BufferTransport wire;
    std::array<const char, 8> payload = { };
    sendJackMessage(&wire, makeJackMsgHeader(1, sizeof(payload), 1), payload.data(), sizeof(payload));
    MsgHeader got { };
    readJackMsgHeader(&wire, got);

    // Reading more than the header's length is an error, even though the bytes are there.
    PayloadReader body(&wire, got.m_length, { });
    std::array<char, sizeof(payload) + 1> big = { };
    CHECK(body.read(big.data(), sizeof(big)) < 0);
}

void testShortReadIsNotDone()
{
    BufferTransport wire;
    std::array<const char, 8> payload = { };
    sendJackMessage(&wire, makeJackMsgHeader(1, sizeof(payload), 1), payload.data(), sizeof(payload));
    MsgHeader got { };
    readJackMsgHeader(&wire, got);

    PayloadReader body(&wire, got.m_length, { });
    std::array<char, 4> part = { };
    CHECK(body.read(part.data(), sizeof(part)) == 0);
    CHECK(!body.done());
}

void testBadMagicIsRefused()
{
    BufferTransport wire;
    MsgHeader bad = makeJackMsgHeader(1, 0, 1);
    bad.m_magic = 0x12345678;
    sendJackMessage(&wire, bad, nullptr, 0);
    MsgHeader got { };
    CHECK(readJackMsgHeader(&wire, got) < 0);
}

void testReplyWriter()
{
    PayloadWriter writer;
    std::array<const std::uint32_t, 2> values = { 7, 9 };
    CHECK(writer.write(const_cast<std::uint32_t*>(&values[0]), sizeof(values[0])) == 0);
    CHECK(writer.write(const_cast<std::uint32_t*>(&values[1]), sizeof(values[1])) == 0);
    CHECK(writer.m_data.size() == 2 * sizeof(std::uint32_t));
    CHECK(writer.m_fds.empty());
}

void testNotificationRoundTrip()
{
    BufferTransport wire;
    ClientNotification sent("system", 3, jackmini::kXRunCallback, 1, "msg", 5, 6);
    CHECK(sent.write(&wire, 91) == 0);

    MsgHeader got { };
    CHECK(readJackMsgHeader(&wire, got) == 0);
    CHECK(got.m_type == jackmini::kXRunCallback);
    CHECK(got.m_request == 91);
    CHECK(got.m_length == ClientNotification::kPayloadSize);

    ClientNotification back;
    PayloadReader body(&wire, got.m_length, { });
    CHECK(back.read(&body) == 0);
    CHECK(body.done());
    CHECK(std::strcmp(back.m_name.data(), "system") == 0);
    CHECK(back.m_refNum == 3);
    CHECK(back.m_notify == jackmini::kXRunCallback);
    CHECK(back.m_sync == 1);
    CHECK(back.m_value1 == 5 && back.m_value2 == 6);
    CHECK(std::strcmp(back.m_message.data(), "msg") == 0);
}

void testSyncReplyEchoesRequest()
{
    BufferTransport wire;
    Reply res(42);
    CHECK(sendJackReply(&wire, res, makeJackMsgHeader(jackmini::kXRunCallback, 0, 91)) == 0);

    class ReplySocket : public detail::ClientRequestInterface {
    public:
        BufferTransport& m_wire;
        explicit ReplySocket(BufferTransport& wire)
            : m_wire(wire)
        {
        }
        std::int32_t read(void* data, std::int32_t len) override { return m_wire.read(data, len); }
        std::int32_t write(void* data, std::int32_t len) override { return m_wire.write(data, len); }
        std::int32_t readWithFds(void* data, std::int32_t len, std::vector<UniqueFd>& fds) override { return m_wire.read(data, len); }
    } socket(wire);

    Reply back;
    CHECK(readJackReply(&socket, jackmini::kXRunCallback, 91, back) == 0);
    CHECK(back.m_result == 42);

    BufferTransport again;
    Reply other(1);
    sendJackReply(&again, other, makeJackMsgHeader(jackmini::kXRunCallback, 0, 91));
    ReplySocket socket2(again);
    Reply wrong;
    CHECK(readJackReply(&socket2, jackmini::kXRunCallback, 92, wrong) < 0);
}

void testRequestIdsAreDistinct()
{
    const std::uint32_t first = nextJackRequestId();
    const std::uint32_t second = nextJackRequestId();
    CHECK(second != first);
}

} // namespace

int main()
{
    testHeaderLayout();
    testRoundTrip();
    testOverrunIsRefused();
    testShortReadIsNotDone();
    testBadMagicIsRefused();
    testReplyWriter();
    testNotificationRoundTrip();
    testSyncReplyEchoesRequest();
    testRequestIdsAreDistinct();

    if (gFailures == 0) {
        std::printf("message frame checks passed\n");
        return 0;
    }
    std::printf("%d message frame check(s) failed\n", gFailures);
    return 1;
}
