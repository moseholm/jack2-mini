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

JackAlsaRingBuffer::JackAlsaRingBuffer(int channels, jack_nframes_t capacity)
    : fChannels(0)
    , fCapacity(0)
    , fMask(0)
    , fData(NULL)
    , fReadPos(0)
    , fWritePos(0)
{
    Init(channels, capacity);
}

JackAlsaRingBuffer::~JackAlsaRingBuffer()
{
    Free();
}

void JackAlsaRingBuffer::Free()
{
    if (fData) {
        for (int i = 0; i < fChannels; i++) {
            delete[] fData[i];
        }
        delete[] fData;
        fData = NULL;
    }
    fChannels = 0;
    fCapacity = 0;
    fMask = 0;
}

bool JackAlsaRingBuffer::Init(int channels, jack_nframes_t capacity)
{
    Free();

    if (channels <= 0 || capacity == 0) {
        return false;
    }

    const uint64_t rounded = std::bit_ceil(static_cast<uint64_t>(capacity));
    if (rounded > UINT_MAX) {
        return false;
    }

    fChannels = channels;
    fCapacity = static_cast<jack_nframes_t>(rounded);
    fMask = rounded - 1;
    fData = new float*[channels];
    for (int i = 0; i < channels; i++) {
        fData[i] = new float[fCapacity];
        std::memset(fData[i], 0, sizeof(float) * fCapacity);
    }
    Reset();
    return true;
}

void JackAlsaRingBuffer::Reset() noexcept
{
    fReadPos.store(0, std::memory_order_relaxed);
    fWritePos.store(0, std::memory_order_relaxed);
}

bool JackAlsaRingBuffer::LockMemory() const noexcept
{
    if (!fData || fCapacity == 0) {
        return true;
    }

    bool locked = mlock(fData, sizeof(float*) * fChannels) == 0;
    for (int i = 0; i < fChannels; i++) {
        if (fData[i] != NULL) {
            locked = (mlock(fData[i], sizeof(float) * fCapacity) == 0) && locked;
        }
    }
    return locked;
}

jack_nframes_t JackAlsaRingBuffer::AvailableRead() const noexcept
{
    const uint64_t write = fWritePos.load(std::memory_order_acquire);
    const uint64_t read = fReadPos.load(std::memory_order_relaxed);
    uint64_t avail = write - read;
    if (avail > fCapacity) [[unlikely]] {
        avail = fCapacity;
    }
    return static_cast<jack_nframes_t>(avail);
}

jack_nframes_t JackAlsaRingBuffer::AvailableWrite() const noexcept
{
    return fCapacity - AvailableRead();
}

jack_nframes_t JackAlsaRingBuffer::Write(const float* const* src, jack_nframes_t nframes) noexcept
{
    const uint64_t write = fWritePos.load(std::memory_order_relaxed);
    const uint64_t read = fReadPos.load(std::memory_order_acquire);
    const uint64_t free_frames = fCapacity - (write - read);

    if (nframes > free_frames) [[unlikely]] {
        nframes = static_cast<jack_nframes_t>(free_frames);
    }
    if (nframes == 0) [[unlikely]] {
        return 0;
    }

    const uint64_t offset = write & fMask;
    uint64_t first = fCapacity - offset;
    if (first > nframes) {
        first = nframes;
    }
    const uint64_t second = nframes - first;

    for (int ch = 0; ch < fChannels; ch++) {
        std::memcpy(fData[ch] + offset, src[ch], sizeof(float) * first);
        if (second) [[unlikely]] {
            std::memcpy(fData[ch], src[ch] + first, sizeof(float) * second);
        }
    }

    fWritePos.store(write + nframes, std::memory_order_release);
    return nframes;
}

jack_nframes_t JackAlsaRingBuffer::Read(float* const* dst, jack_nframes_t nframes) noexcept
{
    const uint64_t write = fWritePos.load(std::memory_order_acquire);
    const uint64_t read = fReadPos.load(std::memory_order_relaxed);
    uint64_t avail = write - read;

    if (avail > fCapacity) [[unlikely]] {
        avail = fCapacity;
    }

    const jack_nframes_t available = static_cast<jack_nframes_t>(avail);
    const jack_nframes_t to_read = std::min(nframes, available);

    if (to_read) {
        const uint64_t offset = read & fMask;
        uint64_t first = fCapacity - offset;
        if (first > to_read) {
            first = to_read;
        }
        const uint64_t second = to_read - first;

        for (int ch = 0; ch < fChannels; ch++) {
            std::memcpy(dst[ch], fData[ch] + offset, sizeof(float) * first);
            if (second) [[unlikely]] {
                std::memcpy(dst[ch] + first, fData[ch], sizeof(float) * second);
            }
        }

        fReadPos.store(read + to_read, std::memory_order_release);
    }

    if (to_read < nframes) [[unlikely]] {
        for (int ch = 0; ch < fChannels; ch++) {
            std::memset(dst[ch] + to_read, 0, sizeof(float) * (nframes - to_read));
        }
    }

    return available;
}

} // namespace jackmini
