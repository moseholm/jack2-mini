// Copyright (C) 2007 Dmitry Baikov
// Original JACK MIDI API implementation Copyright (C) 2004 Ian Esten
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

#include "JackCompilerDeps.h"
#include "JackConstants.h"
#include "JackPlatformPlug.h"
#include "types.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <stddef.h>

// Type for raw event data contained in @ref jack_midi_event_t.
typedef std::byte MidiData;

// A Jack MIDI event.
struct MidiEventC {
    NframesC m_time;  // < Sample index at which event is valid
    size_t m_size;  // < Number of bytes of data in \a buffer
    MidiData* m_buffer;  // < Raw MIDI data
};

#ifdef __cplusplus
extern "C" {
#endif

LIB_EXPORT std::uint32_t jack_midi_get_event_count(void* portBuffer);
LIB_EXPORT std::int32_t jack_midi_event_get(MidiEventC* event, void* portBuffer, std::uint32_t eventIndex);
LIB_EXPORT void jack_midi_clear_buffer(void* portBuffer);
LIB_EXPORT void jack_midi_reset_buffer(void* portBuffer);
LIB_EXPORT size_t jack_midi_max_event_size(void* portBuffer);
LIB_EXPORT MidiData* jack_midi_event_reserve(void* portBuffer, NframesC time, size_t dataSize);
LIB_EXPORT std::int32_t jack_midi_event_write(void* portBuffer, NframesC time, const MidiData* data, size_t dataSize);
LIB_EXPORT std::uint32_t jack_midi_get_lost_event_count(void* portBuffer);

#ifdef __cplusplus
}
#endif


namespace jackmini {

struct LIB_EXPORT MidiEvent {
    // Most MIDI events are < 4 bytes in size, so we can save a lot, storing them inplace.
    enum { kInlineSizeMax = sizeof(ShmsizeC) };

    std::uint32_t m_time;
    ShmsizeC m_size;
    union {
        ShmsizeC m_offset;
        std::array<MidiData, kInlineSizeMax> m_data;
    };

    MidiData* getData(void* buffer)
    {
        if (m_size <= kInlineSizeMax) {
            return m_data.data();
        } else {
            return (MidiData*)buffer + m_offset;
        }
    }
};

// To store events with arbitrarily sized payload, but still have O(1) indexed access
// we use a trick here:
// Events are stored in an linear array from the beginning of the buffer,
// but their data (if not inlined) is stored from the end of the same buffer.

struct LIB_EXPORT MidiBuffer {
    enum { kMagic = 0x900df00d };

    std::uint32_t m_magic;
    ShmsizeC m_bufferSize;
    NframesC m_nframes;
    ShmsizeC m_writePos; //!< data write position from the end of the buffer.
    std::uint32_t m_eventCount;
    std::uint32_t m_lostEvents;

    MidiEvent m_events[1]; // Using 0 size does not compile with older GCC versions, so use 1 here.

    std::int32_t isValid() const
    {
        return m_magic == kMagic;
    }
    void reset(NframesC nframes);
    ShmsizeC maxEventSize() const;

    // checks only size constraints.
    MidiData* reserveEvent(NframesC time, ShmsizeC size);
};

void midiBufferInit(void* buffer, size_t bufferSize, NframesC nframes);

} // namespace jackmini
