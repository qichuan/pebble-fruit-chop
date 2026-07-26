#pragma once

#include <pebble.h>
#include "fc_config.h"
#include "fruit.h"

typedef enum {
  STATE_TITLE = 0,
  STATE_PLAYING,
  STATE_GAMEOVER,
} GameState;

// A fruit or bomb in flight. Bombs share the pool so spawning and the physics
// step stay a single code path.
typedef struct {
  bool active;
  FruitType type;
  int32_t x, y;     // 8.8 fixed
  int32_t vx, vy;   // 8.8 fixed, per frame
  int32_t angle;    // TRIG_MAX_ANGLE units
  int32_t spin;
  uint8_t radius;   // px
} Fruit;

// One side of a sliced fruit.
typedef struct {
  bool active;
  FruitType type;
  int32_t x, y;
  int32_t vx, vy;
  int32_t angle;    // start of this half's 180-degree sector
  int32_t spin;
  uint8_t radius;
  uint8_t ttl;      // frames remaining
} Half;

void game_init(GRect bounds);
void game_reset(void);
void game_step(void);

// Slices any fruit whose bounding circle the segment p0->p1 crosses.
// Returns the number of fruits cut. Slicing a bomb ends the game.
int game_slice_segment(GPoint p0, GPoint p1);

GameState game_get_state(void);
void game_set_state(GameState state);
int game_get_score(void);
int game_get_lives(void);

const Fruit *game_fruits(void);
const Half *game_halves(void);

#if FC_DEBUG_SWIPE
// Parks a stationary target at `at` so the scripted swipe has something
// deterministic to cut. Debug harness only.
void game_debug_park(FruitType type, GPoint at);
#endif
