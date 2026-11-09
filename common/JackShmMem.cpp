/*
 Copyright (C) 2004-2008 Grame

 This program is free software; you can redistribute it and/or modify
 it under the terms of the GNU Lesser General Public License as published by
 the Free Software Foundation; either version 2.1 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU Lesser General Public License for more details.

 You should have received a copy of the GNU Lesser General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.

 */

#include "JackShmMem.h"
#include "JackCString.h"
#include "JackError.h"
#include <stdio.h>
#include <array>

namespace jackmini {

static unsigned int fSegmentNum = 0;
static std::uint32_t gSize = 0;
static void* gAddress = nullptr;
static int gFd = -1;

JackShmMem::JackShmMem()
{
    JackShmMemAble::Init();
    LockMemory();
}

JackShmMem::~JackShmMem()
{
    UnlockMemory();
}

void JackShmMemAble::Init()
{
    fSize = gSize;
    fAddress = gAddress;
    fFd = gFd;
}

void* JackShmMem::operator new(size_t size, void* memory)
{
    jack_log("JackShmMem::new placement size = %ld", size);
    return memory;
}

void* JackShmMem::operator new(size_t size)
{
    std::array<char, 64> name { };

    jackmini::FormatCString(name, "jack-shared{}", fSegmentNum++);

    auto segment = CreateSegment(size, name.data());
    if (!segment) {
        jack_error("Cannot create shared memory segment name = %s size = %zu err = %s", name.data(), size, segment.error().message().c_str());
        throw std::bad_alloc();
    }

    auto address = MapSegment(segment->get(), size);
    if (!address) {
        jack_error("Cannot map shared memory segment name = %s err = %s", name.data(), address.error().message().c_str());
        throw std::bad_alloc();
    }

    // It is unsafe to set object fields directly (may be overwritten during object initialization),
    // so use an intermediate global data
    gSize = size;
    gAddress = *address;
    gFd = segment->release();

    jack_log("JackShmMem::new fd = %d attached = %p size = %zu", gFd, *address, size);
    return *address;
}

void JackShmMem::operator delete(void* p, size_t size)
{
    JackShmMem* obj = (JackShmMem*)p;
    // The descriptor is closed and the mapping removed; the segment goes when the last one is gone.
    UniqueFd fd(obj->fFd);

    jack_log("JackShmMem::delete size = %ld fd = %d", size, fd.get());

    UnmapSegment(p, obj->fSize);
}

void JackShmMem::operator delete(void* obj)
{
    if (obj) {
        JackShmMem::operator delete(obj, 0);
    }
}

void LockMemoryImp(void* ptr, size_t size)
{
    if (CHECK_MLOCK((char*)ptr, size)) {
        jack_log("Succeeded in locking %u byte memory area", size);
    } else {
        jack_error("Cannot lock down %u byte memory area (%s)", size, strerror(errno));
    }
}

void InitLockMemoryImp(void* ptr, size_t size)
{
    if (CHECK_MLOCK((char*)ptr, size)) {
        memset(ptr, 0, size);
        jack_log("Succeeded in locking %u byte memory area", size);
    } else {
        jack_error("Cannot lock down %u byte memory area (%s)", size, strerror(errno));
    }
}

void UnlockMemoryImp(void* ptr, size_t size)
{
    if (CHECK_MUNLOCK((char*)ptr, size)) {
        jack_log("Succeeded in unlocking %u byte memory area", size);
    } else {
        jack_error("Cannot unlock down %u byte memory area (%s)", size, strerror(errno));
    }
}

void LockAllMemory()
{
    if (CHECK_MLOCKALL()) {
        jack_log("Succeeded in locking all memory");
    } else {
        jack_error("Cannot lock all memory (%s)", strerror(errno));
    }
}

void UnlockAllMemory()
{
    if (CHECK_MUNLOCKALL()) {
        jack_log("Succeeded in unlocking all memory");
    } else {
        jack_error("Cannot unlock all memory (%s)", strerror(errno));
    }
}


} // namespace jackmini
