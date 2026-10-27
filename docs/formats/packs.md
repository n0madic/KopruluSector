# Resource packs and mission sounds

The graphics and sounds the SDL runtime loads per mission ([ADR 0012](../adr/0012-sdl3-runtime.md); packs: [ADR 0004](../adr/0004-resource-packs.md)), written by `python3 -m converter resources --data DIR` after `missions` and `data`. Every file is a [Koprulu container](container.md) with the data set's profile id and source hash.

- **Writers:** `converter/kpf/resources.py` (containers), `converter/emit/pages.py` (atlas packer and pages), `converter/emit/packs.py` (the `resources` step that writes them).
- **Pack membership:** `converter/packs.py`. GRP geometry, atlas names per GRP and each GRP's first frame within its atlas: [`grpmeta.kpd`](kpd.md#grpmetakpd-schema-1).
- **Dump:** `python3 -m converter dump FILE`.
- **Tests:** `converter/tests/test_resources.py` (synthetic round trips and the packer; with `generated/` present, every GRP frame read back from the pack pages, the tileset sheets against the viewer's CRCs, the mission sounds against the `.kpm` `WAVS`; with `KOPRULU_ORIGINAL` also every frame against the GRP's indices).

All integers are little-endian.

| Path under `generated/` | Kind | Content |
|---|---|---|
| `packs/<pack>.kpd` | `PACK` | atlas pages, frame records and sounds of one pack: `doodads`, `effects`, `neutral`, `protoss`, `terran`, `zerg` (a pack with atlases or sounds) |
| `packs/music.kpd` | `MUSC` | the music tracks |
| `missions/<campaign>/<mission>_sounds.kpd` | `MSND` | the WAVS sounds of one catalog mission (every mission has one, count 0 allowed) |
| `reports/packs.json` | | per pack its atlases, sounds, OGG and WAV bytes, `kpd_bytes`, `pages`, `page_fill`, `page_pixels`, `distinct_frames`; per mission its sounds and `kpd_bytes`; totals under `kpd_bytes` |

## `PACK` (schema 1)

| Section | Class | Content |
|---|---|---|
| `ATLS` | canonical | u8 count; per atlas of the pack, in grpmeta `ATLS` order: u8 length + atlas name, u32 first frame in this file's `FRMS`, u32 frame count |
| `FRMS` | derived `SKY1` v1 | u32 count; per frame 10 bytes: u16 page, u16 x, u16 y, u16 width, u16 height |
| `PAGE` | derived `SKY1` v1 | u16 page count, u16 page width, u16 page height; per page: u32 size, an 8-bit grayscale PNG |
| `SNDS` | canonical | u16 count; per sound, sorted by sfx id: u16 sfx id, u32 size, the OGG bytes |

**Frames.** The frames of an atlas are those of the grpmeta `GRPS` entries whose atlas it is, in `GRPS` order, each GRP's frames 0 … n − 1 in order. Frame f of `GRPS` entry e is `FRMS[ATLS.first(atlas of e) + AFRM[e] + f]` of the entry's pack (grpmeta `AFRM`). A record's width and height are the GRP frame's; the frame's texels are rows `y … y + height − 1`, columns `x … x + width − 1` of page `page`, row-major, top row first. A frame of zero width or height is the record (0, 0, 0, 0, 0) and takes no page space. Frames with identical pixels (same width, height and indices) share one rectangle: stock GRPs repeat many frames, so the six packs store 10,654 distinct images for 20,598 frames.

**Pages.** Every page of every pack is 2048 × 2048, so the runtime can put the pages of all loaded packs into one texture array. A texel is the palette index, 0 transparent; every texel outside the frames is 0. The runtime reads texels by integer coordinates, so frames have no padding, extrusion, rotation or trimming. Pack pages have no opacity mask: the converter rejects a GRP that draws an opaque index 0 (`OpaqueZeroError`; ADR 0003; no stock GRP does). Portrait atlases are not part of the packs.

