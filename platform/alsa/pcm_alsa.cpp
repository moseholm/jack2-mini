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

#include "pcm_alsa.hpp"

#include "JackError.h"
#include "JackTime.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <poll.h>
#include <sys/time.h>
#include <algorithm>
#include <cerrno>
#include <cstring>

namespace jackmini::alsa {

namespace {

struct FormatEntry {
    snd_pcm_format_t m_format;
    bool m_littleEndian;
};

constexpr bool kHostLittleEndian = __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__;

// Ordered the way the old driver tried them: the 32-bit formats first (ascending), the 16-bit ones last.
constexpr std::array<FormatEntry, 9> kFormats = {{
    { SND_PCM_FORMAT_FLOAT_LE, true },
    { SND_PCM_FORMAT_S32_LE, true },
    { SND_PCM_FORMAT_S32_BE, false },
    { SND_PCM_FORMAT_S24_3LE, true },
    { SND_PCM_FORMAT_S24_3BE, false },
    { SND_PCM_FORMAT_S24_LE, true },
    { SND_PCM_FORMAT_S24_BE, false },
    { SND_PCM_FORMAT_S16_LE, true },
    { SND_PCM_FORMAT_S16_BE, false },
}};
constexpr std::size_t kFormatCount = sizeof(kFormats) / sizeof(kFormats[0]);

// The format index for an explicit request.
std::size_t requestedFormatIndex(SampleFormat format)
{
    switch (format) {
    case SampleFormat::kFloat:
        return 0;
    case SampleFormat::kS32:
        return 1;
    default:
        return 7;
    }
}

// The ALSA default plug device reports up to 16 million channels; the old driver limits an unset count to 2.
constexpr std::uint32_t kMaxDefaultChannels = 1024;

std::int32_t configureStream(const char* name, snd_pcm_t* handle, snd_pcm_stream_t direction, const Request& request,
    std::uint32_t wantedChannels, StreamInfo& info, std::uint32_t userPeriods, bool playback)
{
    snd_pcm_hw_params_t* hw = nullptr;
    snd_pcm_sw_params_t* sw = nullptr;
    snd_pcm_hw_params_alloca(&hw);
    snd_pcm_sw_params_alloca(&sw);
    const char* streamName = direction == SND_PCM_STREAM_PLAYBACK ? "playback" : "capture";
    std::int32_t err = 0;

    if ((err = snd_pcm_hw_params_any(handle, hw)) < 0) {
        jack_error("ALSA: no %s configurations available (%s)", streamName, snd_strerror(err));
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
                    streamName);
                return -1;
            }
        }
    }
    info.m_interleaved = access != SND_PCM_ACCESS_MMAP_NONINTERLEAVED;

    std::size_t index = 0;
    if (request.m_format != SampleFormat::kAny) {
        index = requestedFormatIndex(request.m_format);
        if (snd_pcm_hw_params_set_format(handle, hw, kFormats[index].m_format) < 0) {
            jack_error("ALSA: %s does not support the requested sample format", name);
            return -1;
        }
    } else {
        const bool ascending = request.m_sampleBytes == 4;
        index = ascending ? 0 : kFormatCount - 1;
        while (snd_pcm_hw_params_set_format(handle, hw, kFormats[index].m_format) < 0) {
            if (ascending ? index + 1 >= kFormatCount : index == 0) {
                jack_error("Sorry. The audio interface \"%s\" doesn't support any of the hardware sample formats"
                           " that JACK's alsa-driver can use.",
                    name);
                return -1;
            }
            ascending ? ++index : --index;
        }
    }
    info.m_format = kFormats[index].m_format;
    info.m_byteSwapped = kFormats[index].m_littleEndian != kHostLittleEndian;
    info.m_sampleBytes = snd_pcm_format_physical_width(info.m_format) / 8;

    std::uint32_t rate = request.m_rate;
    if (snd_pcm_hw_params_set_rate_near(handle, hw, &rate, nullptr) < 0) {
        jack_error("ALSA: cannot set sample/frame rate to %u for %s", request.m_rate, streamName);
        return -1;
    }
    info.m_rate = rate;

    std::uint32_t channels = wantedChannels;
    if (channels == 0) {
        if (snd_pcm_hw_params_get_channels_max(hw, &channels) < 0) {
            jack_error("ALSA: cannot read the channel count of the %s stream", streamName);
            return -1;
        }
        if (channels > kMaxDefaultChannels) {
            jack_error("ALSA: the \"default\" device is the plug layer, which is less efficient than a hardware"
                       " device such as hw:0; using 2 channels");
            channels = 2;
        }
    }
    if (snd_pcm_hw_params_set_channels(handle, hw, channels) < 0) {
        jack_error("ALSA: cannot set channel count to %u for %s", channels, streamName);
        return -1;
    }
    info.m_channels = channels;

    if (snd_pcm_hw_params_set_period_size(handle, hw, request.m_framesPerCycle, 0) < 0) {
        jack_error("ALSA: cannot set period size to %u frames for %s", request.m_framesPerCycle, streamName);
        return -1;
    }

    std::uint32_t periods = userPeriods;
    snd_pcm_hw_params_set_periods_min(handle, hw, &periods, nullptr);
    if (periods < userPeriods) {
        periods = userPeriods;
    }
    if (snd_pcm_hw_params_set_periods_near(handle, hw, &periods, nullptr) < 0) {
        jack_error("ALSA: cannot set number of periods to %u for %s", periods, streamName);
        return -1;
    }
    if (periods < userPeriods) {
        jack_error("ALSA: got smaller periods %u than %u for %s", periods, userPeriods, streamName);
        return -1;
    }
    info.m_periods = periods;

    if (snd_pcm_hw_params_set_buffer_size(handle, hw, periods * request.m_framesPerCycle) < 0) {
        jack_error("ALSA: cannot set buffer length to %u for %s", periods * request.m_framesPerCycle, streamName);
        return -1;
    }
    if (snd_pcm_hw_params(handle, hw) < 0) {
        jack_error("ALSA: cannot set hardware parameters for %s", streamName);
        return -1;
    }
    info.m_sbits = snd_pcm_hw_params_get_sbits(hw);

    snd_pcm_sw_params_current(handle, sw);
    if (snd_pcm_sw_params_set_start_threshold(handle, sw, 0U) < 0) {
        jack_error("ALSA: cannot set start mode for %s", streamName);
        return -1;
    }

    snd_pcm_uframes_t stopThreshold = static_cast<snd_pcm_uframes_t>(periods) * request.m_framesPerCycle;
    if (request.m_softMode) {
        stopThreshold = static_cast<snd_pcm_uframes_t>(-1);
    }
    if (snd_pcm_sw_params_set_stop_threshold(handle, sw, stopThreshold) < 0) {
        jack_error("ALSA: cannot set stop mode for %s", streamName);
        return -1;
    }
    if (snd_pcm_sw_params_set_silence_threshold(handle, sw, 0) < 0) {
        jack_error("ALSA: cannot set silence threshold for %s", streamName);
        return -1;
    }

    // The playback wakes up when a whole period of the buffer is free, and the extra periods the user asked
    // for are kept as slack.  The capture wakes up for each period.
    const snd_pcm_uframes_t availMin = playback
        ? static_cast<snd_pcm_uframes_t>(request.m_framesPerCycle) * (periods - userPeriods + 1)
        : request.m_framesPerCycle;
    if (snd_pcm_sw_params_set_avail_min(handle, sw, availMin) < 0) {
        jack_error("ALSA: cannot set avail min for %s", streamName);
        return -1;
    }

    if (snd_pcm_sw_params_set_tstamp_mode(handle, sw, SND_PCM_TSTAMP_ENABLE) < 0) {
        jack_info("Could not enable ALSA time stamp mode for %s", streamName);
    }
    if (snd_pcm_sw_params_set_tstamp_type(handle, sw, SND_PCM_TSTAMP_TYPE_MONOTONIC) < 0) {
        jack_info("Could not use monotonic ALSA time stamps for %s", streamName);
    }

    if (snd_pcm_sw_params(handle, sw) < 0) {
        jack_error("ALSA: cannot set software parameters for %s", streamName);
        return -1;
    }

    jack_info("ALSA: final selected sample format for %s: %s, %u periods", streamName,
        snd_pcm_format_name(info.m_format), periods);
    return 0;
}

