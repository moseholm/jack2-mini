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

#include <arpa/inet.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>
#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include "Channel.h"

namespace jackmini {

// \brief Client socket.

class ClientSocket : public detail::ClientRequestInterface {

private:
    std::int32_t m_socket = -1;
    std::int32_t m_timeOut = 0;
    bool m_nonBlocking = false; // the socket(int) constructor does not set it
    bool m_promiscuous = false;
    std::int32_t m_promiscuousGid = -1;

public:
    ClientSocket();
    ClientSocket(std::int32_t socket);

    std::int32_t connect(const char* dir, const char* name, std::int32_t which);
    std::int32_t close();
    std::int32_t read(void* data, std::int32_t len);
    std::int32_t write(void* data, std::int32_t len);
    std::int32_t readWithFds(void* data, std::int32_t len, std::vector<UniqueFd>& fds) override;
    std::int32_t writeWithFds(void* data, std::int32_t len, std::span<const std::int32_t> fds) override;
    std::int32_t getFd()
    {
        return m_socket;
    }
    void setReadTimeOut(std::int64_t sec);
    void setWriteTimeOut(std::int64_t sec);

    void setNonBlocking(bool onoff);
};

// \brief Server socket.

inline constexpr std::int32_t kSocketMaxNameSize = 256;


class ServerSocket {

private:
    std::int32_t m_socket;
    std::array<char, kSocketMaxNameSize> m_name;
    bool m_promiscuous;
    std::int32_t m_promiscuousGid;

public:
    ServerSocket();
    ~ServerSocket()
    {
    }

    std::int32_t bind(const char* dir, const char* name, std::int32_t which);
    ClientSocket* accept();
    std::int32_t close();
    std::int32_t getFd()
    {
        return m_socket;
    }
};

} // namespace jackmini
