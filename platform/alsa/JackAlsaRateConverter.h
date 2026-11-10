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

#include "resample/JackResampler.h"

#include <cstdint>
#include <stddef.h>

namespace jackmini {

// \brief Streams planar float audio through a JackResampler.
//
// The converter keeps input and output staging so that callers can work in
// whole JACK cycles: NeedInput() says how many input frames to supply for a
// requested output count, and Convert() returns the frames it could produce.
// Not thread safe; intended to be used from the primary RT thread only.

class AlsaRateConverter {

private:
    Resampler* m_resampler;
    std::int32_t m_channels;
    std::uint32_t m_capacity;
    float** m_in;
    float** m_out;
    float** m_outPtr;
    std::uint32_t m_inCount;
    std::uint32_t m_outCount;

    AlsaRateConverter(const AlsaRateConverter&);
    AlsaRateConverter& operator=(const AlsaRateConverter&);

public:
    // Takes ownership of resampler.
    AlsaRateConverter(Resampler* resampler, std::int32_t channels, std::uint32_t capacity);
    ~AlsaRateConverter();

    bool isValid() const { return m_in != nullptr && m_out != nullptr; }

    void reset();

    // Ratio correction forwarded to the resampler, 1.0 is nominal.
    void setRate(double rate);

    // Input frames held in staging, not yet consumed by the resampler.
    std::uint32_t backlog() const { return m_inCount; }

    // Filter delay of the resampler, in frames.
    std::uint32_t delay();

    // Output frames the staged input would produce, not yet converted.
    std::uint32_t outputBacklog();

    // Input frames to supply before Convert(out_frames) to produce out_frames.
    std::uint32_t needInput(std::uint32_t outFrames);

    // Pushes in_frames and produces up to out_frames.  Returns the count produced.
    std::uint32_t convert(const float* const* in, std::uint32_t inFrames,
        float* const* out, std::uint32_t outFrames);
};

} // namespace jackmini
