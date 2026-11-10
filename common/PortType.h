// Copyright (C) 2007 Dmitry Baikov
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

#include "Constants.h"
#include "types.h"
#include <stddef.h>
#include <cstdint>

namespace jackmini {

extern const PortTypeIdC gPORTTYPESMAX;

struct PortType {
    const char* m_name;
    size_t (*m_size)();
    void (*m_init)(void* buffer, size_t bufferSize, NframesC nframes);
    void (*m_mixdown)(void* mixbuffer, void** srcBuffers, std::int32_t srcCount, NframesC nframes);
};

extern PortTypeIdC getPortTypeId(const char* portType);
extern const struct PortType* getPortType(PortTypeIdC portTypeId);

extern const struct PortType gAudioPortType;
extern const struct PortType gMidiPortType;

} // namespace jackmini
