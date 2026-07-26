#include "game.h"
#include "fixed.h"

static GRect s_bounds;

static Fruit s_fruits[MAX_FRUITS];
static Half s_halves[MAX_HALVES];
static Juice s_juice[MAX_JUICE];

static GameState s_state;
static int s_score;
static int s_lives;
static int s_spawn_timer;

// ---------------------------------------------------------------------------
// Difficulty
//
// Only the spawn pressure and the bomb rate change. Physics stays identical so
// the game feels the same in the hand at every setting -- easy gives you more
// room to work in, hard gives you less, but a fruit never flies differently.
// ---------------------------------------------------------------------------
typedef struct {
  uint8_t interval;      // frames between spawn attempts at score 0
  uint8_t interval_min;  // floor once the score has climbed
  uint8_t bomb_pct;
  const char *name;
} DifficultySpec;

static const DifficultySpec s_difficulty[DIFF_COUNT] = {
  [DIFF_EASY]   = { 28, 16, 7,  "EASY" },
  [DIFF_NORMAL] = { FC_SPAWN_INTERVAL, FC_SPAWN_INTERVAL_MIN, FC_BOMB_CHANCE_PCT,
                    "NORMAL" },
  [DIFF_HARD]   = { 16, 7,  22, "HARD" },
};

static Difficulty s_difficulty_sel = DIFF_NORMAL;

// Persistent storage. Key 0 remembers the chosen difficulty; the high scores
// live one key per difficulty above it. persist_read_int returns 0 for a key
// that was never written, which is the right default for both.
#define PERSIST_KEY_DIFFICULTY 1
#define PERSIST_KEY_HIGH_BASE 2

static int s_high[DIFF_COUNT];

// Derived from the screen height in game_init so emery and gabbro play alike.
static int32_t s_gravity;
static int32_t s_launch_vy;

void game_init(GRect bounds) {
  s_bounds = bounds;

  // Stored as difficulty+1, because persist_read_int cannot distinguish a key
  // holding 0 from one that was never written -- and an unwritten key must mean
  // NORMAL, not EASY.
  const int32_t saved = persist_read_int(PERSIST_KEY_DIFFICULTY) - 1;
  s_difficulty_sel = (saved >= 0 && saved < DIFF_COUNT) ? (Difficulty)saved
                                                        : DIFF_NORMAL;
  for (int i = 0; i < DIFF_COUNT; i++) {
    s_high[i] = persist_read_int(PERSIST_KEY_HIGH_BASE + i);
  }

  // Solve for the gravity and launch speed that put the apex at APEX_NUM/APEX_DEN
  // of the screen height, reached in half of FC_AIRTIME_FRAMES.
  //   apex = g * t^2 / 2  and  v0 = g * t,  with t = airtime/2
  const int32_t t = FC_AIRTIME_FRAMES / 2;
  const int32_t apex = (bounds.size.h * FC_APEX_NUM) / FC_APEX_DEN;
  s_gravity = (2 * apex * FP_ONE) / (t * t);
  s_launch_vy = -(s_gravity * t);

  game_reset();
}

void game_reset(void) {
  for (int i = 0; i < MAX_FRUITS; i++) {
    s_fruits[i].active = false;
  }
  for (int i = 0; i < MAX_HALVES; i++) {
    s_halves[i].active = false;
  }
  for (int i = 0; i < MAX_JUICE; i++) {
    s_juice[i].active = false;
  }
  s_score = 0;
  s_lives = FC_START_LIVES;
  s_spawn_timer = 0;
}

Difficulty game_get_difficulty(void) { return s_difficulty_sel; }
const char *game_difficulty_name(Difficulty d) {
  return s_difficulty[(d < DIFF_COUNT) ? d : DIFF_NORMAL].name;
}
int game_get_high_score(void) { return s_high[s_difficulty_sel]; }

void game_set_difficulty(Difficulty d) {
  if (d >= DIFF_COUNT) {
    return;
  }
  s_difficulty_sel = d;
  persist_write_int(PERSIST_KEY_DIFFICULTY, (int32_t)d + 1);
}

