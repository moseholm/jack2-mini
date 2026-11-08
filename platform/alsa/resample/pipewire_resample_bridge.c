/*
Copyright (C) 2026 JACK contributors

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include "pipewire_resample_bridge.h"
#include "resample.h"

#include <spa/support/cpu.h>

#include <stdlib.h>
#include <string.h>

struct jack_pw_resampler {
    struct resample r;
};

/*
 * The resampler table is ordered from the widest kernel to the portable C
 * one, and find_resample_info() treats cpu_flags == 0 as "portable only".
 * On x86 ask the compiler runtime which kernels this CPU can execute.
 * __builtin_cpu_supports() masks AVX/AVX2/FMA with the OS XSAVE state, so it
 * is safe to use even when the kernel has not enabled the extended registers.
 * NEON is part of every AArch64 core, so it is simply reported as present.
 */
static uint32_t jack_pw_detect_cpu_flags(void)
{
    uint32_t flags = 0;

#if defined(HAVE_NEON) && defined(__aarch64__)
    flags |= SPA_CPU_FLAG_NEON;
#endif

#if (defined(__GNUC__) || defined(__clang__)) && \
    (defined(__x86_64__) || defined(__i386__))
    __builtin_cpu_init();
#ifdef HAVE_SSE
    if (__builtin_cpu_supports("sse")) {
        flags |= SPA_CPU_FLAG_SSE;
    }
#endif
#ifdef HAVE_AVX2
    if (__builtin_cpu_supports("avx2")) {
        flags |= SPA_CPU_FLAG_AVX2;
    }
#endif
#ifdef HAVE_FMA
    if (__builtin_cpu_supports("fma")) {
        flags |= SPA_CPU_FLAG_FMA3;
    }
#endif
#endif

    return flags;
}

struct jack_pw_resampler *jack_pw_resampler_new(unsigned int channels,
                                                unsigned int in_rate,
                                                unsigned int out_rate,
                                                int quality)
{
    struct jack_pw_resampler *res;

    if (channels == 0 || in_rate == 0 || out_rate == 0) {
        return NULL;
    }

    res = calloc(1, sizeof(*res));
    if (res == NULL) {
        return NULL;
    }

    res->r.log = NULL;
    res->r.options = 0;
    res->r.cpu_flags = jack_pw_detect_cpu_flags();
    res->r.channels = channels;
    res->r.i_rate = in_rate;
    res->r.o_rate = out_rate;
    res->r.rate = 1.0;
    res->r.quality = quality;
    memset(&res->r.config, 0, sizeof(res->r.config));

    if (resample_native_init(&res->r) < 0) {
        free(res);
        return NULL;
    }
    return res;
}

void jack_pw_resampler_free(struct jack_pw_resampler *r)
{
    if (r == NULL) {
        return;
    }
    resample_free(&r->r);
    free(r);
}

void jack_pw_resampler_reset(struct jack_pw_resampler *r)
{
    resample_reset(&r->r);
}

void jack_pw_resampler_set_rate(struct jack_pw_resampler *r, double rate)
{
    resample_update_rate(&r->r, rate);
}

unsigned int jack_pw_resampler_in_len(struct jack_pw_resampler *r, unsigned int out_len)
{
    return resample_in_len(&r->r, out_len);
}

unsigned int jack_pw_resampler_out_len(struct jack_pw_resampler *r, unsigned int in_len)
{
    return resample_out_len(&r->r, in_len);
}

void jack_pw_resampler_process(struct jack_pw_resampler *r,
                               const float *const *src, unsigned int *in_len,
                               float *const *dst, unsigned int *out_len)
{
    resample_process(&r->r, (const void **)src, in_len, (void **)dst, out_len);
}

unsigned int jack_pw_resampler_delay(struct jack_pw_resampler *r)
{
    return resample_delay(&r->r);
}

const char *jack_pw_resampler_kernel_name(struct jack_pw_resampler *r)
{
    if (r == NULL || r->r.func_name == NULL) {
        return "unknown";
    }
    return r->r.func_name;
}
