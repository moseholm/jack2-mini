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

#include "JackPlatformPlug.h"
#include "JackTime.h"
#include "JackTypes.h"
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>

namespace jackmini {

struct JackClientControl;

// \brief Client activation count.
//
// The class holds the configuration only: the number of connected inputs of a client. The state that is copied on write
// holds it. The live counter, which the realtime thread decrements in a cycle, is not in the copied state: it is an
// atomic array in the graph manager (protocol.md section 4.5), and it is reset from this count at the start of each cycle.
class JackActivationCount {
private:
    SInt32 fCount;

public:
    JackActivationCount()
        : fCount(0)
    {
        static_assert(offsetof(JackActivationCount, fCount) % sizeof(fCount) == 0,
            "fCount must be aligned within JackActivationCount");
        assert(reinterpret_cast<std::uintptr_t>(CountPtr()) % alignof(SInt32) == 0);
    }

    // The counter is reached through its offset: the class is packed, and a reference to a packed member is not
    // allowed.  The constructor checks that the address is aligned.
    SInt32* CountPtr()
    {
        return reinterpret_cast<SInt32*>(reinterpret_cast<char*>(this) + offsetof(JackActivationCount, fCount));
    }

    // Decrements the live counter; signals the client when it reaches zero.
    static bool Signal(std::atomic<SInt32>& live, JackSynchro* synchro, JackClientControl* control);

    inline void SetValue(int val)
    {
        std::atomic_ref<SInt32>(*CountPtr()).store(val);
    }
    inline void IncValue()
    {
        std::atomic_ref<SInt32>(*CountPtr()).fetch_add(1);
    }
    inline void DecValue()
    {
        std::atomic_ref<SInt32>(*CountPtr()).fetch_sub(1);
    }
    inline int GetCount() const
    {
        return std::atomic_ref<SInt32>(*const_cast<JackActivationCount*>(this)->CountPtr()).load();
    }
};

} // namespace jackmini
