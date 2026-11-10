// Copyright (C) 2026 JACK contributors
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

#pragma once

#include <cstdint>

namespace jackmini {

// \brief Told when a card's I/O fails.  Called from the card's thread.

class JackAlsaCardFailureSink {
public:
    virtual ~JackAlsaCardFailureSink()
    {
    }
    virtual void OnCardFailed(std::int32_t card_id) = 0;
};

} // namespace jackmini
