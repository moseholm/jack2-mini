/*
 * Minimal subset of PipeWire's spa/utils/defs.h, used by the vendored
 * resampler in ../resample-native*.c.  Only the macros those files need are
 * provided.  PipeWire's logging is replaced by no-ops; CPU feature flags live
 * in spa/support/cpu.h and are selected at runtime by the bridge.
 */

#ifndef JACK_SPA_SHIM_UTILS_DEFS_H
#define JACK_SPA_SHIM_UTILS_DEFS_H

#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define SPA_RESTRICT __restrict
#define SPA_LIKELY(x) __builtin_expect(!!(x), 1)
#define SPA_UNLIKELY(x) __builtin_expect(!!(x), 0)

#define SPA_MIN(a, b) ((a) < (b) ? (a) : (b))
#define SPA_MAX(a, b) ((a) > (b) ? (a) : (b))
#define SPA_CLAMP(v, low, high) SPA_MIN(SPA_MAX(v, low), high)
#define SPA_N_ELEMENTS(arr) (sizeof(arr) / sizeof((arr)[0]))

#define SPA_ROUND_UP_N(num, align) ((((num) + (align) - 1) / (align)) * (align))
#define SPA_PTROFF(ptr_, offset_, type_) ((type_*)((char*)(ptr_) + (ptrdiff_t)(offset_)))
#define SPA_PTROFF_ALIGN(ptr_, offset_, alignment_, type_) \
    SPA_PTROFF(ptr_, SPA_ROUND_UP_N((size_t)(offset_), (size_t)(alignment_)), type_)

#define SPA_FOR_EACH_ELEMENT_VAR(arr, _var) \
    for (__typeof__(&(arr)[0]) _var = (arr); _var < (arr) + SPA_N_ELEMENTS(arr); _var++)

static inline void* spa_memcpy(void* dst, const void* src, size_t n)
{
    return memcpy(dst, src, n);
}

static inline void* spa_memmove(void* dst, const void* src, size_t n)
{
    return memmove(dst, src, n);
}

static inline int spa_streq(const char* a, const char* b)
{
    return strcmp(a, b) == 0;
}

#endif
