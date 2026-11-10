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

#include "JackPosixThread.h"
#include "JackError.h"
#include "JackGlobals.h"
#include "JackTime.h"
#include <cstdint>
#include <string.h> // for memset
#include <unistd.h> // for _POSIX_PRIORITY_SCHEDULING check

// #define JACK_SCHED_POLICY SCHED_RR
inline constexpr std::int32_t kSchedPolicy = SCHED_FIFO;

#ifndef SCHED_RESET_ON_FORK
#define SCHED_RESET_ON_FORK 0x40000000
#endif

namespace jackmini {

void* PosixThread::threadHandler(void* arg)
{
    PosixThread* obj = (PosixThread*)arg;
    RunnableInterface* runnable = obj->m_runnable;
    std::int32_t err;

    if ((err = pthread_setcanceltype(obj->m_cancellation, nullptr)) != 0) {
        jack_error("pthread_setcanceltype err = %s", strerror(err));
    }

    // Signal creation thread when started with StartSync
    jack_log("JackPosixThread::ThreadHandler : start");
    obj->m_status = kIniting;

    // Call Init method
    if (!runnable->init()) {
        jack_error("Thread init fails: thread quits");
        return 0;
    }

    obj->m_status = kRunning;

    // If Init succeed, start the thread loop
    bool res = true;
    while (obj->m_status == kRunning && res) {
        res = runnable->execute();
    }

    jack_log("JackPosixThread::ThreadHandler : exit");
    pthread_exit(0);
    return 0; // never reached
}

std::int32_t PosixThread::start()
{
    m_status = kStarting;

    // Check if the thread was correctly started
    if (startImp(&m_thread, m_priority, m_realTime, threadHandler, this) < 0) {
        m_status = kIdle;
        return -1;
    } else {
        return 0;
    }
}

std::int32_t PosixThread::startSync()
{
    m_status = kStarting;

    if (startImp(&m_thread, m_priority, m_realTime, threadHandler, this) < 0) {
        m_status = kIdle;
        return -1;
    } else {
        std::int32_t count = 0;
        while (m_status == kStarting && ++count < 1000) {
            sleepMicroseconds(1000);
        }
        return (count == 1000) ? -1 : 0;
    }
}

std::int32_t PosixThread::startImp(NativeThreadC* thread, std::int32_t priority, std::int32_t realtime, void* (*startRoutine)(void*), void* arg)
{
    pthread_attr_t attributes;
    struct sched_param rtParam;
    pthread_attr_init(&attributes);
    std::int32_t res;

    if ((res = pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_JOINABLE))) {
        jack_error("Cannot request joinable thread creation for thread res = %d", res);
        return -1;
    }

    if ((res = pthread_attr_setscope(&attributes, PTHREAD_SCOPE_SYSTEM))) {
        jack_error("Cannot set scheduling scope for thread res = %d", res);
        return -1;
    }

    if (realtime) {

        jack_log("JackPosixThread::StartImp : create RT thread");

        if ((res = pthread_attr_setinheritsched(&attributes, PTHREAD_EXPLICIT_SCHED))) {
            jack_error("Cannot request explicit scheduling for RT thread res = %d", res);
            return -1;
        }

        if ((res = pthread_attr_setschedpolicy(&attributes, kSchedPolicy))) {
            jack_error("Cannot set RR scheduling class for RT thread res = %d", res);
            return -1;
        }

        rtParam = { };
        rtParam.sched_priority = priority;

        if ((res = pthread_attr_setschedparam(&attributes, &rtParam))) {
            jack_error("Cannot set scheduling priority for RT thread res = %d", res);
            return -1;
        }

    } else {
        jack_log("JackPosixThread::StartImp : create non RT thread");
        if ((res = pthread_attr_setinheritsched(&attributes, PTHREAD_EXPLICIT_SCHED))) {
            jack_log("Cannot request explicit scheduling for non RT thread res = %d", res);
        }
    }

    if ((res = pthread_attr_setstacksize(&attributes, kThreadStack))) {
        jack_error("Cannot set thread stack size res = %d", res);
        return -1;
    }

    if ((res = Globals::s_jackThreadCreator(thread, &attributes, startRoutine, arg))) {
        jack_error("Cannot create thread res = %d", res);
        return -1;
    }

    pthread_attr_destroy(&attributes);
    return 0;
}

std::int32_t PosixThread::kill()
{
    if (m_thread != (NativeThreadC)nullptr) { // If thread has been started
        jack_log("JackPosixThread::Kill");
        void* status;
        pthread_cancel(m_thread);
        pthread_join(m_thread, &status);
        m_status = kIdle;
        m_thread = (NativeThreadC)nullptr;
        return 0;
    } else {
        return -1;
    }
}

std::int32_t PosixThread::stop()
{
    if (m_thread != (NativeThreadC)nullptr) { // If thread has been started
        jack_log("JackPosixThread::Stop");
        void* status;
        m_status = kIdle; // Request for the thread to stop
        pthread_join(m_thread, &status);
        m_thread = (NativeThreadC)nullptr;
        return 0;
    } else {
        return -1;
    }
}