**Packer** (`converter/emit/pages.py`, producer `SKY1` version 1; the producer inputs hash is the SHA-256 of the pack's GRP files in `FRMS` order). The distinct frame images are sorted by height, then width (both descending), then by first use. Each goes to the first page where it fits, at the skyline position with the lowest top edge, ties to the lowest x; an image that fits no page opens a new one. The same inputs give byte-identical files.

**Sounds.** The `sfxdata.dat` sounds of the pack (`pack_of_sound`: `sound\<race>\…` → that race, every other sound `effects`), encoded to OGG (`oggenc -q 3`, a stream serial fixed per output path; `converter/emit/sound.py` `OggCache`). Ids without an existing file (0, 86, 104, 105, 1066) are absent. Ids naming one file each carry its bytes. `sounds.kpd` `PACK` names the pack of every sfx id.

## `MUSC` (schema 1)

`TRKS`: u8 count (the 25 tracks of `ui.kpd` `MUSI`, `0x508c58`); per track k: u8 flags (bit 0: the track loops, MUSI kind 1: StartMusicTrack `0x46f1b0` opens those with the loop flag `0x40000`), u32 size, the OGG bytes. A track without a file (track 0) has size 0. Tracks that name one file (16 and 19, 17 and 20, 18 and 21, 5 and 23) each carry its bytes.

## `MSND` (schema 1)

`SNDS` as in `PACK`, keyed by the mission's sound index: entry i is sound index i of the mission's [`WAVS`](kpm.md#wavs) (the position among existing sounds in string-id order). Indices run 0 … count − 1.

## Stock contents

| Pack | Frames | Distinct images | Pages | Fill | Sounds | OGG bytes | File bytes |
|---|---:|---:|---:|---:|---:|---:|---:|
| terran | 3,510 | 1,906 | 1 | 59.0 % | 378 | 5.37 M | 6,141,008 |
| protoss | 3,400 | 1,492 | 1 | 86.2 % | 341 | 5.06 M | 6,087,540 |
| zerg | 6,171 | 2,582 | 2 | 66.3 % | 275 | 3.36 M | 5,188,128 |
| neutral | 2,706 | 1,453 | 1 | 44.0 % | 0 | 0 | 576,136 |
| effects | 4,358 | 2,794 | 3 | 75.0 % | 145 | 1.56 M | 3,561,076 |
| doodads | 453 | 427 | 1 | 47.7 % | 0 | 0 | 599,332 |

Fill is the frame texels over the page texels. Each pack uses the fewest 2048² pages its distinct frame texels allow. The OGG bytes count each file once; the file repeats the bytes of sfx ids that name one file.

Music: 25 tracks (track 0 without a file), 31,457,684 bytes. Mission sounds: 64 files, 1,105 sounds, 57,304,060 bytes in all, at most 2,691,980 for one mission.

## HD art (ADR 0013): `HDPK` and `HDTS` (schema 2)

`python3 -m converter hd --scr DIR [--variant hd2|hd|all]` writes StarCraft: Remastered's HD art from the player's install into `generated/hd/<variant>/` (`hd2`: ×2, `hd`: ×4; `converter/emit/hd.py`); it is never shipped. The runtime reads it with `kp_present/kp_hd.c` and draws it with `kp_gfx`'s HD world (`--graphics`).

**`packs/<pack>.kpd` (`HDPK`)**, one per SD pack, its frames in the SD pack's `FRMS` order (so an SD frame's global index is its HD frame's):

| Section | Content |
|---|---|
| `HEAD` | u8 scale (2 or 4), u16 page size (2048), u16 pages, u32 frames, u8 the pages' format (0: BC3 diffuse and BC1 team blocks; 1: ETC2 RGBA8 with EAC alpha and EAC R11, the same sizes) |
| `FRMS` | per frame: u16 page, x, y, width, height of the HD frame in its page; i16 dx, dy: its centre less the SD frame's centre times the scale, in half HD pixels, y down (both boxes are centred on the image's position); width 0 without HD art |
| `DIFF` | the pages' BC3 blocks: the diffuse sheets |
| `TEAM` | the pages' BC1 blocks: the team colour masks (zero where an image has none) |

An image's frames come from its own anim (`anim/main_<image>.anim`, `HD2/anim/…`) or the one `SD/images.rel` names; a GRP takes the first images.dat entry naming it. Each frame keeps its BC blocks: its rectangle in the sheet widened to whole blocks (a rectangle two frames share is placed once; its frame's `x`, `y` are inside it). The rectangles are packed by image, tallest first, bottom-left along a skyline into the last four pages still open, else a new page, so an image's frames lie on few pages. The page is the unit the runtime streams: the pack is opened from its header, section table and `FRMS` (`kp_hd_pack_head_size`, `kp_hd_pack_open`), and a page's blocks are read at `DIFF` and `TEAM`'s offsets plus the page's index times its size (`kp_hd_page_offsets`). On 1.23.10.13515 the frames fill 80 % (HD2) and 82 % (HD) of the pages: HD2 48 pages, 354 MB with the tilesets; HD 189 pages, 1.4 GB (schema 1 packed each image's sheet whole into 4096² and 8192² pages, 51–75 % full, 498 MB and 1.9 GB).

**`tilesets/<name>.kpd` (`HDTS`)**: `HEAD` (u16 tile count: the tileset's 1.17 megatiles, u16 tile size: 64 or 128, FourCC `DXT1`, or `ETC2` for ETC2 RGB8 blocks), `TILE` (each megatile's blocks; tile *i* is megatile *i* of `.vf4`/`.vx4`, Remastered's appended tiles left out); where the tileset has water (`TileSet/<name>.tmsk`: `KSMT`, u16 version, u16 count, then u16 megatile and u16 frame of `<name>_mask.dds.grp`), `WMAP` (u16 per megatile: 0 dry, else 1 + its mask) and `WMSK` (the masks, tile size squared bytes each: the BC1 frame's red, 255 water; empty and repeated ones left out). On 1.23.10.13515 `badlands`, `jungle`, `ashworld` (its lava), `desert`, `ice` and `twilight` have masks; `install` and `platform` none.

**`ui.kpd` (`HDUI`)**: the interface's HD art (`converter/emit/hd_ui.py`): the GRPs and PCX of `ui.kpd` (its dialog GRPs, PCX and glue files) that have HD art, frame for frame. A GRP's `*.dds.grp` (`unit\cmdbtns\cmdicons.grp` from `unit/cmdicons/`, a race's `cmdbtns.grp` and `dlgs\<race>.grp` from `game/consoles/<race>/`), a PCX's `.DDS` when it is the SD picture drawn again (the correlation of their luminance gradients, the HD art centred on the SD picture, at least 0.5: Remastered drew most glue screens anew for its own layout, and those stay SD), a console PCX (`game\<z|t|p>console.pcx`) its three pieces `console_left`, `_center`, `_right` on the screen's bottom: the outer ones at the edges of an 853-wide screen around the 640 columns, the centre one where its luminance edges over the status panel's columns (138–408) agree best with the SD console's (a search of ±12 logical pixels from column 138). `HEAD` (u8 scale, u16 assets, u32 frames), `ASET` (per asset a 64-byte NUL-padded lowercase SD path, u32 first frame, u16 frames, u16 flags: 1 the console's pieces), `FRMS` (per frame u32 offset into `IMGS`, u32 size, u16 width, height in HD pixels, u8 format: 0 BC1, 1 BC3, 2 PNG RGBA, u8 0, i16 x, y: a piece's top-left in the console PCX's pixels), `IMGS`. An HD frame is drawn centred on its SD frame's trimmed rectangle (most are exactly twice its size in HD2); a picture centred on its SD one. On 1.23.10.13515 HD2 has 60 assets, 2,174 frames, 35 MB (128 pictures drawn anew left out; HD 64 assets, 142 MB); the pieces sit at x −106, 135–144 and 172–183. Phones get it as it is (decoded on the CPU into the atlas).

**`portraits.kpd` (`HDPV`)**: the HD portraits (`converter/emit/hd_video.py`): each portrait movie of `portraits.kpd` (`portrait\<unit>\<name><variant>.smk`) from its WebM (`HD2/portrait/<unit>/<name><variant>.webm`, else the HD one) as [KPV1](../../converter/formats/kpv.py) (one palette of the clip, no dithering) with the SD movie's frame count, rate and ring frame, so the SD movie's frame n is the clip's frame n. Both variants take HD2's clips (224 × 240; 17 are 112 × 120): Remastered's HD console shows a portrait about 200 × 240 pixels wide at 4 ×, which they fill, and HD's 448 × 480 would be four times the bytes. `HEAD` (u16 clips), `CLIP` (per clip a 64-byte NUL-padded lowercase SD path, u32 offset into `DATA`, u32 size), `DATA` (the clips, each from a 4-byte boundary). The runtime reads a clip when its movie is first shown and decodes its frames on the CPU. On 1.23.10.13515: 621 of 621 movies, 281 MB. Remastered's HD glue movies are not imported: the main menu's are drawn for its own layout (`glue/mainmenu/single.webm` 1700 × 700 against the SD 320 × 116), the campaign's are the SD movies' size.

**`water.kpd` (`HDWA`)**: the water's normal maps, `effect/water_normal_1.dds.grp` (the large waves; HD2 45 frames of 64²) and `_2` (the detail; 120 frames of 256²): `HEAD` (u16 frames, u16 size of each set, FourCC `DXT1` or `ETC2`), `NRMA`, `NRMB` (their blocks, frame after frame).

**Phones (format 1, `ETC2`).** `tools/product.sh android|ios|ios-sim` stages HD2 transcoded by `kp_hd_etc2` (`native/kp_core/tools/kp_hd_etc2.c`, `kp_present/kp_etc.c`) into `build/hd-etc2/`, again when a source is newer: each page's BC blocks decoded and encoded as ETC2 RGBA8 (ETC1's individual and differential colour modes, the colour error weighted by alpha; EAC alpha) and EAC R11 (the team mask), each tile as ETC2 RGB8, which every GLES 3.0 takes (S3TC is an extension many phones lack). The frames and the other header fields stay. HD2 in about 18 s on 14 cores, the same 354 MB; against the BC pages the premultiplied colour is 37–42 dB, the alpha 46–51 dB, the masks (0 and 255) exact.
