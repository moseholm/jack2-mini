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
#include "JackError.h"
#include "JackFdSegment.h"

#include <errno.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <cstddef>
#include <cstdint>
#include <new> // GCC 4.0

inline bool checkMlock(const void* ptr, std::size_t size)
{
    return mlock(ptr, size) == 0;
}

inline bool checkMunlock(const void* ptr, std::size_t size)
{
    return munlock(ptr, size) == 0;
}

inline bool checkMlockall()
{
    return mlockall(MCL_CURRENT | MCL_FUTURE) == 0;
}

inline bool checkMunlockall()
{
    return munlockall() == 0;
}

namespace jackmini {

void LockMemoryImp(void* ptr, size_t size);
void InitLockMemoryImp(void* ptr, size_t size);
void UnlockMemoryImp(void* ptr, size_t size);
void LockAllMemory();
void UnlockAllMemory();

// \brief A shared segment as it is passed between the server and a client: its descriptor and its size.
//
// The server keeps the descriptor of each segment it owns. A client receives its own descriptors with the open reply, maps them, and closes them.

struct JackShmSegment {
    std::int32_t fFd = -1;
    std::uint32_t fSize = 0;
};

// \brief A class which objects possibly want to be allocated in shared memory derives from this class.

class JackShmMemAble {
protected:
    std::uint32_t fSize;
    void* fAddress;
    std::int32_t fFd;

public:
    void Init();

    std::int32_t GetShmFd() const
    {
        return fFd;
    }

    std::uint32_t GetShmSize() const
    {
        return fSize;
    }

    std::byte* GetShmAddress()
    {
        return static_cast<std::byte*>(fAddress);
    }

    void LockMemory()
    {
        LockMemoryImp(this, fSize);
    }

    void UnlockMemory()
    {
        UnlockMemoryImp(this, fSize);
    }
};

// \brief The base class for shared memory management.
//
// A class which objects need to be allocated in shared memory derives from this class.

class SERVER_EXPORT JackShmMem : public JackShmMemAble {

protected:
    JackShmMem();
    ~JackShmMem();

public:
    void* operator new(size_t size);
    void* operator new(size_t size, void* memory);

    void operator delete(void* p, size_t size);
    void operator delete(void* p);
};

// \brief Pointer on a shared memory segment, in the client side.
//
// The segment is mapped from the descriptor the server sent. The mapping is removed when the pointer is destroyed.

template <class T>
class JackShmReadWritePtr {

private:
    void* fAddress;
    std::size_t fSize;

    void Init(const JackShmSegment& segment)
    {
        if (fAddress == nullptr) {
            jack_log("JackShmReadWritePtr::Init fd = %d size = %u", segment.fFd, segment.fSize);
            auto mapped = MapSegment(segment.fFd, segment.fSize);
            if (!mapped) {
                throw std::bad_alloc();
            }
            fAddress = *mapped;
            fSize = segment.fSize;
            GetShmAddress()->LockMemory();
        }
    }

public:
    JackShmReadWritePtr()
        : fAddress(nullptr)
        , fSize(0)
    {
    }

    JackShmReadWritePtr(const JackShmReadWritePtr&) = delete;
    JackShmReadWritePtr& operator=(const JackShmReadWritePtr&) = delete;

    ~JackShmReadWritePtr()
    {
        // Not mapped when the open that owns it failed early: nothing to undo.
        if (fAddress == nullptr) {
            return;
        }
        jack_log("JackShmReadWritePtr::~JackShmReadWritePtr");
        GetShmAddress()->UnlockMemory();
        UnmapSegment(fAddress, fSize);
        fAddress = nullptr;
    }

    T* operator->() const
    {
        return (T*)fAddress;
    }

    operator T*() const
    {
        return (T*)fAddress;
    }

    void Map(const JackShmSegment& segment)
    {
        Init(segment);
    }

    T* GetShmAddress()
    {
        return (T*)fAddress;
    }
};

} // namespace jackmini