// Called on the transition into game over, so a run that ends by bomb records
// its score the same as one that ends by dropping fruit.
static void prv_record_score(void) {
  if (s_score > s_high[s_difficulty_sel]) {
    s_high[s_difficulty_sel] = s_score;
    persist_write_int(PERSIST_KEY_HIGH_BASE + s_difficulty_sel, (int32_t)s_score);
  }
}

static Fruit *prv_free_fruit(void) {
  for (int i = 0; i < MAX_FRUITS; i++) {
    if (!s_fruits[i].active) {
      return &s_fruits[i];
    }
  }
  return NULL;
}

static Juice *prv_free_juice(void) {
  // Unlike the fruit and half pools, juice recycles: a burst that finds the
  // pool full steals the oldest droplet rather than being dropped. A cut that
  // silently produced no splash would read as a bug.
  Juice *oldest = &s_juice[0];
  for (int i = 0; i < MAX_JUICE; i++) {
    if (!s_juice[i].active) {
      return &s_juice[i];
    }
    if (s_juice[i].ttl < oldest->ttl) {
      oldest = &s_juice[i];
    }
  }
  return oldest;
}

// Throws a burst of droplets out of the cut, perpendicular to the blade. Both
// directions are seeded so the splash straddles the cut line the way it does
// when something wet is actually sliced.
static void prv_spray(const Fruit *f, int32_t cut_angle) {
  const int32_t normal = cut_angle + (TRIG_MAX_ANGLE / 4);
  const uint8_t colour = fruit_profile(f->type)->flesh;

  for (int i = 0; i < FC_JUICE_PER_CUT; i++) {
    Juice *j = prv_free_juice();

    const int32_t speed = fc_rand_range(FC_JUICE_SPEED_MIN, FC_JUICE_SPEED_MAX);
    // Fan the droplets around the normal instead of firing them all down one
    // line, and send roughly half to each side.
    const int32_t spread = (speed * fc_rand_range(-FC_JUICE_SPREAD,
                                                  FC_JUICE_SPREAD)) / 100;
    const int32_t dir = (i & 1) ? normal : normal + (TRIG_MAX_ANGLE / 2);

    j->x = f->x;
    j->y = f->y;
    j->vx = ((sin_lookup(dir) * speed) + (cos_lookup(dir) * spread)) / TRIG_MAX_RATIO;
    j->vy = ((-cos_lookup(dir) * speed) + (sin_lookup(dir) * spread)) / TRIG_MAX_RATIO;
    j->colour = colour;
    // Stagger the lifetimes so the splash thins out rather than vanishing in
    // one frame.
    j->ttl = (uint8_t)fc_rand_range(FC_JUICE_TTL / 2, FC_JUICE_TTL);
    j->ttl0 = j->ttl;
    j->active = true;
  }
}

static Half *prv_free_half(void) {
  for (int i = 0; i < MAX_HALVES; i++) {
    if (!s_halves[i].active) {
      return &s_halves[i];
    }
  }
  return NULL;
}

static void prv_spawn(void) {
  Fruit *f = prv_free_fruit();
  if (!f) {
    return;
  }

  const bool is_bomb =
      (fc_rand_range(0, 99) < s_difficulty[s_difficulty_sel].bomb_pct);
  f->type = is_bomb ? FRUIT_BOMB
                    : (FruitType)fc_rand_range(0, FRUIT_BOMB - 1);
  f->radius = is_bomb ? BOMB_RADIUS
                      : (uint8_t)((fc_rand_range(FRUIT_RADIUS_MIN, FRUIT_RADIUS_MAX) *
                                   fruit_profile(f->type)->size_pct) / 100);

  // Launch from just below the bottom edge, aimed back toward the middle so
  // fruit stays on screen long enough to be sliceable.
  const int16_t margin = f->radius + 4;
  const int16_t launch_x = (int16_t)fc_rand_range(margin, s_bounds.size.w - margin);
  f->x = FP(launch_x);
  f->y = FP(s_bounds.size.h + f->radius);

  // Drift toward the horizontal centre so the arc stays on screen, plus jitter.
  // The aim term scales with airtime on its own; the jitter does not, so it is
  // sized to give the same lateral spread over the now-shorter flight.
  const int32_t to_centre = (s_bounds.size.w / 2) - launch_x;
  f->vx = (FP(to_centre) / FC_AIRTIME_FRAMES) + fc_rand_range(-70, 70);

  // Jitter the launch speed +/-15% so the arcs are not identical.
  f->vy = s_launch_vy + ((s_launch_vy * fc_rand_range(-15, 15)) / 100);

  f->angle = 0;
  f->spin = fc_rand_range(FC_SPIN_MIN, FC_SPIN_MAX);
  if (fc_rand_range(0, 1)) {
    f->spin = -f->spin;
  }
  f->active = true;
}

