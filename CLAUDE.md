# Fruit Chop

## What this is

A Fruit Ninja-style game for the touch-screen Pebble watches. Fruits arc up from
the bottom of the screen; you swipe to slice them. Bombs end the run, and so does
dropping three fruits.

| Directory | Language | Role |
| --- | --- | --- |
| `watch/` | C (Pebble SDK 4.17) | The game. All `pebble` commands run from here. |
| `watch/src/pkjs/` | JS (ES5) | Phone side. Nothing but the score share — no network. |
| `docs/` | HTML | The share card, served by GitHub Pages. |
| `developer-portal/` | assets | App-store screenshots and icons. |

There is no server. The game itself is fully offline: data flow is just touch →
blade buffer → slice test → entity pools → framebuffer, and every rule in the
game runs on the watch. The phone is involved in exactly one thing, sharing a
score — see below.

## Commands

All from `watch/`; the pebble tool fails elsewhere.

```bash
pebble build                                   # builds emery + gabbro
pebble clean                                   # needed after editing package.json
pebble install --emulator emery                # or --emulator gabbro
pebble screenshot --emulator emery --no-open shot.png
pebble emu-button --emulator emery click select
pebble logs --emulator emery
pebble emu-app-config --emulator emery --file ../docs/index.html   # the share card
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
| `share.c/.h` | AppMessage. One outbound message per finished run, and nothing else. The only file that touches `app_message_*`. |
| `sound.c/.h` | All audio: the four swish clips and the rotation between them, the looping bomb fuse, the explosion, the one-voice arbitration between them, and the persisted on/off flag. The only file that touches `speaker_*`. |
| `draw.c/.h` | Procedural fruit/bomb rendering, silhouette clipping for halves, juice, HUD, title and game-over screens. |

The timer callback only mutates state and calls `layer_mark_dirty()`; all drawing
happens in `prv_update_proc`. Touch events never touch game state directly — they
only append to the blade buffer, which the slice test then consumes.

Audio follows the same rule as drawing: `game.c` never calls `speaker_*`. It
counts successful cuts and flags a bomb hit; the timer callback drains those with
`game_take_cut_events()` / `game_take_bomb_hit()`, holds the fuse with
`sound_set_fuse(game_has_bomb())`, and ticks `sound_update()`. Draining once a
frame is what stops one stroke through three fruits stacking three swishes.
`sound_update()` is not optional bookkeeping — it is what restarts the fuse and
what feeds the streamed explosion, so it has to run every frame in every state.

## Score sharing

The one thing the phone does. It follows the same drain-once-a-frame rule as
sound: `game.c` raises `s_run_over` inside `prv_record_score()` — already the
single transition into game over, and raised *after* the high score is written so
the reported best includes the run — and `prv_timer_callback` drains it with
`game_take_run_over()` and calls `share_report_run()`. One AppMessage per run,
carrying `SCORE`, `DIFF`, `BEST`.

`src/pkjs/index.js` stores that in `localStorage` and does nothing else until
`showConfiguration` fires, when it opens the GitHub Pages card with the run in
the query string. `docs/index.html` redraws the score as a 1200x630 `<canvas>`
and hands it to the OS share sheet. Deliberately one card and one button: the
page is not a settings screen and should not grow into one.

Things that are easy to get wrong here:

- **The watch cannot open anything on the phone.** There is no share API in the
  SDK and no way to make the phone show UI on demand: `Pebble.openURL()` is only
  dependable inside the `showConfiguration` handler, i.e. when the player taps
  the settings gear next to Fruit Chop in the Pebble app. That is why the flow is
  store-now-show-later, and why `draw_gameover` spends a line pointing at the
  gear. Do not try to make the webview appear at game over.
- **`capabilities: ["configurable"]` in `package.json` is what puts the gear
  there.** Drop it and the share card becomes unreachable, with no other symptom.
- Sending is best effort and must stay that way — no phone, no JS or a busy
  outbox are all logged and dropped. Nothing in `share.c` may block or retry:
  it is called from the frame timer.
- The hint line is drawn only when `connection_service_peek_pebble_app_connection()`
  is true, and the game-over backing plate grows by 16px when it is. Both screens
  need a look after touching that panel; the round one is the tight fit.
- The share page is ES5 with no build step, same as the sibling repos' settings
  pages, and **GitHub Pages must be enabled on this repo (`main`, `/docs`)** or
  the gear opens a 404.
- **`navigator.share` is the only thing that can send the picture,** which is
  why it is the only button. Two shortcuts were tried and removed: per-network
  buttons (`x.com/intent/post` and friends) are intent URLs and a URL cannot
  carry an attachment on any phone, so they posted text and read as a bug next
  to a card; and `<a download>` is dead on iOS, where WKWebView blocks top-level
  navigation to a `data:` URL. Do not put either back. The long-press menu is
  the remaining way to keep the image, which is why `-webkit-touch-callout` must
  stay `default` on the card and why the failure message points at it.
- **`navigator.canShare` may be absent where files still work,** so the page
  attempts the file share whenever one could be built and treats the rejection
  as the answer, rather than refusing up front. A retry inside the `catch` is
  pointless: the tap that authorised the share is spent, so the failure reveals
  the message for the *next* tap instead. `AbortError` is a cancelled sheet,
  not a refusal.
- The link is inside the shared `text`, not passed as `url`. Given both, a good
  many share targets keep the link and drop the image.
- The score in the card is only as fresh as the last run the phone received. A
  run played out of Bluetooth range never arrives, which is why the page prints
  how old the score is and has a no-score state at all.

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
- **The Speaker API exists only on emery, gabbro and flint.** Everywhere else
  `pebble.h` `#define`s every `speaker_*` call to `(0)`, so a portability guard
  would turn a missing speaker into a silent game rather than a build failure —
  `sound.c` deliberately calls them unguarded. Two hard limits shape anything
  audio: `SpeakerPcmFormat` tops out at **16kHz 8-bit signed mono**, and a
  single `speaker_play_tracks()` call is capped at
  `SPEAKER_MAX_SAMPLE_BYTES_TOTAL` (16K, i.e. 1.02s at that format). There is no
  audio resource type either — PCM ships as `raw`, which the SDK embeds
  byte-for-byte, and is read back with `resource_load`. Convert with
  `ffmpeg -i in.wav -ac 1 -ar 16000 -acodec pcm_s8 -f s8 out.raw` and check the
  byte count against the cap; an oversized clip is rejected at runtime, silently.
