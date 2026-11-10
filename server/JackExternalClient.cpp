#include <cstdint>

// Copyright (C) 2001-2003 Paul Davis
// Copyright (C) 2004-2008 Grame
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

#include "JackExternalClient.h"
#include "JackChannel.h"
#include "JackClientControl.h"
#include "JackError.h"
#include "JackGlobals.h"

namespace jackmini {

JackExternalClient::JackExternalClient()
    : fClientControl(nullptr)
{
}

JackExternalClient::~JackExternalClient()
{
}

std::int32_t JackExternalClient::ClientNotify(std::int32_t refnum, const char* name, std::int32_t notify, std::int32_t sync, const char* message, std::int32_t value1, std::int32_t value2)
{
    std::int32_t result = -1;
    jack_log("JackExternalClient::ClientNotify ref = %ld client = %s name = %s notify = %ld", refnum, fClientControl->fName, name, notify);
    fChannel.ClientNotify(refnum, name, notify, sync, message, value1, value2, &result);
    return result;
}

Result<void> JackExternalClient::OpenClient(const char* name, std::int32_t pid, std::int32_t refnum, jack_uuid_t uuid, JackShmSegment* shared_client)
{
    try {

        // Use "placement new" to allocate object in shared memory
        JackShmMemAble* shared_mem = static_cast<JackShmMemAble*>(JackShmMem::operator new(sizeof(JackClientControl)));
        shared_mem->Init();
        const JackShmSegment segment { shared_mem->GetShmFd(), shared_mem->GetShmSize() };
        fClientControl = new (shared_mem) JackClientControl(name, pid, refnum, uuid);

        if (!fClientControl) {
            jack_error("Cannot allocate client shared memory segment");
            return std::unexpected(Error::ServerFailure);
        }

        *shared_client = segment;
        jack_log("JackExternalClient::Open name = %s fd = %d size = %u base = %p", name, segment.fFd, segment.fSize, shared_mem->GetShmAddress());
        return { };

    } catch (std::exception&) {
        return std::unexpected(Error::ServerFailure);
    }
}

Result<void> JackExternalClient::OpenChannel(const char* name)
{
    if (fChannel.Open(name) < 0) {
        jack_error("Cannot connect to client name = %s\n", name);
        return std::unexpected(Error::ServerFailure);
    }
    return { };
}

std::int32_t JackExternalClient::Close()
{
    jack_log("JackExternalClient::Close");
    fChannel.Close();
    if (fClientControl) {
        fClientControl->~JackClientControl();
        JackShmMem::operator delete(fClientControl);
    }
    return 0;
}

JackClientControl* JackExternalClient::GetClientControl() const
{
    return fClientControl;
}

} // namespace jackmini
