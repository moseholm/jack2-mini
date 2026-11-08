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

#include <array>
#include "JackCString.h"
#include "JackAlsaEnumerate.h"
#include "JackError.h"

#include <alsa/asoundlib.h>
#include <stdio.h>

namespace Jack
{

// Maximum channels a stream of this device supports, or 0 if it cannot be opened.
static int ProbeChannels(const std::string& pcm, snd_pcm_stream_t stream, std::string* error)
{
    snd_pcm_t* handle;
    int err = snd_pcm_open(&handle, pcm.c_str(), stream, SND_PCM_NONBLOCK);
    if (err < 0) {
        *error = snd_strerror(err);
        return 0;
    }

    snd_pcm_hw_params_t* params;
    snd_pcm_hw_params_alloca(&params);
    unsigned int channels = 0;
    if (snd_pcm_hw_params_any(handle, params) == 0) {
        snd_pcm_hw_params_get_channels_max(params, &channels);
    }
    snd_pcm_close(handle);
    return (int)channels;
}

static bool HasStream(snd_ctl_t* ctl, int device, snd_pcm_stream_t stream)
{
    snd_pcm_info_t* info;
    snd_pcm_info_alloca(&info);
    snd_pcm_info_set_device(info, device);
    snd_pcm_info_set_subdevice(info, 0);
    snd_pcm_info_set_stream(info, stream);
    return snd_ctl_pcm_info(ctl, info) >= 0;
}

static const JackAlsaPcmInfo* FindKnown(const std::vector<JackAlsaPcmInfo>& known,
                                        const std::string& card_id, int device)
{
    for (size_t i = 0; i < known.size(); i++) {
        if (known[i].card_id == card_id && known[i].device == device) {
            return &known[i];
        }
    }
    return NULL;
}

std::vector<JackAlsaPcmInfo> JackAlsaEnumeratePcms(const std::vector<JackAlsaPcmInfo>& known,
                                                   bool report)
{
    std::vector<JackAlsaPcmInfo> result;
    int card = -1;

    while (snd_card_next(&card) >= 0 && card >= 0) {
        std::array<char, 32> ctl_name{};
        Jack::FormatCString(ctl_name, "hw:{}", card);

        snd_ctl_t* ctl;
        if (snd_ctl_open(&ctl, ctl_name.data(), 0) < 0) {
            continue;
        }

        snd_ctl_card_info_t* card_info;
        snd_ctl_card_info_alloca(&card_info);
        if (snd_ctl_card_info(ctl, card_info) < 0) {
            snd_ctl_close(ctl);
            continue;
        }
        std::string card_id = snd_ctl_card_info_get_id(card_info);

        int device = -1;
        while (snd_ctl_pcm_next_device(ctl, &device) >= 0 && device >= 0) {
            bool playback = HasStream(ctl, device, SND_PCM_STREAM_PLAYBACK);
            bool capture = HasStream(ctl, device, SND_PCM_STREAM_CAPTURE);
            if (!playback && !capture) {
                continue;
            }

            JackAlsaPcmInfo info;
            info.card_id = card_id;
            info.card_num = card;
            info.device = device;
            info.pcm = "hw:CARD=" + card_id + ",DEV=" + std::to_string(device);
            info.playback = playback;
            info.capture = capture;
            info.playback_channels = 0;
            info.capture_channels = 0;

            std::string error;
            if (playback) {
                info.playback_channels = ProbeChannels(info.pcm, SND_PCM_STREAM_PLAYBACK, &error);
            }
            if (capture) {
                info.capture_channels = ProbeChannels(info.pcm, SND_PCM_STREAM_CAPTURE, &error);
            }

            // Busy now: use what was known.  A device in use by this server is busy by design.
            const JackAlsaPcmInfo* old = FindKnown(known, card_id, device);
            if (old != NULL) {
                if (info.playback_channels == 0) {
                    info.playback_channels = old->playback_channels;
                }
                if (info.capture_channels == 0) {
                    info.capture_channels = old->capture_channels;
                }
            }

            if (info.playback_channels == 0 && info.capture_channels == 0) {
                if (report) {
                    jack_info("ALSA device %s is not available now (%s); skipped",
                              info.pcm.c_str(), error.c_str());
                }
                continue;
            }
            info.playback = info.playback_channels > 0;
            info.capture = info.capture_channels > 0;
            result.push_back(info);
        }
        snd_ctl_close(ctl);
    }
    return result;
}

} // end of namespace
