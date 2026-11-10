// Copyright (C) 2001-2005 Paul Davis
// Copyright (C) 2004-2008 Grame
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

#include <cstdint>

#include "CompilerDeps.h"
#include "DriverInterface.h"
#include "PlatformPlug.h"

namespace jackmini {

class DriverClientInterface;
class LockedEngine;

namespace driver {

// The version of this interface. A module built for another version is refused when it is loaded.
inline constexpr std::uint32_t kModuleVersion = 1;

// The one symbol that every driver module exports: a pointer to its Module.
inline constexpr const char* kModuleSymbol = "jackmini_driver_module";

// Describes the driver: its name, its type, and its parameters. The descriptor is allocated by the module.
using DescribeFunction = DriverDesc* (*)();

// Creates the driver's backend on the engine, with the parameters given on the command line or by the control API.
// Returns nullptr on failure.
using InitializeFunction = DriverClientInterface* (*)(LockedEngine* engine, Synchro* table, const JSListC* params);

struct Module {
    std::uint32_t m_version;
    DescribeFunction m_describe;
    InitializeFunction m_initialize;
};

// The type of the exported function that returns the Module.
using ModuleFunction = const Module* (*)();

} // end of namespace driver

} // end of namespace jackmini

// Each driver module exports this function, which returns its Module.
extern "C" SERVER_EXPORT const jackmini::driver::Module* jackmini_driver_module();
