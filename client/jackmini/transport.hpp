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

#ifndef JACKMINI_TRANSPORT_HPP
#define JACKMINI_TRANSPORT_HPP

#include "port.hpp"

#include <cstdint>

namespace jackmini
{

enum class TransportState : std::uint8_t {
    Stopped,
    Rolling,
    Starting,
    Looping,
};

} // namespace jackmini

#endif
