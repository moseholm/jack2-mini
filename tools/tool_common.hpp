// Copyright (C) 2026 JACK contributors
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation; either version 2.1 of the License, or
// (at your option) any later version.
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
// You should have received a copy of the GNU Lesser General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.

#pragma once

// What every tool needs: opening its client, and reporting an error with the exit status.  Kept small: a tool that
// needs something only it uses keeps it in its own file.

#include "jackmini/jackmini.hpp"

#include <cstdio>
#include <string>
#include <string_view>

namespace jackmini::tools {

// Opens the client of a tool.  A tool never starts a server (as jack_lsp did): with no server running, it fails.
// `server` is the name given with -s, or empty for the default server.
inline Result<Client> open_client(std::string_view name, std::string_view server)
{
    OpenOptions options;
    options.start_server = false;
    options.server_name = std::string(server);
    return Client::open(name, options);
}

// Prints "<tool>: <message>" to stderr, and returns the exit status a tool gives after an error.
inline int fail(const char* tool, std::string_view message)
{
    std::fprintf(stderr, "%s: %.*s\n", tool, static_cast<int>(message.size()), message.data());
    return 1;
}

// The message for a failed call: the error's text, from the client library.
inline int fail(const char* tool, Error error)
{
    return fail(tool, to_string(error));
}

} // namespace jackmini::tools
