#include "draw.h"
#include "fixed.h"
#include "sound.h"

static inline GColor prv_col(uint8_t argb) {
  return (GColor){ .argb = argb };
}

static GPoint prv_on_circle(GPoint c, int16_t r, int32_t angle) {
  return GPoint(c.x + (int16_t)((sin_lookup(angle) * r) / TRIG_MAX_RATIO),
                c.y - (int16_t)((cos_lookup(angle) * r) / TRIG_MAX_RATIO));
}

// Rotates a body-local offset, given in units of r/32, into screen space. Same
// convention as prv_on_circle: 0 is straight up, positive is clockwise, so
// silhouettes and the details painted on them stay locked together as a fruit
// spins.
static GPoint prv_local(GPoint c, int16_t r, int32_t angle, int dx, int dy) {
  const int32_t s = sin_lookup(angle);
  const int32_t k = cos_lookup(angle);
  const int32_t x = ((int32_t)dx * r) / 32;
  const int32_t y = ((int32_t)dy * r) / 32;
  return GPoint((int16_t)(c.x + ((x * k) - (y * s)) / TRIG_MAX_RATIO),
                (int16_t)(c.y + ((x * s) + (y * k)) / TRIG_MAX_RATIO));
}

// ---------------------------------------------------------------------------
// Body shapes
//
// Polygon fruits share one file-static GPath over a static point array: GPath
// is a public struct, so this avoids gpath_create's malloc. The points are
// written already rotated and translated by prv_local, leaving the path's own
// rotation and offset at zero -- one convention for silhouette and detail alike.
// ---------------------------------------------------------------------------

// One file-static GPath, pointed at whichever buffer is being filled. GPath is
// a public struct with public fields, so this avoids gpath_create's malloc.
// Points are written already rotated and translated by prv_local, leaving the
// path's own rotation and offset at zero -- one convention throughout.
static GPath s_path = { .num_points = 0, .points = NULL };

static GPoint s_poly[FC_MAX_OUTLINE_POINTS];
static GPoint s_clip[FC_MAX_CLIP_POINTS];

static void prv_fill_points(GContext *ctx, GPoint *pts, int n) {
  if (n < 3) {
    return;
  }
  s_path.points = pts;
  s_path.num_points = n;
  gpath_draw_filled(ctx, &s_path);
}

// Templates are int8_t x,y pairs in units of r/32, authored upright and wound
// in a single direction. Every fruit has one, including the round ones: it is
// the silhouette draw_half clips against the blade, so a shape here is what
// stops a cut banana coming apart as two half-circles.
static const int8_t s_banana[] = {
  -30, 10, -22, -6, -8, -16, 8, -16, 22, -6, 30, 10,
   26, 13,  14,  3,  0,  0, -14, 3, -26, 13,
};
static const int8_t s_strawberry[] = {
    0, -30,  16, -26,  26, -12,  24,  6,  12, 22,
    0,  30, -12,  22, -24,   6, -26, -12, -16, -26,
};
static const int8_t s_pineapple[] = {
    0, -26,  18, -22,  22, -8,  22, 10,  14, 24,
    0,  28, -14,  24, -22, 10, -22, -8, -18, -22,
};
// A 16-gon: at the radii the game uses, its facets are under a pixel, so a cut
// circle still reads as a circle.
static const int8_t s_round[] = {
    0, -32,  12, -30,  23, -23,  30, -12,  32,  0,  30, 12,  23, 23,  12, 30,
    0,  32, -12,  30, -23,  23, -30,  12, -32,  0, -30, -12, -23, -23, -12, -30,
};
// Matches prv_fill_lobed's two equal circles: an ellipse 0.69r wide by r tall.
static const int8_t s_oval[] = {
    0, -32,  11, -28,  19, -16,  22, 0,  19, 16,  11, 28,
    0,  32, -11,  28, -19,  16, -22, 0, -19, -16, -11, -28,
};
static const int8_t s_pear[] = {
    0, -28,  14, -22,  18, -8,  18,  6,  25, 20,  18, 33,
    0,  35, -18,  33, -25,  20, -18,  6, -18, -8, -14, -22,
};

