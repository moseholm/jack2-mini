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

#include "pcm_alsa.hpp"

#include "memops.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace jackmini::alsa {

using PcmCycle = Cycle<Device>;

using ReadCopyFunction = void (*)(DefaultAudioSampleC* dst, std::byte* src, std::uint64_t nsamples,
    std::uint64_t srcSkip);
using WriteCopyFunction = void (*)(std::byte* dst, DefaultAudioSampleC* src, std::uint64_t nsamples,
    std::uint64_t dstSkip, DitherState* state);

// One ALSA card as the JACK drivers use it: the device, the cycle core on the device, and the sample conversion
// between the device buffers and JACK's float buffers.  The conversion functions are chosen once, in open().
class Session {
public:
    Session() = default;
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    // Opens and configures the streams, and builds the cycle.  Returns 0 on success, -1 on error.  The request is
    // updated with the rate and the period count that were set.
    std::int32_t open(const char* captureName, const char* playbackName, Request& request, DitherAlgorithm dither);
    void close();
    bool isOpen() const { return m_cycle != nullptr; }
    bool hasCapture() const { return m_device.hasCapture(); }
    bool hasPlayback() const { return m_device.hasPlayback(); }

    void setCallbacks(const PcmCycle::Callbacks& callbacks);

    const StreamInfo& info(Stream stream) const { return m_device.info(stream); }
    bool captureAndPlaybackNotSynced() const { return m_device.captureAndPlaybackNotSynced(); }
    std::uint32_t framesPerCycle() const { return m_framesPerCycle; }
    std::uint32_t periods() const { return m_periods; }
    std::uint32_t rate() const { return m_rate; }

    std::int32_t start() { return m_cycle->start(); }
    std::int32_t stop() { return m_cycle->stop(); }
    WaitResult wait() { return m_cycle->wait(); }
    std::int32_t read(std::uint32_t frames) { return m_cycle->read(frames); }
    std::int32_t write(std::uint32_t frames) { return m_cycle->write(frames); }
    PcmCycle& cycle() { return *m_cycle; }

    // Converts one channel between a mapped device area and a JACK buffer.  The playback channel is marked done,
    // so the cycle does not silence it.
    void readChannel(const ChannelArea& area, DefaultAudioSampleC* buf, std::size_t frames);
    void writeChannel(std::size_t channel, const ChannelArea& area, DefaultAudioSampleC* buf,
        std::size_t frames);

private:
    Device m_device;
    std::unique_ptr<PcmCycle> m_cycle;
    PcmCycle::Callbacks m_callbacks;
    std::uint32_t m_framesPerCycle = 0;
    std::uint32_t m_periods = 0;
    std::uint32_t m_rate = 0;

    ReadCopyFunction m_readCopy = nullptr;
    WriteCopyFunction m_writeCopy = nullptr;
    std::vector<DitherState> m_dither;
};

} // namespace jackmini::alsa
