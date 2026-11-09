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
| `speech/<loc>/missions/<campaign>/<mission>_sounds.kpd` | `MSND` | a language's spoken lines of the mission (`converter speech`, ADR 0016), by the base's sound index; a line the language lacks is the base's sound |
| `speech/<loc>/units.kpd` | `MSND` | a language's unit voices, keyed by sfx id (only the ids it has); the app looks a voice up here before the packs |
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

The product does not hold the 31 MB container: at start `kp_music_index` (`native/kp_app/kp_music.c`) reads the file's head and the five-byte header of each track (the count, then each track's flags and size) and keeps the tracks' places (offset, size, loop flag; at most `KP_MUSIC_TRACKS` = 32), and `kp_app_audio.c` plays a track as a stream of its byte range in the file (`MIX_SetTrackIOStream`: SDL3_mixer decodes it as it plays, so no track is held in memory; the file is closed when another track starts or the music stops). The web keeps the whole container (its files are in memory already) and streams slices of it. An index that cannot be complete (more than 32 tracks, a track table cut short, a header that cannot be read, a track past the section) is refused whole, and the game plays without music. `test_music`.

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

## HD art (ADR 0013): `HDPK` (schema 3) and `HDTS` (schema 2)

`python3 -m converter hd --scr DIR [--variant hd2|hd|all]` writes StarCraft: Remastered's HD art from the player's install into `generated/hd/<variant>/` (`hd2`: ×2, `hd`: ×4; `converter/emit/hd.py`); it is never shipped. The runtime reads it with `kp_present/kp_hd.c` and draws it with `kp_gfx`'s HD world (`--graphics`).

**`packs/<pack>.kpd` (`HDPK`)**, one per SD pack, its frames in the SD pack's `FRMS` order (so an SD frame's global index is its HD frame's):

| Section | Content |
|---|---|
| `HEAD` | u8 scale (2 or 4), u16 page size (2048), u16 pages, u32 frames, u8 the pages' format (0: BC3 diffuse and BC1 team blocks; 1: ETC2 RGBA8 with EAC alpha and EAC R11, the same sizes; 2: ASTC 4 x 4 and 8 x 8; the lit layers in the phones' section below), u16 lit pages (the first pages) |
| `FRMS` | per frame: u16 page, x, y, width, height of the HD frame in its page; i16 dx, dy: its centre less the SD frame's centre times the scale, in half HD pixels, y down (both boxes are centred on the image's position); width 0 without HD art |
| `DIFF` | the pages' BC3 blocks: the diffuse sheets |
| `TEAM` | the pages' BC1 blocks: the team colour masks (zero where an image has none) |
| `NRML` | the lit pages' BC3 blocks: the normal maps (x in red, y in alpha); present only with lit pages, as the three below |
| `AODP` | their BC3 blocks: the ambient occlusion (green) and the depth (alpha) |
| `SPEC` | their BC1 blocks: the specular colours |
| `EMIS` | their BC1 blocks: the emissive colours (black where an image has none) |

