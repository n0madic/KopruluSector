# Game data files (`.kpd`)

Global data converted once per installation into `generated/data/`, using the [Koprulu container](container.md). Written by `python3 -m converter data --data DIR` (`converter/kpf/kpd.py`). Every file has the data-set source hash shared with the [mission files](kpm.md).

| File | Kind | Status |
|---|---|---|
| `dat.kpd` | `DAT ` | done |
| `strings.kpd` | `STRS` | done |
| `iscript.kpd` | `ISCR` | done |
| `aiscript.kpd` | `AISC` | done |
| `tilesets/<name>.kpd` | `TSET` | done |
| `palettes.kpd` | `PALS` | done |
| `fonts.kpd` | `FONT` | done |
| `ui.kpd` | `UI  ` | done |
| `console.kpd` | `CONW` | done (from a Remastered install: `hd`, `data --scr`; `converter/emit/hd_ui.py`) |
| `sounds.kpd` | `SNDS` | done |
| `portraits.kpd` | `PORT` | done |
| `grpmeta.kpd` | `GRPM` | done |
| `campaign.kpd` | `CAMP` | done (`converter/campaign.py`) |

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

**Requirement sections.** The order, research, technology-use, upgrade and unit-production requirements are bytecode built into `StarCraft.exe`, not DAT files ([DAT requirements](../research/native-dat-requirements.md), [unit requirements](../research/native-requirements.md)). The converter copies each table unchanged from the executable (bases in `defs/dat/requirements-bw-1.17.0.1.json`):

| Fourcc | Domain | Base | Records |
|---|---|---|---:|
| `RQOR` | orders | `0x502090` | 112 |
| `RQRS` | research | `0x501ca0` | 26 |
| `RQTU` | technology use | `0x501de0` | 33 |
| `RQUP` | upgrades | `0x501958` | 83 (51 upgrade ids and 32 level-section markers) |
| `RQUN` | unit production | `0x50da88` | 91 |

A table is u16 words: a leading zero, records `id, tokens…, 0xffff`, and a final `0xffff` in place of a record id. The runtime (`dat.requirements[domain]`, 0-based words) builds the offset arrays the way the native lazy initializers do.

**Runtime form.** `dat.parse` unpacks every column once into Lua arrays indexed by 0-based id, for example `t.units.hit_points[0]`. Several-per-id columns are flat: `column[id * per_id + k]`.

**Signedness.** Values are unsigned. Whether a field is signed depends on the consuming instruction (`movsx`/`movzx`), so the simulation applies it where it reads a column.

**Cost.** Unpacking all tables takes 0.33 ms with LuaJIT JIT on and 0.97 ms with JIT off. It costs about 0.5 MB of heap.

## `strings.kpd` (schema 1)

