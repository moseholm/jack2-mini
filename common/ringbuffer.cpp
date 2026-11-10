// Copyright (C) 2000 Paul Davis
// Copyright (C) 2003 Rohan Drape
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
//
// ISO/POSIX C version of Paul Davis's lock free ringbuffer C++ code.
// This is safe for the case of one read thread and one write thread.

// Lock-free ringbuffer of bytes, for one reader thread and one writer thread. The C API (jack_ringbuffer_*) is the
// one the MIDI code and the clients use; the logic lives in RingBuffer.

#include "JackCompilerDeps.h"
#include <cstddef>
#include <cstdint>
#include <jack/ringbuffer.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <memory>
#include <new>

namespace jackmini {

class RingBuffer {
public:
    // Allocates the data area for at least sz bytes. The size is rounded up to a power of two, and is at least 2.
    // Returns false when the allocation fails.
    bool init(std::size_t sz) noexcept
    {
        std::size_t power_of_two = 2;
        while (power_of_two < sz) {
            power_of_two <<= 1;
        }
        fBuf.reset(new (std::nothrow) std::byte[power_of_two]);
        if (!fBuf) {
            return false;
        }
        fSize = power_of_two;
        fSizeMask = power_of_two - 1;
        return true;
    }

    bool mlock() noexcept
    {
        // USE_MLOCK has never been defined in this build, so the lock is only recorded.
        fMlocked = true;
        return true;
    }

    void reset() noexcept
    {
        fWritePtr.store(0, std::memory_order_relaxed);
        fReadPtr.store(0, std::memory_order_relaxed);
        std::memset(fBuf.get(), 0, fSize);
    }

    void reset_size(std::size_t sz) noexcept
    {
        fSize = sz;
        fSizeMask = sz - 1;
        fWritePtr.store(0, std::memory_order_relaxed);
        fReadPtr.store(0, std::memory_order_relaxed);
    }

    std::size_t read_space() const noexcept
    {
        const std::size_t w = fWritePtr.load(std::memory_order_acquire);
        const std::size_t r = fReadPtr.load(std::memory_order_relaxed);
        return (w - r) & fSizeMask;
    }

    std::size_t write_space() const noexcept
    {
        const std::size_t w = fWritePtr.load(std::memory_order_relaxed);
        const std::size_t r = fReadPtr.load(std::memory_order_acquire);
        return (r - w - 1) & fSizeMask;
    }

    std::size_t read(std::byte* dest, std::size_t cnt) noexcept
    {
        const std::size_t free_cnt = read_space();
        if (free_cnt == 0) {
            return 0;
        }
        const std::size_t to_read = std::min(cnt, free_cnt);
        std::size_t r = fReadPtr.load(std::memory_order_relaxed);
        const std::size_t n1 = (r + to_read > fSize) ? fSize - r : to_read;
        const std::size_t n2 = to_read - n1;

        std::memcpy(dest, fBuf.get() + r, n1);
        r = (r + n1) & fSizeMask;
        fReadPtr.store(r, std::memory_order_release);
        if (n2) {
            std::memcpy(dest + n1, fBuf.get() + r, n2);
            r = (r + n2) & fSizeMask;
            fReadPtr.store(r, std::memory_order_release);
        }
        return to_read;
    }

    std::size_t peek(std::byte* dest, std::size_t cnt) const noexcept
    {
        const std::size_t free_cnt = read_space();
        if (free_cnt == 0) {
            return 0;
        }
        const std::size_t to_read = std::min(cnt, free_cnt);
        const std::size_t r = fReadPtr.load(std::memory_order_relaxed);
        const std::size_t n1 = (r + to_read > fSize) ? fSize - r : to_read;
        const std::size_t n2 = to_read - n1;

        std::memcpy(dest, fBuf.get() + r, n1);
        if (n2) {
            std::memcpy(dest + n1, fBuf.get(), n2);
        }
        return to_read;
    }

    std::size_t write(const std::byte* src, std::size_t cnt) noexcept
    {
        const std::size_t free_cnt = write_space();
        if (free_cnt == 0) {
            return 0;
        }
        const std::size_t to_write = std::min(cnt, free_cnt);
        std::size_t w = fWritePtr.load(std::memory_order_relaxed);
        const std::size_t n1 = (w + to_write > fSize) ? fSize - w : to_write;
        const std::size_t n2 = to_write - n1;

        std::memcpy(fBuf.get() + w, src, n1);
        w = (w + n1) & fSizeMask;
        fWritePtr.store(w, std::memory_order_release);
        if (n2) {
            std::memcpy(fBuf.get() + w, src + n1, n2);
            w = (w + n2) & fSizeMask;
            fWritePtr.store(w, std::memory_order_release);
        }
        return to_write;
    }

