// Copyright (C) 2026 JACK contributors
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

#pragma once

#include "types.h"
#include <cstdint>
#include <atomic>
#include <cstddef>

namespace jackmini {

// \brief Single producer / single consumer planar float ring.
//
// One writer and one reader are assumed, which is exactly the relationship
// between a secondary card I/O thread and the primary RT thread.  The ring is
// non-blocking: a full producer drops frames and an empty consumer is
// zero-filled.
//
// Storage is rounded up to a power of two so the RT path can use a mask instead
// of an integer division.  The two positions live on separate cache lines to
// avoid false sharing between the card thread and the primary RT thread.

class AlsaRingBuffer {

private:
    // Fixed rather than std::hardware_destructive_interference_size: that
    // value depends on the compiler's tuning (it is 256 on generic AArch64
    // builds), and every target we ship has 64-byte cache lines.
    static constexpr std::size_t kCacheLine = 64;

    std::int32_t m_channels;
    NframesC m_capacity;
    std::uint64_t m_mask;
    float** m_data;

    alignas(kCacheLine) std::atomic<std::uint64_t> m_readPos;
    alignas(kCacheLine) std::atomic<std::uint64_t> m_writePos;

    AlsaRingBuffer(const AlsaRingBuffer&);
    AlsaRingBuffer& operator=(const AlsaRingBuffer&);

    void free();

public:
    AlsaRingBuffer(std::int32_t channels, NframesC capacity);
    ~AlsaRingBuffer();

    bool init(std::int32_t channels, NframesC capacity);
    void reset() noexcept;

    // Best-effort mlock() of the sample storage.  Returns false when any
    // page could not be locked; the ring remains usable either way.
    bool lockMemory() const noexcept;

    std::int32_t channels() const noexcept { return m_channels; }
    NframesC capacity() const noexcept { return m_capacity; }
    NframesC availableRead() const noexcept;
    NframesC availableWrite() const noexcept;

    // Push nframes from planar source.  Returns the number of frames
    // actually stored; overflow is dropped.
    NframesC write(const float* const* src, NframesC nframes) noexcept;

    // Pop up to nframes into planar destination.  Missing frames are
    // zero-filled.  Returns the number of frames actually available.
    NframesC read(float* const* dst, NframesC nframes) noexcept;
};

} // namespace jackmini
