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

void lockMemoryImp(void* ptr, size_t size);
void initLockMemoryImp(void* ptr, size_t size);
void unlockMemoryImp(void* ptr, size_t size);
void lockAllMemory();
void unlockAllMemory();

// \brief A shared segment as it is passed between the server and a client: its descriptor and its size.
//
// The server keeps the descriptor of each segment it owns. A client receives its own descriptors with the open reply, maps them, and closes them.

struct ShmSegment {
    std::int32_t m_fd = -1;
    std::uint32_t m_size = 0;
};

// \brief A class which objects possibly want to be allocated in shared memory derives from this class.

class ShmMemAble {
protected:
    std::uint32_t m_size;
    void* m_address;
    std::int32_t m_fd;

public:
    void init();

    std::int32_t getShmFd() const
    {
        return m_fd;
    }

    std::uint32_t getShmSize() const
    {
        return m_size;
    }

    std::byte* getShmAddress()
    {
        return static_cast<std::byte*>(m_address);
    }

    void lockMemory()
    {
        lockMemoryImp(this, m_size);
    }

    void unlockMemory()
    {
        unlockMemoryImp(this, m_size);
    }
};

// \brief The base class for shared memory management.
//
// A class which objects need to be allocated in shared memory derives from this class.

class SERVER_EXPORT ShmMem : public ShmMemAble {

protected:
    ShmMem();
    ~ShmMem();

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
class ShmReadWritePtr {

private:
    void* m_address;
    std::size_t m_size;

    void init(const ShmSegment& segment)
    {
        if (m_address == nullptr) {
            jack_log("JackShmReadWritePtr::Init fd = %d size = %u", segment.m_fd, segment.m_size);
            auto mapped = mapSegment(segment.m_fd, segment.m_size);
            if (!mapped) {
                throw std::bad_alloc();
            }
            m_address = *mapped;
            m_size = segment.m_size;
            getShmAddress()->lockMemory();
        }
    }

public:
    ShmReadWritePtr()
        : m_address(nullptr)
        , m_size(0)
    {
    }

    ShmReadWritePtr(const ShmReadWritePtr&) = delete;
    ShmReadWritePtr& operator=(const ShmReadWritePtr&) = delete;

    ~ShmReadWritePtr()
    {
        // Not mapped when the open that owns it failed early: nothing to undo.
        if (m_address == nullptr) {
            return;
        }
        jack_log("JackShmReadWritePtr::~JackShmReadWritePtr");
        getShmAddress()->unlockMemory();
        unmapSegment(m_address, m_size);
        m_address = nullptr;
    }

    T* operator->() const
    {
        return (T*)m_address;
    }

    operator T*() const
    {
        return (T*)m_address;
    }

    void map(const ShmSegment& segment)
    {
        init(segment);
    }

    T* getShmAddress()
    {
        return (T*)m_address;
    }
};

} // namespace jackmini
