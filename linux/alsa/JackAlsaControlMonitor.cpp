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

namespace Jack
{

JackAlsaControlMonitor::JackAlsaControlMonitor(JackAlsaControlListener* listener)
    : fListener(listener), fThread(this), fWakeFd(-1), fEvent(NULL)
{}

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

void JackAlsaControlMonitor::Dispatch(Device* device)
{
    while (snd_ctl_read(device->ctl, fEvent) == 1) {
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
}

bool JackAlsaControlMonitor::Execute()
{
    std::vector<struct pollfd> fds;
    std::vector<size_t> first;   // index of each device's first pollfd

    for (size_t i = 0; i < fDevices.size(); i++) {
        int count = snd_ctl_poll_descriptors_count(fDevices[i]->ctl);
        first.push_back(fds.size());
        struct pollfd pfd[8];
        if (count > 8) {
            count = 8;
        }
        snd_ctl_poll_descriptors(fDevices[i]->ctl, pfd, count);
        for (int k = 0; k < count; k++) {
            fds.push_back(pfd[k]);
        }
    }

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

    for (size_t i = 0; i < fDevices.size(); i++) {
        size_t begin = first[i];
        size_t end = (i + 1 < fDevices.size()) ? first[i + 1] : fds.size() - 1;
        unsigned short revents = 0;
        if (end > begin) {
            snd_ctl_poll_descriptors_revents(fDevices[i]->ctl, &fds[begin], end - begin, &revents);
        }
        if (revents & (POLLIN | POLLERR)) {
            Dispatch(fDevices[i]);
        }
    }
    return true;
}

int JackAlsaControlMonitor::Start()
{
    if (fDevices.empty()) {
        return 0;
    }
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
