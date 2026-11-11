# StarCraft: Remastered as a data source

Checked on 26 October 2026 against the local install `/Applications/StarCraft` (build 1.23.10.13515, macOS x86_64, locales ruRU and enUS installed). [ADR 0013](../adr/0013-remastered-data-source-and-hd.md) makes Remastered a data source of its own; this note records what its storage holds, how it compares with profile `bw-1.17.0.1`, and what importing its graphics and interface would take.

## Reading the storage

`converter/casc/storage.py` (`CascStorage`) reads a local CASC install read-only: `.build.info` (the active row) → build config → encoding table → local indices (`Data/data/*.idx`, the latest version of each bucket) → `data.NNN` → BLTE (`N`, `Z`, `F` chunks; encrypted `E` chunks are rejected). Remastered's root file is text, one `name|content key` line per file (52,498 names). Lookups accept `/` or `\` and ignore case, so MPQ paths resolve as they are; every read checks the decoded bytes against their MD5 content key.

```
build/cmake/kp_convert extract /Applications/StarCraft 'arr\units.dat' units.dat
python3 -m converter scr-compare --data <profile dir> --scr /Applications/StarCraft
python3 -m converter scr-digests --scr /Applications/StarCraft --out scr-digests.tsv
```

`scr-digests` lists each manifest file's normalized name, status and SHA-256 as the converter reads it; `kp_convert digests /Applications/StarCraft --out PATH` writes the same list from the runtime's C (`kp_res`, ADR 0018 stage D6), and `tools/ci.sh` compares the two.

`converter/tests/test_casc.py` checks the reader on a synthetic storage, and with `KOPRULU_ORIGINAL` and `KOPRULU_SCR` that Remastered's rules files equal the profile's.

## Rules data: identical to 1.17.0.1

`scr-compare` on 1.23.10.13515: all eleven DAT tables (`flingy`, `images`, `mapdata`, `orders`, `portdata`, `sfxdata`, `sprites`, `techdata`, `units`, `upgrades`, `weapons`) and `scripts\iscript.bin`, `aiscript.bin`, `bwscript.bin` equal the profile's effective copies byte for byte (0 of 14 differ). Unit, weapon, upgrade and tech values, the animation scripts and the campaign AI scripts are the same. `scripts/iscriptx.bin` (40,628 bytes) is an additional IScript in the older layout without the header offset (the entry table starts at byte 0); nothing here needs it.

Tilesets grow by appending only. For `badlands`, the first 86,580 bytes of `.cv5` (1,665 groups), 155,008 of `.vf4` (4,844 megatiles) and 1,679,808 of `.vr4` (26,247 minitiles) equal the profile's files; Remastered adds 314 groups, 1,328 megatiles and 21,248 minitiles. `.vx4` becomes `.vx4ex` with 32-bit references, whose first 77,504 entries equal `.vx4`'s. `.wpe` is unchanged. Classic maps reference only the original part.

## The 1.17 files the conversion reads

Every file profile `bw-1.17.0.1`'s conversion reads (4,376, recorded with `scr-manifest`) was looked up in the storage; content keys of locales not installed locally are listed in the root but cannot be read.

