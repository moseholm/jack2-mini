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

#include "RequestDecoder.h"
#include "Channel.h"
#include "LockedEngine.h"
#include "ServerCore.h"

#include <assert.h>
#include <signal.h>
#include <cstdint>

using namespace std;

namespace jackmini {

#define CheckRead(req, socket)                                                                   \
    {                                                                                            \
        if (header.m_length != static_cast<std::uint32_t>(req.size()) || req.read(socket) < 0) { \
            jack_error("CheckRead error");                                                       \
            return -1;                                                                           \
        }                                                                                        \
    }
#define CheckWriteName(error, socket)                                  \
    {                                                                  \
        if (sendJackReply(socket, res, header) < 0) {                  \
            jack_error("%s write error name = %s", error, req.m_name); \
        }                                                              \
    }
#define CheckWriteRefNum(error, socket)                                 \
    {                                                                   \
        if (sendJackReply(socket, res, header) < 0) {                   \
            jack_error("%s write error ref = %d", error, req.m_refNum); \
        }                                                               \
    }
#define CheckWrite(error, socket)                     \
    {                                                 \
        if (sendJackReply(socket, res, header) < 0) { \
            jack_error("%s write error", error);      \
        }                                             \
    }

RequestDecoder::RequestDecoder(ServerCore* server, ClientHandlerInterface* handler)
    : m_server(server)
    , m_handler(handler)
{
}

RequestDecoder::~RequestDecoder()
{
}

std::int32_t RequestDecoder::refuse(detail::ChannelTransactionInterface* socket, const MsgHeader& header)
{
    jack_error("JACK protocol mismatch: request version %d, server version %d", header.m_version, kJackMsgVersion);
    Reply res(kJackMsgProtocolError);
    return sendJackReply(socket, res, header);
}

std::int32_t RequestDecoder::handleRequest(detail::ChannelTransactionInterface* socket, const MsgHeader& header)
{
    if (header.m_version != kJackMsgVersion) {
        refuse(socket, header);
        return -1;
    }

    Request::RequestType type = (Request::RequestType)header.m_type;

    // Read data
    switch (type) {

    case Request::kClientCheck: {
        jack_log("JackRequest::ClientCheck");
        ClientCheckRequest req;
        ClientCheckResult res;
        CheckRead(req, socket);
        res.m_result = replyCode(m_server->getEngine()->clientCheck(req.m_name.data(), req.m_uUID, res.m_name.data(), req.m_protocol, req.m_options, &res.m_status));
        CheckWriteName("JackRequest::ClientCheck", socket);
        // Atomic ClientCheck followed by ClientOpen on same socket
        if (req.m_open) {
            MsgHeader next;
            if (readJackMsgHeader(socket, next) < 0) {
                return -1;
            }
            return handleRequest(socket, next);
        }
        break;
    }

    case Request::kClientOpen: {
        jack_log("JackRequest::ClientOpen");
        ClientOpenRequest req;
        ClientOpenResult res;
        CheckRead(req, socket);
        m_handler->clientAdd(socket, &req, &res);
        CheckWriteName("JackRequest::ClientOpen", socket);
        break;
    }

    case Request::kClientClose: {
        jack_log("JackRequest::ClientClose");
        ClientCloseRequest req;
        Reply res;
        CheckRead(req, socket);
        res.m_result = replyCode(m_server->getEngine()->clientExternalClose(req.m_refNum));
        CheckWriteRefNum("JackRequest::ClientClose", socket);
        m_handler->clientRemove(socket, req.m_refNum);
        // Will cause the wrapping thread to stop
        return -1;
    }

    case Request::kActivateClient: {
        ActivateRequest req;
        Reply res;
        jack_log("JackRequest::ActivateClient");
        CheckRead(req, socket);
        res.m_result = replyCode(m_server->getEngine()->clientActivate(req.m_refNum, req.m_isRealTime, m_server->isRunning()));
        CheckWriteRefNum("JackRequest::ActivateClient", socket);
        break;
    }

    case Request::kDeactivateClient: {
        jack_log("JackRequest::DeactivateClient");
        DeactivateRequest req;
        Reply res;
        CheckRead(req, socket);
        res.m_result = replyCode(m_server->getEngine()->clientDeactivate(req.m_refNum, m_server->isRunning()));
        CheckWriteRefNum("JackRequest::DeactivateClient", socket);
        break;
    }

    case Request::kRegisterPort: {
        jack_log("JackRequest::RegisterPort");
        PortRegisterRequest req;
        PortRegisterResult res;
        CheckRead(req, socket);
        res.m_result = replyCode(m_server->getEngine()->portRegister(req.m_refNum, req.m_name.data(), req.m_portType.data(), req.m_flags, req.m_bufferSize, &res.m_portIndex));
        CheckWriteRefNum("JackRequest::RegisterPort", socket);
        break;
    }

    case Request::kUnRegisterPort: {
        jack_log("JackRequest::UnRegisterPort");
        PortUnRegisterRequest req;
        Reply res;
        CheckRead(req, socket);
        res.m_result = replyCode(m_server->getEngine()->portUnRegister(req.m_refNum, req.m_portIndex));
        CheckWriteRefNum("JackRequest::UnRegisterPort", socket);
        break;
    }

    case Request::kConnectNamePorts: {
        jack_log("JackRequest::ConnectNamePorts");
        PortConnectNameRequest req;
        Reply res;
        CheckRead(req, socket);
        res.m_result = replyCode(m_server->getEngine()->portConnect(req.m_refNum, req.m_src.data(), req.m_dst.data()));
        CheckWriteRefNum("JackRequest::ConnectNamePorts", socket);
        break;
    }

    case Request::kDisconnectNamePorts: {
        jack_log("JackRequest::DisconnectNamePorts");
        PortDisconnectNameRequest req;
        Reply res;
        CheckRead(req, socket);
        res.m_result = replyCode(m_server->getEngine()->portDisconnect(req.m_refNum, req.m_src.data(), req.m_dst.data()));
        CheckWriteRefNum("JackRequest::DisconnectNamePorts", socket);
        break;
    }

    case Request::kConnectPorts: {
        jack_log("JackRequest::ConnectPorts");
        PortConnectRequest req;
        Reply res;
        CheckRead(req, socket);
        res.m_result = replyCode(m_server->getEngine()->portConnect(req.m_refNum, req.m_src, req.m_dst));
        CheckWriteRefNum("JackRequest::ConnectPorts", socket);
        break;
    }

    case Request::kDisconnectPorts: {
        jack_log("JackRequest::DisconnectPorts");
        PortDisconnectRequest req;
        Reply res;
        CheckRead(req, socket);
        res.m_result = replyCode(m_server->getEngine()->portDisconnect(req.m_refNum, req.m_src, req.m_dst));
        CheckWriteRefNum("JackRequest::DisconnectPorts", socket);
        break;
    }

    case Request::kPortRename: {
        jack_log("JackRequest::PortRename");
        PortRenameRequest req;
        Reply res;
        CheckRead(req, socket);
        res.m_result = replyCode(m_server->getEngine()->portRename(req.m_refNum, req.m_port, req.m_name.data()));
        CheckWriteRefNum("JackRequest::PortRename", socket);
        break;
    }

    case Request::kSetBufferSize: {
        jack_log("JackRequest::SetBufferSize");
        SetBufferSizeRequest req;
        Reply res;
        CheckRead(req, socket);
        res.m_result = replyCode(m_server->setBufferSize(req.m_bufferSize));
        CheckWrite("JackRequest::SetBufferSize", socket);
        break;
    }

    case Request::kSetFreeWheel: {
        jack_log("JackRequest::SetFreeWheel");
        SetFreeWheelRequest req;
        Reply res;
        CheckRead(req, socket);
        res.m_result = replyCode(m_server->setFreewheel(req.m_onOff));
        CheckWrite("JackRequest::SetFreeWheel", socket);
        break;
    }

    case Request::kComputeTotalLatencies: {
        jack_log("JackRequest::ComputeTotalLatencies");
        ComputeTotalLatenciesRequest req;
        Reply res;
        CheckRead(req, socket);
        res.m_result = replyCode(m_server->getEngine()->computeTotalLatencies());
        CheckWrite("JackRequest::ComputeTotalLatencies", socket);
        break;
    }

    case Request::kReleaseTimebase: {
        jack_log("JackRequest::ReleaseTimebase");
        ReleaseTimebaseRequest req;
        Reply res;
        CheckRead(req, socket);
        res.m_result = replyCode(m_server->releaseTimebase(req.m_refNum));
        CheckWriteRefNum("JackRequest::ReleaseTimebase", socket);
        break;
    }

    case Request::kSetTimebaseCallback: {
        jack_log("JackRequest::SetTimebaseCallback");
        SetTimebaseCallbackRequest req;
        Reply res;
        CheckRead(req, socket);
        res.m_result = replyCode(m_server->setTimebaseCallback(req.m_refNum, req.m_conditionnal));
        CheckWriteRefNum("JackRequest::SetTimebaseCallback", socket);
        break;
    }


    case Request::kNotification: {
        jack_log("JackRequest::Notification");
        ClientNotificationRequest req;
        CheckRead(req, socket);
        if (req.m_notify == kQUIT) {
            jack_log("JackRequest::Notification kQUIT");
            throw QuitException();
        } else {
            m_server->notify(req.m_refNum, req.m_notify, req.m_value);
        }
        break;
    }


    case Request::kGetClientByUUID: {
        jack_log("JackRequest::GetClientByUUID");
        GetClientNameRequest req;
        ClientNameResult res;
        CheckRead(req, socket);
        res.m_result = replyCode(m_server->getEngine()->getClientNameForUUID(req.m_uUID.data(), res.m_name.data()));
        CheckWrite("JackRequest::GetClientByUUID", socket);
        break;
    }

    case Request::kGetUUIDByClient: {
        jack_log("JackRequest::GetUUIDByClient");
        GetUUIDRequest req;
        UUIDResult res;
        CheckRead(req, socket);
        res.m_result = replyCode(m_server->getEngine()->getUUIDForClientName(req.m_name.data(), res.m_uUID.data()));
        CheckWrite("JackRequest::GetUUIDByClient", socket);
        break;
    }

    case Request::kReserveClientName: {
        jack_log("JackRequest::ReserveClientName");
        ReserveNameRequest req;
        Reply res;
        CheckRead(req, socket);
        res.m_result = replyCode(m_server->getEngine()->reserveClientName(req.m_name.data(), req.m_uUID.data()));
        CheckWrite("JackRequest::ReserveClientName", socket);
        break;
    }


    default:
        jack_error("Unknown request %d", header.m_type);
        return -1;
    }

    return 0;
}

} // namespace jackmini
