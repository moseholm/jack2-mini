// Copyright (C) 2001 Paul Davis
// Copyright (C) 2004-2008 Grame
// Copyright (C) 2016-2026 Filipe Coelho
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

#pragma once

#include "JackCString.h"
#include "JackChannel.h"
#include "JackConstants.h"
#include "JackError.h"
#include "JackMessage.h"
#include "JackPlatformPlug.h"
#include "JackShmMem.h"
#include "JackTime.h"
#include "types.h"
#include <cstddef>
#include <cstdint>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <array>
#include <list>
#include <vector>

namespace jackmini {

inline constexpr std::int32_t kClientNameSize1 = (kClientNameSize + 1);
inline constexpr std::int32_t kMessageSize1 = (kMessageSize + 1);
inline constexpr std::int32_t kPortNameSize1 = (kPortNameSize + 1);
inline constexpr std::int32_t kPortTypeSize1 = (kPortTypeSize + 1);
inline constexpr std::int32_t kRealJackPortNameSize1 = (kRealJackPortNameSize + 1);

#define CheckRes(exp)                                                                \
    {                                                                                \
        std::int32_t reserr = (exp);                                                          \
        if (reserr < 0) {                                                            \
            if (reserr != kRequestErrAborted)                                  \
                jack_error("CheckRes error for " #exp "in %s", __PRETTY_FUNCTION__); \
            return reserr;                                                           \
        }                                                                            \
    }


// \brief Request from client to server.

struct Request {

    enum RequestType {
        kRegisterPort = 1,
        kUnRegisterPort = 2,
        kConnectPorts = 3,
        kDisconnectPorts = 4,
        kSetTimeBaseClient = 5,
        kActivateClient = 6,
        kDeactivateClient = 7,
        kDisconnectPort = 8,
        kSetClientCapabilities = 9,
        kGetPortConnections = 10,
        kGetPortNConnections = 11,
        kReleaseTimebase = 12,
        kSetTimebaseCallback = 13,
        kSetBufferSize = 14,
        kSetFreeWheel = 15,
        kClientCheck = 16,
        kClientOpen = 17,
        kClientClose = 18,
        kConnectNamePorts = 19,
        kDisconnectNamePorts = 20,
        kPortRename = 21,
        kNotification = 22,
        kGetClientByUUID = 23,
        kReserveClientName = 24,
        kGetUUIDByClient = 25,
        kComputeTotalLatencies = 26
    };

    RequestType m_type;
    std::uint32_t m_requestId;

    Request(RequestType type)
        : m_type(type)
        , m_requestId(nextJackRequestId())
    {
    }

    virtual ~Request()
    {
    }

    virtual std::int32_t read(detail::ChannelTransactionInterface* trans) = 0;

    virtual std::int32_t write(detail::ChannelTransactionInterface* trans) = 0;
};

template <std::int32_t DataSize>
struct RequestImpl : Request {
    static constexpr std::int32_t kTotalSize = DataSize;
    static_assert(DataSize <= static_cast<std::int32_t>(kJackMsgMaxPayload), "request payload larger than a message");

    RequestImpl(RequestType type)
        : Request(type)
        , m_packetInit(false)
        , m_packetPos(0)
    {
    }

    std::int32_t writePacketInit()
    {
        if (m_type == 0) {
            jack_error("JackRequestImpl::WritePacketInit error unspecified type");
            return -1;
        }
        if (m_packetInit) {
            jack_error("JackRequestImpl::WritePacketInit error called more than once for type %d", m_type);
            return -1;
        }
        m_packetInit = true;
        m_packetPos = 0;
        return 0;
    }

    std::int32_t writePacket(void* data, std::int32_t len)
    {
        if (!m_packetInit) {
            jack_error("JackRequestImpl::WritePacket failed for type %d because it has not been initialized", m_type);
            return -1;
        }
        if (m_packetPos == -1) {
            jack_error("JackRequestImpl::WritePacket failed for type %d because it has already been sent", m_type);
            return -1;
        }
        if (m_packetPos + len > kTotalSize) {
            jack_error("JackRequestImpl::WritePacket failed for type %d because of too much data, pos %d, len %d, total size %d",
                m_type, m_packetPos, len, kTotalSize);
            return -1;
        }
        jack_log("JackRequestImpl::WritePacket ok for type %d, pos %d, len %d, total size %d (new pos %d)",
            m_type, m_packetPos, len, kTotalSize, m_packetPos + len);
        memcpy(m_packetData.data() + m_packetPos, data, len);
        m_packetPos += len;
        return 0;
    }

    std::int32_t write(detail::ChannelTransactionInterface* trans) override
    {
        if (!m_packetInit || m_packetPos != kTotalSize) {
            jack_error("JackRequestImpl::Write failed for type %d because of incomplete data, pos %d, total size %d",
                m_type, m_packetPos, kTotalSize);
            return -1;
        }
        // invalidate any more writes
        m_packetPos = -1;
        const std::int32_t err = sendJackMessage(trans, makeJackMsgHeader(m_type, kTotalSize, m_requestId), m_packetData.data(), kTotalSize);
        if (err < 0 && err != kRequestErrAborted)
            jack_error("JackRequestImpl::Write failed for type %d", m_type);
        return err;
    }

    static constexpr std::int32_t size() { return DataSize; }

private:
    std::array<std::uint8_t, kTotalSize> m_packetData { };
    bool m_packetInit;
    std::int32_t m_packetPos;
};

// \brief Result from the server.

struct Reply {

    std::int32_t m_result;

    Reply()
        : m_result(-1)
    {
    }
    Reply(std::int32_t result)
        : m_result(result)
    {
    }
    virtual ~Reply()
    {
    }

    virtual std::int32_t read(detail::ChannelTransactionInterface* trans)
    {
        return trans->read(&m_result, sizeof(std::int32_t));
    }

    virtual std::int32_t write(detail::ChannelTransactionInterface* trans)
    {
        return trans->write(&m_result, sizeof(std::int32_t));
    }
};

// The reply to a request: the request's type and id, and the result as the payload.
inline std::int32_t sendJackReply(detail::ChannelTransactionInterface* trans, Reply& res, const MsgHeader& request)
{
    PayloadWriter payload;
    if (res.write(&payload) < 0) {
        return -1;
    }
    const auto length = static_cast<std::uint32_t>(payload.m_data.size());
    return sendJackMessage(trans, makeJackMsgHeader(request.m_type, length, request.m_request), payload.m_data.data(), length, payload.m_fds);
}

// Reads the reply to the message with the given type and id.  The header must match it and carry our protocol version,
// and the payload must be read in full.
inline std::int32_t readJackReply(detail::ClientRequestInterface* socket, std::uint16_t type, std::uint32_t request, Reply& res)
{
    MsgHeader header { };
    std::vector<UniqueFd> fds;
    if (socket->readWithFds(&header, sizeof(header), fds) < 0) {
        return -1;
    }
    if (header.m_magic != kJackMsgMagic) {
        jack_error("JACK reply without the protocol magic number");
        return -1;
    }
    if (header.m_version != kJackMsgVersion) {
        jack_error("JACK protocol mismatch (server %d, client %d)", header.m_version, kJackMsgVersion);
        return -1;
    }
    if (header.m_request != request || header.m_type != type) {
        jack_error("JACK reply does not match its request, type %d request %d", type, request);
        return -1;
    }

    PayloadReader payload(socket, header.m_length, std::move(fds));
    if (res.read(&payload) < 0 || !payload.done()) {
        return -1;
    }
    return 0;
}

// \brief CheckClient request.

struct ClientCheckRequest : public RequestImpl<kClientNameSize1 + 3 * sizeof(std::int32_t) + sizeof(UuidC)> {

    std::array<char, kClientNameSize1> m_name;
    std::int32_t m_protocol;
    std::int32_t m_options;
    std::int32_t m_open;
    UuidC m_uUID;

    ClientCheckRequest()
        : RequestImpl(kClientCheck)
        , m_protocol(0)
        , m_options(0)
        , m_open(0)
        , m_uUID(kUuidEmptyInitializer)
    {
        memset(m_name.data(), 0, sizeof(m_name));
    }

    ClientCheckRequest(const char* name, std::int32_t protocol, std::int32_t options, UuidC uuid, std::int32_t open = false)
        : RequestImpl(kClientCheck)
        , m_protocol(protocol)
        , m_options(options)
        , m_open(open)
        , m_uUID(uuid)
    {
        memset(m_name.data(), 0, sizeof(m_name));
        copyCString(m_name, name);
    }

    std::int32_t read(detail::ChannelTransactionInterface* trans) override
    {
        CheckRes(trans->read(&m_name, sizeof(m_name)));
        CheckRes(trans->read(&m_protocol, sizeof(std::int32_t)));
        CheckRes(trans->read(&m_options, sizeof(std::int32_t)));
        CheckRes(trans->read(&m_uUID, sizeof(UuidC)));
        return trans->read(&m_open, sizeof(std::int32_t));
    }

    std::int32_t write(detail::ChannelTransactionInterface* trans) override
    {
        CheckRes(writePacketInit());
        CheckRes(writePacket(&m_name, sizeof(m_name)));
        CheckRes(writePacket(&m_protocol, sizeof(std::int32_t)));
        CheckRes(writePacket(&m_options, sizeof(std::int32_t)));
        CheckRes(writePacket(&m_uUID, sizeof(UuidC)));
        CheckRes(writePacket(&m_open, sizeof(std::int32_t)));
        return RequestImpl::write(trans);
    }

    static constexpr std::int32_t size() { return sizeof(m_name) + 3 * sizeof(std::int32_t) + sizeof(UuidC); }
};

// \brief CheckClient result.

struct ClientCheckResult : public Reply {

    std::array<char, kClientNameSize + 1> m_name;
    std::int32_t m_status;

    ClientCheckResult()
        : Reply()
        , m_status(0)
    {
        memset(m_name.data(), 0, sizeof(m_name));
    }
    ClientCheckResult(std::int32_t result, const char* name, std::int32_t status)
        : Reply(result)
        , m_status(status)
    {
        memset(m_name.data(), 0, sizeof(m_name));
        copyCString(m_name, name);
    }

    std::int32_t read(detail::ChannelTransactionInterface* trans) override
    {
        CheckRes(Reply::read(trans));
        CheckRes(trans->read(&m_name, sizeof(m_name)));
        CheckRes(trans->read(&m_status, sizeof(std::int32_t)));
        return 0;
    }

    std::int32_t write(detail::ChannelTransactionInterface* trans) override
    {
        CheckRes(Reply::write(trans));
        CheckRes(trans->write(&m_name, sizeof(m_name)));
        CheckRes(trans->write(&m_status, sizeof(std::int32_t)));
        return 0;
    }
};

// \brief NewClient request.

struct ClientOpenRequest : public RequestImpl<sizeof(std::int32_t) + sizeof(UuidC) + kClientNameSize1> {

    std::int32_t m_pID;
    UuidC m_uUID;
    std::array<char, kClientNameSize1> m_name;

    ClientOpenRequest()
        : RequestImpl(kClientOpen)
        , m_pID(0)
        , m_uUID(kUuidEmptyInitializer)
    {
        memset(m_name.data(), 0, sizeof(m_name));
    }

    ClientOpenRequest(const char* name, std::int32_t pid, UuidC uuid)
        : RequestImpl(kClientOpen)
    {
        memset(m_name.data(), 0, sizeof(m_name));
        copyCString(m_name, name);
        m_pID = pid;
        m_uUID = uuid;
    }

    std::int32_t read(detail::ChannelTransactionInterface* trans) override
    {
        CheckRes(trans->read(&m_pID, sizeof(std::int32_t)));
        CheckRes(trans->read(&m_uUID, sizeof(UuidC)));
        return trans->read(&m_name, sizeof(m_name));
    }

    std::int32_t write(detail::ChannelTransactionInterface* trans) override
    {
        CheckRes(writePacketInit());
        CheckRes(writePacket(&m_pID, sizeof(std::int32_t)));
        CheckRes(writePacket(&m_uUID, sizeof(UuidC)));
        CheckRes(writePacket(&m_name, sizeof(m_name)));
        return RequestImpl::write(trans);
    }
};

// \brief NewClient result.
//
// The payload is the status and the sizes of the three segments. The descriptors go with it as SCM_RIGHTS, in one message, in the order engine, graph, client control.

struct ClientOpenResultPayload {
    std::int32_t m_result;
    std::uint32_t m_engineSize;
    std::uint32_t m_graphSize;
    std::uint32_t m_clientSize;
};
static_assert(sizeof(ClientOpenResultPayload) == 16);

struct ClientOpenResult : public Reply {

    ShmSegment m_sharedEngine;
    ShmSegment m_sharedClient;
    ShmSegment m_sharedGraph;

    ClientOpenResult()
        : Reply()
    {
    }

    std::int32_t read(detail::ChannelTransactionInterface* trans) override
    {
        ClientOpenResultPayload payload { };
        std::vector<UniqueFd> fds;
        CheckRes(trans->readWithFds(&payload, sizeof(payload), fds));
        m_result = payload.m_result;
        if (m_result != 0) {
            return 0;
        }
        if (fds.size() != 3) {
            jack_error("JackClientOpenResult::Read expected 3 descriptors, got %zu", fds.size());
            return -1;
        }
        m_sharedEngine = ShmSegment { fds[0].release(), payload.m_engineSize };
        m_sharedGraph = ShmSegment { fds[1].release(), payload.m_graphSize };
        m_sharedClient = ShmSegment { fds[2].release(), payload.m_clientSize };
        return 0;
    }

    std::int32_t write(detail::ChannelTransactionInterface* trans) override
    {
        ClientOpenResultPayload payload { m_result, m_sharedEngine.m_size, m_sharedGraph.m_size, m_sharedClient.m_size };
        if (m_result != 0) {
            return trans->writeWithFds(&payload, sizeof(payload), { });
        }
        const std::array<std::int32_t, 3> fds { m_sharedEngine.m_fd, m_sharedGraph.m_fd, m_sharedClient.m_fd };
        return trans->writeWithFds(&payload, sizeof(payload), fds);
    }
};

// \brief CloseClient request.

struct ClientCloseRequest : public RequestImpl<sizeof(std::int32_t)> {

    std::int32_t m_refNum;

    ClientCloseRequest()
        : RequestImpl(kClientClose)
        , m_refNum(0)
    {
    }

    ClientCloseRequest(std::int32_t refnum)
        : RequestImpl(kClientClose)
        , m_refNum(refnum)
    {
    }

    std::int32_t read(detail::ChannelTransactionInterface* trans) override
    {
        return trans->read(&m_refNum, sizeof(std::int32_t));
    }

    std::int32_t write(detail::ChannelTransactionInterface* trans) override
    {
        CheckRes(writePacketInit());
        CheckRes(writePacket(&m_refNum, sizeof(std::int32_t)));
        return RequestImpl::write(trans);
    }
};

// \brief Activate request.

struct ActivateRequest : public RequestImpl<2 * sizeof(std::int32_t)> {

    std::int32_t m_refNum;
    std::int32_t m_isRealTime;

    ActivateRequest()
        : RequestImpl(kActivateClient)
        , m_refNum(0)
        , m_isRealTime(0)
    {
    }

    ActivateRequest(std::int32_t refnum, std::int32_t isRealTime)
        : RequestImpl(kActivateClient)
        , m_refNum(refnum)
        , m_isRealTime(isRealTime)
    {
    }

    std::int32_t read(detail::ChannelTransactionInterface* trans) override
    {
        CheckRes(trans->read(&m_refNum, sizeof(std::int32_t)));
        return trans->read(&m_isRealTime, sizeof(std::int32_t));
    }

    std::int32_t write(detail::ChannelTransactionInterface* trans) override
    {
        CheckRes(writePacketInit());
        CheckRes(writePacket(&m_refNum, sizeof(std::int32_t)));
        CheckRes(writePacket(&m_isRealTime, sizeof(std::int32_t)));
        return RequestImpl::write(trans);
    }
};

// \brief Deactivate request.

struct DeactivateRequest : public RequestImpl<sizeof(std::int32_t)> {

    std::int32_t m_refNum;

    DeactivateRequest()
        : RequestImpl(kDeactivateClient)
        , m_refNum(0)
    {
    }

    DeactivateRequest(std::int32_t refnum)
        : RequestImpl(kDeactivateClient)
        , m_refNum(refnum)
    {
    }

    std::int32_t read(detail::ChannelTransactionInterface* trans) override
    {
        return trans->read(&m_refNum, sizeof(std::int32_t));
    }

    std::int32_t write(detail::ChannelTransactionInterface* trans) override
    {
        CheckRes(writePacketInit());
        CheckRes(writePacket(&m_refNum, sizeof(std::int32_t)));
        return RequestImpl::write(trans);
    }
};

// \brief PortRegister request.

struct PortRegisterRequest : public RequestImpl<sizeof(std::int32_t) + kPortNameSize1 + kPortTypeSize1 + 2 * sizeof(std::uint32_t)> {

    std::int32_t m_refNum;
    std::array<char, kPortNameSize1> m_name; // port short name
    std::array<char, kPortTypeSize1> m_portType;
    std::uint32_t m_flags;
    std::uint32_t m_bufferSize;

    PortRegisterRequest()
        : RequestImpl(kRegisterPort)
        , m_refNum(0)
        , m_flags(0)
        , m_bufferSize(0)
    {
        memset(m_name.data(), 0, sizeof(m_name));
        memset(m_portType.data(), 0, sizeof(m_portType));
    }

    PortRegisterRequest(std::int32_t refnum, const char* name, const char* portType, std::uint32_t flags, std::uint32_t bufferSize)
        : RequestImpl(kRegisterPort)
        , m_refNum(refnum)
        , m_flags(flags)
        , m_bufferSize(bufferSize)
    {
        memset(m_name.data(), 0, sizeof(m_name));
        memset(m_portType.data(), 0, sizeof(m_portType));
        strncpy(m_name.data(), name, sizeof(m_name) - 1);
        strncpy(m_portType.data(), portType, sizeof(m_portType) - 1);
    }

    std::int32_t read(detail::ChannelTransactionInterface* trans) override
    {
        CheckRes(trans->read(&m_refNum, sizeof(std::int32_t)));
        CheckRes(trans->read(&m_name, sizeof(m_name)));
        CheckRes(trans->read(&m_portType, sizeof(m_portType)));
        CheckRes(trans->read(&m_flags, sizeof(std::uint32_t)));
        CheckRes(trans->read(&m_bufferSize, sizeof(std::uint32_t)));
        return 0;
    }

    std::int32_t write(detail::ChannelTransactionInterface* trans) override
    {
        CheckRes(writePacketInit());
        CheckRes(writePacket(&m_refNum, sizeof(std::int32_t)));
        CheckRes(writePacket(&m_name, sizeof(m_name)));
        CheckRes(writePacket(&m_portType, sizeof(m_portType)));
        CheckRes(writePacket(&m_flags, sizeof(std::uint32_t)));
        CheckRes(writePacket(&m_bufferSize, sizeof(std::uint32_t)));
        return RequestImpl::write(trans);
    }
};

// \brief PortRegister result.

struct PortRegisterResult : public Reply {

    PortIdC m_portIndex;

    PortRegisterResult()
        : Reply()
        , m_portIndex(kNoPort)
    {
    }

    std::int32_t read(detail::ChannelTransactionInterface* trans) override
    {
        CheckRes(Reply::read(trans));
        return trans->read(&m_portIndex, sizeof(PortIdC));
    }

    std::int32_t write(detail::ChannelTransactionInterface* trans) override
    {
        CheckRes(Reply::write(trans));
        return trans->write(&m_portIndex, sizeof(PortIdC));
    }
};

// \brief PortUnregister request.

struct PortUnRegisterRequest : public RequestImpl<sizeof(std::int32_t) + sizeof(PortIdC)> {

    std::int32_t m_refNum;
    PortIdC m_portIndex;

    PortUnRegisterRequest()
        : RequestImpl(kUnRegisterPort)
        , m_refNum(0)
        , m_portIndex(0)
    {
    }

    PortUnRegisterRequest(std::int32_t refnum, PortIdC index)
        : RequestImpl(kUnRegisterPort)
        , m_refNum(refnum)
        , m_portIndex(index)
    {
    }

    std::int32_t read(detail::ChannelTransactionInterface* trans) override
    {
        CheckRes(trans->read(&m_refNum, sizeof(std::int32_t)));
        CheckRes(trans->read(&m_portIndex, sizeof(PortIdC)));
        return 0;
    }

    std::int32_t write(detail::ChannelTransactionInterface* trans) override
    {
        CheckRes(writePacketInit());
        CheckRes(writePacket(&m_refNum, sizeof(std::int32_t)));
        CheckRes(writePacket(&m_portIndex, sizeof(PortIdC)));
        return RequestImpl::write(trans);
    }
};

// \brief PortConnectName request.

struct PortConnectNameRequest : public RequestImpl<sizeof(std::int32_t) + 2 * kRealJackPortNameSize1> {

    std::int32_t m_refNum;
    std::array<char, kRealJackPortNameSize1> m_src; // port full name
    std::array<char, kRealJackPortNameSize1> m_dst; // port full name

    PortConnectNameRequest()
        : RequestImpl(kConnectNamePorts)
        , m_refNum(0)
    {
        memset(m_src.data(), 0, sizeof(m_src));
        memset(m_dst.data(), 0, sizeof(m_dst));
    }

    PortConnectNameRequest(std::int32_t refnum, const char* srcName, const char* dstName)
        : RequestImpl(kConnectNamePorts)
        , m_refNum(refnum)
    {
        memset(m_src.data(), 0, sizeof(m_src));
        memset(m_dst.data(), 0, sizeof(m_dst));
        strncpy(m_src.data(), srcName, sizeof(m_src) - 1);
        strncpy(m_dst.data(), dstName, sizeof(m_dst) - 1);
    }

    std::int32_t read(detail::ChannelTransactionInterface* trans) override
    {
        CheckRes(trans->read(&m_refNum, sizeof(std::int32_t)));
        CheckRes(trans->read(&m_src, sizeof(m_src)));
        CheckRes(trans->read(&m_dst, sizeof(m_dst)));
        return 0;
    }

    std::int32_t write(detail::ChannelTransactionInterface* trans) override
    {
        CheckRes(writePacketInit());
        CheckRes(writePacket(&m_refNum, sizeof(std::int32_t)));
        CheckRes(writePacket(&m_src, sizeof(m_src)));
        CheckRes(writePacket(&m_dst, sizeof(m_dst)));
        return RequestImpl::write(trans);
    }
};

// \brief PortDisconnectName request.

struct PortDisconnectNameRequest : public RequestImpl<sizeof(std::int32_t) + 2 * kRealJackPortNameSize1> {

    std::int32_t m_refNum;
    std::array<char, kRealJackPortNameSize1> m_src; // port full name
    std::array<char, kRealJackPortNameSize1> m_dst; // port full name

    PortDisconnectNameRequest()
        : RequestImpl(kDisconnectNamePorts)
        , m_refNum(0)
    {
        memset(m_src.data(), 0, sizeof(m_src));
        memset(m_dst.data(), 0, sizeof(m_dst));
    }

    PortDisconnectNameRequest(std::int32_t refnum, const char* srcName, const char* dstName)
        : RequestImpl(kDisconnectNamePorts)
        , m_refNum(refnum)
    {
        memset(m_src.data(), 0, sizeof(m_src));
        memset(m_dst.data(), 0, sizeof(m_dst));
        strncpy(m_src.data(), srcName, sizeof(m_src) - 1);
        strncpy(m_dst.data(), dstName, sizeof(m_dst) - 1);
    }

    std::int32_t read(detail::ChannelTransactionInterface* trans) override
    {
        CheckRes(trans->read(&m_refNum, sizeof(std::int32_t)));
        CheckRes(trans->read(&m_src, sizeof(m_src)));
        CheckRes(trans->read(&m_dst, sizeof(m_dst)));
        return 0;
    }

    std::int32_t write(detail::ChannelTransactionInterface* trans) override
    {
        CheckRes(writePacketInit());
        CheckRes(writePacket(&m_refNum, sizeof(std::int32_t)));
        CheckRes(writePacket(&m_src, sizeof(m_src)));
        CheckRes(writePacket(&m_dst, sizeof(m_dst)));
        return RequestImpl::write(trans);
    }
};

// \brief PortConnect request.

struct PortConnectRequest : public RequestImpl<sizeof(std::int32_t) + 2 * sizeof(PortIdC)> {

    std::int32_t m_refNum;
    PortIdC m_src;
    PortIdC m_dst;

    PortConnectRequest()
        : RequestImpl(kConnectPorts)
        , m_refNum(0)
        , m_src(0)
        , m_dst(0)
    {
    }

    PortConnectRequest(std::int32_t refnum, PortIdC src, PortIdC dst)
        : RequestImpl(kConnectPorts)
        , m_refNum(refnum)
        , m_src(src)
        , m_dst(dst)
    {
    }

    std::int32_t read(detail::ChannelTransactionInterface* trans) override
    {
        CheckRes(trans->read(&m_refNum, sizeof(std::int32_t)));
        CheckRes(trans->read(&m_src, sizeof(PortIdC)));
        CheckRes(trans->read(&m_dst, sizeof(PortIdC)));
        return 0;
    }

    std::int32_t write(detail::ChannelTransactionInterface* trans) override
    {
        CheckRes(writePacketInit());
        CheckRes(writePacket(&m_refNum, sizeof(std::int32_t)));
        CheckRes(writePacket(&m_src, sizeof(PortIdC)));
        CheckRes(writePacket(&m_dst, sizeof(PortIdC)));
        return RequestImpl::write(trans);
    }
};

// \brief PortDisconnect request.

struct PortDisconnectRequest : public RequestImpl<sizeof(std::int32_t) + 2 * sizeof(PortIdC)> {

    std::int32_t m_refNum;
    PortIdC m_src;
    PortIdC m_dst;

    PortDisconnectRequest()
        : RequestImpl(kDisconnectPorts)
        , m_refNum(0)
        , m_src(0)
        , m_dst(0)
    {
    }

    PortDisconnectRequest(std::int32_t refnum, PortIdC src, PortIdC dst)
        : RequestImpl(kDisconnectPorts)
        , m_refNum(refnum)
        , m_src(src)
        , m_dst(dst)
    {
    }

    std::int32_t read(detail::ChannelTransactionInterface* trans) override
    {
        CheckRes(trans->read(&m_refNum, sizeof(std::int32_t)));
        CheckRes(trans->read(&m_src, sizeof(PortIdC)));
        CheckRes(trans->read(&m_dst, sizeof(PortIdC)));
        return 0;
    }

    std::int32_t write(detail::ChannelTransactionInterface* trans) override
    {
        CheckRes(writePacketInit());
        CheckRes(writePacket(&m_refNum, sizeof(std::int32_t)));
        CheckRes(writePacket(&m_src, sizeof(PortIdC)));
        CheckRes(writePacket(&m_dst, sizeof(PortIdC)));
        return RequestImpl::write(trans);
    }
};

// \brief PortRename request.

struct PortRenameRequest : public RequestImpl<sizeof(std::int32_t) + sizeof(PortIdC) + kPortNameSize1> {

    std::int32_t m_refNum;
    PortIdC m_port;
    std::array<char, kPortNameSize1> m_name; // port short name

    PortRenameRequest()
        : RequestImpl(kPortRename)
        , m_refNum(0)
        , m_port(0)
    {
        memset(m_name.data(), 0, sizeof(m_name));
    }

    PortRenameRequest(std::int32_t refnum, PortIdC port, const char* name)
        : RequestImpl(kPortRename)
        , m_refNum(refnum)
        , m_port(port)
    {
        memset(m_name.data(), 0, sizeof(m_name));
        strncpy(m_name.data(), name, sizeof(m_name) - 1);
    }

    std::int32_t read(detail::ChannelTransactionInterface* trans) override
    {
        CheckRes(trans->read(&m_refNum, sizeof(std::int32_t)));
        CheckRes(trans->read(&m_port, sizeof(PortIdC)));
        CheckRes(trans->read(&m_name, sizeof(m_name)));
        return 0;
    }

    std::int32_t write(detail::ChannelTransactionInterface* trans) override
    {
        CheckRes(writePacketInit());
        CheckRes(writePacket(&m_refNum, sizeof(std::int32_t)));
        CheckRes(writePacket(&m_port, sizeof(PortIdC)));
        CheckRes(writePacket(&m_name, sizeof(m_name)));
        return RequestImpl::write(trans);
    }
};

// \brief SetBufferSize request.

struct SetBufferSizeRequest : public RequestImpl<sizeof(NframesC)> {

    NframesC m_bufferSize;

    SetBufferSizeRequest()
        : RequestImpl(kSetBufferSize)
        , m_bufferSize(0)
    {
    }

    SetBufferSizeRequest(NframesC bufferSize)
        : RequestImpl(kSetBufferSize)
        , m_bufferSize(bufferSize)
    {
    }

    std::int32_t read(detail::ChannelTransactionInterface* trans) override
    {
        return trans->read(&m_bufferSize, sizeof(NframesC));
    }

    std::int32_t write(detail::ChannelTransactionInterface* trans) override
    {
        CheckRes(writePacketInit());
        CheckRes(writePacket(&m_bufferSize, sizeof(NframesC)));
        return RequestImpl::write(trans);
    }
};

// \brief SetFreeWheel request.

struct SetFreeWheelRequest : public RequestImpl<sizeof(std::int32_t)> {

    std::int32_t m_onOff;

    SetFreeWheelRequest()
        : RequestImpl(kSetFreeWheel)
        , m_onOff(0)
    {
    }

    SetFreeWheelRequest(std::int32_t onoff)
        : RequestImpl(kSetFreeWheel)
        , m_onOff(onoff)
    {
    }

    std::int32_t read(detail::ChannelTransactionInterface* trans) override
    {
        return trans->read(&m_onOff, sizeof(std::int32_t));
    }

    std::int32_t write(detail::ChannelTransactionInterface* trans) override
    {
        CheckRes(writePacketInit());
        CheckRes(writePacket(&m_onOff, sizeof(std::int32_t)));
        return RequestImpl::write(trans);
    }
};

// \brief ComputeTotalLatencies request.

struct ComputeTotalLatenciesRequest : public RequestImpl<0> {

    ComputeTotalLatenciesRequest()
        : RequestImpl(kComputeTotalLatencies)
    {
    }

    std::int32_t read(detail::ChannelTransactionInterface* trans) override
    {
        return 0;
    }

    std::int32_t write(detail::ChannelTransactionInterface* trans) override
    {
        CheckRes(writePacketInit());
        return RequestImpl::write(trans);
    }
};

// \brief ReleaseTimebase request.

struct ReleaseTimebaseRequest : public RequestImpl<sizeof(std::int32_t)> {

    std::int32_t m_refNum;

    ReleaseTimebaseRequest()
        : RequestImpl(kReleaseTimebase)
        , m_refNum(0)
    {
    }

    ReleaseTimebaseRequest(std::int32_t refnum)
        : RequestImpl(kReleaseTimebase)
        , m_refNum(refnum)
    {
    }

    std::int32_t read(detail::ChannelTransactionInterface* trans) override
    {
        return trans->read(&m_refNum, sizeof(std::int32_t));
    }

    std::int32_t write(detail::ChannelTransactionInterface* trans) override
    {
        CheckRes(writePacketInit());
        CheckRes(writePacket(&m_refNum, sizeof(std::int32_t)));
        return RequestImpl::write(trans);
    }
};

// \brief SetTimebaseCallback request.

struct SetTimebaseCallbackRequest : public RequestImpl<sizeof(std::int32_t) + sizeof(std::int32_t)> {

    std::int32_t m_refNum;
    std::int32_t m_conditionnal;

    SetTimebaseCallbackRequest()
        : RequestImpl(kSetTimebaseCallback)
        , m_refNum(0)
        , m_conditionnal(0)
    {
    }

    SetTimebaseCallbackRequest(std::int32_t refnum, std::int32_t conditional)
        : RequestImpl(kSetTimebaseCallback)
        , m_refNum(refnum)
        , m_conditionnal(conditional)
    {
    }

    std::int32_t read(detail::ChannelTransactionInterface* trans) override
    {
        CheckRes(trans->read(&m_refNum, sizeof(std::int32_t)));
        return trans->read(&m_conditionnal, sizeof(std::int32_t));
    }

    std::int32_t write(detail::ChannelTransactionInterface* trans) override
    {
        CheckRes(writePacketInit());
        CheckRes(writePacket(&m_refNum, sizeof(std::int32_t)));
        CheckRes(writePacket(&m_conditionnal, sizeof(std::int32_t)));
        return RequestImpl::write(trans);
    }
};

// \brief GetInternalClientName request.

// \brief GetInternalClient result.

// \brief InternalClientHandle request.

// \brief InternalClientHandle result.

// \brief InternalClientLoad request.

inline constexpr std::int32_t kMaxPath = 256;
inline constexpr std::int32_t kMaxPath1 = (kMaxPath + 1);

// \brief InternalClientLoad result.

// \brief InternalClientUnload request.

// \brief InternalClientLoad result.

// \brief ClientNotification request.

struct ClientNotificationRequest : public RequestImpl<3 * sizeof(std::int32_t)> {

    std::int32_t m_refNum;
    std::int32_t m_notify;
    std::int32_t m_value;

    ClientNotificationRequest()
        : RequestImpl(kNotification)
        , m_refNum(0)
        , m_notify(0)
        , m_value(0)
    {
    }

    ClientNotificationRequest(std::int32_t refnum, std::int32_t notify, std::int32_t value)
        : RequestImpl(kNotification)
        , m_refNum(refnum)
        , m_notify(notify)
        , m_value(value)
    {
    }

    std::int32_t read(detail::ChannelTransactionInterface* trans) override
    {
        CheckRes(trans->read(&m_refNum, sizeof(std::int32_t)));
        CheckRes(trans->read(&m_notify, sizeof(std::int32_t)));
        CheckRes(trans->read(&m_value, sizeof(std::int32_t)));
        return 0;
    }

    std::int32_t write(detail::ChannelTransactionInterface* trans) override
    {
        CheckRes(writePacketInit());
        CheckRes(writePacket(&m_refNum, sizeof(std::int32_t)));
        CheckRes(writePacket(&m_notify, sizeof(std::int32_t)));
        CheckRes(writePacket(&m_value, sizeof(std::int32_t)));
        return RequestImpl::write(trans);
    }
};

// \brief SessionNotify request.

struct ClientNameResult : public Reply {
    std::array<char, kClientNameSize + 1> m_name;

    ClientNameResult()
        : Reply()
    {
        memset(m_name.data(), 0, sizeof(m_name));
    }
    ClientNameResult(std::int32_t result, const char* name)
        : Reply(result)
    {
        memset(m_name.data(), 0, sizeof(m_name));
        strncpy(m_name.data(), name, sizeof(m_name) - 1);
    }

    std::int32_t read(detail::ChannelTransactionInterface* trans) override
    {
        CheckRes(Reply::read(trans));
        CheckRes(trans->read(&m_name, sizeof(m_name)));
        return 0;
    }

    std::int32_t write(detail::ChannelTransactionInterface* trans) override
    {
        CheckRes(Reply::write(trans));
        CheckRes(trans->write(&m_name, sizeof(m_name)));
        return 0;
    }
};

struct UUIDResult : public Reply {
    std::array<char, kUuidStringSize> m_uUID;

    UUIDResult()
        : Reply()
    {
        memset(m_uUID.data(), 0, sizeof(m_uUID));
    }
    UUIDResult(std::int32_t result, const char* uuid)
        : Reply(result)
    {
        memset(m_uUID.data(), 0, sizeof(m_uUID));
        strncpy(m_uUID.data(), uuid, sizeof(m_uUID) - 1);
    }

    std::int32_t read(detail::ChannelTransactionInterface* trans) override
    {
        CheckRes(Reply::read(trans));
        CheckRes(trans->read(&m_uUID, sizeof(m_uUID)));
        return 0;
    }

    std::int32_t write(detail::ChannelTransactionInterface* trans) override
    {
        CheckRes(Reply::write(trans));
        CheckRes(trans->write(&m_uUID, sizeof(m_uUID)));
        return 0;
    }
};

struct GetUUIDRequest : public RequestImpl<kClientNameSize1> {
    std::array<char, kClientNameSize1> m_name;

    GetUUIDRequest()
        : RequestImpl(kGetUUIDByClient)
    {
        memset(m_name.data(), 0, sizeof(m_name));
    }

    GetUUIDRequest(const char* clientName)
        : RequestImpl(kGetUUIDByClient)
    {
        memset(m_name.data(), 0, sizeof(m_name));
        strncpy(m_name.data(), clientName, sizeof(m_name) - 1);
    }

    std::int32_t read(detail::ChannelTransactionInterface* trans) override
    {
        CheckRes(trans->read(&m_name, sizeof(m_name)));
        return 0;
    }

    std::int32_t write(detail::ChannelTransactionInterface* trans) override
    {
        CheckRes(writePacketInit());
        CheckRes(writePacket(&m_name, sizeof(m_name)));
        return RequestImpl::write(trans);
    }
};

struct GetClientNameRequest : public RequestImpl<kUuidStringSize> {
    std::array<char, kUuidStringSize> m_uUID;

    GetClientNameRequest()
        : RequestImpl(kGetClientByUUID)
    {
        memset(m_uUID.data(), 0, sizeof(m_uUID));
    }

    GetClientNameRequest(const char* uuid)
        : RequestImpl(kGetClientByUUID)
    {
        memset(m_uUID.data(), 0, sizeof(m_uUID));
        strncpy(m_uUID.data(), uuid, sizeof(m_uUID) - 1);
    }

    std::int32_t read(detail::ChannelTransactionInterface* trans) override
    {
        CheckRes(trans->read(&m_uUID, sizeof(m_uUID)));
        return 0;
    }

    std::int32_t write(detail::ChannelTransactionInterface* trans) override
    {
        CheckRes(writePacketInit());
        CheckRes(writePacket(&m_uUID, sizeof(m_uUID)));
        return RequestImpl::write(trans);
    }
};

struct ReserveNameRequest : public RequestImpl<sizeof(std::int32_t) + kClientNameSize1 + kUuidStringSize> {
    std::int32_t m_refNum;
    std::array<char, kClientNameSize1> m_name;
    std::array<char, kUuidStringSize> m_uUID;

    ReserveNameRequest()
        : RequestImpl(kReserveClientName)
        , m_refNum(0)
    {
        memset(m_name.data(), 0, sizeof(m_name));
        memset(m_uUID.data(), 0, sizeof(m_uUID));
    }

    ReserveNameRequest(std::int32_t refnum, const char* name, const char* uuid)
        : RequestImpl(kReserveClientName)
        , m_refNum(refnum)
    {
        memset(m_name.data(), 0, sizeof(m_name));
        memset(m_uUID.data(), 0, sizeof(m_uUID));
        strncpy(m_name.data(), name, sizeof(m_name) - 1);
        strncpy(m_uUID.data(), uuid, sizeof(m_uUID) - 1);
    }

    std::int32_t read(detail::ChannelTransactionInterface* trans) override
    {
        CheckRes(trans->read(&m_uUID, sizeof(m_uUID)));
        CheckRes(trans->read(&m_name, sizeof(m_name)));
        CheckRes(trans->read(&m_refNum, sizeof(m_refNum)));
        return 0;
    }

    std::int32_t write(detail::ChannelTransactionInterface* trans) override
    {
        CheckRes(writePacketInit());
        CheckRes(writePacket(&m_uUID, sizeof(m_uUID)));
        CheckRes(writePacket(&m_name, sizeof(m_name)));
        CheckRes(writePacket(&m_refNum, sizeof(m_refNum)));
        return RequestImpl::write(trans);
    }
};


// \brief ClientNotification.

struct ClientNotification {
    static constexpr std::int32_t kPayloadSize = (kClientNameSize + 1) + 5 * sizeof(std::int32_t) + (kMessageSize + 1);

    std::array<char, kClientNameSize + 1> m_name;
    std::int32_t m_refNum;
    std::int32_t m_notify;
    std::int32_t m_value1;
    std::int32_t m_value2;
    std::int32_t m_sync;
    std::array<char, kMessageSize + 1> m_message;

    ClientNotification()
        : m_refNum(0)
        , m_notify(-1)
        , m_value1(-1)
        , m_value2(-1)
        , m_sync(0)
    {
        memset(m_name.data(), 0, sizeof(m_name));
        memset(m_message.data(), 0, sizeof(m_message));
    }
    ClientNotification(const char* name, std::int32_t refnum, std::int32_t notify, std::int32_t sync, const char* message, std::int32_t value1, std::int32_t value2)
        : m_refNum(refnum)
        , m_notify(notify)
        , m_value1(value1)
        , m_value2(value2)
        , m_sync(sync)
    {
        memset(m_name.data(), 0, sizeof(m_name));
        memset(m_message.data(), 0, sizeof(m_message));
        strncpy(m_name.data(), name, sizeof(m_name) - 1);
        if (message) {
            strncpy(m_message.data(), message, sizeof(m_message) - 1);
        }
    }

    // The payload of a notification message.  The header's type is the notification kind (fNotify), and its request id
    // is 0, unless the notification is synchronous: then the client's reply echoes the id.
    std::int32_t read(detail::ChannelTransactionInterface* trans)
    {
        CheckRes(trans->read(&m_name, sizeof(m_name)));
        CheckRes(trans->read(&m_refNum, sizeof(std::int32_t)));
        CheckRes(trans->read(&m_notify, sizeof(std::int32_t)));
        CheckRes(trans->read(&m_value1, sizeof(std::int32_t)));
        CheckRes(trans->read(&m_value2, sizeof(std::int32_t)));
        CheckRes(trans->read(&m_sync, sizeof(std::int32_t)));
        CheckRes(trans->read(&m_message, sizeof(m_message)));
        return 0;
    }

    std::int32_t write(detail::ChannelTransactionInterface* trans, std::uint32_t request)
    {
        std::array<std::byte, kPayloadSize> payload { };
        std::byte* out = payload.data();
        auto put = [&out](const void* data, std::size_t size) {
            memcpy(out, data, size);
            out += size;
        };
        put(m_name.data(), sizeof(m_name));
        put(&m_refNum, sizeof(std::int32_t));
        put(&m_notify, sizeof(std::int32_t));
        put(&m_value1, sizeof(std::int32_t));
        put(&m_value2, sizeof(std::int32_t));
        put(&m_sync, sizeof(std::int32_t));
        put(m_message.data(), sizeof(m_message));
        return sendJackMessage(trans, makeJackMsgHeader(static_cast<std::uint16_t>(m_notify), kPayloadSize, request), payload.data(), kPayloadSize);
    }
};

} // namespace jackmini
