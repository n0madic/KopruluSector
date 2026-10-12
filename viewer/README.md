# Map viewer

Development entry point for plan §7 phase 1 item 7. It shows the terrain of any catalog mission from its `.kpm`, through the product render pipeline:
- index target;
- palette constant;
- integer or sharp scaling.

The terrain is drawn through the tileset template collections, which are loaded by collection proxies.

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
| Esc | Quit |

`viewer.mission = <n>` in a settings file selects the first mission.

**Autotest.** Add a settings file with `[viewer] autotest = 1` through `SPIKE_SETTINGS`. The viewer then loads every mission and logs load, import and fill times.

On HTML5 the autotest also reads back the index target at the top-left and bottom-right of every map. Each megatile fully in view must have the CRC-32 the converter computed from VX4/VR4 (`generated/viewer/<tileset>.mcrc`).

Result on 2026-10-12: 64 missions and all eight tilesets, with 49,920 megatiles checked and 0 mismatches. Fill takes about 26 ms per mission on macOS and 33–65 ms on HTML5.
