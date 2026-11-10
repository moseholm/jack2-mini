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

#include "JackMessage.h"
#include "JackNotification.h"
#include "JackRequest.h"

#include <cstdio>
#include <cstring>
#include <string>

using namespace jackmini;

namespace {

int gFailures = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            gFailures++;                                                \
        }                                                               \
    } while (0)

// A transport over a byte buffer: writes append, reads consume.
class BufferTransport : public detail::JackChannelTransactionInterface {
public:
    std::vector<std::byte> fBytes;
    std::size_t fPos = 0;

    int Read(void* data, int len) override
    {
        if (fPos + len > fBytes.size()) {
            return -1;
        }
        std::memcpy(data, fBytes.data() + fPos, len);
        fPos += len;
        return 0;
    }

    int Write(void* data, int len) override
    {
        const auto* bytes = static_cast<const std::byte*>(data);
        fBytes.insert(fBytes.end(), bytes, bytes + len);
        return 0;
    }

    int WriteWithFds(void* data, int len, std::span<const int> fds) override
    {
        return Write(data, len);
    }
};

void TestHeaderLayout()
{
    CHECK(sizeof(JackMsgHeader) == 16);
    CHECK(kJackMsgMagic == 0x4a4d4e53);
    CHECK(kJackMsgVersion == JACK_PROTOCOL_VERSION);
}

void TestRoundTrip()
{
    BufferTransport wire;
    const char payload[] = "jackmini";
    const JackMsgHeader sent = MakeJackMsgHeader(23, sizeof(payload), 42);
    CHECK(SendJackMessage(&wire, sent, payload, sizeof(payload)) == 0);
    CHECK(wire.fBytes.size() == 16 + sizeof(payload));

    JackMsgHeader got { };
    CHECK(ReadJackMsgHeader(&wire, got) == 0);
    CHECK(got.type == 23);
    CHECK(got.length == sizeof(payload));
    CHECK(got.request == 42);
    CHECK(got.version == kJackMsgVersion);

    JackPayloadReader body(&wire, got.length, { });
    char back[sizeof(payload)] = { };
    CHECK(body.Read(back, sizeof(back)) == 0);
    CHECK(std::strcmp(back, payload) == 0);
    CHECK(body.Done());
}

void TestOverrunIsRefused()
{
    BufferTransport wire;
    const char payload[8] = { };
    SendJackMessage(&wire, MakeJackMsgHeader(1, sizeof(payload), 1), payload, sizeof(payload));
    JackMsgHeader got { };
    ReadJackMsgHeader(&wire, got);

    // Reading more than the header's length is an error, even though the bytes are there.
    JackPayloadReader body(&wire, got.length, { });
    char big[sizeof(payload) + 1] = { };
    CHECK(body.Read(big, sizeof(big)) < 0);
}

void TestShortReadIsNotDone()
{
    BufferTransport wire;
    const char payload[8] = { };
    SendJackMessage(&wire, MakeJackMsgHeader(1, sizeof(payload), 1), payload, sizeof(payload));
    JackMsgHeader got { };
    ReadJackMsgHeader(&wire, got);

    JackPayloadReader body(&wire, got.length, { });
    char part[4] = { };
    CHECK(body.Read(part, sizeof(part)) == 0);
    CHECK(!body.Done());
}

void TestBadMagicIsRefused()
{
    BufferTransport wire;
    JackMsgHeader bad = MakeJackMsgHeader(1, 0, 1);
    bad.magic = 0x12345678;
    SendJackMessage(&wire, bad, nullptr, 0);
    JackMsgHeader got { };
    CHECK(ReadJackMsgHeader(&wire, got) < 0);
}

void TestReplyWriter()
{
    JackPayloadWriter writer;
    const std::uint32_t values[2] = { 7, 9 };
    CHECK(writer.Write(const_cast<std::uint32_t*>(&values[0]), sizeof(values[0])) == 0);
    CHECK(writer.Write(const_cast<std::uint32_t*>(&values[1]), sizeof(values[1])) == 0);
    CHECK(writer.fData.size() == 2 * sizeof(std::uint32_t));
    CHECK(writer.fFds.empty());
}

void TestNotificationRoundTrip()
{
    BufferTransport wire;
    JackClientNotification sent("system", 3, jackmini::kXRunCallback, 1, "msg", 5, 6);
    CHECK(sent.Write(&wire, 91) == 0);

    JackMsgHeader got { };
    CHECK(ReadJackMsgHeader(&wire, got) == 0);
    CHECK(got.type == jackmini::kXRunCallback);
    CHECK(got.request == 91);
    CHECK(got.length == JackClientNotification::PayloadSize);

    JackClientNotification back;
    JackPayloadReader body(&wire, got.length, { });
    CHECK(back.Read(&body) == 0);
    CHECK(body.Done());
    CHECK(std::strcmp(back.fName, "system") == 0);
    CHECK(back.fRefNum == 3);
    CHECK(back.fNotify == jackmini::kXRunCallback);
    CHECK(back.fSync == 1);
    CHECK(back.fValue1 == 5 && back.fValue2 == 6);
    CHECK(std::strcmp(back.fMessage, "msg") == 0);
}

void TestSyncReplyEchoesRequest()
{
    BufferTransport wire;
    JackResult res(42);
    CHECK(SendJackReply(&wire, res, MakeJackMsgHeader(jackmini::kXRunCallback, 0, 91)) == 0);

    class ReplySocket : public detail::JackClientRequestInterface {
    public:
        BufferTransport& fWire;
        explicit ReplySocket(BufferTransport& wire)
            : fWire(wire)
        {
        }
        int Read(void* data, int len) override { return fWire.Read(data, len); }
        int Write(void* data, int len) override { return fWire.Write(data, len); }
        int ReadWithFds(void* data, int len, std::vector<UniqueFd>& fds) override { return fWire.Read(data, len); }
    } socket(wire);

    JackResult back;
    CHECK(ReadJackReply(&socket, jackmini::kXRunCallback, 91, back) == 0);
    CHECK(back.fResult == 42);

    BufferTransport again;
    JackResult other(1);
    SendJackReply(&again, other, MakeJackMsgHeader(jackmini::kXRunCallback, 0, 91));
    ReplySocket socket2(again);
    JackResult wrong;
    CHECK(ReadJackReply(&socket2, jackmini::kXRunCallback, 92, wrong) < 0);
}

void TestRequestIdsAreDistinct()
{
    const std::uint32_t first = NextJackRequestId();
    const std::uint32_t second = NextJackRequestId();
    CHECK(second != first);
}

} // namespace

int main()
{
    TestHeaderLayout();
    TestRoundTrip();
    TestOverrunIsRefused();
    TestShortReadIsNotDone();
    TestBadMagicIsRefused();
    TestReplyWriter();
    TestNotificationRoundTrip();
    TestSyncReplyEchoesRequest();
    TestRequestIdsAreDistinct();

    if (gFailures == 0) {
        std::printf("message frame checks passed\n");
        return 0;
    }
    std::printf("%d message frame check(s) failed\n", gFailures);
    return 1;
}
