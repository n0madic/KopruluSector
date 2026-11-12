# Everything from an install or from `koprulu*.mpq`, the converter in C

Design: [ADR 0019](adr/0019-install-or-repack-converter-in-c.md). The user approved the plan on 2026-11-11.

The folder `--data` names is the only source:
- **an install:** every asset in its original format, from MPQ or CASC;
- **a folder of `koprulu*.mpq`:** `kp_convert`'s compact forms.

The converter becomes `kp_convert`, in C, sharing its builders with the runtime. The Python `converter/` is deleted; Python stays for research only.

| Stage | What | State |
|---|---|---|
| C0 | ADR 0019; libogg, libvorbis with vorbisenc and libvpx's VP9 encoder built for the host's tools (`KP_ENCODERS`); the spikes | done 2026-11-11 |
| C1 | `kp_convert`: `repack`, `verify`, `digests`, `extract` (`kp_data_probe` folded in), `icon`, `scenarios`, `terrain`, on macOS, Linux and Windows; the Makefile, README, `tools/product.sh` and `tools/ci.sh` on `kp_convert`; `manifest`, the listing v3 and `kp_res_raw_stream_open` moved to the stages that need them | done 2026-11-11 |
| C2 | Speech: the lines and voices of `locales/<loc>/Assets/` played as they are, TIME merged from the language's CHK when a mission loads; the repack's language group recompressed; the speech overlays go | done 2026-11-12 |
| C3 | Language texts: `kp_locale.c` (the six tables, dialogs by `defs/l10n`, `rez\*.txt`, the maps' STR, subtitles, the glyphs at start, VFNT from TTF); `kp_convert locale-check`; the text overlays and `glyphs.kpd` go | done 2026-11-12 |
| C4 | Cinematics: the 1.17 SMK transcoded to VP9 and Vorbis in our own WebM by `kp_convert repack`; Remastered's WebM as they are; `CINE` and FFmpeg go | |
| C5 | The HD world: `kp_anim.c`, `images.rel`, the grouped packer, the HD page thread over anim; `HDTS`/`HDWA` from the originals; `HDPK`/`HDTS`/`HDWA` in the `hd2`/`hd` groups | |
| C6 | The HD interface, portraits, console: `kp_convert hd-ui-table` writes `defs/scr/hd-ui-bw-1.17.0.1.json`; `HDUI`/`CONW` built from the originals; `HDUI`/`HDPV`/`CONW` in the `ui` group | |
| C7 | `converter/` deleted; the readers for research moved to `tools/research/`; the bundles carry `koprulu*.mpq` only; the Fold7 run | |

## C0 as built (2026-11-11)

**The libraries.**

- **`tools/toolchain/fetch.sh vorbis`** (also part of the default fetch) fetches libogg 1.3.6 and libvorbis 1.3.7, pinned by SHA-256. It uses GitHub's release assets, because `downloads.xiph.org`'s TLS failed.
- **`third_party/vorbis/kp_vorbis.cmake`** builds them as one library, `kp_vorbis`:
  - the codec and vorbisenc, without vorbisfile or the tools;
  - libogg's `config_types.h` committed under `generated/ogg/`;
  - `HAVE_ALLOCA_H` where `alloca.h` is found (glibc);
  - under `KP_SANITIZE`, without UBSan's `shift-base` check: libvorbis shifts negative values left (`psy.c`'s bark table, `(lo - 1) << 16` with `lo` 0), and the full CI's LLVM UBSan stopped there. ASan and the rest of UBSan still apply, as for CascLib, which runs under ASan only.
- **`third_party/vpx/regenerate.sh`** generates the six configurations: `generic`, `arm64` and `x86_64`, each also as `-enc` with `--enable-vp9-encoder`. Each holds:
  - the headers configure writes;
  - `sources.txt`, from libvpx's own `make libvpx_srcs.txt`, without the `.asm` files that are only included.
