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

#pragma once

#include "common/CString.h"
#include "common/Tools.h"
#include "ipc/protocol/Constants.h"

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

namespace jackmini {

// The names of the global socket and futex objects share a pattern: a rewritten client name, and a UID
// component that is omitted in promiscuous mode so every user reaches the same server.  Keep both formats
// here so they stay in step; the formats themselves are unchanged.

// <dir>/jack_<name>[_<uid>]_<which>
inline void buildSocketName(const char* clientName, const char* dir, std::int32_t which, bool promiscuous, std::span<char> dst)
{
    std::array<char, kSyncMaxNameSize + 1> extClientName {};
    Tools::rewriteName(clientName, extClientName.data());
    const std::string_view extName = extClientName.data();
    if (promiscuous) {
        formatCString(dst, "{}/jack_{}_{}", dir, extName, which);
    } else {
        formatCString(dst, "{}/jack_{}_{}_{}", dir, extName, Tools::getUID(), which);
    }
}

// jack_sem[.<uid>]_<server>_<name>
inline void buildFutexName(const char* clientName, const char* serverName, bool promiscuous, std::span<char> dst)
{
    std::array<char, kSyncMaxNameSize + 1> extClientName {};
    Tools::rewriteName(clientName, extClientName.data());
    if (promiscuous) {
        formatCString(dst, "jack_sem.{}_{}", serverName, extClientName.data());
    } else {
        formatCString(dst, "jack_sem.{}_{}_{}", Tools::getUID(), serverName, extClientName.data());
    }
}

} // namespace jackmini
