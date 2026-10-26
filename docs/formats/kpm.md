# Mission file (`.kpm`, kind `MISN`)

One file per campaign catalog scenario: `generated/missions/<campaign>/<mission>.kpm`, for example `terran/terran01.kpm`. The file uses the [Koprulu container](container.md).

- **Writer:** `converter/kpf/kpm.py`, run with `python3 -m converter missions --data DIR`.
- **Runtime reader:** `native/kp_core/src/core/kp_mission.c`.
- **Dump:** `python3 -m converter dump FILE`.
- **Tests:** `converter/tests/test_chk.py`, `converter/tests/test_kpm.py`, `converter/tests/test_derive.py`, `tests/c/test_data.c`.

Schema version 1. All integers are little-endian.

## Header

| Field | Value |
|---|---|
| kind | `MISN` |
| source hash | data-set hash: SHA-256 of the profile's file list (name, size, SHA-256 of `StarCraft.exe` and the five MPQs). Every file converted from one installation has the same value. |
| provenance hash | SHA-256 of the profile tables the conversion reads: `defs/chk/`, and through the PACK analysis `defs/ai/`, `defs/iscript/`, `defs/requirements/` and `defs/dat/` (`converter/provenance.py`, each file keyed by its directory and name) |

## Import rules

The converter reads the CHK with the native 1.17 loader tables in `defs/chk/bw-1.17.0.1.json`: version descriptors `0x4eeb80`, section tables, required flags and handler addresses. A test checks these tables against the bytes of `StarCraft.exe`.

**Groups.** Campaign scenarios load the header, briefing and use-map-settings groups of the descriptor selected by `VER `.

**Chunk enumeration** follows `0x4c1170`:
- the next header is at `payload + size`;
- a trailing fragment shorter than 8 bytes ends the list;
- a payload running past the end fails only when its tag is read (`0x4c12b0`).

**Payload rules** are those of the handlers (atlas [P23](../research/atlas/p23.md)), for example `DIM ` exactly 4 bytes, `FORC` at most 20, `THG2` a multiple of 10, `MRGN` 1,280 bytes for version 59 and 5,100 for 63/205.

**`VCOD`** must equal the executable's `VCOD` resource 202 byte for byte. Equal bytes guarantee the equal hashes that `0x467e50` requires.

**The converter rejects with an error:**
- a version without a descriptor: the four prerelease `VER=57` scenarios outside the catalog, which are listed in the report;
- a missing required section;
- a payload rule violation;
- a duplicate of a read section: the native dispatcher would run the callback twice, and no campaign map does this;
- a tag that is neither read nor allowlisted.

**Allowlisted tags,** present in the corpus but not read by the game: `TYPE`, `IVER`, `IVE2`, `IOWN`, `ISOM`, `DD2 `, `TILE`, `SWNM`, `UPUS`, `WAV `.

**Corpus-specific rule.** Every catalog map has `MTXM` = 2·W·H bytes and `MASK` = W·H bytes, and the writer requires this. The native loader accepts other lengths: a shorter `MTXM` leaves earlier tiles in place, and `MASK` length is never checked against the map size.

## Sections