std::int32_t PosixThread::killImp(NativeThreadC thread)
{
    if (thread != (NativeThreadC)nullptr) { // If thread has been started
        jack_log("JackPosixThread::Kill");
        void* status;
        pthread_cancel(thread);
        pthread_join(thread, &status);
        return 0;
    } else {
        return -1;
    }
}

std::int32_t PosixThread::stopImp(NativeThreadC thread)
{
    if (thread != (NativeThreadC)nullptr) { // If thread has been started
        jack_log("JackPosixThread::Stop");
        void* status;
        pthread_join(thread, &status);
        return 0;
    } else {
        return -1;
    }
}

std::int32_t PosixThread::acquireRealTime()
{
    return (m_thread != (NativeThreadC)nullptr) ? acquireRealTimeImp(m_thread, m_priority) : -1;
}

std::int32_t PosixThread::acquireSelfRealTime()
{
    return acquireRealTimeImp(pthread_self(), m_priority);
}

std::int32_t PosixThread::acquireRealTime(std::int32_t priority)
{
    m_priority = priority;
    return acquireRealTime();
}

std::int32_t PosixThread::acquireSelfRealTime(std::int32_t priority)
{
    m_priority = priority;
    return acquireSelfRealTime();
}
std::int32_t PosixThread::acquireRealTimeImp(NativeThreadC thread, std::int32_t priority)
{
    struct sched_param rtparam;
    std::int32_t res;
    rtparam = { };
    rtparam.sched_priority = priority;

    jack_log("JackPosixThread::AcquireRealTimeImp priority = %d", priority);

    if ((res = pthread_setschedparam(thread, kSchedPolicy, &rtparam)) == 0)
        return 0;

#ifdef SCHED_RESET_ON_FORK
    jack_log("pthread_setschedparam() failed (%d), trying with SCHED_RESET_ON_FORK.", res);
    if ((res = pthread_setschedparam(thread, kSchedPolicy | SCHED_RESET_ON_FORK, &rtparam)) == 0)
        return 0;
#endif

    jack_error("Cannot use real-time scheduling (RR/%d)"
               " (%d: %s)",
        rtparam.sched_priority, res,
        strerror(res));
    return -1;
}

std::int32_t PosixThread::dropRealTime()
{
    return (m_thread != (NativeThreadC)nullptr) ? dropRealTimeImp(m_thread) : -1;
}

std::int32_t PosixThread::dropSelfRealTime()
{
    return dropRealTimeImp(pthread_self());
}

std::int32_t PosixThread::dropRealTimeImp(NativeThreadC thread)
{
    struct sched_param rtparam;
    std::int32_t res;
    rtparam = { };
    rtparam.sched_priority = 0;

    if ((res = pthread_setschedparam(thread, SCHED_OTHER, &rtparam)) != 0) {
        jack_error("Cannot switch to normal scheduling priority(%s)", strerror(errno));
        return -1;
    }
    return 0;
}

NativeThreadC PosixThread::getThreadID()
{
    return m_thread;
}

bool PosixThread::isThread()
{
    return pthread_self() == m_thread;
}

void PosixThread::terminate()
{
    jack_log("JackPosixThread::Terminate");
    pthread_exit(0);
}

SERVER_EXPORT void threadExit()
{
    jack_log("ThreadExit");
    pthread_exit(0);
}

} // namespace jackmini

bool getThreadRealtimePriorityRange(std::int32_t* minPtr, std::int32_t* maxPtr)
{
#if defined(_POSIX_PRIORITY_SCHEDULING)
    std::int32_t min, max;

    min = sched_get_priority_min(kSchedPolicy);
    if (min == -1) {
        jack_error("sched_get_priority_min() failed.");
        return false;
    }

    max = sched_get_priority_max(kSchedPolicy);
    if (max == -1) {
        jack_error("sched_get_priority_max() failed.");
        return false;
    }

    *minPtr = min;
    *maxPtr = max;

    return true;
#else
    return false;
#endif
}

bool tlsAllocateKey(TlsKey* keyPtr)
{
    std::int32_t ret;

    ret = pthread_key_create(keyPtr, nullptr);
    if (ret != 0) {
        jack_error("pthread_key_create() failed with error %d", ret);
        return false;
    }

    return true;
}

bool tlsFreeKey(TlsKey key)
{
    std::int32_t ret;

    ret = pthread_key_delete(key);
    if (ret != 0) {
        jack_error("pthread_key_delete() failed with error %d", ret);
        return false;
    }

    return true;
}

bool tlsSet(TlsKey key, void* dataPtr)
{
    std::int32_t ret;

    ret = pthread_setspecific(key, (const void*)dataPtr);
    if (ret != 0) {
        jack_error("pthread_setspecific() failed with error %d", ret);
        return false;
    }

    return true;
}

void* tlsGet(TlsKey key)
{
    return pthread_getspecific(key);
}
