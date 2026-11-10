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

#include "ServerGlobals.h"
#include "Globals.h"
#include "ServerCore.h"

namespace jackmini {

ServerCore* ServerGlobals::s_instance = nullptr;

bool (*ServerGlobals::s_onDeviceAcquire)(const char* deviceName) = nullptr;
void (*ServerGlobals::s_onDeviceRelease)(const char* deviceName) = nullptr;
void (*ServerGlobals::s_onDeviceReservationLoop)(void) = nullptr;

// Used for the external C API (JackAPI.cpp)
SERVER_EXPORT GraphManager* getGraphManager()
{
    return ServerGlobals::s_instance->getGraphManager();
}

SERVER_EXPORT EngineControl* getEngineControl()
{
    return ServerGlobals::s_instance->getEngineControl();
}

SERVER_EXPORT Synchro* getSynchroTable()
{
    return ServerGlobals::s_instance->getSynchroTable();
}

} // namespace jackmini
