#pragma once

#include <pebble.h>

// The phone half of the score share. The watch has no share API of its own and
// cannot put anything on the phone screen, so all this does is hand the result
// of a finished run to the PebbleKit JS side, which stores it until the player
// opens the app's settings page. The only file that touches `app_message_*`.
void share_init(void);

// Reports one finished run. Best effort by design: no phone, no JS, a busy
// outbox or a full dictionary all end as a log line and nothing more -- a
// failure here must never be visible in the game.
void share_report_run(int score, int difficulty, int best);
