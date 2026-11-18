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

// The OS primitives shared by the client and the server.  This is the portable
// part of the old PlatformPlug.h; the socket aliases that only the server uses
// are in server/ipc/ServerChannels.h.

#include "ipc/sync/LinuxFutex.h"
#include "common/os/PosixMutex.h"
#include "common/os/PosixProcessSync.h"
#include "common/os/PosixThread.h"

namespace jackmini {

using Mutex = PosixMutex;
using Thread = PosixThread;
using Synchro = LinuxFutex;
using ProcessSync = PosixProcessSync;

} // namespace jackmini
