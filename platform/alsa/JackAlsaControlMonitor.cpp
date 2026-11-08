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

#include "JackAlsaControlMonitor.h"
#include "JackError.h"

#include <errno.h>
#include <poll.h>
#include <stdint.h>
#include <sys/eventfd.h>
#include <unistd.h>

namespace jackmini
{

JackAlsaControlMonitor::JackAlsaControlMonitor(JackAlsaControlListener* listener)
    : fListener(listener), fThread(this), fWakeFd(-1), fUpdateFd(-1), fEvent(NULL)
{
    fUpdateFd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
}

JackAlsaControlMonitor::~JackAlsaControlMonitor()
{
    Stop();
    for (size_t i = 0; i < fDevices.size(); i++) {
        snd_ctl_close(fDevices[i]->ctl);
        delete fDevices[i];
    }
    if (fEvent) {
        snd_ctl_event_free(fEvent);
    }
    if (fWakeFd >= 0) {
        close(fWakeFd);
    }
    if (fUpdateFd >= 0) {
        close(fUpdateFd);
    }
}

void JackAlsaControlMonitor::RemoveWatches(int card)
{
    {
        std::lock_guard<std::mutex> lock(fUpdateMutex);
        fRemovals.push_back(card);
    }
    if (fUpdateFd >= 0) {
        uint64_t one = 1;
        if (write(fUpdateFd, &one, sizeof(one)) < 0) {
            jack_error("control monitor: cannot signal a watch change");
        }
    }
}

void JackAlsaControlMonitor::QueueWatch(int card, const std::string& ctl_name,
                                        const JackAlsaRateElement& elem, JackAlsaRateDirection dir)
{
    PendingWatch watch;
    watch.card = card;
    watch.ctl_name = ctl_name;
    watch.elem = elem;
    watch.dir = dir;
    {
        std::lock_guard<std::mutex> lock(fUpdateMutex);
        fAdditions.push_back(watch);
    }
    if (fUpdateFd >= 0) {
        uint64_t one = 1;
        if (write(fUpdateFd, &one, sizeof(one)) < 0) {
            jack_error("control monitor: cannot signal a watch change");
        }
    }
}

// Runs on the monitor thread.
void JackAlsaControlMonitor::AddPendingWatch(const PendingWatch& pending)
{
    Device* device = NULL;
    for (size_t i = 0; i < fDevices.size(); i++) {
        if (fDevices[i]->name == pending.ctl_name) {
            device = fDevices[i];
            break;
        }
    }
    if (device == NULL) {
        snd_ctl_t* ctl;
        int err = snd_ctl_open(&ctl, pending.ctl_name.c_str(), 0);
        if (err < 0) {
            jack_error("control monitor: cannot open %s for events (%s)",
                       pending.ctl_name.c_str(), snd_strerror(err));
            return;
        }
        device = new Device;
        device->name = pending.ctl_name;
        device->ctl = ctl;
        device->dead = false;
        fDevices.push_back(device);
        Subscribe(device);
    }

    Watch watch;
    watch.card = pending.card;
    watch.dir = pending.dir;
    watch.elem = pending.elem;
    device->watches.push_back(watch);
}

void JackAlsaControlMonitor::ApplyUpdates()
{
    std::vector<int> cards;
    std::vector<PendingWatch> additions;
    {
        std::lock_guard<std::mutex> lock(fUpdateMutex);
        cards.swap(fRemovals);
        additions.swap(fAdditions);
    }
    uint64_t dummy;
    while (read(fUpdateFd, &dummy, sizeof(dummy)) > 0) {
    }

    for (size_t a = 0; a < additions.size(); a++) {
        AddPendingWatch(additions[a]);
    }

    for (size_t c = 0; c < cards.size(); c++) {
        for (size_t i = fDevices.size(); i-- > 0;) {
            Device* device = fDevices[i];
            for (size_t w = device->watches.size(); w-- > 0;) {
                if (device->watches[w].card == cards[c]) {
                    device->watches.erase(device->watches.begin() + w);
                }
            }
            if (device->watches.empty()) {
                snd_ctl_close(device->ctl);
                delete device;
                fDevices.erase(fDevices.begin() + i);
            }
        }
    }
}

int JackAlsaControlMonitor::AddWatch(int card, const std::string& ctl_name,
                                     const JackAlsaRateElement& elem, JackAlsaRateDirection dir)
{
    Device* device = NULL;
    for (size_t i = 0; i < fDevices.size(); i++) {
        if (fDevices[i]->name == ctl_name) {
            device = fDevices[i];
            break;
        }
    }

    if (device == NULL) {
        snd_ctl_t* ctl;
        int err = snd_ctl_open(&ctl, ctl_name.c_str(), 0);
        if (err < 0) {
            jack_error("secondary card control '%s': cannot open for events (%s)",
                       ctl_name.c_str(), snd_strerror(err));
            return err;
        }
        device = new Device;
        device->name = ctl_name;
        device->ctl = ctl;
        device->dead = false;
        fDevices.push_back(device);
    }

    Watch watch;
    watch.card = card;
    watch.dir = dir;
    watch.elem = elem;
    device->watches.push_back(watch);
    return 0;
}

void JackAlsaControlMonitor::Subscribe(Device* device)
{
    snd_ctl_nonblock(device->ctl, 1);
    snd_ctl_subscribe_events(device->ctl, 1);

    // Discard events queued before we subscribed.  Callers read the current
    // values themselves, so these are not changes.
    while (snd_ctl_read(device->ctl, fEvent) == 1) {
    }
}

bool JackAlsaControlMonitor::Init()
{
    if (snd_ctl_event_malloc(&fEvent) < 0) {
        jack_error("control monitor: cannot allocate event");
        return false;
    }
    for (size_t i = 0; i < fDevices.size(); i++) {
        Subscribe(fDevices[i]);
    }
    return true;
}

// Reads the pending events on a device.  Returns false when the device can no
// longer be read, which means it has gone away.
bool JackAlsaControlMonitor::Dispatch(Device* device)
{
    int ret;
    while ((ret = snd_ctl_read(device->ctl, fEvent)) == 1) {
        if (snd_ctl_event_get_type(fEvent) != SND_CTL_EVENT_ELEM) {
            continue;
        }
        if (!(snd_ctl_event_elem_get_mask(fEvent) & SND_CTL_EVENT_MASK_VALUE)) {
            continue;
        }

        unsigned int numid = snd_ctl_event_elem_get_numid(fEvent);
        for (size_t w = 0; w < device->watches.size(); w++) {
            const Watch& watch = device->watches[w];
            if (watch.elem.numid != numid) {
                continue;
            }
            long value = 0;
            if (JackAlsaControlReadRate(device->ctl, watch.elem, &value) == 0 && fListener) {
                fListener->OnRateEvent(watch.card, watch.dir, value);
            }
        }
    }
    return ret == 0 || ret == -EAGAIN;
}

void JackAlsaControlMonitor::MarkDead(Device* device)
{
    if (device->dead) {
        return;
    }
    device->dead = true;
    jack_error("ALSA control device %s went away; its rate controls are no longer monitored",
               device->name.c_str());
}

bool JackAlsaControlMonitor::Execute()
{
    // Each live device contributes its poll descriptors.  Dead devices are left out
    // entirely: a removed device keeps reporting POLLNVAL or POLLHUP, and polling it
    // again would spin the thread.
    struct Range {
        Device* device;
        size_t begin;
        size_t count;
    };
    std::vector<struct pollfd> fds;
    std::vector<Range> ranges;

    for (size_t i = 0; i < fDevices.size(); i++) {
        Device* device = fDevices[i];
        if (device->dead) {
            continue;
        }
        struct pollfd pfd[8];
        int count = snd_ctl_poll_descriptors_count(device->ctl);
        if (count <= 0 || count > 8 ||
            snd_ctl_poll_descriptors(device->ctl, pfd, count) < 0) {
            MarkDead(device);
            continue;
        }
        Range range = { device, fds.size(), (size_t)count };
        for (int k = 0; k < count; k++) {
            fds.push_back(pfd[k]);
        }
        ranges.push_back(range);
    }

    struct pollfd update;
    update.fd = fUpdateFd;
    update.events = POLLIN;
    update.revents = 0;
    fds.push_back(update);

    struct pollfd wake;
    wake.fd = fWakeFd;
    wake.events = POLLIN;
    wake.revents = 0;
    fds.push_back(wake);

    int ret = poll(&fds[0], fds.size(), -1);
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
        ApplyUpdates();
        return true;
    }