- **`kp_vpx.cmake`** reads `sources.txt` instead of the hand-written lists. On x86-64 it builds each intrinsics file for its suffix's extension, as libvpx's `build/make/Makefile` does: MMX, SSE2 to SSE4.1, AVX, AVX2, AVX-512.
  - This replaces the hand-written per-extension lists (MMX, SSE2, SSSE3, AVX2), because the encoder adds SSE4.1, AVX and AVX-512 files.
  - The extension is taken from the match before it is tested: a condition that names `CMAKE_MATCH_1` in the same `if` as its `MATCHES` expands it before the match runs, and then no file gets its flags.
- **The `KP_ENCODERS` option** (CMakeLists.txt) defaults on where the host's tools are built, and off for the product's page, Android and iOS. It picks the `-enc` configuration and builds `kp_vorbis`.

**`test_encoders`** (`tests/c/test_encoders.c`):
- `vp9_round_trip`: ten 64 × 48 frames at the cinematics' settings decode back as 4:4:4 sRGB at 50.4 dB (floor 45), and a second encoding gives the same bytes.
- `vorbis_round_trip`: 22,087 samples at 22,050 Hz mono and `-q 3`. The Ogg pages carry the serial; the last page ends the stream with a granule equal to the sample count; exactly that many samples decode back, at 34.0 dB (floor 30); the bytes are the same again.

  Its length is deliberately not a multiple of the 1,024 samples fed at a time. The first spike lost its tail there: without `vorbis_analysis_wrote(0)` the encoder never flushes the last block.
- The suite skips without `KP_ENCODERS`.
- It ran on macOS arm64 (clang) and Linux arm64 (GCC 15.2). The Windows build compiles the libraries but not `kp_test`.