enum class OpenResult {
    kOpened,
    kUnavailable, // the device could not be opened; the other direction may still work
    kFailed,
};

OpenResult openStream(const char* name, snd_pcm_stream_t direction, const Request& request,
    std::uint32_t wantedChannels, std::uint32_t userPeriods, snd_pcm_t*& handle,
    StreamInfo& info)
{
    const std::int32_t err = snd_pcm_open(&handle, name, direction, SND_PCM_NONBLOCK);
    if (err < 0) {
        handle = nullptr;
        if (err == -EBUSY) {
            jack_error("ALSA: the device \"%s\" is already in use; stop the application using it and start again",
                name);
            return OpenResult::kFailed;
        }
        if (err == -EPERM) {
            jack_error("ALSA: you do not have permission to open the audio device \"%s\"", name);
            return OpenResult::kFailed;
        }
        jack_error("ALSA: cannot open PCM device %s (%s)", name, snd_strerror(err));
        return OpenResult::kUnavailable;
    }

    // The streams are used in blocking mode, as the old driver set them.
    snd_pcm_nonblock(handle, 0);
    const bool playback = direction == SND_PCM_STREAM_PLAYBACK;
    if (configureStream(name, handle, direction, request, wantedChannels, info, userPeriods, playback) < 0) {
        snd_pcm_close(handle);
        handle = nullptr;
        return OpenResult::kFailed;
    }
    return OpenResult::kOpened;
}

} // namespace

