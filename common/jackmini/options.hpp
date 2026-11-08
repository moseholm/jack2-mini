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

#ifndef JACKMINI_OPTIONS_HPP
#define JACKMINI_OPTIONS_HPP

#include <string>

namespace jackmini
{

// Options for Client::open().
struct OpenOptions {
    // Start the server if it is not running.  When false, opening fails with
    // Error::NoServer instead.
    bool start_server = true;

    // Use the name exactly.  When false, the server may append a number if the
    // name is taken.
    bool exact_name = false;

    // The server to connect to.  Empty means the default server.
    std::string server_name;
};

} // namespace jackmini

#endif
