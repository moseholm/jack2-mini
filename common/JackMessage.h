/*
Copyright (C) 2026 JACK contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#ifndef __JackMessage__
#define __JackMessage__

#include "JackChannel.h"
#include "JackConstants.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

namespace jackmini {

// Every message on the control socket (requests and replies) starts with this 16-byte header (protocol.md section 2).
// The payload follows, `length` bytes long.  A reply carries the type and request id of the request it answers.
struct JackMsgHeader {
    std::uint32_t magic;
    std::uint16_t version;
    std::uint16_t type;
    std::uint32_t length;
    std::uint32_t request;
};
static_assert(sizeof(JackMsgHeader) == 16);

constexpr std::uint32_t kJackMsgMagic = 0x4a4d4e53; // "JMNS"
constexpr std::uint16_t kJackMsgVersion = JACK_PROTOCOL_VERSION;
constexpr std::int32_t kJackMsgProtocolError = 7; // jackmini::Error::Protocol (protocol.md section 7)
// Largest payload of a message.  The frame is built on the stack, so nothing allocates on the way out: notifications
// are sent from the realtime thread.
constexpr std::uint32_t kJackMsgMaxPayload = 4096;

// A new request id for each request sent.  Replies echo it, so a reply can be matched to its request.
inline std::uint32_t NextJackRequestId()
{
    static std::atomic<std::uint32_t> next { 1 };
    return next++;
}

// Builds a message from a payload and sends it in one write.
inline JackMsgHeader MakeJackMsgHeader(std::uint16_t type, std::uint32_t length, std::uint32_t request)
{
    return JackMsgHeader { kJackMsgMagic, kJackMsgVersion, type, length, request };
}

// Sends a header and its payload together, with the descriptors (if any) in the same message.
inline int SendJackMessage(detail::JackChannelTransactionInterface* trans, const JackMsgHeader& header,
    const void* payload, std::uint32_t length, std::span<const int> fds = { })
{
    if (length > kJackMsgMaxPayload) {
        return -1;
    }
    std::array<std::byte, sizeof(JackMsgHeader) + kJackMsgMaxPayload> frame;
    std::memcpy(frame.data(), &header, sizeof(JackMsgHeader));
    if (length != 0) {
        std::memcpy(frame.data() + sizeof(JackMsgHeader), payload, length);
    }
    return trans->WriteWithFds(frame.data(), static_cast<int>(sizeof(JackMsgHeader) + length), fds);
}

// Reads a header.  A header without the magic number is not one of ours, and the read fails.
inline int ReadJackMsgHeader(detail::JackChannelTransactionInterface* trans, JackMsgHeader& header)
{
    if (trans->Read(&header, sizeof(JackMsgHeader)) < 0) {
        return -1;
    }
    return (header.magic == kJackMsgMagic) ? 0 : -1;
}

// Collects a reply's payload and descriptors, so that it can be sent as one framed message.
class JackPayloadWriter : public detail::JackChannelTransactionInterface {
public:
    std::vector<std::byte> fData;
    std::vector<int> fFds;

    int Read(void* data, int len) override
    {
        return -1;
    }

    int Write(void* data, int len) override
    {
        const auto* bytes = static_cast<const std::byte*>(data);
        fData.insert(fData.end(), bytes, bytes + len);
        return 0;
    }

    int WriteWithFds(void* data, int len, std::span<const int> fds) override
    {
        fFds.insert(fFds.end(), fds.begin(), fds.end());
        return Write(data, len);
    }
};

// The payload of a message that has been read, as a transaction.  The payload's length is the header's, and it must be
// read in full: reading past it, or stopping short, is a protocol error.  Descriptors received with the header are
// handed to the first read that asks for them.
class JackPayloadReader : public detail::JackChannelTransactionInterface {
public:
    JackPayloadReader(detail::JackChannelTransactionInterface* socket, std::uint32_t length, std::vector<UniqueFd>&& fds)
        : fSocket(socket)
        , fRemaining(length)
        , fFds(std::move(fds))
    {
    }

    int Read(void* data, int len) override
    {
        if (static_cast<std::uint32_t>(len) > fRemaining) {
            return -1;
        }
        const int res = fSocket->Read(data, len);
        fRemaining -= static_cast<std::uint32_t>(len);
        return res;
    }

    int Write(void* data, int len) override
    {
        return -1;
    }

    int ReadWithFds(void* data, int len, std::vector<UniqueFd>& fds) override
    {
        fds = std::move(fFds);
        fFds.clear();
        return Read(data, len);
    }

    // True when the whole payload has been read.
    bool Done() const
    {
        return fRemaining == 0;
    }

private:
    detail::JackChannelTransactionInterface* fSocket;
    std::uint32_t fRemaining;
    std::vector<UniqueFd> fFds;
};

} // namespace jackmini

#endif