Device::~Device()
{
    close();
}

std::int32_t Device::open(const char* captureName, const char* playbackName, Request& request)
{
    close();
    const std::uint32_t userPeriods = request.m_periods;

    OpenResult capture = OpenResult::kUnavailable;
    if (captureName != nullptr) {
        capture = openStream(captureName, SND_PCM_STREAM_CAPTURE, request, request.m_captureChannels, userPeriods,
            m_capture.m_handle, m_capture.m_info);
    }
    OpenResult playback = OpenResult::kUnavailable;
    if (playbackName != nullptr) {
        playback = openStream(playbackName, SND_PCM_STREAM_PLAYBACK, request, request.m_playbackChannels,
            userPeriods, m_playback.m_handle, m_playback.m_info);
    }
    if (capture == OpenResult::kFailed || playback == OpenResult::kFailed) {
        close();
        return -1;
    }
    if (captureName != nullptr && capture == OpenResult::kUnavailable && m_playback.m_handle != nullptr) {
        jack_info("ALSA: cannot open PCM device %s for capture; falling back to playback only", captureName);
    }
    if (playbackName != nullptr && playback == OpenResult::kUnavailable && m_capture.m_handle != nullptr) {
        jack_info("ALSA: cannot open PCM device %s for playback; falling back to capture only", playbackName);
    }
    if (m_capture.m_handle == nullptr && m_playback.m_handle == nullptr) {
        jack_error("ALSA: no PCM device could be opened");
        return -1;
    }

    m_notSynced = false;
    if (m_capture.m_handle && m_playback.m_handle) {
        m_notSynced = snd_pcm_link(m_playback.m_handle, m_capture.m_handle) != 0;
    }

    if (m_capture.m_handle && m_playback.m_handle && m_capture.m_info.m_rate != m_playback.m_info.m_rate) {
        jack_error("playback and capture sample rates do not match (%u vs. %u)", m_playback.m_info.m_rate,
            m_capture.m_info.m_rate);
    }

    // Keep the requested rate when either stream has it; otherwise take the capture rate.  Some hardware runs
    // full duplex at slightly different rates on the two sides, so a mismatch alone is not an error.
    std::uint32_t rate = m_capture.m_handle ? m_capture.m_info.m_rate : m_playback.m_info.m_rate;
    if (m_playback.m_handle && m_playback.m_info.m_rate == request.m_rate) {
        rate = request.m_rate;
    }
    if (m_capture.m_handle && m_capture.m_info.m_rate == request.m_rate) {
        rate = request.m_rate;
    }
    if (rate != request.m_rate) {
        jack_error("sample rate in use (%u Hz) does not match requested rate (%u Hz)", rate, request.m_rate);
    }
    request.m_rate = rate;

    for (StreamState* stream : { &m_capture, &m_playback }) {
        if (stream->m_handle == nullptr) {
            continue;
        }
        snd_pcm_uframes_t period = 0;
        std::int32_t dir = 0;
        snd_pcm_hw_params_t* hw = nullptr;
        snd_pcm_hw_params_alloca(&hw);
        snd_pcm_hw_params_current(stream->m_handle, hw);
        snd_pcm_hw_params_get_period_size(hw, &period, &dir);
        if (period != request.m_framesPerCycle) {
            jack_error("alsa_pcm: requested an interrupt every %u frames but got %lu frames", request.m_framesPerCycle,
                static_cast<std::uint64_t>(period));
            close();
            return -1;
        }
        stream->m_areas.resize(stream->m_info.m_channels);
    }

    request.m_periods = std::max(m_capture.m_info.m_periods, m_playback.m_info.m_periods);
    return 0;
}