// The silhouette for any fruit, as a template and a vertex count.
static int prv_outline(FruitType type, const int8_t **tmpl) {
  switch (fruit_profile(type)->shape) {
    case FRUIT_SHAPE_LOBED:
      if (type == FRUIT_PEAR) {
        *tmpl = s_pear;
        return ARRAY_LENGTH(s_pear) / 2;
      }
      *tmpl = s_oval;
      return ARRAY_LENGTH(s_oval) / 2;

    case FRUIT_SHAPE_PATH:
      switch (type) {
        case FRUIT_BANANA:
          *tmpl = s_banana;
          return ARRAY_LENGTH(s_banana) / 2;
        case FRUIT_STRAWBERRY:
          *tmpl = s_strawberry;
          return ARRAY_LENGTH(s_strawberry) / 2;
        default:
          *tmpl = s_pineapple;
          return ARRAY_LENGTH(s_pineapple) / 2;
      }

    default:
      *tmpl = s_round;
      return ARRAY_LENGTH(s_round) / 2;
  }
}

// Scales a template to radius r, rotates it by `angle` about c, and writes it
// to s_poly. Passing a reduced r is how the inset flesh polygon is built.
static int prv_build(const int8_t *tmpl, int n, GPoint c, int16_t r, int32_t angle) {
  if (n > FC_MAX_OUTLINE_POINTS) {
    n = FC_MAX_OUTLINE_POINTS;
  }
  for (int i = 0; i < n; i++) {
    s_poly[i] = prv_local(c, r, angle, tmpl[2 * i], tmpl[2 * i + 1]);
  }
  return n;
}

static void prv_fill_path(GContext *ctx, const int8_t *tmpl, int n,
                          GPoint c, int16_t r, int32_t angle) {
  prv_fill_points(ctx, s_poly, prv_build(tmpl, n, c, r, angle));
}

// Sutherland-Hodgman clip of s_poly against the half-plane through `c` whose
// inward normal points along `normal_angle`, result in s_clip. This is what
// gives each half the shape the blade actually left behind: the curved side is
// the fruit's own outline, and the straight side is the cut.
//
// The normal is scaled down from the trig lookup so the dot products stay small
// enough for the intersection's fixed-point divide to have headroom in int32.
static int prv_clip_halfplane(int n, GPoint c, int32_t normal_angle) {
  const int32_t nx = sin_lookup(normal_angle) >> 8;
  const int32_t ny = -(cos_lookup(normal_angle) >> 8);

  int m = 0;
  for (int i = 0; i < n && m < FC_MAX_CLIP_POINTS; i++) {
    const GPoint p0 = s_poly[i];
    const GPoint p1 = s_poly[(i + 1) % n];
    const int32_t s0 = (p0.x - c.x) * nx + (p0.y - c.y) * ny;
    const int32_t s1 = (p1.x - c.x) * nx + (p1.y - c.y) * ny;

    if (s0 >= 0) {
      s_clip[m++] = p0;
    }
    // Sign change means the edge crosses the blade: emit the crossing point.
    if ((s0 >= 0) != (s1 >= 0) && m < FC_MAX_CLIP_POINTS) {
      const int32_t t = (s0 * FP_ONE) / (s0 - s1);
      s_clip[m++] = GPoint((int16_t)(p0.x + (((p1.x - p0.x) * t) >> FP_SHIFT)),
                           (int16_t)(p0.y + (((p1.y - p0.y) * t) >> FP_SHIFT)));
    }
  }
  return m;
}

// Two circles on the spin axis. Equal lobes read as an oval (lemon, mango); a
// small lobe over a large one reads as a pear.
static void prv_fill_lobed(GContext *ctx, GPoint c, int16_t r, int32_t angle,
                           int top_pct, int bot_pct, int sep_pct) {
  const int16_t sep = (r * sep_pct) / 100;
  graphics_fill_circle(ctx, prv_on_circle(c, sep, angle), (r * top_pct) / 100);
  graphics_fill_circle(ctx, prv_on_circle(c, -sep, angle), (r * bot_pct) / 100);
}

