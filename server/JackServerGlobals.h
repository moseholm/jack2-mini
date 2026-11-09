/*
Copyright (C) 2005 Grame

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

#ifndef __JackServerGlobals__
#define __JackServerGlobals__

#include "JackCompilerDeps.h"

namespace jackmini {

class JackServer;

/*!
\brief The server instance and the device reservation hooks.  The hooks are set by the control API.
*/
struct SERVER_EXPORT JackServerGlobals {
    static JackServer* fInstance;

    static bool (*on_device_acquire)(const char* device_name);
    static void (*on_device_release)(const char* device_name);
    static void (*on_device_reservation_loop)(void);
};

} // namespace jackmini

#endif