// Splits along the blade: each half is the 180-degree sector on its own side of
// the cut, and the two kick apart along the cut's normal. Separating relative to
// the actual swipe direction is what makes it read as a cut rather than a fruit
// simply falling in two.
static void prv_split(const Fruit *f, int32_t cut_angle) {
  const int32_t normal = cut_angle + (TRIG_MAX_ANGLE / 4);
  const int32_t kick_x = (sin_lookup(normal) * FC_SPLIT_SPEED) / TRIG_MAX_RATIO;
  const int32_t kick_y = -(cos_lookup(normal) * FC_SPLIT_SPEED) / TRIG_MAX_RATIO;

  for (int side = 0; side < 2; side++) {
    Half *h = prv_free_half();
    if (!h) {
      return;
    }
    h->type = f->type;
    h->x = f->x;
    h->y = f->y;
    h->vx = f->vx + (side ? kick_x : -kick_x);
    h->vy = f->vy + (side ? kick_y : -kick_y);
    h->angle = cut_angle + (side ? (TRIG_MAX_ANGLE / 2) : 0);
    h->body_angle = f->angle;
    h->spin = side ? (FC_SPIN_MIN / 2) : -(FC_SPIN_MIN / 2);
    h->radius = f->radius;
    h->ttl = FC_HALF_TTL;
    h->active = true;
  }
}

void game_step(void) {
  if (s_state != STATE_PLAYING) {
    return;
  }

  // Spawn rate tightens as the score climbs, from the chosen difficulty's
  // starting interval down to its floor.
  const DifficultySpec *spec = &s_difficulty[s_difficulty_sel];
  int interval = spec->interval - (s_score / 5);
  if (interval < spec->interval_min) {
    interval = spec->interval_min;
  }
  if (++s_spawn_timer >= interval) {
    s_spawn_timer = 0;
    prv_spawn();
  }

  const int32_t floor_y = FP(s_bounds.size.h);

  for (int i = 0; i < MAX_FRUITS; i++) {
    Fruit *f = &s_fruits[i];
    if (!f->active) {
      continue;
    }
    f->vy += s_gravity;
    f->x += f->vx;
    f->y += f->vy;
    f->angle += f->spin;

    // Retire once it has fallen back past the bottom edge. A dropped fruit
    // costs a life; a dropped bomb is harmless.
    if (f->vy > 0 && f->y > floor_y + FP(f->radius)) {
      f->active = false;
      if (f->type != FRUIT_BOMB && --s_lives <= 0) {
        s_lives = 0;
        s_state = STATE_GAMEOVER;
        prv_record_score();
      }
    }
  }

  for (int i = 0; i < MAX_HALVES; i++) {
    Half *h = &s_halves[i];
    if (!h->active) {
      continue;
    }
    h->vy += s_gravity;
    h->x += h->vx;
    h->y += h->vy;
    // Both angles take the same spin, so the cut face keeps pointing the same
    // way relative to the piece it belongs to.
    h->angle += h->spin;
    h->body_angle += h->spin;
    if (--h->ttl == 0 || h->y > floor_y + FP(h->radius)) {
      h->active = false;
    }
  }

  for (int i = 0; i < MAX_JUICE; i++) {
    Juice *j = &s_juice[i];
    if (!j->active) {
      continue;
    }
    // Droplets hang rather than arc harder than the fruit. That is backwards
    // physically, but gravity is now strong enough that matching it collapses
    // the splash before it can be seen at 30fps.
    j->vy += s_gravity / 2;
    j->x += j->vx;
    j->y += j->vy;
    if (--j->ttl == 0 || j->y > floor_y) {
      j->active = false;
    }
  }
}