static void prv_fill_body(GContext *ctx, FruitType type, const FruitProfile *p,
                          GPoint c, int16_t r, int32_t angle) {
  graphics_context_set_fill_color(ctx, prv_col(p->skin));

  switch (p->shape) {
    case FRUIT_SHAPE_LOBED:
      if (type == FRUIT_PEAR) {
        prv_fill_lobed(ctx, c, r, angle, 55, 78, 32);
      } else {
        prv_fill_lobed(ctx, c, r, angle, 68, 68, 32);
      }
      break;

    case FRUIT_SHAPE_PATH:
      switch (type) {
        case FRUIT_BANANA:
          prv_fill_path(ctx, s_banana, ARRAY_LENGTH(s_banana) / 2, c, r, angle);
          break;
        case FRUIT_STRAWBERRY:
          prv_fill_path(ctx, s_strawberry, ARRAY_LENGTH(s_strawberry) / 2, c, r, angle);
          break;
        default:
          prv_fill_path(ctx, s_pineapple, ARRAY_LENGTH(s_pineapple) / 2, c, r, angle);
          break;
      }
      break;

    default:
      graphics_fill_circle(ctx, c, r);
      break;
  }
}

// ---------------------------------------------------------------------------
// Per-fruit detail
//
// Colour alone cannot separate sixteen fruits through the display filter, so
// every type carries a mark as well: a stem, a crown, seeds, a crease. The
// pairs that sit closest in hue (peach/mango, banana/lemon, lime/green apple)
// are the ones separated by silhouette rather than by these.
// ---------------------------------------------------------------------------

static void prv_dot(GContext *ctx, GPoint p, int16_t r) {
  graphics_fill_circle(ctx, p, r < 1 ? 1 : r);
}

// Flesh core with a paler middle and a ring of seeds: the cross-section read
// that makes kiwi and passion fruit legible at this size.
static void prv_draw_core(GContext *ctx, GPoint c, int16_t r, int32_t angle,
                          GColor flesh, GColor middle, GColor seed, int seeds) {
  graphics_context_set_fill_color(ctx, flesh);
  graphics_fill_circle(ctx, c, (r * 7) / 10);
  graphics_context_set_fill_color(ctx, middle);
  graphics_fill_circle(ctx, c, r / 4);
  graphics_context_set_fill_color(ctx, seed);
  for (int i = 0; i < seeds; i++) {
    prv_dot(ctx, prv_on_circle(c, (r * 9) / 20, angle + (TRIG_MAX_ANGLE * i) / seeds),
            r / 9);
  }
}

static void prv_stem(GContext *ctx, GPoint c, int16_t r, int32_t angle, GColor col) {
  graphics_context_set_stroke_color(ctx, col);
  graphics_context_set_stroke_width(ctx, 2);
  graphics_draw_line(ctx, prv_on_circle(c, r - 1, angle),
                     prv_on_circle(c, r + r / 3, angle));
  graphics_context_set_stroke_width(ctx, 1);
}

static void prv_highlight(GContext *ctx, GPoint c, int16_t r, int32_t angle) {
  graphics_context_set_fill_color(ctx, GColorWhite);
  prv_dot(ctx, prv_on_circle(c, r / 2, angle - TRIG_MAX_ANGLE / 8), r / 6);
}

