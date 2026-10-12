# Phase 2 progress

Plan: `IMPLEMENTATION_PLAN.md` §7 phase 2 (terrain + GRP + IScript). Started and completed 2026-10-12. The design (image and sprite state in simulation, presentation sync, depth-ranked composition) was approved on 2026-10-12; its rendering part is [ADR 0006](adr/0006-depth-ranked-composition.md).

## Work items

| # | Plan item | Status | Where |
|---|---|---|---|
| 1 | Image and sprite pools (main 5000 + health 12 + circles 80 + ghosts 64 + boxes 64; sprites 2500 with row lists; lone thingies 500), `images.dat` initialisation, directions, LO offsets, bounds, depth key (plan item 4) | done: pools with the health-bar and box records, native lists, image init/overlays/release, directions, follow-main, bounds, LO, sprites, lone thingies with visibility, cloak initialisers, health bars and selection circles; when selection creates them is phase 3 | `game/images/` |
| 2 | IScript VM: all 69 opcodes, no-op mode, RNG sources, consumer hooks for unit/flingy/bullet opcodes; checked against original-code traces of every image animation (plan item 4) | done for every opcode without a unit context: 2,917 stock cases + synthetic programs equal the original code; unit/flingy/bullet consumers are hooks bound in phase 3 | `game/images/iscript.lua`, `tools/research/probe_native_iscript_117.py` |
| 3 | Presentation sync: draw list by the native depth key, global ranks, one image-data texture for all meshes, interpolation, events ring (plan item 3) | done; the events ring (D15) carries IScript sounds, played by the phase 9 audio manager | `presentation/`, `game/simulation/events.lua` |
| 4 | Materials for every draw method and the depth-ranked composition (windows, destination layers) (plan item 1) | done: all 18 methods ([ADR 0006](adr/0006-depth-ranked-composition.md)) | `render/materials/world*.{vp,fp}`, `presentation/health_bar.lua` |
| 5 | Render script: palette cycling, brightness/gamma, grayscale, fade, fog hook (plan item 2) | done | `presentation/palette.lua`, `render/koprulu.render_script` |
| 6 | Viewer animation mode: any image, every animation; animated units on the map (plan item 5) | done | `viewer/browser.lua`, `viewer/map_world.lua` |
| 7 | Acceptance: comparison with the full-game emulator's start frames | done | `tools/compose_capture.py`, `tools/research/sc_emulator/capture.py` |

## IScript VM (items 1 and 2)

`game/images/` keeps the image, sprite and lone-thingy records of the original as SoA arrays with the native list helpers (decompiled `0x4119a0`/`0x411930`/`0x411900`/`0x411970`, including their edge behaviour). `iscript.lua` is `RunIScript 0x410480` over the unchanged `iscript.bin`. Opcodes that touch a unit, flingy or bullet call hooks; where the original dereferences a null context the VM raises an error naming the opcode.

**Oracle.** `tools/research/probe_native_iscript_117.py` runs the original 1.17 code in Unicorn on the scenario of `tests/lua/iscript_trace.lua`:
- new-game pools and `InitThingyPools`;
- a lone sprite showing the case's image, then its animation;
- 80 calls of `UpdateThingies`, all original code (interpreter, overlays, sprite spawns, thingy visibility). Only positional sound and the unit-type fit test are recorders.

The state after every frame (records, free heads, RNG state, total and per-source counters, sound and fit events) is folded into a hash; `tests/golden/oracle/iscript/cases.lua` keeps one hash per case and no retail bytes.

**Results** (`tests/lua/test_iscript_golden.lua`, needs `generated/data`):
- all 2,917 image animations of the retail script equal the original, including the 574 cases that end where the original dereferences a missing unit, flingy or bullet context and the 4 that loop without a wait;
- synthetic programs appended to the script cover what stock animations never reach without a unit: `switchul`, `highsprol`, `lowsprul`, `grdsprol`, `return`, the one-byte no-ops, `playframtile` beyond the GRP, `waitrand` and `playsndbtwn` with reversed ranges;
- negative controls: `waitrand` off by one gives 162 differing cases, a wrong RNG source 322.

LuaJIT runs every case in about 0.4 s; the interpreters run every eighth.

**Not covered yet:** the unit-context opcodes (`attack*`, `turn*`, `sigorder`, `move`, `castspell`, `nobrk*`, `ignorerest`, range and arc jumps, gas overlays, damage) and the no-op mode used by walking-speed sampling. They need units; phase 3 checks them with the full-game emulator traces.

## Composition (items 3 and 4)

[ADR 0006](adr/0006-depth-ranked-composition.md): the rank of each image in the native draw order becomes its depth. Overwriting images use depth test GREATER and write depth; destination images (shadow, blends, fire, cloak blends) are drawn in layers of non-overlapping images, each layer reading a fresh copy of the index target; distortion images (cloak, EMP) read a copy of everything ranked before them. All family meshes read one image-data texture.

**Draw methods** (the kernels of `docs/research/native-pixel-blitters.md` and `native-pixel-effects.md`):
- 2–4, 8: distortion by brightness (`0x445f30`) or raw index, with the native linear wrap, and the cloak threshold pass (signed compare against the parameter byte, owner colour map);
- 5–7: trans50 blend, then the threshold; 9, 10, 17: blend, shadow and fire tables. trans50 rows 8..15 take the player colours of the last drawn sprite whose main image uses 5..7 (`0x416bd0`);
- 11: health, shield, energy and timer bars, `presentation/health_bar.lua`, a port of `0x464d20` checked against the original code for all 207 unit types with a bar (13,846 cases: hit point, shield, energy and timer levels, owners, hallucination, crops; `tools/research/probe_native_health_bar_117.py`);
- 12: warp texture frame `param` at the frame's own pixel positions; 13: tselect rows on indices 1..8; 15: frame edges in the consumer's line colour.