// Squared distance from circle centre c to the closest point on segment p0->p1,
// compared against r^2. Testing the segment rather than the latest touch point
// is what stops a fast swipe tunnelling straight through a fruit between two
// touch samples. All integer: on a 260px screen the terms stay far inside int32.
static bool prv_segment_hits_circle(GPoint p0, GPoint p1, GPoint c, int16_t r) {
  const int32_t dx = p1.x - p0.x;
  const int32_t dy = p1.y - p0.y;
  const int32_t fx = p0.x - c.x;
  const int32_t fy = p0.y - c.y;
  const int32_t a = dx * dx + dy * dy;

  int32_t t = 0;  // 8.8 fixed, clamped to [0, FP_ONE]
  if (a > 0) {
    t = (-(fx * dx + fy * dy) * FP_ONE) / a;
    if (t < 0) {
      t = 0;
    } else if (t > FP_ONE) {
      t = FP_ONE;
    }
  }

  const int32_t cx = p0.x + ((dx * t) >> FP_SHIFT) - c.x;
  const int32_t cy = p0.y + ((dy * t) >> FP_SHIFT) - c.y;
  return (cx * cx + cy * cy) <= ((int32_t)r * r);
}

int game_slice_segment(GPoint p0, GPoint p1) {
  if (s_state != STATE_PLAYING) {
    return 0;
  }

  // atan2_lookup returns a maths-convention angle measured from +x. Every
  // drawing API here (graphics_fill_radial, sin/cos_lookup) uses Pebble's
  // convention instead: 0 is straight up, increasing clockwise. They differ by
  // a quarter turn, and without this correction the cut face comes out
  // perpendicular to the swipe.
  const int32_t cut_angle =
      atan2_lookup((int16_t)(p1.y - p0.y), (int16_t)(p1.x - p0.x)) +
      (TRIG_MAX_ANGLE / 4);

  int cut = 0;
  for (int i = 0; i < MAX_FRUITS; i++) {
    Fruit *f = &s_fruits[i];
    if (!f->active) {
      continue;
    }
    const GPoint centre = GPoint(FP_INT(f->x), FP_INT(f->y));
    if (!prv_segment_hits_circle(p0, p1, centre, f->radius)) {
      continue;
    }

    if (f->type == FRUIT_BOMB) {
      f->active = false;
      s_state = STATE_GAMEOVER;
      prv_record_score();
      return cut;
    }

    // Deactivating here is also what prevents a second hit on the same fruit.
    // The loop deliberately does not break: one swipe cutting several fruits is
    // the whole point of the genre.
    prv_split(f, cut_angle);
    prv_spray(f, cut_angle);
    f->active = false;
    s_score++;
    cut++;
  }
  return cut;
}

#if FC_DEBUG_SWIPE
void game_debug_park(FruitType type, GPoint at) {
  // Clear the field first, otherwise whatever happened to be in flight shares
  // the swipe. A stray bomb in a lower pool slot ends the run before the
  // intended target is even tested, which makes the harness useless.
  for (int i = 0; i < MAX_FRUITS; i++) {
    s_fruits[i].active = false;
  }
  for (int i = 0; i < MAX_HALVES; i++) {
    s_halves[i].active = false;
  }
  // Push the next natural spawn well past the end of the scripted swipe.
  s_spawn_timer = 0;

  Fruit *f = prv_free_fruit();
  if (!f) {
    return;
  }
  f->type = type;
  f->radius = (type == FRUIT_BOMB)
      ? BOMB_RADIUS
      : (uint8_t)((FRUIT_RADIUS_MAX * fruit_profile(type)->size_pct) / 100);
  f->x = FP(at.x);
  f->y = FP(at.y);
  // Stationary: the physics step still applies gravity, but over the handful of
  // frames the scripted swipe takes it barely moves, so the hit is repeatable.
  f->vx = 0;
  f->vy = 0;
  f->angle = 0;
  f->spin = 0;
  f->active = true;
}
#endif

GameState game_get_state(void) { return s_state; }
void game_set_state(GameState state) { s_state = state; }
int game_get_score(void) { return s_score; }
int game_get_lives(void) { return s_lives; }
const Fruit *game_fruits(void) { return s_fruits; }
const Half *game_halves(void) { return s_halves; }
const Juice *game_juice(void) { return s_juice; }
