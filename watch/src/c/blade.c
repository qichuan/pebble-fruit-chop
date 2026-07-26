#include "blade.h"
#include "game.h"

// Oldest point first, newest last. A plain array beats a ring buffer here: the
// draw and age passes both want chronological order, and 12 entries make the
// shift on overflow free.
static BladePoint s_points[MAX_BLADE_POINTS];
static int s_count;
static bool s_down;

void blade_reset(void) {
  s_count = 0;
  s_down = false;
}

static void prv_push(GPoint p) {
  if (s_count == MAX_BLADE_POINTS) {
    for (int i = 1; i < MAX_BLADE_POINTS; i++) {
      s_points[i - 1] = s_points[i];
    }
    s_count--;
  }
  s_points[s_count].pos = p;
  s_points[s_count].age = 0;
  s_count++;
}

void blade_feed(const TouchEvent *event) {
  const GPoint p = GPoint(event->x, event->y);

  switch (event->type) {
    case TouchEvent_Touchdown:
      s_count = 0;
      s_down = true;
      prv_push(p);
      break;

    case TouchEvent_PositionUpdate: {
      if (!s_down) {
        // A move without a touchdown is not a cut; still track it for the trail.
        prv_push(p);
        break;
      }
      if (s_count > 0) {
        const GPoint prev = s_points[s_count - 1].pos;
        const int32_t dx = p.x - prev.x;
        const int32_t dy = p.y - prev.y;
        // Speed gate, compared squared to avoid a sqrt. The lower bound stops a
        // slow drag harvesting the screen; the upper bound rejects the huge
        // jumps that appear when touch samples are coalesced or dropped, which
        // would otherwise slice everything along a diagonal.
        const int32_t d2 = dx * dx + dy * dy;
        if (d2 >= (FC_MIN_SLICE_PX * FC_MIN_SLICE_PX) &&
            d2 <= (FC_MAX_SLICE_PX * FC_MAX_SLICE_PX)) {
          game_slice_segment(prev, p);
        }
      }
      prv_push(p);
      break;
    }

    case TouchEvent_Liftoff:
      prv_push(p);
      s_down = false;
      break;
  }
}

void blade_step(void) {
  int drop = 0;
  for (int i = 0; i < s_count; i++) {
    if (++s_points[i].age > FC_BLADE_TTL) {
      drop++;
    }
  }
  if (drop > 0) {
    // Expired points are always the oldest, so compact from the front.
    for (int i = drop; i < s_count; i++) {
      s_points[i - drop] = s_points[i];
    }
    s_count -= drop;
  }
}

void blade_draw(GContext *ctx) {
  if (s_count < 2) {
    return;
  }

  graphics_context_set_antialiased(ctx, true);
  graphics_context_set_stroke_color(ctx, GColorWhite);

  // Taper the trail: oldest segment 1px, newest 5px. The SDK has no per-pixel
  // alpha, so width is what reads as a fading blade.
  for (int i = 0; i < s_count - 1; i++) {
    const uint8_t width = (uint8_t)(1 + ((4 * i) / (s_count - 1)));
    graphics_context_set_stroke_width(ctx, width);
    graphics_draw_line(ctx, s_points[i].pos, s_points[i + 1].pos);
  }

  graphics_context_set_stroke_width(ctx, 1);
}

bool blade_is_down(void) { return s_down; }
