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

#pragma once

#include "JackChannel.h"
#include "JackConstants.h"

#include <cstddef>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

namespace jackmini {

// Every message on the control socket (requests and replies) starts with this 16-byte header (protocol.md section 2).
// The payload follows, `length` bytes long.  A reply carries the type and request id of the request it answers.
struct MsgHeader {
    std::uint32_t m_magic;
    std::uint16_t m_version;
    std::uint16_t m_type;
    std::uint32_t m_length;
    std::uint32_t m_request;
};
static_assert(sizeof(MsgHeader) == 16);

constexpr std::uint32_t kJackMsgMagic = 0x4a4d4e53; // "JMNS"
constexpr std::uint16_t kJackMsgVersion = kProtocolVersion;
constexpr std::int32_t kJackMsgProtocolError = 7; // jackmini::Error::Protocol (protocol.md section 7)
// Largest payload of a message.  The frame is built on the stack, so nothing allocates on the way out: notifications
// are sent from the realtime thread.
constexpr std::uint32_t kJackMsgMaxPayload = 4096;

// A new request id for each request sent.  Replies echo it, so a reply can be matched to its request.
inline std::uint32_t nextJackRequestId()
{
    static std::atomic<std::uint32_t> next { 1 };
    return next++;
}

// Builds a message from a payload and sends it in one write.
inline MsgHeader makeJackMsgHeader(std::uint16_t type, std::uint32_t length, std::uint32_t request)
{
    return MsgHeader { kJackMsgMagic, kJackMsgVersion, type, length, request };
}

// Sends a header and its payload together, with the descriptors (if any) in the same message.
inline std::int32_t sendJackMessage(detail::ChannelTransactionInterface* trans, const MsgHeader& header,
    const void* payload, std::uint32_t length, std::span<const std::int32_t> fds = { })
{
    if (length > kJackMsgMaxPayload) {
        return -1;
    }
    std::array<std::byte, sizeof(MsgHeader) + kJackMsgMaxPayload> frame;
    std::memcpy(frame.data(), &header, sizeof(MsgHeader));
    if (length != 0) {
        std::memcpy(frame.data() + sizeof(MsgHeader), payload, length);
    }
    return trans->writeWithFds(frame.data(), static_cast<std::int32_t>(sizeof(MsgHeader) + length), fds);
}

// Reads a header.  A header without the magic number is not one of ours, and the read fails.
inline std::int32_t readJackMsgHeader(detail::ChannelTransactionInterface* trans, MsgHeader& header)
{
    if (trans->read(&header, sizeof(MsgHeader)) < 0) {
        return -1;
    }
    return (header.m_magic == kJackMsgMagic) ? 0 : -1;
}

// Collects a reply's payload and descriptors, so that it can be sent as one framed message.
class PayloadWriter : public detail::ChannelTransactionInterface {
public:
    std::vector<std::byte> m_data;
    std::vector<std::int32_t> m_fds;

    std::int32_t read(void* data, std::int32_t len) override
    {
        return -1;
    }

    std::int32_t write(void* data, std::int32_t len) override
    {
        const auto* bytes = static_cast<const std::byte*>(data);
        m_data.insert(m_data.end(), bytes, bytes + len);
        return 0;
    }

    std::int32_t writeWithFds(void* data, std::int32_t len, std::span<const std::int32_t> fds) override
    {
        m_fds.insert(m_fds.end(), fds.begin(), fds.end());
        return write(data, len);
    }
};

// The payload of a message that has been read, as a transaction.  The payload's length is the header's, and it must be
// read in full: reading past it, or stopping short, is a protocol error.  Descriptors received with the header are
// handed to the first read that asks for them.
class PayloadReader : public detail::ChannelTransactionInterface {
public:
    PayloadReader(detail::ChannelTransactionInterface* socket, std::uint32_t length, std::vector<UniqueFd>&& fds)
        : m_socket(socket)
        , m_remaining(length)
        , m_fds(std::move(fds))
    {
    }

    std::int32_t read(void* data, std::int32_t len) override
    {
        if (static_cast<std::uint32_t>(len) > m_remaining) {
            return -1;
        }
        const std::int32_t res = m_socket->read(data, len);
        m_remaining -= static_cast<std::uint32_t>(len);
        return res;
    }

    std::int32_t write(void* data, std::int32_t len) override
    {
        return -1;
    }

    std::int32_t readWithFds(void* data, std::int32_t len, std::vector<UniqueFd>& fds) override
    {
        fds = std::move(m_fds);
        m_fds.clear();
        return read(data, len);
    }

    // True when the whole payload has been read.
    bool done() const
    {
        return m_remaining == 0;
    }

private:
    detail::ChannelTransactionInterface* m_socket;
    std::uint32_t m_remaining;
    std::vector<UniqueFd> m_fds;
};

} // namespace jackmini
