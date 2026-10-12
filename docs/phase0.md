# Phase 0 results

Status: complete on the platforms available, 2026-10-12. Plan: `IMPLEMENTATION_PLAN.md` §7 phase 0.

- **Platforms measured:**
  - macOS (Apple M4 Max, Vulkan/Metal, LuaJIT);
  - HTML5 (headless Chrome, WebGL 2 and forced WebGL 1, PUC Lua 5.1 in wasm);
  - the Android emulator (android-36 arm64, software Vulkan, LuaJIT);
  - a physical phone, Galaxy Z Fold7 (Snapdragon 8 Elite, Adreno 830, Android 16, Vulkan): all spikes re-run on 2026-10-12. Defold's Android engine runs LuaJIT with the JIT off, so this is an interpreter measurement, as on iOS;
  - command-line LuaJIT with JIT on and off, as the iOS interpreter mode;
  - PUC Lua 5.1.5.
- **Not available:** iOS devices or simulator runtimes, a mid-range Android device. Emulator numbers are functional checks, not device budgets.

## Items

| # | Item | Result | Where |
|---|---|---|---|
| 1 | Defold project, layout, bootstrap, CI | Defold 1.13.2 pinned. `game.project` with the Koprulu render pipeline and component limits. `tools/ci.sh`: pytest, Lua suites on four backends, bob build, headless Defold run. | [ADR 0001](adr/0001-toolchain-and-test-backends.md) |
| 2 | Converter skeleton: StormLib via ctypes, read from StarDat | `converter/mpq/`, profile `defs/profile/bw-1.17.0.1.json` (exe + 5 MPQ hashes), resolver Patch_rt → BrooDat → StarDat → BroodWar → StarCraft, CLI `python3 -m converter verify/extract`. | `converter/` |
| 3 | Koprulu container: Python writer, Lua loader, round trip, `custom_resources` + `sys.load_buffer_async` | Format spec, writer, reader with version/profile/data-set/derived checks, round-trip tests, async loading in Defold on all platforms. | [container](formats/container.md) |
| 4 | Presentation backend benchmark (2000/3500/5000 images) | `mesh_texture` chosen. The final version (tick-only writes, GPU interpolation) costs 0.35 ms mean, 2.22 ms p95 per frame on a phone running the interpreter at 5000 images. `sprite_go` is for small sets; `mesh_batch` is rejected. Byte parity across all backends. | [spike 04](spikes/04-backend.md), [ADR 0002](adr/0002-presentation-backend.md) |
| 5 | Exact frame selection via flipbook + `cursor` | `cursor = (k + 0.5)/n` is exact for 705 frames, including a 391-frame flipbook; `k/n` is not. | [spike 05](spikes/05-cursor.md) |
| 6 | Palette composition (D10) | Index target, source remap, destination lookups, in-place distortion, threshold, warp texture and outline match the original **byte for byte**: 8,117 blitter and 720 effect cases on WebGL 2. They also run on Vulkan and the Android emulator. | [spike 06](spikes/06-palette.md), [ADR 0005](adr/0005-render-pipeline-clock-data.md) |
| 7 | Atlases of one race; minimal graphics base | Terran: 3,510 frames, one 2048² L8 page. Clean bob build 4.6 s, archive 7.2 MB. No paged atlases. Minimal base GLES 3 / WebGL 2. | [spike 07](spikes/07-atlas.md), [ADR 0003](adr/0003-atlases-and-graphics-base.md) |
| 8 | Resource packs: collection factory "Load Dynamically" vs proxy | Both keep race atlases out of memory before load and after unload, on all three platforms. Factories for race packs, proxies for mission collections. | [spike 08](spikes/08-packs.md), [ADR 0004](adr/0004-resource-packs.md) |
| 9 | Tilemap: 256×256 template, 5,810 megatiles, palette cycling | 256×256 fill: 25 ms (macOS), 42 ms (web), 98 ms (emulator). Desert fits one texture. Terrain pixel-exact against an offline render. Whole-palette cycling. | [spike 09](spikes/09-tilemap.md) |
| 10 | Data access, async loading, compression, heap | Strings and SoA tables; buffer streams never from Lua. Archive compresses custom resources. Heap ≈ file size resident, 2× transient. | [spike 10](spikes/10-data.md) |
| 11 | Sound: pack memory, `sound.play`, music streaming, mission sound collection | OGG packs, streaming off: Defold's streamed read breaks compressed archive entries. `sound.play` 0.01–0.04 ms. Engine limits are in `game.project`. | [spike 11](spikes/11-sound.md) |
| 12 | Binary save files on every platform, including HTML5 | `io` files work everywhere and persist on HTML5 after a `sys.save` flush. | [spike 12](spikes/12-save.md) |
| 13 | Adaptive screen | Resize and rotation on the fly. Integer-nearest and sharp-bilinear modes. Anchored GUI survives resize. | [spike 13](spikes/13-screen.md) |
| 14 | Clock and suspend | Wall-time-driven clock with bounded catch-up. Pause/resume on focus events with a reset of the wall reference. Engine `dt` is unusable for catch-up. | [spike 14](spikes/14-clock.md) |
| 15 | Numeric core | Exact x86 integer arithmetic; game, CRT and portrait RNGs matching the original vectors; native free-list order; structural save/load; identical fingerprint on all four Lua backends. | [ADR 0001](adr/0001-toolchain-and-test-backends.md) |
| 16 | Contracts and golden adapter | M0E manifest E0–E4 with hashes, inputs, scope, Rxx and atlas links. The Extractor completion trace was produced with `sc_emulator`. | [golden oracle](golden-oracle.md) |
| 17 | ADRs | ADR 0001–0005 and this summary. | `docs/adr/` |
| — | Texture upload cost (added after the device run) | Defold's Vulkan `set_texture` costs 1.6–3.3 ms mean, up to 6 ms, per call on Adreno 830, independent of size. Rule: at most one upload per frame; palette as a render constant. | [spike 15](spikes/15-upload.md) |

