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

// Compile-time checks of the shared-memory structures.  Every structure is in a
// shared segment, so its size and alignment must be the same in every process,
// and its fields must sit at natural offsets.  The checks are static, so this
// program only needs to compile.

#include "JackConstants.h"
#include "JackTypes.h"
#include "JackTime.h"
#include "JackActivationCount.h"
#include "JackAtomicArrayState.h"
#include "JackAtomicState.h"
#include "JackClientControl.h"
#include "JackConnectionManager.h"
#include "JackEngineControl.h"
#include "JackFrameTimer.h"
#include "JackGraphManager.h"
#include "JackPort.h"
#include "JackTransportEngine.h"

#include <atomic>
#include <cstddef>
#include <cstdint>

using namespace jackmini;

// Each shared structure has natural alignment: its size is a multiple of its
// alignment, so an array of them stays aligned.
#define LAYOUT(T) static_assert(sizeof(T) % alignof(T) == 0, #T " is not a multiple of its alignment")
LAYOUT(JackClientControl);
LAYOUT(JackEngineControl);
LAYOUT(JackGraphManager);
LAYOUT(JackConnectionManager);
LAYOUT(JackPort);
LAYOUT(JackTransportEngine);
LAYOUT(JackActivationCount);
LAYOUT(JackFrameTimer);
#undef LAYOUT

// The 64-bit counters and times must be 8-byte aligned in every class that holds
// them.  JackClientTiming holds 64-bit times and a state; it is an array in the
// graph segment, so its alignment is what matters.
static_assert(alignof(JackClientTiming) >= alignof(jack_time_t));
static_assert(sizeof(JackClientTiming) % alignof(JackClientTiming) == 0);

// The activation counter and the state counters are 32-bit, read and written
// with std::atomic_ref.  They need 4-byte alignment.
static_assert(alignof(JackActivationCount) >= alignof(SInt32));

// Each field of the per-client timing sits at an offset that is a multiple of its own alignment: the times are 64-bit.
static_assert(offsetof(JackClientTiming, fSignaledAt) % alignof(jack_time_t) == 0);
static_assert(offsetof(JackClientTiming, fAwakeAt) % alignof(jack_time_t) == 0);
static_assert(offsetof(JackClientTiming, fFinishedAt) % alignof(jack_time_t) == 0);

// The activation count of a client is configuration only: one 32-bit field, at offset 0.  The live counter is outside
// the copied state, as an atomic array of the graph manager (protocol.md section 4.5), so it must be lock-free there.
static_assert(sizeof(JackActivationCount) == sizeof(SInt32));
static_assert(std::atomic<SInt32>::is_always_lock_free);

// The deactivation flag of the connection manager is 32-bit, and aligned in the state that is copied (protocol.md section 6).
static_assert(alignof(UInt32) == 4);

int main() { return 0; }
