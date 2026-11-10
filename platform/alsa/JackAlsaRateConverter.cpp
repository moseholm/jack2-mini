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

#include "JackAlsaRateConverter.h"
#include "JackError.h"

#include <cstdint>
#include <string.h>
#include <algorithm>

namespace jackmini {

static float** allocChannels(std::int32_t channels, std::uint32_t frames)
{
    float** buffers = new float*[channels];
    for (std::int32_t i = 0; i < channels; i++) {
        buffers[i] = new float[frames];
        memset(buffers[i], 0, sizeof(float) * frames);
    }
    return buffers;
}

static void freeChannels(float** buffers, std::int32_t channels)
{
    if (buffers == nullptr) {
        return;
    }
    for (std::int32_t i = 0; i < channels; i++) {
        delete[] buffers[i];
    }
    delete[] buffers;
}

AlsaRateConverter::AlsaRateConverter(Resampler* resampler, std::int32_t channels, std::uint32_t capacity)
    : m_resampler(resampler)
    , m_channels(channels)
    , m_capacity(capacity)
    , m_in(nullptr)
    , m_out(nullptr)
    , m_outPtr(nullptr)
    , m_inCount(0)
    , m_outCount(0)
{
    if (m_resampler == nullptr) {
        return;
    }
    m_in = allocChannels(channels, capacity);
    m_out = allocChannels(channels, capacity);
    m_outPtr = new float*[channels];
}

AlsaRateConverter::~AlsaRateConverter()
{
    delete m_resampler;
    freeChannels(m_in, m_channels);
    freeChannels(m_out, m_channels);
    delete[] m_outPtr;
}

void AlsaRateConverter::reset()
{
    m_inCount = 0;
    m_outCount = 0;
    if (m_resampler) {
        m_resampler->reset();
    }
}

void AlsaRateConverter::setRate(double rate)
{
    if (m_resampler) {
        m_resampler->setRate(rate);
    }
}

std::uint32_t AlsaRateConverter::delay()
{
    return m_resampler ? m_resampler->delay() : 0;
}

std::uint32_t AlsaRateConverter::outputBacklog()
{
    return m_resampler ? m_resampler->outputLength(m_inCount) : 0;
}

std::uint32_t AlsaRateConverter::needInput(std::uint32_t outFrames)
{
    std::uint32_t pending = std::min(m_outCount, outFrames);
    std::uint32_t want = m_resampler->inputLength(outFrames - pending);
    if (want <= m_inCount) {
        return 0;
    }
    want -= m_inCount;
    if (m_inCount + want > m_capacity) {
        want = m_capacity - m_inCount;
    }
    return want;
}

std::uint32_t AlsaRateConverter::convert(const float* const* in, std::uint32_t inFrames,
    float* const* out, std::uint32_t outFrames)
{
    if (outFrames > m_capacity) {
        outFrames = m_capacity;
    }

    if (inFrames > m_capacity - m_inCount) {
        jack_log("JackAlsaRateConverter: input overflow, dropped %u frames", inFrames - (m_capacity - m_inCount));
        inFrames = m_capacity - m_inCount;
    }
    for (std::int32_t c = 0; c < m_channels; c++) {
        memcpy(m_in[c] + m_inCount, in[c], sizeof(float) * inFrames);
    }
    m_inCount += inFrames;

    while (m_outCount < outFrames && m_inCount > 0) {
        std::uint32_t inLen = m_inCount;
        std::uint32_t outLen = outFrames - m_outCount;
        for (std::int32_t c = 0; c < m_channels; c++) {
            m_outPtr[c] = m_out[c] + m_outCount;
        }

        m_resampler->process(m_in, &inLen, m_outPtr, &outLen);
        if (inLen == 0 && outLen == 0) {
            break;
        }

        if (inLen > 0 && inLen < m_inCount) {
            for (std::int32_t c = 0; c < m_channels; c++) {
                memmove(m_in[c], m_in[c] + inLen, sizeof(float) * (m_inCount - inLen));
            }
        }
        m_inCount -= inLen;
        m_outCount += outLen;
    }

    std::uint32_t produced = std::min(m_outCount, outFrames);
    for (std::int32_t c = 0; c < m_channels; c++) {
        memcpy(out[c], m_out[c], sizeof(float) * produced);
    }
    if (produced < m_outCount) {
        for (std::int32_t c = 0; c < m_channels; c++) {
            memmove(m_out[c], m_out[c] + produced, sizeof(float) * (m_outCount - produced));
        }
    }
    m_outCount -= produced;
    return produced;
}

} // namespace jackmini
