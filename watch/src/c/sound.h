#pragma once

#include <pebble.h>

// Slice audio. One entry point for playback, one for the on/off setting, which
// the title screen owns and which persists across runs.
void sound_init(void);

bool sound_is_enabled(void);
void sound_set_enabled(bool on);

// Plays the next swish in the rotation, preempting whatever was still sounding.
void sound_play_slice(void);

// The bomb going off. Beats everything, and takes the fuse down with it.
void sound_play_explosion(void);

// Whether a bomb is in the air. The fuse burns for as long as this is set,
// yielding to a swish and picking itself back up afterwards.
void sound_set_fuse(bool on);

// Called once a frame. Restarts the fuse whenever the speaker falls idle under
// it, which is what makes it survive being interrupted.
void sound_update(void);

// Silences anything in flight; called when the app is backgrounded.
void sound_stop(void);
