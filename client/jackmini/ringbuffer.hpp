// Copyright (C) 2026 JACK contributors
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

#pragma once

#include <algorithm>
#include <atomic>
#include <bit>
#include <cstddef>
#include <memory>
#include <span>

namespace jackmini {

// A single-producer, single-consumer ring of T.  Storage is allocated in the
// constructor.  Reading and writing do not allocate and do not block.
// The capacity is rounded up to a power of two.
//
// The producer owns the write index and the consumer owns the read index.
// Each side publishes its index with release and reads the other's with
// acquire, so the items are visible before the index moves past them.
template <class T>
class RingBuffer {
public:
    explicit RingBuffer(std::size_t minCapacity)
        : m_capacity(std::bit_ceil(std::max<std::size_t>(minCapacity, 2)))
        , m_mask(m_capacity - 1)
        , m_storage(std::make_unique<T[]>(m_capacity))
    {
    }

    RingBuffer(const RingBuffer&) = delete;
    RingBuffer& operator=(const RingBuffer&) = delete;

    std::size_t capacity() const noexcept { return m_capacity; }

    std::size_t readSpace() const noexcept
    {
        const std::size_t w = m_write.load(std::memory_order_acquire);
        const std::size_t r = m_read.load(std::memory_order_relaxed);
        return w - r;
    }

    std::size_t writeSpace() const noexcept
    {
        const std::size_t w = m_write.load(std::memory_order_relaxed);
        const std::size_t r = m_read.load(std::memory_order_acquire);
        return m_capacity - (w - r);
    }

    // Each returns the number of items actually moved, which may be less than
    // requested.  Only one thread may write and only one may read.
    std::size_t write(std::span<const T> items) noexcept
    {
        const std::size_t w = m_write.load(std::memory_order_relaxed);
        const std::size_t r = m_read.load(std::memory_order_acquire);
        const std::size_t n = std::min(items.size(), m_capacity - (w - r));
        for (std::size_t i = 0; i < n; i++) {
            m_storage[(w + i) & m_mask] = items[i];
        }
        m_write.store(w + n, std::memory_order_release);
        return n;
    }

    std::size_t read(std::span<T> items) noexcept
    {
        const std::size_t r = m_read.load(std::memory_order_relaxed);
        const std::size_t w = m_write.load(std::memory_order_acquire);
        const std::size_t n = std::min(items.size(), w - r);
        for (std::size_t i = 0; i < n; i++) {
            items[i] = m_storage[(r + i) & m_mask];
        }
        m_read.store(r + n, std::memory_order_release);
        return n;
    }

    // Only when neither side is running.
    void reset() noexcept
    {
        m_write.store(0, std::memory_order_relaxed);
        m_read.store(0, std::memory_order_relaxed);
    }

private:
    const std::size_t m_capacity;
    const std::size_t m_mask;
    std::unique_ptr<T[]> m_storage;
    alignas(64) std::atomic<std::size_t> m_write { 0 };
    alignas(64) std::atomic<std::size_t> m_read { 0 };
};

} // namespace jackmini
