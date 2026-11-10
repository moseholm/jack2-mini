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

// Compile-time checks of the jackmini client API.  The declarations have no
// definitions yet, so this program only checks types and overloads and is
// linked without the implementation.

#include "jackmini/Jackmini.hpp"

#include <concepts>
#include <type_traits>

using namespace jackmini;

// The client is move-only: it owns the server connection and the threads.
static_assert(std::is_move_constructible_v<Client>);
static_assert(std::is_move_assignable_v<Client>);
static_assert(!std::is_copy_constructible_v<Client>);
static_assert(!std::is_copy_assignable_v<Client>);

// A port is a value handle.
static_assert(std::is_trivially_copyable_v<Port>);
static_assert(std::equality_comparable<Port>);
static_assert(!Port { }.valid());

// The process callback must return bool.  A void callable is rejected.
using ProcessOk = decltype(std::declval<Client&>().onProcess(std::declval<bool (*)(const Cycle&)>()));
static_assert(std::same_as<ProcessOk, Result<void>>);
template <class F>
concept AcceptsProcess = requires(Client& c, F f) { c.onProcess(f); };
static_assert(!AcceptsProcess<void (*)(FrameCount)>);
static_assert(AcceptsProcess<bool (*)(const Cycle&)>);

// Setup calls return a result, and open() returns a Result<Client>.
static_assert(std::same_as<decltype(Client::open("x")), Result<Client>>);
static_assert(std::same_as<decltype(std::declval<Client&>().registerPort("p", PortType::kAudio, PortFlags::kInput)),
    Result<Port>>);

// Realtime accessors do not return errors.
static_assert(std::same_as<decltype(std::declval<Client&>().audioBuffer(Port { })), std::span<float>>);
static_assert(std::same_as<decltype(std::declval<Client&>().midiBuffer(Port { })), midi::Buffer>);

// Flags combine.
static_assert(hasFlag(PortFlags::kInput | PortFlags::kPhysical, PortFlags::kPhysical));
static_assert(!hasFlag(PortFlags::kInput, PortFlags::kOutput));

// Uuid is a value type with ordering.
static_assert(Uuid { 1 } < Uuid { 2 });
static_assert(Uuid { }.empty());

// Control API.  Server is move-only, like Client.
static_assert(!std::is_copy_constructible_v<control::Server>);
static_assert(std::is_move_constructible_v<control::Server>);

int main() { return 0; }
