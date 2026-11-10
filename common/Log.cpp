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

#include "Log.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <array>
#include <cstdint>

static void formatAndLog(std::int32_t level, const char* prefix, const char* fmt, va_list ap)
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
    gJackLogRouter(level, buffer.data());
}

void logMessage(std::int32_t level, const char* prefix, const char* fmt, va_list ap)
{
    formatAndLog(level, prefix, fmt, ap);
}

void jack_error(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    formatAndLog(kLogLevelError, nullptr, fmt, ap);
    va_end(ap);
}

void jack_info(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    formatAndLog(kLogLevelInfo, nullptr, fmt, ap);
    va_end(ap);
}

void logDirect(std::int32_t level, const char* message)
{
    void (*logCallback)(const char*);

    switch (level) {
    case kLogLevelInfo:
        logCallback = gJackInfoCallback;
        break;
    case kLogLevelError:
        logCallback = gJackErrorCallback;
        break;
    default:
        return;
    }

    logCallback(message);
}

LogFunction gJackLogRouter = &logDirect;

void defaultJackErrorCallback(const char* desc)
{
    fprintf(stderr, "%s\n", desc);
    fflush(stderr);
}

void defaultJackInfoCallback(const char* desc)
{
    fprintf(stdout, "%s\n", desc);
    fflush(stdout);
}

void silentJackErrorCallback(const char* desc)
{
}

void silentJackInfoCallback(const char* desc)
{
}

void (*gJackErrorCallback)(const char* desc) = &defaultJackErrorCallback;
void (*gJackInfoCallback)(const char* desc) = &defaultJackInfoCallback;
