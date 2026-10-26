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
