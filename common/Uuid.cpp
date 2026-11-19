// Copyright (C) 2004-2008 Grame
// Copyright (C) 2026 JACK contributors
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

// The one implementation of the UUIDs of clients and ports, used by the server, the client and the tools.

#include "common/Uuid.hpp"
#include "common/CString.h"

namespace jackmini {

std::string Uuid::toString() const
{
    return std::to_string(m_value);
}

std::optional<Uuid> Uuid::parse(std::string_view text)
{
    const auto value = parseInteger<std::uint64_t>(text);
    if (!value) {
        return std::nullopt;
    }
    return Uuid { *value };
}

namespace uuid {

Uuid generateClient()
{
    static std::uint32_t count = 0;
    return Uuid { (std::uint64_t { 2 } << 32) | ++count };
}

Uuid generatePort(std::uint32_t portId)
{
    return Uuid { (std::uint64_t { 1 } << 32) | (portId + 1) };
}

bool isServerUuid(Uuid value) noexcept
{
    return value.value() >= (std::uint64_t { 1 } << 32);
}

std::optional<Uuid> parse(std::string_view text)
{
    const auto value = Uuid::parse(text);
    return value && isServerUuid(*value) ? value : std::nullopt;
}

void unparse(Uuid value, std::span<char> dst)
{
    copyCString(dst, value.toString());
}

} // namespace uuid
} // namespace jackmini
