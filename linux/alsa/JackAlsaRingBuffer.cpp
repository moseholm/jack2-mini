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

#include "JackAlsaRingBuffer.h"
#include <string.h>

namespace Jack
{

JackAlsaRingBuffer::JackAlsaRingBuffer(int channels, jack_nframes_t capacity)
    : fChannels(0), fCapacity(0), fData(NULL), fReadPos(0), fWritePos(0)
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
}

bool JackAlsaRingBuffer::Init(int channels, jack_nframes_t capacity)
{
    Free();

    if (channels <= 0 || capacity == 0) {
        return false;
    }

    fChannels = channels;
    fCapacity = capacity;
    fData = new float*[channels];
    for (int i = 0; i < channels; i++) {
        fData[i] = new float[capacity];
        memset(fData[i], 0, sizeof(float) * capacity);
    }
    Reset();
    return true;
}

void JackAlsaRingBuffer::Reset()
{
    fReadPos.store(0, std::memory_order_relaxed);
    fWritePos.store(0, std::memory_order_relaxed);
}

jack_nframes_t JackAlsaRingBuffer::AvailableRead() const
{
    uint64_t write = fWritePos.load(std::memory_order_acquire);
    uint64_t read = fReadPos.load(std::memory_order_relaxed);
    uint64_t avail = write - read;
    return (avail > fCapacity) ? fCapacity : (jack_nframes_t)avail;
}

jack_nframes_t JackAlsaRingBuffer::AvailableWrite() const
{
    return fCapacity - AvailableRead();
}

jack_nframes_t JackAlsaRingBuffer::Write(const float* const* src, jack_nframes_t nframes)
{
    uint64_t write = fWritePos.load(std::memory_order_relaxed);
    uint64_t read = fReadPos.load(std::memory_order_acquire);
    uint64_t free_frames = fCapacity - (write - read);

    if (nframes > free_frames) {
        nframes = (jack_nframes_t)free_frames;
    }
    if (nframes == 0) {
        return 0;
    }

    uint64_t offset = write % fCapacity;
    uint64_t first = fCapacity - offset;
    if (first > nframes) {
        first = nframes;
    }
    uint64_t second = nframes - first;

    for (int ch = 0; ch < fChannels; ch++) {
        memcpy(fData[ch] + offset, src[ch], sizeof(float) * first);
        if (second) {
            memcpy(fData[ch], src[ch] + first, sizeof(float) * second);
        }
    }

    fWritePos.store(write + nframes, std::memory_order_release);
    return nframes;
}

jack_nframes_t JackAlsaRingBuffer::Read(float* const* dst, jack_nframes_t nframes)
{
    uint64_t write = fWritePos.load(std::memory_order_acquire);
    uint64_t read = fReadPos.load(std::memory_order_relaxed);
    uint64_t avail = write - read;

    if (avail > fCapacity) {
        avail = fCapacity;
    }

    jack_nframes_t available = (jack_nframes_t)avail;
    jack_nframes_t to_read = (nframes < available) ? nframes : available;

    if (to_read) {
        uint64_t offset = read % fCapacity;
        uint64_t first = fCapacity - offset;
        if (first > to_read) {
            first = to_read;
        }
        uint64_t second = to_read - first;

        for (int ch = 0; ch < fChannels; ch++) {
            memcpy(dst[ch], fData[ch] + offset, sizeof(float) * first);
            if (second) {
                memcpy(dst[ch] + first, fData[ch], sizeof(float) * second);
            }
        }

        fReadPos.store(read + to_read, std::memory_order_release);
    }

    if (to_read < nframes) {
        for (int ch = 0; ch < fChannels; ch++) {
            memset(dst[ch] + to_read, 0, sizeof(float) * (nframes - to_read));
        }
    }

    return available;
}

} // end of namespace
