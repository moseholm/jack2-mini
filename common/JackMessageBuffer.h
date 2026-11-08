/*
 * messagebuffer.h -- realtime-safe message interface for jackd.
 *
 *  This function is included in libjack so backend drivers can use
 *  it, *not* for external client processes.  The VERBOSE() and
 *  MESSAGE() macros are realtime-safe.
 */

/*
 *  Copyright (C) 2004 Rui Nuno Capela, Steve Harris
 *  Copyright (C) 2008 Nedko Arnaudov
 *  Copyright (C) 2008 Grame
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU Lesser General Public License as published by
 *  the Free Software Foundation; either version 2.1 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

#ifndef __JackMessageBuffer__
#define __JackMessageBuffer__

#include "JackPlatformPlug.h"
#include "JackMutex.h"
#include "JackAtomic.h"
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string_view>

namespace jackmini
{

// The ring index wraps with a mask, so MB_BUFFERS must be a power of two.
inline constexpr std::size_t MB_BUFFERS = 128;
inline constexpr std::size_t MB_BUFFERSIZE = 256;     // message length limit, including the terminator

struct JackMessage
{
    int level;
    std::array<char, MB_BUFFERSIZE> message;
};

/*!
\brief Message buffer to be used from RT threads.

AddMessage() never blocks, allocates or makes a system call.  A bounded
multi-producer ring holds the messages.  The flush thread drains the ring by
polling, so producers never wake it.  A full ring drops the new message and
counts it as an overrun.
*/

class JackMessageBuffer : public JackRunnableInterface
{

    private:

        struct Slot {
            std::atomic<std::size_t> sequence;
            JackMessage entry;
        };

        std::atomic<JackThreadInitCallback> fInit;
        void* fInitArg;
        std::mutex fInitLock;      // serializes SetInitCallback callers, which are not realtime
        std::array<Slot, MB_BUFFERS> fSlots;
        alignas(64) std::atomic<std::size_t> fEnqueuePos;   // written by producers
        alignas(64) std::atomic<std::size_t> fDequeuePos;   // written by the flush thread only
        std::atomic<std::uint32_t> fOverruns;
        JackThread fThread;
        std::atomic<bool> fRunning;

        bool Enqueue(int level, std::string_view message);
        void Flush();

        bool Start();
        bool Stop();

    public:

        JackMessageBuffer();
        ~JackMessageBuffer();

        // JackRunnableInterface interface
        bool Execute();

	    bool static Create();
	    bool static Destroy();

        void AddMessage(int level, std::string_view message);
        int SetInitCallback(JackThreadInitCallback callback, void *arg);
        std::uint32_t Overruns() const;

	    static JackMessageBuffer* fInstance;
};

#ifdef __cplusplus
extern "C"
{
#endif

void JackMessageBufferAdd(int level, const char *message);

#ifdef __cplusplus
}
#endif

};

#endif
