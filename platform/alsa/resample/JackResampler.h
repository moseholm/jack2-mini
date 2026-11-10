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

#include <cstdint>
#include <stddef.h>

namespace jackmini {

// \brief Parameters used to create a resampler.

struct JackResamplerParams {
    std::uint32_t channels;
    std::uint32_t in_rate;
    std::uint32_t out_rate;
    std::int32_t quality;
};

// \brief Multi-channel planar float sample rate converter.
//
// Implementations are created by a JackResamplerFactory and injected into the
// ALSA secondary card code.  The interface is deliberately small so that
// tests can supply fakes.

class JackResampler {

public:
    virtual ~JackResampler()
    {
    }

    // Clear filter history and any pending state.
    virtual void Reset() = 0;

    // Ratio correction around nominal, 1.0 is nominal.  Used by the
    // asynchronous drift controller.
    virtual void SetRate(double rate) = 0;

    // Number of input frames needed to produce out_len output frames.
    virtual std::uint32_t InputLength(std::uint32_t out_len) = 0;

    // Number of output frames produced from in_len input frames.
    virtual std::uint32_t OutputLength(std::uint32_t in_len) = 0;

    // Consume up to *in_len input frames and produce up to *out_len
    // output frames.  Both counts are updated to the frames actually used.
    virtual void Process(const float* const* src, std::uint32_t* in_len,
        float* const* dst, std::uint32_t* out_len) = 0;

    // Latency introduced by the filter, in output frames.
    virtual std::uint32_t Delay() = 0;
};

// \brief Creates resamplers.  Factories are looked up by name.

class JackResamplerFactory {

public:
    virtual ~JackResamplerFactory()
    {
    }

    virtual const char* Name() const = 0;

    // Returns nullptr when the parameters cannot be supported.
    virtual JackResampler* Create(const JackResamplerParams& params) = 0;

    // The factory used when no other is requested.
    static JackResamplerFactory* Default();

    // Returns nullptr for unknown names.  Only "pipewire" exists.
    static JackResamplerFactory* Find(const char* name);
};

} // namespace jackmini
