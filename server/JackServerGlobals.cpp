// Copyright (C) 2005 Grame
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

#include "JackServerGlobals.h"
#include "JackGlobals.h"
#include "JackServer.h"

namespace jackmini {

JackServer* JackServerGlobals::fInstance = nullptr;

bool (*JackServerGlobals::on_device_acquire)(const char* device_name) = nullptr;
void (*JackServerGlobals::on_device_release)(const char* device_name) = nullptr;
void (*JackServerGlobals::on_device_reservation_loop)(void) = nullptr;

// Used for the external C API (JackAPI.cpp)
SERVER_EXPORT JackGraphManager* GetGraphManager()
{
    return JackServerGlobals::fInstance->GetGraphManager();
}

SERVER_EXPORT JackEngineControl* GetEngineControl()
{
    return JackServerGlobals::fInstance->GetEngineControl();
}

SERVER_EXPORT JackSynchro* GetSynchroTable()
{
    return JackServerGlobals::fInstance->GetSynchroTable();
}

} // namespace jackmini
