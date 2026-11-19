// Copyright (C) 2026 JACK contributors
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

// Compile-time checks of the shared-memory structures.  Every structure is in a
// shared segment, so its size and alignment must be the same in every process,
// and its fields must sit at natural offsets.  The checks are static, so this
// program only needs to compile.

#include "ipc/memory/ActivationCount.h"
#include "ipc/memory/AtomicArrayState.h"
#include "ipc/memory/AtomicState.h"
#include "ipc/memory/ClientControl.h"
#include "ipc/memory/ConnectionManager.h"
#include "ipc/protocol/Constants.h"
#include "ipc/memory/EngineControl.h"
#include "ipc/memory/FrameTimer.h"
#include "ipc/memory/GraphManager.h"
#include "ipc/memory/ServerPort.h"
#include "common/Time.h"
#include "ipc/memory/TransportEngine.h"
#include "common/Types.h"

#include <atomic>
#include <cstddef>

using namespace jackmini;

// Each shared structure has natural alignment: its size is a multiple of its
// alignment, so an array of them stays aligned.
#define LAYOUT(T) static_assert(sizeof(T) % alignof(T) == 0, #T " is not a multiple of its alignment")
LAYOUT(ClientControl);
LAYOUT(EngineControl);
LAYOUT(GraphManager);
LAYOUT(ConnectionManager);
LAYOUT(ServerPort);
LAYOUT(TransportEngine);
LAYOUT(ActivationCount);
LAYOUT(FrameTimer);
#undef LAYOUT

// The 64-bit counters and times must be 8-byte aligned in every class that holds
// them.  JackClientTiming holds 64-bit times and a state; it is an array in the
// graph segment, so its alignment is what matters.
static_assert(alignof(ClientTiming) >= alignof(TimeC));

// The UUIDs of the ports and of the clients are stored in the graph segment: 64 bits, and aligned as such.
static_assert(sizeof(UuidC) == 8 && alignof(UuidC) == 8);
static_assert(sizeof(ClientTiming) % alignof(ClientTiming) == 0);

// Latency ranges are copied between processes and are written into the graph's port objects.
static_assert(sizeof(LatencyRangeC) == 2 * sizeof(NframesC));
static_assert(alignof(LatencyRangeC) == alignof(NframesC));
static_assert(offsetof(LatencyRangeC, m_min) == 0);
static_assert(offsetof(LatencyRangeC, m_max) == sizeof(NframesC));

// Transport positions are copied through the graph's atomic state.  These checks fix the natural
// layout that replaced the old packed struct: both sides of the IPC must agree on it.
static_assert(sizeof(PositionC) == 144);
static_assert(alignof(PositionC) == 8);
static_assert(offsetof(PositionC, m_unique1) == 0);
static_assert(offsetof(PositionC, m_usecs) == 8);
static_assert(offsetof(PositionC, m_frameRate) == 16);
static_assert(offsetof(PositionC, m_frame) == 20);
static_assert(offsetof(PositionC, m_barStartTick) == 40);
static_assert(offsetof(PositionC, m_tickDouble) == 104);
static_assert(offsetof(PositionC, m_padding) == 112);
static_assert(offsetof(PositionC, m_unique2) == 136);

// The activation counter and the state counters are 32-bit, read and written
// with std::atomic_ref.  They need 4-byte alignment.
static_assert(alignof(ActivationCount) >= alignof(SInt32));

// Each field of the per-client timing sits at an offset that is a multiple of its own alignment: the times are 64-bit.
static_assert(offsetof(ClientTiming, m_signaledAt) % alignof(TimeC) == 0);
static_assert(offsetof(ClientTiming, m_awakeAt) % alignof(TimeC) == 0);
static_assert(offsetof(ClientTiming, m_finishedAt) % alignof(TimeC) == 0);

// The activation count of a client is configuration only: one 32-bit field, at offset 0.  The live counter is outside
// the copied state, as an atomic array of the graph manager (protocol.md section 4.5), so it must be lock-free there.
static_assert(sizeof(ActivationCount) == sizeof(SInt32));
static_assert(std::atomic<SInt32>::is_always_lock_free);

// The deactivation flag of the connection manager is 32-bit, and aligned in the state that is copied (protocol.md section 6).
static_assert(alignof(UInt32) == 4);

int main() { return 0; }
