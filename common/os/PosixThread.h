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

#include "common/Thread.h"
#include <pthread.h>
#include <cstdint>

namespace jackmini {

// use 512KB stack per thread - the default is way too high to be feasible
// with mlockall() on many systems
inline constexpr std::int32_t kThreadStack = 524288;

// \brief The POSIX thread base class.

class SERVER_EXPORT PosixThread : public detail::ThreadInterface {

protected:
    pthread_t m_thread;
    static void* threadHandler(void* arg);

public:
    PosixThread(RunnableInterface* runnable, bool realTime, std::int32_t priority, std::int32_t cancellation)
        : ThreadInterface(runnable, priority, realTime, cancellation)
        , m_thread((pthread_t) nullptr)
    {
    }
    PosixThread(RunnableInterface* runnable, std::int32_t cancellation = PTHREAD_CANCEL_ASYNCHRONOUS)
        : ThreadInterface(runnable, 0, false, cancellation)
        , m_thread((pthread_t) nullptr)
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

    pthread_t getThreadID();
    bool isThread();

    static std::int32_t acquireRealTimeImp(pthread_t thread, std::int32_t priority);
    static std::int32_t acquireRealTimeImp(pthread_t thread, std::int32_t priority, UInt64 period, UInt64 computation, UInt64 constraint)
    {
        return PosixThread::acquireRealTimeImp(thread, priority);
    }
    static std::int32_t dropRealTimeImp(pthread_t thread);
    static std::int32_t startImp(pthread_t* thread, std::int32_t priority, std::int32_t realtime, void* (*startRoutine)(void*), void* arg);
    static std::int32_t stopImp(pthread_t thread);
    static std::int32_t killImp(pthread_t thread);
};

SERVER_EXPORT void threadExit();

} // namespace jackmini
