// Copyright (C) 2004-2008 Grame
// Copyright (C) 2026 JACK contributors
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation; either version 2.1 of the License, or
// (at your option) any later version.
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
// You should have received a copy of the GNU Lesser General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.

// The UUIDs of clients and ports, used by the server and the clients (not exported).

#include "common/Uuid.h"
#include "common/CString.h"

#include <charconv>
#include <cstdint>

namespace jackmini::uuid {

UuidC generateClient()
{
    static std::uint32_t count = 0;
    return (UuidC { 2 } << 32) | ++count;
}

UuidC generatePort(std::uint32_t portId)
{
    return (UuidC { 1 } << 32) | (portId + 1);
}

std::optional<UuidC> parse(std::string_view text)
{
    text = skipNumericPrefix(text);
    UuidC value = 0;
    const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc() || ptr == text.data()) {
        return std::nullopt;
    }
    if (value < (UuidC { 1 } << 32)) {
        return std::nullopt; // the type bits are missing
    }
    return value;
}

void unparse(UuidC value, std::span<char> dst)
{
    formatCString(dst, "{}", static_cast<std::uint64_t>(value));
}

} // namespace jackmini::uuid
