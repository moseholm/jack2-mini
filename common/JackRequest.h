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
#include <cstdint>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <array>
#include <list>
#include <vector>

namespace jackmini {

#define JACK_CLIENT_NAME_SIZE_1 (JACK_CLIENT_NAME_SIZE + 1)
#define JACK_MESSAGE_SIZE_1 (JACK_MESSAGE_SIZE + 1)
#define JACK_PORT_NAME_SIZE_1 (JACK_PORT_NAME_SIZE + 1)
#define JACK_PORT_TYPE_SIZE_1 (JACK_PORT_TYPE_SIZE + 1)
#define REAL_JACK_PORT_NAME_SIZE_1 (REAL_JACK_PORT_NAME_SIZE + 1)

#define CheckRes(exp)                                                                \
    {                                                                                \
        std::int32_t reserr = (exp);                                                          \
        if (reserr < 0) {                                                            \
            if (reserr != JACK_REQUEST_ERR_ABORTED)                                  \
                jack_error("CheckRes error for " #exp "in %s", __PRETTY_FUNCTION__); \
            return reserr;                                                           \
        }                                                                            \
    }


// \brief Request from client to server.

struct JackRequest {

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

    RequestType fType;
    std::uint32_t fRequestId;

    JackRequest(RequestType type)
        : fType(type)
        , fRequestId(NextJackRequestId())
    {
    }

    virtual ~JackRequest()
    {
    }

    virtual std::int32_t Read(detail::JackChannelTransactionInterface* trans) = 0;

    virtual std::int32_t Write(detail::JackChannelTransactionInterface* trans) = 0;
};

template <std::int32_t DataSize>
struct JackRequestImpl : JackRequest {
    static constexpr std::int32_t TotalSize = DataSize;
    static_assert(DataSize <= static_cast<std::int32_t>(kJackMsgMaxPayload), "request payload larger than a message");

    JackRequestImpl(RequestType type)
        : JackRequest(type)
        , fPacketInit(false)
        , fPacketPos(0)
    {
    }

    std::int32_t WritePacketInit()
    {
        if (fType == 0) {
            jack_error("JackRequestImpl::WritePacketInit error unspecified type");
            return -1;
        }
        if (fPacketInit) {
            jack_error("JackRequestImpl::WritePacketInit error called more than once for type %d", fType);
            return -1;
        }
        fPacketInit = true;
        fPacketPos = 0;
        return 0;
    }

    std::int32_t WritePacket(void* data, std::int32_t len)
    {
        if (!fPacketInit) {
            jack_error("JackRequestImpl::WritePacket failed for type %d because it has not been initialized", fType);
            return -1;
        }
        if (fPacketPos == -1) {
            jack_error("JackRequestImpl::WritePacket failed for type %d because it has already been sent", fType);
            return -1;
        }
        if (fPacketPos + len > TotalSize) {
            jack_error("JackRequestImpl::WritePacket failed for type %d because of too much data, pos %d, len %d, total size %d",
                fType, fPacketPos, len, TotalSize);
            return -1;
        }
        jack_log("JackRequestImpl::WritePacket ok for type %d, pos %d, len %d, total size %d (new pos %d)",
            fType, fPacketPos, len, TotalSize, fPacketPos + len);
        memcpy(fPacketData.data() + fPacketPos, data, len);
        fPacketPos += len;
        return 0;
    }

    std::int32_t Write(detail::JackChannelTransactionInterface* trans) override
    {
        if (!fPacketInit || fPacketPos != TotalSize) {
            jack_error("JackRequestImpl::Write failed for type %d because of incomplete data, pos %d, total size %d",
                fType, fPacketPos, TotalSize);
            return -1;
        }
        // invalidate any more writes
        fPacketPos = -1;
        const std::int32_t err = SendJackMessage(trans, MakeJackMsgHeader(fType, TotalSize, fRequestId), fPacketData.data(), TotalSize);
        if (err < 0 && err != JACK_REQUEST_ERR_ABORTED)
            jack_error("JackRequestImpl::Write failed for type %d", fType);
        return err;
    }

    static constexpr std::int32_t Size() { return DataSize; }

private:
    std::array<std::uint8_t, TotalSize> fPacketData { };
    bool fPacketInit;
    std::int32_t fPacketPos;
};

// \brief Result from the server.

struct JackResult {

    std::int32_t fResult;

    JackResult()
        : fResult(-1)
    {
    }
    JackResult(std::int32_t result)
        : fResult(result)
    {
    }
    virtual ~JackResult()
    {
    }

    virtual std::int32_t Read(detail::JackChannelTransactionInterface* trans)
    {
        return trans->Read(&fResult, sizeof(std::int32_t));
    }

    virtual std::int32_t Write(detail::JackChannelTransactionInterface* trans)
    {
        return trans->Write(&fResult, sizeof(std::int32_t));
    }
};

// The reply to a request: the request's type and id, and the result as the payload.
inline std::int32_t SendJackReply(detail::JackChannelTransactionInterface* trans, JackResult& res, const JackMsgHeader& request)
{
    JackPayloadWriter payload;
    if (res.Write(&payload) < 0) {
        return -1;
    }
    const auto length = static_cast<std::uint32_t>(payload.fData.size());
    return SendJackMessage(trans, MakeJackMsgHeader(request.type, length, request.request), payload.fData.data(), length, payload.fFds);
}

// Reads the reply to the message with the given type and id.  The header must match it and carry our protocol version,
// and the payload must be read in full.
inline std::int32_t ReadJackReply(detail::JackClientRequestInterface* socket, std::uint16_t type, std::uint32_t request, JackResult& res)
{
    JackMsgHeader header { };
    std::vector<UniqueFd> fds;
    if (socket->ReadWithFds(&header, sizeof(header), fds) < 0) {
        return -1;
    }
    if (header.magic != kJackMsgMagic) {
        jack_error("JACK reply without the protocol magic number");
        return -1;
    }
    if (header.version != kJackMsgVersion) {
        jack_error("JACK protocol mismatch (server %d, client %d)", header.version, kJackMsgVersion);
        return -1;
    }
    if (header.request != request || header.type != type) {
        jack_error("JACK reply does not match its request, type %d request %d", type, request);
        return -1;
    }

    JackPayloadReader payload(socket, header.length, std::move(fds));
    if (res.Read(&payload) < 0 || !payload.Done()) {
        return -1;
    }
    return 0;
}

// \brief CheckClient request.

struct JackClientCheckRequest : public JackRequestImpl<JACK_CLIENT_NAME_SIZE_1 + 3 * sizeof(std::int32_t) + sizeof(jack_uuid_t)> {

    char fName[JACK_CLIENT_NAME_SIZE_1];
    std::int32_t fProtocol;
    std::int32_t fOptions;
    std::int32_t fOpen;
    jack_uuid_t fUUID;

    JackClientCheckRequest()
        : JackRequestImpl(kClientCheck)
        , fProtocol(0)
        , fOptions(0)
        , fOpen(0)
        , fUUID(JACK_UUID_EMPTY_INITIALIZER)
    {
        memset(fName, 0, sizeof(fName));
    }

    JackClientCheckRequest(const char* name, std::int32_t protocol, std::int32_t options, jack_uuid_t uuid, std::int32_t open = false)
        : JackRequestImpl(kClientCheck)
        , fProtocol(protocol)
        , fOptions(options)
        , fOpen(open)
        , fUUID(uuid)
    {
        memset(fName, 0, sizeof(fName));
        CopyCString(fName, name);
    }

    std::int32_t Read(detail::JackChannelTransactionInterface* trans) override
    {
        CheckRes(trans->Read(&fName, sizeof(fName)));
        CheckRes(trans->Read(&fProtocol, sizeof(std::int32_t)));
        CheckRes(trans->Read(&fOptions, sizeof(std::int32_t)));
        CheckRes(trans->Read(&fUUID, sizeof(jack_uuid_t)));
        return trans->Read(&fOpen, sizeof(std::int32_t));
    }

    std::int32_t Write(detail::JackChannelTransactionInterface* trans) override
    {
        CheckRes(WritePacketInit());
        CheckRes(WritePacket(&fName, sizeof(fName)));
        CheckRes(WritePacket(&fProtocol, sizeof(std::int32_t)));
        CheckRes(WritePacket(&fOptions, sizeof(std::int32_t)));
        CheckRes(WritePacket(&fUUID, sizeof(jack_uuid_t)));
        CheckRes(WritePacket(&fOpen, sizeof(std::int32_t)));
        return JackRequestImpl::Write(trans);
    }

    static constexpr std::int32_t Size() { return sizeof(fName) + 3 * sizeof(std::int32_t) + sizeof(jack_uuid_t); }
};

// \brief CheckClient result.

struct JackClientCheckResult : public JackResult {

    char fName[JACK_CLIENT_NAME_SIZE + 1];
    std::int32_t fStatus;

    JackClientCheckResult()
        : JackResult()
        , fStatus(0)
    {
        memset(fName, 0, sizeof(fName));
    }
    JackClientCheckResult(std::int32_t result, const char* name, std::int32_t status)
        : JackResult(result)
        , fStatus(status)
    {
        memset(fName, 0, sizeof(fName));
        CopyCString(fName, name);
    }

    std::int32_t Read(detail::JackChannelTransactionInterface* trans) override
    {
        CheckRes(JackResult::Read(trans));
        CheckRes(trans->Read(&fName, sizeof(fName)));
        CheckRes(trans->Read(&fStatus, sizeof(std::int32_t)));
        return 0;
    }

    std::int32_t Write(detail::JackChannelTransactionInterface* trans) override
    {
        CheckRes(JackResult::Write(trans));
        CheckRes(trans->Write(&fName, sizeof(fName)));
        CheckRes(trans->Write(&fStatus, sizeof(std::int32_t)));
        return 0;
    }
};

// \brief NewClient request.

struct JackClientOpenRequest : public JackRequestImpl<sizeof(std::int32_t) + sizeof(jack_uuid_t) + JACK_CLIENT_NAME_SIZE_1> {

    std::int32_t fPID;
    jack_uuid_t fUUID;
    char fName[JACK_CLIENT_NAME_SIZE_1];

    JackClientOpenRequest()
        : JackRequestImpl(kClientOpen)
        , fPID(0)
        , fUUID(JACK_UUID_EMPTY_INITIALIZER)
    {
        memset(fName, 0, sizeof(fName));
    }

    JackClientOpenRequest(const char* name, std::int32_t pid, jack_uuid_t uuid)
        : JackRequestImpl(kClientOpen)
    {
        memset(fName, 0, sizeof(fName));
        CopyCString(fName, name);
        fPID = pid;
        fUUID = uuid;
    }

    std::int32_t Read(detail::JackChannelTransactionInterface* trans) override
    {
        CheckRes(trans->Read(&fPID, sizeof(std::int32_t)));
        CheckRes(trans->Read(&fUUID, sizeof(jack_uuid_t)));
        return trans->Read(&fName, sizeof(fName));
    }

    std::int32_t Write(detail::JackChannelTransactionInterface* trans) override
    {
        CheckRes(WritePacketInit());
        CheckRes(WritePacket(&fPID, sizeof(std::int32_t)));
        CheckRes(WritePacket(&fUUID, sizeof(jack_uuid_t)));
        CheckRes(WritePacket(&fName, sizeof(fName)));
        return JackRequestImpl::Write(trans);
    }
};

// \brief NewClient result.
//
// The payload is the status and the sizes of the three segments. The descriptors go with it as SCM_RIGHTS, in one message, in the order engine, graph, client control.

struct JackClientOpenResultPayload {
    std::int32_t fResult;
    std::uint32_t fEngineSize;
    std::uint32_t fGraphSize;
    std::uint32_t fClientSize;
};
static_assert(sizeof(JackClientOpenResultPayload) == 16);

struct JackClientOpenResult : public JackResult {

    JackShmSegment fSharedEngine;
    JackShmSegment fSharedClient;
    JackShmSegment fSharedGraph;

    JackClientOpenResult()
        : JackResult()
    {
    }

    std::int32_t Read(detail::JackChannelTransactionInterface* trans) override
    {
        JackClientOpenResultPayload payload { };
        std::vector<UniqueFd> fds;
        CheckRes(trans->ReadWithFds(&payload, sizeof(payload), fds));
        fResult = payload.fResult;
        if (fResult != 0) {
            return 0;
        }
        if (fds.size() != 3) {
            jack_error("JackClientOpenResult::Read expected 3 descriptors, got %zu", fds.size());
            return -1;
        }
        fSharedEngine = JackShmSegment { fds[0].release(), payload.fEngineSize };
        fSharedGraph = JackShmSegment { fds[1].release(), payload.fGraphSize };
        fSharedClient = JackShmSegment { fds[2].release(), payload.fClientSize };
        return 0;
    }

    std::int32_t Write(detail::JackChannelTransactionInterface* trans) override
    {
        JackClientOpenResultPayload payload { fResult, fSharedEngine.fSize, fSharedGraph.fSize, fSharedClient.fSize };
        if (fResult != 0) {
            return trans->WriteWithFds(&payload, sizeof(payload), { });
        }
        const std::array<std::int32_t, 3> fds { fSharedEngine.fFd, fSharedGraph.fFd, fSharedClient.fFd };
        return trans->WriteWithFds(&payload, sizeof(payload), fds);
    }
};

// \brief CloseClient request.

struct JackClientCloseRequest : public JackRequestImpl<sizeof(std::int32_t)> {

    std::int32_t fRefNum;

    JackClientCloseRequest()
        : JackRequestImpl(kClientClose)
        , fRefNum(0)
    {
    }

    JackClientCloseRequest(std::int32_t refnum)
        : JackRequestImpl(kClientClose)
        , fRefNum(refnum)
    {
    }

    std::int32_t Read(detail::JackChannelTransactionInterface* trans) override
    {
        return trans->Read(&fRefNum, sizeof(std::int32_t));
    }

    std::int32_t Write(detail::JackChannelTransactionInterface* trans) override
    {
        CheckRes(WritePacketInit());
        CheckRes(WritePacket(&fRefNum, sizeof(std::int32_t)));
        return JackRequestImpl::Write(trans);
    }
};

// \brief Activate request.

struct JackActivateRequest : public JackRequestImpl<2 * sizeof(std::int32_t)> {

    std::int32_t fRefNum;
    std::int32_t fIsRealTime;

    JackActivateRequest()
        : JackRequestImpl(kActivateClient)
        , fRefNum(0)
        , fIsRealTime(0)
    {
    }

    JackActivateRequest(std::int32_t refnum, std::int32_t is_real_time)
        : JackRequestImpl(kActivateClient)
        , fRefNum(refnum)
        , fIsRealTime(is_real_time)
    {
    }

    std::int32_t Read(detail::JackChannelTransactionInterface* trans) override
    {
        CheckRes(trans->Read(&fRefNum, sizeof(std::int32_t)));
        return trans->Read(&fIsRealTime, sizeof(std::int32_t));
    }

    std::int32_t Write(detail::JackChannelTransactionInterface* trans) override
    {
        CheckRes(WritePacketInit());
        CheckRes(WritePacket(&fRefNum, sizeof(std::int32_t)));
        CheckRes(WritePacket(&fIsRealTime, sizeof(std::int32_t)));
        return JackRequestImpl::Write(trans);
    }
};

// \brief Deactivate request.

struct JackDeactivateRequest : public JackRequestImpl<sizeof(std::int32_t)> {

    std::int32_t fRefNum;

    JackDeactivateRequest()
        : JackRequestImpl(kDeactivateClient)
        , fRefNum(0)
    {
    }

    JackDeactivateRequest(std::int32_t refnum)
        : JackRequestImpl(kDeactivateClient)
        , fRefNum(refnum)
    {
    }

    std::int32_t Read(detail::JackChannelTransactionInterface* trans) override
    {
        return trans->Read(&fRefNum, sizeof(std::int32_t));
    }

    std::int32_t Write(detail::JackChannelTransactionInterface* trans) override
    {
        CheckRes(WritePacketInit());
        CheckRes(WritePacket(&fRefNum, sizeof(std::int32_t)));
        return JackRequestImpl::Write(trans);
    }
};

// \brief PortRegister request.

struct JackPortRegisterRequest : public JackRequestImpl<sizeof(std::int32_t) + JACK_PORT_NAME_SIZE_1 + JACK_PORT_TYPE_SIZE_1 + 2 * sizeof(std::uint32_t)> {

    std::int32_t fRefNum;
    char fName[JACK_PORT_NAME_SIZE_1]; // port short name
    char fPortType[JACK_PORT_TYPE_SIZE_1];
    std::uint32_t fFlags;
    std::uint32_t fBufferSize;

    JackPortRegisterRequest()
        : JackRequestImpl(kRegisterPort)
        , fRefNum(0)
        , fFlags(0)
        , fBufferSize(0)
    {
        memset(fName, 0, sizeof(fName));
        memset(fPortType, 0, sizeof(fPortType));
    }

    JackPortRegisterRequest(std::int32_t refnum, const char* name, const char* port_type, std::uint32_t flags, std::uint32_t buffer_size)
        : JackRequestImpl(kRegisterPort)
        , fRefNum(refnum)
        , fFlags(flags)
        , fBufferSize(buffer_size)
    {
        memset(fName, 0, sizeof(fName));
        memset(fPortType, 0, sizeof(fPortType));
        strncpy(fName, name, sizeof(fName) - 1);
        strncpy(fPortType, port_type, sizeof(fPortType) - 1);
    }

    std::int32_t Read(detail::JackChannelTransactionInterface* trans) override
    {
        CheckRes(trans->Read(&fRefNum, sizeof(std::int32_t)));
        CheckRes(trans->Read(&fName, sizeof(fName)));
        CheckRes(trans->Read(&fPortType, sizeof(fPortType)));
        CheckRes(trans->Read(&fFlags, sizeof(std::uint32_t)));
        CheckRes(trans->Read(&fBufferSize, sizeof(std::uint32_t)));
        return 0;
    }

    std::int32_t Write(detail::JackChannelTransactionInterface* trans) override
    {
        CheckRes(WritePacketInit());
        CheckRes(WritePacket(&fRefNum, sizeof(std::int32_t)));
        CheckRes(WritePacket(&fName, sizeof(fName)));
        CheckRes(WritePacket(&fPortType, sizeof(fPortType)));
        CheckRes(WritePacket(&fFlags, sizeof(std::uint32_t)));
        CheckRes(WritePacket(&fBufferSize, sizeof(std::uint32_t)));
        return JackRequestImpl::Write(trans);
    }
};

// \brief PortRegister result.

struct JackPortRegisterResult : public JackResult {

    jack_port_id_t fPortIndex;

    JackPortRegisterResult()
        : JackResult()
        , fPortIndex(NO_PORT)
    {
    }

    std::int32_t Read(detail::JackChannelTransactionInterface* trans) override
    {
        CheckRes(JackResult::Read(trans));
        return trans->Read(&fPortIndex, sizeof(jack_port_id_t));
    }

    std::int32_t Write(detail::JackChannelTransactionInterface* trans) override
    {
        CheckRes(JackResult::Write(trans));
        return trans->Write(&fPortIndex, sizeof(jack_port_id_t));
    }
};

// \brief PortUnregister request.

struct JackPortUnRegisterRequest : public JackRequestImpl<sizeof(std::int32_t) + sizeof(jack_port_id_t)> {

    std::int32_t fRefNum;
    jack_port_id_t fPortIndex;

    JackPortUnRegisterRequest()
        : JackRequestImpl(kUnRegisterPort)
        , fRefNum(0)
        , fPortIndex(0)
    {
    }

    JackPortUnRegisterRequest(std::int32_t refnum, jack_port_id_t index)
        : JackRequestImpl(kUnRegisterPort)
        , fRefNum(refnum)
        , fPortIndex(index)
    {
    }

    std::int32_t Read(detail::JackChannelTransactionInterface* trans) override
    {
        CheckRes(trans->Read(&fRefNum, sizeof(std::int32_t)));
        CheckRes(trans->Read(&fPortIndex, sizeof(jack_port_id_t)));
        return 0;
    }

    std::int32_t Write(detail::JackChannelTransactionInterface* trans) override
    {
        CheckRes(WritePacketInit());
        CheckRes(WritePacket(&fRefNum, sizeof(std::int32_t)));
        CheckRes(WritePacket(&fPortIndex, sizeof(jack_port_id_t)));
        return JackRequestImpl::Write(trans);
    }
};

// \brief PortConnectName request.

struct JackPortConnectNameRequest : public JackRequestImpl<sizeof(std::int32_t) + 2 * REAL_JACK_PORT_NAME_SIZE_1> {

    std::int32_t fRefNum;
    char fSrc[REAL_JACK_PORT_NAME_SIZE_1]; // port full name
    char fDst[REAL_JACK_PORT_NAME_SIZE_1]; // port full name

    JackPortConnectNameRequest()
        : JackRequestImpl(kConnectNamePorts)
        , fRefNum(0)
    {
        memset(fSrc, 0, sizeof(fSrc));
        memset(fDst, 0, sizeof(fDst));
    }

    JackPortConnectNameRequest(std::int32_t refnum, const char* src_name, const char* dst_name)
        : JackRequestImpl(kConnectNamePorts)
        , fRefNum(refnum)
    {
        memset(fSrc, 0, sizeof(fSrc));
        memset(fDst, 0, sizeof(fDst));
        strncpy(fSrc, src_name, sizeof(fSrc) - 1);
        strncpy(fDst, dst_name, sizeof(fDst) - 1);
    }

    std::int32_t Read(detail::JackChannelTransactionInterface* trans) override
    {
        CheckRes(trans->Read(&fRefNum, sizeof(std::int32_t)));
        CheckRes(trans->Read(&fSrc, sizeof(fSrc)));
        CheckRes(trans->Read(&fDst, sizeof(fDst)));
        return 0;
    }

    std::int32_t Write(detail::JackChannelTransactionInterface* trans) override
    {
        CheckRes(WritePacketInit());
        CheckRes(WritePacket(&fRefNum, sizeof(std::int32_t)));
        CheckRes(WritePacket(&fSrc, sizeof(fSrc)));
        CheckRes(WritePacket(&fDst, sizeof(fDst)));
        return JackRequestImpl::Write(trans);
    }
};

// \brief PortDisconnectName request.

struct JackPortDisconnectNameRequest : public JackRequestImpl<sizeof(std::int32_t) + 2 * REAL_JACK_PORT_NAME_SIZE_1> {

    std::int32_t fRefNum;
    char fSrc[REAL_JACK_PORT_NAME_SIZE_1]; // port full name
    char fDst[REAL_JACK_PORT_NAME_SIZE_1]; // port full name

    JackPortDisconnectNameRequest()
        : JackRequestImpl(kDisconnectNamePorts)
        , fRefNum(0)
    {
        memset(fSrc, 0, sizeof(fSrc));
        memset(fDst, 0, sizeof(fDst));
    }

    JackPortDisconnectNameRequest(std::int32_t refnum, const char* src_name, const char* dst_name)
        : JackRequestImpl(kDisconnectNamePorts)
        , fRefNum(refnum)
    {
        memset(fSrc, 0, sizeof(fSrc));
        memset(fDst, 0, sizeof(fDst));
        strncpy(fSrc, src_name, sizeof(fSrc) - 1);
        strncpy(fDst, dst_name, sizeof(fDst) - 1);
    }

    std::int32_t Read(detail::JackChannelTransactionInterface* trans) override
    {
        CheckRes(trans->Read(&fRefNum, sizeof(std::int32_t)));
        CheckRes(trans->Read(&fSrc, sizeof(fSrc)));
        CheckRes(trans->Read(&fDst, sizeof(fDst)));
        return 0;
    }

    std::int32_t Write(detail::JackChannelTransactionInterface* trans) override
    {
        CheckRes(WritePacketInit());
        CheckRes(WritePacket(&fRefNum, sizeof(std::int32_t)));
        CheckRes(WritePacket(&fSrc, sizeof(fSrc)));
        CheckRes(WritePacket(&fDst, sizeof(fDst)));
        return JackRequestImpl::Write(trans);
    }
};

// \brief PortConnect request.

struct JackPortConnectRequest : public JackRequestImpl<sizeof(std::int32_t) + 2 * sizeof(jack_port_id_t)> {

    std::int32_t fRefNum;
    jack_port_id_t fSrc;
    jack_port_id_t fDst;

    JackPortConnectRequest()
        : JackRequestImpl(kConnectPorts)
        , fRefNum(0)
        , fSrc(0)
        , fDst(0)
    {
    }

    JackPortConnectRequest(std::int32_t refnum, jack_port_id_t src, jack_port_id_t dst)
        : JackRequestImpl(kConnectPorts)
        , fRefNum(refnum)
        , fSrc(src)
        , fDst(dst)
    {
    }

    std::int32_t Read(detail::JackChannelTransactionInterface* trans) override
    {
        CheckRes(trans->Read(&fRefNum, sizeof(std::int32_t)));
        CheckRes(trans->Read(&fSrc, sizeof(jack_port_id_t)));
        CheckRes(trans->Read(&fDst, sizeof(jack_port_id_t)));
        return 0;
    }

    std::int32_t Write(detail::JackChannelTransactionInterface* trans) override
    {
        CheckRes(WritePacketInit());
        CheckRes(WritePacket(&fRefNum, sizeof(std::int32_t)));
        CheckRes(WritePacket(&fSrc, sizeof(jack_port_id_t)));
        CheckRes(WritePacket(&fDst, sizeof(jack_port_id_t)));
        return JackRequestImpl::Write(trans);
    }
};

// \brief PortDisconnect request.

struct JackPortDisconnectRequest : public JackRequestImpl<sizeof(std::int32_t) + 2 * sizeof(jack_port_id_t)> {

    std::int32_t fRefNum;
    jack_port_id_t fSrc;
    jack_port_id_t fDst;

    JackPortDisconnectRequest()
        : JackRequestImpl(kDisconnectPorts)
        , fRefNum(0)
        , fSrc(0)
        , fDst(0)
    {
    }

    JackPortDisconnectRequest(std::int32_t refnum, jack_port_id_t src, jack_port_id_t dst)
        : JackRequestImpl(kDisconnectPorts)
        , fRefNum(refnum)
        , fSrc(src)
        , fDst(dst)
    {
    }

    std::int32_t Read(detail::JackChannelTransactionInterface* trans) override
    {
        CheckRes(trans->Read(&fRefNum, sizeof(std::int32_t)));
        CheckRes(trans->Read(&fSrc, sizeof(jack_port_id_t)));
        CheckRes(trans->Read(&fDst, sizeof(jack_port_id_t)));
        return 0;
    }

    std::int32_t Write(detail::JackChannelTransactionInterface* trans) override
    {
        CheckRes(WritePacketInit());
        CheckRes(WritePacket(&fRefNum, sizeof(std::int32_t)));
        CheckRes(WritePacket(&fSrc, sizeof(jack_port_id_t)));
        CheckRes(WritePacket(&fDst, sizeof(jack_port_id_t)));
        return JackRequestImpl::Write(trans);
    }
};

// \brief PortRename request.

struct JackPortRenameRequest : public JackRequestImpl<sizeof(std::int32_t) + sizeof(jack_port_id_t) + JACK_PORT_NAME_SIZE_1> {

    std::int32_t fRefNum;
    jack_port_id_t fPort;
    char fName[JACK_PORT_NAME_SIZE_1]; // port short name

    JackPortRenameRequest()
        : JackRequestImpl(kPortRename)
        , fRefNum(0)
        , fPort(0)
    {
        memset(fName, 0, sizeof(fName));
    }

    JackPortRenameRequest(std::int32_t refnum, jack_port_id_t port, const char* name)
        : JackRequestImpl(kPortRename)
        , fRefNum(refnum)
        , fPort(port)
    {
        memset(fName, 0, sizeof(fName));
        strncpy(fName, name, sizeof(fName) - 1);
    }

    std::int32_t Read(detail::JackChannelTransactionInterface* trans) override
    {
        CheckRes(trans->Read(&fRefNum, sizeof(std::int32_t)));
        CheckRes(trans->Read(&fPort, sizeof(jack_port_id_t)));
        CheckRes(trans->Read(&fName, sizeof(fName)));
        return 0;
    }

    std::int32_t Write(detail::JackChannelTransactionInterface* trans) override
    {
        CheckRes(WritePacketInit());
        CheckRes(WritePacket(&fRefNum, sizeof(std::int32_t)));
        CheckRes(WritePacket(&fPort, sizeof(jack_port_id_t)));
        CheckRes(WritePacket(&fName, sizeof(fName)));
        return JackRequestImpl::Write(trans);
    }
};

// \brief SetBufferSize request.

struct JackSetBufferSizeRequest : public JackRequestImpl<sizeof(jack_nframes_t)> {

    jack_nframes_t fBufferSize;

    JackSetBufferSizeRequest()
        : JackRequestImpl(kSetBufferSize)
        , fBufferSize(0)
    {
    }

    JackSetBufferSizeRequest(jack_nframes_t buffer_size)
        : JackRequestImpl(kSetBufferSize)
        , fBufferSize(buffer_size)
    {
    }

    std::int32_t Read(detail::JackChannelTransactionInterface* trans) override
    {
        return trans->Read(&fBufferSize, sizeof(jack_nframes_t));
    }

    std::int32_t Write(detail::JackChannelTransactionInterface* trans) override
    {
        CheckRes(WritePacketInit());
        CheckRes(WritePacket(&fBufferSize, sizeof(jack_nframes_t)));
        return JackRequestImpl::Write(trans);
    }
};

// \brief SetFreeWheel request.

struct JackSetFreeWheelRequest : public JackRequestImpl<sizeof(std::int32_t)> {

    std::int32_t fOnOff;

    JackSetFreeWheelRequest()
        : JackRequestImpl(kSetFreeWheel)
        , fOnOff(0)
    {
    }

    JackSetFreeWheelRequest(std::int32_t onoff)
        : JackRequestImpl(kSetFreeWheel)
        , fOnOff(onoff)
    {
    }

    std::int32_t Read(detail::JackChannelTransactionInterface* trans) override
    {
        return trans->Read(&fOnOff, sizeof(std::int32_t));
    }

    std::int32_t Write(detail::JackChannelTransactionInterface* trans) override
    {
        CheckRes(WritePacketInit());
        CheckRes(WritePacket(&fOnOff, sizeof(std::int32_t)));
        return JackRequestImpl::Write(trans);
    }
};

// \brief ComputeTotalLatencies request.

struct JackComputeTotalLatenciesRequest : public JackRequestImpl<0> {

    JackComputeTotalLatenciesRequest()
        : JackRequestImpl(kComputeTotalLatencies)
    {
    }

    std::int32_t Read(detail::JackChannelTransactionInterface* trans) override
    {
        return 0;
    }

    std::int32_t Write(detail::JackChannelTransactionInterface* trans) override
    {
        CheckRes(WritePacketInit());
        return JackRequestImpl::Write(trans);
    }
};

// \brief ReleaseTimebase request.

struct JackReleaseTimebaseRequest : public JackRequestImpl<sizeof(std::int32_t)> {

    std::int32_t fRefNum;

    JackReleaseTimebaseRequest()
        : JackRequestImpl(kReleaseTimebase)
        , fRefNum(0)
    {
    }

    JackReleaseTimebaseRequest(std::int32_t refnum)
        : JackRequestImpl(kReleaseTimebase)
        , fRefNum(refnum)
    {
    }

    std::int32_t Read(detail::JackChannelTransactionInterface* trans) override
    {
        return trans->Read(&fRefNum, sizeof(std::int32_t));
    }

    std::int32_t Write(detail::JackChannelTransactionInterface* trans) override
    {
        CheckRes(WritePacketInit());
        CheckRes(WritePacket(&fRefNum, sizeof(std::int32_t)));
        return JackRequestImpl::Write(trans);
    }
};

// \brief SetTimebaseCallback request.

struct JackSetTimebaseCallbackRequest : public JackRequestImpl<sizeof(std::int32_t) + sizeof(std::int32_t)> {

    std::int32_t fRefNum;
    std::int32_t fConditionnal;

    JackSetTimebaseCallbackRequest()
        : JackRequestImpl(kSetTimebaseCallback)
        , fRefNum(0)
        , fConditionnal(0)
    {
    }

    JackSetTimebaseCallbackRequest(std::int32_t refnum, std::int32_t conditional)
        : JackRequestImpl(kSetTimebaseCallback)
        , fRefNum(refnum)
        , fConditionnal(conditional)
    {
    }

    std::int32_t Read(detail::JackChannelTransactionInterface* trans) override
    {
        CheckRes(trans->Read(&fRefNum, sizeof(std::int32_t)));
        return trans->Read(&fConditionnal, sizeof(std::int32_t));
    }

    std::int32_t Write(detail::JackChannelTransactionInterface* trans) override
    {
        CheckRes(WritePacketInit());
        CheckRes(WritePacket(&fRefNum, sizeof(std::int32_t)));
        CheckRes(WritePacket(&fConditionnal, sizeof(std::int32_t)));
        return JackRequestImpl::Write(trans);
    }
};

// \brief GetInternalClientName request.

// \brief GetInternalClient result.

// \brief InternalClientHandle request.

// \brief InternalClientHandle result.

// \brief InternalClientLoad request.

#ifndef MAX_PATH
#define MAX_PATH 256
#endif
#define MAX_PATH_1 (MAX_PATH + 1)

// \brief InternalClientLoad result.

// \brief InternalClientUnload request.

// \brief InternalClientLoad result.

// \brief ClientNotification request.

struct JackClientNotificationRequest : public JackRequestImpl<3 * sizeof(std::int32_t)> {

    std::int32_t fRefNum;
    std::int32_t fNotify;
    std::int32_t fValue;

    JackClientNotificationRequest()
        : JackRequestImpl(kNotification)
        , fRefNum(0)
        , fNotify(0)
        , fValue(0)
    {
    }

    JackClientNotificationRequest(std::int32_t refnum, std::int32_t notify, std::int32_t value)
        : JackRequestImpl(kNotification)
        , fRefNum(refnum)
        , fNotify(notify)
        , fValue(value)
    {
    }

    std::int32_t Read(detail::JackChannelTransactionInterface* trans) override
    {
        CheckRes(trans->Read(&fRefNum, sizeof(std::int32_t)));
        CheckRes(trans->Read(&fNotify, sizeof(std::int32_t)));
        CheckRes(trans->Read(&fValue, sizeof(std::int32_t)));
        return 0;
    }

    std::int32_t Write(detail::JackChannelTransactionInterface* trans) override
    {
        CheckRes(WritePacketInit());
        CheckRes(WritePacket(&fRefNum, sizeof(std::int32_t)));
        CheckRes(WritePacket(&fNotify, sizeof(std::int32_t)));
        CheckRes(WritePacket(&fValue, sizeof(std::int32_t)));
        return JackRequestImpl::Write(trans);
    }
};

// \brief SessionNotify request.

struct JackClientNameResult : public JackResult {
    char fName[JACK_CLIENT_NAME_SIZE + 1];

    JackClientNameResult()
        : JackResult()
    {
        memset(fName, 0, sizeof(fName));
    }
    JackClientNameResult(std::int32_t result, const char* name)
        : JackResult(result)
    {
        memset(fName, 0, sizeof(fName));
        strncpy(fName, name, sizeof(fName) - 1);
    }

    std::int32_t Read(detail::JackChannelTransactionInterface* trans) override
    {
        CheckRes(JackResult::Read(trans));
        CheckRes(trans->Read(&fName, sizeof(fName)));
        return 0;
    }

    std::int32_t Write(detail::JackChannelTransactionInterface* trans) override
    {
        CheckRes(JackResult::Write(trans));
        CheckRes(trans->Write(&fName, sizeof(fName)));
        return 0;
    }
};

struct JackUUIDResult : public JackResult {
    char fUUID[JACK_UUID_STRING_SIZE];

    JackUUIDResult()
        : JackResult()
    {
        memset(fUUID, 0, sizeof(fUUID));
    }
    JackUUIDResult(std::int32_t result, const char* uuid)
        : JackResult(result)
    {
        memset(fUUID, 0, sizeof(fUUID));
        strncpy(fUUID, uuid, sizeof(fUUID) - 1);
    }

    std::int32_t Read(detail::JackChannelTransactionInterface* trans) override
    {
        CheckRes(JackResult::Read(trans));
        CheckRes(trans->Read(&fUUID, sizeof(fUUID)));
        return 0;
    }

    std::int32_t Write(detail::JackChannelTransactionInterface* trans) override
    {
        CheckRes(JackResult::Write(trans));
        CheckRes(trans->Write(&fUUID, sizeof(fUUID)));
        return 0;
    }
};

struct JackGetUUIDRequest : public JackRequestImpl<JACK_CLIENT_NAME_SIZE_1> {
    char fName[JACK_CLIENT_NAME_SIZE_1];

    JackGetUUIDRequest()
        : JackRequestImpl(kGetUUIDByClient)
    {
        memset(fName, 0, sizeof(fName));
    }

    JackGetUUIDRequest(const char* client_name)
        : JackRequestImpl(kGetUUIDByClient)
    {
        memset(fName, 0, sizeof(fName));
        strncpy(fName, client_name, sizeof(fName) - 1);
    }

    std::int32_t Read(detail::JackChannelTransactionInterface* trans) override
    {
        CheckRes(trans->Read(&fName, sizeof(fName)));
        return 0;
    }

    std::int32_t Write(detail::JackChannelTransactionInterface* trans) override
    {
        CheckRes(WritePacketInit());
        CheckRes(WritePacket(&fName, sizeof(fName)));
        return JackRequestImpl::Write(trans);
    }
};

struct JackGetClientNameRequest : public JackRequestImpl<JACK_UUID_STRING_SIZE> {
    char fUUID[JACK_UUID_STRING_SIZE];

    JackGetClientNameRequest()
        : JackRequestImpl(kGetClientByUUID)
    {
        memset(fUUID, 0, sizeof(fUUID));
    }

    JackGetClientNameRequest(const char* uuid)
        : JackRequestImpl(kGetClientByUUID)
    {
        memset(fUUID, 0, sizeof(fUUID));
        strncpy(fUUID, uuid, sizeof(fUUID) - 1);
    }

    std::int32_t Read(detail::JackChannelTransactionInterface* trans) override
    {
        CheckRes(trans->Read(&fUUID, sizeof(fUUID)));
        return 0;
    }

    std::int32_t Write(detail::JackChannelTransactionInterface* trans) override
    {
        CheckRes(WritePacketInit());
        CheckRes(WritePacket(&fUUID, sizeof(fUUID)));
        return JackRequestImpl::Write(trans);
    }
};

struct JackReserveNameRequest : public JackRequestImpl<sizeof(std::int32_t) + JACK_CLIENT_NAME_SIZE_1 + JACK_UUID_STRING_SIZE> {
    std::int32_t fRefNum;
    char fName[JACK_CLIENT_NAME_SIZE_1];
    char fUUID[JACK_UUID_STRING_SIZE];

    JackReserveNameRequest()
        : JackRequestImpl(kReserveClientName)
        , fRefNum(0)
    {
        memset(fName, 0, sizeof(fName));
        memset(fUUID, 0, sizeof(fUUID));
    }

    JackReserveNameRequest(std::int32_t refnum, const char* name, const char* uuid)
        : JackRequestImpl(kReserveClientName)
        , fRefNum(refnum)
    {
        memset(fName, 0, sizeof(fName));
        memset(fUUID, 0, sizeof(fUUID));
        strncpy(fName, name, sizeof(fName) - 1);
        strncpy(fUUID, uuid, sizeof(fUUID) - 1);
    }

    std::int32_t Read(detail::JackChannelTransactionInterface* trans) override
    {
        CheckRes(trans->Read(&fUUID, sizeof(fUUID)));
        CheckRes(trans->Read(&fName, sizeof(fName)));
        CheckRes(trans->Read(&fRefNum, sizeof(fRefNum)));
        return 0;
    }

    std::int32_t Write(detail::JackChannelTransactionInterface* trans) override
    {
        CheckRes(WritePacketInit());
        CheckRes(WritePacket(&fUUID, sizeof(fUUID)));
        CheckRes(WritePacket(&fName, sizeof(fName)));
        CheckRes(WritePacket(&fRefNum, sizeof(fRefNum)));
        return JackRequestImpl::Write(trans);
    }
};


// \brief ClientNotification.

struct JackClientNotification {
    static constexpr std::int32_t PayloadSize = (JACK_CLIENT_NAME_SIZE + 1) + 5 * sizeof(std::int32_t) + (JACK_MESSAGE_SIZE + 1);

    char fName[JACK_CLIENT_NAME_SIZE + 1];
    std::int32_t fRefNum;
    std::int32_t fNotify;
    std::int32_t fValue1;
    std::int32_t fValue2;
    std::int32_t fSync;
    char fMessage[JACK_MESSAGE_SIZE + 1];

    JackClientNotification()
        : fRefNum(0)
        , fNotify(-1)
        , fValue1(-1)
        , fValue2(-1)
        , fSync(0)
    {
        memset(fName, 0, sizeof(fName));
        memset(fMessage, 0, sizeof(fMessage));
    }
    JackClientNotification(const char* name, std::int32_t refnum, std::int32_t notify, std::int32_t sync, const char* message, std::int32_t value1, std::int32_t value2)
        : fRefNum(refnum)
        , fNotify(notify)
        , fValue1(value1)
        , fValue2(value2)
        , fSync(sync)
    {
        memset(fName, 0, sizeof(fName));
        memset(fMessage, 0, sizeof(fMessage));
        strncpy(fName, name, sizeof(fName) - 1);
        if (message) {
            strncpy(fMessage, message, sizeof(fMessage) - 1);
        }
    }

    // The payload of a notification message.  The header's type is the notification kind (fNotify), and its request id
    // is 0, unless the notification is synchronous: then the client's reply echoes the id.
    std::int32_t Read(detail::JackChannelTransactionInterface* trans)
    {
        CheckRes(trans->Read(&fName, sizeof(fName)));
        CheckRes(trans->Read(&fRefNum, sizeof(std::int32_t)));
        CheckRes(trans->Read(&fNotify, sizeof(std::int32_t)));
        CheckRes(trans->Read(&fValue1, sizeof(std::int32_t)));
        CheckRes(trans->Read(&fValue2, sizeof(std::int32_t)));
        CheckRes(trans->Read(&fSync, sizeof(std::int32_t)));
        CheckRes(trans->Read(&fMessage, sizeof(fMessage)));
        return 0;
    }

    std::int32_t Write(detail::JackChannelTransactionInterface* trans, std::uint32_t request)
    {
        std::array<std::byte, PayloadSize> payload { };
        std::byte* out = payload.data();
        auto put = [&out](const void* data, std::size_t size) {
            memcpy(out, data, size);
            out += size;
        };
        put(fName, sizeof(fName));
        put(&fRefNum, sizeof(std::int32_t));
        put(&fNotify, sizeof(std::int32_t));
        put(&fValue1, sizeof(std::int32_t));
        put(&fValue2, sizeof(std::int32_t));
        put(&fSync, sizeof(std::int32_t));
        put(fMessage, sizeof(fMessage));
        return SendJackMessage(trans, MakeJackMsgHeader(static_cast<std::uint16_t>(fNotify), PayloadSize, request), payload.data(), PayloadSize);
    }
};

} // namespace jackmini
