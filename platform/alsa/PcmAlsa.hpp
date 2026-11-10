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

#pragma once

#include "PcmCycle.hpp"

#include <alsa/asoundlib.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace jackmini::alsa {

enum class SampleFormat {
    kAny,
    kFloat,
    kS32,
    kS16,
};

struct Request {
    std::uint32_t m_rate = 48000;
    std::uint32_t m_framesPerCycle = 1024;
    std::uint32_t m_periods = 2;
    std::uint32_t m_captureChannels = 0; // 0 takes the device maximum
    std::uint32_t m_playbackChannels = 0;
    SampleFormat m_format = SampleFormat::kAny;
    std::uint32_t m_sampleBytes = 4; // 4 tries the 32-bit formats first, 2 the 16-bit ones first
    bool m_softMode = false; // the stop threshold is never reached: the stream runs until dropped
};

// What the device gave back for one stream.
struct StreamInfo {
    std::uint32_t m_rate = 0;
    std::uint32_t m_channels = 0;
    std::uint32_t m_periods = 0;
    std::uint32_t m_sampleBytes = 0;
    std::uint32_t m_sbits = 0; // significant bits of a sample: 24 for a 32-bit format on a 24-bit device
    snd_pcm_format_t m_format = SND_PCM_FORMAT_UNKNOWN;
    bool m_interleaved = false;
    bool m_byteSwapped = false;
};

// The libasound side of the cycle core: one capture and one playback stream, both memory-mapped.  It is the only
// code that calls snd_pcm_*.  open() configures the streams and sizes every buffer the cycle uses, so the cycle
// itself does not allocate.
class Device {
public:
    Device() = default;
    ~Device();
    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;

    // A null name leaves that direction out.  A direction that cannot be opened is left out when the other one
    // works; EBUSY and EPERM are errors.  Returns 0 on success, -1 on error.  The request is updated with the rate
    // that was set.
    std::int32_t open(const char* captureName, const char* playbackName, Request& request);
    void close();

    const StreamInfo& info(Stream stream) const { return state(stream).m_info; }

    // True when the two streams could not be linked, so each one is started and dropped on its own.
    bool captureAndPlaybackNotSynced() const { return m_notSynced; }

    // The PcmBackend concept.
    bool hasCapture() const { return m_capture.m_handle != nullptr; }
    bool hasPlayback() const { return m_playback.m_handle != nullptr; }
    std::size_t captureChannels() const { return m_capture.m_areas.size(); }
    std::size_t playbackChannels() const { return m_playback.m_areas.size(); }

    std::int32_t pollDescriptorCount(Stream stream) const;
    std::int32_t pollDescriptors(Stream stream, struct pollfd* fds, nfds_t nfds);
    std::int32_t pollRevents(Stream stream, struct pollfd* fds, nfds_t nfds, std::uint16_t* revents);
    std::int32_t poll(struct pollfd* fds, nfds_t nfds, std::int32_t timeoutMs);
    std::uint64_t nowUs() const;

    std::ptrdiff_t avail(Stream stream);
    std::int32_t begin(Stream stream, MappedFrames& mapped);
    std::int32_t commit(Stream stream, std::size_t offset, std::size_t frames);
    void silence(Stream stream, std::size_t channel, std::size_t frames);

    std::int32_t prepare(Stream stream);
    std::int32_t start(Stream stream);
    std::int32_t drop(Stream stream);
    std::int32_t status(XrunStatus& out);

private:
    struct StreamState {
        snd_pcm_t* m_handle = nullptr;
        StreamInfo m_info;
        std::vector<ChannelArea> m_areas; // the areas of the current mapping, set by begin()
    };

    StreamState& state(Stream stream) { return stream == Stream::kCapture ? m_capture : m_playback; }
    const StreamState& state(Stream stream) const { return stream == Stream::kCapture ? m_capture : m_playback; }

    StreamState m_capture;
    StreamState m_playback;
    bool m_notSynced = false;
};

} // namespace jackmini::alsa
