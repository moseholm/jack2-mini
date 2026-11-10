// Copyright (C) 2001 Paul Davis
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

#include "CompilerDeps.h"
#include "Types.h"
#include <atomic>
#include <cstdint>

namespace jackmini {

// \brief The base class for runnable objects, that have an <B> Init </B> and <B> Execute </B> method to be called in a thread.

class RunnableInterface {

protected:
    RunnableInterface()
    {
    }
    virtual ~RunnableInterface()
    {
    }

public:
    virtual bool init() // Called once when the thread is started
    {
        return true;
    }
    virtual bool execute() = 0; // Must be implemented by subclasses
};

namespace detail {

// \brief The thread base class.

class SERVER_EXPORT ThreadInterface {

public:
    enum kThreadState { kIdle,
        kStarting,
        kIniting,
        kRunning };

protected:
    RunnableInterface* m_runnable;
    std::int32_t m_priority;
    bool m_realTime;
    std::atomic<kThreadState> m_status;
    std::int32_t m_cancellation;

public:
    ThreadInterface(RunnableInterface* runnable, std::int32_t priority, bool realTime, std::int32_t cancellation)
        : m_runnable(runnable)
        , m_priority(priority)
        , m_realTime(realTime)
        , m_status(kIdle)
        , m_cancellation(cancellation)
    {
    }

    kThreadState getStatus()
    {
        return m_status;
    }

    void setParams(UInt64 period, UInt64 computation, UInt64 constraint) // Empty implementation, will only make sense on OSX...
    {
    }

    std::int32_t start();
    std::int32_t startSync();
    std::int32_t kill();
    std::int32_t stop();
    void terminate();

    std::int32_t acquireRealTime(); // Used when called from another thread
    std::int32_t acquireSelfRealTime(); // Used when called from thread itself

    std::int32_t acquireRealTime(std::int32_t priority); // Used when called from another thread
    std::int32_t acquireSelfRealTime(std::int32_t priority); // Used when called from thread itself

    std::int32_t dropRealTime(); // Used when called from another thread
    std::int32_t dropSelfRealTime(); // Used when called from thread itself

    NativeThreadC getThreadID();
    bool isThread();

    static std::int32_t acquireRealTimeImp(NativeThreadC thread, std::int32_t priority);
    static std::int32_t acquireRealTimeImp(NativeThreadC thread, std::int32_t priority, UInt64 period, UInt64 computation, UInt64 constraint);
    static std::int32_t dropRealTimeImp(NativeThreadC thread);
    static std::int32_t startImp(NativeThreadC* thread, std::int32_t priority, std::int32_t realtime, void* (*startRoutine)(void*), void* arg);
    static std::int32_t stopImp(NativeThreadC thread);
    static std::int32_t killImp(NativeThreadC thread);
};

} // namespace detail

} // namespace jackmini

bool getThreadRealtimePriorityRange(std::int32_t* minPtr, std::int32_t* maxPtr);

bool tlsAllocateKey(TlsKey* keyPtr);
bool tlsFreeKey(TlsKey key);

bool tlsSet(TlsKey key, void* dataPtr);
void* tlsGet(TlsKey key);
