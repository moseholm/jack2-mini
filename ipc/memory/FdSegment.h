// Copyright (C) 2026 JACK contributors
// Copyright (C) 2026 Mose Holm
//
// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 2.1 of the License, or (at your option) any later version.
//
// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public
// License along with this library; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

#pragma once

#include <cstddef>
#include <cstdint>
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
    explicit UniqueFd(std::int32_t fd) noexcept
        : m_fd(fd)
    {
    }
    UniqueFd(UniqueFd&& other) noexcept
        : m_fd(other.release())
    {
    }
    UniqueFd& operator=(UniqueFd&& other) noexcept;
    UniqueFd(const UniqueFd&) = delete;
    UniqueFd& operator=(const UniqueFd&) = delete;
    ~UniqueFd();

    std::int32_t get() const noexcept { return m_fd; }
    bool valid() const noexcept { return m_fd >= 0; }
    std::int32_t release() noexcept;
    void reset(std::int32_t fd = -1) noexcept;

private:
    std::int32_t m_fd = -1;
};

// Sends a message and file descriptors on a connected Unix socket, in one
// sendmsg().  The descriptors are duplicated into the receiver.  The caller
// keeps its copies, which it closes when it no longer needs them.
std::expected<void, std::error_code> sendWithFds(std::int32_t socket,
    std::span<const std::byte> data,
    std::span<const std::int32_t> fds);

// Receives a message and any descriptors sent with it.  The descriptors are
// close-on-exec.  Returns the number of data bytes read; 0 means the peer
// closed the connection.
std::expected<std::size_t, std::error_code> recvWithFds(std::int32_t socket,
    std::span<std::byte> data,
    std::vector<UniqueFd>& fds);

// Creates an anonymous shared-memory segment of the given size.  It is not
// visible in any directory and disappears when its last descriptor closes.
std::expected<UniqueFd, std::error_code> createSegment(std::size_t size, const char* name);

// Maps a segment and returns its address.  The mapping is shared, so writes by
// one process are seen by the others.
std::expected<void*, std::error_code> mapSegment(std::int32_t fd, std::size_t size);
void unmapSegment(void* address, std::size_t size) noexcept;

} // namespace jackmini