| Inputs | In CASC | Status |
|---|---|---|
| `arr\*.dat`, 4 `arr\*.tbl`, `scripts\*.bin`, 835 unit GRPs, every LO* overlay, cursors, `game\*.grp`, tileset PCX and WPE, 58 glue PCX, the `install` tileset | same path | byte-identical |
| 68 campaign `scenario.chk`, 1,123 unit and interface sounds, 451 mission sounds, 57 `rez\Est*.txt`, 5 FNT fonts | `SD/` (fonts `SD/font/`; `font/*.fnt` are 1,804-byte stubs) | byte-identical; the root's sounds are Remastered's own (16-bit) |
| `dlgs\<race>.grp`, `unit\cmdbtns\*` (4 GRPs, `ticon.pcx`), `glue\PalNl\pOPopup.pcx` | `game/consoles/<race>/dlg.grp`, `…/cmdbtns.grp`, `unit/cmdicons/cmdicons.grp`, `game/ticon.pcx`, `glue/Battlenet/ToonRegistry/popopup.pcx` | byte-identical |
| 7 grown tilesets, `.vx4` | prefix of `.cv5`/`.vf4`/`.vr4`; `.vx4ex` | derived exactly; `desert`, `platform`, `twilight` also fill CV5 groups that are empty in 1.17 (770, 933, 797 through 1023) |
| 173 glue and console PCX | `SD/…/*.DDS`, uncompressed RGBA; the palette from a present PCX of the same screen (171) | 154 exact by rule (alpha 0 → index 0, duplicate colours → the lowest index); the others equal in colour only (full-screen backgrounds with duplicate palette colours) or cropped (consoles, 188 rows of 480) |
| 6 `rez\*.tbl` | `rez/stat_txt.tbl` (its first 1,539 of 1,547 strings identical, the last 8, replay labels, missing); the others only as `SD/rez/*.xml`, `.json`, `.txt` with string ids | not in TBL order, and edited (typo fixes) |
| 69 `rez\*.bin` dialogs | `SD/rez/*.ui.json` for 68 | the JSON lacks the +0x36 words (154 widgets), type 14's extra words (86), the SMK nodes (20) and the shared bitmaps; 23 dialogs have another widget count |
| 25 glue `Dlg.grp`/`arrow.grp`, `conover`, `pbrfull` | `*.dds.grp`, DDS in DXT1/DXT5 | lossy |
| 549 SMK (portraits, glue movies) | `SD/…/*.webm` (VP9 `gbrp`, VP8 with alpha) | lossy (largest error 43 of 255 per channel) |
| 20 music WAVs, 654 mission WAVs | `SD/music`, `SD/campaign` | re-recorded or slightly altered PCM |
| StarCraft.exe's requirement tables and VCOD resource | none (code encrypted) | kept in `defs/` |

## Engine: not comparable statically

The executable's `__text` section is encrypted (entropy 7.997–7.998 bits per byte over the whole section; disassembly is noise), and the binary carries only 923 symbols. Its strings show what the engine adds, not how it behaves:

- an EUD emulation layer presenting the 1.16.1 memory layout to map triggers (`eud_cunit_adapter_t`, `eud_csprite_array_adapter_t` and others, `EudUnsupportedMap`), so the internal structures changed;
- extended object limits (`Game Extended Unit Limits`, `OnOriginalGameLimitsChanged`, a limits dump over images, sprites, thingies, units, bullets, orders and fogged building thingies) and the replay sections `LMTS` (limits), `BFIX` (per-game bug-fix flags) and `CCLR` (player colours);
- network turn rate and latency settings (`Game TurnRate`, `NetLatency`, `DefaultLatency`), `App.ExtendedGameSpeed`, observers.

The limit values and the contents of `BFIX` are not established here. A behavioural comparison would play Remastered replays (their command streams) through `kp_core`; it needs a parser for the replay format of 1.18+ and replays recorded by the user.

## Graphics

Each `images.dat` entry (999) has its own `anim/main_NNN.anim` (HD, ×4 scale, 2,048 MiB in total) and `HD2/anim/main_NNN.anim` (×2, 513 MiB); SD is one file, `SD/mainSD.anim` (37 MiB, 999 entries, DXT1 diffuse plus a BMP team-colour mask). `SD/images.rel` records the images that reuse another image's frames. Header: `ANIM`, scale (4, 2, 1), type (2: one entry, 1: the SD table), layer count, entry count, ten 32-byte layer names. HD layers: `diffuse` (DXT5), `bright` (DXT5), `teamcolor` (DXT1), `normal` (DXT5), `specular` (DXT1), `ao_depth` (DXT5); `emissive` is declared but empty in the entries checked. Frame counts equal the GRPs' (`main_000`: 85 frames = 17 directions × 5), so the IScript goldens apply unchanged; frame offsets are in the scale's pixels.

