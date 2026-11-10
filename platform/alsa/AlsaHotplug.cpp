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

#include "AlsaHotplug.h"
#include "Error.h"

#include <errno.h>
#include <libudev.h>
#include <poll.h>
#include <string.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include <array>
#include <cstdint>

namespace jackmini {

// Quiet time after the last sound event before listeners are told.
static const std::int32_t gKSettleMs = 250;

AlsaHotplug::AlsaHotplug(AlsaHotplugListener* listener)
    : m_listener(listener)
    , m_thread(this)
    , m_udev(nullptr)
    , m_monitor(nullptr)
    , m_wakeFd(-1)
{
    m_udev = udev_new();
    if (m_udev == nullptr) {
        jack_error("hot-plug is disabled: udev is not available");
        return;
    }
    m_monitor = udev_monitor_new_from_netlink(m_udev, "udev");
    if (m_monitor == nullptr || udev_monitor_filter_add_match_subsystem_devtype(m_monitor, "sound", nullptr) < 0 || udev_monitor_enable_receiving(m_monitor) < 0) {
        jack_error("hot-plug is disabled: cannot listen to udev");
        if (m_monitor != nullptr) {
            udev_monitor_unref(m_monitor);
            m_monitor = nullptr;
        }
        return;
    }
    m_wakeFd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
}

AlsaHotplug::~AlsaHotplug()
{
    stop();
    if (m_monitor != nullptr) {
        udev_monitor_unref(m_monitor);
    }
    if (m_udev != nullptr) {
        udev_unref(m_udev);
    }
    if (m_wakeFd >= 0) {
        close(m_wakeFd);
    }
}

bool AlsaHotplug::init()
{
    return true;
}

bool AlsaHotplug::execute()
{
    std::array<struct pollfd, 2> fds;
    fds[0].fd = udev_monitor_get_fd(m_monitor);
    fds[0].events = POLLIN;
    fds[0].revents = 0;
    fds[1].fd = m_wakeFd;
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
            while ((device = udev_monitor_receive_device(m_monitor)) != nullptr) {
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
        ret = poll(fds.data(), 2, gKSettleMs);
        if (ret == 0) {
            m_listener->onSoundChanged();
            return true;
        }
    }
}

std::int32_t AlsaHotplug::start()
{
    if (m_monitor == nullptr) {
        return 0;
    }
    std::uint64_t dummy;
    while (read(m_wakeFd, &dummy, sizeof(dummy)) > 0) {
    }
    if (m_thread.startSync() < 0) {
        jack_error("hot-plug: cannot start the udev thread");
        return -1;
    }
    return 0;
}

std::int32_t AlsaHotplug::stop()
{
    if (m_monitor == nullptr || m_wakeFd < 0) {
        return 0;
    }
    std::uint64_t one = 1;
    if (write(m_wakeFd, &one, sizeof(one)) < 0) {
        jack_error("hot-plug: cannot wake the udev thread");
    }
    return m_thread.stop();
}

} // namespace jackmini
