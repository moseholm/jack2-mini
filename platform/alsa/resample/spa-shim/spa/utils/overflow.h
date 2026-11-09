/*
 * Minimal subset of PipeWire's spa/utils/overflow.h.  Returns non-zero when
 * the operation would overflow, like the upstream helpers.
 */

#pragma once

#include <stddef.h>

#define spa_overflow_mul(a, b, res) __builtin_mul_overflow((a), (b), (res))
#define spa_overflow_add(a, b, res) __builtin_add_overflow((a), (b), (res))
