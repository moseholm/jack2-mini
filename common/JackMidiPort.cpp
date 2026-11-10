// Copyright (C) 2007 Dmitry Baikov
// Copyright (C) 2018 Filipe Coelho
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

#include "JackMidiPort.h"
#include "JackError.h"
#include "JackPortType.h"
#include <cstdint>
#include <assert.h>
#include <string.h>

namespace jackmini {

LIB_EXPORT void MidiBuffer::reset(NframesC nframes)
{
    // This line ate 1 hour of my life... dsbaikov
    this->m_nframes = nframes;
    m_writePos = 0;
    m_eventCount = 0;
    m_lostEvents = 0;
}

LIB_EXPORT ShmsizeC MidiBuffer::maxEventSize() const
{
    assert(((jack_shmsize_t)-1) < 0); // jack_shmsize_t should be signed
    ShmsizeC left = m_bufferSize - (sizeof(MidiBuffer) + sizeof(MidiEvent) * (m_eventCount + 1) + m_writePos);
    if (left < 0) {
        return 0;
    }
    if (left <= MidiEvent::kInlineSizeMax) {
        return MidiEvent::kInlineSizeMax;
    }
    return left;
}

LIB_EXPORT MidiData* MidiBuffer::reserveEvent(NframesC time, ShmsizeC size)
{
    ShmsizeC space = maxEventSize();
    if (space == 0 || size > space) {
        jack_error("JackMidiBuffer::ReserveEvent - the buffer does not have "
                   "enough room to enqueue a %lu byte event",
            size);
        m_lostEvents++;
        return 0;
    }
    MidiEvent* event = &m_events[m_eventCount++];
    event->m_time = time;
    event->m_size = size;

    if (size <= MidiEvent::kInlineSizeMax) {
        return event->m_data.data();
    }

    m_writePos += size;
    event->m_offset = m_bufferSize - m_writePos;
    return (MidiData*)this + event->m_offset;
}

void midiBufferInit(void* buffer, size_t bufferSize, NframesC nframes)
{
    MidiBuffer* midi = (MidiBuffer*)buffer;
    midi->m_magic = MidiBuffer::kMagic;
    // Since port buffer has actually always BUFFER_SIZE_MAX frames, we can safely use all the size
    midi->m_bufferSize = kBufferSizeMax * sizeof(DefaultAudioSampleC);
    midi->reset(nframes);
}

// The mixdown function below, is a simplest (read slowest) implementation possible.
// But, since it is unlikely that it will mix many buffers with many events,
// it should perform quite good.
// More efficient (and possibly, fastest possible) implementation (it exists),
// using calendar queue algorithm is about 3 times bigger, and uses alloca().
// So, let's listen to D.Knuth about premature optimisation, a leave the current
// implementation as is, until it is proved to be a bottleneck.
// Dmitry Baikov.
static void midiBufferMixdown(void* mixbuffer, void** srcBuffers, std::int32_t srcCount, NframesC nframes)
{
    MidiBuffer* mix = static_cast<MidiBuffer*>(mixbuffer);
    if (!mix->isValid()) {
        jack_error("jackmini::MidiBufferMixdown - invalid mix buffer");
        return;
    }
    mix->reset(nframes);

    std::uint32_t mixIndex[srcCount];
    std::int32_t eventCount = 0;
    for (std::int32_t i = 0; i < srcCount; ++i) {
        MidiBuffer* buf = static_cast<MidiBuffer*>(srcBuffers[i]);
        if (!buf->isValid()) {
            jack_error("jackmini::MidiBufferMixdown - invalid source buffer");
            return;
        }
        mixIndex[i] = 0;
        eventCount += buf->m_eventCount;
        mix->m_lostEvents += buf->m_lostEvents;
    }

    std::int32_t eventsDone;
    for (eventsDone = 0; eventsDone < eventCount; ++eventsDone) {
        MidiBuffer* nextBuf = 0;
        MidiEvent* nextEvent = 0;
        std::uint32_t nextBufIndex = 0;

        // find the earliest event
        for (std::int32_t i = 0; i < srcCount; ++i) {
            MidiBuffer* buf = static_cast<MidiBuffer*>(srcBuffers[i]);
            if (mixIndex[i] >= buf->m_eventCount)
                continue;
            MidiEvent* e = &buf->m_events[mixIndex[i]];
            if (!nextEvent || e->m_time < nextEvent->m_time) {
                nextEvent = e;
                nextBuf = buf;
                nextBufIndex = i;
            }
        }
        if (nextEvent == 0) {
            jack_error("jackmini::MidiBufferMixdown - got invalid next event");
            break;
        }

        // write the event
        MidiData* dest = mix->reserveEvent(nextEvent->m_time, nextEvent->m_size);
        if (!dest)
            break;

        memcpy(dest, nextEvent->getData(nextBuf), nextEvent->m_size);
        mixIndex[nextBufIndex]++;
    }
    mix->m_lostEvents += eventCount - eventsDone;
}

static size_t midiBufferSize()
{
    return kBufferSizeMax * sizeof(DefaultAudioSampleC);
}

const PortType gMidiPortType = {
    kDefaultMidiType,
    midiBufferSize,
    midiBufferInit,
    midiBufferMixdown
};

} // namespace jackmini