static void prv_draw_detail(GContext *ctx, FruitType type, const FruitProfile *p,
                            GPoint c, int16_t r, int32_t angle) {
  switch (type) {
    case FRUIT_WATERMELON: {
      // Green rind with the red flesh showing through, plus pips.
      int16_t rind = r / 5;
      if (rind < 3) {
        rind = 3;
      }
      graphics_context_set_fill_color(ctx, prv_col(p->flesh));
      graphics_fill_circle(ctx, c, r - rind);
      graphics_context_set_fill_color(ctx, GColorBlack);
      for (int i = 0; i < 4; i++) {
        prv_dot(ctx, prv_on_circle(c, r / 2, angle + (TRIG_MAX_ANGLE * i) / 4), r / 8);
      }
      break;
    }

    case FRUIT_MANGO:
      // The red cheek. It has to be big to survive the filter, which is also
      // what stops the mango reading as another lemon.
      graphics_context_set_fill_color(ctx, GColorFolly);
      graphics_fill_circle(ctx, prv_on_circle(c, (r * 2) / 5, angle), (r * 2) / 5);
      prv_stem(ctx, c, r, angle, GColorArmyGreen);
      break;

    case FRUIT_PINEAPPLE: {
      // Crown first, then the diamond hatching over the body.
      graphics_context_set_stroke_color(ctx, GColorIslamicGreen);
      graphics_context_set_stroke_width(ctx, 2);
      for (int i = -1; i <= 1; i++) {
        graphics_draw_line(ctx, prv_on_circle(c, (r * 3) / 4, angle),
                           prv_local(c, r, angle, i * 12, -42));
      }
      graphics_context_set_stroke_width(ctx, 1);
      graphics_context_set_stroke_color(ctx, GColorArmyGreen);
      for (int s = 0; s < 2; s++) {
        const int32_t dir = angle + (s ? TRIG_MAX_ANGLE / 8 : -TRIG_MAX_ANGLE / 8);
        for (int i = -1; i <= 1; i++) {
          const GPoint q = prv_on_circle(c, (i * r) / 2, dir + TRIG_MAX_ANGLE / 4);
          graphics_draw_line(ctx, prv_on_circle(q, (r * 3) / 5, dir),
                             prv_on_circle(q, (r * 3) / 5, dir + TRIG_MAX_ANGLE / 2));
        }
      }
      break;
    }

    case FRUIT_COCONUT:
      // The three eyes, clustered at one end as on the real husk.
      graphics_context_set_fill_color(ctx, GColorBlack);
      for (int i = -1; i <= 1; i++) {
        prv_dot(ctx, prv_on_circle(c, r / 2, angle + i * (TRIG_MAX_ANGLE / 12)), r / 7);
      }
      graphics_context_set_stroke_color(ctx, GColorBrass);
      graphics_draw_circle(ctx, c, r - 2);
      break;

    case FRUIT_STRAWBERRY:
      // Calyx over the shoulders, seeds down the body.
      graphics_context_set_stroke_color(ctx, GColorIslamicGreen);
      graphics_context_set_stroke_width(ctx, 2);
      for (int i = -1; i <= 1; i++) {
        graphics_draw_line(ctx, prv_local(c, r, angle, 0, -24),
                           prv_local(c, r, angle, i * 22, -34));
      }
      graphics_context_set_stroke_width(ctx, 1);
      graphics_context_set_fill_color(ctx, GColorPastelYellow);
      for (int i = 0; i < 5; i++) {
        static const int8_t seeds[] = { -12, -8, 10, -10, 0, 2, -8, 12, 10, 8 };
        prv_dot(ctx, prv_local(c, r, angle, seeds[2 * i], seeds[2 * i + 1]), r / 12);
      }
      break;

    case FRUIT_GREEN_APPLE:
    case FRUIT_RED_APPLE:
      // Stem and leaf together: on the green apple the leaf is most of what
      // separates it from a lime at this size.
      prv_stem(ctx, c, r, angle, GColorWindsorTan);
      graphics_context_set_fill_color(ctx, GColorIslamicGreen);
      prv_dot(ctx, prv_local(c, r, angle, 13, -34), r / 5);
      prv_highlight(ctx, c, r, angle);
      break;

    case FRUIT_KIWI:
      prv_draw_core(ctx, c, r, angle, prv_col(p->flesh), GColorWhite, GColorBlack, 8);
      break;

    case FRUIT_BANANA:
      // Brown tips at both ends of the crescent.
      graphics_context_set_fill_color(ctx, GColorWindsorTan);
      prv_dot(ctx, prv_local(c, r, angle, -28, 11), r / 8);
      prv_dot(ctx, prv_local(c, r, angle, 28, 11), r / 8);
      break;

    case FRUIT_LEMON:
      // A nub at each end of the oval, which is what separates a lemon from a
      // lime at a glance.
      graphics_context_set_fill_color(ctx, prv_col(p->skin));
      prv_dot(ctx, prv_on_circle(c, r, angle), r / 6);
      prv_dot(ctx, prv_on_circle(c, -r, angle), r / 6);
      graphics_context_set_stroke_color(ctx, GColorChromeYellow);
      graphics_draw_circle(ctx, c, r / 2);
      break;

    case FRUIT_LIME:
      graphics_context_set_stroke_color(ctx, GColorKellyGreen);
      graphics_context_set_stroke_width(ctx, 2);
      graphics_draw_line(ctx, prv_on_circle(c, r - 1, angle + TRIG_MAX_ANGLE / 4),
                         prv_on_circle(c, r - 1, angle - TRIG_MAX_ANGLE / 4));
      graphics_context_set_stroke_width(ctx, 1);
      graphics_context_set_fill_color(ctx, GColorMintGreen);
      prv_dot(ctx, prv_on_circle(c, r / 2, angle - TRIG_MAX_ANGLE / 8), r / 7);
      break;

    case FRUIT_ORANGE:
      // Segment lines from the centre outward, plus a green calyx.
      graphics_context_set_stroke_color(ctx, GColorOrange);
      for (int i = 0; i < 6; i++) {
        graphics_draw_line(ctx, c,
                           prv_on_circle(c, r - 2, angle + (TRIG_MAX_ANGLE * i) / 6));
      }
      graphics_context_set_fill_color(ctx, GColorIslamicGreen);
      prv_dot(ctx, prv_on_circle(c, r - r / 4, angle), r / 7);
      break;

    case FRUIT_PLUM:
      // The crease that runs pole to pole.
      graphics_context_set_stroke_color(ctx, GColorImperialPurple);
      graphics_context_set_stroke_width(ctx, 2);
      graphics_draw_line(ctx, prv_on_circle(c, r - 1, angle),
                         prv_on_circle(c, -(r - 1), angle));
      graphics_context_set_stroke_width(ctx, 1);
      graphics_context_set_fill_color(ctx, GColorRichBrilliantLavender);
      prv_dot(ctx, prv_on_circle(c, r / 2, angle + TRIG_MAX_ANGLE / 5), r / 7);
      break;

    case FRUIT_PEAR:
      prv_stem(ctx, c, r, angle, GColorWindsorTan);
      graphics_context_set_fill_color(ctx, GColorLimerick);
      prv_dot(ctx, prv_local(c, r, angle, -14, 18), r / 12);
      prv_dot(ctx, prv_local(c, r, angle, 12, 6), r / 12);
      break;

    case FRUIT_PASSION:
      prv_draw_core(ctx, c, r, angle, prv_col(p->flesh), GColorIcterine, GColorBlack, 7);
      graphics_context_set_stroke_color(ctx, GColorBulgarianRose);
      graphics_draw_circle(ctx, c, r - 1);
      break;

    case FRUIT_PEACH:
      // Crease plus a leaf, so it reads apart from the mango.
      graphics_context_set_stroke_color(ctx, GColorRoseVale);
      graphics_context_set_stroke_width(ctx, 2);
      graphics_draw_line(ctx, prv_on_circle(c, r - 1, angle),
                         prv_on_circle(c, -(r - 1), angle));
      graphics_context_set_stroke_width(ctx, 1);
      graphics_context_set_fill_color(ctx, GColorIslamicGreen);
      prv_dot(ctx, prv_local(c, r, angle, 14, -30), r / 5);
      break;

    default:
      break;
  }
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

  const FruitProfile *p = fruit_profile(type);
  prv_fill_body(ctx, type, p, c, r, angle);
  prv_draw_detail(ctx, type, p, c, r, angle);
}

