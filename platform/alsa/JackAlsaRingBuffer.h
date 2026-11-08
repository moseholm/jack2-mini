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
#include <cstddef>

namespace jackmini
{

/*!
\brief Single producer / single consumer planar float ring.

One writer and one reader are assumed, which is exactly the relationship
between a secondary card I/O thread and the primary RT thread.  The ring is
non-blocking: a full producer drops frames and an empty consumer is
zero-filled.

Storage is rounded up to a power of two so the RT path can use a mask instead
of an integer division.  The two positions live on separate cache lines to
avoid false sharing between the card thread and the primary RT thread.
*/

class JackAlsaRingBuffer
{

    private:

        // Fixed rather than std::hardware_destructive_interference_size: that
        // value depends on the compiler's tuning (it is 256 on generic AArch64
        // builds), and every target we ship has 64-byte cache lines.
        static constexpr std::size_t kCacheLine = 64;

        int fChannels;
        jack_nframes_t fCapacity;
        uint64_t fMask;
        float** fData;

        alignas(kCacheLine) std::atomic<uint64_t> fReadPos;
        alignas(kCacheLine) std::atomic<uint64_t> fWritePos;

        JackAlsaRingBuffer(const JackAlsaRingBuffer&);
        JackAlsaRingBuffer& operator=(const JackAlsaRingBuffer&);

        void Free();

    public:

        JackAlsaRingBuffer(int channels, jack_nframes_t capacity);
        ~JackAlsaRingBuffer();

        bool Init(int channels, jack_nframes_t capacity);
        void Reset() noexcept;

        // Best-effort mlock() of the sample storage.  Returns false when any
        // page could not be locked; the ring remains usable either way.
        bool LockMemory() const noexcept;

        int Channels() const noexcept { return fChannels; }
        jack_nframes_t Capacity() const noexcept { return fCapacity; }
        jack_nframes_t AvailableRead() const noexcept;
        jack_nframes_t AvailableWrite() const noexcept;

        // Push nframes from planar source.  Returns the number of frames
        // actually stored; overflow is dropped.
        jack_nframes_t Write(const float* const* src, jack_nframes_t nframes) noexcept;

        // Pop up to nframes into planar destination.  Missing frames are
        // zero-filled.  Returns the number of frames actually available.
        jack_nframes_t Read(float* const* dst, jack_nframes_t nframes) noexcept;
};

} // end of namespace

#endif
