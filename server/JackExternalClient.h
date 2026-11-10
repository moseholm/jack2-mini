// Copyright (C) 2001 Paul Davis
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

#pragma once

#include <cstdint>

#include "JackClientInterface.h"
#include "JackPlatformPlug.h"
#include "jackmini/error.hpp"

namespace jackmini {

struct ClientControl;

// \brief Server side implementation of library clients.

class ExternalClient : public ClientInterface {

private:
    NotifyChannel m_channel;  // Server/client communication channel
    ClientControl* m_clientControl;  // Client control in shared memory

public:
    ExternalClient();
    virtual ~ExternalClient();

    // The shared client control is allocated first; the notification channel is opened only once the driver is
    // known to be running, so a refused open does not create a writer thread.
    Result<void> openClient(const char* name, std::int32_t pid, std::int32_t refnum, UuidC uuid, ShmSegment* sharedClient);
    Result<void> openChannel(const char* name);
    std::int32_t close();

    std::int32_t clientNotify(std::int32_t refnum, const char* name, std::int32_t notify, std::int32_t sync, const char* message, std::int32_t value1, std::int32_t value2);

    ClientControl* getClientControl() const;
};


} // namespace jackmini
