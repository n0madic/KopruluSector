# Phase 1 progress

> **Historical record.** Written before phase S ([phaseS.md](phaseS.md)), when the runtime was Defold with Lua. The Defold build server, `tools/native/build.sh` and `tools/spike.sh` named below were removed in phase S; the build is CMake (`CMakeLists.txt`, `tools/product.sh`).

Plan: phase 1 (converter, dependency analyzer, map viewer). Started 2026-10-12.

## Work items

| # | Plan item | Status | Where |
|---|---|---|---|
| 1a | CHK reader by the native loader tables (`VER` descriptors, required flags, payload rules, corpus allowlist, errors) | done | `converter/formats/chk.py`, `defs/chk/bw-1.17.0.1.json` |
| 1b | DAT by the native descriptors; SC/BW share one archive set, differences are runtime rules | done | `converter/formats/dat.py`, `defs/dat/bw-1.17.0.1.json` |
| 1c | TBL byte-exact | done: raw tables in `strings.kpd`, native lookup in Lua | `converter/kpf/kpd.py`, `game/data/strings.lua` |
| 1d | PCX, tileset, GRP | done: PCX (all palette tables by the native loaders, shapes checked); tileset (native terrain import byte-exact on all 68 maps); GRP and LO (all 833 files, native packet and image-geometry probes, [grpmeta](formats/kpd.md#grpmetakpd-schema-1)) | `converter/formats/`, `converter/derive/terrain.py` |
| 1e | FNT, dialog BIN, RIFF/WAV metadata, SMK portraits | done: FNT (4,460 native glyph blits), dialog BIN (102 native fixups), RIFF (60 native parser cases), SMK portraits (519 movies equal to smackw32) | `converter/formats/fnt.py`, `dialog.py`, `wav.py`, `smk.py` |
| 2a | `.kpm` writer, canonical sections | done | `converter/kpf/kpm.py`, [kpm format](formats/kpm.md) |
| 2b | `.kpd` writers | done: `dat`, `strings`, `iscript`, `aiscript`, `tilesets`, `palettes`, `fonts`, `ui`, `sounds`, `portraits`, `grpmeta` | [kpd formats](formats/kpd.md) |
| 2c | Derived sections `WALK`, `RGNS`, `CONT`, `PACK` | `WALK`, `RGNS`, `CONT` (native hashes 68/68 each, all synthetic research fixtures) and `PACK` (analyzer) done | `converter/derive/walk.py`, `regions.py`, `contours.py`, [kpm](formats/kpm.md) |
| 2d | `tools/dump` | done: readable decoders for every `.kpd` kind and every `.kpm` section | `python3 -m converter dump` |
| 3 | Dependency analyzer, M1 mission choice | done: AI closure equals the research inventory (11,497 instructions, 64 opcode types), triggers equal the manifest on 68 CHKs, IScript closure equals the research traversal; M1 = terran/terran01 | `converter/analyze/`, [analyzer](analyzer.md) |
| 4 | Defold resource generators | done: terrain (tilesources, tilemap templates, mission template collections), minimaps (`<mission>_minimap.png` by the native rules `0x46c920`/`0x46c990`/`0x46ca20` and palette map `0x46e260`), glyph and portrait atlases, GRP atlases per pack, resource packs, mission sound collections, viewer debug overlays and sprite probe | `converter/emit/`, [packs](formats/packs.md) |
| 5 | `game/data` loaders | done: `.kpm` (canonical and derived sections), strings, DAT SoA, scripts, tilesets with the terrain import, palettes, fonts, ui, sounds, portraits, `grpmeta`, override effects (`rules.lua`) and UNIT records (`unit_records.lua`) | `game/data/` |
| 6 | Reference harness | done: M0E probe adapter (phase 0) and the full-game emulator trace adapter (per-frame digests, sound requests, PNG hashes, input log; M1 Wasteland victory and all 64 no-input runs) | `tools/oracle/`, [golden oracle](golden-oracle.md) |
| 7 | Map viewer | done: terrain of any mission, camera, resize, mission switching, static units and doodads through the `mesh_texture` backend, packs loaded per `PACK`, minimap, walkability and region overlays | [viewer](../viewer/README.md) |

## CHK import (item 1a)

`python3 -m converter missions --data DIR` converts all 64 catalog scenarios in under a second. It writes:
- `generated/missions/<campaign>/<mission>.kpm`;
- `generated/reports/missions.json`, with section sizes, allowlisted tags and mission sounds per map, plus the four rejected prerelease scenarios.

Results on the 1.17.0.1 data:
- **64 catalog maps accepted:** 31 of version 59 and 33 of version 205.
- **Rejected:** the four physical `VER=57` scenarios (`protoss/tutorial`, `terran/terran07`, `terran/terran10`, `zerg/tutorial`), as the native preflight does.
- **No duplicate sections** in the corpus.
- **Every `VCOD`** equals the executable resource.
- **`MTXM`/`MASK` sizes** match the dimensions in every map.
- **WAV references.** 1,105 of the 1,106 resolve and read. The exception is `protoss10\...\PAB05ura.wav`, an archive entry with two undecompressable sectors; it is reported and treated as missing. (2026-11-11, issue #2: the reference copy's damage, not the release's; [campaigns](research/campaigns.md#damaged-archive-entry).)

The converter output is byte-identical across runs (test).

The loader tables in `defs/chk/` are compared with `StarCraft.exe` bytes by `converter/tests/test_chk.py`, which reads the PE through the stdlib-only `converter/pe.py`.

## Map viewer, terrain (items 4 and 7)

**Converter output.** `python3 -m converter terrain` writes:
- an L8 tilesource of all megatiles per tileset;
- a 256×256 tilemap template defined by its two corner cells;
- a mission template collection per tileset, holding the tilemap and `presentation/terrain.script`.

The fill runs inside the template collection because `tilemap.set_tile` only reaches components of the calling script's collection. The data comes through the shared module `presentation/terrain.lua`.

**Viewer.** The viewer (`viewer/`) is a settings overlay, so the game build in CI does not depend on `generated/`. For each mission it:
1. imports the terrain with `game/data/terrain.lua`;
2. loads the tileset template through its proxy;
3. fills the tilemap;
4. sends the WPE palette as `kp_palette`.

**Verification.**
- **HTML5 byte check:** every rendered megatile was compared with the converter's CRC of its 32×32 indices. 64 missions, 49,920 megatiles, 0 mismatches.
- **Visual check:** two missions in the browser, platform and badlands.
- **Performance:** load + import + fill per mission is 1 + 1–3.4 + ~26 ms on macOS.

## Map viewer, scene (item 7)

**Scene.** After the terrain, the viewer loads the mission's `PACK` packs through their collection factories (unloading the others) and builds the static scene with `presentation/viewer_scene.lua`:
- preplaced units (`UNIT`, start locations skipped) and `THG2` sprites and units;
- each as its main image, plus the turret of units with a subunit, in the static pose of `presentation/static_pose.lua` (IScript Init up to the first wait);
- placed by the native frame bounds rule `0x40ee70` and drawn through `presentation/backend/mesh_texture.lua` (removed after phase 2 replaced it; its atlas frame reader now lives in `world_images.lua`), one mesh per atlas, in depth order (elevation, map y, record order);
- player colours through the `tunit` remap rows.

Only normal draw functions (`images.dat` 0 and 1) are drawn here; shadows, blends and the other draw functions are phase 2 materials. Phase 2 replaced this static scene by the animated one of `viewer/map_world.lua` drawn by depth rank ([phase 2](phase2.md)); `viewer_scene.lua` and `static_pose.lua` were removed.

**Minimap and overlays.** The minimap is the converter's `<mission>_minimap.png` with the view rectangle; a click moves the camera. W and R toggle the walkability and region overlays (`generated/viewer/<campaign>_<mission>_{walk,regions}.png`).

**Verification** (`tools/spike.sh viewer web` with `viewer/autotest.settings`, also `KOPRULU_VIEWER=1 tools/ci.sh`). In each mission, every isolated static image of a probe animation fully inside the top-left view is read back from the index target and compared with its GRP frame's indices (`viewer/sprite_check.lua`, frames from `generated/viewer/sprite_probe.bin`; player colour indices 8–15 skipped).
- **Result on 2026-10-12:** 76 images in 18 missions, 6 of them from frames bob packed rotated, 38,340 pixels, 0 mismatches.
- **Bug it found:** the first version drew every frame upside down. The vertex y of `resource.get_atlas` geometries points down relative to their UVs.
- **Other runs:** all 64 scenes on macOS and HTML5 without errors (60–756 images per mission).
- **Visual check:** terran01 and zerg02 in the browser.

## Lua heap after loading the largest mission (acceptance)

`tools/lua/mission_heap.lua` loads a mission with its tileset, `dat.kpd` (SoA), `iscript.kpd`, `aiscript.kpd` and `strings.kpd`. It drops the container string, as the loader does, and compares the resident heap with the budget from spike 10:

> resident ≤ 1.1 × (loaded file bytes + unpacked DAT columns)

`tools/ci.sh` runs it on the largest mission under LuaJIT with the JIT off (the iOS mode) and under PUC Lua 5.1 (HTML5).

| Mission | Backend | Files | DAT SoA | Resident | Budget |
|---|---|---|---|---|---|
| expterran/terran07 (largest) | LuaJIT JIT off | 1,632 KB | 526 KB | 2,191 KB | 2,374 KB |
| expterran/terran07 | PUC Lua 5.1 | 1,632 KB | 1,000 KB | 2,704 KB | 2,896 KB |
| expterran/terran08 | LuaJIT JIT off | 1,600 KB | 526 KB | 2,158 KB | 2,338 KB |
| expzerg/zerg10 | LuaJIT JIT off | 1,548 KB | 526 KB | 2,107 KB | 2,281 KB |

**Peak.** The sampled peak (garbage not yet collected included) is about 2.2× the files, in line with spike 10's "2× until the file string is dropped".

**Measurement artefact.** LuaJIT keeps `io.read`'s temporary buffer and halves it at each full collection. The script therefore runs several collections before counting. In Defold the data comes from `sys.load_buffer`, so this buffer does not exist there.