The cloak initialisers (`0x40ed00`, `0x40ed30`) and the selection allocators (`0x40eb60`/`0x40f6e0`/`0x40f540` health bars, `0x40ec90`/`0x411460` circles, the pool records of `0x40f8b0`) are in `game/images/`.

**Check.** The viewer's HTML5 autotest logs the top-left view of all 64 missions: at creation, after 48 ticks, and after putting the view's units under the runtime draw effects (`viewer/effect_scene.lua`) at 12 and 40 more ticks; plus twelve images of the animation browser. `tools/viewer_compose_check.py` rebuilds every view on the CPU from the retail files with the original kernels (distortions in place, in the original's traversal order). All 268 views are byte-identical (`KOPRULU_VIEWER=1 KOPRULU_ORIGINAL=DIR tools/ci.sh`). Defects found by it:
- with 65,536 ranks the 16-bit WebGL depth buffer merged neighbouring ranks, so a shadow skipped the image just before it;
- reading a render constant buffer's field returns a copy, so the lookups (and the tick interpolation) were never set;
- Defold 1.13 records at most 1024 render commands per frame; with 23 composition windows the last window was dropped. Copies now change only the render target, and the render script keeps a command budget.

## Palette, events and the browser (items 3, 5, 6)

- `presentation/palette.lua`: the stock cycles of `tileset.kpd` (`CYCL`) run by the original's driver `0x4ccd30` (two passes, BYTE counters, right rotations) on real time: the original steps it whenever its tick counter advanced more than 9 ms. Brightness 60–140 applies `trunc(256·pow(c/256, (200−b)/100))` per channel (the CRC of all 81 tables matches the original's measured outputs); grayscale truncates each channel; a fade shows its start palette until 10 ms after it began, then the target, as the original's single wait does.
- The render script has a `kp_fog` pass slot in index space after the world images (the fog comes with visibility, phase 3).
- `game/simulation/events.lua` is the D15 ring; the viewer scene emits IScript sounds into it and drains it every frame. Playing them needs the native 8-channel manager (phase 9).
- `viewer/browser.lua` shows any image in any animation and direction (keys A, `[ ]`, `; '`, `, .`, space); `iscript.kpd` schema 2 carries the animation slots per header type for it.

## Acceptance (item 7)

`tools/research/sc_emulator/capture.py` captures frames of the full-game emulator. The draw procedure `0x445ea0` runs with its terrain redraw flag and the whole 16×16 dirty grid set; at the call of `DrawSortedSprites 0x415b30` the screen holds the terrain, and right after it (before the fog pass, the platform stars and the interface) the screen, the image and sprite records, rows, camera, vision byte, megatile array and remap records are stored. `tools/compose_capture.py` rebuilds each frame: the records become a `game/images` world, `presentation/draw_list.lua` orders it for the captured view, and the CPU kernels of the viewer check draw the images over the original's terrain; every pixel of the 640×400 game viewport must equal the original's screen. Separately, the terrain drawn from the captured megatile array is compared with the original's.

**Result** (2026-10-12): all 64 campaign missions at game frames 1, 300 and 600, 192 frames with 7,903 drawn images: composed over the original's terrain, every pixel of the 640×400 viewport equals the original's screen in every frame. The terrain drawn from the megatile array equals the original's in 127 frames; the other 65 are frames of the 23 missions with Zerg creep, where the original draws creep edges over the tiles (phase 3).

The first run found a defect: trans50 rows 8..15 follow the player colours of the last sprite drawn with a cloak blend main image (`0x416bd0`), so a Dark Templar's player-colour pixels blended through other rows than the stock table; 12 Protoss frames differed until the draw list carried that state.

Capture: `python -m sc_emulator campaigns --capture 1,300,600 --frames 602 --out-root DIR` (from `tools/research`, private venv); compare: `KOPRULU_CAPTURES=DIR KOPRULU_ORIGINAL=DIR tools/ci.sh` or `tools/compose_capture.py --data DIR --root DIR`.

## Deferred (not phase 2)

- Unit-context IScript opcodes and the no-op mode: phase 3, against the full-game emulator traces.
- When selection circles and health bars exist (sprite selection flags, `0x415590`, the dashed circles `0x23b`), with the unit values a bar shows: phase 3. Placement boxes (method 15 with their own frame sizes and the consumer's line colour `0x6d237a`): interface phase.
- Creep edges over the terrain (the only terrain difference in the captured frames) and the fog material in the `kp_fog` slot: phase 3 (creep and visibility state).
- Sound playback of the event ring: the native 8-channel manager, phase 9. The grayscale and fades are driven by the pause and mission flow of later phases.
- Player colours from `COLR`: phase 3 player setup.
- Advanced (pattern) palette cycles: no stock tileset defines one; the driver handles simple rotations only.
- View edges differ from the original by design (ADR 0006): the view is the target, the original clips to 640×400 and wraps in 640×480.
- Past about 45 distortion windows in one view the render script's command budget merges the rest (reported); exact composition beyond that needs fewer commands per window.
- 60 FPS with 5000 animated images on a target mid-range phone and on iOS: no device available (as in phase 0).
