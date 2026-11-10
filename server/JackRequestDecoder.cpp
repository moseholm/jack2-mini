// Copyright (C) 2012 Grame
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

#include "JackRequestDecoder.h"
#include "JackChannel.h"
#include "JackLockedEngine.h"
#include "JackServer.h"

#include <cstdint>
#include <assert.h>
#include <signal.h>

using namespace std;

namespace jackmini {

#define CheckRead(req, socket)                                                                 \
    {                                                                                          \
        if (header.length != static_cast<std::uint32_t>(req.Size()) || req.Read(socket) < 0) { \
            jack_error("CheckRead error");                                                     \
            return -1;                                                                         \
        }                                                                                      \
    }
#define CheckWriteName(error, socket)                                 \
    {                                                                 \
        if (SendJackReply(socket, res, header) < 0) {                 \
            jack_error("%s write error name = %s", error, req.fName); \
        }                                                             \
    }
#define CheckWriteRefNum(error, socket)                                \
    {                                                                  \
        if (SendJackReply(socket, res, header) < 0) {                  \
            jack_error("%s write error ref = %d", error, req.fRefNum); \
        }                                                              \
    }
#define CheckWrite(error, socket)                     \
    {                                                 \
        if (SendJackReply(socket, res, header) < 0) { \
            jack_error("%s write error", error);      \
        }                                             \
    }

JackRequestDecoder::JackRequestDecoder(JackServer* server, JackClientHandlerInterface* handler)
    : fServer(server)
    , fHandler(handler)
{
}

JackRequestDecoder::~JackRequestDecoder()
{
}

std::int32_t JackRequestDecoder::Refuse(detail::JackChannelTransactionInterface* socket, const JackMsgHeader& header)
{
    jack_error("JACK protocol mismatch: request version %d, server version %d", header.version, kJackMsgVersion);
    JackResult res(kJackMsgProtocolError);
    return SendJackReply(socket, res, header);
}

std::int32_t JackRequestDecoder::HandleRequest(detail::JackChannelTransactionInterface* socket, const JackMsgHeader& header)
{
    if (header.version != kJackMsgVersion) {
        Refuse(socket, header);
        return -1;
    }

    JackRequest::RequestType type = (JackRequest::RequestType)header.type;

    // Read data
    switch (type) {

    case JackRequest::kClientCheck: {
        jack_log("JackRequest::ClientCheck");
        JackClientCheckRequest req;
        JackClientCheckResult res;
        CheckRead(req, socket);
        res.fResult = ReplyCode(fServer->GetEngine()->ClientCheck(req.fName, req.fUUID, res.fName, req.fProtocol, req.fOptions, &res.fStatus));
        CheckWriteName("JackRequest::ClientCheck", socket);
        // Atomic ClientCheck followed by ClientOpen on same socket
        if (req.fOpen) {
            JackMsgHeader next;
            if (ReadJackMsgHeader(socket, next) < 0) {
                return -1;
            }
            return HandleRequest(socket, next);
        }
        break;
    }

    case JackRequest::kClientOpen: {
        jack_log("JackRequest::ClientOpen");
        JackClientOpenRequest req;
        JackClientOpenResult res;
        CheckRead(req, socket);
        fHandler->ClientAdd(socket, &req, &res);
        CheckWriteName("JackRequest::ClientOpen", socket);
        break;
    }

    case JackRequest::kClientClose: {
        jack_log("JackRequest::ClientClose");
        JackClientCloseRequest req;
        JackResult res;
        CheckRead(req, socket);
        res.fResult = ReplyCode(fServer->GetEngine()->ClientExternalClose(req.fRefNum));
        CheckWriteRefNum("JackRequest::ClientClose", socket);
        fHandler->ClientRemove(socket, req.fRefNum);
        // Will cause the wrapping thread to stop
        return -1;
    }

    case JackRequest::kActivateClient: {
        JackActivateRequest req;
        JackResult res;
        jack_log("JackRequest::ActivateClient");
        CheckRead(req, socket);
        res.fResult = ReplyCode(fServer->GetEngine()->ClientActivate(req.fRefNum, req.fIsRealTime, fServer->IsRunning()));
        CheckWriteRefNum("JackRequest::ActivateClient", socket);
        break;
    }

    case JackRequest::kDeactivateClient: {
        jack_log("JackRequest::DeactivateClient");
        JackDeactivateRequest req;
        JackResult res;
        CheckRead(req, socket);
        res.fResult = ReplyCode(fServer->GetEngine()->ClientDeactivate(req.fRefNum, fServer->IsRunning()));
        CheckWriteRefNum("JackRequest::DeactivateClient", socket);
        break;
    }

    case JackRequest::kRegisterPort: {
        jack_log("JackRequest::RegisterPort");
        JackPortRegisterRequest req;
        JackPortRegisterResult res;
        CheckRead(req, socket);
        res.fResult = ReplyCode(fServer->GetEngine()->PortRegister(req.fRefNum, req.fName, req.fPortType, req.fFlags, req.fBufferSize, &res.fPortIndex));
        CheckWriteRefNum("JackRequest::RegisterPort", socket);
        break;
    }

    case JackRequest::kUnRegisterPort: {
        jack_log("JackRequest::UnRegisterPort");
        JackPortUnRegisterRequest req;
        JackResult res;
        CheckRead(req, socket);
        res.fResult = ReplyCode(fServer->GetEngine()->PortUnRegister(req.fRefNum, req.fPortIndex));
        CheckWriteRefNum("JackRequest::UnRegisterPort", socket);
        break;
    }

    case JackRequest::kConnectNamePorts: {
        jack_log("JackRequest::ConnectNamePorts");
        JackPortConnectNameRequest req;
        JackResult res;
        CheckRead(req, socket);
        res.fResult = ReplyCode(fServer->GetEngine()->PortConnect(req.fRefNum, req.fSrc, req.fDst));
        CheckWriteRefNum("JackRequest::ConnectNamePorts", socket);
        break;
    }

    case JackRequest::kDisconnectNamePorts: {
        jack_log("JackRequest::DisconnectNamePorts");
        JackPortDisconnectNameRequest req;
        JackResult res;
        CheckRead(req, socket);
        res.fResult = ReplyCode(fServer->GetEngine()->PortDisconnect(req.fRefNum, req.fSrc, req.fDst));
        CheckWriteRefNum("JackRequest::DisconnectNamePorts", socket);
        break;
    }

    case JackRequest::kConnectPorts: {
        jack_log("JackRequest::ConnectPorts");
        JackPortConnectRequest req;
        JackResult res;
        CheckRead(req, socket);
        res.fResult = ReplyCode(fServer->GetEngine()->PortConnect(req.fRefNum, req.fSrc, req.fDst));
        CheckWriteRefNum("JackRequest::ConnectPorts", socket);
        break;
    }

    case JackRequest::kDisconnectPorts: {
        jack_log("JackRequest::DisconnectPorts");
        JackPortDisconnectRequest req;
        JackResult res;
        CheckRead(req, socket);
        res.fResult = ReplyCode(fServer->GetEngine()->PortDisconnect(req.fRefNum, req.fSrc, req.fDst));
        CheckWriteRefNum("JackRequest::DisconnectPorts", socket);
        break;
    }

    case JackRequest::kPortRename: {
        jack_log("JackRequest::PortRename");
        JackPortRenameRequest req;
        JackResult res;
        CheckRead(req, socket);
        res.fResult = ReplyCode(fServer->GetEngine()->PortRename(req.fRefNum, req.fPort, req.fName));
        CheckWriteRefNum("JackRequest::PortRename", socket);
        break;
    }

    case JackRequest::kSetBufferSize: {
        jack_log("JackRequest::SetBufferSize");
        JackSetBufferSizeRequest req;
        JackResult res;
        CheckRead(req, socket);
        res.fResult = ReplyCode(fServer->SetBufferSize(req.fBufferSize));
        CheckWrite("JackRequest::SetBufferSize", socket);
        break;
    }

    case JackRequest::kSetFreeWheel: {
        jack_log("JackRequest::SetFreeWheel");
        JackSetFreeWheelRequest req;
        JackResult res;
        CheckRead(req, socket);
        res.fResult = ReplyCode(fServer->SetFreewheel(req.fOnOff));
        CheckWrite("JackRequest::SetFreeWheel", socket);
        break;
    }

    case JackRequest::kComputeTotalLatencies: {
        jack_log("JackRequest::ComputeTotalLatencies");
        JackComputeTotalLatenciesRequest req;
        JackResult res;
        CheckRead(req, socket);
        res.fResult = ReplyCode(fServer->GetEngine()->ComputeTotalLatencies());
        CheckWrite("JackRequest::ComputeTotalLatencies", socket);
        break;
    }

    case JackRequest::kReleaseTimebase: {
        jack_log("JackRequest::ReleaseTimebase");
        JackReleaseTimebaseRequest req;
        JackResult res;
        CheckRead(req, socket);
        res.fResult = ReplyCode(fServer->ReleaseTimebase(req.fRefNum));
        CheckWriteRefNum("JackRequest::ReleaseTimebase", socket);
        break;
    }

    case JackRequest::kSetTimebaseCallback: {
        jack_log("JackRequest::SetTimebaseCallback");
        JackSetTimebaseCallbackRequest req;
        JackResult res;
        CheckRead(req, socket);
        res.fResult = ReplyCode(fServer->SetTimebaseCallback(req.fRefNum, req.fConditionnal));
        CheckWriteRefNum("JackRequest::SetTimebaseCallback", socket);
        break;
    }


    case JackRequest::kNotification: {
        jack_log("JackRequest::Notification");
        JackClientNotificationRequest req;
        CheckRead(req, socket);
        if (req.fNotify == kQUIT) {
            jack_log("JackRequest::Notification kQUIT");
            throw JackQuitException();
        } else {
            fServer->Notify(req.fRefNum, req.fNotify, req.fValue);
        }
        break;
    }


    case JackRequest::kGetClientByUUID: {
        jack_log("JackRequest::GetClientByUUID");
        JackGetClientNameRequest req;
        JackClientNameResult res;
        CheckRead(req, socket);
        res.fResult = ReplyCode(fServer->GetEngine()->GetClientNameForUUID(req.fUUID, res.fName));
        CheckWrite("JackRequest::GetClientByUUID", socket);
        break;
    }

    case JackRequest::kGetUUIDByClient: {
        jack_log("JackRequest::GetUUIDByClient");
        JackGetUUIDRequest req;
        JackUUIDResult res;
        CheckRead(req, socket);
        res.fResult = ReplyCode(fServer->GetEngine()->GetUUIDForClientName(req.fName, res.fUUID));
        CheckWrite("JackRequest::GetUUIDByClient", socket);
        break;
    }

    case JackRequest::kReserveClientName: {
        jack_log("JackRequest::ReserveClientName");
        JackReserveNameRequest req;
        JackResult res;
        CheckRead(req, socket);
        res.fResult = ReplyCode(fServer->GetEngine()->ReserveClientName(req.fName, req.fUUID));
        CheckWrite("JackRequest::ReserveClientName", socket);
        break;
    }


    default:
        jack_error("Unknown request %d", header.type);
        return -1;
    }

    return 0;
}

} // namespace jackmini
