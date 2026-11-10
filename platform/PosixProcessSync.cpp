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

#include "PosixProcessSync.h"
#include "Error.h"
#include <chrono>
#include <cstdint>

namespace jackmini {

// pthread_cond_timedwait() takes an absolute CLOCK_REALTIME deadline, which is
// what std::chrono::system_clock is on Linux.
static timespec realtimeDeadline(std::int64_t usec)
{
    const auto deadline = std::chrono::system_clock::now() + std::chrono::microseconds(usec);
    const auto secs = std::chrono::time_point_cast<std::chrono::seconds>(deadline);
    const auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(deadline - secs);
    return timespec { static_cast<time_t>(secs.time_since_epoch().count()), static_cast<std::int64_t>(nanos.count()) };
}

void PosixProcessSync::signal()
{
    std::int32_t res = pthread_cond_signal(&m_cond);
    if (res != 0) {
        jack_error("JackPosixProcessSync::Signal error err = %s", strerror(res));
    }
}

// TO DO : check thread consistency?
void PosixProcessSync::lockedSignal()
{
    std::int32_t res = pthread_mutex_lock(&m_mutex);
    if (res != 0) {
        jack_error("JackPosixProcessSync::LockedSignal error err = %s", strerror(res));
    }
    res = pthread_cond_signal(&m_cond);
    if (res != 0) {
        jack_error("JackPosixProcessSync::LockedSignal error err = %s", strerror(res));
    }
    res = pthread_mutex_unlock(&m_mutex);
    if (res != 0) {
        jack_error("JackPosixProcessSync::LockedSignal error err = %s", strerror(res));
    }
}

void PosixProcessSync::signalAll()
{
    std::int32_t res = pthread_cond_broadcast(&m_cond);
    if (res != 0) {
        jack_error("JackPosixProcessSync::SignalAll error err = %s", strerror(res));
    }
}

// TO DO : check thread consistency?
void PosixProcessSync::lockedSignalAll()
{
    std::int32_t res = pthread_mutex_lock(&m_mutex);
    if (res != 0) {
        jack_error("JackPosixProcessSync::LockedSignalAll error err = %s", strerror(res));
    }
    res = pthread_cond_broadcast(&m_cond);
    if (res != 0) {
        jack_error("JackPosixProcessSync::LockedSignalAll error err = %s", strerror(res));
    }
    res = pthread_mutex_unlock(&m_mutex);
    if (res != 0) {
        jack_error("JackPosixProcessSync::LockedSignalAll error err = %s", strerror(res));
    }
}

void PosixProcessSync::wait()
{
    ThrowIf(!pthread_equal(pthread_self(), m_owner), Exception("JackPosixProcessSync::Wait: a thread has to have locked a mutex before it can wait"));
    m_owner = 0;

    std::int32_t res = pthread_cond_wait(&m_cond, &m_mutex);
    if (res != 0) {
        jack_error("JackPosixProcessSync::Wait error err = %s", strerror(res));
    } else {
        m_owner = pthread_self();
    }
}

// TO DO : check thread consistency?
void PosixProcessSync::lockedWait()
{
    std::int32_t res;
    res = pthread_mutex_lock(&m_mutex);
    if (res != 0) {
        jack_error("JackPosixProcessSync::LockedWait error err = %s", strerror(res));
    }
    if ((res = pthread_cond_wait(&m_cond, &m_mutex)) != 0) {
        jack_error("JackPosixProcessSync::LockedWait error err = %s", strerror(res));
    }
    res = pthread_mutex_unlock(&m_mutex);
    if (res != 0) {
        jack_error("JackPosixProcessSync::LockedWait error err = %s", strerror(res));
    }
}

bool PosixProcessSync::timedWait(std::int64_t usec)
{
    ThrowIf(!pthread_equal(pthread_self(), m_owner), Exception("JackPosixProcessSync::TimedWait: a thread has to have locked a mutex before it can wait"));
    m_owner = 0;

    std::int32_t res;

    jack_log("JackPosixProcessSync::TimedWait time out = %ld", usec);
    const auto start = std::chrono::steady_clock::now();

    const timespec deadline = realtimeDeadline(usec);
    res = pthread_cond_timedwait(&m_cond, &m_mutex, &deadline);
    if (res != 0 && res != ETIMEDOUT) {
        jack_error("JackPosixProcessSync::TimedWait error usec = %ld err = %s", usec, strerror(res));
    } else {
        m_owner = pthread_self();
    }

    jack_log("JackPosixProcessSync::TimedWait finished delta = %5.1lf",
        std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count());

    return (res == 0);
}

// TO DO : check thread consistency?
bool PosixProcessSync::lockedTimedWait(std::int64_t usec)
{
    std::int32_t res1, res2;

    res1 = pthread_mutex_lock(&m_mutex);
    if (res1 != 0) {
        jack_error("JackPosixProcessSync::LockedTimedWait error err = %s", usec, strerror(res1));
    }

    jack_log("JackPosixProcessSync::TimedWait time out = %ld", usec);
    const auto start = std::chrono::steady_clock::now();

    const timespec deadline = realtimeDeadline(usec);
    res2 = pthread_cond_timedwait(&m_cond, &m_mutex, &deadline);
    if (res2 != 0) {
        jack_error("JackPosixProcessSync::LockedTimedWait error usec = %ld err = %s", usec, strerror(res2));
    }

    res1 = pthread_mutex_unlock(&m_mutex);
    if (res1 != 0) {
        jack_error("JackPosixProcessSync::LockedTimedWait error err = %s", usec, strerror(res1));
    }

    jack_log("JackPosixProcessSync::TimedWait finished delta = %5.1lf",
        std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count());

    return (res2 == 0);
}


} // namespace jackmini
