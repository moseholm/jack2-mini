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

#include "JackSocket.h"
#include "JackCString.h"
#include "JackConstants.h"
#include "JackError.h"
#include "JackTools.h"
#include "promiscuous.hpp"
#include <cstddef>
#include <cstdint>
#include <fcntl.h>
#include <pthread.h>
#include <string.h>
#include <array>
#include <format>
#include <span>

namespace jackmini {

static void buildName(const char* clientName, char* res, const char* dir, std::int32_t which, std::int32_t size, bool promiscuous)
{
    std::array<char, kSyncMaxNameSize + 1> extClientName;
    Tools::rewriteName(clientName, extClientName.data());
    const std::string_view extName = extClientName.data();
    const auto end = promiscuous
        ? std::format_to_n(res, size - 1, "{}/jack_{}_{}", dir, extName, which)
        : std::format_to_n(res, size - 1, "{}/jack_{}_{}_{}", dir, extName, Tools::getUID(), which);
    *end.out = '\0';
}

ClientSocket::ClientSocket()
    : ClientRequestInterface()
    , m_socket(-1)
    , m_timeOut(0)
    , m_nonBlocking(false)
{
    const char* promiscuous = getenv("JACK_PROMISCUOUS_SERVER");
    m_promiscuous = (promiscuous != nullptr);
    m_promiscuousGid = jackmini::groupToGid(promiscuous);
}

ClientSocket::ClientSocket(std::int32_t socket)
    : ClientRequestInterface()
    , m_socket(socket)
    , m_timeOut(0)
    , m_promiscuous(false)
    , m_promiscuousGid(-1)
{
}

void ClientSocket::setReadTimeOut(std::int64_t sec)
{
    struct timeval timout;
    timout.tv_sec = sec;
    timout.tv_usec = 0;
    if (setsockopt(m_socket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timout, sizeof(timeval)) < 0) {
        jack_error("SetReadTimeOut fd = %ld err = %s", m_socket, strerror(errno));
    }
}

void ClientSocket::setWriteTimeOut(std::int64_t sec)
{
    struct timeval timout;
    timout.tv_sec = sec;
    timout.tv_usec = 0;
    if (setsockopt(m_socket, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timout, sizeof(timeval)) < 0) {
        jack_error("SetWriteTimeOut fd = %ld err = %s", m_socket, strerror(errno));
    }
}

void ClientSocket::setNonBlocking(bool onoff)
{
    if (onoff) {
        m_nonBlocking = true;
        std::int64_t flags = 0;
        if (fcntl(m_socket, F_SETFL, flags | O_NONBLOCK) < 0) {
            jack_error("SetNonBlocking fd = %ld err = %s", m_socket, strerror(errno));
        }
    }
}

std::int32_t ClientSocket::connect(const char* dir, const char* name, std::int32_t which) // A revoir : utilisation de "which"
{
    struct sockaddr_un addr;

    if ((m_socket = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
        jack_error("Cannot create socket err = %s", strerror(errno));
        return -1;
    }

    addr.sun_family = AF_UNIX;
    buildName(name, addr.sun_path, dir, which, sizeof(addr.sun_path), m_promiscuous);
    jack_log("JackClientSocket::Connect : addr.sun_path %s", addr.sun_path);

    if (::connect(m_socket, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        // The caller reports the failure; a server that is not running is an ordinary answer.
        jack_log("JackClientSocket::Connect failed err = %s", strerror(errno));
        ::close(m_socket);
        m_socket = -1;
        return -1;
    }

    return 0;
}

std::int32_t ClientSocket::close()
{
    jack_log("JackClientSocket::Close");
    if (m_socket > 0) {
        shutdown(m_socket, SHUT_RDWR);
        ::close(m_socket);
        m_socket = -1;
        return 0;
    } else {
        return -1;
    }
}

std::int32_t ClientSocket::read(void* data, std::int32_t len)
{
    std::int32_t res;
    std::uint8_t* udata = (std::uint8_t*)data;


    while (len != 0 && (res = ::read(m_socket, udata, len)) != len) {
        if (res <= 0) {
            if (errno == EWOULDBLOCK || errno == EAGAIN) {
                if (m_nonBlocking) {
                    jack_log("JackClientSocket::Read time out on non-blocking socket fd = %d", m_socket);
                    // For a non blocking socket, a read failure is not considered as an error
                    memset(data, 0, len);
                    return 0;
                }
                jack_error("JackClientSocket::Read time out on blocking socket fd = %d", m_socket);
                return -1;
            }
            if (errno == 0 || errno == ENOTCONN) {
                // aborted reading due to shutdown
                return kRequestErrAborted;
            }
            jack_error("Cannot read socket fd = %d err = %s", m_socket, strerror(errno));
            return -1;
        }
        udata += res;
        len -= res;
    }

    return 0;
}

std::int32_t ClientSocket::write(void* data, std::int32_t len)
{
    std::int32_t res;
    std::uint8_t* udata = (std::uint8_t*)data;


    while (len != 0 && (res = ::write(m_socket, udata, len)) != len) {
        if (res <= 0) {
            if (errno == EWOULDBLOCK || errno == EAGAIN) {
                if (m_nonBlocking) {
                    // For a non blocking socket, a write failure is not considered as an error
                    jack_log("JackClientSocket::Write time out on non-blocking socket fd = %d", m_socket);
                    return 0;
                }
                jack_error("JackClientSocket::Write time out on blocking socket fd = %d", m_socket);
                return -1;
            }
            if (errno == 0 || errno == ENOTCONN) {
                // aborted reading due to shutdown
                return kRequestErrAborted;
            }
            jack_error("Cannot write socket fd = %ld err = %s", m_socket, strerror(errno));
            return -1;
        }
        udata += res;
        len -= res;
    }

    return 0;
}

std::int32_t ClientSocket::writeWithFds(void* data, std::int32_t len, std::span<const std::int32_t> fds)
{
    const std::span<const std::byte> bytes(static_cast<const std::byte*>(data), len);
    const auto sent = sendWithFds(m_socket, bytes, fds);
    if (!sent) {
        // A full non-blocking socket drops the message, as Write() does: the notifications sent from the RT thread
        // are not worth blocking for.
        if (sent.error() == std::error_code(EAGAIN, std::system_category()) && m_nonBlocking) {
            jack_log("JackClientSocket::WriteWithFds dropped on a full non-blocking socket fd = %d", m_socket);
            return 0;
        }
        jack_error("JackClientSocket::WriteWithFds fd = %d err = %s", m_socket, sent.error().message().c_str());
        return -1;
    }
    return 0;
}

std::int32_t ClientSocket::readWithFds(void* data, std::int32_t len, std::vector<UniqueFd>& fds)
{
    const std::span<std::byte> bytes(static_cast<std::byte*>(data), len);
    const auto got = recvWithFds(m_socket, bytes, fds);
    if (!got) {
        jack_error("JackClientSocket::ReadWithFds fd = %d err = %s", m_socket, got.error().message().c_str());
        return -1;
    }
    if (*got == 0) {
        // The peer closed the connection.
        return kRequestErrAborted;
    }
    // The descriptors arrived with the first bytes; the rest is plain data.
    return read(static_cast<std::uint8_t*>(data) + *got, len - static_cast<std::int32_t>(*got));
}

ServerSocket::ServerSocket()
    : m_socket(-1)
{
    const char* promiscuous = getenv("JACK_PROMISCUOUS_SERVER");
    m_promiscuous = (promiscuous != nullptr);
    m_promiscuousGid = jackmini::groupToGid(promiscuous);
}

std::int32_t ServerSocket::bind(const char* dir, const char* name, std::int32_t which) // A revoir : utilisation de "which"
{
    struct sockaddr_un addr;

    if ((m_socket = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
        jack_error("Cannot create server socket err = %s", strerror(errno));
        return -1;
    }

    addr.sun_family = AF_UNIX;
    // Socket name has to be kept in fName to be "unlinked".
    buildName(name, m_name.data(), dir, which, sizeof(addr.sun_path), m_promiscuous);
    copyCString(std::span<char>(addr.sun_path), m_name.data());

    jack_log("JackServerSocket::Bind : addr.sun_path %s", addr.sun_path);
    unlink(m_name.data()); // Security...

    if (::bind(m_socket, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        jack_error("Cannot bind server to socket err = %s", strerror(errno));
        goto error;
    }

    if (listen(m_socket, 100) < 0) {
        jack_error("Cannot enable listen on server socket err = %s", strerror(errno));
        goto error;
    }

    if (m_promiscuous && (jackmini::setPromiscuousPerms(-1, m_name.data(), m_promiscuousGid) < 0))
        goto error;

    return 0;

error:
    unlink(m_name.data());
    ::close(m_socket);
    m_socket = -1;
    return -1;
}

ClientSocket* ServerSocket::accept()
{
    struct sockaddr_un clientAddr { };
    socklen_t clientAddrlen;

    clientAddrlen = sizeof(clientAddr);

    std::int32_t fd = ::accept(m_socket, (struct sockaddr*)&clientAddr, &clientAddrlen);
    if (fd < 0) {
        jack_error("Cannot accept new connection err = %s", strerror(errno));
        return 0;
    } else {
        return new ClientSocket(fd);
    }
}

std::int32_t ServerSocket::close()
{
    if (m_socket > 0) {
        jack_log("JackServerSocket::Close %s", m_name);
        shutdown(m_socket, SHUT_RDWR);
        ::close(m_socket);
        unlink(m_name.data());
        m_socket = -1;
        return 0;
    } else {
        return -1;
    }
}

} // namespace jackmini
