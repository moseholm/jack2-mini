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

#include <cstdint>
#include <arpa/inet.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>
#include <span>
#include <vector>

#include "JackChannel.h"

namespace jackmini {

// \brief Client socket.

class JackClientSocket : public detail::JackClientRequestInterface {

private:
    std::int32_t fSocket = -1;
    std::int32_t fTimeOut = 0;
    bool fNonBlocking = false; // the socket(int) constructor does not set it
    bool fPromiscuous = false;
    std::int32_t fPromiscuousGid = -1;

public:
    JackClientSocket();
    JackClientSocket(std::int32_t socket);

    std::int32_t Connect(const char* dir, const char* name, std::int32_t which);
    std::int32_t Close();
    std::int32_t Read(void* data, std::int32_t len);
    std::int32_t Write(void* data, std::int32_t len);
    std::int32_t ReadWithFds(void* data, std::int32_t len, std::vector<UniqueFd>& fds) override;
    std::int32_t WriteWithFds(void* data, std::int32_t len, std::span<const std::int32_t> fds) override;
    std::int32_t GetFd()
    {
        return fSocket;
    }
    void SetReadTimeOut(std::int64_t sec);
    void SetWriteTimeOut(std::int64_t sec);

    void SetNonBlocking(bool onoff);
};

// \brief Server socket.

#define SOCKET_MAX_NAME_SIZE 256


class JackServerSocket {

private:
    std::int32_t fSocket;
    char fName[SOCKET_MAX_NAME_SIZE];
    bool fPromiscuous;
    std::int32_t fPromiscuousGid;

public:
    JackServerSocket();
    ~JackServerSocket()
    {
    }

    std::int32_t Bind(const char* dir, const char* name, std::int32_t which);
    JackClientSocket* Accept();
    std::int32_t Close();
    std::int32_t GetFd()
    {
        return fSocket;
    }
};

} // namespace jackmini
