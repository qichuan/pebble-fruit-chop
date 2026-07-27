# Fruit Chop

## What this is

A Fruit Ninja-style game for the touch-screen Pebble watches. Fruits arc up from
the bottom of the screen; you swipe to slice them. Bombs end the run, and so does
dropping three fruits.

| Directory | Language | Role |
| --- | --- | --- |
| `watch/` | C (Pebble SDK 4.17) | The entire game. All `pebble` commands run from here. |
| `developer-portal/` | assets | App-store screenshots and icons. |

There is no server and no PebbleKit JS — the game is fully offline, so there is no
`src/pkjs/` and no `messageKeys`. Data flow is just: touch → blade buffer →
slice test → entity pools → framebuffer.

## Commands

All from `watch/`; the pebble tool fails elsewhere.

```bash
pebble build                                   # builds emery + gabbro
pebble clean                                   # needed after editing package.json
pebble install --emulator emery                # or --emulator gabbro
pebble screenshot --emulator emery --no-open shot.png
pebble emu-button --emulator emery click select
pebble logs --emulator emery
pebble kill
```

Pass `--emulator <name>` explicitly — more than one emulator is usually running.
This tool version does **not** accept `--scale`. Add `--vnc` to every
emulator-touching command in a headless environment.

## Architecture

`watch/src/c/`:

| File | Owns |
| --- | --- |
| `fc_config.h` | Every tunable in the game. Start here when adjusting feel. |
| `fixed.h` | 8.8 fixed-point macros and the RNG helper. |
| `fruit.c/.h` | The roster. `FruitType` plus one `FruitProfile` row per fruit: skin, flesh, silhouette, relative size. Adding a fruit is an enum member and a row. |
| `main.c` | Window lifecycle, the AppTimer loop, state machine, buttons, touch subscription, and the debug swipe harness. |
| `game.c/.h` | Pure simulation: entity pools, physics, spawning, slice resolution, score, lives, juice particles, difficulty table and persisted high scores. No `graphics_*` calls. |
| `blade.c/.h` | Touch point buffer, speed gating, trail rendering. `blade_feed()` is the single input entry point. |
| `draw.c/.h` | Procedural fruit/bomb rendering, silhouette clipping for halves, juice, HUD, title and game-over screens. |

The timer callback only mutates state and calls `layer_mark_dirty()`; all drawing
happens in `prv_update_proc`. Touch events never touch game state directly — they
only append to the blade buffer, which the slice test then consumes.

## Key constraints & gotchas

- **Touch only exists on emery and gabbro** (`PBL_TOUCH`). `targetPlatforms` is
  deliberately just those two, unlike the sibling repos which list all seven.
- **Touch does not work in watchfaces.** `watchapp.watchface` must stay `false`.
- **The emulator cannot be sent touch events.** There is no `pebble emu-touch`,
  and libpebble2 has no touch packet. That is the entire reason the debug swipe
  harness exists — see below.
- **`atan2_lookup` and the drawing APIs disagree by 90 degrees.** `atan2_lookup`
  returns a maths-convention angle from +x; `graphics_fill_radial`, `sin_lookup`
  and `cos_lookup` measure from straight up, clockwise. `game_slice_segment`
  corrects for this; without it, cut faces come out perpendicular to the swipe.
- **`FRUIT_BOMB` must stay last in `FruitType`,** immediately before
  `FRUIT_TYPE_COUNT`. `prv_spawn` picks a fruit with
  `fc_rand_range(0, FRUIT_BOMB - 1)`, so everything below the bomb is edible.
- **The display filter squashes the warm colours together**, and sixteen fruits
  do not fit in what is left. `GColorOrange` renders around (228, 95, 100) —
  i.e. as red — so it is never a body fill; oranges use `GColorChromeYellow`.
  But `GColorChromeYellow` and `GColorRajah` in turn land in the same salmon
  band as `GColorMelon`, so orange, mango and peach cannot be separated by
  colour either: the mango is a true `GColorYellow` with a red cheek, and all
  three halve into different cut faces. Check any new colour against a
  screenshot, never against its name.
- **Colour alone is not enough, so every fruit also carries a silhouette and a
  mark.** `FruitShape` is circle, two-lobed, or polygon (a `GPath` over a static
  buffer — no `gpath_create`, no malloc), and `prv_draw_detail` adds the stem,
  crown, seeds or crease. The pairs closest in hue are always the ones separated
  by shape.
- **A sliced half is the fruit's own outline clipped against the blade,** not a
  pie sector. `prv_clip_halfplane` in `draw.c` runs Sutherland-Hodgman on the
  silhouette from `prv_outline`, so a cut banana leaves two banana pieces. This
  is why `Half` carries both `angle` (where the blade went) and `body_angle`
  (which way the fruit was facing) — clipping needs both, and both advance by
  `spin`. The skin and the inset flesh are clipped on the *same* line, which is
  what leaves a rind on the curved edge but bare flesh on the flat cut face.
  Only the bomb still uses `graphics_fill_radial`, being a plain circle.
  Types whose insides are the recognisable part (watermelon pips, citrus
  segments, kiwi and passion seeds, stone fruit stones) draw detail on the cut
  face; keep those radii well inside `r` so they cannot poke out of a narrow
  silhouette like the banana's.