**The spikes** (in the scratchpad, not committed; the results are in [ADR 0019](adr/0019-install-or-repack-converter-in-c.md#measurements-stage-c0)):
- **HD pages from anim:** the 868 anims' headers in 0.14 s; their files in 1.13 s for HD2 and 3.68 s for HD ×4.
- **Glyphs:** 5 fonts × 271 codes rasterized in 33 ms.
- **ruRU from CASC:** 361.5 MB, against the converter's 69 MB of speech.
- **libvorbisenc against oggenc:** `buzz.wav` byte for byte; `paryes00.wav` the same pages, granules and packet counts, 3 bytes apart.
- **libvpx's VP9 encoder against FFmpeg's `libvpx-vp9`:** `blizzard` and `Terran2` the same IVF bytes, at 43.25 and 41.41 dB. The x86-64 build under Wine gives the same bytes as arm64.

**Decisions taken from them:**
- The HD pages are built from anim at a mission's start, with no layout table in `defs/`.
- The glyphs are rasterized at start.
- The speech is recompressed in the repack.
- C4's golden is the current pack's IVF bytes.

**Open after C0:**
- The encoders' generic C build (`generic-enc`) is not compared with the SIMD builds' bytes.
- Not measured yet: the anims' read with a cold cache (C5), the glyphs on the Fold7 (C3), the language group on the web (C7).
- On Linux with GCC 15.2, `tests/c/test_res.c`'s `dir_path` tripped `-Werror=restrict` at `-O3`: a false overlap between two members of one structure, older than phase C. Resolved in C1: the folder is the test's own string (`kp_t_temp_dir`).
- `kp_test` did not build for Windows (`mkdir` with a mode, among others). Resolved in C1: it builds with mingw-w64 and runs under Wine.

## C1 as built (2026-11-11)

**`kp_convert`** (`native/kp_convert/`, the `kp_convert` target; `kp_convert_lib` holds all but `main` for `test_convert`). One C tool on `kp_app_core`, built where `KP_ENCODERS` is on: by default with the host's tools, and alone with `-DKP_TOOLS=OFF -DKP_ENCODERS=ON` (macOS 4.7 MB, linking only the system's C and C++ libraries).

| Command | What | Was |
|---|---|---|
| `verify DIR...` | every manifest file read through `kp_res`, the statuses and time by kind; exact and derived files checked to the byte; a 1.17 install's `StarCraft.exe` by its SHA-256 (`executable_sha256`, compiled in by `tools/defs_c.py`) | `kp_data_probe`, `converter verify` |
| `digests DIR --out PATH` | each manifest file's name, status and SHA-256 | `kp_data_probe --digests` |
| `extract DIR NAME OUT` | a file as `kp_res` reads it, or a Remastered file by its CASC name (`kp_res_raw_read`) | `converter extract`, `scr-extract` |
| `repack DIR [--single] [--wav] [--res-log] [--cache] [--threads]` | `kp_repack.c`: the groups or one `koprulu.mpq`, StormLib's writer; a WAV as Ogg Vorbis (`kp_ogg.c`, libvorbisenc at `-q 3`, the CRC-32 of the name as the serial) on every core (`kp_work.c`), each kept in `generated/cache/repack-vorbis/<sha>-q3-s<serial>.ogg` | `converter repack` (oggenc) |
| `scenarios DIR` | `kp_scenario.c`: each spec of `tests/scenarios/` rewrites its base CHK (OWNR, UNIT, THG2, MBRF, TRIG, STR, MRGN, PTEC); `index.txt` with the packs of the profile's compiled `SPAK` | `converter scenarios` |
| `terrain DIR` | the eight tilesets' megatile CRCs | `converter terrain` |
| `icon DIR` | `kp_icon.c`: the executable's icon group 102 (a bounds-checked PE32 reader), the sizes it lacks resampled on premultiplied colour, PNG, ICO and ICNS, the iOS, web and Android files | `converter icon` |

A command writes a file only when its bytes change, and says how many it wrote.

**Equal to the Python converter**, from the 1.17 install and from Remastered's, on macOS arm64, Linux arm64 (Docker, GCC 15.2) and Windows x86-64 (mingw-w64, under Wine):
- the test scenarios: 159 CHKs and `index.txt`, byte for byte;
- the terrain checks' CRCs, byte for byte;
- the icon's 27 files, byte for byte. `kp_icon.c` is built with `-ffp-contract=off`: otherwise clang on arm64 and GCC fuse the resampling's multiply-adds and a few pixels differ by one;
- the repack's listings: every file's digest, size and status, each Ogg's WAV fields. The Ogg files themselves are within 195 bytes of oggenc's over 101 MB.

**The Ogg bytes depend on the processor.** libvorbis computes in floating point: the Windows x86-64 build's Oggs differ from macOS arm64's in 1,678 of 2,244 files, though they decode to the same sound within the encoder's rounding. The listings are equal. `test_convert` checks what the Ogg decodes to, not its bytes.

**Measured** (the M-series Mac, the cache empty): the 1.17 install's repack takes 20.8 s with 2,244 WAVs encoded (the Python converter took 44.7 s); Remastered's `--single` takes 10.3 s. The Windows build under Wine takes 29 s for the 1.17 repack.

**`kp_test` on every desktop.** The suites build with mingw-w64 and run under Wine; a test's folders come from `kp_t_temp_dir` (removed when the test ends) and `kp_t_mkdir`, not `mkdtemp` and `mkdir` with a mode. `kp_fs_list` skips folders on POSIX as it does on Windows. The two tests of a repack read from slices of one file skip on Windows: StormLib's `fd:` streams are POSIX's (Android's APK). On Linux the build needs no `-Wno-error=restrict` any more.

**`test_convert`** (13 tests; they skip without `KP_ENCODERS`): the Ogg of a 16-bit mono and an 8-bit stereo WAV decodes back above its SNR floor, the same bytes twice, a float WAV refused; the serial is the name's CRC; a stand-in profile's repack reads back through `kp_res` alone (the Ogg's serial, the WAV's header), then `--single` from the cache with nothing encoded; a repack of a repack refused; the groups by name; a scenario's sections rewritten, a bad spec refused; the icon group of a synthetic PE32, the resampling, the PNG rows, the 24 files of one image and nothing written the second time; `kp_fs`, `kp_work`.

**The tools moved:**
- `Makefile`: `verify`, `repack`, `scenarios`, `terrain` and `icon` build `kp_convert` (`convert-tool`) and run it on `SC`, else `SCR`;
- `tools/ci.sh`: `kp_convert verify` over the 1.17 install, `digests` over Remastered's (compared with `converter scr-digests`), `scenarios` and `terrain` from the install before the app's checks, the repacks by `kp_convert`;
- `tools/product.sh`: the repacks' hints name `kp_convert repack`;
- README.

