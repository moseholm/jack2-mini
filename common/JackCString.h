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
#include <array>
#include <cctype>
#include <charconv>
#include <concepts>
#include <cstddef>
#include <format>
#include <span>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <utility>

namespace jackmini
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

/*!
\brief Format into a fixed-size C string buffer.

Like snprintf(): the text is truncated to fit and the result is always
terminated.  The format string is checked at compile time.
*/
template <typename T>
inline constexpr bool IsStdArray = false;
template <typename T, std::size_t N>
inline constexpr bool IsStdArray<std::array<T, N>> = true;

template <typename... Args>
void FormatCString(std::span<char> dst, std::format_string<Args...> fmt, Args&&... args)
{
    // A std::array is a range, so it would be formatted as a list, not as text.
    static_assert((!IsStdArray<std::remove_cvref_t<Args>> && ...), "pass .data() for a std::array argument");
    if (dst.empty()) {
        return;
    }
    const auto result = std::format_to_n(dst.data(), dst.size() - 1, fmt, std::forward<Args>(args)...);
    *result.out = '\0';
}

/*!
\brief Skip the prefix that strtol() accepted: leading white space and one '+'.
*/
inline std::string_view SkipNumericPrefix(std::string_view text)
{
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) {
        text.remove_prefix(1);
    }
    if (text.size() > 1 && text[0] == '+' && std::isdigit(static_cast<unsigned char>(text[1]))) {
        text.remove_prefix(1);
    }
    return text;
}

/*!
\brief Parse a leading integer the way atoi() did.

Returns 0 when there are no digits or the value does not fit.  Anything after
the digits is ignored.  Unlike strtoul(), a minus sign is not accepted for an
unsigned type.
*/
template <std::integral T>
T ParseLeadingInteger(std::string_view text)
{
    text = SkipNumericPrefix(text);
    T value{};
    const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    return ec == std::errc() ? value : T{};
}

}

#endif
