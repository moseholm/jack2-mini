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

// The client session (client/client_session.hpp) against a running server: open, name handling, and close. Not a
// ctest: it needs a server, for example `jackd -d dummy`.

#include "client_session.hpp"

#include <cstdio>
#include <string>

namespace
{

int failures = 0;

#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::fprintf(stderr, "FAILED %s:%d: %s\n", __FILE__,       \
                         __LINE__, #cond);                             \
            ++failures;                                                \
        }                                                              \
    } while (0)

} // end of anonymous namespace

int main()
{
    using jackmini::OpenOptions;
    using jackmini::client::ClientSession;

    OpenOptions exact;
    exact.exact_name = true;

    auto first = ClientSession::open("session_e2e", exact);
    CHECK(first.has_value());
    if (!first) {
        std::fprintf(stderr, "no server? error %d\n", static_cast<int>(first.error()));
        return 1;
    }
    CHECK((*first)->ref() >= 0);
    CHECK((*first)->name() == "session_e2e");
    CHECK((*first)->server_alive());

    // The exact name is taken: the open fails with NameTaken.
    auto taken = ClientSession::open("session_e2e", exact);
    CHECK(!taken.has_value());
    CHECK(!taken.has_value() && taken.error() == jackmini::Error::NameTaken);

    // Without exact_name the server gives the client another name.
    auto renamed = ClientSession::open("session_e2e", OpenOptions{});
    CHECK(renamed.has_value());
    if (renamed) {
        CHECK((*renamed)->name() != "session_e2e");
        CHECK((*renamed)->ref() != (*first)->ref());
        CHECK((*renamed)->close().has_value());
        CHECK((*renamed)->close().has_value());   // closing twice does nothing
    }

    CHECK((*first)->close().has_value());
    auto again = ClientSession::open("session_e2e", exact);
    CHECK(again.has_value());
    if (again) {
        CHECK((*again)->close().has_value());
    }

    if (failures) {
        std::fprintf(stderr, "client session e2e: %d failure(s)\n", failures);
        return 1;
    }
    std::printf("client session e2e checks passed\n");
    return 0;
}
