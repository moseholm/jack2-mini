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

#ifndef JACKMINI_MIDI_HPP
#define JACKMINI_MIDI_HPP

#include "port.hpp"

#include <cstddef>
#include <span>

namespace jackmini::midi
{

// One MIDI event within a cycle.
struct Event {
    FrameCount time;                       // frame within the cycle
    std::span<const std::byte> data;       // the raw MIDI bytes
};

// The events of a MIDI port for one cycle.  Valid only inside the process
// callback.  Reading does not allocate.
class Buffer {
public:
    std::size_t count() const noexcept;
    Event event(std::size_t index) const noexcept;

    // Adds an event at the given frame.  Returns false when the buffer is full.
    bool write(FrameCount time, std::span<const std::byte> data) noexcept;
    void clear() noexcept;

private:
    friend class Client;
    explicit Buffer(void* handle) noexcept : fHandle(handle) {}
    void* fHandle = nullptr;
};

} // namespace jackmini::midi

#endif