- **Sound is muteable system-wide and the app cannot override it.** `sound.c`
  checks `speaker_is_muted()` before starting anything, which covers both the
  Sounds & Haptics setting and Quiet Time.
- **The speaker is one voice.** `speaker_play_tracks()` mixes up to four tracks,
  but only within a single call — there is no way to add a track to something
  already playing. So `sound.c` arbitrates by hand: explosion beats swish beats
  fuse, tracked in `s_voice`. The fuse is the awkward one, being the only
  sustained sound: it plays with `loop = true` under a 10s note and is stopped by
  hand when the last bomb leaves, and `sound_update()` polls
  `speaker_get_status()` once a frame to restart it after a swish has cut it off.
  Polling rather than `speaker_set_finish_callback()` — a callback that restarts
  playback gets re-entered by its own `speaker_stop()`.
- **The fuse needs its own buffer.** A looping sample is still being read long
  after the call that started it, so a swish loading into the same buffer would
  tear it. The one-shots can share, since a swish and an explosion never overlap.
- **A 1.02s cap is short for a sound with a tail, so the explosion is streamed.**
  The fuse is a 0.95s slice cut from the steady middle of the recording, which
  loops cleanly because it is broadband hiss, and fits the one-shot path. The
  explosion has to keep rumbling under the game-over screen, so it goes through
  `speaker_stream_open` / `_write` / `_close` instead, pumped 4K at a time from
  `sound_update()`. Pacing is free: `speaker_stream_write` returns short when its
  buffer is full, so advancing by what it accepted is the whole flow control.
  Two traps. A stream that is open but unwritten reports `SpeakerStatusIdle`,
  which looks like a free voice — `sound_update()` checks `s_boom_left` before it
  believes the status. And `speaker_stream_close()` deliberately lets the buffered
  tail play out, so `sound_stop()` has to close *and* `speaker_stop()`.
- **The source explosion never decays** — it is still at -12dB at 4.2s and then
  just stops. Any length needs a fade; the shipped clip is 2.5s with a 0.6s one.
- **Bombs are rolled `FC_BOMB_LEAD_FRAMES` before they launch** so the fuse can be
  heard before the bomb is seen — at ~1.5s of airtime, a fuse lit at launch reads
  as description rather than warning. `prv_roll_wave` picks the wave and
  `prv_spawn_wave` launches it later; `game_has_bomb()` counts a rolled-but-unlaunched
  bomb. Keep the roll per item and stopping at the first hit: rolling once for the
  whole wave instead would quietly cut HARD's bomb rate from 59% a wave to 26%.
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

UP and DOWN only do this **during play** — on the title screen UP is the
difficulty picker and DOWN toggles sound, so the harness never shadows a real
control.

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
