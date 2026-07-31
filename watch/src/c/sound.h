#pragma once

#include <pebble.h>

// Slice audio. One entry point for playback, one for the on/off setting, which
// the title screen owns and which persists across runs.
void sound_init(void);

bool sound_is_enabled(void);
void sound_set_enabled(bool on);

// Plays the next swish in the rotation, preempting whatever was still sounding.
void sound_play_slice(void);

// Silences anything in flight; called when the app is backgrounded.
void sound_stop(void);
