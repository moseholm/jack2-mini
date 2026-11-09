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

#ifndef JACKMINI_ALSA_PCM_SESSION_HPP
#define JACKMINI_ALSA_PCM_SESSION_HPP

#include "pcm_alsa.hpp"

#include "memops.h"

#include <memory>
#include <vector>

namespace jackmini::alsa
{

using PcmCycle = Cycle<Device>;

using ReadCopyFunction = void (*)(jack_default_audio_sample_t* dst, char* src, unsigned long nsamples,
                                  unsigned long src_skip);
using WriteCopyFunction = void (*)(char* dst, jack_default_audio_sample_t* src, unsigned long nsamples,
                                   unsigned long dst_skip, dither_state_t* state);

// One ALSA card as the JACK drivers use it: the device, the cycle core on the device, and the sample conversion
// between the device buffers and JACK's float buffers.  The conversion functions are chosen once, in open().
class Session
{
public:
    Session() = default;
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    // Opens and configures the streams, and builds the cycle.  Returns 0 on success, -1 on error.  The request is
    // updated with the rate and the period count that were set.
    int open(const char* capture_name, const char* playback_name, Request& request, DitherAlgorithm dither);
    void close();
    bool is_open() const { return cycle_ != nullptr; }

    void set_callbacks(const PcmCycle::Callbacks& callbacks);

    const StreamInfo& info(Stream stream) const { return device_.info(stream); }
    bool capture_and_playback_not_synced() const { return device_.capture_and_playback_not_synced(); }
    std::uint32_t frames_per_cycle() const { return frames_per_cycle_; }
    std::uint32_t periods() const { return periods_; }
    std::uint32_t rate() const { return rate_; }

    int start() { return cycle_->start(); }
    int stop() { return cycle_->stop(); }
    WaitResult wait() { return cycle_->wait(); }
    int read(std::uint32_t frames) { return cycle_->read(frames); }
    int write(std::uint32_t frames) { return cycle_->write(frames); }
    PcmCycle& cycle() { return *cycle_; }

    // Converts one channel between a mapped device area and a JACK buffer.  The playback channel is marked done,
    // so the cycle does not silence it.
    void read_channel(const ChannelArea& area, jack_default_audio_sample_t* buf, std::size_t frames);
    void write_channel(std::size_t channel, const ChannelArea& area, jack_default_audio_sample_t* buf,
                       std::size_t frames);

private:
    Device device_;
    std::unique_ptr<PcmCycle> cycle_;
    PcmCycle::Callbacks callbacks_;
    std::uint32_t frames_per_cycle_ = 0;
    std::uint32_t periods_ = 0;
    std::uint32_t rate_ = 0;

    ReadCopyFunction read_copy_ = nullptr;
    WriteCopyFunction write_copy_ = nullptr;
    std::vector<dither_state_t> dither_;
};

} // namespace jackmini::alsa

#endif
