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

#include "JackAlsaHotplug.h"
#include "JackError.h"

#include <array>
#include <cstdint>
#include <errno.h>
#include <libudev.h>
#include <poll.h>
#include <cstdint>
#include <string.h>
#include <sys/eventfd.h>
#include <unistd.h>

namespace jackmini {

// Quiet time after the last sound event before listeners are told.
static const std::int32_t kSettleMs = 250;

JackAlsaHotplug::JackAlsaHotplug(JackAlsaHotplugListener* listener)
    : fListener(listener)
    , fThread(this)
    , fUdev(nullptr)
    , fMonitor(nullptr)
    , fWakeFd(-1)
{
    fUdev = udev_new();
    if (fUdev == nullptr) {
        jack_error("hot-plug is disabled: udev is not available");
        return;
    }
    fMonitor = udev_monitor_new_from_netlink(fUdev, "udev");
    if (fMonitor == nullptr || udev_monitor_filter_add_match_subsystem_devtype(fMonitor, "sound", nullptr) < 0 || udev_monitor_enable_receiving(fMonitor) < 0) {
        jack_error("hot-plug is disabled: cannot listen to udev");
        if (fMonitor != nullptr) {
            udev_monitor_unref(fMonitor);
            fMonitor = nullptr;
        }
        return;
    }
    fWakeFd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
}

JackAlsaHotplug::~JackAlsaHotplug()
{
    Stop();
    if (fMonitor != nullptr) {
        udev_monitor_unref(fMonitor);
    }
    if (fUdev != nullptr) {
        udev_unref(fUdev);
    }
    if (fWakeFd >= 0) {
        close(fWakeFd);
    }
}

bool JackAlsaHotplug::Init()
{
    return true;
}

bool JackAlsaHotplug::Execute()
{
    std::array<struct pollfd, 2> fds;
    fds[0].fd = udev_monitor_get_fd(fMonitor);
    fds[0].events = POLLIN;
    fds[0].revents = 0;
    fds[1].fd = fWakeFd;
    fds[1].events = POLLIN;
    fds[1].revents = 0;

    // Waits forever for the first event.  After an event, waits only as long as
    // the burst is still coming in.
    bool pending = false;
    std::int32_t ret = poll(fds.data(), 2, -1);
    for (;;) {
        if (ret < 0) {
            if (errno == EINTR) {
                return true;
            }
            jack_error("hot-plug: poll failed (%s)", strerror(errno));
            return false;
        }
        if (fds[1].revents & POLLIN) {
            return false;
        }
        if (fds[0].revents & POLLIN) {
            struct udev_device* device;
            while ((device = udev_monitor_receive_device(fMonitor)) != nullptr) {
                const char* action = udev_device_get_action(device);
                if (action != nullptr && (strcmp(action, "add") == 0 || strcmp(action, "remove") == 0 || strcmp(action, "change") == 0)) {
                    pending = true;
                }
                udev_device_unref(device);
            }
        }
        if (!pending) {
            return true;
        }
        // Keep draining while events arrive; report once they stop.
        fds[0].revents = 0;
        fds[1].revents = 0;
        ret = poll(fds.data(), 2, kSettleMs);
        if (ret == 0) {
            fListener->OnSoundChanged();
            return true;
        }
    }
}

std::int32_t JackAlsaHotplug::Start()
{
    if (fMonitor == nullptr) {
        return 0;
    }
    std::uint64_t dummy;
    while (read(fWakeFd, &dummy, sizeof(dummy)) > 0) {
    }
    if (fThread.StartSync() < 0) {
        jack_error("hot-plug: cannot start the udev thread");
        return -1;
    }
    return 0;
}

std::int32_t JackAlsaHotplug::Stop()
{
    if (fMonitor == nullptr || fWakeFd < 0) {
        return 0;
    }
    std::uint64_t one = 1;
    if (write(fWakeFd, &one, sizeof(one)) < 0) {
        jack_error("hot-plug: cannot wake the udev thread");
    }
    return fThread.Stop();
}

} // namespace jackmini