- **`pebble emu-button` can permanently wedge the emulator's screenshot
  service.** Once it happens, every later `screenshot` and `emu-button` on that
  instance returns `libpebble2.exceptions.TimeoutError`, and it never recovers —
  only `pebble kill` plus a reinstall does. It is environmental, not an app
  bug: confirm by rebuilding a known-good commit and reproducing. When it
  strikes, stop trying to drive the UI by button and instead drive the state
  from code: temporarily force the state you want in `prv_timer_callback` (a few
  frames after launch, call `game_set_state` and `prv_debug_start_swipe`), then
  take a single screenshot on a fresh boot. Screenshots taken before any button
  press are reliable.
- **The touch sensor draws power while subscribed**, so subscribe in
  `window_appear` and unsubscribe in `window_disappear`, not load/unload.
- **`persist_read_int` cannot tell "holds 0" from "never written",** so the
  stored difficulty is written as `difficulty + 1`. Without that an unwritten
  key reads as 0 and the game defaults to EASY instead of NORMAL.
- **The Gothic system fonts have no arrow glyphs.** `▲`/`▼` render as tofu
  boxes; the title screen's difficulty arrows are drawn with `prv_tri` instead.
- **`FC_AIRTIME_FRAMES` is the one knob for how heavy the game feels.** Gravity
  is derived as `1/t^2`, so changing it moves the whole arc at once — do not
  hand-tune gravity and launch speed separately. Three things are calibrated
  against it and need revisiting whenever it moves: the spawn intervals (shorter
  flights empty the field), the launch `vx` jitter in `prv_spawn` (the aim term
  scales with airtime, the jitter does not), and the juice gravity divisor.
  Note the target is *not* literal gravity: at watch scale a 170px toss would
  land in well under a tenth of a second, so this is tuned to the fastest arc
  that is still cuttable. Keep per-frame travel below the smallest fruit radius
  or the slice test starts tunnelling between frames.
- Physics constants are derived from `layer_get_bounds()` at runtime, never
  hardcoded, so the 200x228 and 260x260 screens play identically.

## Debug swipe harness

`FC_DEBUG_SWIPE` in `fc_config.h` (currently `0`) makes the buttons drive
scripted swipes so the slice pipeline can be exercised and screenshotted from the
CLI. During play: **SELECT** cuts a watermelon, **UP** cuts a bomb (ends the run),
**DOWN** cuts the next fruit in the roster, cycling through all sixteen so each
can be screenshotted without a rebuild. The parked target's name is logged.

UP and DOWN only do this **during play** — on the title screen they belong to
the difficulty picker, so the harness never shadows a real control.

Two things to know when scripting it. Natural spawns keep falling in the gaps
between button presses, so a long capture run loses all three lives partway
through — restart between cuts with SELECT ... DOWN ... screenshot ... BACK, which
stays inside the app. Pressing BACK on the title screen exits to the launcher and
resets the cycle counter.

Each one clears the field, parks a stationary target at the centre, then feeds
fabricated `TouchEvent`s through `blade_feed()` — byte-for-byte the same entry
point the real TouchService uses, so synthetic and real input cannot diverge.
After the cut it runs `FC_DEBUG_SETTLE_FRAMES` of physics so the halves separate,
then freezes the field for `FC_DEBUG_FREEZE_FRAMES` because `pebble screenshot`
is a ~1s round trip and the halves would otherwise be gone before capture.

**It is `0` in the shipping build, and must stay that way.** With it on, SELECT
/ UP / DOWN slice fruit during play, which reads as a cheat — the buttons are
supposed to do nothing mid-game. Set it to `1` only while developing, and put it
back before building anything a player will see. With it off there is no path
from a button press to `game_slice_segment` at all: the only caller is
`blade_feed()`, and the only caller of that is the real touch handler.

## Conventions

- Static functions are prefixed `prv_`, file-scope state `s_`.
- Fixed-size static arrays only; no `malloc`.
- `wscript` is the stock SDK default and is byte-identical to the sibling repos —
  never edit it.
- No `appinfo.json` in source; `package.json` is the only app config.
- System fonts only (`FONT_KEY_GOTHIC_*`).
- Platform differences via `PBL_IF_ROUND_ELSE` / `#if defined(PBL_ROUND)`; layout
  always derived from `layer_get_bounds()`.

## Verifying a change

Build, install, then walk the states with `pebble emu-button` and read every
screenshot rather than assuming it rendered:

```bash
pebble build && pebble install --emulator emery
pebble emu-button --emulator emery click select   # title -> playing
pebble emu-button --emulator emery click select   # scripted slice; field freezes
pebble screenshot --emulator emery --no-open slice.png
```

The second SELECT only cuts anything with `FC_DEBUG_SWIPE` set to `1`; in the
shipping build the buttons do nothing during play, so temporarily flip the flag
to exercise the slice pipeline and flip it back afterwards.

Repeat with `--emulator gabbro` to confirm the round HUD is not clipped.

Real touch input cannot be scripted. Verify it by dragging the mouse across the
emulator's SDL window (QEMU has a `pebble-touch` device and runs with
`show-cursor=on`), or on real hardware with `pebble install --phone <ip>`.
