// Copyright (C) 2026 JACK contributors
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation; either version 2.1 of the License, or
// (at your option) any later version.
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
// You should have received a copy of the GNU Lesser General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.

#pragma once

#include <atomic>
#include <chrono>
#include <thread>

// Waits for events, never for time: a cycle, a callback, a state.  The condition is polled; the time limit only guards
// against a stalled server.  Counting cycles keeps the programs exact with the server in freewheel mode (jackd -F) and
// still correct in real time.

template <typename Pred>
bool WaitUntil(Pred pred)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    while (!pred()) {
        if (std::chrono::steady_clock::now() > deadline) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
    return true;
}

// Waits until a counter has moved by n.
inline bool WaitCycles(const std::atomic<unsigned long>& counter, unsigned long n)
{
    const unsigned long target = counter.load() + n;
    return WaitUntil([&] { return counter.load() >= target; });
}
