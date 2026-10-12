# Spike 09: terrain tilemap template

Plan: phase 0 item 9, §2 (Terrain), §2.2, D10. Inputs: [terrain assets](../research/evidence/terrain-assets.json), [palette cycles](../research/native-palette-bootstrap.md).

## Method

`python3 -m converter.emit.spikes --data <app> tilemap` (`converter/emit/tilemap.py`, decoders in `converter/formats/tileset.py` and `palette.py`) writes:

- **Tile sources.** Indexed megatile sheets with 120 columns, `extrude_borders: 1`, L8 texture profile.
  - Desert: all 5,810 megatiles, 3840×1568 image.
  - Installation: all 1,431 megatiles, 3840×384 image.
- **Templates.** 256×256 `.tilemap` files with layer `terrain`.
  - `corners`: only cells (0,0) and (255,255), which define the bounds.
  - `full`: all 65,536 cells, for comparison.
- **`<tileset>.kpd`** (KPF kind `TSET`) with three sections: `CV5M` (megatile per group×16+cell), `WPE ` and `CYCL` (stock simple cycle ranges, speed 8).
- **`.kpm` (KPF kind `MISN`).** `META` (w, h, era) and the raw `MTXM` for these maps:
  - `desert_terran05a` (192×128);
  - `desert_256` (that map tiled to 256×256, the worst case);
  - `install_terran04` (128×128);
  - `install_64` (64×64 crop).

The spike (`spikes/09-tilemap`) works as follows:

- Each tileset template collection is loaded through a collection proxy.
- `terrain.script` loads the `.kpd` and `.kpm` through `game/data/loader.lua` and maps each MTXM WORD through CV5 to a megatile with `string.byte`. It fills the template with `tilemap.set_tile`, rows top-down from template row 256.
- It then clears the template cells outside a smaller map and probes the bounds.
- The render script draws the tilemap into an RGBA8 index render target. One palette pass over a full-screen mesh quad then looks colors up in a 256×1 runtime palette texture (`resource.create_texture` / `resource.set_texture`).
- Palette cycling rotates the whole palette by the stock tileset table (Desert: 1–13 and 248–254, Install: none), using the native simple rotation and counter.
  The research does not establish the wall-clock cadence of the cycle driver, so the spike calls it once per frame.
- Before cycling starts, a capture of the map's top-left 640×480 is compared pixel by pixel with an offline render (MTXM → CV5 → VX4/VR4 → WPE) by `check_capture.py`.

## Results

Raw logs are in `build/spikes/09-tilemap/`: `macos.log`, `web-webgl{1,2}.{log,png,check.log}`, `android.log`.

| | macOS (LuaJIT, M4 Max) | Web (PUC Lua 5.1, WebGL 2 / 1) | Android emulator (LuaJIT) | Galaxy Z Fold7 (Adreno 830, LuaJIT JIT off) |
|---|---|---|---|---|
| fill 256×256 (65,536 `set_tile`) | 24.8 ms | 42 ms | 97.9 ms | 60.6 ms |
| of which MTXM→megatile decode only | 0.14 ms | 6 ms | 6.1 ms | 4.2 ms |
| fill 192×128 Desert map | 9.3–9.7 ms | 15–16 ms | 16.9–18.0 ms | 17.4–29.5 ms |
| fill 128×128 / 64×64 Install | 6.4 / 1.6 ms | 10 / 3 ms | 10.7 / 3.3 ms | 33.0 / 2.8 ms |
| clear rest of template (40,960 / 61,440 cells) | 15–16 / 23 ms | 20–21 / 30 ms | 25.6 / 39.7 ms | 24.9–41.0 / 37.2 ms |
| proxy load: Desert, corners template | 23–57 ms | 23–37 ms | 46.6–108 ms | 51.5–64.4 ms |
| proxy load: Desert, full template | 42.5 ms | 59–60 ms | 66.4 ms | 54.5 ms |
| `.kpd` load + CV5 table build | 1.8–3.2 ms | 2–3 ms | 3.5–12.6 ms | 2.9–8.6 ms |
| palette upload (256×1 RGBA `set_texture`) | 0.7–0.9 ms | 0.23–0.35 ms | **7.5 ms** | **8.1 ms** |
| cycling cost averaged per frame (1 upload / 8 frames) | 0.09–0.11 ms | 0.03–0.04 ms | 0.94 ms | 1.01 ms |

Other results:

- **Texture size.** The Desert tilesource texture is 4096×2048 L8 (8.4 MB `.texturec`); Installation is 2 MB.
- **Tile range.** All 5,810 Desert tiles are addressable in one tilesource.
- **Template file size.** The full template compiles to 852 KB; the corners template to 141 bytes.
- **Bounds.** `tilemap.get_bounds` returns 1,1,256,256 for every map size, both after the fill and after the corner cells are cleared.
  `set_tile` outside the template (x = 0 or x = 257) does not raise in Lua. The engine logs "supplied tile was out of range" and changes nothing.
  Cells never set stay empty and are not drawn, so a fresh corners template only needs the two corner cells cleared when they lie outside the map.
- **Exactness.** The top-left 640×480 of `desert_256` matched the offline render in 307,200 of 307,200 pixels, on both WebGL 2 and forced WebGL 1.
  macOS was checked visually during development. The window capture could not be repeated later because the display went to sleep (`screencapture` failed).
- **Frame time.** Average frame time at zoom 1 and with the whole 8192² map in view was the display interval: 8.7 ms on macOS at 120 Hz, 17.4 ms on web/Android at 60 Hz. The emulator rose to 19.9 ms with the whole map in view.
  This does not measure the GPU cost of the tilemap. Drawing the whole map needs `tilemap.max_tile_count = 262144`.

## Conclusion

- **One 256×256 corners template per tileset**, loaded fresh with the mission proxy:
  - fill only the map area (≤ 18 ms for real campaign maps on the emulator);
  - clear only the template corner cells that lie outside the map;
  - clamp the camera to the map size.

  Never reuse a filled template for a smaller map: clearing the remainder costs more than the fill.
- **The largest tileset (Desert, 5,810 megatiles) fits one 4096×2048 L8 tilesource.**
- **Palette cycling over the whole palette works** through a runtime 256×1 texture and the D10 palette pass, exact on WebGL 2 and WebGL 1.
  The upload is not cheap on a real GPU either: 8.1 ms per upload on an Adreno 830 phone with vsync on. [Spike 15](15-upload.md) shows it is a fixed per-call cost of Defold's Vulkan `set_texture`, not a size cost. The product pipeline therefore keeps the palette in a render constant (ADR 0005). This spike keeps its own palette texture.

## Not measured

- iOS and a mid-range Android device (the phone above is a flagship).
- GPU time of the tilemap and the palette pass.
- The native cadence of the palette-cycle driver, which the research leaves open.
- A retained macOS capture, for the reason given under Results.
