# Implementation status

The traceability ledger of `IMPLEMENTATION_PLAN.md` §6 and §8. Each implemented contract is linked to its research domain (R01–R37 of the [completion ledger](research/completion-ledger.md)), its evidence, the Koprulu code and tests, and the next gate it has not passed. State as of phase 2 (2026-10-12).

The marks are independent axes, not one scale (§8):
- `original-static`: data or instructions decoded;
- `original-isolated`: the original function or chain ran to its return on declared inputs;
- `implemented` / `fixture-verified`: Koprulu code exists and passes the named tests;
- `original-game-observed`: compared with a full-game observation of the original;
- `runtime-verified`: checked in running Defold;
- `approximation`: a deliberate difference with its ADR.

No mission has a gameplay status yet: the simulation of units, orders and triggers starts in phase 3.

## Data and conversion (phase 1)

| Contract | R | Evidence | Code and tests | Marks | Next gate |
|---|---|---|---|---|---|
| CHK loader tables, section rules, corpus allowlist | R03, R01 | [mission UNIT processing](research/native-mission-units.md), [overrides](research/native-map-overrides.md) | `converter/formats/chk.py`, `defs/chk/`; `converter/tests/test_chk.py` compares the tables with the executable | original-static, implemented, fixture-verified | initial mission state (phase 3) |
| DAT layouts and TBL lookup | R02 | [DAT schemas](research/dat-schemas.md) | `converter/formats/dat.py`, `game/data/dat.lua`, `strings.lua`; `test_dat`, `test_mission` | original-static, implemented, fixture-verified | consumers of each field, by phase |
| Terrain import (MTXM/CV5 → megatiles and tile flags) | R04 | [terrain import](research/native-terrain-import.md) | `converter/derive/terrain.py`, `game/data/terrain.lua`; `test_terrain`; viewer megatile CRCs | original-isolated (68 maps byte-exact), implemented, runtime-verified (HTML5: 49,920 megatiles), original-game-observed (192 emulator frames) | creep and creep edges (R24, phase 3): the only terrain difference in the captured frames |
| `WALK`, `RGNS`, `CONT` | R13 | [regions](research/native-region-generation.md) | `converter/derive/walk.py`, `regions.py`, `contours.py` | original-isolated (native hashes 68/68), implemented | pathing consumers (phase 3) |
| GRP/LO decoding, `grpmeta`, atlases and packs | R05, R01 | [image geometry](research/native-image-geometry.md), [pixel blitters](research/native-pixel-blitters.md) | `converter/formats/grp.py`, `lo.py`, `converter/emit/packs.py`, `game/data/grpmeta.lua`; [packs](formats/packs.md) | original-isolated, implemented, runtime-verified (sprite readback: 104 images, 84,557 pixels; pack check) | — |
| Palettes, PCX tables, tileset cycles | R06 | [pixel assets](research/native-pixel-blitters.md), [palette bootstrap](research/native-palette-bootstrap.md) | `converter/formats/pcx.py`, `palette.py`, `game/data/palettes.lua`; `test_palettes` | original-static, original-isolated (loaders), implemented, fixture-verified | — |
| FNT, dialog BIN, RIFF/WAV, SMK portraits | R07 | [phase 1](phase1.md) | `converter/formats/fnt.py`, `dialog.py`, `wav.py`, `smk.py`; `test_fonts`, `test_ui`, `test_sounds`, `test_portraits` | original-isolated (glyph blits, fixups, parser, smackw32 frames), implemented | interface and briefing rendering (phase 9) |
| IScript and AIScript closure, triggers | R11, R25, R30 | [analyzer](analyzer.md) | `converter/analyze/` | original-static | the VMs and trigger engine (phases 2, 5, 7) |

## Simulation core (phases 0–2)

