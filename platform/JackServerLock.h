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

#ifndef __JackServerLock__
#define __JackServerLock__

#include <expected>
#include <system_error>

namespace Jack
{

// The single-instance rule of a server: one server per user.  It is an flock() on a file in dir, named after the user,
// whatever the server's name.  The kernel releases the lock when its holder exits, so a server that was killed does not
// block the next start.  The file is not removed on release: removing it would let a second server lock a new file
// while the first one still holds the old one.
//
// The lock belongs to the process.  Acquiring it while it is held fails with std::errc::file_exists.
std::expected<void, std::error_code> AcquireServerLock(const char* dir);
void ReleaseServerLock() noexcept;

} // namespace Jack

#endif
