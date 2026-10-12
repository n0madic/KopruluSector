# Phase 1 progress

Plan: `IMPLEMENTATION_PLAN.md` §7 phase 1 (converter, dependency analyzer, map viewer). Started 2026-10-12.

## Work items

| # | Plan item | Status | Where |
|---|---|---|---|
| 1a | CHK reader by the native loader tables (`VER` descriptors, required flags, payload rules, corpus allowlist, errors) | done | `converter/formats/chk.py`, `defs/chk/bw-1.17.0.1.json` |
| 1b | DAT by the native descriptors; SC/BW share one archive set, differences are runtime rules | done | `converter/formats/dat.py`, `defs/dat/bw-1.17.0.1.json` |
| 1c | TBL byte-exact | done: raw tables in `strings.kpd`, native lookup in Lua | `converter/kpf/kpd.py`, `game/data/strings.lua` |
| 1d | PCX, tileset, GRP | phase-0 decoders, to be completed | `converter/formats/` |
| 1e | FNT, dialog BIN, RIFF/WAV metadata, SMK portraits | todo | |
| 2a | `.kpm` writer, canonical sections | done | `converter/kpf/kpm.py`, [kpm format](formats/kpm.md) |
| 2b | `.kpd` writers | `dat`, `strings` done; `iscript`, `aiscript`, `grpmeta`, `palettes`, `fonts`, `ui`, `sounds`, `tilesets` todo | [kpd formats](formats/kpd.md) |
| 2c | Derived sections `WALK`, `RGNS`, `CONT`, `PACK` | todo | |
| 2d | `tools/dump` | done for the container and `MISN` sections | `python3 -m converter dump` |
| 3 | Dependency analyzer, M1 mission choice | todo | |
| 4 | Defold resource generators | todo (spike generators exist in `converter/emit/`) | |
| 5 | `game/data` loaders | `.kpm`, string tables, DAT SoA done | `game/data/mission.lua`, `strings.lua`, `dat.lua` |
| 6 | Reference harness | todo | |
| 7 | Map viewer | todo | |

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
- **All 1,106 WAV references** resolve to files in the campaign archives.

The converter output is byte-identical across runs (test).

The loader tables in `defs/chk/` are compared with `StarCraft.exe` bytes by `converter/tests/test_chk.py`, which reads the PE through the stdlib-only `converter/pe.py`.
