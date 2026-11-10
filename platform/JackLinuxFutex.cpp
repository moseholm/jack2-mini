// Copyright (C) 2004-2008 Grame
// Copyright (C) 2016 Filipe Coelho
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

#include "JackLinuxFutex.h"
#include "JackCString.h"
#include "JackConstants.h"
#include "JackError.h"
#include "JackTools.h"
#include "promiscuous.hpp"
#include <cstdint>
#include <fcntl.h>
#include <linux/futex.h>
#include <stdio.h>
#include <sys/mman.h>
#include <syscall.h>
#include <array>
#include <climits>

#if !defined(SYS_futex) && defined(SYS_futex_time64)
#define SYS_futex SYS_futex_time64
#endif

namespace jackmini {

LinuxFutex::LinuxFutex()
    : Synchro()
    , m_sharedMem(-1)
    , m_futex(nullptr)
    , m_private(false)
{
    const char* promiscuous = getenv("JACK_PROMISCUOUS_SERVER");
    m_promiscuous = (promiscuous != nullptr);
    m_promiscuousGid = jackmini::groupToGid(promiscuous);
}

void LinuxFutex::buildName(const char* clientName, const char* serverName, char* res, std::int32_t size)
{
    std::array<char, kSyncMaxNameSize + 1> extClientName { };
    Tools::rewriteName(clientName, extClientName.data());
    if (m_promiscuous) {
        jackmini::formatCString(std::span<char>(res, size), "jack_sem.{}_{}", serverName, extClientName.data());
    } else {
        jackmini::formatCString(std::span<char>(res, size), "jack_sem.{}_{}_{}", Tools::getUID(), serverName, extClientName.data());
    }
}

bool LinuxFutex::signal()
{
    if (!m_futex) {
        jack_error("JackLinuxFutex::Signal name = %s already deallocated!!", m_name);
        return false;
    }

    if (m_flush) {
        return true;
    }

    if (!__sync_bool_compare_and_swap(&m_futex->m_futex, 0, 1)) {
        // already unlocked, do not wake futex
        if (!m_futex->m_internal)
            return true;
    }

    ::syscall(SYS_futex, m_futex, m_futex->m_internal ? FUTEX_WAKE_PRIVATE : FUTEX_WAKE, 1, nullptr, nullptr, 0);
    return true;
}

bool LinuxFutex::signalAll()
{
    return signal();
}

bool LinuxFutex::wait()
{
    if (!m_futex) {
        jack_error("JackLinuxFutex::Wait name = %s already deallocated!!", m_name);
        return false;
    }

    if (m_futex->m_needsChange) {
        m_futex->m_needsChange = false;
        m_futex->m_internal = !m_futex->m_internal;
    }

    const std::int32_t waitMode = m_futex->m_internal ? FUTEX_WAIT_PRIVATE : FUTEX_WAIT;

    for (;;) {
        if (__sync_bool_compare_and_swap(&m_futex->m_futex, 1, 0))
            return true;

        if (::syscall(SYS_futex, m_futex, waitMode, 0, nullptr, nullptr, 0) != 0)
            if (errno != EAGAIN && errno != EINTR)
                return false;
    }
}

bool LinuxFutex::timedWait(std::int64_t usec)
{
    if (usec == LONG_MAX)
        return wait();

    if (!m_futex) {
        jack_error("JackLinuxFutex::TimedWait name = %s already deallocated!!", m_name);
        return false;
    }

    if (m_futex->m_needsChange) {
        m_futex->m_needsChange = false;
        m_futex->m_internal = !m_futex->m_internal;
    }

    const uint secs = usec / 1000000;
    const std::int32_t nsecs = (usec % 1000000) * 1000;

    const timespec timeout = { static_cast<time_t>(secs), nsecs };
    const std::int32_t waitMode = m_futex->m_internal ? FUTEX_WAIT_PRIVATE : FUTEX_WAIT;

    for (;;) {
        if (__sync_bool_compare_and_swap(&m_futex->m_futex, 1, 0))
            return true;

        if (::syscall(SYS_futex, m_futex, waitMode, 0, &timeout, nullptr, 0) != 0)
            if (errno != EAGAIN && errno != EINTR)
                return false;
    }
}

// Server side : publish the futex in the global namespace
bool LinuxFutex::allocate(const char* name, const char* serverName, std::int32_t value, bool internal)
{
    buildName(name, serverName, m_name.data(), sizeof(m_name));
    jack_log("JackLinuxFutex::Allocate name = %s val = %ld", m_name, value);

    if ((m_sharedMem = shm_open(m_name.data(), O_CREAT | O_RDWR, 0777)) < 0) {
        jack_error("Allocate: can't check in named futex name = %s err = %s", m_name, strerror(errno));
        return false;
    }

    if (ftruncate(m_sharedMem, sizeof(FutexData)) != 0) {
        jack_error("Allocate: can't set shared memory size in named futex name = %s err = %s", m_name, strerror(errno));
        return false;
    }

    if (m_promiscuous && (jackmini::setPromiscuousPerms(m_sharedMem, m_name.data(), m_promiscuousGid) < 0)) {
        close(m_sharedMem);
        m_sharedMem = -1;
        shm_unlink(m_name.data());
        return false;
    }

    FutexData* futex = (FutexData*)mmap(nullptr, sizeof(FutexData), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_LOCKED, m_sharedMem, 0);

    // try without MAP_LOCKED
    if (futex == nullptr || futex == MAP_FAILED) {
        futex = (FutexData*)mmap(nullptr, sizeof(FutexData), PROT_READ | PROT_WRITE, MAP_SHARED, m_sharedMem, 0);
    }

    if (futex == nullptr || futex == MAP_FAILED) {
        jack_error("Allocate: can't check in named futex name = %s err = %s", m_name, strerror(errno));
        close(m_sharedMem);
        m_sharedMem = -1;
        shm_unlink(m_name.data());
        return false;
    }

    m_private = internal;

    futex->m_futex = value;
    futex->m_internal = internal;
    futex->m_wasInternal = internal;
    futex->m_needsChange = false;
    futex->m_externalCount = 0;
    m_futex = futex;
    return true;
}

// Client side : get the published futex from server
bool LinuxFutex::connect(const char* name, const char* serverName)
{
    buildName(name, serverName, m_name.data(), sizeof(m_name));
    jack_log("JackLinuxFutex::Connect name = %s", m_name);

    // Temporary...
    if (m_futex) {
        jack_log("Already connected name = %s", name);
        return true;
    }

    if ((m_sharedMem = shm_open(m_name.data(), O_RDWR, 0)) < 0) {
        jack_error("Connect: can't connect named futex name = %s err = %s", m_name, strerror(errno));
        return false;
    }

    FutexData* futex = (FutexData*)mmap(nullptr, sizeof(FutexData), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_LOCKED, m_sharedMem, 0);

    // try without MAP_LOCKED
    if (futex == nullptr || futex == MAP_FAILED) {
        futex = (FutexData*)mmap(nullptr, sizeof(FutexData), PROT_READ | PROT_WRITE, MAP_SHARED, m_sharedMem, 0);
    }

    if (futex == nullptr || futex == MAP_FAILED) {
        jack_error("Connect: can't connect named futex name = %s err = %s", m_name, strerror(errno));
        close(m_sharedMem);
        m_sharedMem = -1;
        return false;
    }

    if (!m_private && futex->m_wasInternal) {
        const char* externalSync = getenv("JACK_INTERNAL_CLIENT_SYNC");

        if (externalSync != nullptr && strstr(m_name.data(), externalSync) != nullptr && ++futex->m_externalCount == 1) {
            jack_error("Note: client %s running as external client temporarily", m_name);
            futex->m_needsChange = true;
        }
    }

    m_futex = futex;
    return true;
}

bool LinuxFutex::connectInput(const char* name, const char* serverName)
{
    return connect(name, serverName);
}

bool LinuxFutex::connectOutput(const char* name, const char* serverName)
{
    return connect(name, serverName);
}

bool LinuxFutex::disconnect()
{
    if (!m_futex) {
        return true;
    }

    if (!m_private && m_futex->m_wasInternal) {
        const char* externalSync = getenv("JACK_INTERNAL_CLIENT_SYNC");

        if (externalSync != nullptr && strstr(m_name.data(), externalSync) != nullptr && --m_futex->m_externalCount == 0) {
            jack_error("Note: client %s now running as internal client again", m_name);
            m_futex->m_needsChange = true;
        }
    }

    munmap(m_futex, sizeof(FutexData));
    m_futex = nullptr;

    close(m_sharedMem);
    m_sharedMem = -1;
    return true;
}

// Server side : destroy the futex
void LinuxFutex::destroy()
{
    if (!m_futex) {
        return;
    }

    munmap(m_futex, sizeof(FutexData));
    m_futex = nullptr;

    close(m_sharedMem);
    m_sharedMem = -1;

    shm_unlink(m_name.data());
}

} // namespace jackmini
