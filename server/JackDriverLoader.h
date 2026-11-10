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

#include "JackPlatformPlug.h"
#include "driver_interface.h"

jack_driver_desc_t* jack_find_driver_descriptor(JSList* drivers, const char* name);
JSList* jack_drivers_load(JSList* drivers);
void jack_print_driver_param_usage(jack_driver_desc_t* desc, std::uint64_t param, FILE* file);
void jack_print_driver_options(jack_driver_desc_t* desc, FILE* file);
