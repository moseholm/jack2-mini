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

#pragma once

#include "Channel.h"
#include "Message.h"
#include "jackmini/Error.hpp"

#include <cstdint>
#include <expected>

namespace jackmini {

class ServerCore;

// The protocol replies 0 on success and -1 on failure. The reason is logged where it was detected.
inline std::int32_t replyCode(const Result<void>& result)
{
    return result ? 0 : -1;
}

struct ClientOpenRequest;
struct ClientOpenResult;

struct ClientHandlerInterface {

    virtual void clientAdd(detail::ChannelTransactionInterface* socket, ClientOpenRequest* req, ClientOpenResult* res) = 0;
    virtual void clientRemove(detail::ChannelTransactionInterface* socket, std::int32_t refnum) = 0;

    virtual ~ClientHandlerInterface()
    {
    }
};

// \brief Request decoder

class RequestDecoder {
private:
    ServerCore* m_server;
    ClientHandlerInterface* m_handler;

public:
    RequestDecoder(ServerCore* server, ClientHandlerInterface* handler);
    virtual ~RequestDecoder();

    std::int32_t handleRequest(detail::ChannelTransactionInterface* socket, const MsgHeader& header);
    // Refuses a message of another protocol version: the reply carries the protocol error.
    std::int32_t refuse(detail::ChannelTransactionInterface* socket, const MsgHeader& header);
};

} // namespace jackmini
