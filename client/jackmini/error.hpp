/*
Copyright (C) 2026 JACK contributors

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

#ifndef JACKMINI_ERROR_HPP
#define JACKMINI_ERROR_HPP

#include <expected>
#include <string_view>

namespace jackmini
{

// Failure reasons for every operation that can fail.  Operations on the
// realtime path do not return errors.
enum class Error {
    InvalidArgument,  // a name, port, or value is not acceptable
    InvalidState,     // the operation is not allowed in the client's current state
    NameTaken,        // a client or port with this name exists
    NotFound,         // no such client, port, or parameter
    NoServer,         // the server is not running and could not be started
    ServerFailure,    // the server refused or failed the request
    Protocol,         // the server's reply did not match the protocol
    Io,               // the connection to the server failed
};

template <class T>
using Result = std::expected<T, Error>;

std::string_view to_string(Error error);

} // namespace jackmini

#endif
