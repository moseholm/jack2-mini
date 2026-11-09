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

#include "pcm_alsa.hpp"

#include "JackError.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <poll.h>
#include <sys/time.h>

extern "C" std::uint64_t jack_get_microseconds();

namespace jackmini::alsa
{

namespace
{

struct FormatEntry {
    snd_pcm_format_t format;
    bool little_endian;
};

constexpr bool kHostLittleEndian = __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__;

// Ordered the way the old driver tried them: the 32-bit formats first (ascending), the 16-bit ones last.
constexpr FormatEntry kFormats[] = {
    {SND_PCM_FORMAT_FLOAT_LE, true},
    {SND_PCM_FORMAT_S32_LE, true},
    {SND_PCM_FORMAT_S32_BE, false},
    {SND_PCM_FORMAT_S24_3LE, true},
    {SND_PCM_FORMAT_S24_3BE, false},
    {SND_PCM_FORMAT_S24_LE, true},
    {SND_PCM_FORMAT_S24_BE, false},
    {SND_PCM_FORMAT_S16_LE, true},
    {SND_PCM_FORMAT_S16_BE, false},
};
constexpr std::size_t kFormatCount = sizeof(kFormats) / sizeof(kFormats[0]);

// The format index for an explicit request.
std::size_t requested_format_index(SampleFormat format)
{
    switch (format) {
    case SampleFormat::Float: return 0;
    case SampleFormat::S32: return 1;
    default: return 7;
    }
}

// The ALSA default plug device reports up to 16 million channels; the old driver limits an unset count to 2.
constexpr unsigned kMaxDefaultChannels = 1024;

int configure_stream(const char* name, snd_pcm_t* handle, snd_pcm_stream_t direction, Request& request,
                     StreamInfo& info, std::uint32_t user_periods, bool playback)
{
    snd_pcm_hw_params_t* hw = nullptr;
    snd_pcm_sw_params_t* sw = nullptr;
    snd_pcm_hw_params_alloca(&hw);
    snd_pcm_sw_params_alloca(&sw);
    const char* stream_name = direction == SND_PCM_STREAM_PLAYBACK ? "playback" : "capture";
    int err = 0;

    if ((err = snd_pcm_hw_params_any(handle, hw)) < 0) {
        jack_error("ALSA: no %s configurations available (%s)", stream_name, snd_strerror(err));
        return -1;
    }
    if (snd_pcm_hw_params_set_periods_integer(handle, hw) < 0) {
        jack_error("ALSA: cannot restrict period size to integral value.");
        return -1;
    }

    snd_pcm_access_t access = SND_PCM_ACCESS_MMAP_NONINTERLEAVED;
    if (snd_pcm_hw_params_set_access(handle, hw, access) < 0) {
        access = SND_PCM_ACCESS_MMAP_INTERLEAVED;
        if (snd_pcm_hw_params_set_access(handle, hw, access) < 0) {
            access = SND_PCM_ACCESS_MMAP_COMPLEX;
            if (snd_pcm_hw_params_set_access(handle, hw, access) < 0) {
                jack_error("ALSA: mmap-based access is not possible for the %s stream of this audio interface",
                           stream_name);
                return -1;
            }
        }
    }
    info.interleaved = access != SND_PCM_ACCESS_MMAP_NONINTERLEAVED;

    std::size_t index = 0;
    if (request.format != SampleFormat::Any) {
        index = requested_format_index(request.format);
        if (snd_pcm_hw_params_set_format(handle, hw, kFormats[index].format) < 0) {
            jack_error("ALSA: %s does not support the requested sample format", name);
            return -1;
        }
    } else {
        const bool ascending = request.sample_bytes == 4;
        index = ascending ? 0 : kFormatCount - 1;
        while (snd_pcm_hw_params_set_format(handle, hw, kFormats[index].format) < 0) {
            if (ascending ? index + 1 >= kFormatCount : index == 0) {
                jack_error("Sorry. The audio interface \"%s\" doesn't support any of the hardware sample formats"
                           " that JACK's alsa-driver can use.", name);
                return -1;
            }
            ascending ? ++index : --index;
        }
    }
    info.format = kFormats[index].format;
    info.byte_swapped = kFormats[index].little_endian != kHostLittleEndian;
    info.sample_bytes = snd_pcm_format_physical_width(info.format) / 8;

    unsigned int rate = request.rate;
    if (snd_pcm_hw_params_set_rate_near(handle, hw, &rate, nullptr) < 0) {
        jack_error("ALSA: cannot set sample/frame rate to %u for %s", request.rate, stream_name);
        return -1;
    }
    info.rate = rate;

    unsigned int channels = request.channels;
    if (channels == 0) {
        if (snd_pcm_hw_params_get_channels_max(hw, &channels) < 0) {
            jack_error("ALSA: cannot read the channel count of the %s stream", stream_name);
            return -1;
        }
        if (channels > kMaxDefaultChannels) {
            jack_error("ALSA: the \"default\" device is the plug layer, which is less efficient than a hardware"
                       " device such as hw:0; using 2 channels");
            channels = 2;
        }
    }
    if (snd_pcm_hw_params_set_channels(handle, hw, channels) < 0) {
        jack_error("ALSA: cannot set channel count to %u for %s", channels, stream_name);
        return -1;
    }
    info.channels = channels;

    if (snd_pcm_hw_params_set_period_size(handle, hw, request.frames_per_cycle, 0) < 0) {
        jack_error("ALSA: cannot set period size to %u frames for %s", request.frames_per_cycle, stream_name);
        return -1;
    }

    unsigned int periods = user_periods;
    snd_pcm_hw_params_set_periods_min(handle, hw, &periods, nullptr);
    if (periods < user_periods) {
        periods = user_periods;
    }
    if (snd_pcm_hw_params_set_periods_near(handle, hw, &periods, nullptr) < 0) {
        jack_error("ALSA: cannot set number of periods to %u for %s", periods, stream_name);
        return -1;
    }
    if (periods < user_periods) {
        jack_error("ALSA: got smaller periods %u than %u for %s", periods, user_periods, stream_name);
        return -1;
    }
    info.periods = periods;

    if (snd_pcm_hw_params_set_buffer_size(handle, hw, periods * request.frames_per_cycle) < 0) {
        jack_error("ALSA: cannot set buffer length to %u for %s", periods * request.frames_per_cycle, stream_name);
        return -1;
    }
    if (snd_pcm_hw_params(handle, hw) < 0) {
        jack_error("ALSA: cannot set hardware parameters for %s", stream_name);
        return -1;
    }
    info.sbits = snd_pcm_hw_params_get_sbits(hw);

    snd_pcm_sw_params_current(handle, sw);
    if (snd_pcm_sw_params_set_start_threshold(handle, sw, 0U) < 0) {
        jack_error("ALSA: cannot set start mode for %s", stream_name);
        return -1;
    }

    snd_pcm_uframes_t stop_threshold = static_cast<snd_pcm_uframes_t>(periods) * request.frames_per_cycle;
    if (request.soft_mode) {
        stop_threshold = static_cast<snd_pcm_uframes_t>(-1);
    }
    if (snd_pcm_sw_params_set_stop_threshold(handle, sw, stop_threshold) < 0) {
        jack_error("ALSA: cannot set stop mode for %s", stream_name);
        return -1;
    }
    if (snd_pcm_sw_params_set_silence_threshold(handle, sw, 0) < 0) {
        jack_error("ALSA: cannot set silence threshold for %s", stream_name);
        return -1;
    }

    // The playback wakes up when a whole period of the buffer is free, and the extra periods the user asked
    // for are kept as slack.  The capture wakes up for each period.
    const snd_pcm_uframes_t avail_min = playback
        ? static_cast<snd_pcm_uframes_t>(request.frames_per_cycle) * (periods - user_periods + 1)
        : request.frames_per_cycle;
    if (snd_pcm_sw_params_set_avail_min(handle, sw, avail_min) < 0) {
        jack_error("ALSA: cannot set avail min for %s", stream_name);
        return -1;
    }

    if (snd_pcm_sw_params_set_tstamp_mode(handle, sw, SND_PCM_TSTAMP_ENABLE) < 0) {
        jack_info("Could not enable ALSA time stamp mode for %s", stream_name);
    }
    if (snd_pcm_sw_params_set_tstamp_type(handle, sw, SND_PCM_TSTAMP_TYPE_MONOTONIC) < 0) {
        jack_info("Could not use monotonic ALSA time stamps for %s", stream_name);
    }

    if (snd_pcm_sw_params(handle, sw) < 0) {
        jack_error("ALSA: cannot set software parameters for %s", stream_name);
        return -1;
    }

    jack_info("ALSA: final selected sample format for %s: %s, %u periods", stream_name,
              snd_pcm_format_name(info.format), periods);
    return 0;
}

int open_stream(const char* name, snd_pcm_stream_t direction, Request& request, std::uint32_t user_periods,
                snd_pcm_t*& handle, StreamInfo& info)
{
    if (snd_pcm_open(&handle, name, direction, SND_PCM_NONBLOCK) < 0) {
        jack_error("ALSA: cannot open \"%s\"", name);
        handle = nullptr;
        return -1;
    }
    const bool playback = direction == SND_PCM_STREAM_PLAYBACK;
    if (configure_stream(name, handle, direction, request, info, user_periods, playback) < 0) {
        snd_pcm_close(handle);
        handle = nullptr;
        return -1;
    }
    return 0;
}

} // namespace

Device::~Device()
{
    close();
}

int Device::open(const char* capture_name, const char* playback_name, Request& request)
{
    close();
    const std::uint32_t user_periods = request.periods;
    Request capture_request = request;
    Request playback_request = request;

    if (capture_name != nullptr &&
        open_stream(capture_name, SND_PCM_STREAM_CAPTURE, capture_request, user_periods, capture_.handle,
                    capture_.info) < 0) {
        return -1;
    }
    if (playback_name != nullptr &&
        open_stream(playback_name, SND_PCM_STREAM_PLAYBACK, playback_request, user_periods, playback_.handle,
                    playback_.info) < 0) {
        close();
        return -1;
    }

    not_synced_ = false;
    if (capture_.handle && playback_.handle) {
        not_synced_ = snd_pcm_link(playback_.handle, capture_.handle) != 0;
    }

    if (capture_.handle && playback_.handle && capture_.info.rate != playback_.info.rate) {
        jack_error("playback and capture sample rates do not match (%u vs. %u)", playback_.info.rate,
                   capture_.info.rate);
    }

    // Keep the requested rate when either stream has it; otherwise take the capture rate.  Some hardware runs
    // full duplex at slightly different rates on the two sides, so a mismatch alone is not an error.
    std::uint32_t rate = capture_.handle ? capture_.info.rate : playback_.info.rate;
    if (playback_.handle && playback_.info.rate == request.rate) {
        rate = request.rate;
    }
    if (capture_.handle && capture_.info.rate == request.rate) {
        rate = request.rate;
    }
    if (rate != request.rate) {
        jack_error("sample rate in use (%u Hz) does not match requested rate (%u Hz)", rate, request.rate);
    }
    request.rate = rate;

    for (StreamState* stream : {&capture_, &playback_}) {
        if (stream->handle == nullptr) {
            continue;
        }
        const std::uint32_t frames = request.frames_per_cycle;
        snd_pcm_uframes_t period = 0;
        int dir = 0;
        snd_pcm_hw_params_t* hw = nullptr;
        snd_pcm_hw_params_alloca(&hw);
        snd_pcm_hw_params_current(stream->handle, hw);
        snd_pcm_hw_params_get_period_size(hw, &period, &dir);
        if (period != frames) {
            jack_error("alsa_pcm: requested an interrupt every %u frames but got %lu frames", frames,
                       static_cast<unsigned long>(period));
            close();
            return -1;
        }
        stream->areas.resize(stream->info.channels);
    }

    request.periods = std::max(capture_.info.periods, playback_.info.periods);
    return 0;
}

void Device::close()
{
    not_synced_ = false;
    for (StreamState* stream : {&capture_, &playback_}) {
        if (stream->handle != nullptr) {
            snd_pcm_close(stream->handle);
            stream->handle = nullptr;
        }
        stream->areas.clear();
    }
}

int Device::poll_descriptor_count(Stream stream) const
{
    return snd_pcm_poll_descriptors_count(state(stream).handle);
}

int Device::poll_descriptors(Stream stream, struct pollfd* fds, nfds_t nfds)
{
    return snd_pcm_poll_descriptors(state(stream).handle, fds, static_cast<unsigned int>(nfds));
}

int Device::poll_revents(Stream stream, struct pollfd* fds, nfds_t nfds, unsigned short* revents)
{
    return snd_pcm_poll_descriptors_revents(state(stream).handle, fds, static_cast<unsigned int>(nfds), revents);
}

int Device::poll(struct pollfd* fds, nfds_t nfds, int timeout_ms)
{
    return ::poll(fds, nfds, timeout_ms);
}

std::uint64_t Device::now_us() const
{
    return jack_get_microseconds();
}

std::ptrdiff_t Device::avail(Stream stream)
{
    return snd_pcm_avail_update(state(stream).handle);
}

int Device::begin(Stream stream, MappedFrames& mapped)
{
    StreamState& s = state(stream);
    const snd_pcm_channel_area_t* areas = nullptr;
    snd_pcm_uframes_t offset = 0;
    snd_pcm_uframes_t frames = mapped.requested;

    const int err = snd_pcm_mmap_begin(s.handle, &areas, &offset, &frames);
    if (err < 0) {
        return err;
    }

    mapped.offset = offset;
    mapped.frames = frames;
    for (std::size_t channel = 0; channel < s.areas.size() && channel < mapped.channel_count; ++channel) {
        const snd_pcm_channel_area_t& area = areas[channel];
        ChannelArea& out = s.areas[channel];
        out.data = static_cast<std::byte*>(area.addr) + area.first / 8 + offset * area.step / 8;
        out.stride = area.step / 8;
        mapped.channels[channel] = out;
    }
    return 0;
}

int Device::commit(Stream stream, std::size_t offset, std::size_t frames)
{
    const snd_pcm_sframes_t done = snd_pcm_mmap_commit(state(stream).handle, offset, frames);
    if (done < 0) {
        return static_cast<int>(done);
    }
    return 0;
}

void Device::silence(Stream stream, std::size_t channel, std::size_t frames)
{
    StreamState& s = state(stream);
    const ChannelArea& area = s.areas[channel];
    const std::size_t sample_bytes = s.info.sample_bytes;

    // Every supported format is silent at all-zero bytes.
    if (area.stride == sample_bytes) {
        std::memset(area.data, 0, frames * sample_bytes);
        return;
    }
    for (std::size_t frame = 0; frame < frames; ++frame) {
        std::memset(area.data + frame * area.stride, 0, sample_bytes);
    }
}

int Device::prepare(Stream stream)
{
    return snd_pcm_prepare(state(stream).handle);
}

int Device::start(Stream stream)
{
    return snd_pcm_start(state(stream).handle);
}

int Device::drop(Stream stream)
{
    return snd_pcm_drop(state(stream).handle);
}

int Device::status(XrunStatus& out)
{
    out = XrunStatus{};
    const StreamState& s = capture_.handle ? capture_ : playback_;

    snd_pcm_status_t* status = nullptr;
    snd_pcm_status_alloca(&status);
    const int err = snd_pcm_status(s.handle, status);
    if (err < 0) {
        return err;
    }

    const snd_pcm_state_t state = snd_pcm_status_get_state(status);
    out.suspended = state == SND_PCM_STATE_SUSPENDED;
    out.xrun = state == SND_PCM_STATE_XRUN;
    if (out.xrun) {
        struct timeval now{};
        struct timeval trigger{};
        struct timeval diff{};
        snd_pcm_status_get_tstamp(status, &now);
        snd_pcm_status_get_trigger_tstamp(status, &trigger);
        timersub(&now, &trigger, &diff);
        out.delayed_usecs = static_cast<float>(diff.tv_sec * 1000000.0 + diff.tv_usec);
    }
    return 0;
}

} // namespace jackmini::alsa
