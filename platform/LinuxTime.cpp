// Copyright (C) 2001-2003 Paul Davis
// Copyright (C) 2005 Jussi Laako
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

#include "Constants.h"
#include "Error.h"
#include "Time.h"
#include "Types.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <ctime>

#if defined(__gnu_linux__) && (defined(__i386__) || defined(__x86_64__))
#define HPET_SUPPORT
#endif

namespace jackmini {

namespace {

#ifdef HPET_SUPPORT

constexpr std::size_t kHpetMapSize = 1024;
constexpr std::size_t kHpetCaps = 0x000;
constexpr std::size_t kHpetPeriod = 0x004;
constexpr std::size_t kHpetCounter = 0x0f0;
constexpr std::uint32_t kHpetCapsCounter64 = 1u << 13;

#if defined(__x86_64__)
using HpetCounter = std::uint64_t;
#else
using HpetCounter = std::uint32_t;
#endif

// The HPET is mapped once and kept until the process exits. now() is read from the realtime cycle, so it does not
// allocate or lock.
class Hpet {
public:
    Hpet() = default;
    Hpet(const Hpet&) = delete;
    Hpet& operator=(const Hpet&) = delete;

    ~Hpet()
    {
        if (m_ptr) {
            munmap(m_ptr, kHpetMapSize);
        }
        if (m_fd >= 0) {
            close(m_fd);
        }
    }

    bool open()
    {
        m_fd = ::open("/dev/hpet", O_RDONLY);
        if (m_fd < 0) {
            jack_error("This system has no accessible HPET device (%s)", strerror(errno));
            return false;
        }

        void* map = mmap(nullptr, kHpetMapSize, PROT_READ, MAP_SHARED, m_fd, 0);
        if (map == MAP_FAILED) {
            jack_error("This system has no mappable HPET device (%s)", strerror(errno));
            close(m_fd);
            m_fd = -1;
            return false;
        }
        m_ptr = static_cast<std::byte*>(map);

        // The period is read once. It is assumed to be constant.
        m_period = readRegister<std::uint32_t>(kHpetPeriod);
        const auto caps = readRegister<std::uint32_t>(kHpetCaps);
        m_wrap = ((caps & kHpetCapsCounter64) && (sizeof(HpetCounter) == sizeof(std::uint64_t))) ? 0 : (std::uint64_t(1) << 32);
        return true;
    }

    TimeC now()
    {
        const auto counter = readRegister<HpetCounter>(kHpetCounter);
        if (counter < m_previous) {
            m_offset += m_wrap;
        }
        m_previous = counter;
        const long double time = static_cast<long double>(m_offset + counter)
            * static_cast<long double>(m_period) * 1e-9L;
        return static_cast<TimeC>(time + 0.5L);
    }

private:
    template <typename T>
    T readRegister(std::size_t offset) const
    {
        return *reinterpret_cast<const volatile T*>(m_ptr + offset);
    }

    std::int32_t m_fd = -1;
    std::byte* m_ptr = nullptr;
    std::uint32_t m_period = 0; // femtoseconds
    std::uint64_t m_offset = 0;
    std::uint64_t m_wrap = 0;
    HpetCounter m_previous = 0;
};

Hpet gHpet;

bool openHpet()
{
    return gHpet.open();
}

TimeC getMicrosecondsFromHpet()
{
    return gHpet.now();
}

#else

bool openHpet()
{
    jack_error("This version of JACK or this computer does not have HPET support.\n"
               "Please choose a different clock source.");
    return false;
}

TimeC getMicrosecondsFromHpet()
{
    // Never called: openHpet() fails on this platform.
    return 0;
}

#endif // HPET_SUPPORT

TimeC getMicrosecondsFromSystem()
{
#ifdef CLOCK_MONOTONIC_RAW
    constexpr clockid_t kClock = CLOCK_MONOTONIC_RAW;
#else
    constexpr clockid_t kClock = CLOCK_MONOTONIC;
#endif
    timespec time { };
    clock_gettime(kClock, &time);
    const auto elapsed = std::chrono::seconds(time.tv_sec) + std::chrono::nanoseconds(time.tv_nsec);
    return static_cast<TimeC>(std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count());
}

TimeC (*gCurrentClock)() = nullptr;

} // end of anonymous namespace

void sleepMicroseconds(std::int64_t usec)
{
    usleep(usec);
}

void initTime()
{
    // Nothing to do: the system clock needs no setup.
}

void endTime()
{
}

void setClockSource(TimerType source)
{
    jack_log("Clock source : %s", clockSourceName(source));

    switch (source) {
    case kTimerHpet:
        if (openHpet()) {
            gCurrentClock = getMicrosecondsFromHpet;
        } else {
            gCurrentClock = getMicrosecondsFromSystem;
        }
        break;

    case kTimerSystemClock:
    default:
        gCurrentClock = getMicrosecondsFromSystem;
        break;
    }
}

const char* clockSourceName(TimerType source)
{
    switch (source) {
    case kTimerHpet:
        return "hpet";
    case kTimerSystemClock:
        return "system clock via clock_gettime";
    }

    return "unknown";
}

TimeC getMicroseconds()
{
    return gCurrentClock();
}

} // end of namespace jackmini
