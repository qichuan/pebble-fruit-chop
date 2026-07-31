#pragma once

#include <pebble.h>
#include "fc_config.h"
#include "fruit.h"

typedef enum {
  STATE_TITLE = 0,
  STATE_PLAYING,
  STATE_GAMEOVER,
} GameState;

typedef enum {
  DIFF_EASY = 0,
  DIFF_NORMAL,
  DIFF_HARD,
  DIFF_COUNT,
} Difficulty;

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
  int32_t angle;       // start of this half's 180-degree sector
  // The parent fruit's own rotation at the moment of the cut. Kept alongside
  // `angle` because the two are independent: `angle` says where the blade went,
  // this says which way the fruit was facing, and draw_half needs both to clip
  // the right piece off the right silhouette. Both advance by `spin`, so they
  // stay locked to each other as the half tumbles.
  int32_t body_angle;
  int32_t spin;
  uint8_t radius;
  uint8_t ttl;      // frames remaining
} Half;

// A droplet of juice thrown off a cut. It carries its own colour so game.c can
// spawn it from the fruit's flesh without knowing anything about drawing.
typedef struct {
  bool active;
  int32_t x, y;
  int32_t vx, vy;
  uint8_t colour;   // GColor argb byte
  uint8_t ttl;
  uint8_t ttl0;     // ttl at spawn, so draw can shrink the droplet as it ages
} Juice;

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

// Fruits cut since this was last called, then clears. Slicing happens on touch
// events, which arrive between frames; main.c drains this once a frame so a
// stroke through three fruits makes one sound rather than three overlapping
// ones. Keeping it a count rather than a flag leaves room for a combo bonus.
int game_take_cut_events(void);

// Difficulty is chosen on the title screen and persists across runs, as does a
// separate high score per difficulty -- a score on easy should not sit in the
// same column as one on hard.
Difficulty game_get_difficulty(void);
void game_set_difficulty(Difficulty d);
const char *game_difficulty_name(Difficulty d);
int game_get_high_score(void);

const Fruit *game_fruits(void);
const Half *game_halves(void);
const Juice *game_juice(void);

#if FC_DEBUG_SWIPE
// Parks a stationary target at `at` so the scripted swipe has something
// deterministic to cut. Debug harness only.
void game_debug_park(FruitType type, GPoint at);
#endif
