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

#include <compare>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace jackmini {

// Identifies a client or a port across the server.  Zero is the empty value.
// The text form is decimal, as written on the control protocol and by the tools.
class Uuid {
public:
    constexpr Uuid() noexcept = default;
    constexpr explicit Uuid(std::uint64_t value) noexcept
        : m_value(value)
    {
    }

    constexpr std::uint64_t value() const noexcept { return m_value; }
    constexpr bool empty() const noexcept { return m_value == 0; }

    std::string toString() const;

    // Decimal, with leading white space and '+' accepted; trailing characters are rejected.
    static std::optional<Uuid> parse(std::string_view text);

    friend constexpr auto operator<=>(Uuid, Uuid) = default;

private:
    std::uint64_t m_value = 0;
};

namespace uuid {

// The raw value of an empty UUID, for the fixed-width fields of the protocol and the shared memory.
inline constexpr std::uint64_t kEmpty = 0;

// Client identifiers are numbered by the server; port identifiers are derived from the port number.
// The high word holds the kind (2 for a client, 1 for a port).
Uuid generateClient();
Uuid generatePort(std::uint32_t portId);

// A server UUID has the type bits set.  Uuid::parse accepts any value; this is the stricter protocol check.
bool isServerUuid(Uuid value) noexcept;

// The same decimal text form as Uuid::parse and Uuid::toString, into a fixed-size buffer.
std::optional<Uuid> parse(std::string_view text);
void unparse(Uuid value, std::span<char> dst);

constexpr bool empty(Uuid value) noexcept { return value.empty(); }

} // namespace uuid
} // namespace jackmini
