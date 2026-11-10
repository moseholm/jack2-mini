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

#include "JackMessage.h"
#include "JackNotification.h"
#include "JackRequest.h"
#include "JackRequestDecoder.h"
#include "JackSocket.h"

#include <sys/socket.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

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

// A connected pair of stream sockets, closed on destruction.
struct SocketPair {
    int fd[2] = { -1, -1 };

    SocketPair()
    {
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, fd) != 0) {
            std::printf("socketpair failed\n");
            gFailures++;
        }
    }
    ~SocketPair()
    {
        ::close(fd[0]);
        ::close(fd[1]);
    }
};

// The server's side of the conversation: decodes each request it is sent and replies to it.  It keeps what it was sent,
// so the test can check what the client's request carried.
class FakeServer {
public:
    explicit FakeServer(int fd)
        : fSocket(fd)
    {
    }

    void Start()
    {
        fThread = std::thread([this] { Run(); });
    }

    void Join()
    {
        if (fThread.joinable()) {
            fThread.join();
        }
    }

    jack_nframes_t fBufferSize = 0;
    int fClosedRefNum = -1;
    jack_port_id_t fRenamedPort = NO_PORT;
    std::string fRenamedTo;
    std::string fUUIDFor;
    int fRequestsServed = 0;

private:
    JackClientSocket fSocket;
    std::thread fThread;

    // Decodes a request payload: the length is the request's own, and it must be read in full.
    template <class Req>
    bool Decode(const JackMsgHeader& header, Req& req)
    {
        if (header.length != Req::Size()) {
            return false;
        }
        JackPayloadReader payload(&fSocket, header.length, { });
        return req.Read(&payload) == 0 && payload.Done();
    }

    void Run()
    {
        for (;;) {
            JackMsgHeader header { };
            if (ReadJackMsgHeader(&fSocket, header) < 0) {
                return; // the client closed the connection
            }
            fRequestsServed++;
            switch (header.type) {
            case JackRequest::kSetBufferSize: {
                JackSetBufferSizeRequest req;
                if (!Decode(header, req)) {
                    return;
                }
                fBufferSize = req.fBufferSize;
                JackResult res(0);
                SendJackReply(&fSocket, res, header);
                break;
            }
            case JackRequest::kClientClose: {
                JackClientCloseRequest req;
                if (!Decode(header, req)) {
                    return;
                }
                fClosedRefNum = req.fRefNum;
                JackResult res(0);
                SendJackReply(&fSocket, res, header);
                break;
            }
            case JackRequest::kPortRename: {
                JackPortRenameRequest req;
                if (!Decode(header, req)) {
                    return;
                }
                fRenamedPort = req.fPort;
                fRenamedTo = req.fName;
                JackResult res(0);
                SendJackReply(&fSocket, res, header);
                break;
            }
            case JackRequest::kGetUUIDByClient: {
                JackGetUUIDRequest req;
                if (!Decode(header, req)) {
                    return;
                }
                fUUIDFor = req.fName;
                JackUUIDResult res;
                std::snprintf(res.fUUID, sizeof(res.fUUID), "%s", "00000000-0000-0000-0000-000000000042");
                res.fResult = 0;
                SendJackReply(&fSocket, res, header);
                break;
            }
            case JackRequest::kComputeTotalLatencies: {
                JackComputeTotalLatenciesRequest req;
                if (!Decode(header, req)) {
                    return;
                }
                JackResult res(0);
                SendJackReply(&fSocket, res, header);
                break;
            }
            default:
                return;
            }
        }
    }
};

// A well-formed request is answered by its reply, with the request's type and id echoed.
void TestRequestsGetTheirReplies()
{
    SocketPair pair;
    FakeServer server(pair.fd[1]);
    server.Start();
    JackClientSocket client(pair.fd[0]);

    JackSetBufferSizeRequest setBuffer(1024);
    CHECK(setBuffer.Write(&client) == 0);
    JackResult res1;
    CHECK(ReadJackReply(&client, setBuffer.fType, setBuffer.fRequestId, res1) == 0);
    CHECK(res1.fResult == 0);

    JackClientCloseRequest close(7);
    CHECK(close.Write(&client) == 0);
    JackResult res2;
    CHECK(ReadJackReply(&client, close.fType, close.fRequestId, res2) == 0);

    JackPortRenameRequest rename(3, 5, "renamed:port");
    CHECK(rename.Write(&client) == 0);
    JackResult res3;
    CHECK(ReadJackReply(&client, rename.fType, rename.fRequestId, res3) == 0);

    JackGetUUIDRequest uuid("some client");
    CHECK(uuid.Write(&client) == 0);
    JackUUIDResult res4;
    CHECK(ReadJackReply(&client, uuid.fType, uuid.fRequestId, res4) == 0);
    CHECK(std::strcmp(res4.fUUID, "00000000-0000-0000-0000-000000000042") == 0);

    JackComputeTotalLatenciesRequest latencies;
    CHECK(latencies.Write(&client) == 0);
    JackResult res5;
    CHECK(ReadJackReply(&client, latencies.fType, latencies.fRequestId, res5) == 0);

    ::shutdown(pair.fd[0], SHUT_WR);
    server.Join();

    CHECK(server.fBufferSize == 1024);
    CHECK(server.fClosedRefNum == 7);
    CHECK(server.fRenamedPort == 5);
    CHECK(server.fRenamedTo == "renamed:port");
    CHECK(server.fUUIDFor == "some client");
    CHECK(server.fRequestsServed == 5);
}

