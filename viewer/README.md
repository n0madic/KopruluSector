# Map viewer

Development entry point for plan §7 phase 1 item 7 and phase 2 item 5. It shows any catalog mission from its `.kpm`, through the product render pipeline:
- index target;
- palette constant;
- integer or sharp scaling.

The terrain is drawn through the tileset template collections, which are loaded by collection proxies. A minimap in the bottom-left corner shows the view.

**Animated scene.** `viewer/map_world.lua` creates the mission's preplaced units and THG2 sprites as `game/images` sprites; their images run the IScript VM every tick (42 ms, the "fastest" speed). This is a viewer approximation: units have no CUnit, orders or movement yet, so opcodes that need a unit run against viewer hooks that only turn the sprite. `presentation/images.lua` orders the visible sprites by the native depth key every tick and the `world_images` backend draws them by depth rank ([ADR 0006](../docs/adr/0006-depth-ranked-composition.md)), every draw method included. The atlases come from the mission's resource packs (`PACK`). The palette cycles on real time (`presentation/palette.lua`); IScript sounds go into the scene's event ring and are counted, not played (the audio manager is phase 9).

**Animation browser.** A shows one image at the centre of the view in a world of its own (`viewer/browser.lua`): any image whose atlas the mission's packs hold, in any of its animations and directions. Unit opcodes run against the viewer hooks; an animation that needs more stops with its fault shown. Switching the mission closes the browser.

## Prepare the data

Run the converter (needs the original data, see `defs/profile/`):

```sh
python3 -m converter missions --data DIR
python3 -m converter data --data DIR
python3 -m converter resources --data DIR
```

## Run

```sh
SPIKE_SETTINGS_FILE=viewer/viewer.settings tools/spike.sh viewer macos
SPIKE_SETTINGS_FILE=viewer/viewer.settings tools/spike.sh viewer web
```

The `macos` run closes after `SPIKE_TIMEOUT` seconds (default 120).

**Controls:**

| Input | Action |
|---|---|
| Arrows or drag | Pan |
| N / P | Next / previous mission |
| M | Scale mode (auto, integer, sharp) |
| W / R | Walkability / region overlay |
| B | Brightness option 60, 80, …, 140 |
| G | Grayscale (as the paused game shows it) |
| A | Animation browser on/off |
| [ / ] | Browser: previous / next image |
| ; / ' | Browser: previous / next animation |
| , / . | Browser: previous / next direction |
| Space | Browser: restart the animation |
| Click on the minimap | Move the view there |
| Esc | Quit |

`viewer.mission = <n>` in a settings file selects the first mission; `viewer.autotest_count = <n>` limits the autotest to n missions from it.

**Autotest.** `SPIKE_SETTINGS=viewer/autotest.settings` (`[viewer] autotest = 1`). The viewer then loads every mission with its packs and scene and logs load, import and fill times and the scene size. `KOPRULU_VIEWER=1 tools/ci.sh` runs the web autotest and fails on any mismatch.

```sh
SPIKE_SETTINGS_FILE=viewer/viewer.settings SPIKE_SETTINGS=viewer/autotest.settings SPIKE_TIMEOUT=600 tools/spike.sh viewer web
```

The autotest keeps the scene still (no ticks) except for timed runs: 48 ticks, then 12 and 28 more after the draw effects. On HTML5 it also reads back the index target:
- **Terrain**, at the top-left and bottom-right of every map, before the scene is drawn. Each megatile fully in view must have the CRC-32 the converter computed from VX4/VR4 (`generated/viewer/<tileset>.mcrc`).
- **Sprites**, at the top-left after the scene is drawn. Every probe-animation image fully in view that no later-drawn image covers must show its GRP frame's indices (`viewer/sprite_check.lua`, `generated/viewer/sprite_probe.bin`).
- **Composition**, the whole top-left view at creation, after 48 ticks, and after the draw effects at 12 and 40 more ticks (`viewer/effect_scene.lua`: the view's units cloaked, decloaked, under the cloak blends, EMP, warp texture or outline, some with selection circles and health bars); on the first mission also twelve images of the animation browser. The viewer logs the CRC of the index target, the composition plan (with its budget fallbacks and the images past a family's capacity, both 0 unless the plan was degraded) and the draw list (with the pixels of health bars); `tools/viewer_compose_check.py --data DIR` draws the same view on the CPU with the original kernels from the retail GRPs, tilesets and PCX tables and compares. `viewer.compose_dump = <mission>` also logs that view's pixels, and the script then reports the first differing pixels.

Result on 2026-10-12:
- 64 missions and all eight tilesets.
- Terrain: 49,920 megatiles checked, 0 mismatches.
- Sprites: 104 images (22 from rotated atlas frames), 84,557 pixels, 0 mismatches.
- Composition: 268 views, all equal to the CPU, with every draw method drawn (methods 2–8, 11–13 and 15 hundreds of times each) and up to 23 composition windows in a view.
- Fill takes about 26 ms per mission on macOS and 33–65 ms on HTML5. With about 1,400 images a tick of the scene takes 0.2 ms and the presentation sync up to 1.9 ms on macOS.

In the desktop app's browser pane a synthetic click sends press and release within one engine frame, so the minimap never sees the press. A real click, or JS mouse events 100 ms apart, works.

**Pack check.** The viewer collection references every resource pack (`/generated/packs/packs.go`) and mission assets proxy (`/generated/missions/mission_assets.go`), so these runs also build them. With `[viewer] packcheck = 1` in a `SPIKE_SETTINGS` file, `viewer/packcheck.script` loads, creates, deletes and unloads each pack, checks that every atlas is one page of at most 4096², loads all packs together, then loads and unloads every mission assets collection. Results and timings: [resource packs](../docs/formats/packs.md#check).
