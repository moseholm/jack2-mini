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

#include <string.h>
#include <algorithm>

namespace jackmini {

static float** AllocChannels(int channels, unsigned int frames)
{
    float** buffers = new float*[channels];
    for (int i = 0; i < channels; i++) {
        buffers[i] = new float[frames];
        memset(buffers[i], 0, sizeof(float) * frames);
    }
    return buffers;
}

static void FreeChannels(float** buffers, int channels)
{
    if (buffers == nullptr) {
        return;
    }
    for (int i = 0; i < channels; i++) {
        delete[] buffers[i];
    }
    delete[] buffers;
}

JackAlsaRateConverter::JackAlsaRateConverter(JackResampler* resampler, int channels, unsigned int capacity)
    : fResampler(resampler)
    , fChannels(channels)
    , fCapacity(capacity)
    , fIn(nullptr)
    , fOut(nullptr)
    , fOutPtr(nullptr)
    , fInCount(0)
    , fOutCount(0)
{
    if (fResampler == nullptr) {
        return;
    }
    fIn = AllocChannels(channels, capacity);
    fOut = AllocChannels(channels, capacity);
    fOutPtr = new float*[channels];
}

JackAlsaRateConverter::~JackAlsaRateConverter()
{
    delete fResampler;
    FreeChannels(fIn, fChannels);
    FreeChannels(fOut, fChannels);
    delete[] fOutPtr;
}

void JackAlsaRateConverter::Reset()
{
    fInCount = 0;
    fOutCount = 0;
    if (fResampler) {
        fResampler->Reset();
    }
}

void JackAlsaRateConverter::SetRate(double rate)
{
    if (fResampler) {
        fResampler->SetRate(rate);
    }
}

unsigned int JackAlsaRateConverter::Delay()
{
    return fResampler ? fResampler->Delay() : 0;
}

unsigned int JackAlsaRateConverter::OutputBacklog()
{
    return fResampler ? fResampler->OutputLength(fInCount) : 0;
}

unsigned int JackAlsaRateConverter::NeedInput(unsigned int out_frames)
{
    unsigned int pending = std::min(fOutCount, out_frames);
    unsigned int want = fResampler->InputLength(out_frames - pending);
    if (want <= fInCount) {
        return 0;
    }
    want -= fInCount;
    if (fInCount + want > fCapacity) {
        want = fCapacity - fInCount;
    }
    return want;
}

unsigned int JackAlsaRateConverter::Convert(const float* const* in, unsigned int in_frames,
    float* const* out, unsigned int out_frames)
{
    if (out_frames > fCapacity) {
        out_frames = fCapacity;
    }

    if (in_frames > fCapacity - fInCount) {
        jack_log("JackAlsaRateConverter: input overflow, dropped %u frames", in_frames - (fCapacity - fInCount));
        in_frames = fCapacity - fInCount;
    }
    for (int c = 0; c < fChannels; c++) {
        memcpy(fIn[c] + fInCount, in[c], sizeof(float) * in_frames);
    }
    fInCount += in_frames;

    while (fOutCount < out_frames && fInCount > 0) {
        unsigned int in_len = fInCount;
        unsigned int out_len = out_frames - fOutCount;
        for (int c = 0; c < fChannels; c++) {
            fOutPtr[c] = fOut[c] + fOutCount;
        }

        fResampler->Process(fIn, &in_len, fOutPtr, &out_len);
        if (in_len == 0 && out_len == 0) {
            break;
        }

        if (in_len > 0 && in_len < fInCount) {
            for (int c = 0; c < fChannels; c++) {
                memmove(fIn[c], fIn[c] + in_len, sizeof(float) * (fInCount - in_len));
            }
        }
        fInCount -= in_len;
        fOutCount += out_len;
    }

    unsigned int produced = std::min(fOutCount, out_frames);
    for (int c = 0; c < fChannels; c++) {
        memcpy(out[c], fOut[c], sizeof(float) * produced);
    }
    if (produced < fOutCount) {
        for (int c = 0; c < fChannels; c++) {
            memmove(fOut[c], fOut[c] + produced, sizeof(float) * (fOutCount - produced));
        }
    }
    fOutCount -= produced;
    return produced;
}

} // namespace jackmini
