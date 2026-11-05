/*
Copyright (C) 2026 JACK contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#ifndef __JackAlsaRingBuffer__
#define __JackAlsaRingBuffer__

#include "types.h"
#include <atomic>

namespace Jack
{

/*!
\brief Single producer / single consumer planar float ring.

One writer and one reader are assumed, which is exactly the relationship
between a secondary card I/O thread and the primary RT thread.  The ring is
non-blocking: a full producer drops frames and an empty consumer is
zero-filled.
*/

class JackAlsaRingBuffer
{

    private:

        int fChannels;
        jack_nframes_t fCapacity;
        float** fData;

        std::atomic<uint64_t> fReadPos;
        std::atomic<uint64_t> fWritePos;

        JackAlsaRingBuffer(const JackAlsaRingBuffer&);
        JackAlsaRingBuffer& operator=(const JackAlsaRingBuffer&);

        void Free();

    public:

        JackAlsaRingBuffer(int channels, jack_nframes_t capacity);
        ~JackAlsaRingBuffer();

        bool Init(int channels, jack_nframes_t capacity);
        void Reset();

        int Channels() const { return fChannels; }
        jack_nframes_t Capacity() const { return fCapacity; }
        jack_nframes_t AvailableRead() const;
        jack_nframes_t AvailableWrite() const;

        // Push nframes from planar source.  Returns the number of frames
        // actually stored; overflow is dropped.
        jack_nframes_t Write(const float* const* src, jack_nframes_t nframes);

        // Pop up to nframes into planar destination.  Missing frames are
        // zero-filled.  Returns the number of frames actually available.
        jack_nframes_t Read(float* const* dst, jack_nframes_t nframes);
};

} // end of namespace

#endif
