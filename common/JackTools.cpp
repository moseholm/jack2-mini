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

#include "JackTools.h"
#include "JackCString.h"
#include "JackConstants.h"
#include "JackError.h"
#include <cstdint>
#include <array>
#include <filesystem>
#include <string>
#include <system_error>
#include "driver_interface.h"

#include <assert.h>
#include <dlfcn.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using namespace std;

namespace jackmini {

#define DEFAULT_TMP_DIR "/tmp"
std::string jack_tmpdir = DEFAULT_TMP_DIR;

void JackTools::KillServer()
{
    kill(GetPID(), SIGINT);
}

std::int32_t JackTools::MkDir(const char* path)
{
    std::error_code ec;
    // create_directory() reports an existing directory as "not created", which is an error here.
    return !std::filesystem::create_directory(path, ec);
}

std::int32_t JackTools::GetPID()
{
    return getpid();
}

std::int32_t JackTools::GetUID()
{
    return geteuid();
}

const char* JackTools::DefaultServerName()
{
    const char* server_name;
    if ((server_name = getenv("JACK_DEFAULT_SERVER")) == nullptr) {
        server_name = JACK_DEFAULT_SERVER_NAME;
    }
    return server_name;
}

// returns the name of the per-user subdirectory of jack_tmpdir
const char* JackTools::UserDir()
{
    static char user_dir[JACK_PATH_MAX + 1] = "";

    // format the path name on the first call
    if (user_dir[0] == '\0') {
        if (getenv("JACK_PROMISCUOUS_SERVER")) {
            jackmini::FormatCString(user_dir, "{}/jack", jack_tmpdir);
        } else {
            jackmini::FormatCString(user_dir, "{}/jack-{}", jack_tmpdir, GetUID());
        }
    }

    return user_dir;
}

// returns the name of the per-server subdirectory of jack_user_dir()
const char* JackTools::ServerDir(const char* server_name, char* server_dir)
{
    // format the path name into the suppled server_dir char array,
    // assuming that server_dir is at least as large as JACK_PATH_MAX + 1

    jackmini::FormatCString(std::span<char>(server_dir, JACK_PATH_MAX + 1), "{}/{}", UserDir(), server_name);
    return server_dir;
}

void JackTools::CleanupFiles(const char* server_name)
{
    std::array<char, JACK_PATH_MAX + 1> dir_name { };
    ServerDir(server_name, dir_name.data());

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
    const std::filesystem::path server_dir(dir_name.data());

    // nothing to do if the server directory does not exist
    if (!std::filesystem::is_directory(server_dir, ec)) {
        return;
    }

    // remove all the files in this directory, they are mine
    for (std::filesystem::directory_iterator it(server_dir, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code remove_ec;
        std::filesystem::remove(it->path(), remove_ec);
        if (remove_ec) {
            jack_error("cannot unlink `%s' (%s)", it->path().c_str(), remove_ec.message().c_str());
        }
    }

    // now, delete the per-server subdirectory, itself
    std::error_code rmdir_ec;
    std::filesystem::remove(server_dir, rmdir_ec);
    if (rmdir_ec) {
        jack_error("cannot remove `%s' (%s)", dir_name.data(), rmdir_ec.message().c_str());
    }

    // finally, delete the per-user subdirectory, if empty
    std::error_code user_ec;
    std::filesystem::remove(UserDir(), user_ec);
    if (user_ec && user_ec != std::make_error_code(std::errc::directory_not_empty)) {
        jack_error("cannot remove `%s' (%s)", UserDir(), user_ec.message().c_str());
    }
}

std::int32_t JackTools::GetTmpdir()
{
    FILE* in;
    size_t len;
    std::array<char, JACK_PATH_MAX + 2> buf { };  // allow tmpdir to live anywhere, plus newline, plus null

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

    jack_tmpdir.assign(buf.data(), len - 1);

    pclose(in);
    return 0;
}

void JackTools::RewriteName(const char* name, char* new_name)
{
    size_t i;
    for (i = 0; i < strlen(name); i++) {
        if ((name[i] == '/') || (name[i] == '\\')) {
            new_name[i] = '_';
        } else {
            new_name[i] = name[i];
        }
    }
    new_name[i] = '\0';
}

void PrintLoadError(const char* so_name)
{
    jack_log("error loading %s err = %s", so_name, dlerror());
}

void BuildClientPath(char* path_to_so, std::int32_t path_len, const char* so_name)
{
    const char* internal_dir;
    if ((internal_dir = getenv("JACK_INTERNAL_DIR")) == 0) {
        if ((internal_dir = getenv("JACK_DRIVER_DIR")) == 0) {
            internal_dir = ADDON_DIR;
        }
    }

    jackmini::FormatCString(std::span<char>(path_to_so, path_len), "{}/{}.so", internal_dir, so_name);
}

} // namespace jackmini
