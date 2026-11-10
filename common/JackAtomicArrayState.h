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

#include "JackAtomic.h"
#include "JackCompilerDeps.h"
#include <array>
#include <string.h> // for memcpy
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>

namespace jackmini {

// \brief Counter for CAS

struct AtomicArrayCounter {
    union {
        struct {
            std::array<std::uint8_t, 4> fByteVal;
        } scounter;
        UInt32 fLongVal;
    } info;

    AtomicArrayCounter()
    {
        info.fLongVal = 0;
    }

    AtomicArrayCounter(const AtomicArrayCounter& obj)
    {
        info.fLongVal = obj.info.fLongVal;
    }
    AtomicArrayCounter(volatile const AtomicArrayCounter& obj)
    {
        info.fLongVal = obj.info.fLongVal;
    }

    AtomicArrayCounter(volatile AtomicArrayCounter& obj)
    {
        info.fLongVal = obj.info.fLongVal;
    }

    AtomicArrayCounter& operator=(const AtomicArrayCounter& obj)
    {
        info.fLongVal = obj.info.fLongVal;
        return *this;
    }
    AtomicArrayCounter& operator=(volatile AtomicArrayCounter& obj)
    {
        info.fLongVal = obj.info.fLongVal;
        return *this;
    }

    AtomicArrayCounter& operator=(AtomicArrayCounter& obj)
    {
        info.fLongVal = obj.info.fLongVal;
        return *this;
    }
};

template <typename T>
constexpr auto& longVal1(T&& e)
{
    return e.info.fLongVal;
}

template <typename T>
constexpr auto& getIndex1(T&& e, std::int32_t state)
{
    return e.info.scounter.fByteVal[state];
}

template <typename T>
constexpr void setIndex1(T&& e, std::int32_t state, std::uint8_t val)
{
    e.info.scounter.fByteVal[state] = val;
}

template <typename T>
constexpr void incIndex1(T&& e, std::int32_t state)
{
    e.info.scounter.fByteVal[state]++;
}

template <typename T>
constexpr std::int32_t swapIndex1(T&& e, std::int32_t state)
{
    return (e.info.scounter.fByteVal[0] == state) ? 0 : state;
}

// \brief Loads a shared counter atomically.  Every read of the counter goes through this.
static inline AtomicArrayCounter LoadCounter1(const AtomicArrayCounter& counter)
{
    AtomicArrayCounter result;
    longVal1(result) = std::atomic_ref<UInt32>(const_cast<UInt32&>(counter.info.fLongVal)).load();
    return result;
}

// \brief A class to handle several states in a lock-free manner
//
// Requirement:
//
// - a "current" state
// - several possible "pending" state
// - an TrySwitchState(int state) operation to atomically switch a "pending" to the "current" state (the pending becomes the current).
//
// The TrySwitchState operation returns a "current" state (either the same if switch fails or the new one, one can know if the switch has succeeded)
//
// - a WriteNextStartState(int state) returns a "pending" state to be written into
// - a WriteNextStartStop(int state) make the written "pending" state become "switchable"
//
// Different pending states can be written independently and concurrently.
//
// GetCurrentIndex() *must* return an increasing value to be able to check reading current state coherency
//
// The fCounter is an array of indexes to access the current and 3 different "pending" states.
//
// WriteNextStateStart(int index) must return a valid state to be written into, and must invalidate state "index" ==> cur state switch.
// WriteNextStateStop(int index) makes the "index" state become "switchable" with the current state.
// TrySwitchState(int index) must detect that pending state is a new state, and does the switch
// ReadCurrentState() must return the state
// GetCurrentIndex() must return an index increased each new switch.
// WriteNextStateStart(int index1) and WriteNextStateStart(int index2) can be interleaved
//
// [switch counter][index state][index state][cur index]

// CHECK livelock


template <class T>
class JackAtomicArrayState {

protected:
    // fState[0] ==> current
    // fState[1] ==> pending
    // fState[2] ==> request

    std::array<T, 3> fState;
    alignas(UInt32) alignas(AtomicArrayCounter) AtomicArrayCounter fCounter;

