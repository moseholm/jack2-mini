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

#include <cstdint>

#include "PlatformPlug.h"
#include "Thread.h"

struct udev;
struct udev_monitor;

namespace jackmini {

// \brief Told when the sound devices on the system have changed.
//
// Called once a burst of udev events has settled, so one device change that
// produces several events is reported once.

class AlsaHotplugListener {
public:
    virtual ~AlsaHotplugListener()
    {
    }
    virtual void onSoundChanged() = 0;
};

// \brief Watches the kernel's sound devices through udev.
//
// No polling: the thread sleeps until udev reports an add, remove or change in
// the "sound" subsystem.  If udev is not available the watcher stays silent and
// the server runs with the cards it found at startup.

class AlsaHotplug : public RunnableInterface {

private:
    AlsaHotplugListener* m_listener;
    Thread m_thread;
    struct udev* m_udev;
    struct udev_monitor* m_monitor;
    std::int32_t m_wakeFd;

    AlsaHotplug(const AlsaHotplug&);
    AlsaHotplug& operator=(const AlsaHotplug&);

public:
    explicit AlsaHotplug(AlsaHotplugListener* listener);
    virtual ~AlsaHotplug();

    bool available() const { return m_monitor != nullptr; }

    std::int32_t start();
    std::int32_t stop();

    virtual bool init();
    virtual bool execute();
};

} // namespace jackmini
