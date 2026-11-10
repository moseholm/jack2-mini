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

// Conformance of the control socket (protocol.md sections 2, 3 and 8).  A fake server runs on one end of a socket pair
// and answers the requests the way a server does; the other end is the client's socket.  The requests and replies go
// through the real message code.  The malformed and version-mismatch cases go through the real request decoder, which
// refuses them before it needs the engine, so the decoder runs without a server.

#include "Message.h"
#include "Notification.h"
#include "Request.h"
#include "RequestDecoder.h"
#include "Socket.h"

#include <sys/socket.h>
#include <unistd.h>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

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

// A connected pair of stream sockets, closed on destruction.
struct SocketPair {
    std::array<std::int32_t, 2> m_fd = { -1, -1 };

    SocketPair()
    {
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, m_fd.data()) != 0) {
            std::printf("socketpair failed\n");
            gFailures++;
        }
    }
    ~SocketPair()
    {
        ::close(m_fd[0]);
        ::close(m_fd[1]);
    }
};

// The server's side of the conversation: decodes each request it is sent and replies to it.  It keeps what it was sent,
// so the test can check what the client's request carried.
class FakeServer {
public:
    explicit FakeServer(std::int32_t fd)
        : m_socket(fd)
    {
    }

    void start()
    {
        m_thread = std::thread([this] { run(); });
    }

    void join()
    {
        if (m_thread.joinable()) {
            m_thread.join();
        }
    }

    NframesC m_bufferSize = 0;
    std::int32_t m_closedRefNum = -1;
    PortIdC m_renamedPort = kNoPort;
    std::string m_renamedTo;
    std::string m_uUIDFor;
    std::int32_t m_requestsServed = 0;

private:
    ClientSocket m_socket;
    std::thread m_thread;

    // Decodes a request payload: the length is the request's own, and it must be read in full.
    template <class Req>
    bool decode(const MsgHeader& header, Req& req)
    {
        if (header.m_length != Req::size()) {
            return false;
        }
        PayloadReader payload(&m_socket, header.m_length, { });
        return req.read(&payload) == 0 && payload.done();
    }

    void run()
    {
        for (;;) {
            MsgHeader header { };
            if (readJackMsgHeader(&m_socket, header) < 0) {
                return; // the client closed the connection
            }
            m_requestsServed++;
            switch (header.m_type) {
            case Request::kSetBufferSize: {
                SetBufferSizeRequest req;
                if (!decode(header, req)) {
                    return;
                }
                m_bufferSize = req.m_bufferSize;
                Reply res(0);
                sendJackReply(&m_socket, res, header);
                break;
            }
            case Request::kClientClose: {
                ClientCloseRequest req;
                if (!decode(header, req)) {
                    return;
                }
                m_closedRefNum = req.m_refNum;
                Reply res(0);
                sendJackReply(&m_socket, res, header);
                break;
            }
            case Request::kPortRename: {
                PortRenameRequest req;
                if (!decode(header, req)) {
                    return;
                }
                m_renamedPort = req.m_port;
                m_renamedTo = req.m_name.data();
                Reply res(0);
                sendJackReply(&m_socket, res, header);
                break;
            }
            case Request::kGetUUIDByClient: {
                GetUUIDRequest req;
                if (!decode(header, req)) {
                    return;
                }
                m_uUIDFor = req.m_name.data();
                UUIDResult res;
                std::snprintf(res.m_uUID.data(), sizeof(res.m_uUID), "%s", "00000000-0000-0000-0000-000000000042");
                res.m_result = 0;
                sendJackReply(&m_socket, res, header);
                break;
            }
            case Request::kComputeTotalLatencies: {
                ComputeTotalLatenciesRequest req;
                if (!decode(header, req)) {
                    return;
                }
                Reply res(0);
                sendJackReply(&m_socket, res, header);
                break;
            }
            default:
                return;
            }
        }
    }
};

// A well-formed request is answered by its reply, with the request's type and id echoed.
void testRequestsGetTheirReplies()
{
    SocketPair pair;
    FakeServer server(pair.m_fd[1]);
    server.start();
    ClientSocket client(pair.m_fd[0]);

    SetBufferSizeRequest setBuffer(1024);
    CHECK(setBuffer.write(&client) == 0);
    Reply res1;
    CHECK(readJackReply(&client, setBuffer.m_type, setBuffer.m_requestId, res1) == 0);
    CHECK(res1.m_result == 0);

    ClientCloseRequest close(7);
    CHECK(close.write(&client) == 0);
    Reply res2;
    CHECK(readJackReply(&client, close.m_type, close.m_requestId, res2) == 0);

    PortRenameRequest rename(3, 5, "renamed:port");
    CHECK(rename.write(&client) == 0);
    Reply res3;
    CHECK(readJackReply(&client, rename.m_type, rename.m_requestId, res3) == 0);

    GetUUIDRequest uuid("some client");
    CHECK(uuid.write(&client) == 0);
    UUIDResult res4;
    CHECK(readJackReply(&client, uuid.m_type, uuid.m_requestId, res4) == 0);
    CHECK(std::strcmp(res4.m_uUID.data(), "00000000-0000-0000-0000-000000000042") == 0);

    ComputeTotalLatenciesRequest latencies;
    CHECK(latencies.write(&client) == 0);
    Reply res5;
    CHECK(readJackReply(&client, latencies.m_type, latencies.m_requestId, res5) == 0);

    ::shutdown(pair.m_fd[0], SHUT_WR);
    server.join();

    CHECK(server.m_bufferSize == 1024);
    CHECK(server.m_closedRefNum == 7);
    CHECK(server.m_renamedPort == 5);
    CHECK(server.m_renamedTo == "renamed:port");
    CHECK(server.m_uUIDFor == "some client");
    CHECK(server.m_requestsServed == 5);
}

