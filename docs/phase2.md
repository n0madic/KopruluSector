# Phase 2 progress

Plan: `IMPLEMENTATION_PLAN.md` §7 phase 2 (terrain + GRP + IScript). Started 2026-10-12. The design (image and sprite state in simulation, presentation sync, depth-ranked composition) was approved on 2026-10-12; its rendering part is recorded in ADR 0006 once implemented.

## Work items

| # | Plan item | Status | Where |
|---|---|---|---|
| 1 | Image and sprite pools (main 5000 + health 12 + circles 80 + ghosts 64 + boxes 64; sprites 2500 with row lists; lone thingies 500), `images.dat` initialisation, directions, LO offsets, bounds, depth key (plan item 4) | core done: pools, native lists, image init/overlays/release, directions, follow-main, bounds, LO, sprites (create, destroy, move, update, play, depth key), lone thingies with visibility; the selection-circle, health-bar and placement-preview allocators come with their materials (item 4) | `game/images/` |
| 2 | IScript VM: all 69 opcodes, no-op mode, RNG sources, consumer hooks for unit/flingy/bullet opcodes; checked against original-code traces of every image animation (plan item 4) | done for every opcode without a unit context: 2,917 stock cases + synthetic programs equal the original code; unit/flingy/bullet consumers are hooks bound in phase 3 | `game/images/iscript.lua`, `tools/research/probe_native_iscript_117.py` |
| 3 | Presentation sync: draw list by the native depth key, global ranks, one image-data texture for all meshes, interpolation, events ring (plan item 3) | done except the events ring (sounds) | `presentation/images.lua`, `presentation/backend/world_images.lua` |
| 4 | Materials for every draw method and the depth-ranked composition (windows, destination layers) (plan item 1) | composition done ([ADR 0006](adr/0006-depth-ranked-composition.md)); methods 0/1, 9, 10, 13 (identity colours for now), 14, 16, 17 drawn; 2–8, 11, 12, 15 to do | `render/materials/world*.{vp,fp}`, `render/koprulu.render_script` |
| 5 | Render script: palette cycling, brightness/gamma, grayscale, fade, fog hook (plan item 2) | todo | `render/koprulu.render_script` |
| 6 | Viewer animation mode: any image, every animation; animated units on the map (plan item 5) | animated map scene done (units through viewer hooks); single-image animation browser to do | `viewer/map_world.lua` |
| 7 | Acceptance: comparison with the full-game emulator's start frames | todo | |

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

[ADR 0006](adr/0006-depth-ranked-composition.md): the rank of each image in the native draw order becomes its depth. Overwriting images use depth test GREATER and write depth; destination images (shadow, blends, fire) are drawn in layers of non-overlapping images, each layer reading a fresh copy of the index target. All family meshes read one image-data texture.

**Check.** The viewer's HTML5 autotest logs the top-left view of all 64 missions at creation and after 48 ticks; `tools/viewer_compose_check.py` rebuilds every view on the CPU from the retail files with the original kernels. 128 of 128 views are byte-identical (`KOPRULU_VIEWER=1 KOPRULU_ORIGINAL=DIR tools/ci.sh`). The first run found a real defect: with 65,536 ranks the 16-bit WebGL depth buffer merged neighbouring ranks, so a shadow skipped the image just before it.

## Deferred (not phase 2)

- 60 FPS with 5000 animated images on a target mid-range phone and on iOS: no device available (as in phase 0).
