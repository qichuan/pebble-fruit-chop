#include "fruit.h"

// Sixteen fruits cannot be told apart by hue alone on a 64-colour display -- the
// filter collapses neighbouring shades (see CLAUDE.md on orange vs red). So each
// row pairs a colour with a silhouette, and pairs that sit close in colour
// (peach/mango, banana/lemon) are always separated by shape.
//
// size_pct is relative to the random base radius in fc_config.h. The mean across
// the table is ~99%, so the roster's average size is set by FRUIT_RADIUS_MIN/MAX
// and this column only says which fruits are big ones.
static const FruitProfile s_profiles[FRUIT_TYPE_COUNT] = {
  [FRUIT_WATERMELON]  = { GColorIslamicGreenARGB8, GColorRedARGB8,
                          FRUIT_SHAPE_ROUND, 125, "watermelon" },
  // Neither Rajah nor ChromeYellow works here: both land in the salmon-pink
  // band through the filter, which is exactly where the peach already sits.
  // A true yellow body with a red cheek is the mango read, and the crescent
  // silhouette keeps it clear of the banana despite the shared skin colour.
  [FRUIT_MANGO]       = { GColorYellowARGB8,       GColorIcterineARGB8,
                          FRUIT_SHAPE_LOBED, 100, "mango" },
  [FRUIT_PINEAPPLE]   = { GColorLimerickARGB8,     GColorIcterineARGB8,
                          FRUIT_SHAPE_PATH,  120, "pineapple" },
  [FRUIT_COCONUT]     = { GColorWindsorTanARGB8,   GColorWhiteARGB8,
                          FRUIT_SHAPE_ROUND, 110, "coconut" },
  [FRUIT_STRAWBERRY]  = { GColorFollyARGB8,        GColorMelonARGB8,
                          FRUIT_SHAPE_PATH,   80, "strawberry" },
  [FRUIT_GREEN_APPLE] = { GColorKellyGreenARGB8,   GColorPastelYellowARGB8,
                          FRUIT_SHAPE_ROUND,  95, "green apple" },
  [FRUIT_RED_APPLE]   = { GColorRedARGB8,          GColorPastelYellowARGB8,
                          FRUIT_SHAPE_ROUND,  95, "red apple" },
  [FRUIT_KIWI]        = { GColorArmyGreenARGB8,    GColorInchwormARGB8,
                          FRUIT_SHAPE_ROUND,  90, "kiwifruit" },
  [FRUIT_BANANA]      = { GColorYellowARGB8,       GColorPastelYellowARGB8,
                          FRUIT_SHAPE_PATH,  105, "banana" },
  [FRUIT_LEMON]       = { GColorIcterineARGB8,     GColorPastelYellowARGB8,
                          FRUIT_SHAPE_LOBED,  85, "lemon" },
  [FRUIT_LIME]        = { GColorBrightGreenARGB8,  GColorMintGreenARGB8,
                          FRUIT_SHAPE_ROUND,  80, "lime" },
  // Not GColorOrange: through the display filter it renders within a few units
  // of GColorRed, which would make oranges and red apples indistinguishable.
  // Flesh is a shade off the skin on purpose: a cut face in the same colour as
  // the rind loses the segment lines drawn over it.
  [FRUIT_ORANGE]      = { GColorChromeYellowARGB8, GColorRajahARGB8,
                          FRUIT_SHAPE_ROUND, 100, "orange" },
  [FRUIT_PLUM]        = { GColorPurpleARGB8,       GColorMelonARGB8,
                          FRUIT_SHAPE_ROUND,  85, "plum" },
  [FRUIT_PEAR]        = { GColorSpringBudARGB8,    GColorPastelYellowARGB8,
                          FRUIT_SHAPE_LOBED, 105, "pear" },
  [FRUIT_PASSION]     = { GColorJazzberryJamARGB8, GColorIcterineARGB8,
                          FRUIT_SHAPE_ROUND,  85, "passion fruit" },
  [FRUIT_PEACH]       = { GColorMelonARGB8,        GColorRajahARGB8,
                          FRUIT_SHAPE_ROUND, 100, "peach" },
  // The bomb draws itself in draw.c and never uses skin/flesh, but it still
  // needs a row so fruit_profile() is total and its radius scale is neutral.
  [FRUIT_BOMB]        = { GColorBlackARGB8,        GColorDarkGrayARGB8,
                          FRUIT_SHAPE_ROUND, 100, "bomb" },
};

const FruitProfile *fruit_profile(FruitType type) {
  // The enum is unsigned here, so one bound check covers both ends.
  if ((unsigned)type >= FRUIT_TYPE_COUNT) {
    type = FRUIT_BOMB;
  }
  return &s_profiles[type];
}
