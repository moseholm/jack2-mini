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

#include "Tools.h"
#include "CString.h"
#include "Constants.h"
#include "DriverInterface.h"
#include "Error.h"
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>

#include <assert.h>
#include <dlfcn.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using namespace std;

namespace jackmini {

static constexpr const char* kDefaultTmpDir = "/tmp";
std::string gJackTmpdir = kDefaultTmpDir;

void Tools::killServer()
{
    kill(getPID(), SIGINT);
}

std::int32_t Tools::mkDir(const char* path)
{
    std::error_code ec;
    // create_directory() reports an existing directory as "not created", which is an error here.
    return !std::filesystem::create_directory(path, ec);
}

std::int32_t Tools::getPID()
{
    return getpid();
}

std::int32_t Tools::getUID()
{
    return geteuid();
}

const char* Tools::defaultServerName()
{
    const char* serverName;
    if ((serverName = getenv("JACK_DEFAULT_SERVER")) == nullptr) {
        serverName = kDefaultServerName;
    }
    return serverName;
}

// returns the name of the per-user subdirectory of jack_tmpdir
const char* Tools::userDir()
{
    static std::array<char, kPathMax + 1> userDir { };

    // format the path name on the first call
    if (userDir[0] == '\0') {
        if (getenv("JACK_PROMISCUOUS_SERVER")) {
            jackmini::formatCString(userDir, "{}/jack", gJackTmpdir);
        } else {
            jackmini::formatCString(userDir, "{}/jack-{}", gJackTmpdir, getUID());
        }
    }

    return userDir.data();
}

// returns the name of the per-server subdirectory of jack_user_dir()
const char* Tools::serverDir(const char* serverName, char* serverDir)
{
    // format the path name into the suppled server_dir char array,
    // assuming that server_dir is at least as large as JACK_PATH_MAX + 1

    jackmini::formatCString(std::span<char>(serverDir, kPathMax + 1), "{}/{}", userDir(), serverName);
    return serverDir;
}

void Tools::cleanupFiles(const char* serverName)
{
    std::array<char, kPathMax + 1> dirName { };
    serverDir(serverName, dirName.data());

    // On termination, we remove all files that jackd creates so
    // subsequent attempts to start jackd will not believe that an
    // instance is already running. If the server crashes or is
    // terminated with SIGKILL, this is not possible. So, cleanup
    // is also attempted when jackd starts.
    //
    // There are several tricky issues. First, the previous JACK
    // server may have run for a different user ID, so its files
    // may be inaccessible. This is handled by using a separate
    // JACK_TMP_DIR subdirectory for each user. Second, there may
    // be other servers running with different names. Each gets
    // its own subdirectory within the per-user directory. The
    // current process has already registered as `server_name', so
    // we know there is no other server actively using that name.

    std::error_code ec;
    const std::filesystem::path serverDir(dirName.data());

    // nothing to do if the server directory does not exist
    if (!std::filesystem::is_directory(serverDir, ec)) {
        return;
    }

    // remove all the files in this directory, they are mine
    for (std::filesystem::directory_iterator it(serverDir, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code removeEc;
        std::filesystem::remove(it->path(), removeEc);
        if (removeEc) {
            jack_error("cannot unlink `%s' (%s)", it->path().c_str(), removeEc.message().c_str());
        }
    }

    // now, delete the per-server subdirectory, itself
    std::error_code rmdirEc;
    std::filesystem::remove(serverDir, rmdirEc);
    if (rmdirEc) {
        jack_error("cannot remove `%s' (%s)", dirName.data(), rmdirEc.message().c_str());
    }

    // finally, delete the per-user subdirectory, if empty
    std::error_code userEc;
    std::filesystem::remove(userDir(), userEc);
    if (userEc && userEc != std::make_error_code(std::errc::directory_not_empty)) {
        jack_error("cannot remove `%s' (%s)", userDir(), userEc.message().c_str());
    }
}

std::int32_t Tools::getTmpdir()
{
    FILE* in;
    size_t len;
    std::array<char, kPathMax + 2> buf { }; // allow tmpdir to live anywhere, plus newline, plus null

    if ((in = popen("jackd -l", "r")) == nullptr) {
        return -1;
    }

    if (fgets(buf.data(), sizeof(buf), in) == nullptr) {
        pclose(in);
        return -1;
    }

    len = strlen(buf.data());

    if (buf[len - 1] != '\n') {
        // didn't get a whole line
        pclose(in);
        return -1;
    }

    gJackTmpdir.assign(buf.data(), len - 1);

    pclose(in);
    return 0;
}

void Tools::rewriteName(const char* name, char* newName)
{
    size_t i;
    for (i = 0; i < strlen(name); i++) {
        if ((name[i] == '/') || (name[i] == '\\')) {
            newName[i] = '_';
        } else {
            newName[i] = name[i];
        }
    }
    newName[i] = '\0';
}

void printLoadError(const char* soName)
{
    jack_log("error loading %s err = %s", soName, dlerror());
}

void buildClientPath(char* pathToSo, std::int32_t pathLen, const char* soName)
{
    const char* internalDir;
    if ((internalDir = getenv("JACK_INTERNAL_DIR")) == 0) {
        if ((internalDir = getenv("JACK_DRIVER_DIR")) == 0) {
            internalDir = kAddonDir;
        }
    }

    jackmini::formatCString(std::span<char>(pathToSo, pathLen), "{}/{}.so", internalDir, soName);
}

} // namespace jackmini
