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

#include "CompilerDeps.h"
#include <jack/ringbuffer.h>
#include <cstddef>
#include <cstdint>

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
        std::size_t powerOfTwo = 2;
        while (powerOfTwo < sz) {
            powerOfTwo <<= 1;
        }
        m_buf.reset(new (std::nothrow) std::byte[powerOfTwo]);
        if (!m_buf) {
            return false;
        }
        m_size = powerOfTwo;
        m_sizeMask = powerOfTwo - 1;
        return true;
    }

    bool mlock() noexcept
    {
        // USE_MLOCK has never been defined in this build, so the lock is only recorded.
        m_mlocked = true;
        return true;
    }

    void reset() noexcept
    {
        m_writePtr.store(0, std::memory_order_relaxed);
        m_readPtr.store(0, std::memory_order_relaxed);
        std::memset(m_buf.get(), 0, m_size);
    }

    void resetSize(std::size_t sz) noexcept
    {
        m_size = sz;
        m_sizeMask = sz - 1;
        m_writePtr.store(0, std::memory_order_relaxed);
        m_readPtr.store(0, std::memory_order_relaxed);
    }

    std::size_t readSpace() const noexcept
    {
        const std::size_t w = m_writePtr.load(std::memory_order_acquire);
        const std::size_t r = m_readPtr.load(std::memory_order_relaxed);
        return (w - r) & m_sizeMask;
    }

    std::size_t writeSpace() const noexcept
    {
        const std::size_t w = m_writePtr.load(std::memory_order_relaxed);
        const std::size_t r = m_readPtr.load(std::memory_order_acquire);
        return (r - w - 1) & m_sizeMask;
    }

    std::size_t read(std::byte* dest, std::size_t cnt) noexcept
    {
        const std::size_t freeCnt = readSpace();
        if (freeCnt == 0) {
            return 0;
        }
        const std::size_t toRead = std::min(cnt, freeCnt);
        std::size_t r = m_readPtr.load(std::memory_order_relaxed);
        const std::size_t n1 = (r + toRead > m_size) ? m_size - r : toRead;
        const std::size_t n2 = toRead - n1;

        std::memcpy(dest, m_buf.get() + r, n1);
        r = (r + n1) & m_sizeMask;
        m_readPtr.store(r, std::memory_order_release);
        if (n2) {
            std::memcpy(dest + n1, m_buf.get() + r, n2);
            r = (r + n2) & m_sizeMask;
            m_readPtr.store(r, std::memory_order_release);
        }
        return toRead;
    }

    std::size_t peek(std::byte* dest, std::size_t cnt) const noexcept
    {
        const std::size_t freeCnt = readSpace();
        if (freeCnt == 0) {
            return 0;
        }
        const std::size_t toRead = std::min(cnt, freeCnt);
        const std::size_t r = m_readPtr.load(std::memory_order_relaxed);
        const std::size_t n1 = (r + toRead > m_size) ? m_size - r : toRead;
        const std::size_t n2 = toRead - n1;

        std::memcpy(dest, m_buf.get() + r, n1);
        if (n2) {
            std::memcpy(dest + n1, m_buf.get(), n2);
        }
        return toRead;
    }

    std::size_t write(const std::byte* src, std::size_t cnt) noexcept
    {
        const std::size_t freeCnt = writeSpace();
        if (freeCnt == 0) {
            return 0;
        }
        const std::size_t toWrite = std::min(cnt, freeCnt);
        std::size_t w = m_writePtr.load(std::memory_order_relaxed);
        const std::size_t n1 = (w + toWrite > m_size) ? m_size - w : toWrite;
        const std::size_t n2 = toWrite - n1;

        std::memcpy(m_buf.get() + w, src, n1);
        w = (w + n1) & m_sizeMask;
        m_writePtr.store(w, std::memory_order_release);
        if (n2) {
            std::memcpy(m_buf.get() + w, src + n1, n2);
            w = (w + n2) & m_sizeMask;
            m_writePtr.store(w, std::memory_order_release);
        }
        return toWrite;
    }

    void readAdvance(std::size_t cnt) noexcept
    {
        const std::size_t r = m_readPtr.load(std::memory_order_relaxed);
        m_readPtr.store((r + cnt) & m_sizeMask, std::memory_order_release);
    }

    void writeAdvance(std::size_t cnt) noexcept
    {
        const std::size_t w = m_writePtr.load(std::memory_order_relaxed);
        m_writePtr.store((w + cnt) & m_sizeMask, std::memory_order_release);
    }

    void readVector(RingbufferDataC* vec) const noexcept
    {
        const std::size_t r = m_readPtr.load(std::memory_order_relaxed);
        const std::size_t freeCnt = readSpace();
        describe(r, freeCnt, vec);
    }

    void writeVector(RingbufferDataC* vec) const noexcept
    {
        const std::size_t w = m_writePtr.load(std::memory_order_relaxed);
        const std::size_t freeCnt = writeSpace();
        describe(w, freeCnt, vec);
    }

