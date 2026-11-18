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

#include <stddef.h>
#include <cstdint>

namespace jackmini {

// \brief Parameters used to create a resampler.

struct ResamplerParams {
    std::uint32_t m_channels;
    std::uint32_t m_inRate;
    std::uint32_t m_outRate;
    std::int32_t m_quality;
};

// \brief Multi-channel planar float sample rate converter.
//
// Implementations are created by a JackResamplerFactory and injected into the
// ALSA secondary card code.  The interface is deliberately small so that
// tests can supply fakes.

class Resampler {

public:
    virtual ~Resampler()
    {
    }

    // Clear filter history and any pending state.
    virtual void reset() = 0;

    // Ratio correction around nominal, 1.0 is nominal.  Used by the
    // asynchronous drift controller.
    virtual void setRate(double rate) = 0;

    // Number of input frames needed to produce out_len output frames.
    virtual std::uint32_t inputLength(std::uint32_t outLen) = 0;

    // Number of output frames produced from in_len input frames.
    virtual std::uint32_t outputLength(std::uint32_t inLen) = 0;

    // Consume up to *in_len input frames and produce up to *out_len
    // output frames.  Both counts are updated to the frames actually used.
    virtual void process(const float* const* src, std::uint32_t* inLen,
        float* const* dst, std::uint32_t* outLen) = 0;

    // Latency introduced by the filter, in output frames.
    virtual std::uint32_t delay() = 0;
};

// \brief Creates resamplers.  Factories are looked up by name.

class ResamplerFactory {

public:
    virtual ~ResamplerFactory()
    {
    }

    virtual const char* name() const = 0;

    // Returns nullptr when the parameters cannot be supported.
    virtual Resampler* create(const ResamplerParams& params) = 0;

    // The factory used when no other is requested.
    static ResamplerFactory* defaultFactory();

    // Returns nullptr for unknown names.  Only "pipewire" exists.
    static ResamplerFactory* find(const char* name);
};

} // namespace jackmini
