// Copyright (C) 2004-2008 Grame
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

#include "ipc/memory/ActivationCount.h"
#include "ipc/memory/Atomic.h"
#include "ipc/memory/ClientControl.h"
#include "ipc/protocol/Constants.h"
#include "common/Error.h"

namespace jackmini {

bool ActivationCount::signal(std::atomic<SInt32>& live, Synchro* synchro, ClientControl* control) JACK_REALTIME_FN
{
    if (live.load() == 0) {
        // Transfer activation to next clients
        JACK_RT_UNCHECKED("error path logging", jack_log("JackActivationCount::Signal value = 0 ref = %ld", control->m_refNum));
        JACK_RTSAN_SCOPED_DISABLE(); // FUTEX_WAKE only wakes waiters; it cannot block.
        JACK_RT_UNCHECKED("futex wake does not block", return synchro->signal());
    } else if (live.fetch_sub(1) == 1) {
        JACK_RTSAN_SCOPED_DISABLE(); // FUTEX_WAKE only wakes waiters; it cannot block.
        JACK_RT_UNCHECKED("futex wake does not block", return synchro->signal());
    } else {
        return true;
    }
}

} // namespace jackmini
