/*
Copyright (C) 2026 JACK contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License as published by
the Free Software Foundation; either version 2.1 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
*/

#ifndef JACKMINI_RINGBUFFER_HPP
#define JACKMINI_RINGBUFFER_HPP

#include <cstddef>
#include <span>

namespace jackmini
{

// A single-producer, single-consumer ring of T.  Storage is allocated in the
// constructor.  Reading and writing do not allocate and do not block.
// The capacity is rounded up to a power of two.
template <class T>
class RingBuffer {
public:
    explicit RingBuffer(std::size_t min_capacity);

    RingBuffer(const RingBuffer&) = delete;
    RingBuffer& operator=(const RingBuffer&) = delete;

    std::size_t capacity() const noexcept;
    std::size_t read_space() const noexcept;   // items the consumer can read now
    std::size_t write_space() const noexcept;  // items the producer can write now

    // Each returns the number of items actually moved, which may be less than
    // requested.  Only one thread may write and only one may read.
    std::size_t write(std::span<const T> items) noexcept;
    std::size_t read(std::span<T> items) noexcept;

    void reset() noexcept;  // only when neither side is running
};

} // namespace jackmini

#endif
