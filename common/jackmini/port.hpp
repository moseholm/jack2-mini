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

#ifndef JACKMINI_PORT_HPP
#define JACKMINI_PORT_HPP

#include "uuid.hpp"

#include <cstdint>
#include <string_view>

namespace jackmini
{

using FrameCount = std::uint32_t;

// The client library is built with hidden visibility by default; the API is exported explicitly.
#define JACKMINI_API __attribute__((visibility("default")))

enum class PortType : std::uint8_t {
    Audio,
    Midi,
};

// Flags given when a port is registered.  They can be combined.
enum class PortFlags : std::uint32_t {
    None = 0,
    Input = 1u << 0,
    Output = 1u << 1,
    Physical = 1u << 2,
    CanMonitor = 1u << 3,
    Terminal = 1u << 4,
};

constexpr PortFlags operator|(PortFlags a, PortFlags b) noexcept {
    return static_cast<PortFlags>(static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
}
constexpr bool has_flag(PortFlags set, PortFlags flag) noexcept {
    return (static_cast<std::uint32_t>(set) & static_cast<std::uint32_t>(flag)) != 0;
}

// A handle to a registered port.  It is a value: copies refer to the same
// port.  It is valid until the port is unregistered or its client closes.
// A handle belongs to the client that obtained it: another client's port is
// reached through that client's find_port().
class Port {
public:
    constexpr Port() noexcept = default;

    constexpr bool valid() const noexcept { return fId != 0; }
    constexpr std::uint32_t id() const noexcept { return fId; }

    friend constexpr bool operator==(Port, Port) = default;

private:
    friend class Client;
    constexpr explicit Port(std::uint32_t id) noexcept : fId(id) {}
    std::uint32_t fId = 0;
};

} // namespace jackmini

#endif
