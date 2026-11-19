// Copyright (C) 2006-2008 Grame
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

inline constexpr char kDirSeparator = '/';
#include <cstdint>
#include <span>
#include <string_view>

#include "common/CompilerDeps.h"
#include "common/Error.h"

namespace jackmini {

// \brief Utility functions.

struct SERVER_EXPORT Tools {
    static std::int32_t getPID();
    static std::int32_t getUID();

    static void killServer();

    static std::int32_t mkDir(std::string_view path);
    static std::string_view userDir();
    static void serverDir(std::string_view serverName, std::span<char> serverDir);
    static std::string_view defaultServerName();
    static void cleanupFiles(std::string_view serverName);
    static std::int32_t getTmpdir();
    static void rewriteName(std::string_view name, std::span<char> newName);

    static std::int32_t computationMicroSec(std::int32_t bufferSize)
    {
        if (bufferSize < 128) {
            return 500;
        } else if (bufferSize < 256) {
            return 300;
        } else {
            return 100;
        }
    }
};

void buildClientPath(std::span<char> pathToSo, std::string_view soName);
void printLoadError(std::string_view soName);

} // namespace jackmini
