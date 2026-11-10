//  Copyright (C) 2004 Rui Nuno Capela, Steve Harris
//  Copyright (C) 2008 Nedko Arnaudov
//  Copyright (C) 2008 Grame
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU Lesser General Public License as published by
//  the Free Software Foundation; either version 2.1 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU Lesser General Public License for more details.
//
//  You should have received a copy of the GNU Lesser General Public License
//  along with this program; if not, write to the Free Software
//  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.

#include "JackMessageBuffer.h"
#include "JackError.h"
#include "JackGlobals.h"
#include "JackTime.h"
#include <algorithm>
#include <chrono>
#include <string>
#include <thread>

namespace jackmini {

namespace {
// How often the flush thread drains the ring.  Producers never wake it.
constexpr auto kPollInterval = std::chrono::milliseconds(5);
} // namespace

JackMessageBuffer* JackMessageBuffer::fInstance = nullptr;

JackMessageBuffer::JackMessageBuffer()
    : fInit(nullptr)
    , fInitArg(nullptr)
    , fEnqueuePos(0)
    , fDequeuePos(0)
    , fOverruns(0)
    , fThread(this)
    , fRunning(false)
{
    for (std::size_t i = 0; i < MB_BUFFERS; i++) {
        fSlots[i].sequence.store(i, std::memory_order_relaxed);
    }
}

JackMessageBuffer::~JackMessageBuffer()
{
}

bool JackMessageBuffer::Start()
{
    // Before StartSync()...
    fRunning = true;
    if (fThread.StartSync() == 0) {
        return true;
    } else {
        fRunning = false;
        return false;
    }
}

bool JackMessageBuffer::Stop()
{
    const std::uint32_t overruns = fOverruns.load(std::memory_order_relaxed);
    if (overruns > 0) {
        jack_error("WARNING: %u message buffer overruns!", overruns);
    } else {
        jack_log("no message buffer overruns");
    }

    fRunning = false;
    fThread.Stop();
    Flush();
    return true;
}

// Vyukov-style bounded queue.  Each slot's sequence says whether it is free for
// the position currently being claimed (producer) or holds data for it (consumer).
bool JackMessageBuffer::Enqueue(int level, std::string_view message)
{
    std::size_t pos = fEnqueuePos.load(std::memory_order_relaxed);
    Slot* slot = nullptr;
    for (;;) {
        slot = &fSlots[pos & (MB_BUFFERS - 1)];
        const std::size_t seq = slot->sequence.load(std::memory_order_acquire);
        const auto diff = static_cast<std::ptrdiff_t>(seq - pos);
        if (diff == 0) {
            if (fEnqueuePos.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                break;
            }
        } else if (diff < 0) {
            // The reader has not freed this slot yet, so the ring is full.
            return false;
        } else {
            pos = fEnqueuePos.load(std::memory_order_relaxed);
        }
    }

    slot->entry.level = level;
    const std::size_t length = std::min(message.size(), MB_BUFFERSIZE - 1);
    std::ranges::copy(message.substr(0, length), slot->entry.message.begin());
    slot->entry.message[length] = '\0';
    slot->sequence.store(pos + 1, std::memory_order_release);
    return true;
}

// Only the flush thread (or Stop() after it has been joined) calls this.
void JackMessageBuffer::Flush()
{
    for (;;) {
        const std::size_t pos = fDequeuePos.load(std::memory_order_relaxed);
        Slot& slot = fSlots[pos & (MB_BUFFERS - 1)];
        const std::size_t seq = slot.sequence.load(std::memory_order_acquire);
        if (static_cast<std::ptrdiff_t>(seq - (pos + 1)) < 0) {
            return; // empty
        }
        fDequeuePos.store(pos + 1, std::memory_order_relaxed);
        jack_log_direct(slot.entry.level, slot.entry.message.data());
        slot.sequence.store(pos + MB_BUFFERS, std::memory_order_release);
    }
}

void JackMessageBuffer::AddMessage(int level, std::string_view message)
{
    if (!Enqueue(level, message)) {
        fOverruns.fetch_add(1, std::memory_order_relaxed);
    }
}

std::uint32_t JackMessageBuffer::Overruns() const
{
    return fOverruns.load(std::memory_order_relaxed);
}

bool JackMessageBuffer::Execute()
{
    if (JackThreadInitCallback init = fInit.load(std::memory_order_acquire)) {
        // the client asked for all threads to run a thread
        // initialization callback, which includes us.
        init(fInitArg);
        fInit.store(nullptr, std::memory_order_release);
    }

    Flush();
    std::this_thread::sleep_for(kPollInterval);
    return true;
}

bool JackMessageBuffer::Create()
{
    if (fInstance == nullptr) {
        fInstance = new JackMessageBuffer();
        if (!fInstance->Start()) {
            jack_error("JackMessageBuffer::Create cannot start thread");
            delete fInstance;
            fInstance = nullptr;
            return false;
        }
    }

    return true;
}

bool JackMessageBuffer::Destroy()
{
    if (fInstance != nullptr) {
        fInstance->Stop();
        delete fInstance;
        fInstance = nullptr;
        return true;
    } else {
        return false;
    }
}

void JackMessageBufferAdd(int level, const char* message)
{
    if (jackmini::JackMessageBuffer::fInstance == nullptr) {
        // Unable to print message with realtime safety. Complain and print it anyway.
        jack_log_direct(LOG_LEVEL_ERROR, "messagebuffer not initialized, skip message");
    } else {
        // scan at most one buffer's worth, so an unterminated input cannot run away
        const char* end = std::char_traits<char>::find(message, jackmini::MB_BUFFERSIZE, '\0');
        const std::size_t length = end ? static_cast<std::size_t>(end - message) : jackmini::MB_BUFFERSIZE;
        jackmini::JackMessageBuffer::fInstance->AddMessage(level, std::string_view(message, length));
    }
}

int JackMessageBuffer::SetInitCallback(JackThreadInitCallback callback, void* arg)
{
    if (fInstance && callback && fRunning.load(std::memory_order_acquire)) {
        std::lock_guard<std::mutex> guard(fInitLock);
        // set up the callback
        fInitArg = arg;
        fInit.store(callback, std::memory_order_release);
        // wait for the flush thread to run it, or for shutdown
        while (fInit.load(std::memory_order_acquire) != nullptr && fRunning.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(kPollInterval);
        }
        return 0;
    }
    jack_error("JackMessageBuffer::SetInitCallback : callback could not be executed");
    return -1;
}

}; // namespace jackmini
