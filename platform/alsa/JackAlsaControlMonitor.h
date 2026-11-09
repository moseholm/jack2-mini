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

#pragma once

#include "JackAlsaControl.h"
#include "JackPlatformPlug.h"
#include "JackThread.h"

#include <alsa/asoundlib.h>
#include <mutex>
#include <string>
#include <vector>

namespace jackmini {

/*!
\brief Receives rate values read from ALSA control events.
*/

class JackAlsaControlListener {
public:
    virtual ~JackAlsaControlListener()
    {
    }
    virtual void OnRateEvent(int card, JackAlsaRateDirection dir, long value) = 0;
};

/*!
\brief One thread that watches the rate elements of all secondary cards.

Each control device gets one handle, shared by the elements on that device.
The thread subscribes to VALUE events, polls the handles together with a
wake-up eventfd, and reports each value change to the listener.  It never
writes to a control, so writes from other threads use their own handles.
*/

class JackAlsaControlMonitor : public JackRunnableInterface {

private:
    struct Watch {
        int card;
        JackAlsaRateDirection dir;
        JackAlsaRateElement elem;
    };

    struct Device {
        std::string name;
        snd_ctl_t* ctl;
        std::vector<Watch> watches;
        bool dead; // the device went away; it is no longer polled
    };

    JackAlsaControlListener* fListener;
    std::vector<Device*> fDevices;
    JackThread fThread;
    int fWakeFd;
    int fUpdateFd;
    std::mutex fUpdateMutex;
    std::vector<int> fRemovals;
    struct PendingWatch {
        int card;
        std::string ctl_name;
        JackAlsaRateElement elem;
        JackAlsaRateDirection dir;
    };
    std::vector<PendingWatch> fAdditions;
    snd_ctl_event_t* fEvent;

    void Subscribe(Device* device);
    bool Dispatch(Device* device);
    void MarkDead(Device* device);
    void ApplyUpdates();
    void AddPendingWatch(const PendingWatch& watch);

    JackAlsaControlMonitor(const JackAlsaControlMonitor&);
    JackAlsaControlMonitor& operator=(const JackAlsaControlMonitor&);

public:
    explicit JackAlsaControlMonitor(JackAlsaControlListener* listener);
    virtual ~JackAlsaControlMonitor();

    // Must be called before Start().  Opens the device on first use.
    int AddWatch(int card, const std::string& ctl_name, const JackAlsaRateElement& elem,
        JackAlsaRateDirection dir);

    // Starts watching an element while the thread is running.  The thread adds it
    // between polls; the control device is opened and subscribed on that thread.
    void QueueWatch(int card, const std::string& ctl_name, const JackAlsaRateElement& elem,
        JackAlsaRateDirection dir);

    // Stops watching a card's elements.  Safe while the thread is running: the
    // thread applies the removal between polls.  A device with no watches left
    // is closed.
    void RemoveWatches(int card);

    int Start();
    int Stop();

    virtual bool Init();
    virtual bool Execute();
};

} // namespace jackmini