    void read_advance(std::size_t cnt) noexcept
    {
        const std::size_t r = fReadPtr.load(std::memory_order_relaxed);
        fReadPtr.store((r + cnt) & fSizeMask, std::memory_order_release);
    }

    void write_advance(std::size_t cnt) noexcept
    {
        const std::size_t w = fWritePtr.load(std::memory_order_relaxed);
        fWritePtr.store((w + cnt) & fSizeMask, std::memory_order_release);
    }

    void read_vector(jack_ringbuffer_data_t* vec) const noexcept
    {
        const std::size_t r = fReadPtr.load(std::memory_order_relaxed);
        const std::size_t free_cnt = read_space();
        describe(r, free_cnt, vec);
    }

    void write_vector(jack_ringbuffer_data_t* vec) const noexcept
    {
        const std::size_t w = fWritePtr.load(std::memory_order_relaxed);
        const std::size_t free_cnt = write_space();
        describe(w, free_cnt, vec);
    }

private:
    // Two segments when the data wraps past the end of the buffer.
    void describe(std::size_t pos, std::size_t cnt, jack_ringbuffer_data_t* vec) const noexcept
    {
        const std::size_t end = pos + cnt;
        vec[0].buf = reinterpret_cast<char*>(fBuf.get() + pos);
        if (end > fSize) {
            vec[0].len = fSize - pos;
            vec[1].buf = reinterpret_cast<char*>(fBuf.get());
            vec[1].len = end & fSizeMask;
        } else {
            vec[0].len = cnt;
            vec[1].len = 0;
        }
    }

    std::unique_ptr<std::byte[]> fBuf;
    std::atomic<std::size_t> fWritePtr { 0 };
    std::atomic<std::size_t> fReadPtr { 0 };
    std::size_t fSize = 0;
    std::size_t fSizeMask = 0;
    bool fMlocked = false;
};

} // end of namespace jackmini

// The public C type. Its contents are private to this file.
struct jack_ringbuffer_t : jackmini::RingBuffer { };

extern "C" {

LIB_EXPORT jack_ringbuffer_t* jack_ringbuffer_create(size_t sz)
{
    std::unique_ptr<jack_ringbuffer_t> rb(new (std::nothrow) jack_ringbuffer_t);
    if (!rb || !rb->init(sz)) {
        return nullptr;
    }
    return rb.release();
}

LIB_EXPORT void jack_ringbuffer_free(jack_ringbuffer_t* rb)
{
    delete rb;
}

LIB_EXPORT std::int32_t jack_ringbuffer_mlock(jack_ringbuffer_t* rb)
{
    return rb->mlock() ? 0 : -1;
}

LIB_EXPORT void jack_ringbuffer_reset(jack_ringbuffer_t* rb)
{
    rb->reset();
}

LIB_EXPORT void jack_ringbuffer_reset_size(jack_ringbuffer_t* rb, size_t sz)
{
    rb->reset_size(sz);
}

LIB_EXPORT size_t jack_ringbuffer_read_space(const jack_ringbuffer_t* rb)
{
    return rb->read_space();
}

LIB_EXPORT size_t jack_ringbuffer_write_space(const jack_ringbuffer_t* rb)
{
    return rb->write_space();
}

LIB_EXPORT size_t jack_ringbuffer_read(jack_ringbuffer_t* rb, char* dest, size_t cnt)
{
    return rb->read(reinterpret_cast<std::byte*>(dest), cnt);
}

LIB_EXPORT size_t jack_ringbuffer_peek(jack_ringbuffer_t* rb, char* dest, size_t cnt)
{
    return rb->peek(reinterpret_cast<std::byte*>(dest), cnt);
}

LIB_EXPORT size_t jack_ringbuffer_write(jack_ringbuffer_t* rb, const char* src, size_t cnt)
{
    return rb->write(reinterpret_cast<const std::byte*>(src), cnt);
}

LIB_EXPORT void jack_ringbuffer_read_advance(jack_ringbuffer_t* rb, size_t cnt)
{
    rb->read_advance(cnt);
}

LIB_EXPORT void jack_ringbuffer_write_advance(jack_ringbuffer_t* rb, size_t cnt)
{
    rb->write_advance(cnt);
}

LIB_EXPORT void jack_ringbuffer_get_read_vector(const jack_ringbuffer_t* rb, jack_ringbuffer_data_t* vec)
{
    rb->read_vector(vec);
}

LIB_EXPORT void jack_ringbuffer_get_write_vector(const jack_ringbuffer_t* rb, jack_ringbuffer_data_t* vec)
{
    rb->write_vector(vec);
}

} // extern "C"
