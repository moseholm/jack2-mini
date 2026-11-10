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

#include "JackConstants.h"
#include "JackError.h"
#include "JackTime.h"
#include "JackTypes.h"

#include <cstddef>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <cerrno>
#include <chrono>
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
        if (fPtr) {
            munmap(fPtr, kHpetMapSize);
        }
        if (fFd >= 0) {
            close(fFd);
        }
    }

    bool open()
    {
        fFd = ::open("/dev/hpet", O_RDONLY);
        if (fFd < 0) {
            jack_error("This system has no accessible HPET device (%s)", strerror(errno));
            return false;
        }

        void* map = mmap(nullptr, kHpetMapSize, PROT_READ, MAP_SHARED, fFd, 0);
        if (map == MAP_FAILED) {
            jack_error("This system has no mappable HPET device (%s)", strerror(errno));
            close(fFd);
            fFd = -1;
            return false;
        }
        fPtr = static_cast<std::byte*>(map);

        // The period is read once. It is assumed to be constant.
        fPeriod = read_register<std::uint32_t>(kHpetPeriod);
        const auto caps = read_register<std::uint32_t>(kHpetCaps);
        fWrap = ((caps & kHpetCapsCounter64) && (sizeof(HpetCounter) == sizeof(std::uint64_t))) ? 0 : (std::uint64_t(1) << 32);
        return true;
    }

    jack_time_t now()
    {
        const auto counter = read_register<HpetCounter>(kHpetCounter);
        if (counter < fPrevious) {
            fOffset += fWrap;
        }
        fPrevious = counter;
        const long double time = static_cast<long double>(fOffset + counter)
            * static_cast<long double>(fPeriod) * 1e-9L;
        return static_cast<jack_time_t>(time + 0.5L);
    }

private:
    template <typename T>
    T read_register(std::size_t offset) const
    {
        return *reinterpret_cast<const volatile T*>(fPtr + offset);
    }

    std::int32_t fFd = -1;
    std::byte* fPtr = nullptr;
    std::uint32_t fPeriod = 0; // femtoseconds
    std::uint64_t fOffset = 0;
    std::uint64_t fWrap = 0;
    HpetCounter fPrevious = 0;
};

Hpet hpet;

bool open_hpet()
{
    return hpet.open();
}

jack_time_t get_microseconds_from_hpet()
{
    return hpet.now();
}

#else

bool open_hpet()
{
    jack_error("This version of JACK or this computer does not have HPET support.\n"
               "Please choose a different clock source.");
    return false;
}

jack_time_t get_microseconds_from_hpet()
{
    // Never called: open_hpet() fails on this platform.
    return 0;
}

#endif // HPET_SUPPORT

jack_time_t get_microseconds_from_system()
{
#ifdef CLOCK_MONOTONIC_RAW
    constexpr clockid_t kClock = CLOCK_MONOTONIC_RAW;
#else
    constexpr clockid_t kClock = CLOCK_MONOTONIC;
#endif
    timespec time { };
    clock_gettime(kClock, &time);
    const auto elapsed = std::chrono::seconds(time.tv_sec) + std::chrono::nanoseconds(time.tv_nsec);
    return static_cast<jack_time_t>(std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count());
}

jack_time_t (*current_clock)() = nullptr;

} // end of anonymous namespace

void sleep_microseconds(std::int64_t usec)
{
    usleep(usec);
}

void init_time()
{
    // Nothing to do: the system clock needs no setup.
}

void end_time()
{
}

void set_clock_source(jack_timer_type_t source)
{
    jack_log("Clock source : %s", clock_source_name(source));

    switch (source) {
    case JACK_TIMER_HPET:
        if (open_hpet()) {
            current_clock = get_microseconds_from_hpet;
        } else {
            current_clock = get_microseconds_from_system;
        }
        break;

    case JACK_TIMER_SYSTEM_CLOCK:
    default:
        current_clock = get_microseconds_from_system;
        break;
    }
}

const char* clock_source_name(jack_timer_type_t source)
{
    switch (source) {
    case JACK_TIMER_HPET:
        return "hpet";
    case JACK_TIMER_SYSTEM_CLOCK:
        return "system clock via clock_gettime";
    }

    return "unknown";
}

jack_time_t get_microseconds()
{
    return current_clock();
}

} // end of namespace jackmini