Terrain: `TileSet/<name>.dds.vr4` holds one DDS tile per megatile, 128 × 128 (64 × 64 in `HD2/`, DXT1), so HD tile *i* is megatile *i* of `.vf4`/`.vx4ex`: the HD art replaces the composition of 8 × 8 minitiles. Layout: `u32` file size, `u16` count, `u16` version (`0x1004` in HD, `0x1002` in HD2), then per tile `u32` 0, `u16` width, `u16` height, `u32` size and the DDS file. Checked by image on the HD2 sets (each SD megatile composed from `.vx4ex`/`.vr4`/`.wpe` and both reduced to 8 × 8 means): the mean absolute difference of the identity pairing is 5.1–9.9 of 255 against 15.5–56.5 for a random pairing, and the identity pairing is closer than the random one for 95.6–99.3 % of the megatiles (`ashworld`, `badlands`, `desert`, `ice`). Five tilesets have exactly as many HD tiles as megatiles (`ice` 7,174, `install` 1,431, `jungle` 7,038, `platform` 5,711, `twilight` 8,552). Three have trailing tiles no terrain file references (every CV5 reference is below the megatile count): `ashworld` +528 and `desert` +256 are copies of existing megatiles' tiles (415 and 185 byte-identical, the rest identical at 8 × 8), `badlands` +416 are flat grey placeholder shapes (86 distinct). Water and lava use masks instead of palette cycling: `.tmsk` (`KSMT`, `u16` version 1, `u16` count, then `u16` megatile and `u16` frame pairs; megatiles of the original range only) selects a frame of `<name>_mask.dds.grp` for `ShadersGLSL/water.glsl`.

