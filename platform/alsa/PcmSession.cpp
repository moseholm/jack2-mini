#include <cstddef>
#include <cstdint>

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

#include "PcmSession.hpp"

#include "Error.h"
#include "Log.h"

namespace jackmini::alsa {

namespace {

// The copy from a JACK float buffer to a device playback area.  Null when the format has no conversion.
WriteCopyFunction selectWrite(const StreamInfo& info, DitherAlgorithm dither)
{
    const bool swapped = info.m_byteSwapped;

    if (info.m_format == SND_PCM_FORMAT_FLOAT_LE) {
        return sampleMoveDSFloatLE;
    }

    switch (info.m_sampleBytes) {
    case 2:
        switch (dither) {
        case kRectangular:
            jack_info("Rectangular dithering at 16 bits");
            return swapped ? sampleMoveDitherRectD16SSs : sampleMoveDitherRectD16SS;
        case kTriangular:
            jack_info("Triangular dithering at 16 bits");
            return swapped ? sampleMoveDitherTriD16SSs : sampleMoveDitherTriD16SS;
        case kShaped:
            jack_info("Noise-shaped dithering at 16 bits");
            return swapped ? sampleMoveDitherShapedD16SSs : sampleMoveDitherShapedD16SS;
        default:
            return swapped ? sampleMoveD16SSs : sampleMoveD16SS;
        }
    case 3:
        return swapped ? sampleMoveD24SSs : sampleMoveD24SS;
    case 4:
        switch (info.m_format) {
        case SND_PCM_FORMAT_S24_LE:
        case SND_PCM_FORMAT_S24_BE:
            return swapped ? sampleMoveD32l24SSs : sampleMoveD32l24SS;
        case SND_PCM_FORMAT_S32_LE:
        case SND_PCM_FORMAT_S32_BE:
            if (info.m_sbits == 32) {
                return swapped ? sampleMoveD32SSs : sampleMoveD32SS;
            }
            if (info.m_sbits == 24) {
                jack_log("sample format is SND_PCM_FORMAT_S32 but only 24 bits available");
                return swapped ? sampleMoveD32u24SSs : sampleMoveD32u24SS;
            }
            jack_error("unsupported sample format for playback: SND_PCM_FORMAT_S32 with %u bits", info.m_sbits);
            return nullptr;
        default:
            jack_error("unsupported 4 byte sample_format");
            return nullptr;
        }
    default:
        jack_error("impossible sample width (%u) discovered!", info.m_sampleBytes);
        return nullptr;
    }
}

// The copy from a device capture area to a JACK float buffer.  Null when the format has no conversion.
ReadCopyFunction selectRead(const StreamInfo& info)
{
    const bool swapped = info.m_byteSwapped;

    if (info.m_format == SND_PCM_FORMAT_FLOAT_LE) {
        return sampleMoveFloatLESSs;
    }

    switch (info.m_sampleBytes) {
    case 2:
        return swapped ? sampleMoveDSS16s : sampleMoveDSS16;
    case 3:
        return swapped ? sampleMoveDSS24s : sampleMoveDSS24;
    case 4:
        switch (info.m_format) {
        case SND_PCM_FORMAT_S24_LE:
        case SND_PCM_FORMAT_S24_BE:
            return swapped ? sampleMoveDSS32l24s : sampleMoveDSS32l24;
        case SND_PCM_FORMAT_S32_LE:
        case SND_PCM_FORMAT_S32_BE:
            if (info.m_sbits == 32) {
                return swapped ? sampleMoveDSS32s : sampleMoveDSS32;
            }
            if (info.m_sbits == 24) {
                jack_log("sample format is SND_PCM_FORMAT_S32 but only 24 bits available");
                return swapped ? sampleMoveDSS32u24s : sampleMoveDSS32u24;
            }
            jack_error("unsupported sample format for capture: SND_PCM_FORMAT_S32 with %u bits", info.m_sbits);
            return nullptr;
        default:
            jack_error("unsupported 4 byte sample_format");
            return nullptr;
        }
    default:
        jack_error("impossible sample width (%u) discovered!", info.m_sampleBytes);
        return nullptr;
    }
}

} // namespace

Session::~Session()
{
    close();
}

std::int32_t Session::open(const char* captureName, const char* playbackName, Request& request, DitherAlgorithm dither)
{
    close();
    if (m_device.open(captureName, playbackName, request) < 0) {
        return -1;
    }

    if (m_device.hasPlayback()) {
        m_writeCopy = selectWrite(m_device.info(Stream::kPlayback), dither);
        if (m_writeCopy == nullptr) {
            close();
            return -1;
        }
    }
    if (m_device.hasCapture()) {
        m_readCopy = selectRead(m_device.info(Stream::kCapture));
        if (m_readCopy == nullptr) {
            close();
            return -1;
        }
    }

    const StreamInfo& timing = m_device.hasPlayback() ? m_device.info(Stream::kPlayback) : m_device.info(Stream::kCapture);
    m_framesPerCycle = request.m_framesPerCycle;
    m_periods = timing.m_periods;
    m_rate = request.m_rate;
    m_dither.assign(m_device.playbackChannels(), DitherState { });

    Config config;
    config.m_framesPerCycle = m_framesPerCycle;
    config.m_bufferFrames = m_framesPerCycle * m_periods;
    config.m_periodUsecs = static_cast<std::uint64_t>(m_framesPerCycle) * 1000000ull / m_rate;
    config.m_captureAndPlaybackNotSynced = m_device.captureAndPlaybackNotSynced();

    m_cycle = std::make_unique<PcmCycle>(m_device, config);
    m_cycle->setCallbacks(m_callbacks);
    return 0;
}

void Session::close()
{
    m_cycle.reset();
    m_device.close();
    m_dither.clear();
    m_readCopy = nullptr;
    m_writeCopy = nullptr;
    m_framesPerCycle = 0;
    m_periods = 0;
    m_rate = 0;
}

void Session::setCallbacks(const PcmCycle::Callbacks& callbacks)
{
    m_callbacks = callbacks;
    if (m_cycle) {
        m_cycle->setCallbacks(m_callbacks);
    }
}

void Session::readChannel(const ChannelArea& area, DefaultAudioSampleC* buf, std::size_t frames)
{
    m_readCopy(buf, reinterpret_cast<std::byte*>(area.m_data), frames, area.m_stride);
}

void Session::writeChannel(std::size_t channel, const ChannelArea& area, DefaultAudioSampleC* buf,
    std::size_t frames)
{
    m_writeCopy(reinterpret_cast<std::byte*>(area.m_data), buf, frames, area.m_stride, &m_dither[channel]);
    m_cycle->markChannelDone(channel);
}

} // namespace jackmini::alsa
