#pragma once

#include <pebble.h>

// 8.8 fixed-point. The Pebble toolchain has no reliable hard-float, so all
// physics runs in integers. 8 fractional bits gives sub-pixel velocity while
// leaving plenty of headroom for a 260px screen in an int32_t.
#define FP_SHIFT 8
#define FP_ONE (1 << FP_SHIFT)

#define FP(px) ((int32_t)((px) * FP_ONE))
#define FP_INT(v) ((int16_t)((v) >> FP_SHIFT))
#define FP_MUL(a, b) (((a) * (b)) >> FP_SHIFT)
#define FP_DIV(a, b) (((a) << FP_SHIFT) / (b))

// Random helpers. rand() is seeded from time() in prv_init.
static inline int32_t fc_rand_range(int32_t lo, int32_t hi) {
  return (hi <= lo) ? lo : (lo + (rand() % (hi - lo + 1)));
}
