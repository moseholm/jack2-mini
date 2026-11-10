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

#include "FdSegment.h"
#include "types.h"

#include <cstdint>
#include <span>
#include <vector>

namespace jackmini {

class ClientInterface;
class ServerCore;
struct EngineControl;
class GraphManager;
struct ShmSegment;

namespace detail {

class ChannelTransactionInterface {

public:
    ChannelTransactionInterface()
    {
    }
    virtual ~ChannelTransactionInterface()
    {
    }

    virtual std::int32_t read(void* data, std::int32_t len) = 0;
    virtual std::int32_t write(void* data, std::int32_t len) = 0;

    // Writes data with descriptors, in one message. The receiver gets its own copies.
    virtual std::int32_t writeWithFds(void* data, std::int32_t len, std::span<const std::int32_t> fds) { return -1; }
    // Reads data and the descriptors sent with it. The caller owns the descriptors it gets.
    virtual std::int32_t readWithFds(void* data, std::int32_t len, std::vector<UniqueFd>& fds) { return -1; }
};

class RequestInterface {

public:
    RequestInterface()
    {
    }
    virtual ~RequestInterface()
    {
    }

    virtual std::int32_t connect(const char* dir, const char* name, std::int32_t which) = 0;
    virtual std::int32_t close() = 0;
};

class ClientRequestInterface : public ChannelTransactionInterface, public RequestInterface {

public:
    ClientRequestInterface()
    {
    }
    virtual ~ClientRequestInterface()
    {
    }

    virtual std::int32_t read(void* data, std::int32_t len) { return -1; }
    virtual std::int32_t write(void* data, std::int32_t len) { return -1; }

    virtual std::int32_t connect(const char* dir, const char* name, std::int32_t which) { return -1; }
    virtual std::int32_t close() { return -1; }
};

// \brief Inter process channel for server/client bidirectionnal communication : request and (receiving) notifications.

} // namespace detail

} // namespace jackmini
