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

#include "JackError.h"
#include "JackGlobals.h"
#include "JackMessageBuffer.h"
#include <cstdint>
#include <stdarg.h>
#include <stdio.h>

using namespace jackmini;

static bool change_thread_log_function(jack_log_function_t log_function)
{
    return (jack_tls_get(JackGlobals::fKeyLogFunction) == nullptr
        && jack_tls_set(JackGlobals::fKeyLogFunction, (void*)log_function));
}

SERVER_EXPORT std::int32_t set_threaded_log_function()
{
    return change_thread_log_function(JackMessageBufferAdd);
}

void jack_log_thread_route(std::int32_t level, const char* message)
{
    jack_log_function_t log_function = (jack_log_function_t)jack_tls_get(JackGlobals::fKeyLogFunction);

    // if log function is not overridden for thread, use the default one
    (log_function != nullptr ? log_function : jack_log_direct)(level, message);
}

// The messages of this library go through the thread routing.
static const bool log_router_installed = (jack_log_router = jack_log_thread_route, true);

SERVER_EXPORT void jack_log(const char* fmt, ...)
{
    if (JackGlobals::fVerbose) {
        va_list ap;
        va_start(ap, fmt);
        jack_log_message(LOG_LEVEL_INFO, "Jack: ", fmt, ap);
        va_end(ap);
    }
}
