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

#ifndef __JackAlsaReconfigWorker__
#define __JackAlsaReconfigWorker__

#include "JackAlsaControl.h"
#include "JackAlsaControlMonitor.h"
#include "JackPlatformPlug.h"
#include "JackThread.h"

#include <condition_variable>
#include <map>
#include <mutex>
#include <utility>

namespace Jack
{

/*!
\brief Handles a rate request on the worker thread.  Not real-time.
*/

class JackAlsaRateRequestHandler
{
    public:
        virtual ~JackAlsaRateRequestHandler()
        {}
        virtual void HandleRateRequest(int card, JackAlsaRateDirection dir, long value) = 0;
};

/*!
\brief Serialises rate requests from the control monitor.

Events arrive on the monitor thread and are stored per (card, direction).  A
newer value replaces an older one that has not been handled yet, so a burst of
writes results in one handler call with the latest value.  The handler runs on
the worker thread, never on the RT thread.
*/

class JackAlsaReconfigWorker : public JackRunnableInterface, public JackAlsaControlListener
{

    private:

        typedef std::pair<int, int> Key;

        JackAlsaRateRequestHandler* fHandler;
        std::mutex fMutex;
        std::condition_variable fCond;
        std::map<Key, long> fPending;
        bool fRunning;
        JackThread fThread;

        JackAlsaReconfigWorker(const JackAlsaReconfigWorker&);
        JackAlsaReconfigWorker& operator=(const JackAlsaReconfigWorker&);

    public:

        explicit JackAlsaReconfigWorker(JackAlsaRateRequestHandler* handler);
        virtual ~JackAlsaReconfigWorker();

        // JackAlsaControlListener
        virtual void OnRateEvent(int card, JackAlsaRateDirection dir, long value);

        int Start();
        int Stop();

        virtual bool Init();
        virtual bool Execute();
};

} // end of namespace

#endif