    for (size_t i = 0; i < ranges.size(); i++) {
        Device* device = ranges[i].device;
        unsigned short revents = 0;
        if (snd_ctl_poll_descriptors_revents(device->ctl, &fds[ranges[i].begin],
                                             ranges[i].count, &revents) < 0) {
            MarkDead(device);
            continue;
        }
        if (revents & POLLIN) {
            if (!Dispatch(device)) {
                MarkDead(device);
                continue;
            }
        }
        if (revents & (POLLERR | POLLHUP | POLLNVAL)) {
            MarkDead(device);
        }
    }
    return true;
}

int JackAlsaControlMonitor::Start()
{
    // Runs even with nothing watched yet: watches can be queued while it runs.
    if (fWakeFd < 0) {
        fWakeFd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
        if (fWakeFd < 0) {
            jack_error("control monitor: cannot create wake-up fd (%s)", strerror(errno));
            return -1;
        }
    }
    // Drain a wake-up left over from an earlier Stop().
    uint64_t dummy;
    while (read(fWakeFd, &dummy, sizeof(dummy)) > 0) {
    }
    if (fThread.StartSync() < 0) {
        jack_error("control monitor: cannot start thread");
        return -1;
    }
    return 0;
}

int JackAlsaControlMonitor::Stop()
{
    if (fWakeFd >= 0) {
        uint64_t one = 1;
        if (write(fWakeFd, &one, sizeof(one)) < 0) {
            jack_error("control monitor: cannot wake thread");
        }
    }
    return fThread.Stop();
}

} // end of namespace
