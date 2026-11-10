// Copyright (C) 2004-2008 Grame
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

// The UUIDs of clients and ports, used by the server and the clients (not exported).

#include "JackCString.h"
#include "JackError.h"
#include "jack/uuid.h"
#include <cstdint>
#include <stdio.h>
#include <string.h>
#include <cctype>
#include <charconv>
#include <span>
#include <string_view>

using namespace jackmini;

jack_uuid_t jack_client_uuid_generate()
{
    static std::uint32_t uuid_cnt = 0;
    jack_uuid_t uuid = 0x2;  // JackUUIDClient
    ;
    uuid = (uuid << 32) | ++uuid_cnt;
    return uuid;
}

jack_uuid_t jack_port_uuid_generate(std::uint32_t port_id)
{
    jack_uuid_t uuid = 0x1;  // JackUUIDPort
    uuid = (uuid << 32) | (port_id + 1);
    return uuid;
}

std::uint32_t jack_uuid_to_index(jack_uuid_t u)
{
    return (u & 0xffff) - 1;
}

std::int32_t jack_uuid_compare(jack_uuid_t a, jack_uuid_t b)
{
    if (a == b) {
        return 0;
    }

    if (a < b) {
        return -1;
    }

    return 1;
}

void jack_uuid_copy(jack_uuid_t* dst, jack_uuid_t src)
{
    *dst = src;
}

void jack_uuid_clear(jack_uuid_t* u)
{
    *u = kUuidEmptyInitializer;
}

std::int32_t jack_uuid_parse(const char* b, jack_uuid_t* u)
{
    const std::string_view text = SkipNumericPrefix(b);
    std::uint64_t value = 0;
    const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc()) {
        return -1;
    }

    *u = value;
    if (*u < (0x1LL << 32)) {
        // has not type bits set - not legal
        return -1;
    }

    return 0;
}

void jack_uuid_unparse(jack_uuid_t u, char b[kUuidStringSize])
{
    jackmini::FormatCString(std::span<char>(b, kUuidStringSize), "{}", static_cast<std::uint64_t>(u));
}

std::int32_t jack_uuid_empty(jack_uuid_t u)
{
    return u == kUuidEmptyInitializer;
}