The sections below are canonical. The terrain-derived sections `WALK`, `RGNS` and `CONT` follow them; see [Derived sections](#derived-sections). `PACK` is added in a later step.

| FourCC | Content |
|---|---|
| `META` | 68 bytes, see below |
| `MTXM` | CHK `MTXM`, W·H u16 tile words, row-major, high bit included |
| `MASK` | CHK `MASK`, W·H bytes; bit p set means player p starts unexplored and unseen (`0x469260`) |
| `SPRT` | CHK `THG2` records, 10 bytes each, in file order: u16 id, u16 x, u16 y, u8 owner, u8 unused, u16 flags |
| `UNIT` | CHK `UNIT` records, 36 bytes each, in file order, raw (no field corrections) |
| `LOCS` | CHK `MRGN`: 64 records (version 59) or 255 (63/205), 20 bytes each |
| `STRS` | CHK `STR ` unchanged: u16 count, u16 offsets, blob. Look up with `kp_map_string` (`native/kp_core/src/core/kp_mission.c`, `0x4c13b0` rules) |
| `TRIG` | CHK `TRIG`, 2,400-byte records in raw order; the converter rejects a reachable condition of a type above 23, a reachable action of a type above 59, an Order to a location past 255 and a Move Unit whose nonzero number has a low byte of 0 (location 256), which the original's tables would index past, and a reachable Opponents condition, Modify Unit Resource Amount or Modify Unit Hangar Count action, which are outside the V1 profile (no campaign map reaches them; `converter/analyze/triggers.py validate`); ignored entries (flag `0x02`) and those after the first type 0 are never reached and stay as the map has them |
| `MBRF` | CHK `MBRF`, 2,400-byte records in raw order |
| `UPRP` | CHK `UPRP`, 64 records of 20 bytes |
| `RULE` | override sections in native dispatch order, see below |
| `WAVS` | WAV strings of the mission, see below |

### `META`

| Offset | Size | Field |
|---:|---:|---|
| 0 | 2 | CHK version (59, 63 or 205) |
| 2 | 2 | `ERA ` raw value; the tileset is `era & 7` (Brood War is enabled; without it values above 4 are rejected) |
| 4 | 2 | width in tiles |
| 6 | 2 | height in tiles |
| 8 | 12 | `OWNR`, slot controller per player |
| 20 | 12 | `SIDE`, race per player |
| 32 | 20 | `FORC`, zero-filled to 20 bytes as `0x469320` does: force of players 1–8, four u16 force-name strings, four force flag bytes |
| 52 | 2 | `SPRP` scenario-name string |
| 54 | 2 | `SPRP` description string |
| 56 | 8 | `COLR` (version 205), zero when absent |
| 64 | 1 | flags: bit 0 = `COLR` present |
| 65 | 3 | zero |

### `RULE`

A sequence of `{fourcc tag, u32 size, payload}` entries without padding, holding the override sections present in the file. They are in the order the native table dispatches them:
- **version 59/63:** `UNIS`, `UPGS`, `TECS`, `PUNI`, `UPGR`, `PTEC`, then the optional `UNIx`, `UPGx`, `TECx`, `PUPx`, `PTEx`, which overwrite the base settings;
- **version 205:** `UNIx`, `UPGx`, `TECx`, `PUNI`, `PUPx`, `PTEx`.

`UPGR/PUPx/PTEC/PTEx` stay in the native planar layout ([map overrides](../research/native-map-overrides.md)). The runtime applies the handlers in this order.

### `WAVS`

Section schema 2. u16 count, then 12-byte entries sorted by string id: u16 string id, u16 sound index, u32 RIFF data length, u32 nAvgBytesPerSec (both 0 for a missing file). The lengths come from the native RIFF walker (`converter/formats/wav.py`) and feed the duration arithmetic `u32(length * 1000) / nAvgBytesPerSec` of `0x49da70`.

**Which strings are listed.** Every WAV string (the low 16 bits of action `+0x08`) used by:
- TRIG actions 7 Transmission and 8 Play WAV;
- MBRF actions 2 Play WAV and 8 Transmission.

Disabled actions are included.

**Sound path.** The original builds the path as `<mission directory>\<string>` (`0x4b6240`, `0x44fd10`).

**Sound index.** The sound index is the position among the existing sounds of the mission in string-id order. It is the id of the sound's OGG in `generated/missions/<campaign>/<mission>_sounds.kpd` ([`MSND`](packs.md#msnd-schema-1)). `0xffff` means the file is missing from the archives, unreadable, or rejected by the native WAV parser (`ParseWaveFile` `0x49d830`, which plays nothing then; listed under `unreadable_sounds` too). Of the 1,106 references in the catalog, 1,105 read correctly. The exception is `campaign\protoss\protoss10\staredit\wav\PAB05ura.wav`, the MBRF Transmission (record 0, action 14) of the Protoss 10 briefing. It is a damaged entry of the retail StarCraft.mpq (`tools/research/probe_storm_read.py`):
- the original `storm.dll`, run in the emulator's machine, decodes sectors 0–218;
- it fails on sector 219 of 244 with Storm error `0x85100083`, shown through `SErrDisplayError`, which terminates the process. The original game on this installation therefore exits when the briefing reaches that line;
- StormLib already fails on sector 218.

The remake cannot reproduce the termination. The converter treats the line as missing (no sound) and lists it under `unreadable_sounds` in `generated/reports/missions.json`, rather than inventing content.

## Derived sections

`WALK`, `RGNS` and `CONT` hold the terrain state that the original builds at map load (`0x490d80`), before doodads, units and other mutations. The converter always builds them from the tileset files of `ERA & 7`.

**Producers:** `converter/derive/walk.py`, `regions.py`, `contours.py`. They reproduce the native 1.17 routines; the tests compare their output with native hashes for all 68 campaign CHKs (64 catalog maps and the four `VER=57` maps) and with the synthetic research fixtures, and the `RGNS` sections of the 64 catalog maps with the graphs the game builds at mission start (`docs/research/evidence/native-ingame-regions.json`, from the full-game emulator's state dumps by `tools/research/ingame_regions.py`). Version 1 of `RGNS` was built without the console strips and numbered the regions differently from the game wherever the strips change the classes.

**Inputs:** `MTXM`, the tileset's `CV5` and `VF4`, width and height. Megatile IDs and tile flags come from the native terrain import (`0x445720`, `converter/derive/terrain.py`). The region graph is built from the tile flags after `LoadTileMapSection 0x469170` blocks the console strips (each of the bottom-corner 5×2 rectangles and the bottom row clears `0x20410000` and sets `0x00800000`), as the game does before it calls `0x481d50`; the walk raster and the contours depend on the megatile IDs and `VF4` only.

**Section header:**

| Section | Algorithm | Version | Schema |
|---|---|---:|---:|
| `WALK` | `WLK1` | 1 | 1 |
| `RGNS` | `RGN1` | 2 | 1 |
| `CONT` | `CNT1` | 1 | 1 |

The inputs hash is the first 16 bytes of SHA-256 over `MTXM ‖ CV5 ‖ VF4 ‖ u16 width ‖ u16 height`. All three sections carry the same value.

### `WALK`

The walk raster of `0x48b1c0`, one byte per 8×8-pixel mini-cell. The section covers only the map: `height·4` rows of `width·4` bytes, row-major, without a header.

The original uses a fixed 1024×1024 buffer with a row stride of 1024. A reader that needs native addressing copies row `y` to offset `y·1024`; cells outside the map are zero.

Each byte is one of:
- `0x80`: blocked. This is a mini-cell whose VF4 bit 0 is clear, or a cell under the bottom UI: the last 8 rows block 20 cells at each side, and the last 4 rows are blocked across the full width;
- `0x00`–`0x0f`: walkable. Bits 1, 2, 4, 8 mark a blocked or off-map neighbour at the top, right, bottom and left.

Contour construction consumes the direction bits of the live grid. A runtime that builds contours itself must therefore work on a copy.

The alternate tile layer read by the elevation query (`0x445690`, array `0x66696c`) is not converter data. It is the creep backup map (`GetCreepBackupMap` `0x4c0320`), which `InitCreepEngine` `0x4c0690` allocates zero-filled (width × height WORDs) at map load and only creep changes at runtime (atlas P42, P15). The simulation therefore starts it as zeros.

### `RGNS`

The complete region graph after `0x48f060` (rectangles, merging, compaction, split cells) and `0x48f0e0` (cut ranks). Layout:

| Part | Size | Content |
|---|---:|---|
| header | 12 | u16 width, u16 height, u16 region count `R`, u16 split count `S`, u32 neighbour words `N` |
| regions | `32·R` | records in original ID order, see below |
| neighbours | `2·N` | u16 region IDs; each region's array is contiguous |
| raster | `2·W·H` | u16 per tile, row-major with stride `W` (native stride 256): a region ID, or `0x2000 + split index` |
| splits | `6·S` | records `u16 mask, u16 walkable region, u16 blocked region`, in allocation order |

Region record. Native offsets in the 64-byte region object are given in brackets.

| Offset | Size | Field |
|---:|---:|---|
| 0 | 2 | class [`+0x00`]: `0x1ff9` middle, `0x1ffa` high, `0x1ffb` low ground, `0x1ffd` unwalkable |
| 2 | 2 | group [`+0x02`]; region 0 has the reserved group `0x4000` |
| 4 | 2 | tile count [`+0x04`], counted before split cells replaced raster entries |
| 6 | 1 | ground-neighbour prefix length [`+0x06`] |
| 7 | 1 | neighbour count [`+0x07`] |
| 8 | 1 | count of neighbour ID 0 [`+0x21`] |
| 9 | 1 | cut rank 0..5 [`+0x20`] |
| 10 | 2 | zero |
| 12 | 4 | first neighbour word in the neighbour array |
| 16 | 8 | i32 x, i32 y: fixed8 pixel centre [`+0x10`] |
| 24 | 8 | i16 left, top, right, bottom: pixel bounds, right/bottom exclusive [`+0x18`] |

**Region 0** is the reserved bottom row (`0x490f50`). Its centre x is 16 pixels to the right of the bounds midpoint. The other centres keep the history of rectangle creation; they are not recomputed from the tiles.

**Neighbour arrays** are in native order:
1. ground neighbours (nonzero, class other than `0x1ffd`);
2. nonzero `0x1ffd` neighbours;
3. region 0 last.

**Split records** replace partially walkable tiles. Bit `mini_y·4 + mini_x` of the mask selects the blocked region; a clear bit selects the walkable region. The bottom map row can hold split records `[0xffff, 0, 0]`. They stay in place because later split IDs depend on them.

### `CONT`

The four collision-contour arrays of `0x48a7f0`, in the order top, right, bottom, left.

| Part | Size | Content |
|---|---:|---|
| header | 16 | u16 count ×4, then u16 capacity ×4 |
| records | `8·Σcount` | the four arrays, one after another |

**Record:** `i16 axis, i16 start, i16 end, u8 direction, u8 info`.
- Top and bottom contours use y as the axis and an x interval; right and left contours use x as the axis and a y interval.
- Coordinates are pixels and can be −1 at the map edge.
- `info` holds the corner and traversal-mode bits ([native contours](../research/native-contours.md)).

**Order.** Each array is sorted by the original CRT `qsort` (`0x4d4490`), comparing signed axis, then signed start (`0x48ad40`). That qsort is not stable. The converter runs the same algorithm, so equal keys get the native order. No campaign map has equal keys.

**Capacity** is the native capacity word after construction. It starts at 5000 per direction. Before each append, if any array is full, all four grow by 2500 (`0x48b040`). Brood War Terran07, Terran08 and Zerg10 end at 7500; every other campaign map ends at 5000. The storage is then shrunk to the counts, but the capacity words keep these values for later growth in the mission.


### `PACK` (derived `PAK1` v1)

The resource packs the mission loads before it starts (ADR 0004): u8 count, then per pack a u8 name length and the ASCII name, sorted. Pack names come from `converter/packs.py`.

The dependency analyzer produces the list (`converter/analyze/profile.py`, [analyzer](../analyzer.md)). The inputs hash is the SHA-256 of the data set hash and every canonical section of the file.

The runtime reads it as `mission.packs`.
