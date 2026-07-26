#pragma once

#include <pebble.h>
#include "game.h"

void draw_fruit(GContext *ctx, FruitType type, GPoint c, int16_t r, int32_t angle);
void draw_half(GContext *ctx, const Half *h);
void draw_juice(GContext *ctx);
void draw_hud(GContext *ctx, GRect bounds);
void draw_title(GContext *ctx, GRect bounds, bool touch_ok);
void draw_gameover(GContext *ctx, GRect bounds);