void draw_half(GContext *ctx, const Half *h) {
  const GPoint c = GPoint(FP_INT(h->x), FP_INT(h->y));
  const int16_t r = h->radius;

  // A half is the fruit's own silhouette with everything on the far side of the
  // blade clipped away, so a cut banana leaves two pieces of banana rather than
  // two half-circles. h->angle is the cut direction game.c baked in at split
  // time; the inward normal is a quarter turn from it, and h->body_angle says
  // which way the silhouette itself was facing.
  const int32_t a0 = h->angle;
  const int32_t normal = a0 + (TRIG_MAX_ANGLE / 4);

  graphics_context_set_antialiased(ctx, true);

  if (h->type == FRUIT_BOMB) {
    // The bomb is a plain circle and never spins into anything else, so the
    // sector primitive still draws it exactly right.
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_fill_radial(ctx, GRect(c.x - r, c.y - r, 2 * r, 2 * r),
                         GOvalScaleModeFitCircle, r, a0, a0 + (TRIG_MAX_ANGLE / 2));
    return;
  }

  const FruitProfile *p = fruit_profile(h->type);
  const int8_t *tmpl;
  const int n = prv_outline(h->type, &tmpl);

  // Rind thickness tracks the radius: a flat 3px rind vanishes on larger fruit.
  int16_t rind = r / 6;
  if (rind < 3) {
    rind = 3;
  } else if (rind > 5) {
    rind = 5;
  }

  graphics_context_set_fill_color(ctx, prv_col(p->skin));
  prv_build(tmpl, n, c, r, h->body_angle);
  prv_fill_points(ctx, s_clip, prv_clip_halfplane(n, c, normal));

  // The flesh is the same outline shrunk by the rind, clipped on the same line
  // rather than an inset one. That is what leaves a rind around the curved edge
  // while the flat cut face stays bare flesh, as a real cut does.
  graphics_context_set_fill_color(ctx, prv_col(p->flesh));
  prv_build(tmpl, n, c, r - rind, h->body_angle);
  prv_fill_points(ctx, s_clip, prv_clip_halfplane(n, c, normal));

  // Detail on the cut face, for the fruits whose insides are the recognisable
  // part. The mid-angle is where the exposed face has the most room. Radii stay
  // well inside r so the marks cannot poke out of a narrow silhouette.
  const int32_t mid = normal;
  switch (h->type) {
    case FRUIT_WATERMELON:
      graphics_context_set_fill_color(ctx, GColorBlack);
      for (int i = -1; i <= 1; i++) {
        prv_dot(ctx, prv_on_circle(c, (r * 3) / 5, mid + i * (TRIG_MAX_ANGLE / 8)), r / 9);
      }
      break;

    case FRUIT_ORANGE:
    case FRUIT_LEMON:
    case FRUIT_LIME:
      // Orange gets white pith lines: GColorOrange over the Rajah flesh is too
      // close in value to survive the filter.
      graphics_context_set_stroke_color(ctx,
          h->type == FRUIT_LIME ? GColorKellyGreen
                                : (h->type == FRUIT_LEMON ? GColorChromeYellow
                                                          : GColorWhite));
      for (int i = -1; i <= 1; i++) {
        graphics_draw_line(ctx, c,
                           prv_on_circle(c, (r * 3) / 5, mid + i * (TRIG_MAX_ANGLE / 6)));
      }
      break;

    case FRUIT_PEACH:
    case FRUIT_PLUM:
    case FRUIT_MANGO:
      // The stone: the same outline again at half size, clipped on the same
      // line, so it sits in the cut face as a halved stone would.
      graphics_context_set_fill_color(ctx, GColorBulgarianRose);
      prv_build(tmpl, n, c, r / 2, h->body_angle);
      prv_fill_points(ctx, s_clip, prv_clip_halfplane(n, c, normal));
      break;

    case FRUIT_KIWI:
    case FRUIT_PASSION:
      graphics_context_set_fill_color(ctx,
          h->type == FRUIT_KIWI ? GColorWhite : GColorBlack);
      for (int i = -1; i <= 1; i++) {
        prv_dot(ctx, prv_on_circle(c, r / 2, mid + i * (TRIG_MAX_ANGLE / 8)), r / 9);
      }
      break;

    default:
      break;
  }
}