**Sections.** Each section is one runtime TBL file, with its text in UTF-8 (ADR 0016; `kpd.table_utf8`: the original's windows-1252 bytes transcoded and the offsets moved, the rows stay in place, a table already in UTF-8 unchanged; only `GHST` has any in enUS):

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

## `iscript.kpd` (schema 2)

- **Reader:** `game/data/scripts.lua`.
- **Provenance hash:** `defs/iscript/`, which gives the `SLOT` counts.
- **Tests:**
  - `converter/tests/test_scripts.py`;
  - `tests/lua/test_scripts.lua`.

**Sections:**

| Section | Content |
|---|---|
| `CODE` | `scripts\iscript.bin` unchanged. Program counters are byte offsets into it. |
| `HDRS` | u16 count, then `{u16 iscript id, u16 header offset}` sorted by id |
| `SLOT` | u8 count, then `{u8 header type, u8 animation slots}` sorted by type: `header_animations` of `defs/iscript/` |

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
  - `converter/tests/test_resources.py` (`SHET`, `EDGE`);
  - `tests/lua/test_terrain.lua`.

| Section | Class | Content |
|---|---|---|
| `CV5 ` | canonical | CV5 unchanged: 52-byte groups, flags WORD at +2, 16 megatile WORDs at +20 |
| `VF4 ` | canonical | VF4 unchanged: 16 mini-cell flag WORDs per megatile (input of the later `WALK` producer) |
| `WPE ` | canonical | 256 × (R, G, B, 0). The fourth byte is not alpha. `presentation/palette.lua` starts from these RGB bytes and the runtime sends its output as `kp_palette`. |
| `CYCL` | canonical | u8 range count, u8 speed (8), then (low, high) inclusive index pairs of the stock simple cycles, all enabled by the native masks ([palette bootstrap](../research/native-palette-bootstrap.md)); `presentation/palette.lua` runs them |
| `MFLG` | derived `MFL1` v1 | u32 megatile flags by the `0x445562` rules; the inputs hash is the SHA-256 of VF4 |
| `MMNI` | canonical | per megatile 4 bytes: the VR4 pixel (7, 6) of its minitiles 0, 1, 4, 5 (the flip bit ignored), the console minimap's terrain colours before its palette map (`0x46c920`, `0x46ca20`, `0x46e260`; plan phase 9B) |
| `SHET` | canonical | the SDL runtime's terrain texture (phase S): u16 megatile count, u16 columns (128), then an 8-bit grayscale PNG of every megatile, 32 × 32 palette indices each, megatile t at x = (t % 128) · 32, y = (t // 128) · 32, no extrusion; the rest of the last row is 0. Every megatile texel is opaque, index 0 included. The megatiles are those of the Defold tilesource (`converter/emit/terrain.py` `tile_sheet`, `Tileset.render_megatile`), so the viewer's `generated/viewer/<name>.mcrc` CRCs hold for them |
| `EDGE` | canonical | creep's edge frames (`tileset\<name>.grp`, 37 frames in 1.17): u16 frame count, then an 8-bit grayscale PNG of the frames side by side, frame k in the 32 × 32 cell at x = k · 32, each at its GRP offset, 0 where it draws nothing (a frame never draws index 0: checked). The game draws frame b − 1 over a tile whose megatile word has bit 15 and whose border byte b (the creep engine's, `kp_creep.c`) is not 0 (`0x40ad1b`, `0x4c03b0`); the SDL runtime appends the cells to `SHET`'s sheet after the megatiles. A container without it draws no edges |

**Converter checks.** The converter verifies the record sizes, every CV5 → megatile reference and every VX4 → VR4 reference. VX4 and VR4 are used only to prerender the megatiles (`SHET`, the Defold tilesource) and are not shipped.

**Placement of palette data.** The plan lists WPE and the cycle ranges under `palettes.kpd`. They are keyed by tileset, so they live here; `palettes.kpd` holds the remap and blend tables.

**Terrain import.** `terrain.import(mission, tileset)` implements `ImportMapTiles` `0x445720`. It returns the megatile id and the tile flags of every cell:
- tile flags start with low WORD `0xffff`;
- they add `(CV5 flags & 0xd8fa) << 16` and the megatile flags;
- a high tile bit sets id bit 15 and flag `0x400000`;
- a tile selecting a missing CV5 group stops the import.

**Verification.** The Python twin reproduces the native output hashes byte for byte:
- the megatile flags of all eight tilesets;
- the megatile ids and flags of all 68 campaign CHKs, 1,045,504 cells (`native-terrain-import-probes.json`).

The Lua version is tested against the Python twin.

## `palettes.kpd` (schema 1)

- **Reader:** `game/data/palettes.lua`.
- **Tests:**
  - `converter/tests/test_palettes.py`;
  - `tests/lua/test_palettes.lua`.

The section contents follow the native loaders. Original data was checked for:
- every PCX shape listed in `native-pixel-blitters-probes.json`;
- the `tunit` prefix;
- the section sizes of all eight tilesets.

| Section | Content |
|---|---|
| `PAL0`…`PAL7` | One per tileset in `ERA & 7` order, see below |
| `TUNI` | First `0x60` bytes of `game\tunit.pcx`: 12 player colour rows of 8 indices (`0x41d570`) |
| `TMIN` | First 12 bytes of `game\tminimap.pcx`: minimap player colours |
| `TSEL` | First 24 bytes of `game\tselect.pcx`: selection-circle colours (`0x40fef0`) |
| `THPB` | First 19 bytes of `game\thpbar.pcx`: health-bar colours |

