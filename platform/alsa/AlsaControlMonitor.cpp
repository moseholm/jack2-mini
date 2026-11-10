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

#include "AlsaControlMonitor.h"
#include "Error.h"

#include <errno.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include <array>
#include <cstdint>

namespace jackmini {

AlsaControlMonitor::AlsaControlMonitor(AlsaControlListener* listener)
    : m_listener(listener)
    , m_thread(this)
    , m_wakeFd(-1)
    , m_updateFd(-1)
    , m_event(nullptr)
{
    m_updateFd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
}

AlsaControlMonitor::~AlsaControlMonitor()
{
    stop();
    for (size_t i = 0; i < m_devices.size(); i++) {
        snd_ctl_close(m_devices[i]->m_ctl);
        delete m_devices[i];
    }
    if (m_event) {
        snd_ctl_event_free(m_event);
    }
    if (m_wakeFd >= 0) {
        close(m_wakeFd);
    }
    if (m_updateFd >= 0) {
        close(m_updateFd);
    }
}

void AlsaControlMonitor::removeWatches(std::int32_t card)
{
    {
        std::lock_guard<std::mutex> lock(m_updateMutex);
        m_removals.push_back(card);
    }
    if (m_updateFd >= 0) {
        std::uint64_t one = 1;
        if (write(m_updateFd, &one, sizeof(one)) < 0) {
            jack_error("control monitor: cannot signal a watch change");
        }
    }
}

void AlsaControlMonitor::queueWatch(std::int32_t card, const std::string& ctlName,
    const AlsaRateElement& elem, AlsaRateDirection dir)
{
    PendingWatch watch;
    watch.m_card = card;
    watch.m_ctlName = ctlName;
    watch.m_elem = elem;
    watch.m_dir = dir;
    {
        std::lock_guard<std::mutex> lock(m_updateMutex);
        m_additions.push_back(watch);
    }
    if (m_updateFd >= 0) {
        std::uint64_t one = 1;
        if (write(m_updateFd, &one, sizeof(one)) < 0) {
            jack_error("control monitor: cannot signal a watch change");
        }
    }
}

// Runs on the monitor thread.
void AlsaControlMonitor::addPendingWatch(const PendingWatch& pending)
{
    Device* device = nullptr;
    for (size_t i = 0; i < m_devices.size(); i++) {
        if (m_devices[i]->m_name == pending.m_ctlName) {
            device = m_devices[i];
            break;
        }
    }
    if (device == nullptr) {
        snd_ctl_t* ctl;
        std::int32_t err = snd_ctl_open(&ctl, pending.m_ctlName.c_str(), 0);
        if (err < 0) {
            jack_error("control monitor: cannot open %s for events (%s)",
                pending.m_ctlName.c_str(), snd_strerror(err));
            return;
        }
        device = new Device;
        device->m_name = pending.m_ctlName;
        device->m_ctl = ctl;
        device->m_dead = false;
        m_devices.push_back(device);
        subscribe(device);
    }

    Watch watch;
    watch.m_card = pending.m_card;
    watch.m_dir = pending.m_dir;
    watch.m_elem = pending.m_elem;
    device->m_watches.push_back(watch);
}

void AlsaControlMonitor::applyUpdates()
{
    std::vector<std::int32_t> cards;
    std::vector<PendingWatch> additions;
    {
        std::lock_guard<std::mutex> lock(m_updateMutex);
        cards.swap(m_removals);
        additions.swap(m_additions);
    }
    std::uint64_t dummy;
    while (read(m_updateFd, &dummy, sizeof(dummy)) > 0) {
    }

    for (size_t a = 0; a < additions.size(); a++) {
        addPendingWatch(additions[a]);
    }

    for (size_t c = 0; c < cards.size(); c++) {
        for (size_t i = m_devices.size(); i-- > 0;) {
            Device* device = m_devices[i];
            for (size_t w = device->m_watches.size(); w-- > 0;) {
                if (device->m_watches[w].m_card == cards[c]) {
                    device->m_watches.erase(device->m_watches.begin() + w);
                }
            }
            if (device->m_watches.empty()) {
                snd_ctl_close(device->m_ctl);
                delete device;
                m_devices.erase(m_devices.begin() + i);
            }
        }
    }
}

std::int32_t AlsaControlMonitor::addWatch(std::int32_t card, const std::string& ctlName,
    const AlsaRateElement& elem, AlsaRateDirection dir)
{
    Device* device = nullptr;
    for (size_t i = 0; i < m_devices.size(); i++) {
        if (m_devices[i]->m_name == ctlName) {
            device = m_devices[i];
            break;
        }
    }

    if (device == nullptr) {
        snd_ctl_t* ctl;
        std::int32_t err = snd_ctl_open(&ctl, ctlName.c_str(), 0);
        if (err < 0) {
            jack_error("secondary card control '%s': cannot open for events (%s)",
                ctlName.c_str(), snd_strerror(err));
            return err;
        }
        device = new Device;
        device->m_name = ctlName;
        device->m_ctl = ctl;
        device->m_dead = false;
        m_devices.push_back(device);
    }

    Watch watch;
    watch.m_card = card;
    watch.m_dir = dir;
    watch.m_elem = elem;
    device->m_watches.push_back(watch);
    return 0;
}

void AlsaControlMonitor::subscribe(Device* device)
{
    snd_ctl_nonblock(device->m_ctl, 1);
    snd_ctl_subscribe_events(device->m_ctl, 1);

    // Discard events queued before we subscribed.  Callers read the current
    // values themselves, so these are not changes.
    while (snd_ctl_read(device->m_ctl, m_event) == 1) {
    }
}

bool AlsaControlMonitor::init()
{
    if (snd_ctl_event_malloc(&m_event) < 0) {
        jack_error("control monitor: cannot allocate event");
        return false;
    }
    for (size_t i = 0; i < m_devices.size(); i++) {
        subscribe(m_devices[i]);
    }
    return true;
}

// Reads the pending events on a device.  Returns false when the device can no
// longer be read, which means it has gone away.
bool AlsaControlMonitor::dispatch(Device* device)
{
    std::int32_t ret;
    while ((ret = snd_ctl_read(device->m_ctl, m_event)) == 1) {
        if (snd_ctl_event_get_type(m_event) != SND_CTL_EVENT_ELEM) {
            continue;
        }
        if (!(snd_ctl_event_elem_get_mask(m_event) & SND_CTL_EVENT_MASK_VALUE)) {
            continue;
        }

        std::uint32_t numid = snd_ctl_event_elem_get_numid(m_event);
        for (size_t w = 0; w < device->m_watches.size(); w++) {
            const Watch& watch = device->m_watches[w];
            if (watch.m_elem.m_numid != numid) {
                continue;
            }
            std::int64_t value = 0;
            if (alsaControlReadRate(device->m_ctl, watch.m_elem, &value) == 0 && m_listener) {
                m_listener->onRateEvent(watch.m_card, watch.m_dir, value);
            }
        }
    }
    return ret == 0 || ret == -EAGAIN;
}

void AlsaControlMonitor::markDead(Device* device)
{
    if (device->m_dead) {
        return;
    }
    device->m_dead = true;
    jack_error("ALSA control device %s went away; its rate controls are no longer monitored",
        device->m_name.c_str());
}

bool AlsaControlMonitor::execute()
{
    // Each live device contributes its poll descriptors.  Dead devices are left out
    // entirely: a removed device keeps reporting POLLNVAL or POLLHUP, and polling it
    // again would spin the thread.
    struct Range {
        Device* m_device;
        size_t m_begin;
        size_t m_count;
    };
    std::vector<struct pollfd> fds;
    std::vector<Range> ranges;

    for (size_t i = 0; i < m_devices.size(); i++) {
        Device* device = m_devices[i];
        if (device->m_dead) {
            continue;
        }
        std::array<struct pollfd, 8> pfd;
        std::int32_t count = snd_ctl_poll_descriptors_count(device->m_ctl);
        if (count <= 0 || count > 8 || snd_ctl_poll_descriptors(device->m_ctl, pfd.data(), count) < 0) {
            markDead(device);
            continue;
        }
        Range range = { device, fds.size(), (size_t)count };
        for (std::int32_t k = 0; k < count; k++) {
            fds.push_back(pfd[k]);
        }
        ranges.push_back(range);
    }

    struct pollfd update;
    update.fd = m_updateFd;
    update.events = POLLIN;
    update.revents = 0;
    fds.push_back(update);

    struct pollfd wake;
    wake.fd = m_wakeFd;
    wake.events = POLLIN;
    wake.revents = 0;
    fds.push_back(wake);

    std::int32_t ret = poll(&fds[0], fds.size(), -1);
    if (ret < 0) {
        if (errno == EINTR) {
            return true;
        }
        jack_error("control monitor: poll failed (%s)", strerror(errno));
        return false;
    }

    if (fds.back().revents & POLLIN) {
        return false;
    }

    if (fds[fds.size() - 2].revents & POLLIN) {
        applyUpdates();
        return true;
    }

    for (size_t i = 0; i < ranges.size(); i++) {
        Device* device = ranges[i].m_device;
        std::uint16_t revents = 0;
        if (snd_ctl_poll_descriptors_revents(device->m_ctl, &fds[ranges[i].m_begin],
                ranges[i].m_count, &revents)
            < 0) {
            markDead(device);
            continue;
        }
        if (revents & POLLIN) {
            if (!dispatch(device)) {
                markDead(device);
                continue;
            }
        }
        if (revents & (POLLERR | POLLHUP | POLLNVAL)) {
            markDead(device);
        }
    }
    return true;
}

std::int32_t AlsaControlMonitor::start()
{
    // Runs even with nothing watched yet: watches can be queued while it runs.
    if (m_wakeFd < 0) {
        m_wakeFd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
        if (m_wakeFd < 0) {
            jack_error("control monitor: cannot create wake-up fd (%s)", strerror(errno));
            return -1;
        }
    }
    // Drain a wake-up left over from an earlier Stop().
    std::uint64_t dummy;
    while (read(m_wakeFd, &dummy, sizeof(dummy)) > 0) {
    }
    if (m_thread.startSync() < 0) {
        jack_error("control monitor: cannot start thread");
        return -1;
    }
    return 0;
}

std::int32_t AlsaControlMonitor::stop()
{
    if (m_wakeFd >= 0) {
        std::uint64_t one = 1;
        if (write(m_wakeFd, &one, sizeof(one)) < 0) {
            jack_error("control monitor: cannot wake thread");
        }
    }
    return m_thread.stop();
}

} // namespace jackmini
