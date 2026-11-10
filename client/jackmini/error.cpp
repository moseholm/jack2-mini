// Copyright (C) 2026 JACK contributors
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

#include "error.hpp"

namespace jackmini {

std::string_view toString(Error error)
{
    switch (error) {
    case Error::kInvalidArgument:
        return "invalid argument";
    case Error::kInvalidState:
        return "operation not allowed in the current state";
    case Error::kNameTaken:
        return "name already in use";
    case Error::kNotFound:
        return "not found";
    case Error::kNoServer:
        return "server not running and could not be started";
    case Error::kServerFailure:
        return "server failed the request";
    case Error::kProtocol:
        return "unexpected reply from the server";
    case Error::kIo:
        return "connection to the server failed";
    }
    return "unknown error";
}

} // namespace jackmini
