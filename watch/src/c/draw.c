#include "draw.h"
#include "fixed.h"

// Skin/rind colour, and the colour revealed on a cut face.
static GColor prv_skin(FruitType type) {
  switch (type) {
    case FRUIT_APPLE:      return GColorRed;
    // Not GColorOrange: through the display filter it renders within a few
    // units of GColorRed, making apples and oranges indistinguishable in play.
    case FRUIT_ORANGE:     return GColorChromeYellow;
    case FRUIT_WATERMELON: return GColorIslamicGreen;
    case FRUIT_DURIAN:     return GColorLimerick;
    default:               return GColorBlack;
  }
}

static GColor prv_flesh(FruitType type) {
  switch (type) {
    case FRUIT_APPLE:      return GColorPastelYellow;
    case FRUIT_ORANGE:     return GColorChromeYellow;
    case FRUIT_WATERMELON: return GColorRed;
    case FRUIT_DURIAN:     return GColorIcterine;
    default:               return GColorDarkGray;
  }
}

static GPoint prv_on_circle(GPoint c, int16_t r, int32_t angle) {
  return GPoint(c.x + (int16_t)((sin_lookup(angle) * r) / TRIG_MAX_RATIO),
                c.y - (int16_t)((cos_lookup(angle) * r) / TRIG_MAX_RATIO));
}

static void prv_draw_bomb(GContext *ctx, GPoint c, int16_t r) {
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_circle(ctx, c, r);
  graphics_context_set_stroke_color(ctx, GColorLightGray);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_draw_circle(ctx, c, r);

  // Highlight glint.
  graphics_context_set_fill_color(ctx, GColorLightGray);
  graphics_fill_circle(ctx, GPoint(c.x - r / 3, c.y - r / 3), 2);

  // Fuse, with a tip that flickers between frames so the bomb reads as live.
  const GPoint fuse_base = GPoint(c.x + r / 2, c.y - r + 1);
  const GPoint fuse_tip = GPoint(fuse_base.x + 3, fuse_base.y - 5);
  graphics_context_set_stroke_color(ctx, GColorWindsorTan);
  graphics_context_set_stroke_width(ctx, 2);
  graphics_draw_line(ctx, fuse_base, fuse_tip);

  graphics_context_set_fill_color(ctx,
      (time_ms(NULL, NULL) / 120) % 2 ? GColorYellow : GColorRed);
  graphics_fill_circle(ctx, fuse_tip, 2);
  graphics_context_set_stroke_width(ctx, 1);
}

void draw_fruit(GContext *ctx, FruitType type, GPoint c, int16_t r, int32_t angle) {
  if (type == FRUIT_BOMB) {
    prv_draw_bomb(ctx, c, r);
    return;
  }

  graphics_context_set_antialiased(ctx, true);

  // Durian spikes radiate past the body, so they go down first.
  if (type == FRUIT_DURIAN) {
    graphics_context_set_stroke_color(ctx, GColorLimerick);
    graphics_context_set_stroke_width(ctx, 2);
    for (int i = 0; i < 10; i++) {
      const int32_t a = angle + (TRIG_MAX_ANGLE * i) / 10;
      graphics_draw_line(ctx, prv_on_circle(c, r - 2, a), prv_on_circle(c, r + 4, a));
    }
    graphics_context_set_stroke_width(ctx, 1);
  }

  graphics_context_set_fill_color(ctx, prv_skin(type));
  graphics_fill_circle(ctx, c, r);

  switch (type) {
    case FRUIT_WATERMELON:
      // Green rind with red flesh showing through.
      graphics_context_set_fill_color(ctx, GColorRed);
      graphics_fill_circle(ctx, c, r - 3);
      graphics_context_set_fill_color(ctx, GColorBlack);
      for (int i = 0; i < 3; i++) {
        const int32_t a = angle + (TRIG_MAX_ANGLE * i) / 3;
        graphics_fill_circle(ctx, prv_on_circle(c, r / 2, a), 1);
      }
      break;

    case FRUIT_ORANGE:
      // Segment lines from the centre outward.
      graphics_context_set_stroke_color(ctx, GColorOrange);
      for (int i = 0; i < 6; i++) {
        const int32_t a = angle + (TRIG_MAX_ANGLE * i) / 6;
        graphics_draw_line(ctx, c, prv_on_circle(c, r - 2, a));
      }
      break;

    case FRUIT_APPLE: {
      // Stem plus a small specular highlight.
      const GPoint top = prv_on_circle(c, r - 1, angle);
      graphics_context_set_stroke_color(ctx, GColorWindsorTan);
      graphics_context_set_stroke_width(ctx, 2);
      graphics_draw_line(ctx, top, prv_on_circle(c, r + 4, angle));
      graphics_context_set_stroke_width(ctx, 1);
      graphics_context_set_fill_color(ctx, GColorWhite);
      graphics_fill_circle(ctx, prv_on_circle(c, r / 2, angle - TRIG_MAX_ANGLE / 8), 2);
      break;
    }

    case FRUIT_DURIAN:
      graphics_context_set_stroke_color(ctx, GColorArmyGreen);
      graphics_draw_circle(ctx, c, r - 3);
      break;

    default:
      break;
  }
}

