// Copyright (C) 2006 Grame
//
// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 2.1 of the License, or (at your option) any later version.
//
// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public
// License along with this library; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
//
// Grame Research Laboratory, 9 rue du Garet, 69001 Lyon - France
// grame@grame.fr

#pragma once

#include "JackError.h"
#include "JackPlatformPlug.h"
#include <assert.h>


namespace jackmini {
// \brief Base class for "lockable" objects.

class LockAble {

protected:
    Mutex m_mutex;

    LockAble(const char* name = nullptr)
        : m_mutex(name)
    {
    }
    ~LockAble()
    {
    }

public:
    bool lock()
    {
        return m_mutex.lock();
    }

    bool trylock()
    {
        return m_mutex.trylock();
    }

    bool unlock()
    {
        return m_mutex.unlock();
    }
};

class Lock {
private:
    LockAble* m_obj;

public:
    Lock(LockAble* obj)
        : m_obj(obj)
    {
        m_obj->lock();
    }

    Lock(const LockAble* obj)
        : m_obj((LockAble*)obj)
    {
        m_obj->lock();
    }

    ~Lock()
    {
        m_obj->unlock();
    }
};


} // namespace jackmini
