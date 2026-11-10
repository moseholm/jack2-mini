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

#include "JackAlsaEnumerate.h"
#include "JackCString.h"
#include "JackError.h"
#include <cstdint>
#include <array>

#include <alsa/asoundlib.h>
#include <stdio.h>

namespace jackmini {

// Maximum channels a stream of this device supports, or 0 if it cannot be opened.
static std::int32_t probeChannels(const std::string& pcm, snd_pcm_stream_t stream, std::string* error)
{
    snd_pcm_t* handle;
    std::int32_t err = snd_pcm_open(&handle, pcm.c_str(), stream, SND_PCM_NONBLOCK);
    if (err < 0) {
        *error = snd_strerror(err);
        return 0;
    }

    snd_pcm_hw_params_t* params;
    snd_pcm_hw_params_alloca(&params);
    std::uint32_t channels = 0;
    if (snd_pcm_hw_params_any(handle, params) == 0) {
        snd_pcm_hw_params_get_channels_max(params, &channels);
    }
    snd_pcm_close(handle);
    return (std::int32_t)channels;
}

static bool hasStream(snd_ctl_t* ctl, std::int32_t device, snd_pcm_stream_t stream)
{
    snd_pcm_info_t* info;
    snd_pcm_info_alloca(&info);
    snd_pcm_info_set_device(info, device);
    snd_pcm_info_set_subdevice(info, 0);
    snd_pcm_info_set_stream(info, stream);
    return snd_ctl_pcm_info(ctl, info) >= 0;
}

static const AlsaPcmInfo* findKnown(const std::vector<AlsaPcmInfo>& known,
    const std::string& cardId, std::int32_t device)
{
    for (size_t i = 0; i < known.size(); i++) {
        if (known[i].m_cardId == cardId && known[i].m_device == device) {
            return &known[i];
        }
    }
    return nullptr;
}

std::vector<AlsaPcmInfo> alsaEnumeratePcms(const std::vector<AlsaPcmInfo>& known,
    bool report)
{
    std::vector<AlsaPcmInfo> result;
    std::int32_t card = -1;

    while (snd_card_next(&card) >= 0 && card >= 0) {
        std::array<char, 32> ctlName { };
        jackmini::formatCString(ctlName, "hw:{}", card);

        snd_ctl_t* ctl;
        if (snd_ctl_open(&ctl, ctlName.data(), 0) < 0) {
            continue;
        }

        snd_ctl_card_info_t* cardInfo;
        snd_ctl_card_info_alloca(&cardInfo);
        if (snd_ctl_card_info(ctl, cardInfo) < 0) {
            snd_ctl_close(ctl);
            continue;
        }
        std::string cardId = snd_ctl_card_info_get_id(cardInfo);

        std::int32_t device = -1;
        while (snd_ctl_pcm_next_device(ctl, &device) >= 0 && device >= 0) {
            bool playback = hasStream(ctl, device, SND_PCM_STREAM_PLAYBACK);
            bool capture = hasStream(ctl, device, SND_PCM_STREAM_CAPTURE);
            if (!playback && !capture) {
                continue;
            }

            AlsaPcmInfo info;
            info.m_cardId = cardId;
            info.m_cardNum = card;
            info.m_device = device;
            info.m_pcm = "hw:CARD=" + cardId + ",DEV=" + std::to_string(device);
            info.m_playback = playback;
            info.m_capture = capture;
            info.m_playbackChannels = 0;
            info.m_captureChannels = 0;

            std::string error;
            if (playback) {
                info.m_playbackChannels = probeChannels(info.m_pcm, SND_PCM_STREAM_PLAYBACK, &error);
            }
            if (capture) {
                info.m_captureChannels = probeChannels(info.m_pcm, SND_PCM_STREAM_CAPTURE, &error);
            }

            // Busy now: use what was known.  A device in use by this server is busy by design.
            const AlsaPcmInfo* old = findKnown(known, cardId, device);
            if (old != nullptr) {
                if (info.m_playbackChannels == 0) {
                    info.m_playbackChannels = old->m_playbackChannels;
                }
                if (info.m_captureChannels == 0) {
                    info.m_captureChannels = old->m_captureChannels;
                }
            }

            if (info.m_playbackChannels == 0 && info.m_captureChannels == 0) {
                if (report) {
                    jack_info("ALSA device %s is not available now (%s); skipped",
                        info.m_pcm.c_str(), error.c_str());
                }
                continue;
            }
            info.m_playback = info.m_playbackChannels > 0;
            info.m_capture = info.m_captureChannels > 0;
            result.push_back(info);
        }
        snd_ctl_close(ctl);
    }
    return result;
}

} // namespace jackmini
