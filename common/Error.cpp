// Copyright (C) 2001 Paul Davis
// Copyright (C) 2004-2008 Grame
// Copyright (C) 2008 Nedko Arnaudov
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

#include "Error.h"
#include "Globals.h"
#include "MessageBuffer.h"
#include <stdarg.h>
#include <stdio.h>
#include <cstdint>

using namespace jackmini;

static bool changeThreadLogFunction(LogFunction logFunction)
{
    return (tlsGet(Globals::s_keyLogFunction) == nullptr
        && tlsSet(Globals::s_keyLogFunction, (void*)logFunction));
}

SERVER_EXPORT std::int32_t setThreadedLogFunction()
{
    return changeThreadLogFunction(messageBufferAdd);
}

void logThreadRoute(std::int32_t level, const char* message)
{
    LogFunction logFunction = (LogFunction)tlsGet(Globals::s_keyLogFunction);

    // if log function is not overridden for thread, use the default one
    (logFunction != nullptr ? logFunction : logDirect)(level, message);
}

// The messages of this library go through the thread routing.
static const bool gLogRouterInstalled = (gJackLogRouter = logThreadRoute, true);

SERVER_EXPORT void jack_log(const char* fmt, ...)
{
    if (Globals::s_verbose) {
        va_list ap;
        va_start(ap, fmt);
        logMessage(kLogLevelInfo, "Jack: ", fmt, ap);
        va_end(ap);
    }
}
