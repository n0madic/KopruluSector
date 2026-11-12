# ADR 0019: Everything from an install or from `koprulu*.mpq`, the converter in C

Status: accepted (phase C, 2026-11-11; the user approved the plan after two drafts and chose: the folder `--data` names decides the source and nothing else; the repack compact and recompressed for the phones and the web; the converter fully in C; the 1.17 cinematics transcoded in C; Python kept for research only). It replaces, in part:
- [ADR 0013](0013-remastered-data-source-and-hd.md)'s rule that the HD art comes from the converter only;
- [ADR 0015](0015-video-decoder.md)'s cinematics packs (`CINE`) and its transcode through FFmpeg and oggenc;
- [ADR 0016](0016-localization.md)'s refusal to read Remastered's language files at run time (its "Reading Remastered's files at run time" alternative);
- [ADR 0018](0018-original-data-at-run-time.md) item 5 (the converter's role) and its repack's priority over the install (item 3).

Stages and their state: [docs/phaseC.md](../phaseC.md).

## Context

Since phase D the game reads its SD data straight from an install, a 1.17 install's MPQ archives or StarCraft: Remastered's CASC storage. Five kinds of data still go through the Python converter and files of their own in `generated/`:
- the HD art (`generated/hd*/`);
- the console's pieces (`console.kpd`);
- the cinematics (`CINE` packs);
- the languages' overlays;
- the speech.

A player with a Remastered install therefore still needs a conversion step, with Python, Pillow, FFmpeg and oggenc. The converter also repeats the runtime: `converter/casc/rebuild.py` is `kp_scr.c` again, `converter/emit/pages.py` is `kp_pages.c` again, and `kp_data_probe --digests` checks the twins against each other.

## Decision

1. **The source is the folder `--data` names, and nothing else** (`kp_res`'s detection, stage D1):
   - **an install** (1.17 or Remastered): every asset in its original format from MPQ or CASC. That covers SD, the HD world, the HD interface and portraits, the console, the movies, the languages' texts and the speech, with no conversion step;
   - **a folder of `koprulu*.mpq`**: everything from there, as `kp_convert` repacked and recompressed it for compactness.

   There is no third source and no mixing: the runtime reads none of `generated/hd*`, `generated/locale`, `generated/speech`, `generated/video` or `console.kpd`, and a repack no longer falls back to an install beside it file by file. The runtime is the same on every platform; only the bundles differ in what they carry.

2. **One builder per kind of data, in C, shared by the runtime and `kp_convert`.**
   - Texts, speech and movies are read by the same code from both sources; only where the bytes come from differs.
   - The HD world, the HD interface and the console have two readers:
     - a builder from the originals (anim, `images.rel`, DDS, `dds.grp`, VR4);
     - a loader of the containers `HDPK`, `HDTS`, `HDWA`, `HDUI`, `HDPV` and `CONW`.

     A container is the serialization of the builder's structures, and `kp_convert` writes it by calling the same builder.
   - The HD interface's placement heuristics (`place_frame`, `same_picture`, `place_piece`) run only in `kp_convert hd-ui-table`. It writes `defs/scr/hd-ui-bw-1.17.0.1.json`, keyed by each DDS's SHA-256. The runtime applies the table; a DDS whose digest it does not list falls back to the SD art, with a log line naming the command that regenerates it.

3. **`kp_convert`**, a C tool on `kp_app_core`, replaces the Python converter. Its commands:
   - `repack`, `verify`, `digests` and `manifest`, which absorb `kp_data_probe`;
   - `icon`, `scenarios`, `terrain`, `hd-ui-table` and `locale-check`.

   It is built for every desktop: Windows, Linux and macOS. Its encoders are built for the host only:
   - **libvpx's VP9 encoder:** the same release as the decoder, in a second generated configuration;
   - **libogg 1.3.6 and libvorbis 1.3.7 with vorbisenc:** BSD, fetched by `tools/toolchain/fetch.sh vorbis`, built as `kp_vorbis`.

   The CMake option `KP_ENCODERS` (on where the host's tools are built; off for the product's page and the phones' cross builds) selects libvpx's `<arch>-enc` configuration and builds `kp_vorbis` and `kp_convert`.

   FFmpeg and oggenc are no longer needed. The `converter/` package is deleted at stage C7. Python stays for research only: the emulator, the oracles, `tools/research` (the format readers they need move there) and `tools/defs_c.py`, which generates the committed C tables.

4. **The repack is compact.**
   - The HD pages are BC; the phones' are ETC2 or ASTC (`kp_hd_phone`).
   - The speech is Ogg Vorbis at 22,050 Hz mono when that is smaller. Its original header is kept, for the durations.
   - The 1.17 cinematics are VP9 4:4:4 plus Vorbis, in WebM of our own muxing (nestegg reads it back). Remastered's WebM go in as they are.
   - The listing `(koprulu)` becomes version 3:
     - the containers under internal names `koprulu\...`;
     - Remastered's files under their raw names;
     - each uncompressed file's offset, for the web's HTTP Range reads;
     - the groups: `core`, the missions, the music, `hd2`, `hd`, `ui`, `locale-<loc>` and `cine`.

     An MPQ over 4 GB is written as version 4.

5. **Deleted:**
   - the `CINE` packs;
   - the overlays `STRS`, `UI`, `FONT`, `MISN`, `MSND` and `SNDS`;
   - `glyphs.kpd`;
   - the loose files the runtime read from `generated/`.

   The formats `HDPK`, `HDTS`, `HDWA`, `HDUI`, `HDPV` and `CONW` do not change; they move into the MPQ. Saves do not change: a trigger's TIME, merged from the language's CHK when a mission loads, is stored in the saved copies of the triggers (`a_save_keeps_its_speech`).

6. **Each emitter's output is recorded before it is deleted.** For each Python emitter that goes, a private golden of its output is recorded first, and the C builder must match it byte for byte:
   - the overlays' sections;
   - the `HDPK` pages;
   - the `HDUI` frames;
   - `CONW`.

   A test then checks that what is built from an install equals what is loaded from the repack.

## Measurements (stage C0)

macOS on an M-series Mac, StarCraft: Remastered 1.23.10 at `/Applications/StarCraft` and the 1.17.0.1 install, release builds:

- **HD pages from anim** (every one of the 868 anims):
  - HD2: headers in 0.14 s (748 KB), all files in 1.13 s (537.6 MB, 475 MB/s), the blocks copied in 24 ms;
  - HD ×4: 0.12 s, 3.68 s (2,147.6 MB, 584 MB/s), 0.11 s;
  - a mission reads a subset, and the layout needs the headers only.
- **Glyphs at start** (`kp_fnt_supplement`): 5 fonts × 271 codes in 33 ms.
- **A language in CASC** (ruRU): 361.5 MB in all, of which the speech is 216.9 MB of Ogg and 125.3 MB of WAV, CHK 17.2 MB and the texts about 1 MB. The converter's speech at 22,050 Hz mono is 69 MB.
- **libvorbisenc against `oggenc -q 3`:**
  - `buzz.wav`: byte for byte the same;
  - `paryes00.wav` (22,050 Hz mono, 49,989 samples): the same pages, granules and packet counts, 14,764 against 14,761 bytes; two audio pages differ by 1 to 2 bytes.
- **libvpx's VP9 encoder against FFmpeg's `libvpx-vp9`** (both libvpx 1.17.0):
  - `blizzard` (171 frames) and `Terran2` (508 frames): the IVF the same byte for byte (745,822 and 1,638,260 bytes);
  - PSNR against the SMK's frames as `kp_smk` decodes them: 43.25 and 41.41 dB, as ADR 0015 measured;
  - one thread: 2.3 s and 11.5 s;
  - the x86-64 build (mingw, its SIMD chosen at run time by cpuid; run under Wine and Rosetta, so which extensions ran is not known): `blizzard`'s IVF the same bytes again.

Consequences of the measurements:
- **HD:** building the pages from the originals at a mission's start fits; no layout table in `defs/` is needed (the plan's fallback).
- **Glyphs:** rasterizing the supplement's glyphs at start replaces `glyphs.kpd`.
- **Speech:** recompressing the speech in the repack saves about 5×.
- **Encoding:** `kp_convert`'s Vorbis needs no tolerance beyond the bytes oggenc writes. Its VP9 reproduces the current transcode exactly, on arm64 and x86-64 alike: the private golden of stage C4 is the current pack's IVF bytes.

Not measured at C0:
- the encoders' generic C build (`generic-enc`: a Linux x86-64 host without NASM) against the SIMD builds' bytes;
- reading the anims with a cold disk cache;
- the glyphs' time on the Fold7;
- reading the language group on the web.

The stages that use them (C3, C5, C7) measure them.

## Consequences

- `third_party/vpx/regenerate.sh` generates six configurations: `generic`, `arm64` and `x86_64`, each also in an `-enc` variant. It writes the headers and `sources.txt`, the list of sources libvpx's own `make libvpx_srcs.txt` builds. `kp_vpx.cmake` reads that list instead of hand-written ones, and builds each x86 intrinsics file for its extension as libvpx's Makefile does (MMX to AVX-512).
- Built and run: macOS arm64 (clang), Linux arm64 (GCC 15.2, the image of `tools/native/linux.Dockerfile`: libvorbis needs `HAVE_ALLOCA_H` there, `kp_vorbis.cmake` checks for it); the Windows build (mingw-w64, NASM) compiles both libraries. `kp_test` itself does not build for Windows (POSIX calls), so `test_encoders` runs on the other two.
- `test_encoders` encodes a VP9 movie and an Ogg Vorbis stream and decodes them back:
  - the frames are 4:4:4 sRGB, 50.4 dB;
  - the samples number exactly the input's, a length that is not a multiple of the feed, 34 dB;
  - the same input gives the same bytes.

  It skips in a build without `KP_ENCODERS`.
- The 1.17 cinematics' transcode takes CPU at conversion time, as it did through FFmpeg (ADR 0015: 138 s on 14 cores). Each movie is encoded on one thread and the movies run in parallel.
- Phones and the web need the repack; a desktop takes either source.

## Stage C4: the cinematics (2026-11-12)

- **Where the movies live.** The repack's two groups, `koprulu-cinematics.mpq` (`sd/smk/`) and `koprulu-cinematics-hd2.mpq` (`hd2/smk/`), apart with `--single` too, by Remastered's names. From Remastered both sets go in as they are (status `remastered`), the base's subtitles in the core; from 1.17 each Smacker movie is transcoded into `sd/smk/<name>.webm` (status `converted`). A bundle carries the sets `KOPRULU_CINEMATICS` picks.
- **The transcode** (`kp_movie.c`, `kp_webm_write.c`): the frames kp_cine's player decodes, VP9 profile 1 4:4:4 sRGB at ADR 0015's settings (constant quality 32, `--cpu-used 2`, a key frame at least every 128 frames, one thread a movie, the movies in parallel), the sound as Vorbis at `-q 3`, muxed by our own writer and read back by nestegg before it is kept. Each WebM is cached by its SMK's SHA-256 and the settings (`KP_MOVIE_SETTINGS`). The 1.17 install's 28: 202.0 MB for 605.4 MB of Smacker, 146 s on 14 cores with an empty cache (FFmpeg's took 138 s).
- **Equal to the Python converter.** Before `cinematics_smk.py` went, its SD pack was recorded (`tests/golden/cinematics/converter.json`, private): per movie the SHA-256 of the VP9 packets and of the frame table, the Vorbis packets' count and bytes, the sound's samples. `test_convert.movies_equal_the_converters` compares a repack's 28 (`KP_TEST_MOVIES`, a step of `tools/ci.sh`): the VP9 packets and frame tables equal, the same Vorbis packets and samples, their bytes 26 apart in 13.3 MB (libvorbisenc's floats against Homebrew's oggenc, C0's 3 bytes in `paryes00.wav`). No PSNR threshold is needed: the pictures are the Python pack's.
- **The web reads a movie by its range.** The movies' groups are stored uncompressed; the core's listing (version 4) places each file in its group (`in` lines). While a group is not open, `kp_res_raw_range` names the file and the range, and the player fetches that alone (`KP_CINE_LOAD_WAIT` while it comes). A computer or a phone opens every group it carries and reads through StormLib.
- **Removed:** the `CINE` container and its reader (`kp_cine_index`, the pack's host), `converter cinematics`, `emit/cinematics.py`, `emit/cinematics_smk.py`, `emit/sound.py`, `formats/ivf.py`, `formats/srt.py`, their tests, `make cinematics` and `CINE=`, `generated/video/`; FFmpeg is left only for the HD portraits (C6) and the test fixtures (C7), oggenc nowhere.

## Stage C5: the HD world (2026-11-12)

- **From an install.** `kp_anim.c` reads the anims' heads and lays out each SD pack's HD pack as the converter did:
  - `SD/images.rel` names the anim of an image without its own;
  - the grouped packer (`kp_pages_pack_groups`) places each image's frames together, the lit images first, on the last four open pages.

  A mission's packs are laid out at its start: on terran02, 155 ms for HD2 and 138 ms for HD. The HD loader's thread builds each page from the anims' sheets through a reader of its own (`kp_res_raw_place`, `kp_res_reader_*`). `kp_hd_scr.c` builds `HDTS` and `HDWA` from the tilesets' and the water's DDS files. Nothing is read from `generated/hd/` for the world.
- **The repack's HD sets.** `kp_convert repack --hd` writes them with the same code:
  - `hd2` and `hd` in BC (the default from a Remastered install);
  - `hd2-astc` and `hd2-etc2` for phones, transcoded by `kp_hd_transcode.c` (`kp_hd_phone`'s code, now `kp_convert`'s) and cached by their BC bytes.

  Each set is a group, `koprulu-<set>.mpq`, read by ranges like the movies'. Each pack's head goes in the core, so the web fetches only pages. Sizes: HD2 648.8 MB, HD 2,449.4 MB, `hd2-etc2` 606.9 MB, `hd2-astc` 545.4 MB.
- **The source a variant draws from** (`kp_world_hd_pick`): the first source whose pages the context takes as they are, else the first it can decode.
  - The repack's sets come first, in BC, ASTC, ETC2 order, then the install's anims.
  - `--graphics auto` and the video options ask the same.
- **Equal to the Python converter.** Before `converter hd` stopped writing the world, its output over the install was recorded in `tests/golden/hd/converter.json` (private): the SHA-256 of each pack's HEAD and FRMS, each page's layers, and each tileset's and the water's sections, HD2 and HD.
  - `test_hd` compares the packs and pages built from the install (`KP_TEST_SCR`): byte for byte.
  - It compares a repack's sets too (`KP_TEST_HD_REPACK`, a step of `tools/ci.sh`): byte for byte.
- **Removed:**
  - `converter/emit/hd.py`'s packs, tilesets and water (it keeps the interface and portraits until C6) and `converter/formats/anim.py`;
  - `kp_hd_phone`'s transcoding modes and `tools/product.sh`'s phone transcoding;
  - the runtime's reads of `generated/hd/<variant>/packs/`, `tilesets/` and `water.kpd`.

## Stage C6: the HD interface, portraits and console (2026-11-12)

- **The placement table.** `kp_convert hd-ui-table` runs the Python converter's heuristics, now only in C (`kp_hd_ui_table.c`):
  - where a frame moved (`place_frame`);
  - which PCXs' HD art is the SD picture drawn again (`same_picture`);
  - where a console's centre piece lies (`place_piece`).

  It writes `defs/scr/hd-ui-bw-1.17.0.1.json`, committed and compiled as `SHUI`. An HD file is named by its CASC content key, not by a SHA-256: the index holds the key, so nothing is read. A file whose key is not the table's stays SD, with a log line that names the command. `tools/ci.sh` makes the table again and compares.
- **From an install.** At start the runtime lays a variant's `ui.kpd` out by the table, from the DDS headers alone, in 70 to 77 ms. It reads and decodes a frame when the canvas first shows it, and builds the consoles' `CONW` in 164 ms. The portraits are Remastered's WebM, found by their own names.
- **The repack.**
  - A group per variant, `koprulu-ui-<variant>.mpq`, holds its `ui.kpd` (HD2's 35.9 MB, HD's 144.0 MB), read by ranges, with its head in the core.
  - `koprulu-portraits.mpq` holds the portraits' WebM under their raw names (72.8 MB).
  - The core holds `koprulu\console.kpd`.

  A phone or the web carries only HD2's group.
- **`HDPV` is gone:** one lookup by name serves both sources.
- **Equal to the Python converter.** Before `converter hd` went, its output was recorded in `tests/golden/hdui/converter.json` (private):
  - per variant, `ui.kpd`'s sections and each frame's size, format, place and image;
  - `CONW`;
  - the 621 portraits' clips.

  `test_hd` compares the build from the install and a repack's groups: byte for byte. `kp_hd_ui_scr.c` and `kp_hd_ui_table.c` build with `-ffp-contract=off`, so they round as Python's doubles did.
- **Removed:**
  - `converter/emit/hd.py`, `hd_ui.py`, `hd_video.py`, `converter hd`, `converter console`;
  - the `HDPV` reader;
  - the runtime's reads of `generated/hd/` and `generated/data/console.kpd`;
  - `tools/web_hd_heads.py` and `kp_load_check`.

  FFmpeg is left only for the test fixtures (C7).

## Stage C7: the Python converter deleted (2026-11-12)

- **`converter/` is gone.** What research reads moved, with its history, to `tools/research/scdata/` (imported as
  `scdata` with `tools/research` on the path): the formats' readers, MPQ and CASC, the container and its `kpd`/`kpm`
  writers, the analyzer, the missions' survey, the packs' membership, the UI tables and their tests. The command line,
  the derived terrain's and the packer's Python twins, `dump` and `fixtures.py` were deleted.
- **The research's commands** are scripts: `tools/research/scr_manifest.py`, `scr_compare.py`, `scr_digests.py`.
  `scr_manifest.py` stays Python (it records the analyzer's survey); `kp_convert manifest` is a `TODO:`.
- **The fixtures are committed** (`tests/fixtures/kpf/`, 2.2 MB, synthetic), with the expectations the Python twins
  computed; the 8 MiB container is built by `test_data.c`.
- **The derived terrain** (walk, regions, contours) of the 68 campaign CHKs is compared with the native evidence by
  `test_chk` in C, as the Python twins' tests did.
- **The Python tests** (`scdata`'s and `tools/tests/`) run by `tools/python_tests.py`, the renamed runner.
- **FFmpeg is research only** (the 1.17 movies' decode golden, the research readers' Remastered movies). The game and
  `kp_convert` need neither Python nor FFmpeg.
- **The Fold7** over a phone's repack of Remastered (HD2 ASTC, ruRU, the HD2 movies): `hd_game.kps` on terran02 and on
  terran01 in ruRU with its speech, `hd_glue.kps` and the 28 movies' check, all with 0 failures.
