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
char* getControlDeviceName(const char* deviceName);

std::string alsaControlDeviceName(const std::string& pcm)
{
    char* name = getControlDeviceName(pcm.c_str());
    std::string result = name ? name : "";
    free(name);
    return result;
}

std::int32_t alsaControlLockCard(const std::string& ctlName)
{
    std::string file = std::string(kServerDir) + "/jack_alsa_";
    for (size_t i = 0; i < ctlName.size(); i++) {
        char c = ctlName[i];
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
            ctlName.c_str());
        close(fd);
        return -1;
    }
    return fd;
}

void alsaControlUnlockCard(std::int32_t fd)
{
    if (fd >= 0) {
        close(fd);
    }
}

std::string alsaRateElementName(AlsaRateDirection dir, std::int32_t device)
{
    std::string name = (dir == kRateCapture) ? "JACK Secondary Capture Rate" : "JACK Secondary Playback Rate";
    if (device >= 0) {
        name += " d" + std::to_string(device);
    }
    return name;
}

struct ElementMatch {
    std::uint32_t m_numid;
    std::uint32_t m_iface;
    std::uint32_t m_index;
};

// Prefer a MIXER element; fall back to a CARD element of the same name.
static bool findElement(snd_ctl_t* ctl, const std::string& name, ElementMatch* match)
{
    snd_ctl_elem_list_t* list;
    bool found = false;
    bool foundMixer = false;

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
        if (iface == SND_CTL_ELEM_IFACE_MIXER && !foundMixer) {
            match->m_numid = snd_ctl_elem_list_get_numid(list, i);
            match->m_iface = iface;
            match->m_index = snd_ctl_elem_list_get_index(list, i);
            found = true;
            foundMixer = true;
        } else if (iface == SND_CTL_ELEM_IFACE_CARD && !found) {
            match->m_numid = snd_ctl_elem_list_get_numid(list, i);
            match->m_iface = iface;
            match->m_index = snd_ctl_elem_list_get_index(list, i);
            found = true;
        }
    }

    snd_ctl_elem_list_free_space(list);
    snd_ctl_elem_list_free(list);
    return found;
}

static std::int32_t elementInfo(snd_ctl_t* ctl, std::uint32_t numid, snd_ctl_elem_info_t* info)
{
    snd_ctl_elem_info_set_numid(info, numid);
    return snd_ctl_elem_info(ctl, info);
}

// Integer element that a direction can use.  Playback needs a writable one.
static bool usable(snd_ctl_t* ctl, const ElementMatch& match, AlsaRateDirection dir, bool* writable)
{
    snd_ctl_elem_info_t* info;
    if (snd_ctl_elem_info_malloc(&info) < 0) {
        return false;
    }
    bool ok = false;
    if (elementInfo(ctl, match.m_numid, info) == 0) {
        *writable = snd_ctl_elem_info_is_writable(info);
        ok = (snd_ctl_elem_info_get_type(info) == SND_CTL_ELEM_TYPE_INTEGER) && (dir == kRateCapture || *writable);
    }
    snd_ctl_elem_info_free(info);
    return ok;
}

static void fill(AlsaRateElement* out, const ElementMatch& match, const std::string& name, bool writable)
{
    out->m_numid = match.m_numid;
    out->m_iface = match.m_iface;
    out->m_index = match.m_index;
    out->m_name = name;
    out->m_user = false;
    out->m_writable = writable;
}

static std::array<const char* const, 7> gKCaptureProbe = {
    "Capture Rate", "Sample Rate", "Rate", "Clock Rate", "Sample Clock Rate", "IEC958 Sample Rate", nullptr
};

static std::array<const char* const, 7> gKPlaybackProbe = {
    "Playback Rate", "Sample Rate", "Rate", "Clock Rate", "Sample Clock Rate", "IEC958 Sample Rate", nullptr
};

