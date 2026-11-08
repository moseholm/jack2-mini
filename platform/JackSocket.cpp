/*
Copyright (C) 2004-2008 Grame

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

#include "JackSocket.h"
#include "JackConstants.h"
#include "JackTools.h"
#include "JackError.h"
#include "promiscuous.h"
#include "JackCString.h"
#include <array>
#include <format>
#include <span>
#include <string.h>
#include <pthread.h>
#include <fcntl.h>

namespace Jack
{

static void BuildName(const char* client_name, char* res, const char* dir, int which, int size, bool promiscuous)
{
    std::array<char, SYNC_MAX_NAME_SIZE + 1> ext_client_name;
    JackTools::RewriteName(client_name, ext_client_name.data());
    const std::string_view ext_name = ext_client_name.data();
    const auto end = promiscuous
        ? std::format_to_n(res, size - 1, "{}/jack_{}_{}", dir, ext_name, which)
        : std::format_to_n(res, size - 1, "{}/jack_{}_{}_{}", dir, ext_name, JackTools::GetUID(), which);
    *end.out = '\0';
}

JackClientSocket::JackClientSocket(): JackClientRequestInterface(), fSocket(-1), fTimeOut(0), fNonBlocking(false)
{
    const char* promiscuous = getenv("JACK_PROMISCUOUS_SERVER");
    fPromiscuous = (promiscuous != NULL);
    fPromiscuousGid = jack_group2gid(promiscuous);
}

JackClientSocket::JackClientSocket(int socket): JackClientRequestInterface(), fSocket(socket),fTimeOut(0), fPromiscuous(false), fPromiscuousGid(-1)
{}

void JackClientSocket::SetReadTimeOut(long sec)
{
    struct timeval timout;
    timout.tv_sec = sec;
    timout.tv_usec = 0;
    if (setsockopt(fSocket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timout, sizeof(timeval)) < 0) {
        jack_error("SetReadTimeOut fd = %ld err = %s", fSocket, strerror(errno));
    }
}

void JackClientSocket::SetWriteTimeOut(long sec)
{
    struct timeval timout;
    timout.tv_sec = sec ;
    timout.tv_usec = 0;
    if (setsockopt(fSocket, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timout, sizeof(timeval)) < 0) {
        jack_error("SetWriteTimeOut fd = %ld err = %s", fSocket, strerror(errno));
    }
}

void JackClientSocket::SetNonBlocking(bool onoff)
{
    if (onoff) {
        fNonBlocking = true;
        long flags = 0;
        if (fcntl(fSocket, F_SETFL, flags | O_NONBLOCK) < 0) {
            jack_error("SetNonBlocking fd = %ld err = %s", fSocket, strerror(errno));
        }
    }
}

int JackClientSocket::Connect(const char* dir, const char* name, int which) // A revoir : utilisation de "which"
{
    struct sockaddr_un addr;

    if ((fSocket = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
        jack_error("Cannot create socket err = %s", strerror(errno));
        return -1;
    }

    addr.sun_family = AF_UNIX;
    BuildName(name, addr.sun_path, dir, which, sizeof(addr.sun_path), fPromiscuous);
    jack_log("JackClientSocket::Connect : addr.sun_path %s", addr.sun_path);

    if (connect(fSocket, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        jack_error("Cannot connect to server socket err = %s", strerror(errno));
        close(fSocket);
        fSocket = -1;
        return -1;
    }

    return 0;
}

int JackClientSocket::Close()
{
    jack_log("JackClientSocket::Close");
    if (fSocket > 0) {
        shutdown(fSocket, SHUT_RDWR);
        close(fSocket);
        fSocket = -1;
        return 0;
    } else {
        return -1;
    }
}

int JackClientSocket::Read(void* data, int len)
{
    int res;
    uint8_t* udata = (uint8_t*)data;


    while (len != 0 && (res = read(fSocket, udata, len)) != len) {
        if (res <= 0) {
            if (errno == EWOULDBLOCK || errno == EAGAIN) {
                if (fNonBlocking) {
                    jack_log("JackClientSocket::Read time out on non-blocking socket fd = %d", fSocket);
                    // For a non blocking socket, a read failure is not considered as an error
                    memset(data, 0, len);
                    return 0;
                }
                jack_error("JackClientSocket::Read time out on blocking socket fd = %d", fSocket);
                return -1;
            }
            if (errno == 0 || errno == ENOTCONN) {
                // aborted reading due to shutdown
                return JACK_REQUEST_ERR_ABORTED;
            }
            jack_error("Cannot read socket fd = %d err = %s", fSocket, strerror(errno));
            return -1;
        }
        udata += res;
        len -= res;
    }

    return 0;
}

int JackClientSocket::Write(void* data, int len)
{
    int res;
    uint8_t* udata = (uint8_t*)data;


    while (len != 0 && (res = write(fSocket, udata, len)) != len) {
        if (res <= 0) {
            if (errno == EWOULDBLOCK || errno == EAGAIN) {
                if (fNonBlocking) {
                    // For a non blocking socket, a write failure is not considered as an error
                    jack_log("JackClientSocket::Write time out on non-blocking socket fd = %d", fSocket);
                    return 0;
                }
                jack_error("JackClientSocket::Write time out on blocking socket fd = %d", fSocket);
                return -1;
            }
            if (errno == 0 || errno == ENOTCONN) {
                // aborted reading due to shutdown
                return JACK_REQUEST_ERR_ABORTED;
            }
            jack_error("Cannot write socket fd = %ld err = %s", fSocket, strerror(errno));
            return -1;
        }
        udata += res;
        len -= res;
    }

    return 0;
}

int JackClientSocket::WriteWithFds(void* data, int len, std::span<const int> fds)
{
    const std::span<const std::byte> bytes(static_cast<const std::byte*>(data), len);
    const auto sent = SendWithFds(fSocket, bytes, fds);
    if (!sent) {
        // A full non-blocking socket drops the message, as Write() does: the notifications sent from the RT thread
        // are not worth blocking for.
        if (sent.error() == std::error_code(EAGAIN, std::system_category()) && fNonBlocking) {
            jack_log("JackClientSocket::WriteWithFds dropped on a full non-blocking socket fd = %d", fSocket);
            return 0;
        }
        jack_error("JackClientSocket::WriteWithFds fd = %d err = %s", fSocket, sent.error().message().c_str());
        return -1;
    }
    return 0;
}

int JackClientSocket::ReadWithFds(void* data, int len, std::vector<UniqueFd>& fds)
{
    const std::span<std::byte> bytes(static_cast<std::byte*>(data), len);
    const auto got = RecvWithFds(fSocket, bytes, fds);
    if (!got) {
        jack_error("JackClientSocket::ReadWithFds fd = %d err = %s", fSocket, got.error().message().c_str());
        return -1;
    }
    if (*got == 0) {
        // The peer closed the connection.
        return JACK_REQUEST_ERR_ABORTED;
    }
    // The descriptors arrived with the first bytes; the rest is plain data.
    return Read(static_cast<uint8_t*>(data) + *got, len - static_cast<int>(*got));
}

JackServerSocket::JackServerSocket(): fSocket( -1)
{
    const char* promiscuous = getenv("JACK_PROMISCUOUS_SERVER");
    fPromiscuous = (promiscuous != NULL);
    fPromiscuousGid = jack_group2gid(promiscuous);
}

int JackServerSocket::Bind(const char* dir, const char* name, int which) // A revoir : utilisation de "which"
{
    struct sockaddr_un addr;

    if ((fSocket = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
        jack_error("Cannot create server socket err = %s", strerror(errno));
        return -1;
    }

    addr.sun_family = AF_UNIX;
    // Socket name has to be kept in fName to be "unlinked".
    BuildName(name, fName, dir, which, sizeof(addr.sun_path), fPromiscuous);
    CopyCString(addr.sun_path, fName);
   
    jack_log("JackServerSocket::Bind : addr.sun_path %s", addr.sun_path);
    unlink(fName); // Security...

    if (bind(fSocket, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        jack_error("Cannot bind server to socket err = %s", strerror(errno));
        goto error;
    }

    if (listen(fSocket, 100) < 0) {
        jack_error("Cannot enable listen on server socket err = %s", strerror(errno));
        goto error;
    }

    if (fPromiscuous && (jack_promiscuous_perms(-1, fName, fPromiscuousGid) < 0))
        goto error;

    return 0;

error:
    unlink(fName);
    close(fSocket);
    fSocket = -1;
    return -1;
}

JackClientSocket* JackServerSocket::Accept()
{
    struct sockaddr_un client_addr{};
    socklen_t client_addrlen;

    client_addrlen = sizeof(client_addr);

    int fd = accept(fSocket, (struct sockaddr*)&client_addr, &client_addrlen);
    if (fd < 0) {
        jack_error("Cannot accept new connection err = %s", strerror(errno));
        return 0;
    } else {
        return new JackClientSocket(fd);
    }
}

int JackServerSocket::Close()
{
    if (fSocket > 0) {
        jack_log("JackServerSocket::Close %s", fName);
        shutdown(fSocket, SHUT_RDWR);
        close(fSocket);
        unlink(fName);
        fSocket = -1;
        return 0;
    } else {
        return -1;
    }
}

} // end of namespace


