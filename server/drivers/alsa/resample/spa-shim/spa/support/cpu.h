/*
 * Minimal subset of PipeWire's spa/support/cpu.h.  Only the x86 feature
 * flags used by the vendored resampler dispatcher are defined here; the JACK
 * build detects them at runtime and leaves the portable C kernel as the
 * fallback.
 */

#pragma once

#include <stdint.h>

#include <spa/utils/defs.h>

/* ARM specific */
#define SPA_CPU_FLAG_NEON (1 << 5)

/* x86 specific */
#define SPA_CPU_FLAG_SSE (1 << 3) /**< SSE */
#define SPA_CPU_FLAG_SSSE3 (1 << 7) /**< SSSE3 */
#define SPA_CPU_FLAG_AVX (1 << 11) /**< AVX */
#define SPA_CPU_FLAG_AVX2 (1 << 15) /**< AVX2 */
#define SPA_CPU_FLAG_FMA3 (1 << 16) /**< Haswell FMA3 */
#define SPA_CPU_FLAG_SLOW_UNALIGNED (1 << 20) /**< unaligned loads/stores are slow */