An image's frames come from its own anim (`anim/main_<image>.anim`, `HD2/anim/…`) or the one `SD/images.rel` names; a GRP takes the first images.dat entry naming it. Each frame keeps its BC blocks: its rectangle in the sheet widened to whole blocks (a rectangle two frames share is placed once; its frame's `x`, `y` are inside it). The rectangles are packed by image, tallest first, bottom-left along a skyline into the last four pages still open, else a new page, so an image's frames lie on few pages. The images Remastered lights (their anim has `normal` and `ao_depth`) are packed first, onto pages of their own, the lit pages; only they keep their lighting layers, in the same rectangles as their diffuse blocks (`bright` is left out). The page is the unit the runtime streams: the pack is opened from its header, section table and `FRMS` (`kp_hd_pack_head_size`, `kp_hd_pack_open`), and a page's blocks are read at `DIFF` and `TEAM`'s offsets plus the page's index times its size, a lit page's layers at `NRML`, `AODP`, `SPEC` and `EMIS`'s (`kp_hd_page_offsets`). On 1.23.10.13515 HD2 has 50 pages, 20 lit, 540 MB of packs; HD 193 pages, 72 lit, 2.0 GB (schema 2, without the layers, packed the lit and unlit images together: HD2 48 pages, 354 MB with the tilesets; HD 189 pages, 1.4 GB, the frames filling 80 % and 82 % of them; schema 1 packed each image's sheet whole into 4096² and 8192² pages, 51–75 % full, 498 MB and 1.9 GB).

**`tilesets/<name>.kpd` (`HDTS`)**: `HEAD` (u16 tile count: the tileset's 1.17 megatiles, u16 tile size: 64 or 128, FourCC `DXT1`, or `ETC2` for ETC2 RGB8 blocks, also in a phone's ASTC build), `TILE` (each megatile's blocks; tile *i* is megatile *i* of `.vf4`/`.vx4`, Remastered's appended tiles left out); where the tileset has water (`TileSet/<name>.tmsk`: `KSMT`, u16 version, u16 count, then u16 megatile and u16 frame of `<name>_mask.dds.grp`), `WMAP` (u16 per megatile: 0 dry, else 1 + its mask) and `WMSK` (the masks, tile size squared bytes each: the BC1 frame's red, 255 water; empty and repeated ones left out). On 1.23.10.13515 `badlands`, `jungle`, `ashworld` (its lava), `desert`, `ice` and `twilight` have masks; `install` and `platform` none. `EDGE`: creep's edge frames (`TileSet/<name>.dds.grp`, 37 DXT1 images with one-bit alpha, the variant's scale times the SD frames' sizes or a little larger): u16 frame count, then each frame decoded into a tile of the tile size squared RGBA8 texels, transparent where it draws nothing, at the SD frame's offset in `tileset\<name>.grp` times the scale moved in so it stays in the tile; `kp_hd_etc2` keeps it as it is.

**`ui.kpd` (`HDUI`)**: the interface's HD art (`converter/emit/hd_ui.py`): the GRPs and PCX of `ui.kpd` (its dialog GRPs, PCX and glue files) that have HD art, frame for frame. A GRP's `*.dds.grp` (`unit\cmdbtns\cmdicons.grp` from `unit/cmdicons/`, a race's `cmdbtns.grp` and `dlgs\<race>.grp` from `game/consoles/<race>/`), a PCX's `.DDS` when it is the SD picture drawn again (the correlation of their luminance gradients, the HD art centred on the SD picture, at least 0.5: Remastered drew most glue screens anew for its own layout, and those stay SD; the score screens' `untab.pcx` strips (SD 93 × 4) stay SD too, `hd_ui.SD_ONLY`: Remastered's strip is wider and laid over the SD panel it hid the tabs' notches), a console PCX (`game\<z|t|p>console.pcx`) five frames: its three pieces `console_left`, `_center`, `_right` and the two wings' tiles (`hd_ui.wing_tile`: a part of the piece's wing with no feature that catches the eye, its far end crossfaded into its start, PNG; the runtime repeats them), the pieces on the screen's bottom: the outer ones at the edges of an 853-wide screen around the 640 columns, the centre one where its luminance edges over the status panel's columns (138–408) agree best with the SD console's (a search of ±12 logical pixels from column 138). `HEAD` (u8 scale, u16 assets, u32 frames), `ASET` (per asset a 64-byte NUL-padded lowercase SD path, u32 first frame, u16 frames, u16 flags: 1 the console's pieces), `FRMS` (per frame u32 offset into `IMGS`, u32 size, u16 width, height in HD pixels, u8 format: 0 BC1, 1 BC3, 2 PNG RGBA, u8 0, i16 x, y: the image's top-left in HD pixels from the top-left of the SD frame it replaces, a GRP frame's, a PCX's or the console PCX's), `IMGS`; schema 3 (the other HD files 2). The runtime draws every frame at that offset (`kp_hd_ui_frame_place`); the converter decides it (`hd_ui.place_frame`): a GRP frame lies centred on its SD frame's trimmed rectangle (most are exactly twice its size in HD2), a wireframe (its square layers side by side) centred on the GRP canvas, unless the frame's opaque texels agree (intersection over union, the SD mask at the scale against the HD alpha at least half, a wireframe's layers ORed) by 0.3 better at another of those places or the canvas's origin, or 0.95 there: the minimap's `game\blink.grp` (frames 1–19 kept from the canvas's origin), the placeholder wireframes' text (`BLANK`, the unused units' names) and a few frames drawn again a pixel or two aside, 62 of HD2's 2,180 frames (the log names them); frames drawn anew agree at most 0.18 better elsewhere and keep their place. A picture lies centred on its SD one. On 1.23.10.13515 HD2 has 60 assets, 2,174 frames, 35 MB (128 pictures drawn anew and the 6 strips left out; HD 64 assets, 2,178 frames, 142 MB); the pieces sit at x −106, 135–144 and 172–183. Phones get it as it is (decoded on the CPU into the atlas).

**`portraits.kpd` (`HDPV`)**: the HD portraits (`converter/emit/hd_video.py`): each portrait movie of `portraits.kpd` (`portrait\<unit>\<name><variant>.smk`) from its WebM (`HD2/portrait/<unit>/<name><variant>.webm`, else the HD one) as [KPV1](../../converter/formats/kpv.py) (one palette of the clip, no dithering) with the SD movie's frame count, rate and ring frame, so the SD movie's frame n is the clip's frame n. Both variants take HD2's clips (224 × 240; 17 are 112 × 120): Remastered's HD console shows a portrait about 200 × 240 pixels wide at 4 ×, which they fill, and HD's 448 × 480 would be four times the bytes. `HEAD` (u16 clips), `CLIP` (per clip a 64-byte NUL-padded lowercase SD path, u32 offset into `DATA`, u32 size), `DATA` (the clips, each from a 4-byte boundary). The runtime reads a clip when its movie is first shown and decodes its frames on the CPU. On 1.23.10.13515: 621 of 621 movies, 281 MB, written once per run (`hd --variant all`: HD2's file, hard-linked into `hd/`). The converted clips are cached under `<out>/cache/scr` by their inputs' hash, written whole (a temporary file renamed) and read back only when every frame lies within the file. Remastered's HD glue movies are not imported: the main menu's are drawn for its own layout (`glue/mainmenu/single.webm` 1700 × 700 against the SD 320 × 116), the campaign's are the SD movies' size.

**`water.kpd` (`HDWA`)**: the water's normal maps, `effect/water_normal_1.dds.grp` (the large waves; HD2 45 frames of 64²) and `_2` (the detail; 120 frames of 256²): `HEAD` (u16 frames, u16 size of each set, FourCC `DXT1` or `ETC2`), `NRMA`, `NRMB` (their blocks, frame after frame).

**The web (stage R4b).** `tools/product.sh web` serves the HD2 files beside the page (`generated/hd/hd2/`) and preloads only their heads (a container's bytes before its first bulk section, `DIFF`, `TEAM`, `IMGS` or `DATA`: packs, `ui.kpd`, `portraits.kpd`; `tools/web_hd_heads.py`) and `generated/web_sizes.txt` (`path size` per file, the cinematics' packs among them, which `kp_app_read_head` reports as the file's size); the runtime fetches pages, frames and clips as byte ranges and the tilesets and `water.kpd` whole (`kp_app_fetch.c`). A cinematics pack's `DATA` must be its last section, since the player reads the others at once from the head (`web_hd_heads.py` refuses any other order). The server must answer Range requests (`tools/web_serve.py`); one that answers 200 costs the whole file per range, or the pack's movies once.

**Phones (formats 1 and 2).** `tools/product.sh android|ios|ios-sim` stages HD2 transcoded by `kp_hd_phone` (`native/kp_core/tools/kp_hd_phone.c`, `kp_present/kp_etc.c`) into `build/hd-astc/` and `build/hd-etc2/`, again when a source is newer (a cache's `.source` names the converter output and `kp_hd_phone`'s SHA-256; another of either empties the cache). `KOPRULU_PHONE_BLOCKS=astc|etc2` picks the packs' format: `astc` by default, `etc2` on `ios-sim`, whose context takes no ASTC. The frames and the other header fields stay, lit pages included.

| Section | BC (0) | ETC2 (1) | ASTC (2) | 2048² page |
|---|---|---|---|---|
| `DIFF` | BC3 | ETC2 RGBA8 (EAC alpha) | 4 x 4 | 4 MB each |
| `TEAM` | BC1 | EAC R11 | 8 x 8 | 2 MB, 2 MB, 1.05 MB |
| `NRML` | BC3 | EAC RG11: x, y | EAC RG11 | 4 MB each |
| `AODP` | BC3 | EAC R11: the occlusion | EAC R11 | 4 MB, 2 MB, 2 MB |
| `SPEC`, `EMIS` | BC1 | ETC2 RGB8 | 6 x 6 | 2 MB, 2 MB, 1.87 MB |

The tiles (`HDTS`) and the water's normal maps (`HDWA`) are ETC2 RGB8 with either: a tile is a whole number of blocks only at ASTC's 4 x 4 and 8 x 8. kp_gfx swizzles EAC RG11's green into alpha and the occlusion's EAC R11 red into green, so the shader reads each format alike; WebGL has no swizzle, so a browser refuses RG11's blocks and draws such pages decoded and unlit. ETC2's colour modes are ETC1's individual and differential, the colour error weighted by alpha; ASTC comes from Arm's encoder (`-thorough`, the host's tool only). Every GLES 3.0 takes ETC2 and EAC (S3TC is an extension many phones lack); a device without ASTC draws SD from an ASTC build. HD2's packs: 442 MB in ASTC, 501 MB in ETC2, and the tilesets and water 79 MB. Against the BC pages, HD2 in dB: ETC2's colour 31–34, alpha 46–54; ASTC's 34–38 and 44–52, its alpha weighted up; the normal maps' x and y 51 and 47; the occlusion 50; the masks (0 and 255) exact ([ADR 0013](../adr/0013-remastered-data-source-and-hd.md), After R5, the phones' pages).

## Cinematics (ADR 0015): `CINE` (schema 2)

`python3 -m converter cinematics --scr DIR [--variant sd|hd2|all] [--subtitles auto|none|LOCALE] [--out generated]` writes `generated/video/cinematics-sd.kpd` and `cinematics-hd2.kpd` (`converter/emit/cinematics.py`) from the player's Remastered install: the 28 movies of the original's `smk\` table (`defs/campaign/cinematics.json`) from `SD/smk/` or `HD2/Smk/`, WebM split by FFmpeg without re-encoding into the VP9 packets (IVF, `converter/formats/ivf.py`) and the Ogg Vorbis stream. The two packs are independent of each other and of the HD art (`generated/hd/`); the optional player (`-DKP_CINEMATICS=ON`) plays one of them with libvpx and SDL3_mixer. `python3 -m converter cinematics --data DIR` writes the same `cinematics-sd.kpd` from the 1.17 install instead (`converter/emit/cinematics_smk.py`, ADR 0015's 1.17 section): each Smacker movie, checked against `defs/campaign/cinematics.json` (`smk`), transcoded into VP9 4:4:4 at its own size and exact rate (50000/3333) and Ogg Vorbis by oggenc, without `SUBS`; the format is the same, the player cannot tell the two apart.

| Section | Content |
|---|---|
| `HEAD` | u8 variant (0 SD, 1 HD2), u8 0, u16 clips, u16 preludes |
| `CLIP` | per clip, 108 bytes: 64-byte NUL-padded lower-case name (`smk\<name>`), u8 id (the original's table index 0..27), u8 0, u16 0, u16 width, height as coded, u16 width, height as shown, u32 frame rate numerator and denominator, u32 frames, u32 offset and u32 size of the packets in `DATA`, u32 first frame in `FRMS`, u32 offset and u32 size of the Ogg in `DATA` (size 0: no sound) |
| `FRMS` | per frame, the clips in turn: u32 packet size, bit 31 set for a key frame; a clip's packets follow each other from its offset |
| `PREL` | per prelude u8 cinematic, u8 prelude (`PlayCinematicWithPrelude` `0x458fc0`'s table `0x4edad8`: the briefing's movie is played first) |
| `SUBS` | schema 2, only when a movie has subtitles: Remastered's `locales/<locale>/Assets/Smk/<name>.srt` (not the original's). 4-byte locale (two small letters, two capitals: `ruRU`), u32 cues, u32 text bytes; per clip (`CLIP`'s order) u32 first cue, u32 cues (one clip's after the other's); per cue u32 start and end in ms, u32 offset and size of its UTF-8 text in the text (its lines apart by `\n`), by their start; the text. At most 128 cues and 16 KB of text a clip |
| `DATA` | each clip's packets and its Ogg, each from a 4-byte boundary |

`--subtitles auto` (the default) takes the install's text locale (`.build.info`'s tags group with `text?`), else enUS, the first whose `.srt` files the local storage holds; a locale named must be held (Remastered downloads only its own locale's); `none` writes no `SUBS`. The player reads schema 1 and 2 (a schema 1 pack has no subtitles).

The converter refuses a movie whose video is not VP9, whose sound is not Vorbis (a movie without sound gets size 0), whose frame rate is none or has a term above 1,000,000, whose video starts more than half a frame before or after its sound (the real movies: 12 ms SD, 3 ms HD2), with a packet of 0 bytes (libvpx refuses one; the player refuses it too), whose container would pass 32 bits, or whose packets' times stray more than half a frame from that constant rate (the pack keeps the rate, not the times; the real SD and HD2 movies stray at most 0.015 frame). The player (`kp_cine_index`) checks the container's kind and schema (`kp_kpf_check`), every section's bounds and the same rate bound (`KP_CINE_MAX_RATE_TERM`).

SD movies are VP9 profile 1 (4:4:4, `gbrp`, full range), coded 320 wide and shown 640 wide (a pixel of 2:1; 640 × 308 is the original's size); their frame counts equal the retail SMKs'. The HD2 movies are 960 × 540, 4:2:0, studio range; their `Pre*` movies run at 29.97 fps, `blizzard` is SD's 320 × 308 (shown 640 wide), and some frame counts differ from the SD set's (`starintr` 2081 against 2082), so a clip's rate and count are its own. On 1.23.10.13515: SD 28 movies, 190 MB; HD2 329 MB; both in about 4 s (no conversion, a copy). The sound's length equals the video's (the intro: 138.80 s of Vorbis, 2082 frames at 15.0015 fps). Both streams were decoded back out of the pack (libvpx's output equals FFmpeg's, `docs/adr/0015-video-decoder.md`).