**Removed:** `converter verify`, `extract`, `scr-extract`, `scenarios`, `terrain`, `icon` and `repack`; `converter/repack.py`, `converter/icon.py`, their tests, StormLib's writer in `converter/mpq/stormlib.py` and `reports/scr-import.json`; `native/kp_core/tools/kp_data_probe.c`.

**Decisions taken in C1:**
- **`manifest` stays `converter scr-manifest` until C7.** It records what the Python conversion reads, so it goes with that conversion. It now also records the test scenarios' packs (`defs/campaign/packs-bw-1.17.0.1.json`, `scenarios`), compiled as `SPAK`. Its run dropped `rez\video.bin` from the input manifest: nothing has read it since our own Video Options dialog replaced it (2026-10-27).
- **`scr-compare`, `scr-digests` and `dump` stay in Python until C7**: they check the Python readers, which go then.
- **The listing stays `koprulu repack 2` until C2**: v3's raw names come with the language group, the first files that need them. Done in C2.
- **The bundles carry no `kp_convert`.** A bundle is the converter's output, its repack; a player with an install points `--data` at the install, which needs no conversion. `kp_convert` builds and runs on each desktop on its own (README: mingw-w64 for Windows).
- **`kp_res_raw_stream_open` waits for its consumer**, the HD page thread of C5. `extract` reads a raw CASC name through `kp_res_raw_read`.

**Open after C1:**
- The old `generated/cache/repack-ogg/` is not read any more; it can be deleted by hand.
- For C3: the languages' second escaping (a quote `\"`, a backslash `\\\\`; `rebuild.LANGUAGE_ESCAPES`, undone since 2026-11-12) goes into `kp_locale.c` with the rest of the XML values' rules.
- macOS's linker warns of `libkp_app_core.a` given twice when it links `kp_test`; harmless.

## C2 as built (2026-11-12)

**Remastered's own files by their CASC names** (`kp_res`). The listing is `koprulu repack 3`: a file of Remastered's own, which no manifest lists, is listed with the status `remastered` under its CASC name, lower case with `/`. `kp_res_raw_read`, `kp_res_raw_head`, `kp_res_raw_held` and `kp_res_raw_files` (a `KpFiles` over them) read such a name from the repack first (its size checked, a CHK's SHA-256 too; a copy that differs is logged and the install's read), else from the CASC install, case-insensitively and with either separator. The reads are recorded in `--res-log` as `remastered`, with the repack's file or `CASC` as the source. They replace `kp_res_casc_file` and `kp_res_casc_held` (the portraits, the cinematics, `kp_convert extract`). `KpData.raw` holds them for the app and the oracle.

**The mission's TIME** (`kp_host_game_speech`). When a campaign mission loads, the language's map `locales/<loc>/Assets/campaign/<c>/<m>/staredit/scenario.chk` is read, checked as a CHK and its `TRIG` and `MBRF` merged into a copy of the mission's (`time_merged`, the Python converter's rule):
- an action alike but for its TIME (+0x0C) takes the language's;
- an action that differs in more keeps the mission's whole;
- a record that differs in its conditions, flags or actions is counted, and the first named.

The profile's `SPEX` (`tools/defs_c.py`, from `defs/l10n/locales.json`'s `speech`) names the missions where that is Remastered's own fix (`expterran/terran03` in each language); elsewhere the app logs the difference and plays the merged records, the oracle refuses them. `LANG` (the languages, the base's first) is compiled from the same file. A map of another size, or a damaged one, leaves the mission's own triggers with a log line. A save keeps the merged copies, as before (`a_save_keeps_its_speech`).

