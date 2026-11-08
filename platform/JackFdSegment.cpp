/*
Copyright (C) 2026 JACK contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include "JackFdSegment.h"

#include <array>
#include <cerrno>
#include <cstring>
#include <sys/mman.h>
#include <sys/socket.h>
#include <unistd.h>

namespace Jack
{

namespace
{

std::error_code LastError()
{
    return std::error_code(errno, std::system_category());
}

// Room for this many descriptors in one message.  More than the server ever sends.
constexpr std::size_t kMaxFds = 8;

} // namespace

UniqueFd& UniqueFd::operator=(UniqueFd&& other) noexcept
{
    if (this != &other) {
        reset(other.release());
    }
    return *this;
}

UniqueFd::~UniqueFd()
{
    reset();
}

int UniqueFd::release() noexcept
{
    const int fd = fFd;
    fFd = -1;
    return fd;
}

void UniqueFd::reset(int fd) noexcept
{
    if (fFd >= 0) {
        ::close(fFd);
    }
    fFd = fd;
}

std::expected<void, std::error_code> SendWithFds(int socket,
                                                 std::span<const std::byte> data,
                                                 std::span<const int> fds)
{
    if (fds.size() > kMaxFds) {
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    }

    // The descriptors travel with the first part; a stream socket may take the rest in later calls.
    std::size_t done = 0;
    while (done < data.size()) {
        iovec iov{};
        iov.iov_base = const_cast<std::byte*>(data.data() + done);
        iov.iov_len = data.size() - done;

        alignas(cmsghdr) std::array<char, CMSG_SPACE(kMaxFds * sizeof(int))> control{};
        msghdr msg{};
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
        if (done == 0 && !fds.empty()) {
            msg.msg_control = control.data();
            msg.msg_controllen = CMSG_SPACE(fds.size() * sizeof(int));
            cmsghdr* header = CMSG_FIRSTHDR(&msg);
            header->cmsg_level = SOL_SOCKET;
            header->cmsg_type = SCM_RIGHTS;
            header->cmsg_len = CMSG_LEN(fds.size() * sizeof(int));
            std::memcpy(CMSG_DATA(header), fds.data(), fds.size() * sizeof(int));
        }

        ssize_t sent;
        do {
            sent = sendmsg(socket, &msg, MSG_NOSIGNAL);
        } while (sent < 0 && errno == EINTR);
        if (sent < 0) {
            return std::unexpected(LastError());
        }
        done += static_cast<std::size_t>(sent);
    }
    return {};
}

std::expected<std::size_t, std::error_code> RecvWithFds(int socket,
                                                        std::span<std::byte> data,
                                                        std::vector<UniqueFd>& fds)
{
    iovec iov{};
    iov.iov_base = data.data();
    iov.iov_len = data.size();

    alignas(cmsghdr) std::array<char, CMSG_SPACE(kMaxFds * sizeof(int))> control{};
    msghdr msg{};
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = control.data();
    msg.msg_controllen = control.size();

    ssize_t got;
    do {
        got = recvmsg(socket, &msg, MSG_CMSG_CLOEXEC);
    } while (got < 0 && errno == EINTR);
    if (got < 0) {
        return std::unexpected(LastError());
    }

    for (cmsghdr* header = CMSG_FIRSTHDR(&msg); header != nullptr; header = CMSG_NXTHDR(&msg, header)) {
        if (header->cmsg_level == SOL_SOCKET && header->cmsg_type == SCM_RIGHTS) {
            const std::size_t count = (header->cmsg_len - CMSG_LEN(0)) / sizeof(int);
            int received[kMaxFds];
            std::memcpy(received, CMSG_DATA(header), count * sizeof(int));
            for (std::size_t i = 0; i < count; i++) {
                fds.emplace_back(received[i]);
            }
        }
    }
    return static_cast<std::size_t>(got);
}

std::expected<UniqueFd, std::error_code> CreateSegment(std::size_t size, const char* name)
{
    const int fd = memfd_create(name, MFD_CLOEXEC);
    if (fd < 0) {
        return std::unexpected(LastError());
    }
    UniqueFd segment(fd);
    if (ftruncate(fd, static_cast<off_t>(size)) != 0) {
        return std::unexpected(LastError());
    }
    return segment;
}

std::expected<void*, std::error_code> MapSegment(int fd, std::size_t size)
{
    void* address = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (address == MAP_FAILED) {
        return std::unexpected(LastError());
    }
    return address;
}

void UnmapSegment(void* address, std::size_t size) noexcept
{
    munmap(address, size);
}

} // namespace Jack
