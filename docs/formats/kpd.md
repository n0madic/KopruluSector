# Game data files (`.kpd`)

Global data converted once per installation into `generated/data/`, using the [Koprulu container](container.md). Written by `python3 -m converter data --data DIR` (`converter/kpf/kpd.py`). Every file has the data-set source hash shared with the [mission files](kpm.md).

| File | Kind | Status |
|---|---|---|
| `dat.kpd` | `DAT ` | done |
| `strings.kpd` | `STRS` | done |
| `iscript.kpd` | `ISCR` | done |
| `aiscript.kpd` | `AISC` | done |
| `tilesets/<name>.kpd` | `TSET` | done |
| `grpmeta.kpd`, `palettes.kpd`, `fonts.kpd`, `ui.kpd`, `sounds.kpd` | | planned (plan §4.2) |

## `dat.kpd` (schema 1)

- **Reader:** `game/data/dat.lua`.
- **Tests:**
  - `converter/tests/test_dat.py`;
  - `tests/lua/test_dat.lua`.

All eleven DAT files of the generic loader `0x4442b0`:

| Table | Fourcc |
|---|---|
| flingy | `FLNG` |
| images | `IMAG` |
| mapdata | `MAPD` |
| orders | `ORDR` |
| portdata | `PORT` |
| sfxdata | `SFXD` |
| sprites | `SPRT` |
| techdata | `TECH` |
| units | `UNIT` |
| upgrades | `UPGR` |
| weapons | `WEAP` |

**Section payloads.** Each section is the effective file unchanged. A DAT file already is the column-major form the plan asks for: each column is `count` little-endian elements, columns back to back in descriptor order.

**Layout source.** The layout comes from `defs/dat/bw-1.17.0.1.json`. A test compares it with the descriptors in `StarCraft.exe`, including the zero terminator, and checks every effective file's size and SHA-256. The converter rejects a file whose size differs from the descriptor total, as the native loader does with fatal error 24.

**SC and BW data.** In 1.17 the archive set is the same in both game modes: patch_rt → BrooDat → StarDat → CD archive (atlas [S01](../research/atlas/s01.md)). The tables are therefore identical for original and Brood War campaigns. SC/BW differences are runtime rules keyed on the expansion flag `0x536720`, for example the `broodwar_only` columns and the Ultralisk armor bonus; they are not separate data.

**`COLS` section.** A self-describing column table, so the Lua reader carries no copy of the layout:
- u16 file count;
- per file: fourcc, u16 records, u16 column count;
- per column:
  - u8 element size;
  - u8 elements per id;
  - u16 element count;
  - u16 first id;
  - u32 byte offset in the file section;
  - u8 name length and the ASCII name (PyMS label).

**Subranges.** Some columns cover only part of the id range:

| Column | Range |
|---|---|
| `units.infestation` | from unit 106 |
| `units.addon_position` | 2 per unit from 106 |
| `units` sound columns | units 0–105 |
| `units.staredit_placement_size` | 2 per unit |
| `units.unit_extents` | 4 per unit |
| `sprites.health_bar`, `selection_circle_image`, `selection_circle_offset` | from sprite 130 |

**Runtime form.** `dat.parse` unpacks every column once into Lua arrays indexed by 0-based id, for example `t.units.hit_points[0]`. Several-per-id columns are flat: `column[id * per_id + k]`.

**Signedness.** Values are unsigned. Whether a field is signed depends on the consuming instruction (`movsx`/`movzx`), so the simulation applies it where it reads a column.

**Cost.** Unpacking all tables takes 0.33 ms with LuaJIT JIT on and 0.97 ms with JIT off. It costs about 0.5 MB of heap.

## `strings.kpd` (schema 1)

**Sections.** Each section is one runtime TBL file, byte for byte:

| Fourcc | File |
|---|---|
| `STAT` | `rez\stat_txt.tbl` |
| `NETW` | `rez\network.tbl` |
| `GLUA` | `rez\gluAll.tbl` |
| `GHST` | `rez\gluHist.tbl` |
| `TIPS` | `rez\tips.tbl` |
| `HELP` | `rez\help_txt.tbl` |

The raw table keeps everything the plan requires: the count, the offsets (including shared ones), allocated blocks, and the embedded NULs after the first substring (274 blocks in `stat_txt`). Control bytes are not normalised.

**Lookup.** Use `game/data/strings.lua`, which follows `0x4c13b0`:
- only the low 16 bits of the id are used;
- id 0 returns nil;
- an id past the count returns the empty string.

