// Copyright (C) 2008 Grame
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

#include "CompilerDeps.h"

#include <iostream>
#include <stdexcept>
#include <string>

namespace jackmini {

#define ThrowIf(inCondition, inException) \
    if (inCondition) {                    \
        throw(inException);               \
    }


// \brief Exception base class.

class SERVER_EXPORT Exception : public std::runtime_error {

public:
    Exception(const std::string& msg)
        : std::runtime_error(msg)
    {
    }
    Exception(char* msg)
        : std::runtime_error(msg)
    {
    }
    Exception(const char* msg)
        : std::runtime_error(msg)
    {
    }

    std::string message()
    {
        return what();
    }

    void printMessage();
};

// \brief Exception thrown by JackEngine in temporary mode.

class SERVER_EXPORT TemporaryException : public Exception {

public:
    TemporaryException(const std::string& msg)
        : Exception(msg)
    {
    }
    TemporaryException(char* msg)
        : Exception(msg)
    {
    }
    TemporaryException(const char* msg)
        : Exception(msg)
    {
    }
    TemporaryException()
        : Exception("")
    {
    }
};

// \brief

class SERVER_EXPORT QuitException : public Exception {

public:
    QuitException(const std::string& msg)
        : Exception(msg)
    {
    }
    QuitException(char* msg)
        : Exception(msg)
    {
    }
    QuitException(const char* msg)
        : Exception(msg)
    {
    }
    QuitException()
        : Exception("")
    {
    }
};

} // namespace jackmini
