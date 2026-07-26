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
| `main.c` | Window lifecycle, the AppTimer loop, state machine, buttons, touch subscription, and the debug swipe harness. |
| `game.c/.h` | Pure simulation: entity pools, physics, spawning, slice resolution, score, lives. No `graphics_*` calls. |
| `blade.c/.h` | Touch point buffer, speed gating, trail rendering. `blade_feed()` is the single input entry point. |
| `draw.c/.h` | Procedural fruit/bomb rendering, HUD, title and game-over screens. |

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
- **`GColorOrange` is not usable next to `GColorRed`.** Through the display
  filter both render around (228, 95, 100), making apples and oranges
  indistinguishable. Oranges use `GColorChromeYellow` instead.
- **The touch sensor draws power while subscribed**, so subscribe in
  `window_appear` and unsubscribe in `window_disappear`, not load/unload.
- Physics constants are derived from `layer_get_bounds()` at runtime, never
  hardcoded, so the 200x228 and 260x260 screens play identically.

## Debug swipe harness

`FC_DEBUG_SWIPE` in `fc_config.h` (currently `1`) makes the buttons drive
scripted swipes so the slice pipeline can be exercised and screenshotted from the
CLI. During play: **SELECT** cuts a watermelon, **UP** cuts a bomb (ends the run),
**DOWN** cuts a durian.

Each one clears the field, parks a stationary target at the centre, then feeds
fabricated `TouchEvent`s through `blade_feed()` — byte-for-byte the same entry
point the real TouchService uses, so synthetic and real input cannot diverge.
After the cut it runs `FC_DEBUG_SETTLE_FRAMES` of physics so the halves separate,
then freezes the field for `FC_DEBUG_FREEZE_FRAMES` because `pebble screenshot`
is a ~1s round trip and the halves would otherwise be gone before capture.

**Set `FC_DEBUG_SWIPE` to `0` before publishing.**

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

Repeat with `--emulator gabbro` to confirm the round HUD is not clipped.

Real touch input cannot be scripted. Verify it by dragging the mouse across the
emulator's SDL window (QEMU has a `pebble-touch` device and runs with
`show-cursor=on`), or on real hardware with `pebble install --phone <ip>`.
