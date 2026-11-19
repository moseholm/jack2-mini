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

#include "common/Tools.h"
#include "common/CString.h"
#include "ipc/protocol/Constants.h"
#include "server/drivers/DriverInterface.h"
#include "common/Error.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <system_error>

#include <dlfcn.h>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace std;

namespace jackmini {

static constexpr const char* kDefaultTmpDir = "/tmp";
std::string gJackTmpdir = kDefaultTmpDir;

void Tools::killServer()
{
    kill(getPID(), SIGINT);
}

std::int32_t Tools::mkDir(std::string_view path)
{
    std::error_code ec;
    // create_directory() reports an existing directory as "not created", which is an error here.
    return !std::filesystem::create_directory(std::string(path), ec);
}

std::int32_t Tools::getPID()
{
    return getpid();
}

std::int32_t Tools::getUID()
{
    return geteuid();
}

std::string_view Tools::defaultServerName()
{
    const char* serverName = getenv("JACK_DEFAULT_SERVER");
    return serverName != nullptr ? std::string_view(serverName) : std::string_view(kDefaultServerName);
}

// returns the name of the per-user subdirectory of jack_tmpdir
std::string_view Tools::userDir()
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
void Tools::serverDir(std::string_view serverName, std::span<char> serverDir)
{
    // format the path name into the supplied serverDir char array,
    // assuming that serverDir is at least as large as JACK_PATH_MAX + 1

    jackmini::formatCString(serverDir, "{}/{}", userDir(), serverName);
}

void Tools::cleanupFiles(std::string_view serverName)
{
    std::array<char, kPathMax + 1> dirName { };
    serverDir(serverName, dirName);

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
    const std::string_view userDirPath = userDir();
    std::filesystem::remove(std::filesystem::path(userDirPath), userEc);
    if (userEc && userEc != std::make_error_code(std::errc::directory_not_empty)) {
        jack_error("cannot remove `%s' (%s)", std::string(userDirPath).c_str(), userEc.message().c_str());
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

    len = std::strlen(buf.data());

    if (buf[len - 1] != '\n') {
        // didn't get a whole line
        pclose(in);
        return -1;
    }

    gJackTmpdir.assign(buf.data(), len - 1);

    pclose(in);
    return 0;
}

void Tools::rewriteName(std::string_view name, std::span<char> newName)
{
    if (newName.empty()) {
        return;
    }
    const size_t count = std::min(name.size(), newName.size() - 1);
    for (size_t i = 0; i < count; i++) {
        if ((name[i] == '/') || (name[i] == '\\')) {
            newName[i] = '_';
        } else {
            newName[i] = name[i];
        }
    }
    newName[count] = '\0';
}

void printLoadError(std::string_view soName)
{
    const std::string name(soName);
    jack_log("error loading %s err = %s", name.c_str(), dlerror());
}

void buildClientPath(std::span<char> pathToSo, std::string_view soName)
{
    const char* internalDir;
    if ((internalDir = getenv("JACK_INTERNAL_DIR")) == 0) {
        if ((internalDir = getenv("JACK_DRIVER_DIR")) == 0) {
            internalDir = kAddonDir.data();
        }
    }

    jackmini::formatCString(pathToSo, "{}/{}.so", internalDir, soName);
}

} // namespace jackmini
