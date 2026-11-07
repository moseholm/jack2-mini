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

#ifndef __JackCString__
#define __JackCString__

#include <algorithm>
#include <cstddef>
#include <span>
#include <string_view>

namespace Jack
{

/*!
\brief Copy text into a fixed-size C string buffer.

The text is truncated to fit and the result is always terminated.  The rest of
the buffer is zero-filled, as strncpy() does.  An empty buffer is left alone.
*/
inline void CopyCString(std::span<char> dst, std::string_view src)
{
    if (dst.empty()) {
        return;
    }
    const std::size_t n = std::min(src.size(), dst.size() - 1);
    std::ranges::copy(src.substr(0, n), dst.begin());
    std::ranges::fill(dst.subspan(n), '\0');
}

}

#endif