std::int32_t alsaControlResolveRate(snd_ctl_t* ctl, AlsaRateDirection dir,
    const std::string& configured,
    AlsaRateElement* out, std::string* error)
{
    ElementMatch match;
    bool writable = false;

    if (!configured.empty()) {
        if (!findElement(ctl, configured, &match)) {
            *error = "native rate control '" + configured + "' not found";
            return -1;
        }
        if (!usable(ctl, match, dir, &writable)) {
            *error = "native rate control '" + configured + "' is not an integer element" + (dir == kRatePlayback ? " that can be written" : "");
            return -1;
        }
        fill(out, match, configured, writable);
        return 0;
    }

    const char* const* probe = (dir == kRateCapture) ? gKCaptureProbe.data() : gKPlaybackProbe.data();
    for (std::int32_t i = 0; probe[i] != nullptr; i++) {
        if (findElement(ctl, probe[i], &match) && usable(ctl, match, dir, &writable)) {
            fill(out, match, probe[i], writable);
            return 0;
        }
    }
    return 1;
}

static void setId(snd_ctl_elem_id_t* id, const std::string& name, std::uint32_t iface, std::uint32_t index)
{
    snd_ctl_elem_id_set_interface(id, (snd_ctl_elem_iface_t)iface);
    snd_ctl_elem_id_set_name(id, name.c_str());
    snd_ctl_elem_id_set_index(id, index);
}

std::int32_t alsaControlCreateRate(snd_ctl_t* ctl, AlsaRateDirection dir, std::int32_t device, std::int64_t initial,
    AlsaRateElement* out)
{
    std::string name = alsaRateElementName(dir, device);
    snd_ctl_elem_id_t* id;
    snd_ctl_elem_id_alloca(&id);
    setId(id, name, SND_CTL_ELEM_IFACE_MIXER, 0);

    std::int32_t err = snd_ctl_elem_add_integer(ctl, id, 1, 1, 768000, 1);
    if (err == -EBUSY) {
        // Another client already holds an element with this name.  If nobody
        // owns it, it was left behind by a crashed JACK and can be replaced.
        ElementMatch stale;
        if (findElement(ctl, name, &stale)) {
            snd_ctl_elem_info_t* info;
            snd_ctl_elem_info_alloca(&info);
            // Only a server that holds the card lock reaches this point, so an
            // element left behind by a crashed server can be replaced.  The
            // kernel reports an unowned element with owner -1.
            std::int32_t owner = (elementInfo(ctl, stale.m_numid, info) == 0) ? (std::int32_t)snd_ctl_elem_info_get_owner(info) : -1;
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
    if (!findElement(ctl, name, &match)) {
        return -ENOENT;
    }

    snd_ctl_elem_value_t* value;
    snd_ctl_elem_value_alloca(&value);
    snd_ctl_elem_value_set_numid(value, match.m_numid);
    snd_ctl_elem_value_set_integer(value, 0, initial);
    if ((err = snd_ctl_elem_write(ctl, value)) < 0) {
        return err;
    }

    fill(out, match, name, true);
    out->m_user = true;
    return 0;
}

std::int32_t alsaControlRemoveRate(snd_ctl_t* ctl, const AlsaRateElement& elem)
{
    snd_ctl_elem_id_t* id;
    snd_ctl_elem_id_alloca(&id);
    setId(id, elem.m_name, elem.m_iface, elem.m_index);
    snd_ctl_elem_id_set_numid(id, elem.m_numid);
    return snd_ctl_elem_remove(ctl, id);
}

std::int32_t alsaControlReadRate(snd_ctl_t* ctl, const AlsaRateElement& elem, std::int64_t* value)
{
    snd_ctl_elem_value_t* v;
    snd_ctl_elem_value_alloca(&v);
    snd_ctl_elem_value_set_numid(v, elem.m_numid);
    std::int32_t err = snd_ctl_elem_read(ctl, v);
    if (err < 0) {
        return err;
    }
    *value = snd_ctl_elem_value_get_integer(v, 0);
    return 0;
}

std::int32_t alsaControlWriteRate(snd_ctl_t* ctl, const AlsaRateElement& elem, std::int64_t value)
{
    snd_ctl_elem_value_t* v;
    snd_ctl_elem_value_alloca(&v);
    snd_ctl_elem_value_set_numid(v, elem.m_numid);
    snd_ctl_elem_value_set_integer(v, 0, value);
    return snd_ctl_elem_write(ctl, v);
}

} // namespace jackmini