**Lines and voices played as they are.** A mission's spoken line is the language's `.ogg` of the WAV string's path (`kp_ui_data_speech_path`), streamed by SDL3_mixer; a unit voice is the language's `.wav` of sfxdata's path, decoded as the base's are. A name the language lacks, or holds empty (Remastered's silent stand-ins), plays the base's. Changing the speech drops the decoded voices. The interface's talking and response durations come from each voice's WAV header (`kp_ui_data_speech_sounds`: the base's SFX records with the language's where it has the file). The speech's packs are gone from the app: `kp_pack_sound`, `kp_mission_sounds_load`, the world's mission sounds. A language has speech when the data holds its marker voice, `locales/<loc>/Assets/sound/Terran/advisor/TAdUpd00.wav` (`KP_DATA_SPEECH_MARKER`). The flow logs `speech of <loc>: <mission>: N actions' TIME` and `speech of <loc>: N voices`.

**The oracle** takes the speech's locale (`KP_SPEECH`) and its source (`KP_SPEECH_DATA`: an install or a repack with the language's group; `tools/state_check.py --speech LOC --speech-data DIR`, default `KOPRULU_SCR`). It refuses a mission without the map, or one that differs in more than TIME without an exception. `tools/research/sc_emulator/speech.py` merges the same records for the original's game, from the 1.17 map and the language's map in the install (`play ... --speech LOC:CAMPAIGN/MISSION --scr DIR`). Its records equal the Python overlays' `TRIG` for all 64 missions of the four languages.

**The repack's language groups.** `kp_convert repack` from Remastered writes `koprulu-locale-<loc>.mpq` for each language the install holds (the `LANG` entries after the base whose marker voice it holds), or those `--locales` names (`none`: none). The groups stay apart even with `--single`, so a bundle carries only its languages. A group holds each catalog mission's map and lines and sfxdata's voices. `kp_ogg_speech` re-encodes a sound only when the result is smaller:
- mono (the channels averaged);
- 44,100 Hz halved to 22,050 Hz through a 65-tap Blackman-windowed sinc;
- `-q 3`, the CRC-32 of the name as the serial;
- a line (Ogg Vorbis, decoded by libvorbis) replaced by its Ogg;
- a voice (WAV, 8, 16 or 24 bits: deDE keeps five at 24) stored as `ogg` with its WAV's header, for the durations.

