#include <pebble.h>
#include <stdlib.h>

#include "fc_config.h"
#include "fixed.h"
#include "game.h"
#include "draw.h"
#include "blade.h"
#include "sound.h"

static Window *s_window;
static Layer *s_canvas;
static AppTimer *s_timer;
static bool s_touch_ok;

// ---------------------------------------------------------------------------
// Debug swipe harness
//
// The emulator cannot inject touch: there is no `pebble emu-touch`, and
// libpebble2 carries no touch packet. Without this, the entire slice pipeline
// would be unverifiable from the CLI. A button press parks a stationary target
// and plays a scripted swipe through blade_feed() -- the exact entry point real
// TouchService events use, so synthetic and real input cannot diverge.
// ---------------------------------------------------------------------------
#if FC_DEBUG_SWIPE
static int s_dbg_step = -1;   // -1 = idle
static int s_dbg_settle;      // frames of motion after the cut, before freezing
static int s_dbg_freeze;
static GPoint s_dbg_from, s_dbg_to;

static void prv_debug_start_swipe(FruitType type) {
  if (game_get_state() != STATE_PLAYING || s_dbg_step >= 0) {
    return;
  }
  const GRect b = layer_get_bounds(s_canvas);
  game_debug_park(type, GPoint(b.size.w / 2, b.size.h / 2));
  APP_LOG(APP_LOG_LEVEL_DEBUG, "debug swipe target: %s", fruit_profile(type)->name);

  // A line through the exact centre, so the parked target is always crossed.
  s_dbg_from = GPoint((b.size.w * 15) / 100, (b.size.h * 65) / 100);
  s_dbg_to = GPoint((b.size.w * 85) / 100, (b.size.h * 35) / 100);
  s_dbg_step = 0;
}

// Emits one waypoint per frame, so segment lengths match a real finger's.
static void prv_debug_step(void) {
  if (s_dbg_step < 0) {
    return;
  }
  const int n = FC_DEBUG_SWIPE_STEPS;
  TouchEvent e = {
    .x = (int16_t)(s_dbg_from.x + ((s_dbg_to.x - s_dbg_from.x) * s_dbg_step) / n),
    .y = (int16_t)(s_dbg_from.y + ((s_dbg_to.y - s_dbg_from.y) * s_dbg_step) / n),
    .type = (s_dbg_step == 0) ? TouchEvent_Touchdown
                              : (s_dbg_step == n ? TouchEvent_Liftoff
                                                 : TouchEvent_PositionUpdate),
  };
  blade_feed(&e);

  if (s_dbg_step == n) {
    s_dbg_step = -1;
    s_dbg_settle = FC_DEBUG_SETTLE_FRAMES;
    APP_LOG(APP_LOG_LEVEL_DEBUG, "debug swipe done: score=%d lives=%d state=%d",
            game_get_score(), game_get_lives(), (int)game_get_state());
  } else {
    s_dbg_step++;
  }
}
#endif  // FC_DEBUG_SWIPE

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

static void prv_touch_handler(const TouchEvent *event, void *context) {
  if (game_get_state() == STATE_TITLE) {
    if (event->type == TouchEvent_Touchdown) {
      blade_reset();
      game_reset();
      game_set_state(STATE_PLAYING);
    }
    layer_mark_dirty(s_canvas);
    return;
  }

  blade_feed(event);
  // Redraw immediately so the trail tracks the finger between physics frames.
  layer_mark_dirty(s_canvas);
}

static void prv_select_click(ClickRecognizerRef recognizer, void *context) {
  switch (game_get_state()) {
    case STATE_TITLE:
    case STATE_GAMEOVER:
      blade_reset();
      game_reset();
      game_set_state(STATE_PLAYING);
      break;
    case STATE_PLAYING:
#if FC_DEBUG_SWIPE
      prv_debug_start_swipe(FRUIT_WATERMELON);
#endif
      break;
  }
  layer_mark_dirty(s_canvas);
}

static void prv_back_click(ClickRecognizerRef recognizer, void *context) {
  // Mid-game BACK returns to the title rather than dumping the player out of
  // the app on a stray press. From the title it falls through to the default
  // pop, which exits.
  if (game_get_state() == STATE_PLAYING || game_get_state() == STATE_GAMEOVER) {
    blade_reset();
    game_set_state(STATE_TITLE);
    layer_mark_dirty(s_canvas);
    return;
  }
  window_stack_pop(true);
}

// On the title screen UP picks the difficulty and DOWN toggles sound. Difficulty
// wraps rather than clamping, because it is down to one button now and clamping
// would leave no way back to EASY. During play both are free, so the debug
// harness borrows them there and nowhere else.
static void prv_up_click(ClickRecognizerRef recognizer, void *context) {
  if (game_get_state() == STATE_TITLE) {
    game_set_difficulty((Difficulty)(((int)game_get_difficulty() + 1) % DIFF_COUNT));
    layer_mark_dirty(s_canvas);
    return;
  }
#if FC_DEBUG_SWIPE
  prv_debug_start_swipe(FRUIT_BOMB);  // verifies the bomb -> game over path
#endif
}

