#pragma once

#include <pebble.h>
#include "fc_config.h"

typedef struct {
  GPoint pos;
  uint8_t age;   // frames since recorded
} BladePoint;

void blade_reset(void);

// The single entry point for touch input. Real TouchService events and the
// debug swipe harness both come through here, so they exercise identical code.
void blade_feed(const TouchEvent *event);

// Ages the trail and drops expired points. Called once per game frame.
void blade_step(void);

void blade_draw(GContext *ctx);

bool blade_is_down(void);
