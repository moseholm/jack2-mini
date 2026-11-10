// Copyright (C) 2001-2003 Paul Davis
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
#include "Types.h"

namespace jackmini {

SERVER_EXPORT void initTime();
SERVER_EXPORT void endTime();
SERVER_EXPORT TimeC getMicroseconds();
SERVER_EXPORT void sleepMicroseconds(std::int64_t usec);

void setClockSource(TimerType source);
const char* clockSourceName(TimerType source);

} // namespace jackmini