// A request whose length is not its own is refused by the decoder, which sends no reply.
void TestMalformedRequestIsRefused()
{
    SocketPair pair;
    JackClientSocket server(pair.fd[1]);
    JackClientSocket client(pair.fd[0]);

    // ClientClose takes four bytes; this one claims seven.
    const char payload[7] = { };
    JackMsgHeader header = MakeJackMsgHeader(JackRequest::kClientClose, 7, 9);
    CHECK(SendJackMessage(&client, header, payload, sizeof(payload)) == 0);

    JackMsgHeader got { };
    CHECK(ReadJackMsgHeader(&server, got) == 0);
    JackRequestDecoder decoder(nullptr, nullptr);
    CHECK(decoder.HandleRequest(&server, got) < 0);

    char byte;
    CHECK(::recv(pair.fd[0], &byte, 1, MSG_DONTWAIT) < 0); // nothing came back
}

// A client of another protocol version is refused: the reply carries the Protocol error, with the server's version.
void TestVersionMismatchIsRefused()
{
    SocketPair pair;
    JackClientSocket server(pair.fd[1]);
    JackClientSocket client(pair.fd[0]);

    JackMsgHeader old = MakeJackMsgHeader(JackRequest::kClientCheck, 0, 77);
    old.version = 9;
    CHECK(client.Write(&old, sizeof(old)) == 0);

    JackMsgHeader got { };
    CHECK(ReadJackMsgHeader(&server, got) == 0);
    CHECK(got.version == 9);
    JackRequestDecoder decoder(nullptr, nullptr);
    CHECK(decoder.HandleRequest(&server, got) < 0);

    // The client, of the current version, reads the refusal as a reply to its request.
    JackResult res;
    CHECK(ReadJackReply(&client, JackRequest::kClientCheck, 77, res) == 0);
    CHECK(res.fResult == kJackMsgProtocolError);
}

// The client refuses a reply of another version, and one with no magic number.
void TestBadHeadersAreRefused()
{
    {
        SocketPair pair;
        JackClientSocket server(pair.fd[1]);
        JackClientSocket client(pair.fd[0]);
        const char garbage[sizeof(JackMsgHeader)] = { 'x', 'y', 'z', 'w' };
        CHECK(client.Write(const_cast<char*>(garbage), sizeof(garbage)) == 0);
        JackMsgHeader got { };
        CHECK(ReadJackMsgHeader(&server, got) < 0);
    }
    {
        SocketPair pair;
        JackClientSocket server(pair.fd[1]);
        JackClientSocket client(pair.fd[0]);
        JackResult res(0);
        JackMsgHeader reply = MakeJackMsgHeader(JackRequest::kSetBufferSize, 4, 11);
        reply.version = 9;
        SendJackMessage(&server, reply, &res.fResult, sizeof(res.fResult));
        JackResult back;
        CHECK(ReadJackReply(&client, JackRequest::kSetBufferSize, 11, back) < 0);
    }
}

// A reply that does not answer the request, or whose payload is not the reply's length, is refused.
void TestMismatchedRepliesAreRefused()
{
    {
        SocketPair pair;
        JackClientSocket server(pair.fd[1]);
        JackClientSocket client(pair.fd[0]);
        JackResult res(0);
        SendJackReply(&server, res, MakeJackMsgHeader(JackRequest::kSetBufferSize, 0, 12));
        JackResult back;
        CHECK(ReadJackReply(&client, JackRequest::kSetBufferSize, 13, back) < 0); // another id
    }
    {
        SocketPair pair;
        JackClientSocket server(pair.fd[1]);
        JackClientSocket client(pair.fd[0]);
        const std::uint16_t two = 0;
        SendJackMessage(&server, MakeJackMsgHeader(JackRequest::kSetBufferSize, sizeof(two), 14), &two, sizeof(two));
        JackResult back;
        CHECK(ReadJackReply(&client, JackRequest::kSetBufferSize, 14, back) < 0); // a result is four bytes
    }
}

// A synchronous notification gets the client's reply, which echoes the notification's id.
void TestSynchronousNotification()
{
    SocketPair pair;
    JackClientSocket server(pair.fd[0]);
    JackClientSocket client(pair.fd[1]);

    const std::uint32_t request = NextJackRequestId();
    JackClientNotification notice("system", 3, kAddClient, 1, "", 0, 0);
    CHECK(notice.Write(&server, request) == 0);

    JackMsgHeader header { };
    CHECK(ReadJackMsgHeader(&client, header) == 0);
    CHECK(header.type == kAddClient);
    CHECK(header.request == request);
    CHECK(header.length == JackClientNotification::PayloadSize);
    JackClientNotification got;
    JackPayloadReader payload(&client, header.length, { });
    CHECK(got.Read(&payload) == 0 && payload.Done());
    CHECK(std::strcmp(got.fName, "system") == 0);

    JackResult res(0);
    CHECK(SendJackReply(&client, res, header) == 0);
    JackResult back;
    CHECK(ReadJackReply(&server, kAddClient, request, back) == 0);
    CHECK(back.fResult == 0);
}

} // namespace

int main()
{
    TestRequestsGetTheirReplies();
    TestMalformedRequestIsRefused();
    TestVersionMismatchIsRefused();
    TestBadHeadersAreRefused();
    TestMismatchedRepliesAreRefused();
    TestSynchronousNotification();

    if (gFailures == 0) {
        std::printf("protocol conformance checks passed\n");
        return 0;
    }
    std::printf("%d protocol conformance check(s) failed\n", gFailures);
    return 1;
}
