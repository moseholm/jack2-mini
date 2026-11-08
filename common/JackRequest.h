/*
Copyright (C) 2001 Paul Davis
Copyright (C) 2004-2008 Grame
Copyright (C) 2016-2026 Filipe Coelho

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License as published by
the Free Software Foundation; either version 2.1 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.

*/

#ifndef __JackRequest__
#define __JackRequest__

#include "JackCString.h"
#include "JackConstants.h"
#include "JackError.h"
#include "JackPlatformPlug.h"
#include "JackChannel.h"
#include "JackMessage.h"
#include "JackShmMem.h"
#include "JackTime.h"
#include "types.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <array>
#include <list>
#include <vector>

namespace jackmini
{

#define JACK_CLIENT_NAME_SIZE_1 (JACK_CLIENT_NAME_SIZE + 1)
#define JACK_MESSAGE_SIZE_1 (JACK_MESSAGE_SIZE + 1)
#define JACK_PORT_NAME_SIZE_1 (JACK_PORT_NAME_SIZE + 1)
#define JACK_PORT_TYPE_SIZE_1 (JACK_PORT_TYPE_SIZE + 1)
#define REAL_JACK_PORT_NAME_SIZE_1 (REAL_JACK_PORT_NAME_SIZE + 1)

#define CheckRes(exp) { \
    int reserr = (exp); \
    if (reserr < 0) { \
        if (reserr != JACK_REQUEST_ERR_ABORTED) \
            jack_error("CheckRes error for " #exp "in %s", __PRETTY_FUNCTION__); \
        return reserr; \
    } \
}



/*!
\brief Request from client to server.
*/

struct JackRequest
{

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
        : fType(type), fRequestId(NextJackRequestId())
    {}

    virtual ~JackRequest()
    {}

    virtual int Read(detail::JackChannelTransactionInterface* trans) = 0;

    virtual int Write(detail::JackChannelTransactionInterface* trans) = 0;
};

template<int DataSize>
struct JackRequestImpl : JackRequest
{
    static constexpr int TotalSize = DataSize;
    static_assert(DataSize <= static_cast<int>(kJackMsgMaxPayload), "request payload larger than a message");

    JackRequestImpl(RequestType type)
        : JackRequest(type), fPacketInit(false), fPacketPos(0)
    {
    }

    int WritePacketInit()
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

    int WritePacket(void* data, int len)
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

    int Write(detail::JackChannelTransactionInterface* trans) override
    {
        if (!fPacketInit || fPacketPos != TotalSize) {
            jack_error("JackRequestImpl::Write failed for type %d because of incomplete data, pos %d, total size %d",
                       fType, fPacketPos, TotalSize);
            return -1;
        }
        // invalidate any more writes
        fPacketPos = -1;
        const int err = SendJackMessage(trans, MakeJackMsgHeader(fType, TotalSize, fRequestId), fPacketData.data(), TotalSize);
        if (err < 0 && err != JACK_REQUEST_ERR_ABORTED)
            jack_error("JackRequestImpl::Write failed for type %d", fType);
        return err;
    }

    static constexpr int Size() { return DataSize; }
private:
    std::array<uint8_t, TotalSize> fPacketData{};
    bool fPacketInit;
    int fPacketPos;
};

/*!
\brief Result from the server.
*/

struct JackResult
{

    int fResult;

    JackResult(): fResult( -1)
    {}
    JackResult(int result): fResult(result)
    {}
    virtual ~JackResult()
    {}

    virtual int Read(detail::JackChannelTransactionInterface* trans)
    {
        return trans->Read(&fResult, sizeof(int));
    }

