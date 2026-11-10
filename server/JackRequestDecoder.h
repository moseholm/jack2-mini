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

#include "JackChannel.h"
#include "JackMessage.h"
#include "jackmini/error.hpp"

#include <expected>

namespace jackmini {

class JackServer;

// The protocol replies 0 on success and -1 on failure. The reason is logged where it was detected.
inline int ReplyCode(const Result<void>& result)
{
    return result ? 0 : -1;
}

struct JackClientOpenRequest;
struct JackClientOpenResult;

struct JackClientHandlerInterface {

    virtual void ClientAdd(detail::JackChannelTransactionInterface* socket, JackClientOpenRequest* req, JackClientOpenResult* res) = 0;
    virtual void ClientRemove(detail::JackChannelTransactionInterface* socket, int refnum) = 0;

    virtual ~JackClientHandlerInterface()
    {
    }
};

// \brief Request decoder

class JackRequestDecoder {
private:
    JackServer* fServer;
    JackClientHandlerInterface* fHandler;

public:
    JackRequestDecoder(JackServer* server, JackClientHandlerInterface* handler);
    virtual ~JackRequestDecoder();

    int HandleRequest(detail::JackChannelTransactionInterface* socket, const JackMsgHeader& header);
    // Refuses a message of another protocol version: the reply carries the protocol error.
    int Refuse(detail::JackChannelTransactionInterface* socket, const JackMsgHeader& header);
};

} // namespace jackmini
