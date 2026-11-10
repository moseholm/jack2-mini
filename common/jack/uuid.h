// Copyright (C) 2013 Paul Davis
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
#include <jack/types.h>


inline constexpr std::int32_t kUuidSize = 36;
inline constexpr std::int32_t kUuidStringSize = (kUuidSize + 1); // includes trailing null
inline constexpr std::int32_t kUuidEmptyInitializer = 0;

extern UuidC jack_client_uuid_generate();
extern UuidC jack_port_uuid_generate(std::uint32_t portId);

extern std::uint32_t jack_uuid_to_index(UuidC);

extern std::int32_t jack_uuid_compare(UuidC, UuidC);
extern void jack_uuid_copy(UuidC* dst, UuidC src);
extern void jack_uuid_clear(UuidC*);
extern std::int32_t jack_uuid_parse(const char* buf, UuidC*);
extern void jack_uuid_unparse(UuidC, char buf[kUuidStringSize]);
extern std::int32_t jack_uuid_empty(UuidC);
