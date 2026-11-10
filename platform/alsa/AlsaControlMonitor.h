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

#include "AlsaControl.h"
#include "PlatformPlug.h"
#include "Thread.h"

#include <alsa/asoundlib.h>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace jackmini {

// \brief Receives rate values read from ALSA control events.

class AlsaControlListener {
public:
    virtual ~AlsaControlListener()
    {
    }
    virtual void onRateEvent(std::int32_t card, AlsaRateDirection dir, std::int64_t value) = 0;
};

// \brief One thread that watches the rate elements of all secondary cards.
//
// Each control device gets one handle, shared by the elements on that device.
// The thread subscribes to VALUE events, polls the handles together with a
// wake-up eventfd, and reports each value change to the listener.  It never
// writes to a control, so writes from other threads use their own handles.

class AlsaControlMonitor : public RunnableInterface {

private:
    struct Watch {
        std::int32_t m_card;
        AlsaRateDirection m_dir;
        AlsaRateElement m_elem;
    };

    struct Device {
        std::string m_name;
        snd_ctl_t* m_ctl;
        std::vector<Watch> m_watches;
        bool m_dead; // the device went away; it is no longer polled
    };

    AlsaControlListener* m_listener;
    std::vector<Device*> m_devices;
    Thread m_thread;
    std::int32_t m_wakeFd;
    std::int32_t m_updateFd;
    std::mutex m_updateMutex;
    std::vector<std::int32_t> m_removals;
    struct PendingWatch {
        std::int32_t m_card;
        std::string m_ctlName;
        AlsaRateElement m_elem;
        AlsaRateDirection m_dir;
    };
    std::vector<PendingWatch> m_additions;
    snd_ctl_event_t* m_event;

    void subscribe(Device* device);
    bool dispatch(Device* device);
    void markDead(Device* device);
    void applyUpdates();
    void addPendingWatch(const PendingWatch& watch);

    AlsaControlMonitor(const AlsaControlMonitor&);
    AlsaControlMonitor& operator=(const AlsaControlMonitor&);

public:
    explicit AlsaControlMonitor(AlsaControlListener* listener);
    virtual ~AlsaControlMonitor();

    // Must be called before Start().  Opens the device on first use.
    std::int32_t addWatch(std::int32_t card, const std::string& ctlName, const AlsaRateElement& elem,
        AlsaRateDirection dir);

    // Starts watching an element while the thread is running.  The thread adds it
    // between polls; the control device is opened and subscribed on that thread.
    void queueWatch(std::int32_t card, const std::string& ctlName, const AlsaRateElement& elem,
        AlsaRateDirection dir);

    // Stops watching a card's elements.  Safe while the thread is running: the
    // thread applies the removal between polls.  A device with no watches left
    // is closed.
    void removeWatches(std::int32_t card);

    std::int32_t start();
    std::int32_t stop();

    virtual bool init();
    virtual bool execute();
};

} // namespace jackmini
