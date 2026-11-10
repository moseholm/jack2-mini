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

// The basic logging of the JACK libraries: the error and info messages and where they go.  It has no dependencies, so
// it is a library of its own (libjacklog).  The host (JackError) may route the messages of some threads elsewhere by
// setting jack_log_router.

#pragma once

#include "JackCompilerDeps.h"
#include <stdarg.h>


#define LOG_LEVEL_INFO 1
#define LOG_LEVEL_ERROR 2

typedef void (*jack_log_function_t)(int level, const char* message);

LIB_EXPORT void jack_error(const char* fmt, ...);
LIB_EXPORT void jack_info(const char* fmt, ...);

// Formats a message and passes it to jack_log_router.
LIB_EXPORT void jack_log_message(int level, const char* prefix, const char* fmt, va_list ap);

// Passes a message to the callbacks: the default delivery.
LIB_EXPORT void jack_log_direct(int level, const char* message);

// Where messages go.  Defaults to jack_log_direct.
LIB_EXPORT extern jack_log_function_t jack_log_router;

LIB_EXPORT extern void (*jack_error_callback)(const char* desc);
LIB_EXPORT extern void (*jack_info_callback)(const char* desc);

LIB_EXPORT extern void default_jack_error_callback(const char* desc);
LIB_EXPORT extern void default_jack_info_callback(const char* desc);

LIB_EXPORT void silent_jack_error_callback(const char* desc);
LIB_EXPORT void silent_jack_info_callback(const char* desc);
