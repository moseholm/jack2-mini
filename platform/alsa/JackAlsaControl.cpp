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

#include "JackAlsaControl.h"
#include "JackError.h"

#include "JackPlatformPlug.h"

#include <array>
#include <cstdint>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <unistd.h>
#include <vector>

namespace jackmini {

// Defined in JackAlsaDriver.cpp.
char* get_control_device_name(const char* device_name);

std::string JackAlsaControlDeviceName(const std::string& pcm)
{
    char* name = get_control_device_name(pcm.c_str());
    std::string result = name ? name : "";
    free(name);
    return result;
}

std::int32_t JackAlsaControlLockCard(const std::string& ctl_name)
{
    std::string file = std::string(kServerDir) + "/jack_alsa_";
    for (size_t i = 0; i < ctl_name.size(); i++) {
        char c = ctl_name[i];
        file += (isalnum((unsigned char)c) ? c : '_');
    }
    file += ".lock";

    std::int32_t fd = open(file.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0644);
    if (fd < 0) {
        jack_error("cannot open card lock %s (%s)", file.c_str(), strerror(errno));
        return -1;
    }
    if (flock(fd, LOCK_EX | LOCK_NB) < 0) {
        jack_error("ALSA card %s is already in use by another JACK server or by this server; "
                   "a card may serve only one JACK server",
            ctl_name.c_str());
        close(fd);
        return -1;
    }
    return fd;
}

void JackAlsaControlUnlockCard(std::int32_t fd)
{
    if (fd >= 0) {
        close(fd);
    }
}

std::string JackAlsaRateElementName(JackAlsaRateDirection dir, std::int32_t device)
{
    std::string name = (dir == RateCapture) ? "JACK Secondary Capture Rate" : "JACK Secondary Playback Rate";
    if (device >= 0) {
        name += " d" + std::to_string(device);
    }
    return name;
}

struct ElementMatch {
    std::uint32_t numid;
    std::uint32_t iface;
    std::uint32_t index;
};

// Prefer a MIXER element; fall back to a CARD element of the same name.
static bool FindElement(snd_ctl_t* ctl, const std::string& name, ElementMatch* match)
{
    snd_ctl_elem_list_t* list;
    bool found = false;
    bool found_mixer = false;

    if (snd_ctl_elem_list_malloc(&list) < 0) {
        return false;
    }
    if (snd_ctl_elem_list(ctl, list) < 0) {
        snd_ctl_elem_list_free(list);
        return false;
    }
    std::uint32_t count = snd_ctl_elem_list_get_count(list);
    snd_ctl_elem_list_alloc_space(list, count);
    if (snd_ctl_elem_list(ctl, list) < 0) {
        snd_ctl_elem_list_free_space(list);
        snd_ctl_elem_list_free(list);
        return false;
    }

    count = snd_ctl_elem_list_get_count(list);
    for (std::uint32_t i = 0; i < count; i++) {
        if (name != snd_ctl_elem_list_get_name(list, i)) {
            continue;
        }
        std::uint32_t iface = snd_ctl_elem_list_get_interface(list, i);
        if (iface == SND_CTL_ELEM_IFACE_MIXER && !found_mixer) {
            match->numid = snd_ctl_elem_list_get_numid(list, i);
            match->iface = iface;
            match->index = snd_ctl_elem_list_get_index(list, i);
            found = true;
            found_mixer = true;
        } else if (iface == SND_CTL_ELEM_IFACE_CARD && !found) {
            match->numid = snd_ctl_elem_list_get_numid(list, i);
            match->iface = iface;
            match->index = snd_ctl_elem_list_get_index(list, i);
            found = true;
        }
    }

    snd_ctl_elem_list_free_space(list);
    snd_ctl_elem_list_free(list);
    return found;
}

static std::int32_t ElementInfo(snd_ctl_t* ctl, std::uint32_t numid, snd_ctl_elem_info_t* info)
{
    snd_ctl_elem_info_set_numid(info, numid);
    return snd_ctl_elem_info(ctl, info);
}

// Integer element that a direction can use.  Playback needs a writable one.
static bool Usable(snd_ctl_t* ctl, const ElementMatch& match, JackAlsaRateDirection dir, bool* writable)
{
    snd_ctl_elem_info_t* info;
    if (snd_ctl_elem_info_malloc(&info) < 0) {
        return false;
    }
    bool ok = false;
    if (ElementInfo(ctl, match.numid, info) == 0) {
        *writable = snd_ctl_elem_info_is_writable(info);
        ok = (snd_ctl_elem_info_get_type(info) == SND_CTL_ELEM_TYPE_INTEGER) && (dir == RateCapture || *writable);
    }
    snd_ctl_elem_info_free(info);
    return ok;
}

static void Fill(JackAlsaRateElement* out, const ElementMatch& match, const std::string& name, bool writable)
{
    out->numid = match.numid;
    out->iface = match.iface;
    out->index = match.index;
    out->name = name;
    out->user = false;
    out->writable = writable;
}

static std::array<const char* const, 7> kCaptureProbe = {
    "Capture Rate", "Sample Rate", "Rate", "Clock Rate", "Sample Clock Rate", "IEC958 Sample Rate", nullptr
};

static std::array<const char* const, 7> kPlaybackProbe = {
    "Playback Rate", "Sample Rate", "Rate", "Clock Rate", "Sample Clock Rate", "IEC958 Sample Rate", nullptr
};

std::int32_t JackAlsaControlResolveRate(snd_ctl_t* ctl, JackAlsaRateDirection dir,
    const std::string& configured,
    JackAlsaRateElement* out, std::string* error)
{
    ElementMatch match;
    bool writable = false;

    if (!configured.empty()) {
        if (!FindElement(ctl, configured, &match)) {
            *error = "native rate control '" + configured + "' not found";
            return -1;
        }
        if (!Usable(ctl, match, dir, &writable)) {
            *error = "native rate control '" + configured + "' is not an integer element" + (dir == RatePlayback ? " that can be written" : "");
            return -1;
        }
        Fill(out, match, configured, writable);
        return 0;
    }

    const char* const* probe = (dir == RateCapture) ? kCaptureProbe.data() : kPlaybackProbe.data();
    for (std::int32_t i = 0; probe[i] != nullptr; i++) {
        if (FindElement(ctl, probe[i], &match) && Usable(ctl, match, dir, &writable)) {
            Fill(out, match, probe[i], writable);
            return 0;
        }
    }
    return 1;
}

static void SetId(snd_ctl_elem_id_t* id, const std::string& name, std::uint32_t iface, std::uint32_t index)
{
    snd_ctl_elem_id_set_interface(id, (snd_ctl_elem_iface_t)iface);
    snd_ctl_elem_id_set_name(id, name.c_str());
    snd_ctl_elem_id_set_index(id, index);
}

std::int32_t JackAlsaControlCreateRate(snd_ctl_t* ctl, JackAlsaRateDirection dir, std::int32_t device, std::int64_t initial,
    JackAlsaRateElement* out)
{
    std::string name = JackAlsaRateElementName(dir, device);
    snd_ctl_elem_id_t* id;
    snd_ctl_elem_id_alloca(&id);
    SetId(id, name, SND_CTL_ELEM_IFACE_MIXER, 0);

    std::int32_t err = snd_ctl_elem_add_integer(ctl, id, 1, 1, 768000, 1);
    if (err == -EBUSY) {
        // Another client already holds an element with this name.  If nobody
        // owns it, it was left behind by a crashed JACK and can be replaced.
        ElementMatch stale;
        if (FindElement(ctl, name, &stale)) {
            snd_ctl_elem_info_t* info;
            snd_ctl_elem_info_alloca(&info);
            // Only a server that holds the card lock reaches this point, so an
            // element left behind by a crashed server can be replaced.  The
            // kernel reports an unowned element with owner -1.
            std::int32_t owner = (ElementInfo(ctl, stale.numid, info) == 0) ? (std::int32_t)snd_ctl_elem_info_get_owner(info) : -1;
            if (owner > 0) {
                jack_error("ALSA control '%s' is in use by process %d; another JACK server may be using this card",
                    name.c_str(), owner);
                return -EBUSY;
            }
            if (snd_ctl_elem_remove(ctl, id) == 0) {
                err = snd_ctl_elem_add_integer(ctl, id, 1, 1, 768000, 1);
            }
        }
    }
    if (err < 0) {
        return err;
    }

    // Unlock so applications can write the element.  The kernel then clears
    // the owner, which is also how a stale element is recognised later.
    if (snd_ctl_elem_unlock(ctl, id) < 0) {
        jack_error("cannot unlock ALSA control '%s'", name.c_str());
    }

    ElementMatch match;
    if (!FindElement(ctl, name, &match)) {
        return -ENOENT;
    }

    snd_ctl_elem_value_t* value;
    snd_ctl_elem_value_alloca(&value);
    snd_ctl_elem_value_set_numid(value, match.numid);
    snd_ctl_elem_value_set_integer(value, 0, initial);
    if ((err = snd_ctl_elem_write(ctl, value)) < 0) {
        return err;
    }

    Fill(out, match, name, true);
    out->user = true;
    return 0;
}

std::int32_t JackAlsaControlRemoveRate(snd_ctl_t* ctl, const JackAlsaRateElement& elem)
{
    snd_ctl_elem_id_t* id;
    snd_ctl_elem_id_alloca(&id);
    SetId(id, elem.name, elem.iface, elem.index);
    snd_ctl_elem_id_set_numid(id, elem.numid);
    return snd_ctl_elem_remove(ctl, id);
}

std::int32_t JackAlsaControlReadRate(snd_ctl_t* ctl, const JackAlsaRateElement& elem, std::int64_t* value)
{
    snd_ctl_elem_value_t* v;
    snd_ctl_elem_value_alloca(&v);
    snd_ctl_elem_value_set_numid(v, elem.numid);
    std::int32_t err = snd_ctl_elem_read(ctl, v);
    if (err < 0) {
        return err;
    }
    *value = snd_ctl_elem_value_get_integer(v, 0);
    return 0;
}

std::int32_t JackAlsaControlWriteRate(snd_ctl_t* ctl, const JackAlsaRateElement& elem, std::int64_t value)
{
    snd_ctl_elem_value_t* v;
    snd_ctl_elem_value_alloca(&v);
    snd_ctl_elem_value_set_numid(v, elem.numid);
    snd_ctl_elem_value_set_integer(v, 0, value);
    return snd_ctl_elem_write(ctl, v);
}

} // namespace jackmini