**`PAL<n>` layout:**

1. **Remap tables.** One per remap record 1–7 of `0x508828`, in order: `ofire`, `gfire`, `bfire`, `bexpl`, `trans50`, `red`, `green`. `images.dat` Remapping indexes them from 1.

   Each table is a u16 row count (PCX height + 1), then rows × 256 bytes. As in `LoadRemapTables` `0x466810`, table row r + 1 is PCX row r.

   Row 0 is never initialized natively and no stock image reads it (opaque index 0 does not occur), so it is stored as zeros.
2. **`dark.pcx`.** `0x2000` bytes: 32 darkness rows (`0x4668f0`).
3. **`shift.pcx`.** 256 bytes.

**Copy rule.** Every fixed-size table is the first N pixel bytes, row-major, as `SBmpLoadImage` copies them into an N-byte buffer.

**Runtime blend tables.** The UI blend tables (`0x6cf7b4`, `BuildBlendTable` `0x4c9c70`) are built at runtime from the current palette. They are not converter data.

## `fonts.kpd` (schema 1)

- **Reader:** `game/data/fonts.lua`.
- **Glyph atlas:** `converter/emit/fonts.py`.
- **Tests:**
  - `converter/tests/test_fonts.py`;
  - `tests/lua/test_fonts.lua`.

| Section | Content |
|---|---|
| `F8  `, `F10 `, `F14 `, `F16 `, `F16X` | Decoded FNT bytes of `font8` … `font16x` |
| `TFGM` | `game\tFontGam.pcx` colour rows: 192 bytes = 24 rows of 8 indices |
| `TFNT` | `glue\title\tFont.pcx` colour rows: 40 bytes = 5 rows |

**FNT bytes.** The effective StarDat fonts are encrypted. Their decoded bytes equal the BroodWar.mpq `files\font\*.fnt` plaintext plus one trailing NUL ([font decoder](../research/native-font-decoder.md)). The converter therefore reads the plaintext, appends the NUL, and requires the SHA-256 of the native-decoded font recorded in `defs/fonts/bw-1.17.0.1.json`, which also records this provenance. No key or decryption is involved.

**Glyph atlas.** `generated/fonts/fonts.atlas` holds one animation per font. Frame `code - low - 1` is byte `code`.

Each texel is the value the packet stream of blitter `0x4cfdf0` selects:
- 0 = not written;
- k + 1 = colour-row entry k.

A zero skip plots even mask 0; a positive skip plots only a nonzero mask inside the glyph.

**Verification.** All 4,460 native glyph blits of `native-text-probes.json` reproduce byte for byte: every glyph of every font, two colour rows, two strides.

## `console.kpd` (schema 1)

