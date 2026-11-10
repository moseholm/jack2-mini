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

#include "Channel.h"
#include "ClientControl.h"
#include "Error.h"
#include "ExternalClient.h"
#include "Globals.h"

namespace jackmini {

ExternalClient::ExternalClient()
    : m_clientControl(nullptr)
{
}

ExternalClient::~ExternalClient()
{
}

std::int32_t ExternalClient::clientNotify(std::int32_t refnum, const char* name, std::int32_t notify, std::int32_t sync, const char* message, std::int32_t value1, std::int32_t value2)
{
    std::int32_t result = -1;
    jack_log("JackExternalClient::ClientNotify ref = %ld client = %s name = %s notify = %ld", refnum, m_clientControl->m_name, name, notify);
    m_channel.clientNotify(refnum, name, notify, sync, message, value1, value2, &result);
    return result;
}

Result<void> ExternalClient::openClient(const char* name, std::int32_t pid, std::int32_t refnum, UuidC uuid, ShmSegment* sharedClient)
{
    try {

        // Use "placement new" to allocate object in shared memory
        ShmMemAble* sharedMem = static_cast<ShmMemAble*>(ShmMem::operator new(sizeof(ClientControl)));
        sharedMem->init();
        const ShmSegment segment { sharedMem->getShmFd(), sharedMem->getShmSize() };
        m_clientControl = new (sharedMem) ClientControl(name, pid, refnum, uuid);

        if (!m_clientControl) {
            jack_error("Cannot allocate client shared memory segment");
            return std::unexpected(Error::kServerFailure);
        }

        *sharedClient = segment;
        jack_log("JackExternalClient::Open name = %s fd = %d size = %u base = %p", name, segment.m_fd, segment.m_size, sharedMem->getShmAddress());
        return { };

    } catch (std::exception&) {
        return std::unexpected(Error::kServerFailure);
    }
}

Result<void> ExternalClient::openChannel(const char* name)
{
    if (m_channel.open(name) < 0) {
        jack_error("Cannot connect to client name = %s\n", name);
        return std::unexpected(Error::kServerFailure);
    }
    return { };
}

std::int32_t ExternalClient::close()
{
    jack_log("JackExternalClient::Close");
    m_channel.close();
    if (m_clientControl) {
        m_clientControl->~ClientControl();
        ShmMem::operator delete(m_clientControl);
    }
    return 0;
}

ClientControl* ExternalClient::getClientControl() const
{
    return m_clientControl;
}

} // namespace jackmini
