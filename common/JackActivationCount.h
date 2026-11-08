/*
Copyright (C) 2004-2008 Grame

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

#ifndef __JackActivationCount__
#define __JackActivationCount__

#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include "JackPlatformPlug.h"
#include "JackTime.h"
#include "JackTypes.h"

namespace Jack
{

struct JackClientControl;

/*!
\brief Client activation counter.
*/


class JackActivationCount
{

    private:

        alignas(SInt32) SInt32 fValue;
        SInt32 fCount;

    public:

        JackActivationCount(): fValue(0), fCount(0)
        {
            static_assert(offsetof(JackActivationCount, fValue) % sizeof(fValue) == 0,
                          "fValue must be aligned within JackActivationCount");
            static_assert(offsetof(JackActivationCount, fCount) % sizeof(fCount) == 0,
                          "fCount must be aligned within JackActivationCount");
            assert(reinterpret_cast<std::uintptr_t>(ValuePtr()) % alignof(SInt32) == 0);
            assert(reinterpret_cast<std::uintptr_t>(CountPtr()) % alignof(SInt32) == 0);
        }

        bool Signal(JackSynchro* synchro, JackClientControl* control);

        /* The counters are reached through their offsets: the class is packed, and a
         * reference to a packed member is not allowed.  The constructor checks that
         * the addresses are aligned. */
        SInt32* ValuePtr()
        {
            return reinterpret_cast<SInt32*>(reinterpret_cast<char*>(this) + offsetof(JackActivationCount, fValue));
        }
        SInt32* CountPtr()
        {
            return reinterpret_cast<SInt32*>(reinterpret_cast<char*>(this) + offsetof(JackActivationCount, fCount));
        }

        inline void Reset()
        {
            std::atomic_ref<SInt32>(*ValuePtr()).store(std::atomic_ref<SInt32>(*CountPtr()).load());
        }

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

        inline int GetValue() const
        {
            return std::atomic_ref<SInt32>(*const_cast<JackActivationCount*>(this)->ValuePtr()).load();
        }

};

} // end of namespace


#endif