    virtual int Write(detail::JackChannelTransactionInterface* trans)
    {
        return trans->Write(&fResult, sizeof(int));
    }

};

// The reply to a request: the request's type and id, and the result as the payload.
inline int SendJackReply(detail::JackChannelTransactionInterface* trans, JackResult& res, const JackMsgHeader& request)
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
inline int ReadJackReply(detail::JackClientRequestInterface* socket, std::uint16_t type, std::uint32_t request, JackResult& res)
{
    JackMsgHeader header{};
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

/*!
\brief CheckClient request.
*/

struct JackClientCheckRequest : public JackRequestImpl<JACK_CLIENT_NAME_SIZE_1 + 3 * sizeof(int) + sizeof(jack_uuid_t)>
{

    char fName[JACK_CLIENT_NAME_SIZE_1];
    int fProtocol;
    int fOptions;
    int fOpen;
    jack_uuid_t fUUID;

    JackClientCheckRequest()
        : JackRequestImpl(kClientCheck), fProtocol(0), fOptions(0), fOpen(0), fUUID(JACK_UUID_EMPTY_INITIALIZER)
    {
        memset(fName, 0, sizeof(fName));
    }

    JackClientCheckRequest(const char* name, int protocol, int options, jack_uuid_t uuid, int open = false)
        : JackRequestImpl(kClientCheck), fProtocol(protocol), fOptions(options), fOpen(open), fUUID(uuid)
    {
        memset(fName, 0, sizeof(fName));
        CopyCString(fName, name);
    }

    int Read(detail::JackChannelTransactionInterface* trans) override
    {
        CheckRes(trans->Read(&fName, sizeof(fName)));
        CheckRes(trans->Read(&fProtocol, sizeof(int)));
        CheckRes(trans->Read(&fOptions, sizeof(int)));
        CheckRes(trans->Read(&fUUID, sizeof(jack_uuid_t)));
        return trans->Read(&fOpen, sizeof(int));
    }

    int Write(detail::JackChannelTransactionInterface* trans) override
    {
        CheckRes(WritePacketInit());
        CheckRes(WritePacket(&fName, sizeof(fName)));
        CheckRes(WritePacket(&fProtocol, sizeof(int)));
        CheckRes(WritePacket(&fOptions, sizeof(int)));
        CheckRes(WritePacket(&fUUID, sizeof(jack_uuid_t)));
        CheckRes(WritePacket(&fOpen, sizeof(int)));
        return JackRequestImpl::Write(trans);
    }

    static constexpr int Size() { return sizeof(fName) + 3 * sizeof(int) + sizeof(jack_uuid_t); }

};

/*!
\brief CheckClient result.
*/

struct JackClientCheckResult : public JackResult
{

    char fName[JACK_CLIENT_NAME_SIZE+1];
    int fStatus;

    JackClientCheckResult(): JackResult(), fStatus(0)
    {
        memset(fName, 0, sizeof(fName));
    }
    JackClientCheckResult(int32_t result, const char* name, int status)
            : JackResult(result), fStatus(status)
    {
        memset(fName, 0, sizeof(fName));
        CopyCString(fName, name);
    }

    int Read(detail::JackChannelTransactionInterface* trans) override
    {
        CheckRes(JackResult::Read(trans));
        CheckRes(trans->Read(&fName, sizeof(fName)));
        CheckRes(trans->Read(&fStatus, sizeof(int)));
        return 0;
    }

    int Write(detail::JackChannelTransactionInterface* trans) override
    {
        CheckRes(JackResult::Write(trans));
        CheckRes(trans->Write(&fName, sizeof(fName)));
        CheckRes(trans->Write(&fStatus, sizeof(int)));
        return 0;
    }

};

/*!
\brief NewClient request.
*/

struct JackClientOpenRequest : public JackRequestImpl<sizeof(int) + sizeof(jack_uuid_t) + JACK_CLIENT_NAME_SIZE_1>
{

    int fPID;
    jack_uuid_t fUUID;
    char fName[JACK_CLIENT_NAME_SIZE_1];

    JackClientOpenRequest()
        : JackRequestImpl(kClientOpen), fPID(0), fUUID(JACK_UUID_EMPTY_INITIALIZER)
    {
        memset(fName, 0, sizeof(fName));
    }

    JackClientOpenRequest(const char* name, int pid, jack_uuid_t uuid)
        : JackRequestImpl(kClientOpen)
    {
        memset(fName, 0, sizeof(fName));
        CopyCString(fName, name);
        fPID = pid;
        fUUID = uuid;
    }

    int Read(detail::JackChannelTransactionInterface* trans) override
    {
        CheckRes(trans->Read(&fPID, sizeof(int)));
        CheckRes(trans->Read(&fUUID, sizeof(jack_uuid_t)));
        return trans->Read(&fName, sizeof(fName));
    }

    int Write(detail::JackChannelTransactionInterface* trans) override
    {
        CheckRes(WritePacketInit());
        CheckRes(WritePacket(&fPID, sizeof(int)));
        CheckRes(WritePacket(&fUUID, sizeof(jack_uuid_t)));
        CheckRes(WritePacket(&fName, sizeof(fName)));
        return JackRequestImpl::Write(trans);
    }

};

/*!
\brief NewClient result.

The payload is the status and the sizes of the three segments. The descriptors go with it as SCM_RIGHTS, in one message, in the order engine, graph, client control.
*/

struct JackClientOpenResultPayload
{
    int32_t fResult;
    uint32_t fEngineSize;
    uint32_t fGraphSize;
    uint32_t fClientSize;
};
static_assert(sizeof(JackClientOpenResultPayload) == 16);

struct JackClientOpenResult : public JackResult
{

    JackShmSegment fSharedEngine;
    JackShmSegment fSharedClient;
    JackShmSegment fSharedGraph;

    JackClientOpenResult()
            : JackResult()
    {}

    int Read(detail::JackChannelTransactionInterface* trans) override
    {
        JackClientOpenResultPayload payload{};
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
        fSharedEngine = JackShmSegment{ fds[0].release(), payload.fEngineSize };
        fSharedGraph = JackShmSegment{ fds[1].release(), payload.fGraphSize };
        fSharedClient = JackShmSegment{ fds[2].release(), payload.fClientSize };
        return 0;
    }

    int Write(detail::JackChannelTransactionInterface* trans) override
    {
        JackClientOpenResultPayload payload{ fResult, fSharedEngine.fSize, fSharedGraph.fSize, fSharedClient.fSize };
        if (fResult != 0) {
            return trans->WriteWithFds(&payload, sizeof(payload), {});
        }
        const std::array<int, 3> fds{ fSharedEngine.fFd, fSharedGraph.fFd, fSharedClient.fFd };
        return trans->WriteWithFds(&payload, sizeof(payload), fds);
    }

};

/*!
\brief CloseClient request.
*/

struct JackClientCloseRequest : public JackRequestImpl<sizeof(int)>
{

    int fRefNum;

    JackClientCloseRequest()
        : JackRequestImpl(kClientClose), fRefNum(0)
    {}

    JackClientCloseRequest(int refnum)
        : JackRequestImpl(kClientClose), fRefNum(refnum)
    {}

    int Read(detail::JackChannelTransactionInterface* trans) override
    {
        return trans->Read(&fRefNum, sizeof(int));
    }

    int Write(detail::JackChannelTransactionInterface* trans) override
    {
        CheckRes(WritePacketInit());
        CheckRes(WritePacket(&fRefNum, sizeof(int)));
        return JackRequestImpl::Write(trans);
    }

};

/*!
\brief Activate request.
*/

struct JackActivateRequest : public JackRequestImpl<2 * sizeof(int)>
{

    int fRefNum;
    int fIsRealTime;

    JackActivateRequest()
        : JackRequestImpl(kActivateClient), fRefNum(0), fIsRealTime(0)
    {}

    JackActivateRequest(int refnum, int is_real_time)
        : JackRequestImpl(kActivateClient), fRefNum(refnum), fIsRealTime(is_real_time)
    {}

    int Read(detail::JackChannelTransactionInterface* trans) override
    {
        CheckRes(trans->Read(&fRefNum, sizeof(int)));
        return trans->Read(&fIsRealTime, sizeof(int));
    }

    int Write(detail::JackChannelTransactionInterface* trans) override
    {
        CheckRes(WritePacketInit());
        CheckRes(WritePacket(&fRefNum, sizeof(int)));
        CheckRes(WritePacket(&fIsRealTime, sizeof(int)));
        return JackRequestImpl::Write(trans);
    }

};

/*!
\brief Deactivate request.
*/

struct JackDeactivateRequest : public JackRequestImpl<sizeof(int)>
{

    int fRefNum;

    JackDeactivateRequest()
        : JackRequestImpl(kDeactivateClient), fRefNum(0)
    {}

    JackDeactivateRequest(int refnum)
        : JackRequestImpl(kDeactivateClient), fRefNum(refnum)
    {}

    int Read(detail::JackChannelTransactionInterface* trans) override
    {
        return trans->Read(&fRefNum, sizeof(int));
    }

    int Write(detail::JackChannelTransactionInterface* trans) override
    {
        CheckRes(WritePacketInit());
        CheckRes(WritePacket(&fRefNum, sizeof(int)));
        return JackRequestImpl::Write(trans);
    }

};

/*!
\brief PortRegister request.
*/

struct JackPortRegisterRequest : public JackRequestImpl<sizeof(int) + JACK_PORT_NAME_SIZE_1 + JACK_PORT_TYPE_SIZE_1 + 2 * sizeof(unsigned int)>
{

    int fRefNum;
    char fName[JACK_PORT_NAME_SIZE_1];   // port short name
    char fPortType[JACK_PORT_TYPE_SIZE_1];
    unsigned int fFlags;
    unsigned int fBufferSize;

    JackPortRegisterRequest()
        : JackRequestImpl(kRegisterPort), fRefNum(0), fFlags(0), fBufferSize(0)
    {
        memset(fName, 0, sizeof(fName));
        memset(fPortType, 0, sizeof(fPortType));
    }

    JackPortRegisterRequest(int refnum, const char* name, const char* port_type, unsigned int flags, unsigned int buffer_size)
        : JackRequestImpl(kRegisterPort), fRefNum(refnum), fFlags(flags), fBufferSize(buffer_size)
    {
        memset(fName, 0, sizeof(fName));
        memset(fPortType, 0, sizeof(fPortType));
        strncpy(fName, name, sizeof(fName)-1);
        strncpy(fPortType, port_type, sizeof(fPortType)-1);
    }

    int Read(detail::JackChannelTransactionInterface* trans) override
    {
        CheckRes(trans->Read(&fRefNum, sizeof(int)));
        CheckRes(trans->Read(&fName, sizeof(fName)));
        CheckRes(trans->Read(&fPortType, sizeof(fPortType)));
        CheckRes(trans->Read(&fFlags, sizeof(unsigned int)));
        CheckRes(trans->Read(&fBufferSize, sizeof(unsigned int)));
        return 0;
    }

    int Write(detail::JackChannelTransactionInterface* trans) override
    {
        CheckRes(WritePacketInit());
        CheckRes(WritePacket(&fRefNum, sizeof(int)));
        CheckRes(WritePacket(&fName, sizeof(fName)));
        CheckRes(WritePacket(&fPortType, sizeof(fPortType)));
        CheckRes(WritePacket(&fFlags, sizeof(unsigned int)));
        CheckRes(WritePacket(&fBufferSize, sizeof(unsigned int)));
        return JackRequestImpl::Write(trans);
    }

};

/*!
\brief PortRegister result.
*/

struct JackPortRegisterResult : public JackResult
{

    jack_port_id_t fPortIndex;

    JackPortRegisterResult(): JackResult(), fPortIndex(NO_PORT)
    {}

    int Read(detail::JackChannelTransactionInterface* trans) override
    {
        CheckRes(JackResult::Read(trans));
        return trans->Read(&fPortIndex, sizeof(jack_port_id_t));
    }

    int Write(detail::JackChannelTransactionInterface* trans) override
    {
        CheckRes(JackResult::Write(trans));
        return trans->Write(&fPortIndex, sizeof(jack_port_id_t));
    }

};

/*!
\brief PortUnregister request.
*/

struct JackPortUnRegisterRequest : public JackRequestImpl<sizeof(int) + sizeof(jack_port_id_t)>
{

    int fRefNum;
    jack_port_id_t fPortIndex;

    JackPortUnRegisterRequest()
        : JackRequestImpl(kUnRegisterPort), fRefNum(0), fPortIndex(0)
    {}

    JackPortUnRegisterRequest(int refnum, jack_port_id_t index)
        : JackRequestImpl(kUnRegisterPort), fRefNum(refnum), fPortIndex(index)
    {}

    int Read(detail::JackChannelTransactionInterface* trans) override
    {
        CheckRes(trans->Read(&fRefNum, sizeof(int)));
        CheckRes(trans->Read(&fPortIndex, sizeof(jack_port_id_t)));
        return 0;
    }

    int Write(detail::JackChannelTransactionInterface* trans) override
    {
        CheckRes(WritePacketInit());
        CheckRes(WritePacket(&fRefNum, sizeof(int)));
        CheckRes(WritePacket(&fPortIndex, sizeof(jack_port_id_t)));
        return JackRequestImpl::Write(trans);
    }

};

/*!
\brief PortConnectName request.
*/

struct JackPortConnectNameRequest : public JackRequestImpl<sizeof(int) + 2 * REAL_JACK_PORT_NAME_SIZE_1>
{

    int fRefNum;
    char fSrc[REAL_JACK_PORT_NAME_SIZE_1];    // port full name
    char fDst[REAL_JACK_PORT_NAME_SIZE_1];    // port full name

    JackPortConnectNameRequest()
        : JackRequestImpl(kConnectNamePorts), fRefNum(0)
    {
        memset(fSrc, 0, sizeof(fSrc));
        memset(fDst, 0, sizeof(fDst));
    }

    JackPortConnectNameRequest(int refnum, const char* src_name, const char* dst_name)
        : JackRequestImpl(kConnectNamePorts), fRefNum(refnum)
    {
        memset(fSrc, 0, sizeof(fSrc));
        memset(fDst, 0, sizeof(fDst));
        strncpy(fSrc, src_name, sizeof(fSrc)-1);
        strncpy(fDst, dst_name, sizeof(fDst)-1);
    }

    int Read(detail::JackChannelTransactionInterface* trans) override
    {
        CheckRes(trans->Read(&fRefNum, sizeof(int)));
        CheckRes(trans->Read(&fSrc, sizeof(fSrc)));
        CheckRes(trans->Read(&fDst, sizeof(fDst)));
        return 0;
    }

    int Write(detail::JackChannelTransactionInterface* trans) override
    {
        CheckRes(WritePacketInit());
        CheckRes(WritePacket(&fRefNum, sizeof(int)));
        CheckRes(WritePacket(&fSrc, sizeof(fSrc)));
        CheckRes(WritePacket(&fDst, sizeof(fDst)));
        return JackRequestImpl::Write(trans);
    }

};

/*!
\brief PortDisconnectName request.
*/

struct JackPortDisconnectNameRequest : public JackRequestImpl<sizeof(int) + 2 * REAL_JACK_PORT_NAME_SIZE_1>
{

    int fRefNum;
    char fSrc[REAL_JACK_PORT_NAME_SIZE_1];    // port full name
    char fDst[REAL_JACK_PORT_NAME_SIZE_1];    // port full name

    JackPortDisconnectNameRequest()
        : JackRequestImpl(kDisconnectNamePorts), fRefNum(0)
    {
        memset(fSrc, 0, sizeof(fSrc));
        memset(fDst, 0, sizeof(fDst));
    }

    JackPortDisconnectNameRequest(int refnum, const char* src_name, const char* dst_name)
        : JackRequestImpl(kDisconnectNamePorts), fRefNum(refnum)
    {
        memset(fSrc, 0, sizeof(fSrc));
        memset(fDst, 0, sizeof(fDst));
        strncpy(fSrc, src_name, sizeof(fSrc)-1);
        strncpy(fDst, dst_name, sizeof(fDst)-1);
    }

    int Read(detail::JackChannelTransactionInterface* trans) override
    {
        CheckRes(trans->Read(&fRefNum, sizeof(int)));
        CheckRes(trans->Read(&fSrc, sizeof(fSrc)));
        CheckRes(trans->Read(&fDst, sizeof(fDst)));
        return 0;
    }

    int Write(detail::JackChannelTransactionInterface* trans) override
    {
        CheckRes(WritePacketInit());
        CheckRes(WritePacket(&fRefNum, sizeof(int)));
        CheckRes(WritePacket(&fSrc, sizeof(fSrc)));
        CheckRes(WritePacket(&fDst, sizeof(fDst)));
        return JackRequestImpl::Write(trans);
    }

};

/*!
\brief PortConnect request.
*/

struct JackPortConnectRequest : public JackRequestImpl<sizeof(int) + 2 * sizeof(jack_port_id_t)>
{

    int fRefNum;
    jack_port_id_t fSrc;
    jack_port_id_t fDst;

    JackPortConnectRequest()
        : JackRequestImpl(kConnectPorts), fRefNum(0), fSrc(0), fDst(0)
    {}

    JackPortConnectRequest(int refnum, jack_port_id_t src, jack_port_id_t dst)
        : JackRequestImpl(kConnectPorts), fRefNum(refnum), fSrc(src), fDst(dst)
    {}

    int Read(detail::JackChannelTransactionInterface* trans) override
    {
        CheckRes(trans->Read(&fRefNum, sizeof(int)));
        CheckRes(trans->Read(&fSrc, sizeof(jack_port_id_t)));
        CheckRes(trans->Read(&fDst, sizeof(jack_port_id_t)));
        return 0;
    }

    int Write(detail::JackChannelTransactionInterface* trans) override
    {
        CheckRes(WritePacketInit());
        CheckRes(WritePacket(&fRefNum, sizeof(int)));
        CheckRes(WritePacket(&fSrc, sizeof(jack_port_id_t)));
        CheckRes(WritePacket(&fDst, sizeof(jack_port_id_t)));
        return JackRequestImpl::Write(trans);
    }

};

/*!
\brief PortDisconnect request.
*/

struct JackPortDisconnectRequest : public JackRequestImpl<sizeof(int) + 2 * sizeof(jack_port_id_t)>
{

    int fRefNum;
    jack_port_id_t fSrc;
    jack_port_id_t fDst;

    JackPortDisconnectRequest()
        : JackRequestImpl(kDisconnectPorts), fRefNum(0), fSrc(0), fDst(0)
    {}

    JackPortDisconnectRequest(int refnum, jack_port_id_t src, jack_port_id_t dst)
        : JackRequestImpl(kDisconnectPorts), fRefNum(refnum), fSrc(src), fDst(dst)
    {}

    int Read(detail::JackChannelTransactionInterface* trans) override
    {
        CheckRes(trans->Read(&fRefNum, sizeof(int)));
        CheckRes(trans->Read(&fSrc, sizeof(jack_port_id_t)));
        CheckRes(trans->Read(&fDst, sizeof(jack_port_id_t)));
        return 0;
    }

    int Write(detail::JackChannelTransactionInterface* trans) override
    {
        CheckRes(WritePacketInit());
        CheckRes(WritePacket(&fRefNum, sizeof(int)));
        CheckRes(WritePacket(&fSrc, sizeof(jack_port_id_t)));
        CheckRes(WritePacket(&fDst, sizeof(jack_port_id_t)));
        return JackRequestImpl::Write(trans);
    }

};

/*!
\brief PortRename request.
*/

struct JackPortRenameRequest : public JackRequestImpl<sizeof(int) + sizeof(jack_port_id_t) + JACK_PORT_NAME_SIZE_1>
{

    int fRefNum;
    jack_port_id_t fPort;
    char fName[JACK_PORT_NAME_SIZE_1];   // port short name

    JackPortRenameRequest()
        : JackRequestImpl(kPortRename), fRefNum(0), fPort(0)
    {
        memset(fName, 0, sizeof(fName));
    }

    JackPortRenameRequest(int refnum, jack_port_id_t port, const char* name)
        : JackRequestImpl(kPortRename), fRefNum(refnum), fPort(port)
    {
        memset(fName, 0, sizeof(fName));
        strncpy(fName, name, sizeof(fName)-1);
    }

    int Read(detail::JackChannelTransactionInterface* trans) override
    {
        CheckRes(trans->Read(&fRefNum, sizeof(int)));
        CheckRes(trans->Read(&fPort, sizeof(jack_port_id_t)));
        CheckRes(trans->Read(&fName, sizeof(fName)));
        return 0;
    }

    int Write(detail::JackChannelTransactionInterface* trans) override
    {
        CheckRes(WritePacketInit());
        CheckRes(WritePacket(&fRefNum, sizeof(int)));
        CheckRes(WritePacket(&fPort, sizeof(jack_port_id_t)));
        CheckRes(WritePacket(&fName, sizeof(fName)));
        return JackRequestImpl::Write(trans);
    }

};

/*!
\brief SetBufferSize request.
*/

struct JackSetBufferSizeRequest : public JackRequestImpl<sizeof(jack_nframes_t)>
{

    jack_nframes_t fBufferSize;

    JackSetBufferSizeRequest()
        : JackRequestImpl(kSetBufferSize), fBufferSize(0)
    {}

    JackSetBufferSizeRequest(jack_nframes_t buffer_size)
        : JackRequestImpl(kSetBufferSize), fBufferSize(buffer_size)
    {}

    int Read(detail::JackChannelTransactionInterface* trans) override
    {
        return trans->Read(&fBufferSize, sizeof(jack_nframes_t));
    }

    int Write(detail::JackChannelTransactionInterface* trans) override
    {
        CheckRes(WritePacketInit());
        CheckRes(WritePacket(&fBufferSize, sizeof(jack_nframes_t)));
        return JackRequestImpl::Write(trans);
    }

};

/*!
\brief SetFreeWheel request.
*/

struct JackSetFreeWheelRequest : public JackRequestImpl<sizeof(int)>
{

    int fOnOff;

    JackSetFreeWheelRequest()
        : JackRequestImpl(kSetFreeWheel), fOnOff(0)
    {}

    JackSetFreeWheelRequest(int onoff)
        : JackRequestImpl(kSetFreeWheel), fOnOff(onoff)
    {}

    int Read(detail::JackChannelTransactionInterface* trans) override
    {
        return trans->Read(&fOnOff, sizeof(int));
    }

    int Write(detail::JackChannelTransactionInterface* trans) override
    {
        CheckRes(WritePacketInit());
        CheckRes(WritePacket(&fOnOff, sizeof(int)));
        return JackRequestImpl::Write(trans);
    }

};

/*!
\brief ComputeTotalLatencies request.
*/

struct JackComputeTotalLatenciesRequest : public JackRequestImpl<0>
{

    JackComputeTotalLatenciesRequest()
        : JackRequestImpl(kComputeTotalLatencies)
    {}

    int Read(detail::JackChannelTransactionInterface* trans) override
    {
        return 0;
    }

    int Write(detail::JackChannelTransactionInterface* trans) override
    {
        CheckRes(WritePacketInit());
        return JackRequestImpl::Write(trans);
    }

};

/*!
\brief ReleaseTimebase request.
*/

struct JackReleaseTimebaseRequest : public JackRequestImpl<sizeof(int)>
{

    int fRefNum;

    JackReleaseTimebaseRequest()
        : JackRequestImpl(kReleaseTimebase), fRefNum(0)
    {}

    JackReleaseTimebaseRequest(int refnum)
        : JackRequestImpl(kReleaseTimebase), fRefNum(refnum)
    {}

    int Read(detail::JackChannelTransactionInterface* trans) override
    {
        return trans->Read(&fRefNum, sizeof(int));
    }

    int Write(detail::JackChannelTransactionInterface* trans) override
    {
        CheckRes(WritePacketInit());
        CheckRes(WritePacket(&fRefNum, sizeof(int)));
        return JackRequestImpl::Write(trans);
    }

};

/*!
\brief SetTimebaseCallback request.
*/

struct JackSetTimebaseCallbackRequest : public JackRequestImpl<sizeof(int) + sizeof(int)>
{

    int fRefNum;
    int fConditionnal;

    JackSetTimebaseCallbackRequest()
        : JackRequestImpl(kSetTimebaseCallback), fRefNum(0), fConditionnal(0)
    {}

    JackSetTimebaseCallbackRequest(int refnum, int conditional)
        : JackRequestImpl(kSetTimebaseCallback), fRefNum(refnum), fConditionnal(conditional)
    {}

    int Read(detail::JackChannelTransactionInterface* trans) override
    {
        CheckRes(trans->Read(&fRefNum, sizeof(int)));
        return trans->Read(&fConditionnal, sizeof(int));
    }

    int Write(detail::JackChannelTransactionInterface* trans) override
    {
        CheckRes(WritePacketInit());
        CheckRes(WritePacket(&fRefNum, sizeof(int)));
        CheckRes(WritePacket(&fConditionnal, sizeof(int)));
        return JackRequestImpl::Write(trans);
    }

};

/*!
\brief GetInternalClientName request.
*/

/*!
\brief GetInternalClient result.
*/

/*!
\brief InternalClientHandle request.
*/

/*!
\brief InternalClientHandle result.
*/

/*!
\brief InternalClientLoad request.
*/

#ifndef MAX_PATH
#define MAX_PATH 256
#endif
#define MAX_PATH_1 (MAX_PATH + 1)

/*!
\brief InternalClientLoad result.
*/

/*!
\brief InternalClientUnload request.
*/

/*!
\brief InternalClientLoad result.
*/

/*!
\brief ClientNotification request.
*/

struct JackClientNotificationRequest : public JackRequestImpl<3 * sizeof(int)>
{

    int fRefNum;
    int fNotify;
    int fValue;

    JackClientNotificationRequest()
        : JackRequestImpl(kNotification), fRefNum(0), fNotify(0), fValue(0)
    {}

    JackClientNotificationRequest(int refnum, int notify, int value)
        : JackRequestImpl(kNotification), fRefNum(refnum), fNotify(notify), fValue(value)
    {}

    int Read(detail::JackChannelTransactionInterface* trans) override
    {
        CheckRes(trans->Read(&fRefNum, sizeof(int)));
        CheckRes(trans->Read(&fNotify, sizeof(int)));
        CheckRes(trans->Read(&fValue, sizeof(int)));
        return 0;
    }

    int Write(detail::JackChannelTransactionInterface* trans) override
    {
        CheckRes(WritePacketInit());
        CheckRes(WritePacket(&fRefNum, sizeof(int)));
        CheckRes(WritePacket(&fNotify, sizeof(int)));
        CheckRes(WritePacket(&fValue, sizeof(int)));
        return JackRequestImpl::Write(trans);
    }

};

/*!
\brief SessionNotify request.
*/

struct JackClientNameResult : public JackResult
{
    char fName[JACK_CLIENT_NAME_SIZE+1];

    JackClientNameResult(): JackResult()
    {
        memset(fName, 0, sizeof(fName));
    }
    JackClientNameResult(int32_t result, const char* name)
            : JackResult(result)
    {
        memset(fName, 0, sizeof(fName));
        strncpy(fName, name, sizeof(fName)-1);
    }

    int Read(detail::JackChannelTransactionInterface* trans) override
    {
        CheckRes(JackResult::Read(trans));
        CheckRes(trans->Read(&fName, sizeof(fName)));
        return 0;
    }

    int Write(detail::JackChannelTransactionInterface* trans) override
    {
        CheckRes(JackResult::Write(trans));
        CheckRes(trans->Write(&fName, sizeof(fName)));
        return 0;
    }

};

struct JackUUIDResult : public JackResult
{
    char fUUID[JACK_UUID_STRING_SIZE];

    JackUUIDResult(): JackResult()
    {
        memset(fUUID, 0, sizeof(fUUID));
    }
    JackUUIDResult(int32_t result, const char* uuid)
            : JackResult(result)
    {
        memset(fUUID, 0, sizeof(fUUID));
        strncpy(fUUID, uuid, sizeof(fUUID)-1);
    }

    int Read(detail::JackChannelTransactionInterface* trans) override
    {
        CheckRes(JackResult::Read(trans));
        CheckRes(trans->Read(&fUUID, sizeof(fUUID)));
        return 0;
    }

    int Write(detail::JackChannelTransactionInterface* trans) override
    {
        CheckRes(JackResult::Write(trans));
        CheckRes(trans->Write(&fUUID, sizeof(fUUID)));
        return 0;
    }

};

struct JackGetUUIDRequest : public JackRequestImpl<JACK_CLIENT_NAME_SIZE_1>
{
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
        strncpy(fName, client_name, sizeof(fName)-1);
    }

    int Read(detail::JackChannelTransactionInterface* trans) override
    {
        CheckRes(trans->Read(&fName, sizeof(fName)));
        return 0;
    }

    int Write(detail::JackChannelTransactionInterface* trans) override
    {
        CheckRes(WritePacketInit());
        CheckRes(WritePacket(&fName, sizeof(fName)));
        return JackRequestImpl::Write(trans);
    }

};

struct JackGetClientNameRequest : public JackRequestImpl<JACK_UUID_STRING_SIZE>
{
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
        strncpy(fUUID, uuid, sizeof(fUUID)-1);
    }

