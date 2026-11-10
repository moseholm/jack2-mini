// Copyright (C) 2007 Dmitry Baikov
// Original JACK MIDI implementation Copyright (C) 2004 Ian Esten
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

#include "Error.h"
#include "MidiPort.h"
#include <errno.h>
#include <string.h>
#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

LIB_EXPORT std::uint32_t jack_midi_get_event_count(void* portBuffer);

LIB_EXPORT std::int32_t jack_midi_event_get(MidiEventC* event,
    void* portBuffer, std::uint32_t eventIndex);

LIB_EXPORT void jack_midi_clear_buffer(void* portBuffer);

LIB_EXPORT void jack_midi_reset_buffer(void* portBuffer);

LIB_EXPORT size_t jack_midi_max_event_size(void* portBuffer);

LIB_EXPORT MidiData* jack_midi_event_reserve(void* portBuffer,
    NframesC time, size_t dataSize);

LIB_EXPORT std::int32_t jack_midi_event_write(void* portBuffer,
    NframesC time, const MidiData* data, size_t dataSize);

LIB_EXPORT NframesC jack_midi_get_lost_event_count(void* portBuffer);

#ifdef __cplusplus
}
#endif

using namespace jackmini;

LIB_EXPORT
std::uint32_t jack_midi_get_event_count(void* portBuffer)
{
    MidiBuffer* buf = (MidiBuffer*)portBuffer;
    if (!buf || !buf->isValid()) {
        return 0;
    }
    return buf->m_eventCount;
}

LIB_EXPORT
std::int32_t jack_midi_event_get(MidiEventC* event, void* portBuffer, std::uint32_t eventIndex)
{
    MidiBuffer* buf = (MidiBuffer*)portBuffer;
    if (!buf || !buf->isValid()) {
        return -EINVAL;
    }
    if (eventIndex >= buf->m_eventCount) {
        return -ENOBUFS;
    }
    jackmini::MidiEvent* ev = &buf->m_events[eventIndex];
    event->m_time = ev->m_time;
    event->m_size = ev->m_size;
    event->m_buffer = ev->getData(buf);
    return 0;
}

LIB_EXPORT
void jack_midi_clear_buffer(void* portBuffer)
{
    MidiBuffer* buf = (MidiBuffer*)portBuffer;
    if (buf && buf->isValid()) {
        buf->reset(buf->m_nframes);
    }
}

LIB_EXPORT
void jack_midi_reset_buffer(void* portBuffer)
{
    midiBufferInit(portBuffer, kBufferSizeMax, kBufferSizeMax);
}

LIB_EXPORT
size_t jack_midi_max_event_size(void* portBuffer)
{
    MidiBuffer* buf = (MidiBuffer*)portBuffer;
    if (buf && buf->isValid()) {
        return buf->maxEventSize();
    }
    return 0;
}

LIB_EXPORT
MidiData* jack_midi_event_reserve(void* portBuffer, NframesC time, size_t dataSize)
{
    MidiBuffer* buf = (MidiBuffer*)portBuffer;
    if (!buf) {
        jack_error("jack_midi_event_reserve: port buffer is set to NULL");
        return 0;
    }
    if (!buf->isValid()) {
        jack_error("jack_midi_event_reserve: port buffer is invalid");
        return 0;
    }
    if (time >= buf->m_nframes) {
        jack_error("jack_midi_event_reserve: time parameter is out of range "
                   "(%lu >= %lu)",
            time, buf->m_nframes);
        return 0;
    }
    if (buf->m_eventCount && (buf->m_events[buf->m_eventCount - 1].m_time > time)) {
        jack_error("jack_midi_event_reserve: time parameter is earlier than "
                   "last reserved event");
        return 0;
    }
    return buf->reserveEvent(time, dataSize);
}

LIB_EXPORT
std::int32_t jack_midi_event_write(void* portBuffer,
    NframesC time, const MidiData* data, size_t dataSize)
{
    MidiBuffer* buf = (MidiBuffer*)portBuffer;
    if (!buf || !buf->isValid()) {
        return -EINVAL;
    }
    if (time >= buf->m_nframes || (buf->m_eventCount && buf->m_events[buf->m_eventCount - 1].m_time > time)) {
        return -EINVAL;
    }
    MidiData* dest = buf->reserveEvent(time, dataSize);
    if (!dest) {
        return -ENOBUFS;
    }
    memcpy(dest, data, dataSize);
    return 0;
}

LIB_EXPORT
std::uint32_t jack_midi_get_lost_event_count(void* portBuffer)
{
    MidiBuffer* buf = (MidiBuffer*)portBuffer;
    if (buf && buf->isValid()) {
        return buf->m_lostEvents;
    }
    return 0;
}
