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

#include "JackResampler.h"
#include "pipewire_resample_bridge.h"

#include <cstdint>
#include <string.h>

static constexpr std::int32_t kResampleDefaultQuality = 4;

namespace jackmini {

class PipeWireResampler : public Resampler {

private:
    jack_pw_resampler* m_handle;

public:
    explicit PipeWireResampler(jack_pw_resampler* handle)
        : m_handle(handle)
    {
    }

    virtual ~PipeWireResampler()
    {
        jack_pw_resampler_free(m_handle);
    }

    void reset()
    {
        jack_pw_resampler_reset(m_handle);
    }

    void setRate(double rate)
    {
        jack_pw_resampler_set_rate(m_handle, rate);
    }

    std::uint32_t inputLength(std::uint32_t outLen)
    {
        return jack_pw_resampler_in_len(m_handle, outLen);
    }

    std::uint32_t outputLength(std::uint32_t inLen)
    {
        return jack_pw_resampler_out_len(m_handle, inLen);
    }

    void process(const float* const* src, std::uint32_t* inLen,
        float* const* dst, std::uint32_t* outLen)
    {
        jack_pw_resampler_process(m_handle, src, inLen, dst, outLen);
    }

    std::uint32_t delay()
    {
        return jack_pw_resampler_delay(m_handle);
    }
};

class PipeWireResamplerFactory : public ResamplerFactory {

public:
    const char* name() const
    {
        return "pipewire";
    }

    Resampler* create(const ResamplerParams& params)
    {
        std::int32_t quality = (params.m_quality < 0) ? kResampleDefaultQuality : params.m_quality;
        jack_pw_resampler* handle = jack_pw_resampler_new(params.m_channels, params.m_inRate,
            params.m_outRate, quality);
        if (handle == nullptr) {
            return nullptr;
        }
        return new PipeWireResampler(handle);
    }
};

ResamplerFactory* ResamplerFactory::find(const char* name)
{
    static PipeWireResamplerFactory pipewire;
    if (name == nullptr) {
        return nullptr;
    }
    if (strcmp(name, pipewire.name()) == 0) {
        return &pipewire;
    }
    return nullptr;
}

ResamplerFactory* ResamplerFactory::defaultFactory()
{
    return find("pipewire");
}

} // namespace jackmini
