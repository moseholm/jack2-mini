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


struct AtomicCounter {
    union {
        struct {
            UInt16 m_shortVal1; // Cur
            UInt16 m_shortVal2; // Next
        } m_scounter;
        UInt32 m_longVal;
    } m_info;

    AtomicCounter()
    {
        m_info.m_longVal = 0;
    }

    AtomicCounter(const AtomicCounter& obj)
    {
        m_info.m_longVal = obj.m_info.m_longVal;
    }
    AtomicCounter(volatile const AtomicCounter& obj)
    {
        m_info.m_longVal = obj.m_info.m_longVal;
    }

    AtomicCounter(volatile AtomicCounter& obj)
    {
        m_info.m_longVal = obj.m_info.m_longVal;
    }

    AtomicCounter& operator=(const AtomicCounter& obj)
    {
        m_info.m_longVal = obj.m_info.m_longVal;
        return *this;
    }
    AtomicCounter& operator=(AtomicCounter& obj)
    {
        m_info.m_longVal = obj.m_info.m_longVal;
        return *this;
    }

    AtomicCounter& operator=(volatile AtomicCounter& obj)
    {
        m_info.m_longVal = obj.m_info.m_longVal;
        return *this;
    }
};

template <typename T>
constexpr auto& longVal(T&& e)
{
    return e.m_info.m_longVal;
}

template <typename T>
constexpr auto& curIndexOf(T&& e)
{
    return e.m_info.m_scounter.m_shortVal1;
}

template <typename T>
constexpr auto& nextIndexOf(T&& e)
{
    return e.m_info.m_scounter.m_shortVal2;
}

template <typename T>
constexpr std::int32_t curArrayIndex(T&& e)
{
    return curIndexOf(e) & 0x0001;
}

template <typename T>
constexpr std::int32_t nextArrayIndex(T&& e)
{
    return (curIndexOf(e) + 1) & 0x0001;
}

// \brief Loads a shared counter atomically.  Every read of the counter goes through this, so the 16-bit fields are never read separately from the 32-bit update.
static inline AtomicCounter loadCounter(const AtomicCounter& counter)
{
    AtomicCounter result;
    longVal(result) = std::atomic_ref<UInt32>(const_cast<UInt32&>(counter.m_info.m_longVal)).load();
    return result;
}

// \brief A class to handle two states (switching from one to the other) in a lock-free manner

// CHECK livelock


template <class T>
class AtomicState {

protected:
    std::array<T, 2> m_state;
    alignas(UInt32) alignas(AtomicCounter) AtomicCounter m_counter;
    SInt32 m_callWriteCounter;

    UInt32 writeNextStateStartAux()
    {
        AtomicCounter oldVal;
        AtomicCounter newVal;
        UInt32 curIndex;
        UInt32 nextIndex;
        bool needCopy;
        do {
            oldVal = loadCounter(m_counter);
            newVal = oldVal;
            curIndex = curArrayIndex(newVal);
            nextIndex = nextArrayIndex(newVal);
            needCopy = (curIndexOf(newVal) == nextIndexOf(newVal));
            nextIndexOf(newVal) = curIndexOf(newVal); // Invalidate next index
        } while (!cAS(longVal(oldVal), longVal(newVal), (UInt32*)&m_counter));
        if (needCopy)
            memcpy(&m_state[nextIndex], &m_state[curIndex], sizeof(T));
        return nextIndex;
    }

    void writeNextStateStopAux()
    {
        AtomicCounter oldVal;
        AtomicCounter newVal;
        do {
            oldVal = loadCounter(m_counter);
            newVal = oldVal;
            nextIndexOf(newVal)++; // Set next index
        } while (!cAS(longVal(oldVal), longVal(newVal), (UInt32*)&m_counter));
    }

public:
    AtomicState()
    {
        static_assert(offsetof(AtomicState, m_counter) % sizeof(m_counter) == 0,
            "m_counter must be aligned within JackAtomicState");
        assert(reinterpret_cast<std::uintptr_t>(&m_counter) % alignof(UInt32) == 0);
        longVal(m_counter) = 0;
        m_callWriteCounter = 0;
    }

    ~AtomicState() // Not virtual ??
    {
    }

    // \brief Returns the current state : only valid in the RT reader thread
    T* readCurrentState()
    {
        return &m_state[curArrayIndex(loadCounter(m_counter))];
    }

    // \brief Returns the current state index
    UInt16 getCurrentIndex()
    {
        return curIndexOf(loadCounter(m_counter));
    }

    // \brief Tries to switch to the next state and returns the new current state (either the same as before if case of switch failure or the new one)
    T* trySwitchState()
    {
        AtomicCounter oldVal;
        AtomicCounter newVal;
        do {
            oldVal = loadCounter(m_counter);
            newVal = oldVal;
            curIndexOf(newVal) = nextIndexOf(newVal); // Prepare switch
        } while (!cAS(longVal(oldVal), longVal(newVal), (UInt32*)&m_counter));
        return &m_state[curArrayIndex(loadCounter(m_counter))]; // Read the counter again
    }

    // \brief Tries to switch to the next state and returns the new current state (either the same as before if case of switch failure or the new one)
    T* trySwitchState(bool* result)
    {
        AtomicCounter oldVal;
        AtomicCounter newVal;
        do {
            oldVal = loadCounter(m_counter);
            newVal = oldVal;
            *result = (curIndexOf(newVal) != nextIndexOf(newVal));
            curIndexOf(newVal) = nextIndexOf(newVal); // Prepare switch
        } while (!cAS(longVal(oldVal), longVal(newVal), (UInt32*)&m_counter));
        return &m_state[curArrayIndex(loadCounter(m_counter))]; // Read the counter again
    }

    // \brief Start write operation : setup and returns the next state to update, check for recursive write calls.
    T* writeNextStateStart()
    {
        UInt32 nextIndex = (m_callWriteCounter++ == 0)
            ? writeNextStateStartAux()
            : nextArrayIndex(loadCounter(m_counter)); // We are inside a wrapping WriteNextStateStart call, NextArrayIndex can be read safely
        return &m_state[nextIndex];
    }

    // \brief Stop write operation : make the next state ready to be used by the RT thread
    void writeNextStateStop()
    {
        if (--m_callWriteCounter == 0)
            writeNextStateStopAux();
    }

    bool isPendingChange()
    {
        const AtomicCounter counter = loadCounter(m_counter);
        return curIndexOf(counter) != nextIndexOf(counter);
    }

    // // Single writer : write methods get the *next* state to be updated
    // void TestWriteMethod()
    // {
    // T* state = WriteNextStateStart();
    // ......
    // ......
    // WriteNextStateStop();
    // }
    //
    // // First RT call possibly switch state
    // void TestReadRTMethod1()
    // {
    // T* state = TrySwitchState();
    // ......
    // ......
    // }
    //
    // // Other RT methods can safely use the current state during the *same* RT cycle
    // void TestReadRTMethod2()
    // {
    // T* state = ReadCurrentState();
    // ......
    // ......
    // }
    //
    // // Non RT read methods : must check state coherency
    // void TestReadMethod()
    // {
    // T* state;
    // UInt16 cur_index;
    // UInt16 next_index = GetCurrentIndex();
    // do {
    // cur_index = next_index;
    // state = ReadCurrentState();
    //
    // ......
    // ......
    //
    // next_index = GetCurrentIndex();
    // } while (cur_index != next_index);
    // }
};

} // namespace jackmini
