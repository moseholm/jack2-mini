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

#pragma once

#include <sys/types.h>

namespace jackmini {

// Looks up the gid of a UNIX group, given by name or by number. Returns -1 if it is unknown.
int group_to_gid(const char* group);

// Sets promiscuous permissions on the object referenced by fd, or by path when fd is negative.
int set_promiscuous_perms(int fd, const char* path, gid_t gid);

} // namespace jackmini