static void prv_down_click(ClickRecognizerRef recognizer, void *context) {
  if (game_get_state() == STATE_TITLE) {
    sound_set_enabled(!sound_is_enabled());
    layer_mark_dirty(s_canvas);
    return;
  }
#if FC_DEBUG_SWIPE
  // Cycles the roster, so every fruit can be cut and screenshotted from the CLI
  // without a rebuild between them.
  static int s_dbg_fruit = -1;
  s_dbg_fruit = (s_dbg_fruit + 1) % FRUIT_BOMB;
  prv_debug_start_swipe((FruitType)s_dbg_fruit);
#endif
}

static void prv_click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_SELECT, prv_select_click);
  window_single_click_subscribe(BUTTON_ID_BACK, prv_back_click);
  window_single_click_subscribe(BUTTON_ID_UP, prv_up_click);
  window_single_click_subscribe(BUTTON_ID_DOWN, prv_down_click);
}

// ---------------------------------------------------------------------------
// Loop and rendering
// ---------------------------------------------------------------------------

static void prv_timer_callback(void *data) {
  s_timer = NULL;

#if FC_DEBUG_SWIPE
  prv_debug_step();
  if (s_dbg_settle > 0) {
    // Let the halves fly apart, but hold the blade trail so the screenshot
    // shows both the cut and the swipe that caused it.
    game_step();
    if (--s_dbg_settle == 0) {
      s_dbg_freeze = FC_DEBUG_FREEZE_FRAMES;
    }
  } else if (s_dbg_freeze > 0) {
    s_dbg_freeze--;
  } else
#endif
  {
    game_step();
    blade_step();
  }

  // Drained here rather than played from the slice itself: touch events arrive
  // between frames and one stroke can cut several fruits, which would stack
  // several swishes on top of each other. Draining once a frame collapses them
  // into one, at a cost of at most a frame of latency.
  if (game_take_cut_events() > 0) {
    sound_play_slice();
  }

  layer_mark_dirty(s_canvas);
  s_timer = app_timer_register(FC_FRAME_MS, prv_timer_callback, NULL);
}

static void prv_draw_entities(GContext *ctx) {
  const Fruit *fruits = game_fruits();
  for (int i = 0; i < MAX_FRUITS; i++) {
    if (fruits[i].active) {
      draw_fruit(ctx, fruits[i].type,
                 GPoint(FP_INT(fruits[i].x), FP_INT(fruits[i].y)),
                 fruits[i].radius, fruits[i].angle);
    }
  }

  const Half *halves = game_halves();
  for (int i = 0; i < MAX_HALVES; i++) {
    if (halves[i].active) {
      draw_half(ctx, &halves[i]);
    }
  }

  // Juice last, so droplets read as spray in front of the pieces they came off.
  draw_juice(ctx);
}

static void prv_update_proc(Layer *layer, GContext *ctx) {
  const GRect bounds = layer_get_bounds(layer);

  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, bounds, 0, GCornerNone);

  switch (game_get_state()) {
    case STATE_TITLE:
      draw_title(ctx, bounds, s_touch_ok);
      break;
    case STATE_PLAYING:
      prv_draw_entities(ctx);
      blade_draw(ctx);
      draw_hud(ctx, bounds);
      break;
    case STATE_GAMEOVER:
      // The field stays visible behind the panel so the fatal moment is legible.
      prv_draw_entities(ctx);
      blade_draw(ctx);
      draw_gameover(ctx, bounds);
      break;
  }
}

// ---------------------------------------------------------------------------
// Window lifecycle
// ---------------------------------------------------------------------------

static void prv_window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  const GRect bounds = layer_get_bounds(root);

  s_canvas = layer_create(bounds);
  layer_set_update_proc(s_canvas, prv_update_proc);
  layer_add_child(root, s_canvas);

  game_init(bounds);
  game_set_state(STATE_TITLE);
  blade_reset();
}

static void prv_window_appear(Window *window) {
  // Subscribe here rather than in load: the touch sensor draws power the whole
  // time it is subscribed, so it should be off whenever the app is backgrounded.
  s_touch_ok = touch_service_is_enabled();
  if (s_touch_ok) {
    touch_service_subscribe(prv_touch_handler, NULL);
  } else {
    APP_LOG(APP_LOG_LEVEL_WARNING, "touch unavailable; buttons only");
  }
  s_timer = app_timer_register(FC_FRAME_MS, prv_timer_callback, NULL);
}

static void prv_window_disappear(Window *window) {
  if (s_touch_ok) {
    touch_service_unsubscribe();
  }
  sound_stop();
  if (s_timer) {
    app_timer_cancel(s_timer);
    s_timer = NULL;
  }
}

static void prv_window_unload(Window *window) {
  layer_destroy(s_canvas);
  s_canvas = NULL;
}

static void prv_init(void) {
  srand((unsigned int)time(NULL));
  sound_init();

  s_window = window_create();
  window_set_background_color(s_window, GColorBlack);
  window_set_click_config_provider(s_window, prv_click_config_provider);
  window_set_window_handlers(s_window, (WindowHandlers) {
    .load = prv_window_load,
    .appear = prv_window_appear,
    .disappear = prv_window_disappear,
    .unload = prv_window_unload,
  });
  window_stack_push(s_window, true);
}

static void prv_deinit(void) {
  window_destroy(s_window);
}

int main(void) {
  prv_init();
  app_event_loop();
  prv_deinit();
}
