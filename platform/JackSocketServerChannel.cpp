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

#include "JackSocketServerChannel.h"
#include "JackException.h"
#include "JackGlobals.h"
#include "JackLockedEngine.h"
#include "JackNotification.h"
#include "JackRequest.h"
#include "JackRequestDecoder.h"
#include "JackServer.h"
#include "JackTools.h"

#include <cstdint>
#include <assert.h>
#include <signal.h>

using namespace std;

namespace jackmini {

SocketServerChannel::SocketServerChannel()
    : m_thread(this)
{
    m_rebuild = true;
}

SocketServerChannel::~SocketServerChannel()
{
}

Result<void> SocketServerChannel::open(const char* serverName, ServerCore* server)
{
    jack_log("JackSocketServerChannel::Open");

    // Prepare request socket
    if (m_requestListenSocket.bind(kServerDir, serverName, 0) < 0) {
        jack_log("JackSocketServerChannel::Open : cannot create result listen socket");
        return std::unexpected(Error::kServerFailure);
    }

    // Prepare for poll
    buildPoolTable();

    m_decoder = std::make_unique<RequestDecoder>(server, this);
    m_server = server;
    return { };
}

void SocketServerChannel::close()
{
    m_requestListenSocket.close();

    // Close remaining client sockets
    std::map<std::int32_t, std::pair<std::int32_t, ClientSocket*>>::iterator it;

    for (it = m_socketTable.begin(); it != m_socketTable.end(); it++) {
        pair<std::int32_t, ClientSocket*> elem = (*it).second;
        ClientSocket* socket = elem.second;
        assert(socket);
        socket->close();
        delete socket;
    }

    m_decoder.reset();
}

Result<void> SocketServerChannel::start()
{
    if (m_thread.start() != 0) {
        jack_error("Cannot start Jack server listener");
        return std::unexpected(Error::kServerFailure);
    } else {
        return { };
    }
}

void SocketServerChannel::stop()
{
    m_thread.stop();
}

void SocketServerChannel::clientCreate()
{
    jack_log("JackSocketServerChannel::ClientCreate socket");
    ClientSocket* socket = m_requestListenSocket.accept();
    if (socket) {
        m_socketTable[socket->getFd()] = make_pair(-1, socket);
        m_rebuild = true;
    } else {
        jack_error("Client socket cannot be created");
    }
}

std::int32_t SocketServerChannel::getFd(ClientSocket* socketAux)
{
    std::map<std::int32_t, std::pair<std::int32_t, ClientSocket*>>::iterator it;

    for (it = m_socketTable.begin(); it != m_socketTable.end(); it++) {
        pair<std::int32_t, ClientSocket*> elem = (*it).second;
        ClientSocket* socket = elem.second;
        if (socketAux == socket) {
            return (*it).first;
        }
    }
    return -1;
}

void SocketServerChannel::clientAdd(detail::ChannelTransactionInterface* socketAux, ClientOpenRequest* req, ClientOpenResult* res)
{
    std::int32_t refnum = -1;
    res->m_result = replyCode(m_server->getEngine()->clientExternalOpen(req->m_name.data(), req->m_pID, req->m_uUID, &refnum, &res->m_sharedEngine, &res->m_sharedClient, &res->m_sharedGraph));
    if (res->m_result == 0) {
        ClientSocket* socket = dynamic_cast<ClientSocket*>(socketAux);
        assert(socket);
        std::int32_t fd = getFd(socket);
        assert(fd >= 0);
        m_socketTable[fd].first = refnum;
        m_rebuild = true;
        jack_log("JackSocketServerChannel::ClientAdd ref = %d fd = %d", refnum, fd);
    } else {
        jack_error("Cannot create new client");
    }
}

void SocketServerChannel::clientRemove(detail::ChannelTransactionInterface* socketAux, std::int32_t refnum)
{
    ClientSocket* socket = dynamic_cast<ClientSocket*>(socketAux);
    assert(socket);
    std::int32_t fd = getFd(socket);
    assert(fd >= 0);

    jack_log("JackSocketServerChannel::ClientRemove ref = %d fd = %d", refnum, fd);
    m_socketTable.erase(fd);
    socket->close();
    delete socket;
    m_rebuild = true;
}

void SocketServerChannel::clientKill(std::int32_t fd)
{
    pair<std::int32_t, ClientSocket*> elem = m_socketTable[fd];
    ClientSocket* socket = elem.second;
    std::int32_t refnum = elem.first;
    assert(socket);

    jack_log("JackSocketServerChannel::ClientKill ref = %d fd = %d", refnum, fd);
    if (refnum == -1) { // Should never happen... correspond to a client that started the socket but never opened...
        jack_log("Client was not opened : probably correspond to server_check");
    } else {
        m_server->getEngine()->clientKill(refnum);
    }

    m_socketTable.erase(fd);
    socket->close();
    delete socket;
    m_rebuild = true;
}

void SocketServerChannel::buildPoolTable()
{
    if (m_rebuild) {
        m_rebuild = false;
        m_pollTable.resize(m_socketTable.size() + 1);

        jack_log("JackSocketServerChannel::BuildPoolTable size = %d", m_socketTable.size() + 1);

        // First fd is the server request socket
        m_pollTable[0].fd = m_requestListenSocket.getFd();
        m_pollTable[0].events = POLLIN | POLLERR;

        // Next fd for clients
        map<std::int32_t, pair<std::int32_t, ClientSocket*>>::iterator it;
        std::int32_t i;

        for (i = 1, it = m_socketTable.begin(); it != m_socketTable.end(); it++, i++) {
            jack_log("JackSocketServerChannel::BuildPoolTable fSocketTable i = %ld fd = %ld", i, it->first);
            m_pollTable[i].fd = it->first;
            m_pollTable[i].events = POLLIN | POLLPRI | POLLERR | POLLHUP | POLLNVAL;
        }
    }
}

bool SocketServerChannel::init()
{
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGPIPE);
    pthread_sigmask(SIG_BLOCK, &set, 0);
    return true;
}