`.fol` is the foliage map of the option "Environment Effects" (ruRU "Эффекты окружения"; its tooltip: small objects added to randomize the terrain's appearance). The binary has `LoadFoliage(const char*, const char*, Foliage*)`, `FoliageMap`, `foliage.bin`, `foliage.anim`, the keys `distributionLambda` and `distributionThreshold`, the setting `ShowFoliage` and the draw layer `DrawLayer_Foliage` between `DrawLayer_Tilemap` and `DrawLayer_TilemapEffect`, under creep and units, so it is decoration only. The pieces:

- `TileSet/foliage.bin`, shared by every tileset: 79 records of 60 bytes, nine `f32` placement parameters (17 distinct sets; the last two grow with the sprite: 1.0, 2.0, 3.0, 6.0), then `u32` frame count (1, or 16 and 64 for the two animated types), `u32` flags (`0xf`, `0x50f`, `0xa0f`, `0xa`, `0x2000f`), `u32` sheet of `foliage.anim`, `u32` first frame, `u32` width and height in HD pixels (128, 256, 512, or the sprite's own size for the three water items).
- `TileSet/foliage.anim` (and `HD2/`): three DXT5 sheets. Sheet 0 (90 frames): grass tufts, cracks, scorch lines, lava cracks and pits, bones, rocks, pebbles, hatches, with a 16-frame animation at frames 74–89 (type 74); sheet 1 (64 frames): a small animated creature (type 75); sheet 2 (3 frames): debris (types 76–78).
- `TileSet/<name>.fol`: `u32` count of the tileset's original megatiles, `u32` 79 (types), `u32` 10 (mask bytes), then for each original megatile a `f32` and a 10-byte mask of the types that may appear on it. Remastered's appended megatiles have no entry. The `f32` is 0 or 10 on most megatiles and 28–50 on a few that carry foliage, presumably the distribution's density (`distributionLambda`). Megatiles with foliage: `badlands` 628 of 4,844, `ashworld` 121, `desert` 174, `ice` 100, `jungle` 87, `twilight` 49, `install` 20, `platform` 170. In `jungle` the water megatiles 49–59 carry the water debris (76, 78) and bit 79, which no record describes.
- The Carbot skin's `.fol` files equal these byte for byte: the map depends on the terrain, not on the art. Icons, dialog frames and cursors are `*.dds.grp`: one DDS per frame. `ShadersGLSL/` covers deferred lighting, team colour, the cloak buffer, hallucination, heat distortion and the health bars.

Importing this means a second, RGBA path beside the palette-indexed one: team colour from the mask instead of remapping indices 8–15, shadows, cloak and hallucination as shaders, water from masks. DXT data can go to desktop and WebGL as it is; Android and iOS need ASTC or ETC2 transcoded at conversion time. The palette-index checks (fog, panels, palette pass) do not apply to it; it would be an `approximation` under ADR 0011. The simulation does not depend on it. Remastered's HD art is the paid part of the product: like the MPQs, it can only be read from the user's own install, never shipped.

The sprites' lighting (`ShadersGLSL/`, read 2026-11-02; analysis only, the formulas restated in ADR 0013 After R5): the deferred path writes each sprite's colour (its team colour and hallucination applied), its normal, occlusion and depth, specular and emissive colours and a per-sprite amount (`data.z`) into buffers; a blit lights the screen. A normal is decoded from the normal map's red (x) and alpha (y), x negated for a mirrored frame, z = sqrt(max(1 − x² − y², 0.1)), normalized; on the HD2 pages +x points right and +y up, so the light from (200, 50, 500) comes from the upper left, in front. The light is white; the highlight is the half vector's cosine (light and eye, the eye at half the screen's width, a quarter of its height and 500 × the lighting scale up, the fragment at the origin) to the 16th power, only where the lambert term is positive; the result times `ao_depth`'s green, the point lights added (their z 125 × the lighting scale, a fragment's its depth × 75 × the scale, radius falloff), the emissive added, mixed over the plain colour by the amount. `sprite_forward_lit` is the same per sprite without the buffers. The amount, the lighting scale (`data.w`) and the point lights come from the executable, whose code is encrypted (above). On the HD2 pages the lit sprites' opaque texels average an occlusion of 0.70 (Terran) to 0.80 (Zerg) and a lambert term of 0.72–0.74, so with the whole light (amount 1) a sprite is about half as bright as unlit.

## Interface

`SD/rez/*.ui.json` (128 dialogs) carries the classic BIN dialogs one to one, rectangles in 640 × 480 (the command card at `[496,354,144,126]`), with new flags: `RC_ANCHOR_TOPRIGHT` (34 controls), `RC_ANCHOR_CENTER` (4), `RC_SMALLTEXT`/`RC_BIGTEXT`/`RC_BIGGERTEXT`, `RC_FLC_HD`, `RC_POPUP_BACKGROUND`, `RC_NO_TEXTKERN`. The HD console is three pieces, `console_left` 1970 × 700 (the minimap frame and a filler panel), `console_center` 1156 × 512 (the selection and status window), `console_right` 2286 × 700 (a filler panel, the portrait and the command card) (×4); the SD console is one 640 × 188 image, so SD is 4:3 only. Together the HD pieces span about 1,353 game pixels, more than 16:9 needs (about 853): the filler panels on the inner sides of the outer pieces are what a narrower screen covers or crops.

Options in the binary: `GameOriginalAspectRatioOption`, `GameConsoleSplitOption`, `VideoFilterModeOption`, `VideoLightingEnabledOption`, `HardwareCursorOption`. The options screen is HTML (`webui/dist/OptionsPanel/OptionsPanel.js`) and sends the client named settings, listed in the binary in the screen's order. `AspectRatio` is "Widescreen" or "Pillarbox" (`GameOriginalAspectRatioOption`). `MinimapPosition` is a boolean shown as "Mini-Map Position" (ruRU "Положение мини-карты"): true is "Left Corner", false "Standard" (`bnet119_optionsPanel_game_label_consolePosition*` in `rez/gluAll.json`). It is the only boolean console setting without another option class, so it is `GameConsoleSplitOption`, saved under the key `consoleSplit`; the chat command `/console_split` toggles it. Read with the console art, "Left Corner" splits the console: the minimap's piece goes to the screen's left corner, apart from the rest of the console. This last part is inferred from the names and the art; the game was not run (the executable needs Battle.net). Text uses TTF fonts (`font/*.TTF`) and DDS bitmap atlases (`font8`…`font16x`); Battle.net, options and hotkeys are HTML panels (`webui/dist`).

Koprulu's layouts (phase 9) already go further (9:16 to 32:9, the narrow layout). Worth taking from Remastered: the three-piece console art for wide windows and vector fonts at large scales.

## Volumes (content sizes in the root)

| Category | MiB |
|---|---|
| HD anim (×4) | 2,048 |
| HD2 anim (×2) | 513 |
| SD `mainSD.anim` | 37 |
| Carbot skin | 678 |
| HD / HD2 tilesets | 423 / 113 |
| HD / HD2 glue and console art | 898 / 221 |
| HD / HD2 portraits (WebM) | 184 / 58 |
| Sounds and music | 612 |
