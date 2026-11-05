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

/* A pass-through resampler plugin, used only by the unit tests. */

#include "resample/JackResamplerPlugin.h"

#include <stdlib.h>
#include <string.h>

typedef struct {
    unsigned int channels;
    double rate;
} passthrough_t;

static void *pt_create(unsigned int channels, unsigned int in_rate, unsigned int out_rate, int quality)
{
    passthrough_t *p;
    (void)in_rate;
    (void)out_rate;
    (void)quality;
    if (channels == 0 || channels > 2) {
        return NULL;   /* the test only drives two channels */
    }
    p = calloc(1, sizeof(*p));
    if (p) {
        p->channels = channels;
        p->rate = 1.0;
    }
    return p;
}

static void pt_destroy(void *instance) { free(instance); }
static void pt_reset(void *instance) { (void)instance; }
static void pt_set_rate(void *instance, double rate) { ((passthrough_t *)instance)->rate = rate; }
static unsigned int pt_in_len(void *instance, unsigned int out_len) { (void)instance; return out_len; }
static unsigned int pt_out_len(void *instance, unsigned int in_len) { (void)instance; return in_len; }
static unsigned int pt_delay(void *instance) { (void)instance; return 0; }

static void pt_process(void *instance, const float *const *src, unsigned int *in_len,
                       float *const *dst, unsigned int *out_len)
{
    passthrough_t *p = (passthrough_t *)instance;
    unsigned int n = (*in_len < *out_len) ? *in_len : *out_len;
    unsigned int c;
    for (c = 0; c < p->channels; c++) {
        memcpy(dst[c], src[c], sizeof(float) * n);
    }
    *in_len = n;
    *out_len = n;
}

static const jack_resampler_plugin_t plugin = {
    JACK_RESAMPLER_PLUGIN_ABI_VERSION,
    sizeof(jack_resampler_plugin_t),
    "passthrough_test",
    pt_create,
    pt_destroy,
    pt_reset,
    pt_set_rate,
    pt_in_len,
    pt_out_len,
    pt_process,
    pt_delay
};

const jack_resampler_plugin_t *jack_resampler_plugin_v1(void)
{
    return &plugin;
}
