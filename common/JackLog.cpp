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

#include "JackLog.h"
#include <cstdint>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <array>

static void jack_format_and_log(std::int32_t level, const char* prefix, const char* fmt, va_list ap)
{
    std::array<char, 256> buffer { };
    size_t len;

    if (prefix != nullptr) {
        len = strlen(prefix);
        assert(len < 256);
        memcpy(buffer.data(), prefix, len);
    } else {
        len = 0;
    }

    vsnprintf(buffer.data() + len, sizeof(buffer) - len, fmt, ap);
    jack_log_router(level, buffer.data());
}

void jack_log_message(std::int32_t level, const char* prefix, const char* fmt, va_list ap)
{
    jack_format_and_log(level, prefix, fmt, ap);
}

void jack_error(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    jack_format_and_log(kLogLevelError, nullptr, fmt, ap);
    va_end(ap);
}

void jack_info(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    jack_format_and_log(kLogLevelInfo, nullptr, fmt, ap);
    va_end(ap);
}

void jack_log_direct(std::int32_t level, const char* message)
{
    void (*log_callback)(const char*);

    switch (level) {
    case kLogLevelInfo:
        log_callback = jack_info_callback;
        break;
    case kLogLevelError:
        log_callback = jack_error_callback;
        break;
    default:
        return;
    }

    log_callback(message);
}

jack_log_function_t jack_log_router = &jack_log_direct;

void default_jack_error_callback(const char* desc)
{
    fprintf(stderr, "%s\n", desc);
    fflush(stderr);
}

void default_jack_info_callback(const char* desc)
{
    fprintf(stdout, "%s\n", desc);
    fflush(stdout);
}

void silent_jack_error_callback(const char* desc)
{
}

void silent_jack_info_callback(const char* desc)
{
}

void (*jack_error_callback)(const char* desc) = &default_jack_error_callback;
void (*jack_info_callback)(const char* desc) = &default_jack_info_callback;
