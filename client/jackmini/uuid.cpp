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

#include "uuid.hpp"

#include <cstdint>
#include <charconv>
#include <format>

namespace jackmini {

std::string Uuid::to_string() const
{
    return std::format("{:016x}", fValue);
}

std::optional<Uuid> Uuid::parse(std::string_view text)
{
    if (text.size() != 16) {
        return std::nullopt;
    }
    std::uint64_t value = 0;
    const char* end = text.data() + text.size();
    const auto [ptr, ec] = std::from_chars(text.data(), end, value, 16);
    if (ec != std::errc() || ptr != end) {
        return std::nullopt;
    }
    return Uuid { value };
}

} // namespace jackmini
