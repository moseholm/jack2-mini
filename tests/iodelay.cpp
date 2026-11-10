// Copyright (C) 2003-2008 Fons Adriaensen <fons@kokkinizita.net>
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

// --------------------------------------------------------------------------------

#include "jackmini/jackmini.hpp"
#include "jackmini_wait.hpp"

#include <array>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace jackmini;

struct Freq {
    std::int32_t m_p;
    std::int32_t m_f;
    float m_xa;
    float m_ya;
    float m_x1;
    float m_y1;
    float m_x2;
    float m_y2;
};


struct MTDM {
    double m_del;
    double m_err;
    float m_wlp;
    std::int32_t m_cnt;
    std::int32_t m_inv;

    std::array<struct Freq, 13> m_freq;
};


struct MTDM* mtdmNew(double fsamp)
{
    std::int32_t i;
    struct Freq* f;

    struct MTDM* retval = (MTDM*)malloc(sizeof(struct MTDM));

    if (retval == nullptr)
        return nullptr;

    retval->m_cnt = 0;
    retval->m_inv = 0;

    retval->m_freq[0].m_f = 4096;
    retval->m_freq[1].m_f = 2048;
    retval->m_freq[2].m_f = 3072;
    retval->m_freq[3].m_f = 2560;
    retval->m_freq[4].m_f = 2304;
    retval->m_freq[5].m_f = 2176;
    retval->m_freq[6].m_f = 1088;
    retval->m_freq[7].m_f = 1312;
    retval->m_freq[8].m_f = 1552;
    retval->m_freq[9].m_f = 1800;
    retval->m_freq[10].m_f = 3332;
    retval->m_freq[11].m_f = 3586;
    retval->m_freq[12].m_f = 3841;
    retval->m_wlp = 200.0f / fsamp;
    for (i = 0, f = retval->m_freq.data(); i < 13; i++, f++) {
        f->m_p = 128;
        f->m_xa = f->m_ya = 0.0f;
        f->m_x1 = f->m_y1 = 0.0f;
        f->m_x2 = f->m_y2 = 0.0f;
    }

    return retval;
}

std::int32_t mtdmProcess(struct MTDM* self, size_t len, float* ip, float* op)
{
    std::int32_t i;
    float vip, vop, a, c, s;
    struct Freq* f;

    while (len--) {
        vop = 0.0f;
        vip = *ip++;
        for (i = 0, f = self->m_freq.data(); i < 13; i++, f++) {
            a = 2 * (float)M_PI * (f->m_p & 65535) / 65536.0;
            f->m_p += f->m_f;
            c = cosf(a);
            s = -sinf(a);
            vop += (i ? 0.01f : 0.20f) * s;
            f->m_xa += s * vip;
            f->m_ya += c * vip;
        }
        *op++ = vop;
        if (++self->m_cnt == 16) {
            for (i = 0, f = self->m_freq.data(); i < 13; i++, f++) {
                f->m_x1 += self->m_wlp * (f->m_xa - f->m_x1 + 1e-20);
                f->m_y1 += self->m_wlp * (f->m_ya - f->m_y1 + 1e-20);
                f->m_x2 += self->m_wlp * (f->m_x1 - f->m_x2 + 1e-20);
                f->m_y2 += self->m_wlp * (f->m_y1 - f->m_y2 + 1e-20);
                f->m_xa = f->m_ya = 0.0f;
            }
            self->m_cnt = 0;
        }
    }

    return 0;
}

std::int32_t mtdmResolve(struct MTDM* self)
{
    std::int32_t i, k, m;
    double d, e, f0, p;
    struct Freq* f = self->m_freq.data();

    if (hypot(f->m_x2, f->m_y2) < 0.001)
        return -1;
    d = atan2(f->m_y2, f->m_x2) / (2 * M_PI);
    if (self->m_inv)
        d += 0.5;
    if (d > 0.5)
        d -= 1.0;
    f0 = self->m_freq[0].m_f;
    m = 1;
    self->m_err = 0.0;
    for (i = 0; i < 12; i++) {
        f++;
        p = atan2(f->m_y2, f->m_x2) / (2 * M_PI) - d * f->m_f / f0;
        if (self->m_inv)
            p += 0.5;
        p -= floor(p);
        p *= 2;
        k = (std::int32_t)(floor(p + 0.5));
        e = fabs(p - k);
        if (e > self->m_err)
            self->m_err = e;
        if (e > 0.4)
            return 1;
        d += m * (k & 1);
        m *= 2;
    }
    self->m_del = 16 * d;

    return 0;
}

