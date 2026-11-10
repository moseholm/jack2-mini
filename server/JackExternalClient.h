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

struct JackClientControl;

// \brief Server side implementation of library clients.

class JackExternalClient : public JackClientInterface {

private:
    JackNotifyChannel fChannel;  // Server/client communication channel
    JackClientControl* fClientControl;  // Client control in shared memory

public:
    JackExternalClient();
    virtual ~JackExternalClient();

    // The shared client control is allocated first; the notification channel is opened only once the driver is
    // known to be running, so a refused open does not create a writer thread.
    Result<void> OpenClient(const char* name, std::int32_t pid, std::int32_t refnum, jack_uuid_t uuid, JackShmSegment* shared_client);
    Result<void> OpenChannel(const char* name);
    std::int32_t Close();

    std::int32_t ClientNotify(std::int32_t refnum, const char* name, std::int32_t notify, std::int32_t sync, const char* message, std::int32_t value1, std::int32_t value2);

    JackClientControl* GetClientControl() const;
};


} // namespace jackmini
