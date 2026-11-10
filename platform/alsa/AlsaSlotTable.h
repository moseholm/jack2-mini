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

#include <stddef.h>
#include <atomic>
#include <cstdint>

namespace jackmini {

// \brief A fixed table of pointers that the RT thread can read without locks.
//
// Writers (non-RT) place an object in a free slot with Assign() and take it out
// with Remove(). Readers call Get() once per slot per cycle; a slot that is empty
// returns nullptr.  Nothing is allocated, so the table is safe to read on the RT
// thread.  Removing an object does not free it: the caller must wait until the RT
// thread has stopped using the object before destroying it.

template <class T, size_t N>
class AlsaSlotTable {

private:
    std::atomic<T*> m_slots[N];

    AlsaSlotTable(const AlsaSlotTable&);
    AlsaSlotTable& operator=(const AlsaSlotTable&);

public:
    AlsaSlotTable()
    {
        for (size_t i = 0; i < N; i++) {
            m_slots[i].store(nullptr, std::memory_order_relaxed);
        }
    }

    static size_t capacity() { return N; }

    // Returns the slot index, or -1 if the table is full.  Not for the RT thread.
    std::int32_t assign(T* object)
    {
        for (size_t i = 0; i < N; i++) {
            if (m_slots[i].load(std::memory_order_relaxed) == nullptr) {
                m_slots[i].store(object, std::memory_order_release);
                return (std::int32_t)i;
            }
        }
        return -1;
    }

    // Empties every slot holding this object.  Returns how many were emptied.
    size_t remove(T* object)
    {
        size_t removed = 0;
        for (size_t i = 0; i < N; i++) {
            T* expected = object;
            if (m_slots[i].compare_exchange_strong(expected, nullptr, std::memory_order_release)) {
                removed++;
            }
        }
        return removed;
    }

    // Safe to call from the RT thread.
    T* get(size_t index) const
    {
        return m_slots[index].load(std::memory_order_acquire);
    }

    // Number of occupied slots.
    size_t count() const
    {
        size_t count = 0;
        for (size_t i = 0; i < N; i++) {
            if (m_slots[i].load(std::memory_order_acquire) != nullptr) {
                count++;
            }
        }
        return count;
    }
};

} // namespace jackmini
