/*
  Copyright (C) 2006-2008 Grame

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

#include <array>
#include "JackCString.h"
#include "JackConstants.h"
#include "driver_interface.h"
#include "JackTools.h"
#include "JackError.h"

#include <assert.h>
#include <dlfcn.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using namespace std;

namespace Jack {

#define DEFAULT_TMP_DIR "/tmp"
    char* jack_tmpdir = (char*)DEFAULT_TMP_DIR;

    void JackTools::KillServer()
    {
        kill(GetPID(), SIGINT);
    }

    int JackTools::MkDir(const char* path)
    {
        return mkdir(path, 0777) != 0;
    }

    int JackTools::GetPID()
    {
        return getpid();
    }

    int JackTools::GetUID()
    {
        return geteuid();
    }

    const char* JackTools::DefaultServerName()
    {
        const char* server_name;
        if ((server_name = getenv("JACK_DEFAULT_SERVER")) == NULL) {
            server_name = JACK_DEFAULT_SERVER_NAME;
        }
        return server_name;
    }

    /* returns the name of the per-user subdirectory of jack_tmpdir */
    const char* JackTools::UserDir()
    {
        static char user_dir[JACK_PATH_MAX + 1] = "";

        /* format the path name on the first call */
        if (user_dir[0] == '\0') {
            if (getenv("JACK_PROMISCUOUS_SERVER")) {
                Jack::FormatCString(user_dir, "{}/jack", jack_tmpdir);
            } else {
                Jack::FormatCString(user_dir, "{}/jack-{}", jack_tmpdir, GetUID());
            }
        }

        return user_dir;
    }

    /* returns the name of the per-server subdirectory of jack_user_dir() */
    const char* JackTools::ServerDir(const char* server_name, char* server_dir)
    {
        /* format the path name into the suppled server_dir char array,
        * assuming that server_dir is at least as large as JACK_PATH_MAX + 1 */

        Jack::FormatCString(std::span<char>(server_dir, JACK_PATH_MAX + 1), "{}/{}", UserDir(), server_name);
        return server_dir;
    }

    void JackTools::CleanupFiles(const char* server_name)
    {
        DIR* dir;
        struct dirent *dirent;
        std::array<char, JACK_PATH_MAX + 1> dir_name{};
        ServerDir(server_name, dir_name.data());

        /* On termination, we remove all files that jackd creates so
        * subsequent attempts to start jackd will not believe that an
        * instance is already running. If the server crashes or is
        * terminated with SIGKILL, this is not possible. So, cleanup
        * is also attempted when jackd starts.
        *
        * There are several tricky issues. First, the previous JACK
        * server may have run for a different user ID, so its files
        * may be inaccessible. This is handled by using a separate
        * JACK_TMP_DIR subdirectory for each user. Second, there may
        * be other servers running with different names. Each gets
        * its own subdirectory within the per-user directory. The
        * current process has already registered as `server_name', so
        * we know there is no other server actively using that name.
        */

        /* nothing to do if the server directory does not exist */
        if ((dir = opendir(dir_name.data())) == NULL) {
            return;
        }

        /* unlink all the files in this directory, they are mine */
        while ((dirent = readdir(dir)) != NULL) {

            std::array<char, JACK_PATH_MAX + 1> fullpath{};

            if ((strcmp(dirent->d_name, ".") == 0) || (strcmp(dirent->d_name, "..") == 0)) {
                continue;
            }

            Jack::FormatCString(fullpath, "{}/{}", dir_name.data(), dirent->d_name);

            if (unlink(fullpath.data())) {
                jack_error("cannot unlink `%s' (%s)", fullpath.data(), strerror(errno));
            }
        }

        closedir(dir);

        /* now, delete the per-server subdirectory, itself */
        if (rmdir(dir_name.data())) {
            jack_error("cannot remove `%s' (%s)", dir_name.data(), strerror(errno));
        }

        /* finally, delete the per-user subdirectory, if empty */
        if (rmdir(UserDir())) {
            if (errno != ENOTEMPTY) {
                jack_error("cannot remove `%s' (%s)", UserDir(), strerror(errno));
            }
        }
    }

    int JackTools::GetTmpdir()
    {
        FILE* in;
        size_t len;
        std::array<char, JACK_PATH_MAX + 2> buf{}; /* allow tmpdir to live anywhere, plus newline, plus null */

        if ((in = popen("jackd -l", "r")) == NULL) {
            return -1;
        }

        if (fgets(buf.data(), sizeof(buf), in) == NULL) {
            pclose(in);
            return -1;
        }

        len = strlen(buf.data());

        if (buf[len - 1] != '\n') {
            /* didn't get a whole line */
            pclose(in);
            return -1;
        }

        jack_tmpdir = (char *)malloc(len);
        memcpy(jack_tmpdir, buf.data(), len - 1);
        jack_tmpdir[len - 1] = '\0';

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

    void BuildClientPath(char* path_to_so, int path_len, const char* so_name)
    {
        const char* internal_dir;
        if ((internal_dir = getenv("JACK_INTERNAL_DIR")) == 0) {
            if ((internal_dir = getenv("JACK_DRIVER_DIR")) == 0) {
                internal_dir = ADDON_DIR;
            }
        }

        Jack::FormatCString(std::span<char>(path_to_so, path_len), "{}/{}.so", internal_dir, so_name);
    }

}  // end of namespace