void Device::close()
{
    m_notSynced = false;
    for (StreamState* stream : { &m_capture, &m_playback }) {
        if (stream->m_handle != nullptr) {
            snd_pcm_close(stream->m_handle);
            stream->m_handle = nullptr;
        }
        stream->m_areas.clear();
    }
}

std::int32_t Device::pollDescriptorCount(Stream stream) const
{
    return snd_pcm_poll_descriptors_count(state(stream).m_handle);
}

std::int32_t Device::pollDescriptors(Stream stream, struct pollfd* fds, nfds_t nfds)
{
    return snd_pcm_poll_descriptors(state(stream).m_handle, fds, static_cast<std::uint32_t>(nfds));
}

std::int32_t Device::pollRevents(Stream stream, struct pollfd* fds, nfds_t nfds, std::uint16_t* revents)
{
    return snd_pcm_poll_descriptors_revents(state(stream).m_handle, fds, static_cast<std::uint32_t>(nfds), revents);
}

std::int32_t Device::poll(struct pollfd* fds, nfds_t nfds, std::int32_t timeoutMs)
{
    return ::poll(fds, nfds, timeoutMs);
}

std::uint64_t Device::nowUs() const
{
    return jackmini::getMicroseconds();
}

std::ptrdiff_t Device::avail(Stream stream)
{
    return snd_pcm_avail_update(state(stream).m_handle);
}

std::int32_t Device::begin(Stream stream, MappedFrames& mapped)
{
    StreamState& s = state(stream);
    const snd_pcm_channel_area_t* areas = nullptr;
    snd_pcm_uframes_t offset = 0;
    snd_pcm_uframes_t frames = mapped.m_requested;

    const std::int32_t err = snd_pcm_mmap_begin(s.m_handle, &areas, &offset, &frames);
    if (err < 0) {
        return err;
    }

    mapped.m_offset = offset;
    mapped.m_frames = frames;
    for (std::size_t channel = 0; channel < s.m_areas.size() && channel < mapped.m_channelCount; ++channel) {
        const snd_pcm_channel_area_t& area = areas[channel];
        ChannelArea& out = s.m_areas[channel];
        out.m_data = static_cast<std::byte*>(area.addr) + area.first / 8 + offset * area.step / 8;
        out.m_stride = area.step / 8;
        mapped.m_channels[channel] = out;
    }
    return 0;
}

std::int32_t Device::commit(Stream stream, std::size_t offset, std::size_t frames)
{
    const snd_pcm_sframes_t done = snd_pcm_mmap_commit(state(stream).m_handle, offset, frames);
    if (done < 0) {
        return static_cast<std::int32_t>(done);
    }
    return 0;
}

void Device::silence(Stream stream, std::size_t channel, std::size_t frames)
{
    StreamState& s = state(stream);
    const ChannelArea& area = s.m_areas[channel];
    const std::size_t sampleBytes = s.m_info.m_sampleBytes;

    // Every supported format is silent at all-zero bytes.
    if (area.m_stride == sampleBytes) {
        std::memset(area.m_data, 0, frames * sampleBytes);
        return;
    }
    for (std::size_t frame = 0; frame < frames; ++frame) {
        std::memset(area.m_data + frame * area.m_stride, 0, sampleBytes);
    }
}

std::int32_t Device::prepare(Stream stream)
{
    return snd_pcm_prepare(state(stream).m_handle);
}

std::int32_t Device::start(Stream stream)
{
    return snd_pcm_start(state(stream).m_handle);
}

std::int32_t Device::drop(Stream stream)
{
    return snd_pcm_drop(state(stream).m_handle);
}

std::int32_t Device::status(XrunStatus& out)
{
    out = XrunStatus { };
    const StreamState& s = m_capture.m_handle ? m_capture : m_playback;

    snd_pcm_status_t* status = nullptr;
    snd_pcm_status_alloca(&status);
    const std::int32_t err = snd_pcm_status(s.m_handle, status);
    if (err < 0) {
        return err;
    }

    const snd_pcm_state_t state = snd_pcm_status_get_state(status);
    out.m_suspended = state == SND_PCM_STATE_SUSPENDED;
    out.m_xrun = state == SND_PCM_STATE_XRUN;
    if (out.m_xrun) {
        struct timeval now { };
        struct timeval trigger { };
        struct timeval diff { };
        snd_pcm_status_get_tstamp(status, &now);
        snd_pcm_status_get_trigger_tstamp(status, &trigger);
        timersub(&now, &trigger, &diff);
        out.m_delayedUsecs = static_cast<float>(diff.tv_sec * 1000000.0 + diff.tv_usec);
    }
    return 0;
}

} // namespace jackmini::alsa
