// Copyright (C) 2004-2008 Grame
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

#include "PlatformPlug.h"
#include "Time.h"
#include "Types.h"
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>

namespace jackmini {

struct ClientControl;

// \brief Client activation count.
//
// The class holds the configuration only: the number of connected inputs of a client. The state that is copied on write
// holds it. The live counter, which the realtime thread decrements in a cycle, is not in the copied state: it is an
// atomic array in the graph manager (protocol.md section 4.5), and it is reset from this count at the start of each cycle.
class ActivationCount {
private:
    SInt32 m_count;

public:
    ActivationCount()
        : m_count(0)
    {
        static_assert(offsetof(ActivationCount, m_count) % sizeof(m_count) == 0,
            "fCount must be aligned within JackActivationCount");
        assert(reinterpret_cast<std::uintptr_t>(CountPtr()) % alignof(SInt32) == 0);
    }

    // The counter is reached through its offset: the class is packed, and a reference to a packed member is not
    // allowed.  The constructor checks that the address is aligned.
    SInt32* countPtr()
    {
        return reinterpret_cast<SInt32*>(reinterpret_cast<std::byte*>(this) + offsetof(ActivationCount, m_count));
    }

    // Decrements the live counter; signals the client when it reaches zero.
    static bool signal(std::atomic<SInt32>& live, Synchro* synchro, ClientControl* control);

    inline void setValue(std::int32_t val)
    {
        std::atomic_ref<SInt32>(*countPtr()).store(val);
    }
    inline void incValue()
    {
        std::atomic_ref<SInt32>(*countPtr()).fetch_add(1);
    }
    inline void decValue()
    {
        std::atomic_ref<SInt32>(*countPtr()).fetch_sub(1);
    }
    inline std::int32_t getCount() const
    {
        return std::atomic_ref<SInt32>(*const_cast<ActivationCount*>(this)->countPtr()).load();
    }
};

} // namespace jackmini
