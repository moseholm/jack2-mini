// Copyright (C) 2004-2008 Grame
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

#include "JackPlatformPlug.h"
#include "JackRequestDecoder.h"
#include "JackSocket.h"

#include <cstdint>
#include <poll.h>
#include <map>
#include <memory>
#include <vector>

namespace jackmini {

class ServerCore;

// \brief JackServerChannel using sockets.

class SocketServerChannel : public RunnableInterface, public ClientHandlerInterface {

private:
    ServerSocket m_requestListenSocket; // Socket to create request socket for the client
    Thread m_thread; // Thread to execute the event loop
    std::unique_ptr<RequestDecoder> m_decoder;
    ServerCore* m_server;

    std::vector<pollfd> m_pollTable;
    bool m_rebuild;
    std::map<std::int32_t, std::pair<std::int32_t, ClientSocket*>> m_socketTable;

    void buildPoolTable();

    void clientCreate();
    void clientKill(std::int32_t fd);

    void clientAdd(detail::ChannelTransactionInterface* socket, ClientOpenRequest* req, ClientOpenResult* res);
    void clientRemove(detail::ChannelTransactionInterface* socket, std::int32_t refnum);

    std::int32_t getFd(ClientSocket* socket);

public:
    SocketServerChannel();
    ~SocketServerChannel();

    Result<void> open(const char* serverName, ServerCore* server); // Open the Server/Client connection
    void close(); // Close the Server/Client connection

    Result<void> start();
    void stop();

    // JackRunnableInterface interface
    bool init();
    bool execute();
};

} // namespace jackmini