void draw_half(GContext *ctx, const Half *h) {
  const GPoint c = GPoint(FP_INT(h->x), FP_INT(h->y));
  const int16_t r = h->radius;
  const GRect rect = GRect(c.x - r, c.y - r, 2 * r, 2 * r);

  // A half is a 180-degree pie slice. game.c bakes the cut direction into
  // h->angle at split time, so the flat face lines up with the actual swipe.
  const int32_t a0 = h->angle;
  const int32_t a1 = a0 + (TRIG_MAX_ANGLE / 2);

  graphics_context_set_antialiased(ctx, true);

  if (h->type == FRUIT_BOMB) {
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_radial(ctx, rect, GOvalScaleModeFitCircle, r, a0, a1);
    return;
  }

  graphics_context_set_fill_color(ctx, prv_skin(h->type));
  graphics_fill_radial(ctx, rect, GOvalScaleModeFitCircle, r, a0, a1);

  // Exposed cut face, inset so the skin still shows as a rind.
  graphics_context_set_fill_color(ctx, prv_flesh(h->type));
  graphics_fill_radial(ctx, grect_inset(rect, GEdgeInsets(3)),
                       GOvalScaleModeFitCircle, r, a0, a1);
}

void draw_hud(GContext *ctx, GRect bounds) {
  char score[8];
  snprintf(score, sizeof(score), "%d", game_get_score());

  GFont font = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  graphics_context_set_text_color(ctx, GColorWhite);

  // On the round screen the corners are clipped, so the HUD stacks down the
  // centre instead of spreading into them.
  const int16_t inset = PBL_IF_ROUND_ELSE(28, 4);
  const int16_t pip_r = 3;
  const int16_t pip_gap = 10;

#if defined(PBL_ROUND)
  graphics_draw_text(ctx, score, font, GRect(0, inset, bounds.size.w, 22),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  const int16_t pip_y = inset + 26;
  const int16_t pip_x0 = bounds.size.w / 2 - pip_gap;
#else
  graphics_draw_text(ctx, score, font, GRect(inset, inset - 2, 60, 22),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentLeft, NULL);
  const int16_t pip_y = inset + 8;
  const int16_t pip_x0 = bounds.size.w - inset - (2 * pip_gap) - pip_r;
#endif

  for (int i = 0; i < FC_START_LIVES; i++) {
    const GPoint p = GPoint(pip_x0 + i * pip_gap, pip_y);
    if (i < game_get_lives()) {
      graphics_context_set_fill_color(ctx, GColorRed);
      graphics_fill_circle(ctx, p, pip_r);
    } else {
      graphics_context_set_stroke_color(ctx, GColorDarkGray);
      graphics_draw_circle(ctx, p, pip_r);
    }
  }
}

// Shared centred-stack layout for the title and game-over screens.
static void prv_draw_centred(GContext *ctx, GRect bounds, const char *title,
                             const char *line2, const char *line3) {
  GFont big = fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD);
  GFont small = fonts_get_system_font(FONT_KEY_GOTHIC_18);

  graphics_context_set_text_color(ctx, GColorWhite);
  const int16_t y = bounds.size.h / 2 - 46;

  graphics_draw_text(ctx, title, big, GRect(0, y, bounds.size.w, 34),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  graphics_draw_text(ctx, line2, small, GRect(0, y + 40, bounds.size.w, 24),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  graphics_draw_text(ctx, line3, small, GRect(0, y + 66, bounds.size.w, 24),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
}

void draw_title(GContext *ctx, GRect bounds, bool touch_ok) {
  // A row of fruit above the title, as a preview of what the game looks like.
  const int16_t y = bounds.size.h / 2 - 66;
  const int16_t step = bounds.size.w / 5;
  for (int i = 0; i < 4; i++) {
    draw_fruit(ctx, (FruitType)i, GPoint(step * (i + 1), y), 11, 0);
  }

  prv_draw_centred(ctx, bounds, "FRUIT CHOP",
                   touch_ok ? "Swipe to chop" : "Touch unavailable",
                   "SELECT to start");
}

void draw_gameover(GContext *ctx, GRect bounds) {
  char score[24];
  snprintf(score, sizeof(score), "Score %d", game_get_score());
  prv_draw_centred(ctx, bounds, "GAME OVER", score, "SELECT to retry");
}
