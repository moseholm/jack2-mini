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

#include "JackMessageBuffer.h"
#include "JackGlobals.h"
#include "JackError.h"
#include "JackTime.h"
#include <algorithm>
#include <string>

namespace Jack
{

JackMessageBuffer* JackMessageBuffer::fInstance = nullptr;

JackMessageBuffer::JackMessageBuffer()
    :fInit(nullptr),
    fInitArg(NULL),
    fThread(this),
    fGuard(),
    fInBuffer(0),
    fOutBuffer(0),
    fOverruns(0),
    fRunning(false)
{
    static_assert(offsetof(JackMessageBuffer, fOverruns) % sizeof(fOverruns) == 0,
                  "fOverruns must be aligned within JackMessageBuffer");
}

JackMessageBuffer::~JackMessageBuffer()
{}

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
    if (fOverruns > 0) {
        jack_error("WARNING: %d message buffer overruns!", fOverruns);
    } else {
        jack_log("no message buffer overruns");
    }

    if (fGuard.Lock()) {
        fRunning = false;
        fGuard.Signal();
        fGuard.Unlock();
        fThread.Stop();
    } else {
        fThread.Kill();
    }

    Flush();
    return true;
}

void JackMessageBuffer::Flush()
{
    unsigned int out = fOutBuffer.load(std::memory_order_relaxed);
    while (out != fInBuffer.load(std::memory_order_acquire)) {
        const JackMessage& entry = fBuffers[out];
        jack_log_function(entry.level, entry.message.data());
        out = MB_NEXT(out);
        fOutBuffer.store(out, std::memory_order_release);
    }
}

void JackMessageBuffer::AddMessage(int level, std::string_view message)
{
    if (fGuard.Trylock()) {
        const unsigned int in = fInBuffer.load(std::memory_order_relaxed);
        const unsigned int next = MB_NEXT(in);
        if (next == fOutBuffer.load(std::memory_order_acquire)) {
            /* the reader has not caught up, so the ring is full */
            fGuard.Unlock();
            INC_ATOMIC(&fOverruns);
            return;
        }
        JackMessage& entry = fBuffers[in];
        entry.level = level;
        const std::size_t length = std::min(message.size(), MB_BUFFERSIZE - 1);
        std::ranges::copy(message.substr(0, length), entry.message.begin());
        entry.message[length] = '\0';
        fInBuffer.store(next, std::memory_order_release);
        fGuard.Signal();
        fGuard.Unlock();
    } else {            /* lock collision */
        INC_ATOMIC(&fOverruns);
    }
}

bool JackMessageBuffer::Execute()
{
    if (fGuard.Lock()) {
        while (fRunning) {
            fGuard.Wait();
            /* the client asked for all threads to run a thread
            initialization callback, which includes us.
            */
            if (JackThreadInitCallback init = fInit) {
                init(fInitArg);
                fInit = nullptr;
                /* and we're done */
                fGuard.Signal();
            }
            
            /* releasing the mutex reduces contention */
            fGuard.Unlock();
            Flush();
            fGuard.Lock();
        }
        fGuard.Unlock();
    } else {
        jack_error("JackMessageBuffer::Execute lock cannot be taken");
    }

    return false;
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

void JackMessageBufferAdd(int level, const char *message)
{
    if (Jack::JackMessageBuffer::fInstance == nullptr) {
        /* Unable to print message with realtime safety. Complain and print it anyway. */
        jack_log_function(LOG_LEVEL_ERROR, "messagebuffer not initialized, skip message");
    } else {
        /* scan at most one buffer's worth, so an unterminated input cannot run away */
        const char* end = std::char_traits<char>::find(message, Jack::MB_BUFFERSIZE, '\0');
        const std::size_t length = end ? static_cast<std::size_t>(end - message) : Jack::MB_BUFFERSIZE;
        Jack::JackMessageBuffer::fInstance->AddMessage(level, std::string_view(message, length));
    }
}

int JackMessageBuffer::SetInitCallback(JackThreadInitCallback callback, void *arg)
{
    if (fInstance && callback && fRunning && fGuard.Lock()) {
        /* set up the callback */
        fInitArg = arg;
        fInit = callback;
        
        // wake msg buffer thread
        fGuard.Signal();
        // wait for it to be done
        fGuard.Wait();
        // and we're done
        fGuard.Unlock();
        return 0;
    }
    jack_error("JackMessageBuffer::SetInitCallback : callback could not be executed");
    return -1;
}

};

