// Copyright (C) 2014-2017 Cédric Schieli
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public License
// as published by the Free Software Foundation; either version 2.1
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.

#include "promiscuous.hpp"

#include "JackError.h"

#include <cstdint>
#include <fcntl.h>
#include <grp.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace jackmini {

std::int32_t group_to_gid(const char* group)
{
    if (!group || !*group) {
        return -1;
    }

    char* end = nullptr;
    const std::int64_t number = std::strtol(group, &end, 10);
    if (*end == '\0') {
        return static_cast<std::int32_t>(number);
    }

    const std::int64_t size = sysconf(_SC_GETGR_R_SIZE_MAX);
    std::vector<char> buffer(size == -1 ? 4096 : static_cast<std::size_t>(size));
    struct group entry;
    struct group* result = nullptr;
    std::int32_t ret;
    while ((ret = getgrnam_r(group, &entry, buffer.data(), buffer.size(), &result)) == ERANGE) {
        buffer.resize(buffer.size() * 2);
    }
    if (ret || !result) {
        return -1;
    }
    return static_cast<std::int32_t>(entry.gr_gid);
}

std::int32_t set_promiscuous_perms(std::int32_t fd, const char* path, gid_t gid)
{
    mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
    // gid_t is unsigned, so the group is always changed here. A gid of -1 leaves the group as it is.
    const std::int32_t chown_result = (fd < 0) ? chown(path, -1, gid) : fchown(fd, -1, gid);
    if (chown_result < 0) {
        jack_log("Cannot chgrp %s: %s. Falling back to permissive perms.", path, strerror(errno));
    } else {
        mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP;
    }
    if (((fd < 0) ? chmod(path, mode) : fchmod(fd, mode)) < 0) {
        jack_log("Cannot chmod %s: %s. Falling back to default (umask) perms.", path, strerror(errno));
        return -1;
    }
    return 0;
}

} // namespace jackmini
