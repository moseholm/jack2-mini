/*
Copyright (C) 2026 JACK contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License as published by
the Free Software Foundation; either version 2.1 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
*/

#pragma once

#include <compare>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace jackmini {

// Identifies a client or a port across the server.  Zero is the empty value.
class Uuid {
public:
    constexpr Uuid() noexcept = default;
    constexpr explicit Uuid(std::uint64_t value) noexcept
        : fValue(value)
    {
    }

    constexpr std::uint64_t value() const noexcept { return fValue; }
    constexpr bool empty() const noexcept { return fValue == 0; }

    std::string to_string() const;
    static std::optional<Uuid> parse(std::string_view text);

    friend constexpr auto operator<=>(Uuid, Uuid) = default;

private:
    std::uint64_t fValue = 0;
};

} // namespace jackmini
