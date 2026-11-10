// Copyright (C) 2026 JACK contributors
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

#pragma once

#include "AlsaCardFailureSink.h"
#include "AlsaControl.h"
#include "AlsaControlMonitor.h"
#include "AlsaHotplug.h"
#include "PlatformPlug.h"
#include "Thread.h"

#include <condition_variable>
#include <cstdint>
#include <map>
#include <mutex>
#include <utility>
#include <vector>

namespace jackmini {

// \brief Handles a rate request on the worker thread.  Not real-time.

class AlsaRateRequestHandler {
public:
    virtual ~AlsaRateRequestHandler()
    {
    }
    virtual void handleRateRequest(std::int32_t card, AlsaRateDirection dir, std::int64_t value) = 0;
    virtual void handleCardFailure(std::int32_t card) = 0;
    virtual void handleSoundChanged() = 0;
};

// \brief Serialises rate requests from the control monitor.
//
// Events arrive on the monitor thread and are stored per (card, direction).  A
// newer value replaces an older one that has not been handled yet, so a burst of
// writes results in one handler call with the latest value.  The handler runs on
// the worker thread, never on the RT thread.

class AlsaReconfigWorker : public RunnableInterface, public AlsaControlListener, public AlsaCardFailureSink, public AlsaHotplugListener {

private:
    typedef std::pair<std::int32_t, std::int32_t> Key;

    AlsaRateRequestHandler* m_handler;
    std::mutex m_mutex;
    std::condition_variable m_cond;
    std::map<Key, std::int64_t> m_pending;
    std::vector<std::int32_t> m_failures;
    bool m_soundChanged;
    bool m_running;
    Thread m_thread;

    AlsaReconfigWorker(const AlsaReconfigWorker&);
    AlsaReconfigWorker& operator=(const AlsaReconfigWorker&);

public:
    explicit AlsaReconfigWorker(AlsaRateRequestHandler* handler);
    virtual ~AlsaReconfigWorker();

    // JackAlsaControlListener
    virtual void onRateEvent(std::int32_t card, AlsaRateDirection dir, std::int64_t value);

    // JackAlsaCardFailureSink.  Failures are handled before rate requests.
    virtual void onCardFailed(std::int32_t cardId);

    // JackAlsaHotplugListener.  Handled after failures, before rate requests.
    virtual void onSoundChanged();

    std::int32_t start();
    std::int32_t stop();

    virtual bool init();
    virtual bool execute();
};

} // namespace jackmini