The console's shape on a screen other than the original's ([ADR 0011](../adr/0011-interface-panels-and-layouts.md), 2026-11-05), from StarCraft: Remastered's art, for every kind of graphics; a file of its own, as `data --data` (the 1.17 install) has no such art. Optional: without it the console stays the original's, in the `classic` arrangement. Section `CONW`: u8 races; per race a letter (`z`, `t`, `p`), 127 bytes (the colour table: the most used palette indices of the race's console PCX), then five records (left piece, centre piece, right piece, left tile, right tile): i16 x, y (logical, in the console PCX's coordinates), u16 width, height, period (the tiles': they repeat every period columns from x; the pieces 0), width × height cells: 0 none, else 1 + the colour's place in the table, `0x80` where the art covers less than half the logical pixel (the HD art's edge: a silhouette, not an SD pixel).

## `ui.kpd` (schema 4)

- **Reader:** `native/kp_core/src/core/kp_ui_core.c` (`kp_ui_new`).
- **Parser and fixup model:** `converter/formats/dialog.py`.
- **Tests:**
  - `converter/tests/test_ui.py`;
  - `tests/c/test_ui_data.c`.

**Contents:**
- **69 dialogs.** Every `rez\*.bin` the executable names, with `%c` expanded to t/z/p. `rez\gluATZoneList.bin` (the Battle.net zone list) is absent from the archives; the list is in `defs/ui/bw-1.17.0.1.json`.
- **Theme colour banks and effect tables** of the twelve glue themes.
- **The dialog GRPs** (whole files; a frame is a raw bitmap) and the console art (`game\<z|t|p|n>console.pcx`).
- **The executable's interface tables** (schema 2, plan phase 9): `defs/ui/tables-bw-1.17.0.1.json`, read from `StarCraft.exe` and `Local.dll` by `converter/ui_tables.py` and checked against them by `converter/tests/test_ui_tables.py`. Functions are kept as the original's addresses: kp_ui dispatches a button's condition and action and a status layout's refresh and draw by address, and the manifest names each one after the atlas.

| Section | Content |
|---|---|
| `DLGS` | u16 dialog count; per dialog: u8 name length, name, u16 widget count, u16 SMK count, widgets (60 bytes each), SMK nodes (28 bytes each) |
| `POOL` | NUL-terminated texts (UTF-8, ADR 0016) and SMK file names referenced by offset |
| `TFNT` | 12 × 48 bytes: `glue\<theme>\tFont.pcx` per theme in record order (`PalMm`, `PalCs`, `PalRt`, `PalRz`, `PalRp`, `PalPd`, `PalPv`, `PalZd`, `PalZv`, `PalTd`, `PalTv`, `PalNl`). These are the 48-byte prefixes of the 192-byte banks; the rest of each bank is uninitialized natively. |
| `TEFF` | u16 size, then each theme's `tEffect.pcx` pixels |
| `DGRP` | u16 count; per GRP: u8 path length, path, u32 size, the whole file (`<3H` header, 8-byte frame records `{u8 x, y, w, h, u32 offset}`, raw frame bitmaps). Covers the twelve `glue\<theme>\Dlg.grp` and `glue\<theme>\arrow.grp` (the theme's cursor, `LoadGlueCursors` `0x4584e0`: its indices belong to the theme's palette), `dlgs\{terran,zerg,protoss}.grp` and the `defs/ui` `grps` (the console's icons, wireframes and buttons, the 19 cursors, `dlgs\tile.grp`, and the command marker `unit\thingy\blackx.grp` that DrawCommandMarker `0x418be0` draws). The runtime applies `0x4cf570`: low-byte frame request, frame 0 when out of range, offset bit 31 adds 256 to the width. |
| `PCXS` | u16 count; per file: u8 path length, path, u32 size, the PCX file: `game\zconsole.pcx`, `tconsole`, `pconsole`, `nconsole` (LoadConsoleBitmap `0x4a7e20`) |
| `BTNS` | u16 set count (250, `0x50ae98`); per set: u16 linked set (`0xffff` none; 16 → 1, 19 → 2), u16 button count, then the native 20-byte buttons {u16 position, u16 icon, u32 condition address, u32 action address, u16 condition value, u16 action value, u16 enabled string, u16 disabled string} (atlas P35) |
| `STLY` | u16 count (228, `0x50ba50`); per unit type: u32 needsRefresh address, u32 draw address (P36) |
| `FLUF` | u8 list count (4: Zerg, Terran, Protoss, replay; `0x50d7b0`); per list: u8 count, then {u16 x, y, w, h} rectangles of `rez\statfluf.bin` dialogs (P36 `0x4a2660`) |
| `CURS` | u8 count (19); per cursor: u8 length, file name under `cursor\` (`0x5031c0`); then the edge-scroll table, 9 × u16 (`0x50320c[dx + 3·dy + 4]`, P13) |
| `RECT` | u8 count; per rectangle: u8 name length, name, 4 × s16 (inclusive): `command_card` `0x5033d0` (P16), `minimap` `0x508bb0` (P13) |
| `MUSI` | u8 count (25, `0x508c58`); per track: u8 length, file (empty for none), u8 kind (1 loops, 2 plays once then `next`), u8 fade-in, u8 next (P24) |
| `VOLT` | u8 count (100), then s32 attenuations in hundredths of a dB (`0x4efc00`, P35) |
| `ACCL` | u8 table count; per table: u8 module (0 `Local.dll`, 1 `StarCraft.exe`), u16 resource id (101, 102, 103; 113), u16 count, then {u16 flags, u16 key, u16 command} (RT_ACCELERATOR entries without their padding; P16, L01) |
| `ESTB` | (schema 3) u8 count (65), per entry u8 name length, name, u16 campaign mission key: the establishing texts `0x4ebba8[64]` and, last, the finale text `0x4ebda8` (`rez\<name>.txt` of RunCreditsScreen `0x415010`; P05) |
| `GFIL` | (schema 3) the glue's files, the `DGRP` layout: each theme's `glue\<theme>\Backgnd.pcx` (InitGlueDialog `0x457f00`), the type-5 bitmaps and SMK nodes of every dialog (in dialog order, without repeats ignoring case), `defs/ui` `glue_files` (the game-mode art, the title, the mission history, the score screens' art, the ready rooms' portrait frames), each theme's `defs/ui` `theme_files` (the popups' images `pOPopup.pcx`, `pDPopup.pcx`, `pEPopup.pcx` of `glue\<theme>\`), and the credits screens' scripts (`ESTB`, `crdt_exp`, `crdt_lst`, `epilog`, `epilogX`) with the backgrounds and colour banks their `</BACKGROUND>` and `</FONTCOLOR>` tags name; about 10 MB |
| `VFNT` | (schema 4, optional) the vector font of large scales (ADR 0011, 2026-10-26), the `DGRP` layout: two files, `fonts\Exo2.ttf` (the text's) and `fonts\Michroma-Regular.ttf` (font16x's), SIL OFL 1.1 (`third_party/fonts`), not original data. Without it the text stays the original's bitmaps |
| `GHST` | (schema 3, optional) the placement ghosts, the `DGRP` layout: frame 0 of the GRP of every building type's (units.dat flag `0x1`) flingy sprite's image (DrawPlacementGhostImage `0x47cd80`), each as a GRP of that one frame with the original's canvas size and row packets, named `ghost\<images.dat id>`; 75 files, about 430 KB. Without it no ghost is drawn |

**Widget record**, in native traversal order (root, then the root's child chain):

| Field | Type |
|---|---|
| parent index | u16, `0xffff` for the root |
| type | u16 |
| id | u16 |
| flags | u32 |
| x1, y1, x2, y2 | s16 |
| width, height | u16 |
| text | u32 pool offset (`0xffffffff` none) + u16 length; for type 5 this is the bitmap file name |
| first SMK index | u16, `0xffff` none |
| SMK count | u16 |
| responsive words | 4 × u16, record +0x36 |
| extra words | 4 × u16, +0x46…+0x4c |
| shared-bitmap reference | u32, `0xffffffff` none: the raw negative value at `+0x10`, a signed offset relative to the file base; the native fixup reads the shared bitmap at file base + value + `0x10` (a value of −1 is rejected, it would read as none) |
| word `+0x1c`, show delay `+0x1e` | 2 × u16 (the delay in milliseconds: hidden at load, shown by the dialog's 100 ms timer) |
| user value `+0x26` | u32 |

**Fixup rules already applied:**
- the first text byte is ASCII-lowercased under flag `0x200`;
- type-14 extra words have the control x/y added (u16 wrap);
- a negative bitmap value becomes a shared-bitmap reference with flag `0x20`.

**SMK node record:**

| Field | Type |
|---|---|
| flags | u16 |
| file name | u32 pool offset + u16 length |
| offset x, offset y | s16 |
| raw unknown words | 4 × u32: +0x06, +0x0e, +0x16, +0x1a |

**Verification.** `dialog.fixup` mirrors the byte mutations of `0x4c33e0`. With the probe's load base and the executable's callback tables, it reproduces all 102 native fixed-buffer hashes: 6 whole files and 96 isolated widgets, including the stops at the bitmap loader.

## `sounds.kpd` (schema 2)

- **Reader:** `game/data/sounds.lua`.
- **Tests:**
  - `converter/tests/test_sounds.py`;
  - `tests/lua/test_sounds.lua`.

`SFX ` holds a u16 count (1,144), then one 22-byte record per `sfxdata.dat` id:

| Field | Type |
|---|---|
| flags | u16: bit 0 file present, bit 1 parsed (a present file the native parser rejects keeps bit 1 clear and zero fields; the original never plays it) |
| format tag, channels | u16 each |
| sample rate | u32 |
| nAvgBytesPerSec | u32 |
| block align, bits per sample | u16 each |
| data length | u32 |

`PACK` (schema 2) holds a u16 count (1,144), then per sfx id a u8: 0 no file, else 1 + the index of its resource pack in (`effects`, `terran`, `protoss`, `zerg`) (`converter/packs.py` `pack_of_sound`; the pack's `sounds<id / 128>` object holds its component `sfx<id>`).

`MUSL` (schema 2, plan phase 9D) holds a u8 count (the 25 tracks of `ui.kpd` `MUSI`), then per track the music WAV's data length and nAvgBytesPerSec (u32 each; zeros without a file): `PollMusicEnd` `0x46ee70` ends a track of kind 2 at its length and starts its next one.

**Path.** `sound\` + `sfxdata.tbl` entry. Id 0 has no entry. Ids 86, 104, 105 and 1066 name files that are absent from the archives, so the original plays nothing for them.

**Parsing.** RIFF parsing follows `ParseWaveFile` `0x49d830` and `FindRiffChunk` `0x49d7c0`:
- `fmt ` is searched from byte 12, must be at least 16 bytes and fit;
- `data` is searched after `fmt ` and must fit;
- chunks are not padded to an even size;
- any format tag is accepted.

All 60 stock parser cases of `native-audiovisual-probes.json` reproduce.

**Durations.**
- `sounds.duration_ms` is the cached duration of `0x49da70`: `u32(length * 1000) / nAvgBytesPerSec`, the product wrapping.
- `sounds.whole_seconds_ms` is the rounding of `0x446740`.
- The signed `portrait_length_adjust` adjustment is in `dat.kpd` (`sfxdata`).

The plan placed the portrait variant table here; it lives in `portraits.kpd` with the portrait palettes.

## `portraits.kpd` (schema 2)

- **Reader:** `game/data/portraits.lua`.
- **Atlases:** `converter/emit/portraits.py`.
- **Tests:**
  - `converter/tests/test_portraits.py`;
  - `tests/lua/test_portraits.lua`.

| Section | Content |
|---|---|
| `ROWS` | u16 count (110 `portdata.dat` rows); per row: idle and talking, each as u16 first SMK index (`0xffff` none) and u16 mask of existing variant digits |
| `SMKS` | u16 count; per entry (621 entries for 519 distinct movies, because rows that share a stem repeat their entries so that each row's variants stay contiguous): u8 path length, path, u16 width, height, decoded frames, header frames, u8 ring flag, u32 µs per frame, then u16 palette index per frame |
| `PALS` | u16 count, then 768-byte RGB palettes. Every stock portrait frame uses the same palette, so the count is 1. |
| `FILE` | the SMK files themselves, in `SMKS` order: u16 count; per file u8 path length, path, u32 size, the bytes (schema 2, phase 9B) |

**Movie paths.** Each is `portrait\` + `portdata.tbl` stem + variant digit + `.smk`, the opening path of `0x4a6b40`/`0x4a6c50`. Digits 0–9 are probed. The mask records which files exist; for example, Zealot idle variant 3 is absent.

**Frames.** Frames are decoded with FFmpeg's smackvideo decoder to indices and per-frame palettes. For all 519 portraits the RGB result equals the original smackw32.dll output frame for frame (`smk-decoder-equivalence.json` hashes, test). FFmpeg is a converter-time dependency; tests that need it are skipped without it.

**Run-time decoding (schema 2).** The console's portrait (`kp_ui`, plan phase 9B) plays the `FILE` movies with kp_core's Smacker decoder (`kp_smk.c`) the way SVid and Smackw32 do (`SVidPlayContinueSingle` with `SmackWait`'s timing); the decoder's indices equal FFmpeg's for all 6,454 frames of the 621 entries.

**KPV1 (ADR 0013).** Converted from StarCraft: Remastered alone (`--scr`), a movie Remastered keeps only as WebM is a KPV1 file in the SMK's place (`converter/formats/kpv.py`; `kp_smk.c` plays it, `smk.decode` reads it): "KPV1", u32 width, height, frames, i32 frame rate and u32 flags as the SMK header's (flag 1: a ring movie, whose extra stored frame leads back to frame 1), 768 bytes of palette (R, G, B), then for each of frames + ring stored frames u32 size and runs (a control byte c < 128: c + 1 literal indices; c ≥ 128: one index c − 125 times). Its indices are its screen's palette's (the PCX palette its SMK's used colours match within 4: the theme's for a glue movie, the game's for a portrait), each pixel the palette's lowest index of its colour or the nearest, alpha below 128 index 0; frames are cut or repeated to the SMK's count. The WebM is lossy, so a KPV1 is an `approximation` of its SMK. The converter keeps the converted movies under `<out>/cache/scr/`.

**Atlases.** Frames are L8 index images in `generated/atlases/portraits_<t|u|z|p|n>.atlas`, one animation per movie. Every texel is opaque, including index 0. The other SMKs (glue animations, cinematics) are UI and video resources of phase 9 and plan §12.2.

## `grpmeta.kpd` (schema 1)

- **Reader:** `game/data/grpmeta.lua`.
- **Writer:** `kpd.build_grpmeta`; opacity runs and the Python twin of `0x40fd50` in `converter/formats/grp.py`; LO checks in `converter/formats/lo.py`.
- **Atlases and packs:** `converter/kpf/resources.py` (the SDL runtime's pack files), `converter/emit/packs.py` (the step that writes them and the Defold resources), see [resource packs](packs.md).
- **Tests:**
  - `converter/tests/test_grpmeta.py`;
  - `converter/tests/test_resources.py` (`AFRM`);
  - `tests/lua/test_grpmeta.lua`.

Every GRP and LO file that `images.dat` names, keyed by `images.tbl` index. The DAT columns are not repeated: `images.grp_file` and the six overlay columns of `dat.kpd` hold `images.tbl` indices, so `TBLX` maps an image id to its entries at runtime (`grpmeta.image_grp`, `grpmeta.image_lo`).

| Section | Content |
|---|---|
| `TBLX` | u16 count (`images.tbl` entries, 929); per entry, index 1 first: u16 GRP entry, u16 LO entry (`0xffff` none) |
| `ATLS` | u8 count; per atlas: u8 flags (bit 0: an identically packed opacity atlas `<name>_mask` exists), u8 length + pack name, u8 length + atlas name |
| `GRPS` | u16 count (832); per GRP: u8 length + path, u16 frame count, u16 canvas width, u16 canvas height, u8 atlas index, u8 length + animation id, u32 first frame in `FRMS` |
| `FRMS` | u32 count (20,598); per frame 8 bytes: u8 x, y, width, height exactly as stored in the GRP, u32 offset of its opacity block in `OPAC` |
| `OPAC` | opacity blocks, see below |
| `LOFS` | u16 count (96); per LO file: u8 length + path, u32 size, the file unchanged |
| `AFRM` | u16 count (= `GRPS` count); per `GRPS` entry, in order: u32 the GRP's first frame within its atlas (the frame count of the atlas's earlier entries). Frame f of entry e is `FRMS[ATLS.first(atlas of e) + AFRM[e] + f]` of the entry's pack file `generated/packs/<pack>.kpd` ([packs](packs.md#pack-schema-1)) |

**Files.** The path is `unit\` + `images.tbl` entry. `0x40ff40` loads the GRPs of 999 images with expansion data (755 otherwise) and shares one load between identical paths; LO files are shared between identical table indices. The archives resolve paths case-insensitively, so entries are deduplicated by lower-case path: the 833 `images.tbl` GRP indices name 832 files (`zerg\zlurker.grp` appears in two spellings). Entries are in order of their first `images.tbl` index; the stored path is that first spelling. Dialog GRPs are in `ui.kpd`.

**Frames.** The frame payload offset is masked with `0x7fffffff`, as the native loader does. The animation id is the atlas animation of the GRP (`atlas.animation_id`, e.g. `terran_marine`); its frame `f` is GRP frame `f`. The atlas resource is `/generated/atlases/<atlas name>.atlas`.

**Opacity blocks.** Per frame: `height` u16 row offsets relative to the block start, then the runs of every row, one byte each: bit 7 transparent, bits 0–6 length. They hold no colours, only what `IsGrpPixelOpaque` `0x40fd50` needs:
- x or y outside the frame (unsigned compare) returns 0;
- otherwise the loop reads the row's packets and subtracts their lengths from x while x > 0 (do-while), and returns 1 when the last packet read was not transparent. A pixel exactly on a run boundary belongs to the preceding run, and x = 0 takes the first packet even if it is empty.

The runs are the RLE packets in canonical form, which gives the same result for every x: adjacent packets of one class are merged, empty packets dropped and runs split at 127. A leading empty packet of the other class is kept, because x = 0 reads it. `grpmeta.pixel_opaque` (Lua) and `grp.runs_pixel_opaque` evaluate the runs; `grp.pixel_opaque` is the twin on raw GRP bytes. The stock blocks take 4.4 MB (700,449 rows, 3.0 M runs from 3.8 M packets).

**Atlases.** `packs.assign_atlases` places each GRP in its pack's atlas (`converter/packs.py`); the SDL runtime finds the atlas's frames in its pack file through `AFRM`, the Defold build in `/generated/atlases/<atlas name>.atlas` through the animation id. A pack over 11.5 M frame-rectangle pixels would be split into `<pack>_1`, `<pack>_2`, …; no stock pack is. The mask flag is set when a GRP of the atlas draws an opaque index 0 (ADR 0003); no stock GRP does.

**Verification** (`test_grpmeta.py` with `KOPRULU_ORIGINAL`):
- `grp-opaque-zero.json`: SHA-256, frame count and opaque pixels of all 833 named files (17,244,859 opaque pixels), and 0 opaque index-0 pixels;
- `native-pixel-blitters-probes.json`: over the 23 GRPs of its `assets_sha256`, 193,262 transparent, 131,449 literal and 26,617 repeat packets and 1,219,820 nontransparent pixels, exactly;
- `native-image-geometry-probes.json`: the native image bounds of all 5,880 recorded frame cases from the stored canvas and frame rectangles, and the 8,294 ordinary and shield LO point cases through `images.dat` column → `TBLX` → stored LO file;
- every row of all 20,598 frames hits like its raw packets, and the per-pixel twins agree on every pixel of each GRP's first frame. The Lua reader matches the Python twin on every pixel of the synthetic fixture, including empty, merged, split and overshooting packets.

## `campaign.kpd` (schema 1)

Written from the manifest `defs/campaign/<profile>.json` (the executable's tables, checked against `StarCraft.exe` by `converter/tests/test_campaign.py`) and `arr\mapdata.dat`/`mapdata.tbl`. Read by `game/data/campaign.lua`; the progression rules are `game/campaign.lua` (atlas P41).

| Section | Content |
|---|---|
| `TABL` | u8 table count (6), then per table in pointer order (`0x50e0b8` Zerg, Terran, Protoss; `0x50e0c4` the expansion's): u8 expansion, u8 race, u16 row count, the rows as the executable keeps them (8 bytes: u16 label (gluAll string id), u16 mission (mapdata index), u16 cinematic, u8 race, u8 hidden), the terminator row (mission 0) included |
| `MAPD` | u16 count (65), then per mapdata index: u16 length and the directory string `0x51abb8` (`campaign\terran\terran01`; empty where the TBL id is 0) |
| `CHEA` | u32 the cheat bits a new game keeps (`0x28000200`, `0x443f40`), u16 count, then per cheat: u32 its bit of `0x5432fc`, u8 length, the lowercase text |

**Stock data:** 85 rows; 57 selectable and 7 hidden playable rows, 15 cinematic rows, 6 terminators.

