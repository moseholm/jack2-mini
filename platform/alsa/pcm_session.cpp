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

#include "pcm_session.hpp"

#include "JackError.h"
#include "JackLog.h"

namespace jackmini::alsa {

namespace {

// The copy from a JACK float buffer to a device playback area.  Null when the format has no conversion.
WriteCopyFunction select_write(const StreamInfo& info, DitherAlgorithm dither)
{
    const bool swapped = info.byte_swapped;

    if (info.format == SND_PCM_FORMAT_FLOAT_LE) {
        return sample_move_dS_floatLE;
    }

    switch (info.sample_bytes) {
    case 2:
        switch (dither) {
        case Rectangular:
            jack_info("Rectangular dithering at 16 bits");
            return swapped ? sample_move_dither_rect_d16_sSs : sample_move_dither_rect_d16_sS;
        case Triangular:
            jack_info("Triangular dithering at 16 bits");
            return swapped ? sample_move_dither_tri_d16_sSs : sample_move_dither_tri_d16_sS;
        case Shaped:
            jack_info("Noise-shaped dithering at 16 bits");
            return swapped ? sample_move_dither_shaped_d16_sSs : sample_move_dither_shaped_d16_sS;
        default:
            return swapped ? sample_move_d16_sSs : sample_move_d16_sS;
        }
    case 3:
        return swapped ? sample_move_d24_sSs : sample_move_d24_sS;
    case 4:
        switch (info.format) {
        case SND_PCM_FORMAT_S24_LE:
        case SND_PCM_FORMAT_S24_BE:
            return swapped ? sample_move_d32l24_sSs : sample_move_d32l24_sS;
        case SND_PCM_FORMAT_S32_LE:
        case SND_PCM_FORMAT_S32_BE:
            if (info.sbits == 32) {
                return swapped ? sample_move_d32_sSs : sample_move_d32_sS;
            }
            if (info.sbits == 24) {
                jack_log("sample format is SND_PCM_FORMAT_S32 but only 24 bits available");
                return swapped ? sample_move_d32u24_sSs : sample_move_d32u24_sS;
            }
            jack_error("unsupported sample format for playback: SND_PCM_FORMAT_S32 with %u bits", info.sbits);
            return nullptr;
        default:
            jack_error("unsupported 4 byte sample_format");
            return nullptr;
        }
    default:
        jack_error("impossible sample width (%u) discovered!", info.sample_bytes);
        return nullptr;
    }
}

// The copy from a device capture area to a JACK float buffer.  Null when the format has no conversion.
ReadCopyFunction select_read(const StreamInfo& info)
{
    const bool swapped = info.byte_swapped;

    if (info.format == SND_PCM_FORMAT_FLOAT_LE) {
        return sample_move_floatLE_sSs;
    }

    switch (info.sample_bytes) {
    case 2:
        return swapped ? sample_move_dS_s16s : sample_move_dS_s16;
    case 3:
        return swapped ? sample_move_dS_s24s : sample_move_dS_s24;
    case 4:
        switch (info.format) {
        case SND_PCM_FORMAT_S24_LE:
        case SND_PCM_FORMAT_S24_BE:
            return swapped ? sample_move_dS_s32l24s : sample_move_dS_s32l24;
        case SND_PCM_FORMAT_S32_LE:
        case SND_PCM_FORMAT_S32_BE:
            if (info.sbits == 32) {
                return swapped ? sample_move_dS_s32s : sample_move_dS_s32;
            }
            if (info.sbits == 24) {
                jack_log("sample format is SND_PCM_FORMAT_S32 but only 24 bits available");
                return swapped ? sample_move_dS_s32u24s : sample_move_dS_s32u24;
            }
            jack_error("unsupported sample format for capture: SND_PCM_FORMAT_S32 with %u bits", info.sbits);
            return nullptr;
        default:
            jack_error("unsupported 4 byte sample_format");
            return nullptr;
        }
    default:
        jack_error("impossible sample width (%u) discovered!", info.sample_bytes);
        return nullptr;
    }
}

} // namespace

Session::~Session()
{
    close();
}

int Session::open(const char* capture_name, const char* playback_name, Request& request, DitherAlgorithm dither)
{
    close();
    if (device_.open(capture_name, playback_name, request) < 0) {
        return -1;
    }

    if (device_.has_playback()) {
        write_copy_ = select_write(device_.info(Stream::Playback), dither);
        if (write_copy_ == nullptr) {
            close();
            return -1;
        }
    }
    if (device_.has_capture()) {
        read_copy_ = select_read(device_.info(Stream::Capture));
        if (read_copy_ == nullptr) {
            close();
            return -1;
        }
    }

    const StreamInfo& timing = device_.has_playback() ? device_.info(Stream::Playback) : device_.info(Stream::Capture);
    frames_per_cycle_ = request.frames_per_cycle;
    periods_ = timing.periods;
    rate_ = request.rate;
    dither_.assign(device_.playback_channels(), dither_state_t { });

    Config config;
    config.frames_per_cycle = frames_per_cycle_;
    config.buffer_frames = frames_per_cycle_ * periods_;
    config.period_usecs = static_cast<std::uint64_t>(frames_per_cycle_) * 1000000ull / rate_;
    config.capture_and_playback_not_synced = device_.capture_and_playback_not_synced();

    cycle_ = std::make_unique<PcmCycle>(device_, config);
    cycle_->set_callbacks(callbacks_);
    return 0;
}

void Session::close()
{
    cycle_.reset();
    device_.close();
    dither_.clear();
    read_copy_ = nullptr;
    write_copy_ = nullptr;
    frames_per_cycle_ = 0;
    periods_ = 0;
    rate_ = 0;
}

void Session::set_callbacks(const PcmCycle::Callbacks& callbacks)
{
    callbacks_ = callbacks;
    if (cycle_) {
        cycle_->set_callbacks(callbacks_);
    }
}

void Session::read_channel(const ChannelArea& area, jack_default_audio_sample_t* buf, std::size_t frames)
{
    read_copy_(buf, reinterpret_cast<char*>(area.data), frames, area.stride);
}

void Session::write_channel(std::size_t channel, const ChannelArea& area, jack_default_audio_sample_t* buf,
    std::size_t frames)
{
    write_copy_(reinterpret_cast<char*>(area.data), buf, frames, area.stride, &dither_[channel]);
    cycle_->mark_channel_done(channel);
}

} // namespace jackmini::alsa
