# Map viewer

Development entry point for plan §7 phase 1 item 7. It shows any catalog mission from its `.kpm`, through the product render pipeline:
- index target;
- palette constant;
- integer or sharp scaling.

The terrain is drawn through the tileset template collections, which are loaded by collection proxies. The static units and doodads come from the mission's resource packs (`PACK`, loaded through their collection factories) and are drawn by the `mesh_texture` backend (`presentation/viewer_scene.lua`). A minimap in the bottom-left corner shows the view.

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
| Click on the minimap | Move the view there |
| Esc | Quit |

`viewer.mission = <n>` in a settings file selects the first mission.

**Autotest.** `SPIKE_SETTINGS=viewer/autotest.settings` (`[viewer] autotest = 1`). The viewer then loads every mission with its packs and scene and logs load, import and fill times and the scene size. `KOPRULU_VIEWER=1 tools/ci.sh` runs the web autotest and fails on any mismatch.

```sh
SPIKE_SETTINGS_FILE=viewer/viewer.settings SPIKE_SETTINGS=viewer/autotest.settings SPIKE_TIMEOUT=600 tools/spike.sh viewer web
```

On HTML5 the autotest also reads back the index target:
- **Terrain**, at the top-left and bottom-right of every map, before the scene is drawn. Each megatile fully in view must have the CRC-32 the converter computed from VX4/VR4 (`generated/viewer/<tileset>.mcrc`).
- **Sprites**, at the top-left after the scene is drawn. Every isolated static image of a probe animation fully in view must show its GRP frame's indices (`viewer/sprite_check.lua`, `generated/viewer/sprite_probe.bin`).

Result on 2026-10-12:
- 64 missions and all eight tilesets.
- Terrain: 49,920 megatiles checked, 0 mismatches.
- Sprites: 76 images (6 from rotated atlas frames), 38,340 pixels, 0 mismatches.
- Fill takes about 26 ms per mission on macOS and 33–65 ms on HTML5.

In the desktop app's browser pane a synthetic click sends press and release within one engine frame, so the minimap never sees the press. A real click, or JS mouse events 100 ms apart, works.

**Pack check.** The viewer collection references every resource pack (`/generated/packs/packs.go`) and mission assets proxy (`/generated/missions/mission_assets.go`), so these runs also build them. With `[viewer] packcheck = 1` in a `SPIKE_SETTINGS` file, `viewer/packcheck.script` loads, creates, deletes and unloads each pack, checks that every atlas is one page of at most 4096², loads all packs together, then loads and unloads every mission assets collection. Results and timings: [resource packs](../docs/formats/packs.md#check).
