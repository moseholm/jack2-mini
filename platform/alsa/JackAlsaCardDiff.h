/*
Copyright (C) 2026 JACK contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#ifndef __JackAlsaCardDiff__
#define __JackAlsaCardDiff__

#include <algorithm>
#include <string>
#include <vector>

namespace jackmini
{

/*!
\brief Which running cards to retire and which desired cards to add.

Cards are compared by key (the port base, the device and its directions).
Indices refer to the \a current and \a desired lists.
*/

struct JackAlsaCardChanges {
    std::vector<size_t> retire;
    std::vector<size_t> add;
};

inline JackAlsaCardChanges JackAlsaDiffCards(const std::vector<std::string>& current,
                                             const std::vector<std::string>& desired)
{
    JackAlsaCardChanges changes;
    for (size_t i = 0; i < current.size(); i++) {
        if (std::find(desired.begin(), desired.end(), current[i]) == desired.end()) {
            changes.retire.push_back(i);
        }
    }
    for (size_t j = 0; j < desired.size(); j++) {
        if (std::find(current.begin(), current.end(), desired[j]) == current.end()) {
            changes.add.push_back(j);
        }
    }
    return changes;
}

} // end of namespace

#endif