Each is cached as `<sha256>-q3-s<serial>-speech.ogg`. From `/Applications/StarCraft`: 6,782 lines and voices encoded, 278.5 MB stored for 1,390.4 MB read, in 37 s with the cache empty; 2,118 names the install lacks or holds empty are left out. The groups: deDE 78.4 MB, frFR 71.4 MB, plPL 73.6 MB, ruRU 70.6 MB (the Python speech: 76, 69, 71 and 69 MB at oggenc's 22,050 Hz); `koprulu.mpq` 131.1 MB.

**Equal to the Python converter.** Before `converter speech` went, its output was recorded in `tests/golden/speech/converter.json`: the SHA-256 of each mission's merged `TRIG` and `MBRF` and of each language's SFX table. The C equals it for the four languages' 64 missions and SFX, read from the install and from the repack (`test_speech.the_speech_equals_the_converters`, `the_speech_sound_records_replace_the_bases`). `campaign_plays_with_the_speech_equal_the_original` replays the 71 plays of `tests/golden/oracle/plays-ruRU` with the speech read from the install, its goldens unchanged.

**The tools.**
- `tools/ci.sh` no longer converts the speech. Each language's product run reads it from the install (`--data . --data "$KOPRULU_SCR" --speech LOC`) and must log terran01's TIME. The C suites read it from `KP_TEST_SCR`, and two of `test_speech`'s tests run again over Remastered's repack.
- `tools/product.sh`: `KOPRULU_SPEECH` is gone. A staged language (`KOPRULU_LOCALES`) brings its repack group on the computers and phones; the web carries none.
- `Makefile`: the `speech` target and `SPEECH` are gone.
- `test_flow.the_speech_is_chosen_apart_from_the_text` reads ruRU's files from `KP_TEST_SCR`.

**Removed:** `converter speech`, `converter/emit/speech.py`, `converter/emit/packs.py`, `MSND` (`resources.build_mission_sounds`), the base's sounds in `converter/base.py`, their tests; the speech overlays (`generated/speech/`, `generated/sounds/speech/`, deleted locally).

**Decisions taken in C2:**
- **The voices keep their WAV header in the listing**, as the base's sounds do, so the durations need no decode. The lines need none: the game times them by the triggers' TIME.
- **The web carries no language group yet.** It would fetch the group when the language is chosen; that comes with C3, whose texts go into the same group (`TODO` in `tools/product.sh`). Until C2 `KOPRULU_SPEECH=ruRU` could preload ruRU's speech on the web (220 to 292 MB, never by default); until C3 the web has no speech.
- **A difference without an exception is logged in the app, refused in the oracle**: a player keeps the language's TIME, the goldens keep their strictness.

**Open after C2:**
- The Ogg bytes still differ by processor (C1); the listings are equal.
- Remastered's repack logs six `cannot size locales/enUS/...: CascLib error 1007` lines (enUS lines of `expterran/terran02` and `terran05b` offline in this install), not looked into.
- The full `kp_test` under Wine stopped with a stack overflow (`___chkstk_ms`). The cause is `test_session`'s tests: each puts two `KpProfile`s (1.26 MB each) on the main thread's stack, and mingw gives that thread 2 MB, where macOS and Linux give 8 MB. The app keeps its profile inside `KpApp`, not on the stack. With the executable's stack reserve raised to 8 MB, all 25 tests pass. The link flag comes with C3. `test_save` stopped there too, with no output; under 8 MB it runs on.

## C3 as built (2026-11-12)

**The language built from its own files** (`native/kp_app/kp_locale.c`). `kp_locale_build` makes a language's parts over the interface's base from `locales/<loc>/Assets/` in the install or the repack's language group (`KpData.raw`), by the rules `converter/emit/locale.py` had:
- the six tables in UTF-8 with u32 offsets, from the language's XML (`kp_scr_xml_strings`): `stat_txt` by position over Remastered's own TBL's rows (`base_rows`, now in `defs/scr/texts-bw-1.17.0.1.json`), the others by the manifest's ids (the profile's `STBL`); a span's NUL mended by the base's row (`with_nuls`), the second escaping of the languages' XML undone;
- the dialogs: each original dialog rebuilt from the profile's `SBIN` with the language's strings (`kp_res_scr_dialog`), its records the base's but for their texts and `layout`'s rectangles; our own dialogs' texts from `defs/l10n/koprulu-<loc>.json`;
- the credits' and epilogues' `rez\*.txt`; the subtitles (`.srt` to `SUBS`, `kp_cine_subs_srt`).

`defs/l10n/locales.json`'s rules are compiled into the profile (`tools/defs_c.py`: `LROW`, `LEXC`, `LTPL`, `LKEP`, `LTXT`, `LLAY`, `LSTR`, `LOWN`). Every text is checked as the converter checked it: UTF-8, the base's printf conversions and NULs, no control byte the base does not use, no Remastered substitution left, a button's form, an ASCII hotkey. A text that fails keeps the base's and is named; a part that does not fit (another row count, another dialog) stays the base's whole. A language is held when the data has `locales/<loc>/Assets/rez/RC3.xml` (`kp_locale_held`); the main menu lists the profile's `LANG` entries the data holds, and with none but the base the glue chooses enUS.

**A mission's strings** (`kp_locale_mission`). When a campaign mission loads, the language's map is read and its STR laid over the mission's: each row the language's but those the game looks up by their text (the sounds' paths, Set Next Scenario's map name: `kp_mission_string_looked_up`) and `strings`' own texts. `kp_host_game_text` takes the result (the same count, at least 2 bytes, or refused with a log line); a map the data lacks leaves the mission's own text.

**The glyphs at start** (`kp_locale_glyphs`). The FNT fonts' supplements are rasterized from Remastered's TrueType pair (`font/EUROSTILE-REG.TTF`, `font/EUROSTILEEXT-REG.TTF`) through `kp_fnt_supplement`: the code points of the languages' code pages, of the languages' names and of the chosen language's text, but those with a byte slot of their own. The same pair is `VFNT`, the vector fonts. A data without the pair (the 1.17 install) keeps the FNT fonts' own glyphs. `kp_locale_glyphs_cover` names a code point a text uses that neither a slot nor the supplement draws; `locale-check` applies it to the missions' strings too.

**The interface takes structures.** `KpUiLocale` holds the parts as pointers (`tbl`, `dlgs`, `pool`, `files`, `supplements`, `vector_fonts`, `speech`), not containers; `kp_ui_set_locale` checks each against the base as before. The cinematics keep the pack's own index (`KpAppCine.pack`) and lay the language's `SUBS` over a copy before each run and check (`kp_cine_subs_overlay` over the bytes). The overlay readers are gone: `KP_FLOW_LOCALE_FILES`, `overlay_parse`, `LOCS`, the kinds' and schemas' defines.

**`kp_convert locale-check DIR [--locale L|all] [--out DIR]`** builds every part of each language a source holds, the 64 missions' strings and the glyphs, and writes `<out>/reports/locale-<loc>.json` in the converter's form (tables, dialogs, scripts, missions, subtitles, glyphs, exceptions, mended, problems); any problem exits 1. From `/Applications/StarCraft`: deDE, frFR, plPL, ruRU, 0 problems, the reports equal the Python converter's but `scripts.base`, a count where it was a list. One language takes 1.3 s from the install, 0.7 s from the repack.

**The repack's language groups hold the texts.** `kp_convert repack` from Remastered adds to each `koprulu-locale-<loc>.mpq` exactly the files the runtime reads for the language: `kp_locale_names` runs `kp_locale_held`, `kp_locale_build` and `kp_locale_mission` for each catalog mission over a `KpFiles` that records each name, so no list is kept apart. The core gets the TrueType pair (the base's vector fonts need it too). The groups grow by about 0.15 MB each (deDE 78.6 MB, frFR 71.5 MB, plPL 73.8 MB, ruRU 70.8 MB), `koprulu.mpq` by 1 MB (132.2 MB). A group written at C2 holds no texts: the language's text stays the base's.

**Equal to the Python converter.** Before `converter locale` went, its output was recorded in `tests/golden/locale/converter.json`: the SHA-256 of each language's six tables, `DLGS`, `POOL`, 64 scripts, `SUBS` (20 movies), each of the 64 missions' `STRS`, the translated rows' counts (6,937; plPL 3,663) and the glyphs (175 codes, the five supplements, `VFNT`). `test_locale.the_languages_equal_the_converters` builds all four languages from the install and from its repack and equals it byte for byte. The base matters for the rows a language keeps: the golden is equal over Remastered's own base (`kp_data_build` over the install), whose TIPS row 80 is empty where 1.17's reads "Tell everyone you know to buy Starcraft.".

**The tools.**
- `tools/ci.sh`: `kp_convert locale-check` over `KOPRULU_SCR` replaces `converter locale`, and its list drives the product's language runs: each language's menu from the install and from Remastered's repack, terran01 with its speech (`--data . --data "$KOPRULU_SCR" --locale LOC`). `test_locale.the_languages_equal_the_converters` runs again over the repack, beside `test_speech`'s two.
- `tools/product.sh`: `KOPRULU_LOCALES` picks the repack's language groups (`auto`: each it has); `generated/locale/` and `glyphs.kpd` are no longer staged; the web carries none (`TODO`).
- `Makefile`: `locale-check` replaces `locale` and `fnt-tool`.

**Windows' stack.** Every executable on Windows links an 8 MB main-thread stack (`KP_STACK_BYTES` in `CMakeLists.txt`, macOS's and Linux's; mingw's default is 2 MB), the open item of C2: under Wine `test_session`, `test_save`, `test_locale`, `test_cine`, `test_flow` and `test_convert` pass, 141 tests.

**Removed:** `converter locale`, `converter/emit/locale.py`, `converter/base.py`, `converter/tests/test_locale.py`, the `kp_fnt_make` tool (`kp_fnt_supplement` stays, the runtime's), the dump's decoders of the overlays' sections.

**Decisions taken in C3:**
- **The glue chooses enUS** when the data holds no other language (before: none chosen, `""`), so the choice is always a language of the list.
- **The glyphs are built for the base's text too**, from the install's TrueType pair: the vector fonts are Remastered's own, as `glyphs.kpd` gave them.
- **`kp_locale_names` records the reads** rather than listing names, so the repack holds what the runtime reads and nothing else.

**Open after C3:**
- `generated/locale/` from the Python converter stays on disk where it was written; nothing reads it (remove it by hand).
- The glyphs' time on the Fold7 is not measured (no device attached); C7's device run covers the language with its speech.
- The web carries no language group yet (C7).