// A request whose length is not its own is refused by the decoder, which sends no reply.
void testMalformedRequestIsRefused()
{
    SocketPair pair;
    ClientSocket server(pair.m_fd[1]);
    ClientSocket client(pair.m_fd[0]);

    // ClientClose takes four bytes; this one claims seven.
    std::array<const char, 7> payload = { };
    MsgHeader header = makeJackMsgHeader(Request::kClientClose, 7, 9);
    CHECK(sendJackMessage(&client, header, payload.data(), sizeof(payload)) == 0);

    MsgHeader got { };
    CHECK(readJackMsgHeader(&server, got) == 0);
    RequestDecoder decoder(nullptr, nullptr);
    CHECK(decoder.handleRequest(&server, got) < 0);

    char byte;
    CHECK(::recv(pair.m_fd[0], &byte, 1, MSG_DONTWAIT) < 0); // nothing came back
}

// A client of another protocol version is refused: the reply carries the Protocol error, with the server's version.
void testVersionMismatchIsRefused()
{
    SocketPair pair;
    ClientSocket server(pair.m_fd[1]);
    ClientSocket client(pair.m_fd[0]);

    MsgHeader old = makeJackMsgHeader(Request::kClientCheck, 0, 77);
    old.m_version = 9;
    CHECK(client.write(&old, sizeof(old)) == 0);

    MsgHeader got { };
    CHECK(readJackMsgHeader(&server, got) == 0);
    CHECK(got.m_version == 9);
    RequestDecoder decoder(nullptr, nullptr);
    CHECK(decoder.handleRequest(&server, got) < 0);

    // The client, of the current version, reads the refusal as a reply to its request.
    Reply res;
    CHECK(readJackReply(&client, Request::kClientCheck, 77, res) == 0);
    CHECK(res.m_result == kJackMsgProtocolError);
}

// The client refuses a reply of another version, and one with no magic number.
void testBadHeadersAreRefused()
{
    {
        SocketPair pair;
        ClientSocket server(pair.m_fd[1]);
        ClientSocket client(pair.m_fd[0]);
        const char garbage[sizeof(MsgHeader)] = { 'x', 'y', 'z', 'w' };
        CHECK(client.write(const_cast<char*>(garbage), sizeof(garbage)) == 0);
        MsgHeader got { };
        CHECK(readJackMsgHeader(&server, got) < 0);
    }
    {
        SocketPair pair;
        ClientSocket server(pair.m_fd[1]);
        ClientSocket client(pair.m_fd[0]);
        Reply res(0);
        MsgHeader reply = makeJackMsgHeader(Request::kSetBufferSize, 4, 11);
        reply.m_version = 9;
        sendJackMessage(&server, reply, &res.m_result, sizeof(res.m_result));
        Reply back;
        CHECK(readJackReply(&client, Request::kSetBufferSize, 11, back) < 0);
    }
}

// A reply that does not answer the request, or whose payload is not the reply's length, is refused.
void testMismatchedRepliesAreRefused()
{
    {
        SocketPair pair;
        ClientSocket server(pair.m_fd[1]);
        ClientSocket client(pair.m_fd[0]);
        Reply res(0);
        sendJackReply(&server, res, makeJackMsgHeader(Request::kSetBufferSize, 0, 12));
        Reply back;
        CHECK(readJackReply(&client, Request::kSetBufferSize, 13, back) < 0); // another id
    }
    {
        SocketPair pair;
        ClientSocket server(pair.m_fd[1]);
        ClientSocket client(pair.m_fd[0]);
        const std::uint16_t two = 0;
        sendJackMessage(&server, makeJackMsgHeader(Request::kSetBufferSize, sizeof(two), 14), &two, sizeof(two));
        Reply back;
        CHECK(readJackReply(&client, Request::kSetBufferSize, 14, back) < 0); // a result is four bytes
    }
}

// A synchronous notification gets the client's reply, which echoes the notification's id.
void testSynchronousNotification()
{
    SocketPair pair;
    ClientSocket server(pair.m_fd[0]);
    ClientSocket client(pair.m_fd[1]);

    const std::uint32_t request = nextJackRequestId();
    ClientNotification notice("system", 3, kAddClient, 1, "", 0, 0);
    CHECK(notice.write(&server, request) == 0);

    MsgHeader header { };
    CHECK(readJackMsgHeader(&client, header) == 0);
    CHECK(header.m_type == kAddClient);
    CHECK(header.m_request == request);
    CHECK(header.m_length == ClientNotification::kPayloadSize);
    ClientNotification got;
    PayloadReader payload(&client, header.m_length, { });
    CHECK(got.read(&payload) == 0 && payload.done());
    CHECK(std::strcmp(got.m_name.data(), "system") == 0);

    Reply res(0);
    CHECK(sendJackReply(&client, res, header) == 0);
    Reply back;
    CHECK(readJackReply(&server, kAddClient, request, back) == 0);
    CHECK(back.m_result == 0);
}

} // namespace

int main()
{
    testRequestsGetTheirReplies();
    testMalformedRequestIsRefused();
    testVersionMismatchIsRefused();
    testBadHeadersAreRefused();
    testMismatchedRepliesAreRefused();
    testSynchronousNotification();

    if (gFailures == 0) {
        std::printf("protocol conformance checks passed\n");
        return 0;
    }
    std::printf("%d protocol conformance check(s) failed\n", gFailures);
    return 1;
}
