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

#ifndef __JackPipeWireResampleBridge__
#define __JackPipeWireResampleBridge__

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Thin C interface over the vendored PipeWire resampler.  All upstream
 * struct handling stays in C; the C++ layer only sees these functions.
 */

struct jack_pw_resampler;

struct jack_pw_resampler *jack_pw_resampler_new(unsigned int channels,
                                                unsigned int in_rate,
                                                unsigned int out_rate,
                                                int quality);
void jack_pw_resampler_free(struct jack_pw_resampler *r);
void jack_pw_resampler_reset(struct jack_pw_resampler *r);
void jack_pw_resampler_set_rate(struct jack_pw_resampler *r, double rate);
unsigned int jack_pw_resampler_in_len(struct jack_pw_resampler *r, unsigned int out_len);
unsigned int jack_pw_resampler_out_len(struct jack_pw_resampler *r, unsigned int in_len);
void jack_pw_resampler_process(struct jack_pw_resampler *r,
                               const float *const *src, unsigned int *in_len,
                               float *const *dst, unsigned int *out_len);
unsigned int jack_pw_resampler_delay(struct jack_pw_resampler *r);

/* Name of the kernel selected at creation time ("c", "sse", "avx2", ...). */
const char *jack_pw_resampler_kernel_name(struct jack_pw_resampler *r);

#ifdef __cplusplus
}
#endif

#endif