    UInt32 WriteNextStateStartAux(std::int32_t state, bool* result)
    {
        AtomicArrayCounter old_val;
        AtomicArrayCounter new_val;
        UInt32 cur_index;
        UInt32 next_index;
        bool need_copy;
        do {
            old_val = LoadCounter1(fCounter);
            new_val = old_val;
            *result = getIndex1(new_val, state);
            cur_index = getIndex1(new_val, 0);
            next_index = swapIndex1(old_val, state);
            need_copy = (getIndex1(new_val, state) == 0); // Written = false, switch just occurred
            setIndex1(new_val, state, 0); // Written = false, invalidate state
        } while (!CAS(longVal1(old_val), longVal1(new_val), (UInt32*)&fCounter));
        if (need_copy)
            memcpy(&fState[next_index], &fState[cur_index], sizeof(T));
        return next_index;
    }

    void WriteNextStateStopAux(std::int32_t state)
    {
        AtomicArrayCounter old_val;
        AtomicArrayCounter new_val;
        do {
            old_val = LoadCounter1(fCounter);
            new_val = old_val;
            setIndex1(new_val, state, 1); // Written = true, state becomes "switchable"
        } while (!CAS(longVal1(old_val), longVal1(new_val), (UInt32*)&fCounter));
    }

public:
    JackAtomicArrayState()
    {
        static_assert(offsetof(JackAtomicArrayState, fCounter) % sizeof(fCounter) == 0,
            "fCounter must be aligned within JackAtomicArrayState");
        assert(reinterpret_cast<std::uintptr_t>(&fCounter) % alignof(UInt32) == 0);
        longVal1(fCounter) = 0;
    }

    ~JackAtomicArrayState() // Not virtual ??
    {
    }

    // \brief Returns the current state : only valid in the RT reader thread

    T* ReadCurrentState()
    {
        return &fState[getIndex1(LoadCounter1(fCounter), 0)];
    }

    // \brief Returns the current switch counter

    UInt16 GetCurrentIndex()
    {
        return getIndex1(LoadCounter1(fCounter), 3);
    }

    // \brief Tries to switch to the next state and returns the new current state (either the same as before if case of switch failure or the new one)

    T* TrySwitchState(std::int32_t state)
    {
        AtomicArrayCounter old_val;
        AtomicArrayCounter new_val;
        do {
            old_val = LoadCounter1(fCounter);
            new_val = old_val;
            if (getIndex1(new_val, state)) { // If state has been written
                setIndex1(new_val, 0, swapIndex1(new_val, state)); // Prepare switch
                setIndex1(new_val, state, 0); // Invalidate the state "state"
                incIndex1(new_val, 3); // Inc switch
            }
        } while (!CAS(longVal1(old_val), longVal1(new_val), (UInt32*)&fCounter));
        return &fState[getIndex1(LoadCounter1(fCounter), 0)]; // Read the counter again
    }

    // \brief Tries to switch to the next state and returns the new current state (either the same as before if case of switch failure or the new one)

    T* TrySwitchState(std::int32_t state, bool* result)
    {
        AtomicArrayCounter old_val;
        AtomicArrayCounter new_val;
        do {
            old_val = LoadCounter1(fCounter);
            new_val = old_val;
            if ((*result = getIndex1(new_val, state))) { // If state has been written
                setIndex1(new_val, 0, swapIndex1(new_val, state)); // Prepare switch
                setIndex1(new_val, state, 0); // Invalidate the state "state"
                incIndex1(new_val, 3); // Inc switch
            }
        } while (!CAS(longVal1(old_val), longVal1(new_val), (UInt32*)&fCounter));
        return &fState[getIndex1(LoadCounter1(fCounter), 0)]; // Read the counter again
    }

    // \brief Start write operation : setup and returns the next state to update, check for recursive write calls.

    T* WriteNextStateStart(std::int32_t state)
    {
        bool tmp;
        UInt32 index = WriteNextStateStartAux(state, &tmp);
        return &fState[index];
    }

    T* WriteNextStateStart(std::int32_t state, bool* result)
    {
        UInt32 index = WriteNextStateStartAux(state, result);
        return &fState[index];
    }

    // \brief Stop write operation : make the next state ready to be used by the RT thread

    void WriteNextStateStop(std::int32_t state)
    {
        WriteNextStateStopAux(state);
    }
};

} // namespace jackmini
