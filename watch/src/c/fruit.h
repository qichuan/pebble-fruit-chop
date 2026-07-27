#pragma once

#include <pebble.h>

// ---------------------------------------------------------------------------
// The roster. Everything that varies per fruit -- colour, silhouette, relative
// size -- lives in the one table in fruit.c, so adding a fruit is a new enum
// member plus a new row. game.c reads size_pct; draw.c reads the rest.
// ---------------------------------------------------------------------------

typedef enum {
  FRUIT_WATERMELON = 0,
  FRUIT_MANGO,
  FRUIT_PINEAPPLE,
  FRUIT_COCONUT,
  FRUIT_STRAWBERRY,
  FRUIT_GREEN_APPLE,
  FRUIT_RED_APPLE,
  FRUIT_KIWI,
  FRUIT_BANANA,
  FRUIT_LEMON,
  FRUIT_LIME,
  FRUIT_ORANGE,
  FRUIT_PLUM,
  FRUIT_PEAR,
  FRUIT_PASSION,
  FRUIT_PEACH,
  // Must stay last before the count: prv_spawn draws a fruit with
  // fc_rand_range(0, FRUIT_BOMB - 1), so everything below FRUIT_BOMB is edible.
  FRUIT_BOMB,
  FRUIT_TYPE_COUNT,
} FruitType;

typedef enum {
  FRUIT_SHAPE_ROUND = 0,  // plain circle
  FRUIT_SHAPE_LOBED,      // two circles on the spin axis: pear, lemon, mango
  FRUIT_SHAPE_PATH,       // rotated polygon: banana, strawberry, pineapple
} FruitShape;

typedef struct {
  // GColor*ARGB8 constants rather than the GColor* macros: those expand to
  // compound literals, which do not belong in a file-scope const initialiser.
  // Rebuild with (GColor){ .argb = ... } at draw time.
  uint8_t skin;
  uint8_t flesh;
  uint8_t shape;     // FruitShape
  uint8_t size_pct;  // scales the random base radius, per type
  const char *name;  // APP_LOG only, under FC_DEBUG_SWIPE
} FruitProfile;

const FruitProfile *fruit_profile(FruitType type);