| Contract | R | Evidence | Code and tests | Marks | Next gate |
|---|---|---|---|---|---|
| RNG (LCG, enable, ranges, per-source counters) | R09 | [PRNG](research/native-prng.md) | `game/simulation/rng.lua`; `test_rng` against vectors from the original functions | original-isolated, implemented, fixture-verified | consumers in the frame order (phase 3) |
| Step clock (speed table `0x503360`, catch-up) | R08 | [frame](research/native-frame.md) | `game/simulation/clock.lua`; `test_clock` | original-static, implemented, fixture-verified | the full frame phases (phase 3) |
| Event ring (D15) | R08, R35 | plan D15 | `game/simulation/events.lua`; `test_events`; the viewer drains IScript sounds | implemented, fixture-verified | sound playback by the native channel manager (R35, phase 9) |
| Image, sprite and lone-thingy pools, native lists, creation, update, depth key | R05, R10 | [image geometry](research/native-image-geometry.md), decompilation of `0x4119a0`/`0x411930`/`0x411900`/`0x411970` | `game/images/`; `test_images`, `test_iscript_golden` | original-isolated (every animation, through the state hash), implemented, fixture-verified | units owning sprites (phase 3) |
| IScript VM, all 69 opcodes | R11 | `tools/research/probe_native_iscript_117.py` | `game/images/iscript.lua`; `test_iscript_golden`: 2,917 stock cases and synthetic programs equal the original | original-isolated, implemented, fixture-verified | unit, flingy and bullet opcodes and the no-op mode against full-game traces (phase 3) |
| Cloak initialisers and callbacks (`0x40ed00`, `0x40ed30`, `0x40e570`, `0x40e5f0`, `0x40e650`) | R06 | [pixel effects](research/native-pixel-effects.md) | `game/images/image.lua`; `test_images` | original-isolated (callbacks), implemented, fixture-verified | when units cloak (R14/R19) |
| Health-bar and circle allocators, pool records (`0x40eb60`, `0x40f6e0`, `0x40f540`, `0x40ec90`, `0x411460`, `0x40f810`) | R05, R34 | [health bars](research/native-health-bars.md) (all but `0x40ec90`/`0x411460` executed), decompilation | `game/images/selection.lua`; `test_selection` | original-isolated (research), implemented, fixture-verified | the selection state that decides when they exist (`0x415590`, phase 3) |

## Presentation and rendering (phases 0–2)

| Contract | R | Evidence | Code and tests | Marks | Next gate |
|---|---|---|---|---|---|
| Native draw list: bounds refresh, depth key order, tail-to-head images, drawn rows, vision | R05 | [image geometry](research/native-image-geometry.md), `0x4165d0`, `0x415b30`, `0x445de0`, `0x416a00` | `game/images/sprite.lua` (`refresh_bounds`, end of tick), `presentation/draw_list.lua`; `test_draw_list`; `tools/compose_capture.py` | implemented, fixture-verified, original-game-observed (192 frames of all 64 missions equal the original's screen) | — |
| Draw methods 0–17 and their kernels, trans50 colour rows (`0x416bd0`) | R06 | [pixel blitters](research/native-pixel-blitters.md), [pixel effects](research/native-pixel-effects.md), [health bars](research/native-health-bars.md) (`0x416bd0`) | `render/materials/world*.fp`, `presentation/world_lookups.lua`; `tools/viewer_compose_check.py` | original-isolated (kernels), implemented, runtime-verified (268 GPU views equal the CPU kernels), original-game-observed (captured frames) | — |
| Composition by depth rank, destination layers, distortion windows | R06 | [ADR 0006](adr/0006-depth-ranked-composition.md) | `render/koprulu.render_script`, `presentation/backend/` | implemented, runtime-verified; approximation at the view edges and, degraded at plan time, past the render command budget ([ADR 0006](adr/0006-depth-ranked-composition.md)) | mobile performance (no device) |
| Health, shield, energy and timer bars (`0x464d20`) | R06, R34 | [health bars](research/native-health-bars.md), `tools/research/probe_native_health_bar_117.py` | `presentation/health_bar.lua`, `bars.lua`; `test_health_bar`: 207 unit types, 13,846 cases equal the original | original-isolated, implemented, fixture-verified, runtime-verified | unit values from the simulation (phase 3) |
| Palette: cycles, brightness, grayscale, fade | R06 | [palette bootstrap](research/native-palette-bootstrap.md) | `presentation/palette.lua`; `test_palette` (gamma CRC of all 81 levels) | original-isolated (cycles, gamma, grayscale), original-static (fade timing), implemented, fixture-verified | the pause and mission flow that trigger grayscale and fades |
| Index target, palette pass, scaling | R06 | [ADR 0005](adr/0005-render-pipeline-clock-data.md) | `render/` | implemented, runtime-verified | fog of war in the `kp_fog` slot (R14, phase 3) |
