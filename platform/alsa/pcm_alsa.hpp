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

#ifndef JACKMINI_ALSA_PCM_ALSA_HPP
#define JACKMINI_ALSA_PCM_ALSA_HPP

#include "pcm_cycle.hpp"

#include <alsa/asoundlib.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace jackmini::alsa
{

enum class SampleFormat {
    Any,
    Float,
    S32,
    S16,
};

struct Request {
    std::uint32_t rate = 48000;
    std::uint32_t frames_per_cycle = 1024;
    std::uint32_t periods = 2;
    std::uint32_t channels = 0;      // 0 takes the device maximum
    SampleFormat format = SampleFormat::Any;
    std::uint32_t sample_bytes = 4;  // 4 tries the 32-bit formats first, 2 the 16-bit ones first
    bool soft_mode = false;          // the stop threshold is never reached: the stream runs until dropped
};

// What the device gave back for one stream.
struct StreamInfo {
    std::uint32_t rate = 0;
    std::uint32_t channels = 0;
    std::uint32_t periods = 0;
    std::uint32_t sample_bytes = 0;
    snd_pcm_format_t format = SND_PCM_FORMAT_UNKNOWN;
    bool interleaved = false;
    bool byte_swapped = false;
};

// The libasound side of the cycle core: one capture and one playback stream, both memory-mapped.  It is the only
// code that calls snd_pcm_*.  open() configures the streams and sizes every buffer the cycle uses, so the cycle
// itself does not allocate.
class Device
{
public:
    Device() = default;
    ~Device();
    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;

    // A null name leaves that direction out.  Returns 0 on success, -1 on error.  The request is updated with the
    // rate that was set.
    int open(const char* capture_name, const char* playback_name, Request& request);
    void close();

    const StreamInfo& info(Stream stream) const { return state(stream).info; }

    // True when the two streams could not be linked, so each one is started and dropped on its own.
    bool capture_and_playback_not_synced() const { return not_synced_; }

    // The PcmBackend concept.
    bool has_capture() const { return capture_.handle != nullptr; }
    bool has_playback() const { return playback_.handle != nullptr; }
    std::size_t capture_channels() const { return capture_.areas.size(); }
    std::size_t playback_channels() const { return playback_.areas.size(); }

    int poll_descriptor_count(Stream stream) const;
    int poll_descriptors(Stream stream, struct pollfd* fds, nfds_t nfds);
    int poll_revents(Stream stream, struct pollfd* fds, nfds_t nfds, unsigned short* revents);
    int poll(struct pollfd* fds, nfds_t nfds, int timeout_ms);
    std::uint64_t now_us() const;

    std::ptrdiff_t avail(Stream stream);
    int begin(Stream stream, MappedFrames& mapped);
    int commit(Stream stream, std::size_t offset, std::size_t frames);
    void silence(Stream stream, std::size_t channel, std::size_t frames);

    int prepare(Stream stream);
    int start(Stream stream);
    int drop(Stream stream);
    int status(XrunStatus& out);

private:
    struct StreamState {
        snd_pcm_t* handle = nullptr;
        StreamInfo info;
        std::vector<ChannelArea> areas;  // the areas of the current mapping, set by begin()
    };

    StreamState& state(Stream stream) { return stream == Stream::Capture ? capture_ : playback_; }
    const StreamState& state(Stream stream) const { return stream == Stream::Capture ? capture_ : playback_; }

    StreamState capture_;
    StreamState playback_;
    bool not_synced_ = false;
};

} // namespace jackmini::alsa

#endif
