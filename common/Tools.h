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
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <cstdint>

#include "CompilerDeps.h"
#include "Error.h"
#include "jslist.h"

namespace jackmini {

// \brief Utility functions.

struct SERVER_EXPORT Tools {
    static std::int32_t getPID();
    static std::int32_t getUID();

    static void killServer();

    static std::int32_t mkDir(const char* path);
    static const char* userDir();
    static const char* serverDir(const char* serverName, char* serverDir);
    static const char* defaultServerName();
    static void cleanupFiles(const char* serverName);
    static std::int32_t getTmpdir();
    static void rewriteName(const char* name, char* newName);

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

void buildClientPath(char* pathToSo, std::int32_t pathLen, const char* soName);
void printLoadError(const char* soName);

} // namespace jackmini
