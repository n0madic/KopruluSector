# Mission file (`.kpm`, kind `MISN`)

One file per campaign catalog scenario: `generated/missions/<campaign>/<mission>.kpm`, for example `terran/terran01.kpm`. The file uses the [Koprulu container](container.md).

- **Writer:** `converter/kpf/kpm.py`, run with `python3 -m converter missions --data DIR`.
- **Runtime reader:** `game/data/mission.lua`.
- **Dump:** `python3 -m converter dump FILE`.
- **Tests:** `converter/tests/test_chk.py`, `converter/tests/test_kpm.py`, `tests/lua/test_mission.lua`.

Schema version 1. All integers are little-endian.

## Header

| Field | Value |
|---|---|
| kind | `MISN` |
| source hash | data-set hash: SHA-256 of the profile's file list (name, size, SHA-256 of `StarCraft.exe` and the five MPQs). Every file converted from one installation has the same value. |
| provenance hash | SHA-256 of the profile tables under `defs/chk/` |

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

All sections are canonical. Derived sections (`WALK`, `RGNS`, `CONT`, `PACK`) are added by their producers in later steps.

| FourCC | Content |
|---|---|
| `META` | 68 bytes, see below |
| `MTXM` | CHK `MTXM`, W·H u16 tile words, row-major, high bit included |
| `MASK` | CHK `MASK`, W·H bytes; bit p set means player p starts unexplored and unseen (`0x469260`) |
| `SPRT` | CHK `THG2` records, 10 bytes each, in file order: u16 id, u16 x, u16 y, u8 owner, u8 unused, u16 flags |
| `UNIT` | CHK `UNIT` records, 36 bytes each, in file order, raw (no field corrections) |
| `LOCS` | CHK `MRGN`: 64 records (version 59) or 255 (63/205), 20 bytes each |
| `STRS` | CHK `STR ` unchanged: u16 count, u16 offsets, blob. Look up with `game/data/strings.lua` (`0x4c13b0` rules) |
| `TRIG` | CHK `TRIG`, 2,400-byte records in raw order |
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

u16 count, then per entry u16 string id and u16 sound index. Entries are sorted by string id.

**Which strings are listed.** Every WAV string (the low 16 bits of action `+0x08`) used by:
- TRIG actions 7 Transmission and 8 Play WAV;
- MBRF actions 2 Play WAV and 8 Transmission.

Disabled actions are included.

**Sound path.** The original builds the path as `<mission directory>\<string>` (`0x4b6240`, `0x44fd10`).

**Sound index.** The sound index is the position among the existing sounds of the mission in string-id order. `0xffff` means the file is missing from the archives. All 1,106 references in the catalog resolve.
