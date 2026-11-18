// Copyright (C) 2013 Paul Davis
// Copyright (C) 2026 JACK contributors
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

#include "common/Types.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace jackmini::uuid {

inline constexpr UuidC kEmpty = 0;

// Client identifiers are numbered by the server; port identifiers are derived from the port number.
// The high word holds the kind (2 for a client, 1 for a port).
UuidC generateClient();
UuidC generatePort(std::uint32_t portId);

// Text form is the decimal value, as used by the control protocol and the command-line tools.
std::optional<UuidC> parse(std::string_view text);
void unparse(UuidC value, std::span<char> dst);

constexpr bool empty(UuidC value) noexcept { return value == kEmpty; }

} // namespace jackmini::uuid