    int Read(detail::JackChannelTransactionInterface* trans) override
    {
        CheckRes(trans->Read(&fUUID, sizeof(fUUID)));
        return 0;
    }

    int Write(detail::JackChannelTransactionInterface* trans) override
    {
        CheckRes(WritePacketInit());
        CheckRes(WritePacket(&fUUID, sizeof(fUUID)));
        return JackRequestImpl::Write(trans);
    }

};

struct JackReserveNameRequest : public JackRequestImpl<sizeof(int) + JACK_CLIENT_NAME_SIZE_1 + JACK_UUID_STRING_SIZE>
{
    int  fRefNum;
    char fName[JACK_CLIENT_NAME_SIZE_1];
    char fUUID[JACK_UUID_STRING_SIZE];

    JackReserveNameRequest()
        : JackRequestImpl(kReserveClientName), fRefNum(0)
    {
        memset(fName, 0, sizeof(fName));
        memset(fUUID, 0, sizeof(fUUID));
    }

    JackReserveNameRequest(int refnum, const char *name, const char* uuid)
        : JackRequestImpl(kReserveClientName), fRefNum(refnum)
    {
        memset(fName, 0, sizeof(fName));
        memset(fUUID, 0, sizeof(fUUID));
        strncpy(fName, name, sizeof(fName)-1);
        strncpy(fUUID, uuid, sizeof(fUUID)-1);
    }

