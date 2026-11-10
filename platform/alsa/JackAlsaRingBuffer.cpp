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

#include "JackAlsaRingBuffer.h"
#include <sys/mman.h>
#include <algorithm>
#include <bit>
#include <climits>
#include <cstdint>
#include <cstring>

namespace jackmini {

AlsaRingBuffer::AlsaRingBuffer(std::int32_t channels, NframesC capacity)
    : m_channels(0)
    , m_capacity(0)
    , m_mask(0)
    , m_data(nullptr)
    , m_readPos(0)
    , m_writePos(0)
{
    init(channels, capacity);
}

AlsaRingBuffer::~AlsaRingBuffer()
{
    free();
}

void AlsaRingBuffer::free()
{
    if (m_data) {
        for (std::int32_t i = 0; i < m_channels; i++) {
            delete[] m_data[i];
        }
        delete[] m_data;
        m_data = nullptr;
    }
    m_channels = 0;
    m_capacity = 0;
    m_mask = 0;
}

bool AlsaRingBuffer::init(std::int32_t channels, NframesC capacity)
{
    free();

    if (channels <= 0 || capacity == 0) {
        return false;
    }

    const std::uint64_t rounded = std::bit_ceil(static_cast<std::uint64_t>(capacity));
    if (rounded > UINT_MAX) {
        return false;
    }

    m_channels = channels;
    m_capacity = static_cast<NframesC>(rounded);
    m_mask = rounded - 1;
    m_data = new float*[channels];
    for (std::int32_t i = 0; i < channels; i++) {
        m_data[i] = new float[m_capacity];
        std::memset(m_data[i], 0, sizeof(float) * m_capacity);
    }
    reset();
    return true;
}

void AlsaRingBuffer::reset() noexcept
{
    m_readPos.store(0, std::memory_order_relaxed);
    m_writePos.store(0, std::memory_order_relaxed);
}

bool AlsaRingBuffer::lockMemory() const noexcept
{
    if (!m_data || m_capacity == 0) {
        return true;
    }

    bool locked = mlock(m_data, sizeof(float*) * m_channels) == 0;
    for (std::int32_t i = 0; i < m_channels; i++) {
        if (m_data[i] != nullptr) {
            locked = (mlock(m_data[i], sizeof(float) * m_capacity) == 0) && locked;
        }
    }
    return locked;
}

NframesC AlsaRingBuffer::availableRead() const noexcept
{
    const std::uint64_t write = m_writePos.load(std::memory_order_acquire);
    const std::uint64_t read = m_readPos.load(std::memory_order_relaxed);
    std::uint64_t avail = write - read;
    if (avail > m_capacity) [[unlikely]] {
        avail = m_capacity;
    }
    return static_cast<NframesC>(avail);
}

NframesC AlsaRingBuffer::availableWrite() const noexcept
{
    return m_capacity - availableRead();
}

NframesC AlsaRingBuffer::write(const float* const* src, NframesC nframes) noexcept
{
    const std::uint64_t write = m_writePos.load(std::memory_order_relaxed);
    const std::uint64_t read = m_readPos.load(std::memory_order_acquire);
    const std::uint64_t freeFrames = m_capacity - (write - read);

    if (nframes > freeFrames) [[unlikely]] {
        nframes = static_cast<NframesC>(freeFrames);
    }
    if (nframes == 0) [[unlikely]] {
        return 0;
    }

    const std::uint64_t offset = write & m_mask;
    std::uint64_t first = m_capacity - offset;
    if (first > nframes) {
        first = nframes;
    }
    const std::uint64_t second = nframes - first;

    for (std::int32_t ch = 0; ch < m_channels; ch++) {
        std::memcpy(m_data[ch] + offset, src[ch], sizeof(float) * first);
        if (second) [[unlikely]] {
            std::memcpy(m_data[ch], src[ch] + first, sizeof(float) * second);
        }
    }

    m_writePos.store(write + nframes, std::memory_order_release);
    return nframes;
}

NframesC AlsaRingBuffer::read(float* const* dst, NframesC nframes) noexcept
{
    const std::uint64_t write = m_writePos.load(std::memory_order_acquire);
    const std::uint64_t read = m_readPos.load(std::memory_order_relaxed);
    std::uint64_t avail = write - read;

    if (avail > m_capacity) [[unlikely]] {
        avail = m_capacity;
    }

    const NframesC available = static_cast<NframesC>(avail);
    const NframesC toRead = std::min(nframes, available);

    if (toRead) {
        const std::uint64_t offset = read & m_mask;
        std::uint64_t first = m_capacity - offset;
        if (first > toRead) {
            first = toRead;
        }
        const std::uint64_t second = toRead - first;

        for (std::int32_t ch = 0; ch < m_channels; ch++) {
            std::memcpy(dst[ch], m_data[ch] + offset, sizeof(float) * first);
            if (second) [[unlikely]] {
                std::memcpy(dst[ch] + first, m_data[ch], sizeof(float) * second);
            }
        }

        m_readPos.store(read + toRead, std::memory_order_release);
    }

    if (toRead < nframes) [[unlikely]] {
        for (std::int32_t ch = 0; ch < m_channels; ch++) {
            std::memset(dst[ch] + toRead, 0, sizeof(float) * (nframes - toRead));
        }
    }

    return available;
}

} // namespace jackmini
