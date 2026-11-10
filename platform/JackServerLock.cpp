// Copyright (C) 2026 JACK contributors
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

#include "JackServerLock.h"

#include "JackFdSegment.h"
#include "JackTools.h"

#include <fcntl.h>
#include <sys/file.h>
#include <cerrno>
#include <format>
#include <string>

namespace jackmini {

namespace {

// The descriptor that holds the lock.  Closing it releases the lock.
UniqueFd gLockFd;

std::error_code LastError()
{
    return std::error_code(errno, std::system_category());
}

} // namespace

std::expected<void, std::error_code> AcquireServerLock(const char* dir)
{
    const std::string lock_path = std::format("{}/jack_{}_server.lock", dir, JackTools::GetUID());

    // O_CLOEXEC keeps the descriptor out of any program the server starts, so the lock ends with the server.
    UniqueFd fd(open(lock_path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0666));
    if (!fd.valid()) {
        return std::unexpected(LastError());
    }

    if (flock(fd.get(), LOCK_EX | LOCK_NB) != 0) {
        if (errno == EWOULDBLOCK) {
            return std::unexpected(std::make_error_code(std::errc::file_exists));
        }
        return std::unexpected(LastError());
    }

    gLockFd = std::move(fd);
    return { };
}

void ReleaseServerLock() noexcept
{
    gLockFd.reset();
}

} // namespace jackmini
