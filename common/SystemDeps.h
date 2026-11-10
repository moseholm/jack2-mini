// Copyright (C) 2004-2006 Grame
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

#pragma once

#include "systemdeps.h"

#include <dlfcn.h>
#include <inttypes.h>
#include <signal.h>
#include <sys/types.h>

#ifndef UINT32_MAX
#define UINT32_MAX 4294967295U
#endif

using DriverHandle = void*;
inline DriverHandle loadDriverModule(const char* name)
{
    return dlopen(name, RTLD_NOW | RTLD_GLOBAL);
}

inline int unloadDriverModule(DriverHandle handle)
{
    return dlclose(handle);
}

inline void* getDriverProc(DriverHandle handle, const char* name)
{
    return dlsym(handle, name);
}

inline DriverHandle loadJackModule(const char* name)
{
    return dlopen(name, RTLD_NOW | RTLD_LOCAL);
}

inline int unloadJackModule(DriverHandle handle)
{
    return dlclose(handle);
}

inline void* getJackProc(DriverHandle handle, const char* name)
{
    return dlsym(handle, name);
}
