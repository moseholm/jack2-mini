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

#pragma once

#include "JackTypes.h"
#include <atomic>

/*
 * Lock-free operations on counters that live in shared memory.  The object must
 * be naturally aligned for its type; users check this with static_asserts on
 * the offsets within their structures.  The operations are seq_cst, which is
 * the ordering the original inline assembly gave.
 */

static inline bool CAS(UInt32 value, UInt32 newvalue, void* addr)
{
    UInt32 expected = value;
    return std::atomic_ref<UInt32>(*static_cast<UInt32*>(addr)).compare_exchange_strong(expected, newvalue);
}

static inline long INC_ATOMIC(SInt32* val)
{
    return std::atomic_ref<SInt32>(*val).fetch_add(1);
}

static inline long DEC_ATOMIC(SInt32* val)
{
    return std::atomic_ref<SInt32>(*val).fetch_sub(1);
}
