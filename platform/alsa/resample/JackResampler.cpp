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

#include <string.h>

#ifndef RESAMPLE_DEFAULT_QUALITY
#define RESAMPLE_DEFAULT_QUALITY 4
#endif

namespace jackmini {

class JackPipeWireResampler : public JackResampler {

private:
    jack_pw_resampler* fHandle;

public:
    explicit JackPipeWireResampler(jack_pw_resampler* handle)
        : fHandle(handle)
    {
    }

    virtual ~JackPipeWireResampler()
    {
        jack_pw_resampler_free(fHandle);
    }

    void Reset()
    {
        jack_pw_resampler_reset(fHandle);
    }

    void SetRate(double rate)
    {
        jack_pw_resampler_set_rate(fHandle, rate);
    }

    unsigned int InputLength(unsigned int out_len)
    {
        return jack_pw_resampler_in_len(fHandle, out_len);
    }

    unsigned int OutputLength(unsigned int in_len)
    {
        return jack_pw_resampler_out_len(fHandle, in_len);
    }

    void Process(const float* const* src, unsigned int* in_len,
        float* const* dst, unsigned int* out_len)
    {
        jack_pw_resampler_process(fHandle, src, in_len, dst, out_len);
    }

    unsigned int Delay()
    {
        return jack_pw_resampler_delay(fHandle);
    }
};

class JackPipeWireResamplerFactory : public JackResamplerFactory {

public:
    const char* Name() const
    {
        return "pipewire";
    }

    JackResampler* Create(const JackResamplerParams& params)
    {
        int quality = (params.quality < 0) ? RESAMPLE_DEFAULT_QUALITY : params.quality;
        jack_pw_resampler* handle = jack_pw_resampler_new(params.channels, params.in_rate,
            params.out_rate, quality);
        if (handle == NULL) {
            return NULL;
        }
        return new JackPipeWireResampler(handle);
    }
};

JackResamplerFactory* JackResamplerFactory::Find(const char* name)
{
    static JackPipeWireResamplerFactory pipewire;
    if (name == NULL) {
        return NULL;
    }
    if (strcmp(name, pipewire.Name()) == 0) {
        return &pipewire;
    }
    return NULL;
}

JackResamplerFactory* JackResamplerFactory::Default()
{
    return Find("pipewire");
}

} // namespace jackmini