## Acceptance (plan §7 phase 0)

- **Presentation backend chosen:** `mesh_texture`, [ADR 0002](adr/0002-presentation-backend.md).
- **Minimal graphics base fixed:** GLES 3 / WebGL 2, [ADR 0003](adr/0003-atlases-and-graphics-base.md). This is also the answer to §12 question 1; the user may override it.
- **Paged atlases decided:** not used.
- **Resource-pack mechanism chosen:** collection factories with dynamic loading, proxies for mission collections ([ADR 0004](adr/0004-resource-packs.md)).
- **World scaling chosen:** integer nearest when the scale is ≥ 2, sharp bilinear otherwise. The visible-area policy remains §12 question 3.
- **Measurements:** every spike has measurements on macOS, HTML5 and the Android emulator.
- **Numeric core** verified on LuaJIT (JIT on/off), PUC Lua 5.1 and Defold.
- **M0E baseline and reference traces** named in `tests/golden/oracle/m0e/manifest.json`.

## Device run (Galaxy Z Fold7, 2026-10-12)

The first device run invalidated three phase-0 results. All three were fixed and re-verified on the device the same day.

- **`mesh_texture` over budget.**
  - The first version took 4.5 ms per frame at 5000 images.
  - The final version: tick-only diff writes, positions interpolated in the vertex shader, the upload deferred to the next frame. It takes **0.35 ms mean and 2.22 ms p95** per frame; the worst frame is 3.79 ms.
  - The other backends stay slower: `sprite_go` 5.7 ms, `mesh_batch` 11.1 ms.
  - The index target is byte-identical across all three backends on WebGL 2 ([spike 04](spikes/04-backend.md), [ADR 0002](adr/0002-presentation-backend.md)).
  - The old "unexplained `mesh_batch` difference" was uninitialised `buffer.create` memory, as was a flake found during the rework. Every buffer is now written in full at creation.
- **Texture uploads cost 1–3.4 ms per call on mobile Vulkan, whatever the size** ([spike 15](spikes/15-upload.md)). The palette is now a render constant (`palette_colors[256]`, message `kp_palette`):
  - 0.1 ms per palette change on the phone;
  - byte-exact on WebGL 2: every pixel equals `palette[index]`, for all 256 indices and two palettes ([ADR 0005](adr/0005-render-pipeline-clock-data.md)).
- **Sound streaming breaks loading.**
  - Root cause: Defold's streamed partial read skips archive decompression. The 25 sounds bob stored LZ4-compressed fail, and the engine aborts on spawn.
  - `game.project` now sets `sound.stream_enabled = 0` and the sound limits. With these product settings all packs load and play on the phone ([spike 11](spikes/11-sound.md), [ADR 0004](adr/0004-resource-packs.md)).
- **Confirmed on the device:**
  - every palette and effect case draws;
  - atlas layouts equal the emulator's;
  - packs are absent before load and after unload;
  - saves round-trip;
  - both scaling modes run;
  - engine `dt` after a stall is 0.008 s;
  - Android runs LuaJIT with the JIT off, as iOS will.

## Open after phase 0

1. **No iOS or mid-range Android measurements.** One flagship Android phone was measured. Still unmeasured:
   - the §10 budgets on iOS and a mid-range device;
   - audio output latency;
   - the OpenGL ES backend (Defold picks Vulkan).

   Needs at least one iPhone and one mid-range Android device (§12 question 4).
2. **Destination-image run splitting.** Exact interleaving in depth order needs one copy per run. Closed in phase 2 by composition by depth rank ([ADR 0006](adr/0006-depth-ranked-composition.md)): destination layers and distortion windows, checked against the original renderer's frames.
3. **Native byte check on macOS** (`spikes/06-palette/native_check.py`) cannot be byte-exact with `screencapture`.
   - With the display awake the capture works: 0 of 8 tiles match.
   - The capture is colour-converted to the display profile, which is lossy: the 0, 1, 2, … baseline ramp reads back as 58, 59, 60, 61, 62, 62, 63, ….
   - An exact native check needs an engine-side readback or a ScreenCaptureKit capture in the window's colour space. The GPU result itself is byte-checked on WebGL 2.
4. **Product questions remain open:**
   - §12.1, already answered technically (see Acceptance);
   - §12.2, videos;
   - §12.3, visible-area policy;
   - §12.4, minimum devices;
   - §12.5, iOS self-build.
5. **Why the Android emulator's archive loaded streamed sounds** was not established. It no longer matters with streaming off.
6. **Not measured:** palette-cycle wall-clock cadence (research gap noted in spike 09), sound release after unload (RSS does not drop), GPU memory.
