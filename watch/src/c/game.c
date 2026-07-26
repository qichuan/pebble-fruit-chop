#include "game.h"
#include "fixed.h"

static GRect s_bounds;

static Fruit s_fruits[MAX_FRUITS];
static Half s_halves[MAX_HALVES];

static GameState s_state;
static int s_score;
static int s_lives;
static int s_spawn_timer;

// Derived from the screen height in game_init so emery and gabbro play alike.
static int32_t s_gravity;
static int32_t s_launch_vy;

void game_init(GRect bounds) {
  s_bounds = bounds;

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
  s_score = 0;
  s_lives = FC_START_LIVES;
  s_spawn_timer = 0;
}

static Fruit *prv_free_fruit(void) {
  for (int i = 0; i < MAX_FRUITS; i++) {
    if (!s_fruits[i].active) {
      return &s_fruits[i];
    }
  }
  return NULL;
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

  const bool is_bomb = (fc_rand_range(0, 99) < FC_BOMB_CHANCE_PCT);
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
  const int32_t to_centre = (s_bounds.size.w / 2) - launch_x;
  f->vx = (FP(to_centre) / FC_AIRTIME_FRAMES) + fc_rand_range(-40, 40);

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

  // Spawn rate tightens as the score climbs.
  int interval = FC_SPAWN_INTERVAL - (s_score / 5);
  if (interval < FC_SPAWN_INTERVAL_MIN) {
    interval = FC_SPAWN_INTERVAL_MIN;
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
      return cut;
    }

    // Deactivating here is also what prevents a second hit on the same fruit.
    // The loop deliberately does not break: one swipe cutting several fruits is
    // the whole point of the genre.
    prv_split(f, cut_angle);
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
