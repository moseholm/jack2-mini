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

#pragma once

#include <cstddef>
#include <expected>
#include <span>
#include <system_error>
#include <vector>

namespace jackmini {

// A file descriptor that is closed when it goes out of scope.  Moving it
// transfers ownership; it is never copied.
class UniqueFd {
public:
    UniqueFd() noexcept = default;
    explicit UniqueFd(int fd) noexcept
        : fFd(fd)
    {
    }
    UniqueFd(UniqueFd&& other) noexcept
        : fFd(other.release())
    {
    }
    UniqueFd& operator=(UniqueFd&& other) noexcept;
    UniqueFd(const UniqueFd&) = delete;
    UniqueFd& operator=(const UniqueFd&) = delete;
    ~UniqueFd();

    int get() const noexcept { return fFd; }
    bool valid() const noexcept { return fFd >= 0; }
    int release() noexcept;
    void reset(int fd = -1) noexcept;

private:
    int fFd = -1;
};

// Sends a message and file descriptors on a connected Unix socket, in one
// sendmsg().  The descriptors are duplicated into the receiver.  The caller
// keeps its copies, which it closes when it no longer needs them.
std::expected<void, std::error_code> SendWithFds(int socket,
    std::span<const std::byte> data,
    std::span<const int> fds);

// Receives a message and any descriptors sent with it.  The descriptors are
// close-on-exec.  Returns the number of data bytes read; 0 means the peer
// closed the connection.
std::expected<std::size_t, std::error_code> RecvWithFds(int socket,
    std::span<std::byte> data,
    std::vector<UniqueFd>& fds);

// Creates an anonymous shared-memory segment of the given size.  It is not
// visible in any directory and disappears when its last descriptor closes.
std::expected<UniqueFd, std::error_code> CreateSegment(std::size_t size, const char* name);

// Maps a segment and returns its address.  The mapping is shared, so writes by
// one process are seen by the others.
std::expected<void*, std::error_code> MapSegment(int fd, std::size_t size);
void UnmapSegment(void* address, std::size_t size) noexcept;

} // namespace jackmini
