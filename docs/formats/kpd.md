# Game data files (`.kpd`)

Global data converted once per installation into `generated/data/`, using the [Koprulu container](container.md). Written by `python3 -m converter data --data DIR` (`converter/kpf/kpd.py`). Every file has the data-set source hash shared with the [mission files](kpm.md).

| File | Kind | Status |
|---|---|---|
| `dat.kpd` | `DAT ` | done |
| `strings.kpd` | `STRS` | done |
| `iscript.kpd`, `aiscript.kpd`, `grpmeta.kpd`, `palettes.kpd`, `fonts.kpd`, `ui.kpd`, `sounds.kpd`, `tilesets/<name>.kpd` | | planned (plan §4.2) |

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
