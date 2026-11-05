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

/*
 * Internal ABI for resampler plugins.  It is versioned and loaded with dlopen.
 * The header is not installed: plugins are built against this file in the
 * source tree.
 *
 * A plugin is a shared object named <name>.so in a directory listed in
 * JACK_RESAMPLER_PATH (colon separated).  It exports the entry point
 * jack_resampler_plugin_v1(), which returns a jack_resampler_plugin_t.  JACK
 * checks abi_version and struct_size before using any function pointer.
 *
 * The built-in "pipewire" resampler cannot be replaced by a plugin.
 */

#ifndef __JackResamplerPlugin__
#define __JackResamplerPlugin__

#ifdef __cplusplus
extern "C" {
#endif

#define JACK_RESAMPLER_PLUGIN_ABI_VERSION 1
#define JACK_RESAMPLER_PLUGIN_ENTRY "jack_resampler_plugin_v1"

typedef struct jack_resampler_plugin {
    unsigned int abi_version;   /* must be JACK_RESAMPLER_PLUGIN_ABI_VERSION */
    unsigned int struct_size;   /* sizeof(jack_resampler_plugin_t) as built */
    const char *name;           /* must match the file name without ".so" */

    /* Returns an opaque instance, or NULL if the parameters are not supported. */
    void *(*create)(unsigned int channels, unsigned int in_rate, unsigned int out_rate, int quality);
    void (*destroy)(void *instance);
    void (*reset)(void *instance);
    void (*set_rate)(void *instance, double rate);
    unsigned int (*in_len)(void *instance, unsigned int out_len);
    unsigned int (*out_len)(void *instance, unsigned int in_len);
    /* Planar float.  On return *in_len and *out_len hold the frames used. */
    void (*process)(void *instance, const float *const *src, unsigned int *in_len,
                    float *const *dst, unsigned int *out_len);
    unsigned int (*delay)(void *instance);
} jack_resampler_plugin_t;

typedef const jack_resampler_plugin_t *(*jack_resampler_plugin_entry_t)(void);

#ifdef __cplusplus
}
#endif

#endif
