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
#include <cstdint>
#include <algorithm>
#include <chrono>
#include <string>
#include <thread>

namespace jackmini {

namespace {
// How often the flush thread drains the ring.  Producers never wake it.
constexpr auto kPollInterval = std::chrono::milliseconds(5);
} // namespace

MessageBuffer* MessageBuffer::s_instance = nullptr;

MessageBuffer::MessageBuffer()
    : m_init(nullptr)
    , m_initArg(nullptr)
    , m_enqueuePos(0)
    , m_dequeuePos(0)
    , m_overruns(0)
    , m_thread(this)
    , m_running(false)
{
    for (std::size_t i = 0; i < kMbBuffers; i++) {
        m_slots[i].m_sequence.store(i, std::memory_order_relaxed);
    }
}

MessageBuffer::~MessageBuffer()
{
}

bool MessageBuffer::start()
{
    // Before StartSync()...
    m_running = true;
    if (m_thread.startSync() == 0) {
        return true;
    } else {
        m_running = false;
        return false;
    }
}

bool MessageBuffer::stop()
{
    const std::uint32_t overruns = m_overruns.load(std::memory_order_relaxed);
    if (overruns > 0) {
        jack_error("WARNING: %u message buffer overruns!", overruns);
    } else {
        jack_log("no message buffer overruns");
    }

    m_running = false;
    m_thread.stop();
    flush();
    return true;
}

// Vyukov-style bounded queue.  Each slot's sequence says whether it is free for
// the position currently being claimed (producer) or holds data for it (consumer).
bool MessageBuffer::enqueue(std::int32_t level, std::string_view message)
{
    std::size_t pos = m_enqueuePos.load(std::memory_order_relaxed);
    Slot* slot = nullptr;
    for (;;) {
        slot = &m_slots[pos & (kMbBuffers - 1)];
        const std::size_t seq = slot->m_sequence.load(std::memory_order_acquire);
        const auto diff = static_cast<std::ptrdiff_t>(seq - pos);
        if (diff == 0) {
            if (m_enqueuePos.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed)) {
                break;
            }
        } else if (diff < 0) {
            // The reader has not freed this slot yet, so the ring is full.
            return false;
        } else {
            pos = m_enqueuePos.load(std::memory_order_relaxed);
        }
    }

    slot->m_entry.m_level = level;
    const std::size_t length = std::min(message.size(), kMbBuffersize - 1);
    std::ranges::copy(message.substr(0, length), slot->m_entry.m_message.begin());
    slot->m_entry.m_message[length] = '\0';
    slot->m_sequence.store(pos + 1, std::memory_order_release);
    return true;
}

// Only the flush thread (or Stop() after it has been joined) calls this.
void MessageBuffer::flush()
{
    for (;;) {
        const std::size_t pos = m_dequeuePos.load(std::memory_order_relaxed);
        Slot& slot = m_slots[pos & (kMbBuffers - 1)];
        const std::size_t seq = slot.m_sequence.load(std::memory_order_acquire);
        if (static_cast<std::ptrdiff_t>(seq - (pos + 1)) < 0) {
            return; // empty
        }
        m_dequeuePos.store(pos + 1, std::memory_order_relaxed);
        logDirect(slot.m_entry.m_level, slot.m_entry.m_message.data());
        slot.m_sequence.store(pos + kMbBuffers, std::memory_order_release);
    }
}

void MessageBuffer::addMessage(std::int32_t level, std::string_view message)
{
    if (!enqueue(level, message)) {
        m_overruns.fetch_add(1, std::memory_order_relaxed);
    }
}

std::uint32_t MessageBuffer::overruns() const
{
    return m_overruns.load(std::memory_order_relaxed);
}

bool MessageBuffer::execute()
{
    if (ThreadInitCallbackC init = m_init.load(std::memory_order_acquire)) {
        // the client asked for all threads to run a thread
        // initialization callback, which includes us.
        init(m_initArg);
        m_init.store(nullptr, std::memory_order_release);
    }

    flush();
    std::this_thread::sleep_for(kPollInterval);
    return true;
}

bool MessageBuffer::create()
{
    if (s_instance == nullptr) {
        s_instance = new MessageBuffer();
        if (!s_instance->start()) {
            jack_error("JackMessageBuffer::Create cannot start thread");
            delete s_instance;
            s_instance = nullptr;
            return false;
        }
    }

    return true;
}

bool MessageBuffer::destroy()
{
    if (s_instance != nullptr) {
        s_instance->stop();
        delete s_instance;
        s_instance = nullptr;
        return true;
    } else {
        return false;
    }
}

void messageBufferAdd(std::int32_t level, const char* message)
{
    if (jackmini::MessageBuffer::s_instance == nullptr) {
        // Unable to print message with realtime safety. Complain and print it anyway.
        logDirect(kLogLevelError, "messagebuffer not initialized, skip message");
    } else {
        // scan at most one buffer's worth, so an unterminated input cannot run away
        const char* end = std::char_traits<char>::find(message, jackmini::kMbBuffersize, '\0');
        const std::size_t length = end ? static_cast<std::size_t>(end - message) : jackmini::kMbBuffersize;
        jackmini::MessageBuffer::s_instance->addMessage(level, std::string_view(message, length));
    }
}

std::int32_t MessageBuffer::setInitCallback(ThreadInitCallbackC callback, void* arg)
{
    if (s_instance && callback && m_running.load(std::memory_order_acquire)) {
        std::lock_guard<std::mutex> guard(m_initLock);
        // set up the callback
        m_initArg = arg;
        m_init.store(callback, std::memory_order_release);
        // wait for the flush thread to run it, or for shutdown
        while (m_init.load(std::memory_order_acquire) != nullptr && m_running.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(kPollInterval);
        }
        return 0;
    }
    jack_error("JackMessageBuffer::SetInitCallback : callback could not be executed");
    return -1;
}

}; // namespace jackmini
