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

#include "uuid.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace jackmini {

using FrameCount = std::uint32_t;

// The client library is built with hidden visibility by default; the API is exported explicitly.
#define JACKMINI_API __attribute__((visibility("default")))

enum class PortType : std::uint8_t {
    kAudio,
    kMidi,
};

// Latency is measured in two directions: capture ports report how late their input is, playback ports how late their
// output reaches the hardware.  A client sets the range of its own ports, usually when the server asks (on_latency).
enum class LatencyMode : std::uint8_t {
    kCapture,
    kPlayback,
};

struct LatencyRange {
    FrameCount m_min = 0;
    FrameCount m_max = 0;

    friend constexpr bool operator==(LatencyRange, LatencyRange) = default;
};

// Flags given when a port is registered.  They can be combined.
enum class PortFlags : std::uint32_t {
    kNone = 0,
    kInput = 1u << 0,
    kOutput = 1u << 1,
    kPhysical = 1u << 2,
    kCanMonitor = 1u << 3,
    kTerminal = 1u << 4,
};

constexpr PortFlags operator|(PortFlags a, PortFlags b) noexcept
{
    return static_cast<PortFlags>(static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
}
constexpr bool hasFlag(PortFlags set, PortFlags flag) noexcept
{
    return (static_cast<std::uint32_t>(set) & static_cast<std::uint32_t>(flag)) != 0;
}

// One port, as the graph has it now.  Everything but the handle: the tools list ports with this.  The type is the
// type string the graph stores (for example "32 bit float mono audio"), and the owner is the client part of the name.
struct PortInfo {
    std::string m_name;
    std::string m_type;
    PortFlags m_flags = PortFlags::kNone;
    LatencyRange m_playbackLatency;
    LatencyRange m_captureLatency;
    std::uint32_t m_totalLatency = 0;
    std::vector<std::string> m_aliases;
    Uuid m_portUuid;
    std::string m_owner;
    Uuid m_ownerUuid;
    std::vector<std::string> m_connections;
};

// A handle to a registered port.  It is a value: copies refer to the same
// port.  It is valid until the port is unregistered or its client closes.
// A handle belongs to the client that obtained it: another client's port is
// reached through that client's find_port().  The client refuses a handle of
// another client (Error::NotFound).
class Port {
public:
    constexpr Port() noexcept = default;

    constexpr bool valid() const noexcept { return m_id != 0; }
    constexpr std::uint32_t id() const noexcept { return m_id; }
    constexpr std::uint32_t owner() const noexcept { return m_owner; }

    friend constexpr bool operator==(Port, Port) = default;

private:
    friend class Client;
    constexpr Port(std::uint32_t id, std::uint32_t owner) noexcept
        : m_id(id)
        , m_owner(owner)
    {
    }
    std::uint32_t m_id = 0;
    std::uint32_t m_owner = 0; // the client that issued the handle: the ids of two clients are not the same ports
};

} // namespace jackmini
