# Fruit Chop

A Fruit Ninja-style game for Pebble Time 2 and Pebble Round 2.

Sixteen fruits arc up from the bottom of the screen — watermelon, mango,
pineapple, coconut, strawberry, green and red apple, kiwifruit, banana, lemon,
lime, orange, plum, pear, passion fruit and peach — each with its own colour,
silhouette and size. Swipe across the touchscreen to slice them. Each fruit
splits along the line of your swipe, keeping its own shape — a cut banana leaves
two banana halves, not two half-circles — and throws a burst of juice in its
flesh colour. Slice a bomb and the run is over; drop three fruits and it is over
too.

Pick a difficulty on the title screen with UP and DOWN: it sets how fast fruit
arrives and how often a bomb comes with it. Your best score is kept separately
for each difficulty and shown under the title.

## Requirements

Touch input exists only on the modern Pebble hardware, so the game targets:

- **emery** — Pebble Time 2, 200x228 colour
- **gabbro** — Pebble Round 2, 260x260 colour

Built with Pebble SDK 4.17 (`touch_service_subscribe` requires SDK 4.9+).

## Building

```bash
cd watch
pebble build
pebble install --emulator emery      # or gabbro, or --phone <ip>
```

## Playing

| Input | Action |
| --- | --- |
| Swipe | Slice fruit |
| Tap (title) | Start |
| SELECT | Start / retry |
| UP / DOWN (title) | Easier / harder |
| BACK | Return to title, or exit from the title |

Development builds remap the buttons to scripted swipes for testing — see
`CLAUDE.md`.

## Layout

```
watch/          the game (all pebble commands run from here)
  src/c/        C sources
developer-portal/  app-store assets
```