private:
    // Two segments when the data wraps past the end of the buffer.
    void describe(std::size_t pos, std::size_t cnt, RingbufferDataC* vec) const noexcept
    {
        const std::size_t end = pos + cnt;
        vec[0].m_buf = reinterpret_cast<char*>(m_buf.get() + pos);
        if (end > m_size) {
            vec[0].m_len = m_size - pos;
            vec[1].m_buf = reinterpret_cast<char*>(m_buf.get());
            vec[1].m_len = end & m_sizeMask;
        } else {
            vec[0].m_len = cnt;
            vec[1].m_len = 0;
        }
    }

    std::unique_ptr<std::byte[]> m_buf;
    std::atomic<std::size_t> m_writePtr { 0 };
    std::atomic<std::size_t> m_readPtr { 0 };
    std::size_t m_size = 0;
    std::size_t m_sizeMask = 0;
    bool m_mlocked = false;
};

} // end of namespace jackmini

// The public C type. Its contents are private to this file.
struct RingbufferC : jackmini::RingBuffer { };

extern "C" {

LIB_EXPORT RingbufferC* jack_ringbuffer_create(size_t sz)
{
    std::unique_ptr<RingbufferC> rb(new (std::nothrow) RingbufferC);
    if (!rb || !rb->init(sz)) {
        return nullptr;
    }
    return rb.release();
}

LIB_EXPORT void jack_ringbuffer_free(RingbufferC* rb)
{
    delete rb;
}

LIB_EXPORT std::int32_t jack_ringbuffer_mlock(RingbufferC* rb)
{
    return rb->mlock() ? 0 : -1;
}

LIB_EXPORT void jack_ringbuffer_reset(RingbufferC* rb)
{
    rb->reset();
}

LIB_EXPORT void jack_ringbuffer_reset_size(RingbufferC* rb, size_t sz)
{
    rb->resetSize(sz);
}

LIB_EXPORT size_t jack_ringbuffer_read_space(const RingbufferC* rb)
{
    return rb->readSpace();
}

LIB_EXPORT size_t jack_ringbuffer_write_space(const RingbufferC* rb)
{
    return rb->writeSpace();
}

LIB_EXPORT size_t jack_ringbuffer_read(RingbufferC* rb, char* dest, size_t cnt)
{
    return rb->read(reinterpret_cast<std::byte*>(dest), cnt);
}

LIB_EXPORT size_t jack_ringbuffer_peek(RingbufferC* rb, char* dest, size_t cnt)
{
    return rb->peek(reinterpret_cast<std::byte*>(dest), cnt);
}

LIB_EXPORT size_t jack_ringbuffer_write(RingbufferC* rb, const char* src, size_t cnt)
{
    return rb->write(reinterpret_cast<const std::byte*>(src), cnt);
}

LIB_EXPORT void jack_ringbuffer_read_advance(RingbufferC* rb, size_t cnt)
{
    rb->readAdvance(cnt);
}

LIB_EXPORT void jack_ringbuffer_write_advance(RingbufferC* rb, size_t cnt)
{
    rb->writeAdvance(cnt);
}

LIB_EXPORT void jack_ringbuffer_get_read_vector(const RingbufferC* rb, RingbufferDataC* vec)
{
    rb->readVector(vec);
}

LIB_EXPORT void jack_ringbuffer_get_write_vector(const RingbufferC* rb, RingbufferDataC* vec)
{
    rb->writeVector(vec);
}

} // extern "C"
