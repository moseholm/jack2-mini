// messagebuffer.h -- realtime-safe message interface for jackd.
//
//  This function is included in libjack so backend drivers can use
//  it, *not* for external client processes.  The VERBOSE() and
//  MESSAGE() macros are realtime-safe.

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

#pragma once

#include "JackAtomic.h"
#include "JackMutex.h"
#include "JackPlatformPlug.h"
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string_view>

namespace jackmini {

// The ring index wraps with a mask, so MB_BUFFERS must be a power of two.
inline constexpr std::size_t kMbBuffers = 128;
inline constexpr std::size_t kMbBuffersize = 256; // message length limit, including the terminator

struct Message {
    std::int32_t m_level;
    std::array<char, kMbBuffersize> m_message;
};

// \brief Message buffer to be used from RT threads.
//
// AddMessage() never blocks, allocates or makes a system call.  A bounded
// multi-producer ring holds the messages.  The flush thread drains the ring by
// polling, so producers never wake it.  A full ring drops the new message and
// counts it as an overrun.

class MessageBuffer : public RunnableInterface {

private:
    struct Slot {
        std::atomic<std::size_t> m_sequence;
        Message m_entry;
    };

    std::atomic<ThreadInitCallbackC> m_init;
    void* m_initArg;
    std::mutex m_initLock; // serializes SetInitCallback callers, which are not realtime
    std::array<Slot, kMbBuffers> m_slots;
    alignas(64) std::atomic<std::size_t> m_enqueuePos; // written by producers
    alignas(64) std::atomic<std::size_t> m_dequeuePos; // written by the flush thread only
    std::atomic<std::uint32_t> m_overruns;
    Thread m_thread;
    std::atomic<bool> m_running;

    bool enqueue(std::int32_t level, std::string_view message);
    void flush();

    bool start();
    bool stop();

public:
    MessageBuffer();
    ~MessageBuffer();

    // JackRunnableInterface interface
    bool execute();

    bool static create();
    bool static destroy();

    void addMessage(std::int32_t level, std::string_view message);
    std::int32_t setInitCallback(ThreadInitCallbackC callback, void* arg);
    std::uint32_t overruns() const;

    static MessageBuffer* s_instance;
};


void messageBufferAdd(std::int32_t level, const char* message);


}; // namespace jackmini