    int Read(detail::JackChannelTransactionInterface* trans) override
    {
        CheckRes(trans->Read(&fUUID, sizeof(fUUID)));
        CheckRes(trans->Read(&fName, sizeof(fName)));
        CheckRes(trans->Read(&fRefNum, sizeof(fRefNum)));
        return 0;
    }

    int Write(detail::JackChannelTransactionInterface* trans) override
    {
        CheckRes(WritePacketInit());
        CheckRes(WritePacket(&fUUID, sizeof(fUUID)));
        CheckRes(WritePacket(&fName, sizeof(fName)));
        CheckRes(WritePacket(&fRefNum, sizeof(fRefNum)));
        return JackRequestImpl::Write(trans);
    }

};


/*!
\brief ClientNotification.
*/

struct JackClientNotification
{
    static constexpr int PayloadSize = (JACK_CLIENT_NAME_SIZE + 1) + 5 * sizeof(int) + (JACK_MESSAGE_SIZE + 1);

    char fName[JACK_CLIENT_NAME_SIZE+1];
    int fRefNum;
    int fNotify;
    int fValue1;
    int fValue2;
    int fSync;
    char fMessage[JACK_MESSAGE_SIZE+1];

    JackClientNotification(): fRefNum(0), fNotify(-1), fValue1(-1), fValue2(-1), fSync(0)
    {
        memset(fName, 0, sizeof(fName));
        memset(fMessage, 0, sizeof(fMessage));
    }
    JackClientNotification(const char* name, int refnum, int notify, int sync, const char* message, int value1, int value2)
            : fRefNum(refnum), fNotify(notify), fValue1(value1), fValue2(value2), fSync(sync)
    {
        memset(fName, 0, sizeof(fName));
        memset(fMessage, 0, sizeof(fMessage));
        strncpy(fName, name, sizeof(fName)-1);
        if (message) {
            strncpy(fMessage, message, sizeof(fMessage)-1);
        }
    }

