#pragma once

// ---------------------------------------------------------------------------
// Every tunable in the game lives here. Slice feel is pure iteration, so keep
// these together and adjust against emulator screenshots rather than scattering
// magic numbers through the source.
// ---------------------------------------------------------------------------

// Frame cadence. 33ms is ~30fps, which the emery/gabbro framebuffers sustain
// comfortably while filling circles and a tapered blade trail.
#define FC_FRAME_MS 33

// Physics. Gravity and launch speed are derived at runtime from the actual
// layer bounds (see game_init) so emery (228 tall) and gabbro (260 tall) play
// identically. These two constants drive that derivation.
#define FC_AIRTIME_FRAMES 80          // ~2.6s from launch to falling off-screen
#define FC_APEX_NUM 3                 // apex height = 3/4 of screen height
#define FC_APEX_DEN 4

#define FC_SPIN_MIN 200               // TRIG_MAX_ANGLE units per frame
#define FC_SPIN_MAX 900

// Entity pools. Fixed-size, no malloc, well under the 128K app RAM budget.
#define MAX_FRUITS 10
#define MAX_HALVES 12
#define MAX_BLADE_POINTS 12

// Base radius before the per-type size_pct in fruit.c is applied. The range is
// deliberately narrow: the profile table now supplies the size variety (a
// strawberry is small, a watermelon is big), so this jitter only has to stop
// two of the same fruit looking stamped from the same die. Effective radius
// across the roster works out at roughly 13-24px.
#define FRUIT_RADIUS_MIN 16
#define FRUIT_RADIUS_MAX 19
#define BOMB_RADIUS 16

// Vertices in a fruit's silhouette template, and in the polygon left after that
// silhouette is clipped against the blade. Clipping a convex polygon against
// one half-plane adds at most one vertex, so the second only needs to be a
// little larger than the first.
#define FC_MAX_OUTLINE_POINTS 16
#define FC_MAX_CLIP_POINTS 18

// Spawning. One spawn attempt every FC_SPAWN_INTERVAL frames, shrinking toward
// FC_SPAWN_INTERVAL_MIN as the score climbs.
#define FC_SPAWN_INTERVAL 34
#define FC_SPAWN_INTERVAL_MIN 16
#define FC_BOMB_CHANCE_PCT 14         // percent of spawns that are bombs

// Blade. A point older than FC_BLADE_TTL frames is dropped, so the trail decays
// on its own even when the finger stops moving.
#define FC_BLADE_TTL 8
#define FC_MIN_SLICE_PX 5             // min segment length that counts as a cut
// Upper bound too: if the firmware coalesces or drops samples during a fast
// flick, a single 200px jump would otherwise slice everything on the diagonal.
#define FC_MAX_SLICE_PX 90

#define FC_SPLIT_SPEED 512            // 8.8 -> 2px/frame kick, perpendicular to the cut

#define FC_HALF_TTL 30                // frames a sliced half stays on screen
#define FC_START_LIVES 3

// Debug swipe harness. There is no `pebble emu-touch` and libpebble2 carries no
// touch packet, so the slice pipeline is otherwise unreachable from the CLI.
// With this set, the UP button fires a synthetic swipe through the same
// blade_feed() entry point that real touch events use.
#define FC_DEBUG_SWIPE 1
#define FC_DEBUG_SWIPE_STEPS 8
// Frames the field is held still after a scripted swipe. `pebble screenshot` is
// a ~1s round trip, so without a generous pause the halves have flown off
// before the frame is captured.
#define FC_DEBUG_FREEZE_FRAMES 90
// Frames of motion allowed between the cut and the freeze, so the halves are
// visibly apart in the captured frame rather than still overlapping.
#define FC_DEBUG_SETTLE_FRAMES 8
