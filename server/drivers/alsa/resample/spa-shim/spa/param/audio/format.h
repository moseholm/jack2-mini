/*
 * Minimal subset of PipeWire's spa/param/audio/format.h.  The resampler only
 * needs the 32-bit float format identifier.
 */

#pragma once

enum spa_audio_format {
    SPA_AUDIO_FORMAT_F32 = 0x11c
};
