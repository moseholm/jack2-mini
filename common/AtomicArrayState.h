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

#include "Atomic.h"
#include "CompilerDeps.h"
#include <string.h> // for memcpy
#include <array>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>

namespace jackmini {

// \brief Counter for CAS

struct AtomicArrayCounter {
    union {
        struct {
            std::array<std::uint8_t, 4> m_byteVal;
        } m_scounter;
        UInt32 m_longVal;
    } m_info;

    AtomicArrayCounter()
    {
        m_info.m_longVal = 0;
    }

    AtomicArrayCounter(const AtomicArrayCounter& obj)
    {
        m_info.m_longVal = obj.m_info.m_longVal;
    }
    AtomicArrayCounter(volatile const AtomicArrayCounter& obj)
    {
        m_info.m_longVal = obj.m_info.m_longVal;
    }

    AtomicArrayCounter(volatile AtomicArrayCounter& obj)
    {
        m_info.m_longVal = obj.m_info.m_longVal;
    }

    AtomicArrayCounter& operator=(const AtomicArrayCounter& obj)
    {
        m_info.m_longVal = obj.m_info.m_longVal;
        return *this;
    }
    AtomicArrayCounter& operator=(volatile AtomicArrayCounter& obj)
    {
        m_info.m_longVal = obj.m_info.m_longVal;
        return *this;
    }

    AtomicArrayCounter& operator=(AtomicArrayCounter& obj)
    {
        m_info.m_longVal = obj.m_info.m_longVal;
        return *this;
    }
};

template <typename T>
constexpr auto& longVal1(T&& e)
{
    return e.m_info.m_longVal;
}

template <typename T>
constexpr auto& getIndex1(T&& e, std::int32_t state)
{
    return e.m_info.m_scounter.m_byteVal[state];
}

template <typename T>
constexpr void setIndex1(T&& e, std::int32_t state, std::uint8_t val)
{
    e.m_info.m_scounter.m_byteVal[state] = val;
}

template <typename T>
constexpr void incIndex1(T&& e, std::int32_t state)
{
    e.m_info.m_scounter.m_byteVal[state]++;
}

template <typename T>
constexpr std::int32_t swapIndex1(T&& e, std::int32_t state)
{
    return (e.m_info.m_scounter.m_byteVal[0] == state) ? 0 : state;
}

// \brief Loads a shared counter atomically.  Every read of the counter goes through this.
static inline AtomicArrayCounter loadCounter1(const AtomicArrayCounter& counter)
{
    AtomicArrayCounter result;
    longVal1(result) = std::atomic_ref<UInt32>(const_cast<UInt32&>(counter.m_info.m_longVal)).load();
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
// The m_counter is an array of indexes to access the current and 3 different "pending" states.
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
class AtomicArrayState {

protected:
    // m_state[0] ==> current
    // m_state[1] ==> pending
    // m_state[2] ==> request

    std::array<T, 3> m_state;
    alignas(UInt32) alignas(AtomicArrayCounter) AtomicArrayCounter m_counter;

    UInt32 writeNextStateStartAux(std::int32_t state, bool* result)
    {
        AtomicArrayCounter oldVal;
        AtomicArrayCounter newVal;
        UInt32 curIndex;
        UInt32 nextIndex;
        bool needCopy;
        do {
            oldVal = loadCounter1(m_counter);
            newVal = oldVal;
            *result = getIndex1(newVal, state);
            curIndex = getIndex1(newVal, 0);
            nextIndex = swapIndex1(oldVal, state);
            needCopy = (getIndex1(newVal, state) == 0); // Written = false, switch just occurred
            setIndex1(newVal, state, 0); // Written = false, invalidate state
        } while (!cAS(longVal1(oldVal), longVal1(newVal), (UInt32*)&m_counter));
        if (needCopy)
            memcpy(&m_state[nextIndex], &m_state[curIndex], sizeof(T));
        return nextIndex;
    }

    void writeNextStateStopAux(std::int32_t state)
    {
        AtomicArrayCounter oldVal;
        AtomicArrayCounter newVal;
        do {
            oldVal = loadCounter1(m_counter);
            newVal = oldVal;
            setIndex1(newVal, state, 1); // Written = true, state becomes "switchable"
        } while (!cAS(longVal1(oldVal), longVal1(newVal), (UInt32*)&m_counter));
    }

public:
    AtomicArrayState()
    {
        static_assert(offsetof(AtomicArrayState, m_counter) % sizeof(m_counter) == 0,
            "m_counter must be aligned within JackAtomicArrayState");
        assert(reinterpret_cast<std::uintptr_t>(&m_counter) % alignof(UInt32) == 0);
        longVal1(m_counter) = 0;
    }

    ~AtomicArrayState() // Not virtual ??
    {
    }

    // \brief Returns the current state : only valid in the RT reader thread

    T* readCurrentState()
    {
        return &m_state[getIndex1(loadCounter1(m_counter), 0)];
    }

    // \brief Returns the current switch counter

    UInt16 getCurrentIndex()
    {
        return getIndex1(loadCounter1(m_counter), 3);
    }

    // \brief Tries to switch to the next state and returns the new current state (either the same as before if case of switch failure or the new one)

    T* trySwitchState(std::int32_t state)
    {
        AtomicArrayCounter oldVal;
        AtomicArrayCounter newVal;
        do {
            oldVal = loadCounter1(m_counter);
            newVal = oldVal;
            if (getIndex1(newVal, state)) { // If state has been written
                setIndex1(newVal, 0, swapIndex1(newVal, state)); // Prepare switch
                setIndex1(newVal, state, 0); // Invalidate the state "state"
                incIndex1(newVal, 3); // Inc switch
            }
        } while (!cAS(longVal1(oldVal), longVal1(newVal), (UInt32*)&m_counter));
        return &m_state[getIndex1(loadCounter1(m_counter), 0)]; // Read the counter again
    }

    // \brief Tries to switch to the next state and returns the new current state (either the same as before if case of switch failure or the new one)

    T* trySwitchState(std::int32_t state, bool* result)
    {
        AtomicArrayCounter oldVal;
        AtomicArrayCounter newVal;
        do {
            oldVal = loadCounter1(m_counter);
            newVal = oldVal;
            if ((*result = getIndex1(newVal, state))) { // If state has been written
                setIndex1(newVal, 0, swapIndex1(newVal, state)); // Prepare switch
                setIndex1(newVal, state, 0); // Invalidate the state "state"
                incIndex1(newVal, 3); // Inc switch
            }
        } while (!cAS(longVal1(oldVal), longVal1(newVal), (UInt32*)&m_counter));
        return &m_state[getIndex1(loadCounter1(m_counter), 0)]; // Read the counter again
    }

    // \brief Start write operation : setup and returns the next state to update, check for recursive write calls.

    T* writeNextStateStart(std::int32_t state)
    {
        bool tmp;
        UInt32 index = writeNextStateStartAux(state, &tmp);
        return &m_state[index];
    }

    T* writeNextStateStart(std::int32_t state, bool* result)
    {
        UInt32 index = writeNextStateStartAux(state, result);
        return &m_state[index];
    }

    // \brief Stop write operation : make the next state ready to be used by the RT thread

    void writeNextStateStop(std::int32_t state)
    {
        writeNextStateStopAux(state);
    }
};

} // namespace jackmini