void mtdmInvert(struct MTDM* self)
{
    self->m_inv ^= 1;
}
// --------------------------------------------------------------------------------

static struct MTDM* gMtdm;
static Client* gJackHandle;
static Port gJackCapt;
static Port gJackPlay;
static std::atomic<std::uint64_t> gCycles { 0 };

LatencyRange gCaptureLatency = { UINT32_MAX, UINT32_MAX };
LatencyRange gPlaybackLatency = { UINT32_MAX, UINT32_MAX };

void latencyCb(LatencyMode mode)
{
    LatencyRange range { 0, 0 };

    if (mode == LatencyMode::kCapture) {
        gJackHandle->setPortLatency(gJackPlay, mode, range);
        range = gJackHandle->portLatency(gJackCapt, mode);
        if ((range.m_min != gCaptureLatency.m_min) || (range.m_max != gCaptureLatency.m_max)) {
            gCaptureLatency = range;
            printf("new capture latency: [%d, %d]\n", range.m_min, range.m_max);
        }
    } else {
        gJackHandle->setPortLatency(gJackCapt, mode, range);
        range = gJackHandle->portLatency(gJackPlay, mode);
        if ((range.m_min != gPlaybackLatency.m_min) || (range.m_max != gPlaybackLatency.m_max)) {
            gPlaybackLatency = range;
            printf("new playback latency: [%d, %d]\n", range.m_min, range.m_max);
        }
    }
}

bool callback(const Cycle& cycle)
{
    const FrameCount nframes = cycle.m_frames;
    auto ip = gJackHandle->audioBuffer(gJackCapt);
    auto op = gJackHandle->audioBuffer(gJackPlay);
    mtdmProcess(gMtdm, nframes, ip.data(), op.data());
    gCycles++;
    return true;
}

int main(std::int32_t ac, char* av[])
{
    float t;
    OpenOptions options;
    options.m_startServer = false;

    auto opened = Client::open("jack_delay", options);
    if (!opened) {
        fprintf(stderr, "Can't connect to Jack, is the server running ?\n");
        exit(1);
    }
    Client client = std::move(*opened);
    gJackHandle = &client;

    gMtdm = mtdmNew(client.sampleRate());

    client.onProcess(callback);
    client.onLatency(latencyCb);

    auto capt = client.registerPort("in", PortType::kAudio, PortFlags::kInput);
    auto play = client.registerPort("out", PortType::kAudio, PortFlags::kOutput);
    if (!capt || !play) {
        fprintf(stderr, "Can't register the ports\n");
        return 1;
    }
    gJackCapt = *capt;
    gJackPlay = *play;

    t = 1000.0f / client.sampleRate();

    if (!client.activate().has_value()) {
        fprintf(stderr, "Can't activate Jack");
        return 1;
    }

    // A report every quarter second of audio: counted in cycles, so it holds in freewheel mode as well.
    const std::uint64_t cyclesPerReport = std::max<std::uint64_t>(1, client.sampleRate() / 4 / client.bufferSize());
    while (1) {
        if (!waitCycles(gCycles, cyclesPerReport)) {
            fprintf(stderr, "The server stopped running cycles\n");
            return 1;
        }
        if (mtdmResolve(gMtdm) < 0)
            printf("Signal below threshold...\n");
        else {
            FrameCount systemicLatency;

            if (gMtdm->m_err > 0.3) {
                mtdmInvert(gMtdm);
                mtdmResolve(gMtdm);
            }
            systemicLatency = (FrameCount)floor(gMtdm->m_del - (gCaptureLatency.m_max + gPlaybackLatency.m_max));

            printf("%10.3lf frames %10.3lf ms total roundtrip latency\n\textra loopback latency: %u frames\n\tuse %u for the backend arguments -I and -O", gMtdm->m_del, gMtdm->m_del * t,
                systemicLatency, systemicLatency / 2);
            if (gMtdm->m_err > 0.2)
                printf(" ??");
            if (gMtdm->m_inv)
                printf(" Inv");
            printf("\n");
        }
    }

    return 0;
}

// --------------------------------------------------------------------------------