`strings.offset` returns the raw byte offset for consumers that read past the first NUL.

**Converter-only tables.** `images.tbl`, `sfxdata.tbl`, `portdata.tbl` and `mapdata.tbl` name MPQ paths. The converter uses them and does not ship them.

## `iscript.kpd` (schema 1)

- **Reader:** `game/data/scripts.lua`.
- **Tests:**
  - `converter/tests/test_scripts.py`;
  - `tests/lua/test_scripts.lua`.

**Sections:**

| Section | Content |
|---|---|
| `CODE` | `scripts\iscript.bin` unchanged. Program counters are byte offsets into it. |
| `HDRS` | u16 count, then `{u16 iscript id, u16 header offset}` sorted by id |

**`HDRS` source.** The table comes from the id index whose offset is the DWORD at file offset 0. The index holds WORD pairs and ends at id `0xffff`. The native search `0x40f340` returns the first match, so a repeated id keeps its first header; the stock index has no repeats (398 ids).

**Header layout.** A header starts with `SCPE`. Its type byte is at +4, and the animation PCs are u16 from +8 (atlas [P04](../research/atlas/p04.md)).

**Converter checks:**
- the index terminator is present;
- every header starts with `SCPE`.

## `aiscript.kpd` (schema 1)

| Section | Content |
|---|---|
| `AISC` | `scripts\aiscript.bin` unchanged, directory included |
| `BWSC` | `scripts\bwscript.bin` unchanged, directory included |

The directories are not rebuilt; the runtime resolves ids with the native rule `0x494d20`, implemented as `scripts.resolve_ai`:
- **directory location:** the DWORD at file offset 0;
- **AIScript records:** 16 bytes, `{id, offset, string, flags}`;
- **BWScript records:** 8 bytes, `{id, offset}`;
- both directories end at id 0;
- an AIScript match with a nonzero offset wins;
- a missing AIScript id or a zero offset falls through to BWScript, where a nonzero offset returns bin selector 1.

**Converter checks:**
- both directories terminate;
- every offset lies inside its file.

**Stock data:** 293 AIScript records and 156 BWScript records. All 177 distinct launch ids in the 68 campaign CHKs resolve (test).

## `tilesets/<name>.kpd` (schema 1)

One file per tileset, named by the `ERA & 7` order: badlands, platform, install, ashworld, jungle, desert, ice, twilight.

- **Reader and import:** `game/data/terrain.lua`.
- **Python twin:** `converter/derive/terrain.py`.
- **Tests:**
  - `converter/tests/test_terrain.py`;
  - `tests/lua/test_terrain.lua`.

| Section | Class | Content |
|---|---|---|
| `CV5 ` | canonical | CV5 unchanged: 52-byte groups, flags WORD at +2, 16 megatile WORDs at +20 |
| `VF4 ` | canonical | VF4 unchanged: 16 mini-cell flag WORDs per megatile (input of the later `WALK` producer) |
| `WPE ` | canonical | 256 × (R, G, B, 0). The fourth byte is not alpha. The runtime sends the RGB bytes as `kp_palette`. |
| `CYCL` | canonical | u8 range count, u8 speed (8), then (low, high) inclusive index pairs of the stock simple cycles ([palette bootstrap](../research/native-palette-bootstrap.md)) |
| `MFLG` | derived `MFL1` v1 | u32 megatile flags by the `0x445562` rules; the inputs hash is the SHA-256 of VF4 |

**Converter checks.** The converter verifies the record sizes, every CV5 → megatile reference and every VX4 → VR4 reference. VX4 and VR4 are used only to prerender the tilesource and are not shipped.

**Placement of palette data.** The plan lists WPE and the cycle ranges under `palettes.kpd`. They are keyed by tileset, so they live here; `palettes.kpd` will hold the remap and blend tables.

**Terrain import.** `terrain.import(mission, tileset)` implements `ImportMapTiles` `0x445720`. It returns the megatile id and the tile flags of every cell:
- tile flags start with low WORD `0xffff`;
- they add `(CV5 flags & 0xd8fa) << 16` and the megatile flags;
- a high tile bit sets id bit 15 and flag `0x400000`;
- a tile selecting a missing CV5 group stops the import.

**Verification.** The Python twin reproduces the native output hashes byte for byte:
- the megatile flags of all eight tilesets;
- the megatile ids and flags of all 68 campaign CHKs, 1,045,504 cells (`native-terrain-import-probes.json`).

The Lua version is tested against the Python twin.
