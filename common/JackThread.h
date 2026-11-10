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

#include "JackCompilerDeps.h"
#include "JackTypes.h"
#include <cstdint>
#include <atomic>

namespace jackmini {

// \brief The base class for runnable objects, that have an <B> Init </B> and <B> Execute </B> method to be called in a thread.

class JackRunnableInterface {

protected:
    JackRunnableInterface()
    {
    }
    virtual ~JackRunnableInterface()
    {
    }

public:
    virtual bool Init()  // Called once when the thread is started
    {
        return true;
    }
    virtual bool Execute() = 0;  // Must be implemented by subclasses
};

namespace detail {

// \brief The thread base class.

class SERVER_EXPORT JackThreadInterface {

public:
    enum kThreadState { kIdle,
        kStarting,
        kIniting,
        kRunning };

protected:
    JackRunnableInterface* fRunnable;
    std::int32_t fPriority;
    bool fRealTime;
    std::atomic<kThreadState> fStatus;
    std::int32_t fCancellation;

public:
    JackThreadInterface(JackRunnableInterface* runnable, std::int32_t priority, bool real_time, std::int32_t cancellation)
        : fRunnable(runnable)
        , fPriority(priority)
        , fRealTime(real_time)
        , fStatus(kIdle)
        , fCancellation(cancellation)
    {
    }

    kThreadState GetStatus()
    {
        return fStatus;
    }

    void SetParams(UInt64 period, UInt64 computation, UInt64 constraint) // Empty implementation, will only make sense on OSX...
    {
    }

    std::int32_t Start();
    std::int32_t StartSync();
    std::int32_t Kill();
    std::int32_t Stop();
    void Terminate();

    std::int32_t AcquireRealTime(); // Used when called from another thread
    std::int32_t AcquireSelfRealTime(); // Used when called from thread itself

    std::int32_t AcquireRealTime(std::int32_t priority); // Used when called from another thread
    std::int32_t AcquireSelfRealTime(std::int32_t priority); // Used when called from thread itself

    std::int32_t DropRealTime(); // Used when called from another thread
    std::int32_t DropSelfRealTime(); // Used when called from thread itself

    jack_native_thread_t GetThreadID();
    bool IsThread();

    static std::int32_t AcquireRealTimeImp(jack_native_thread_t thread, std::int32_t priority);
    static std::int32_t AcquireRealTimeImp(jack_native_thread_t thread, std::int32_t priority, UInt64 period, UInt64 computation, UInt64 constraint);
    static std::int32_t DropRealTimeImp(jack_native_thread_t thread);
    static std::int32_t StartImp(jack_native_thread_t* thread, std::int32_t priority, std::int32_t realtime, void* (*start_routine)(void*), void* arg);
    static std::int32_t StopImp(jack_native_thread_t thread);
    static std::int32_t KillImp(jack_native_thread_t thread);
};

} // namespace detail

} // namespace jackmini

bool jack_get_thread_realtime_priority_range(std::int32_t* min_ptr, std::int32_t* max_ptr);

bool jack_tls_allocate_key(jack_tls_key* key_ptr);
bool jack_tls_free_key(jack_tls_key key);

bool jack_tls_set(jack_tls_key key, void* data_ptr);
void* jack_tls_get(jack_tls_key key);