    // The payload of a notification message.  The header's type is the notification kind (fNotify), and its request id
    // is 0, unless the notification is synchronous: then the client's reply echoes the id.
    int Read(detail::JackChannelTransactionInterface* trans)
    {
        CheckRes(trans->Read(&fName, sizeof(fName)));
        CheckRes(trans->Read(&fRefNum, sizeof(int)));
        CheckRes(trans->Read(&fNotify, sizeof(int)));
        CheckRes(trans->Read(&fValue1, sizeof(int)));
        CheckRes(trans->Read(&fValue2, sizeof(int)));
        CheckRes(trans->Read(&fSync, sizeof(int)));
        CheckRes(trans->Read(&fMessage, sizeof(fMessage)));
        return 0;
    }

    int Write(detail::JackChannelTransactionInterface* trans, std::uint32_t request)
    {
        std::array<std::byte, PayloadSize> payload{};
        std::byte* out = payload.data();
        auto put = [&out](const void* data, std::size_t size) {
            memcpy(out, data, size);
            out += size;
        };
        put(fName, sizeof(fName));
        put(&fRefNum, sizeof(int));
        put(&fNotify, sizeof(int));
        put(&fValue1, sizeof(int));
        put(&fValue2, sizeof(int));
        put(&fSync, sizeof(int));
        put(fMessage, sizeof(fMessage));
        return SendJackMessage(trans, MakeJackMsgHeader(static_cast<std::uint16_t>(fNotify), PayloadSize, request), payload.data(), PayloadSize);
    }

};

} // end of namespace

#endif