bool SocketServerChannel::execute()
{
    try {

        // Global poll
        if ((poll(m_pollTable.data(), m_socketTable.size() + 1, 10000) < 0) && (errno != EINTR)) {
            jack_error("JackSocketServerChannel::Execute : engine poll failed err = %s request thread quits...", strerror(errno));
            return false;
        } else {

            // Poll all clients
            for (std::uint32_t i = 1; i < m_socketTable.size() + 1; i++) {
                std::int32_t fd = m_pollTable[i].fd;
                if (m_pollTable[i].revents & ~POLLIN) {
                    jack_log("JackSocketServerChannel::Execute : poll client error err = %s", strerror(errno));
                    clientKill(fd);
                } else if (m_pollTable[i].revents & POLLIN) {
                    ClientSocket* socket = m_socketTable[fd].second;
                    // Decode header
                    MsgHeader header;
                    if (readJackMsgHeader(socket, header) < 0) {
                        jack_log("JackSocketServerChannel::Execute : cannot decode header");
                        clientKill(fd);
                        // A client of another protocol version is refused, and disconnected
                    } else if (header.m_version != kJackMsgVersion) {
                        m_decoder->refuse(socket, header);
                        clientKill(fd);
                        // Decode request
                    } else {
                        // Result is not needed here
                        m_decoder->handleRequest(socket, header);
                    }
                }
            }

            // Check the server request socket */
            if (m_pollTable[0].revents & POLLERR) {
                jack_error("Error on server request socket err = %s", strerror(errno));
            }

            if (m_pollTable[0].revents & POLLIN) {
                clientCreate();
            }
        }

        buildPoolTable();
        return true;

    } catch (QuitException& e) {
        jack_log("JackSocketServerChannel::Execute : JackQuitException");
        return false;
    }
}

} // namespace jackmini
