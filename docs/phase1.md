# Phase 1 progress

Plan: `IMPLEMENTATION_PLAN.md` §7 phase 1 (converter, dependency analyzer, map viewer). Started 2026-10-12.

## Work items

| # | Plan item | Status | Where |
|---|---|---|---|
| 1a | CHK reader by the native loader tables (`VER` descriptors, required flags, payload rules, corpus allowlist, errors) | done | `converter/formats/chk.py`, `defs/chk/bw-1.17.0.1.json` |
| 1b | DAT by the native descriptors; SC/BW share one archive set, differences are runtime rules | done | `converter/formats/dat.py`, `defs/dat/bw-1.17.0.1.json` |
| 1c | TBL byte-exact | done: raw tables in `strings.kpd`, native lookup in Lua | `converter/kpf/kpd.py`, `game/data/strings.lua` |
| 1d | PCX, tileset, GRP | tileset done (reference checks; native terrain import byte-exact on all 68 maps); PCX, GRP to be completed | `converter/formats/`, `converter/derive/terrain.py` |
| 1e | FNT, dialog BIN, RIFF/WAV metadata, SMK portraits | done: FNT (4,460 native glyph blits), dialog BIN (102 native fixups), RIFF (60 native parser cases), SMK portraits (519 movies equal to smackw32) | `converter/formats/fnt.py`, `dialog.py`, `wav.py`, `smk.py` |
| 2a | `.kpm` writer, canonical sections | done | `converter/kpf/kpm.py`, [kpm format](formats/kpm.md) |
| 2b | `.kpd` writers | `dat`, `strings`, `iscript`, `aiscript`, `tilesets`, `palettes`, `fonts`, `ui`, `sounds`, `portraits` done; `grpmeta` todo | [kpd formats](formats/kpd.md) |
| 2c | Derived sections `WALK`, `RGNS`, `CONT`, `PACK` | `WALK`, `RGNS`, `CONT` (native hashes 68/68 each, all synthetic research fixtures) and `PACK` (analyzer) done | `converter/derive/walk.py`, `regions.py`, `contours.py`, [kpm](formats/kpm.md) |
| 2d | `tools/dump` | done for the container and `MISN` sections | `python3 -m converter dump` |
| 3 | Dependency analyzer, M1 mission choice | done: AI closure equals the research inventory (11,497 instructions, 64 opcode types), triggers equal the manifest on 68 CHKs, IScript closure equals the research traversal; M1 = terran/terran01 | `converter/analyze/`, [analyzer](analyzer.md) |
| 4 | Defold resource generators | terrain done (tilesources, tilemap templates, mission template collections); atlases, packs, mission resource collections, minimaps todo | `converter/emit/terrain.py` |
| 5 | `game/data` loaders | `.kpm`, string tables, DAT SoA, IScript/AI scripts, tilesets + terrain import done | `game/data/mission.lua`, `strings.lua`, `dat.lua`, `scripts.lua`, `terrain.lua` |
| 6 | Reference harness | done: M0E probe adapter (phase 0) and the full-game emulator trace adapter (per-frame digests, sound requests, PNG hashes, input log; M1 Wasteland victory and all 64 no-input runs) | `tools/oracle/`, [golden oracle](golden-oracle.md) |
| 7 | Map viewer | terrain of any mission, camera, resize, mission switching; units/doodads, packs, minimap todo | [viewer](../viewer/README.md) |

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
- **WAV references.** 1,105 of the 1,106 resolve and read. The exception is `protoss10\...\PAB05ura.wav`, a retail archive entry with two undecompressable sectors; it is reported and treated as missing.

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