void draw_juice(GContext *ctx) {
  const Juice *juice = game_juice();
  graphics_context_set_antialiased(ctx, true);

  for (int i = 0; i < MAX_JUICE; i++) {
    const Juice *j = &juice[i];
    if (!j->active) {
      continue;
    }
    // Droplets shrink as they age, so the splash thins out instead of blinking
    // off at full size.
    int16_t r = (FC_JUICE_RADIUS_MAX * j->ttl) / (j->ttl0 ? j->ttl0 : 1);
    if (r < 1) {
      r = 1;
    }
    graphics_context_set_fill_color(ctx, prv_col(j->colour));
    graphics_fill_circle(ctx, GPoint(FP_INT(j->x), FP_INT(j->y)), r);
  }
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

// A small solid triangle, used for the title screen's difficulty arrows.
static void prv_tri(GContext *ctx, GPoint c, int16_t size, bool up) {
  const int16_t dy = up ? -size : size;
  GPoint pts[3] = {
    GPoint(c.x, c.y + dy),
    GPoint(c.x - size, c.y - dy),
    GPoint(c.x + size, c.y - dy),
  };
  prv_fill_points(ctx, pts, 3);
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
  // A row of fruit above the title, as a preview of what the game looks like:
  // one of each silhouette rather than the first four of the enum.
  static const FruitType preview[] = {
    FRUIT_WATERMELON, FRUIT_ORANGE, FRUIT_STRAWBERRY, FRUIT_BANANA,
  };
  GFont big = fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD);
  GFont bold = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD);
  GFont small = fonts_get_system_font(FONT_KEY_GOTHIC_18);

  // Five stacked rows. The row heights are named so the block's total height is
  // summed rather than guessed, and the stack starts at whatever y centres that
  // total -- add or resize a row and it stays centred on both screens by itself.
  const int16_t row_preview = 30;
  const int16_t row_title = 36;
  const int16_t row_diff = 26;
  const int16_t row_sound = 26;
  const int16_t row_best = 24;
  const int16_t row_hint = 24;
  const int16_t stack_h =
      row_preview + row_title + row_diff + row_sound + row_best + row_hint;

  const int16_t step = bounds.size.w / 5;
  int16_t y = (bounds.size.h - stack_h) / 2;

  for (unsigned i = 0; i < ARRAY_LENGTH(preview); i++) {
    draw_fruit(ctx, preview[i], GPoint(step * (i + 1), y + 12), 12, 0);
  }
  y += row_preview;

  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, "FRUIT CHOP", big, GRect(0, y, bounds.size.w, row_title),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  y += row_title;

  // Difficulty on one row and sound on the next, each marked with the triangle
  // of the button that changes it, both at the same x so the pair reads as a
  // column matching how UP sits above DOWN on the case. The arrows are drawn
  // rather than typed because the Gothic system fonts have no arrow glyphs --
  // they come out as tofu boxes.
  const int16_t arrow_x = bounds.size.w / 2 - 58;

  graphics_context_set_text_color(ctx, GColorYellow);
  graphics_draw_text(ctx, game_difficulty_name(game_get_difficulty()), bold,
                     GRect(0, y, bounds.size.w, row_diff),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  graphics_context_set_fill_color(ctx, GColorYellow);
  prv_tri(ctx, GPoint(arrow_x, y + 13), 6, true);
  y += row_diff;

  // Greyed when off, so the state reads before the word does.
  const bool sound_on = sound_is_enabled();
  const GColor sound_col = sound_on ? GColorYellow : GColorDarkGray;
  graphics_context_set_text_color(ctx, sound_col);
  graphics_draw_text(ctx, sound_on ? "SOUND ON" : "SOUND OFF", bold,
                     GRect(0, y, bounds.size.w, row_sound),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  graphics_context_set_fill_color(ctx, sound_col);
  prv_tri(ctx, GPoint(arrow_x, y + 13), 6, false);
  y += row_sound;

  char best[24];
  snprintf(best, sizeof(best), "Best %d", game_get_high_score());
  graphics_context_set_text_color(ctx, GColorLightGray);
  graphics_draw_text(ctx, best, small, GRect(0, y, bounds.size.w, row_best),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
  y += row_best;

  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, touch_ok ? "SELECT to start" : "Touch unavailable",
                     small, GRect(0, y, bounds.size.w, row_hint),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
}

void draw_gameover(GContext *ctx, GRect bounds) {
  char score[24];
  snprintf(score, sizeof(score), "Score %d", game_get_score());

  // Calling out a new record is the payoff for keeping one, so it takes the
  // line that would otherwise just repeat the stored best.
  char best[24];
  const bool is_record = (game_get_score() >= game_get_high_score() &&
                          game_get_score() > 0);
  if (is_record) {
    snprintf(best, sizeof(best), "NEW BEST!");
  } else {
    snprintf(best, sizeof(best), "Best %d", game_get_high_score());
  }

  // The field stays visible behind the panel so the fatal moment is legible,
  // but at the current fruit size a watermelon fills most of the screen and the
  // text was disappearing into it. A solid backing plate keeps both readable.
  graphics_context_set_fill_color(ctx, GColorBlack);
  graphics_fill_rect(ctx, GRect(0, bounds.size.h / 2 - 52, bounds.size.w, 124),
                     6, GCornersAll);

  prv_draw_centred(ctx, bounds, "GAME OVER", score, best);

  GFont small = fonts_get_system_font(FONT_KEY_GOTHIC_18);
  graphics_context_set_text_color(ctx, GColorWhite);
  graphics_draw_text(ctx, "SELECT to retry", small,
                     GRect(0, bounds.size.h / 2 + 46, bounds.size.w, 24),
                     GTextOverflowModeTrailingEllipsis, GTextAlignmentCenter, NULL);
}
