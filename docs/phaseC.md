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
| C4 | Cinematics: the 1.17 SMK transcoded to VP9 and Vorbis in our own WebM by `kp_convert repack`; Remastered's WebM as they are; `CINE` and FFmpeg go | done 2026-11-12 |
| C5 | The HD world: `kp_anim.c`, `images.rel`, the grouped packer, the HD page thread over anim; `HDTS`/`HDWA` from the originals; `HDPK`/`HDTS`/`HDWA` in the `hd2`/`hd` groups, the phones' sets transcoded by `kp_convert` | done 2026-11-12 |
| C6 | The HD interface, portraits, console: `kp_convert hd-ui-table` writes `defs/scr/hd-ui-bw-1.17.0.1.json`; `HDUI`/`CONW` built from the originals; `HDUI` in a group per variant, the portraits' WebM in their own group, `CONW` in the core (`HDPV` gone) | done 2026-11-12 |
| C7 | `converter/` deleted; the readers for research moved to `tools/research/scdata/`; the fixtures committed; the derived terrain's evidence checked in C; the bundles carry `koprulu*.mpq` only; the Fold7 run | done 2026-11-12 |

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
- Not measured yet: the anims' read with a cold cache (C5; still open after it), the glyphs on the Fold7 (C3), the language group on the web (C7). Measured after C7 ([below](#after-c7-the-open-items-2026-11-12)).
- On Linux with GCC 15.2, `tests/c/test_res.c`'s `dir_path` tripped `-Werror=restrict` at `-O3`: a false overlap between two members of one structure, older than phase C. Resolved in C1: the folder is the test's own string (`kp_t_temp_dir`).
- `kp_test` did not build for Windows (`mkdir` with a mode, among others). Resolved in C1: it builds with mingw-w64 and runs under Wine.

## C1 as built (2026-11-11)

**`kp_convert`** (`native/kp_convert/`, the `kp_convert` target; `kp_convert_lib` holds all but `main` for `test_convert`). One C tool on `kp_app_core`, built where `KP_ENCODERS` is on: by default with the host's tools, and alone with `-DKP_TOOLS=OFF -DKP_ENCODERS=ON` (macOS 4.7 MB, linking only the system's C and C++ libraries).

| Command | What | Was |
|---|---|---|
| `verify DIR...` | every manifest file read through `kp_res`, the statuses and time by kind; exact and derived files checked to the byte; a 1.17 install's `StarCraft.exe` by its SHA-256 (`executable_sha256`, compiled in by `tools/defs_c.py`) | `kp_data_probe`, `converter verify` |
| `digests DIR --out PATH` | each manifest file's name, status and SHA-256 | `kp_data_probe --digests` |
| `extract DIR NAME OUT` | a file as `kp_res` reads it, or a Remastered file by its CASC name (`kp_res_raw_read`) | `converter extract`, `scr-extract` |
| `repack DIR [--single] [--wav] [--res-log] [--cache] [--threads]` | `kp_repack.c`: the groups or one `koprulu.mpq`, StormLib's writer; a WAV as Ogg Vorbis (`kp_ogg.c`, libvorbisenc at `-q 3`, the CRC-32 of the name as the serial) on every core (`kp_work.c`), each kept in `generated/cache/repack-vorbis/<sha>-q3-s<serial>.ogg` (since C4 `generated/cache/repack/`, beside the movies' WebM) | `converter repack` (oggenc) |
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
- The old `generated/cache/repack-ogg/` is not read any more; it can be deleted by hand. Deleted after C7.
- For C3: the languages' second escaping (a quote `\"`, a backslash `\\\\`; `rebuild.LANGUAGE_ESCAPES`, undone since 2026-11-12) goes into `kp_locale.c` with the rest of the XML values' rules.
- macOS's linker warns of `libkp_app_core.a` given twice when it links `kp_test`; harmless. Resolved after C7: `kp_test` links it once, through the oracle's library.

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
- The Ogg bytes still differ by processor (C1); the listings are equal. Explained after C7: by build (the compiler
  and its math library), not by processor.
- Remastered's repack logs six `cannot size locales/enUS/...: CascLib error 1007` lines (enUS lines of `expterran/terran02` and `terran05b` offline in this install), not looked into. Resolved after C7: `ERROR_FILE_OFFLINE`, the install's enUS speech not downloaded; said once, in words.
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

**`kp_convert locale-check DIR [--locale L|all] [--out DIR]`** builds every part of each language a source holds, the 64 missions' strings and the glyphs, and writes `<out>/reports/locale-<loc>.json` in the converter's form (tables, dialogs, scripts, missions, subtitles, glyphs, exceptions, mended, problems); any problem exits 1. From `/Applications/StarCraft`: deDE, frFR, plPL, ruRU, 0 problems, the reports equal the Python converter's but `scripts.base`, a count where it was a list (a list again after C7). One language takes 1.3 s from the install, 0.7 s from the repack.

**The repack's language groups hold the texts.** `kp_convert repack` from Remastered adds to each `koprulu-locale-<loc>.mpq` exactly the files the runtime reads for the language: `kp_locale_names` runs `kp_locale_held`, `kp_locale_build` and `kp_locale_mission` for each catalog mission over a `KpFiles` that records each name, so no list is kept apart. The core gets the TrueType pair (the base's vector fonts need it too). The groups grow by about 0.15 MB each (deDE 78.6 MB, frFR 71.5 MB, plPL 73.8 MB, ruRU 70.8 MB), `koprulu.mpq` by 1 MB (132.2 MB). A group written at C2 holds no texts: the language's text stays the base's.

**Equal to the Python converter.** Before `converter locale` went, its output was recorded in `tests/golden/locale/converter.json`: the SHA-256 of each language's six tables, `DLGS`, `POOL`, 64 scripts, `SUBS` (20 movies), each of the 64 missions' `STRS`, the translated rows' counts (6,937; plPL 3,663) and the glyphs (175 codes, the five supplements, `VFNT`). `test_locale.the_languages_equal_the_converters` builds all four languages from the install and from its repack and equals it byte for byte. The base matters for the rows a language keeps: the golden is equal over Remastered's own base (`kp_data_build` over the install), whose TIPS row 80 is empty where 1.17's reads "Tell everyone you know to buy Starcraft.".

**The tools.**
- `tools/ci.sh`: `kp_convert locale-check` over `KOPRULU_SCR` replaces `converter locale`, and its list drives the product's language runs: each language's menu from the install and from Remastered's repack, terran01 with its speech (`--data . --data "$KOPRULU_SCR" --locale LOC`). `test_locale.the_languages_equal_the_converters` runs again over the repack, beside `test_speech`'s two.
- `tools/product.sh`: `KOPRULU_LOCALES` picks the repack's language groups (`auto`: each it has); `generated/locale/` and `glyphs.kpd` are no longer staged; the web carries none (`TODO`; since the after-C7 work it does).
- `Makefile`: `locale-check` replaces `locale` and `fnt-tool`.

**Windows' stack.** Every executable on Windows links an 8 MB main-thread stack (`KP_STACK_BYTES` in `CMakeLists.txt`, macOS's and Linux's; mingw's default is 2 MB), the open item of C2: under Wine `test_session`, `test_save`, `test_locale`, `test_cine`, `test_flow` and `test_convert` pass, 141 tests.

**Removed:** `converter locale`, `converter/emit/locale.py`, `converter/base.py`, `converter/tests/test_locale.py`, the `kp_fnt_make` tool (`kp_fnt_supplement` stays, the runtime's), the dump's decoders of the overlays' sections.

**Decisions taken in C3:**
- **The glue chooses enUS** when the data holds no other language (before: none chosen, `""`), so the choice is always a language of the list.
- **The glyphs are built for the base's text too**, from the install's TrueType pair: the vector fonts are Remastered's own, as `glyphs.kpd` gave them.
- **`kp_locale_names` records the reads** rather than listing names, so the repack holds what the runtime reads and nothing else.

**Open after C3:**
- `generated/locale/` from the Python converter stays on disk where it was written; nothing reads it (remove it by hand). Deleted after C7.
- The glyphs' time on the Fold7 is not measured (no device attached); C7's device run covers the language with its speech. Measured after C7: 46 ms (`tools/native/android_bench.sh --glyphs ruRU`).
- The web carries no language group yet (C7). Resolved after C7: fetched whole when wanted.

## C4 as built (2026-11-12)

**The movies in the repack** (`native/kp_convert/kp_repack.c`). Two groups by Remastered's names, apart with `--single` too: `koprulu-cinematics.mpq` (`sd/smk/<name>.webm`) and `koprulu-cinematics-hd2.mpq` (`hd2/smk/`). From Remastered both sets go in as they are, and the base's subtitles the install holds go in the core (`locales/enus/assets/smk/<name>.srt`; a language's are in its group since C3). From 1.17 each of the catalog's 28 Smacker movies, checked against `CINM`'s size and SHA-256, is transcoded into the SD set's name with the status `converted`. `--wav` keeps the sounds but still transcodes the movies: their names are the WebM's. The groups are stored uncompressed, and the core's listing (version 4) places each of their files: `in <group> <offset> <sha256> <size> <status> <name>` (`kp_mpq_stored_at`), so a group need not be open to know where a movie lies. [The format](formats/repack.md).

**The transcode** (`kp_movie.c`, `kp_webm_write.c`). `kp_movie_smk_webm` opens the SMK with kp_cine's own player and encodes the frames it shows: VP9 profile 1, 4:4:4 sRGB, constant quality 32, `--cpu-used 2`, a key frame at least every 128 frames, one thread a movie (`kp_movie_vp9`). The sound is the first track decoded by `kp_smk`, Vorbis at `-q 3` (`kp_vorbis_from_wav`: the packets alone, with their granules and block sizes). It refuses a sound more than half a frame longer or shorter than the video. `kp_webm_write` muxes them into WebM (the Vorbis headers Xiph-laced as `CodecPrivate`, 1 ms timestamps, a cluster from each key frame, `DiscardPadding` on the sound's last block; no dates or random UIDs, so the same movie gives the same bytes), and the movie is read back by nestegg (`kp_webm_streams`) before it is kept: the same packets and sound length, else the repack fails. Each WebM is cached as `<smk sha256>-vp9-q32-c2-k128-vorbis-q3-webm1.webm` in `generated/cache/repack/`. The 28: 202.0 MB for 605.4 MB, 146 s on 14 cores with an empty cache (FFmpeg's 138 s), the same bytes from the cache.

**The player reads WebM from either source** (`native/kp_app/kp_cine.c`, `sdl/kp_app_cine.c`). The pack's index and its reader are gone; `kp_cine_index_movies` indexes the catalog's movies the data has, as for an install. The host's `load` opens a movie when it begins: the WebM by its name (a repack's or a Remastered install's, HD2 first unless `--cinematics sd`), else the 1.17 install's Smacker file. `load` may answer `KP_CINE_LOAD_WAIT`: the run then waits for the movie, its time standing still (`r->loading`). A press while it waits ends it only if it comes after the wait began. On the web a movie whose group is not open is fetched alone by its range (`kp_res_raw_range`, `kp_fetch_start` with an offset; `web_sizes.txt` lists the groups, `kp_app_web_beside`); a computer or a phone opens every group it carries at start. The subtitles are read with each movie: the `.srt` of the language in effect, else of the install's text locale, else enUS (`KP_LOCALE_SUBTITLES`); `SUBS` and its overlay over the pack are gone, `kp_locale.c` only checks the language's `.srt` files.

**`--cine-check` over any source.** It plays every movie the data has to its end, the GPU's picture against the CPU's. A subtitle is checked pixel by pixel: at least three quarters of its white pixels seen white in the GPU's picture, and more than the movie shows without it (the old area test failed on terran4's «Терраны!», 15 white pixels over a bright frame). On the web it refuses with a line: the movies come by ranges as they play.

**Tests.**
- `test_cine` rewritten over the fixture WebM (cine0.webm sRGB, cine1.webm 4:2:0, cine1.srt): the index, the movie, the subtitles' times, the pictures equal to FFmpeg's, a refused movie (cut, not WebM, a frame off its time, a first frame not key, a zero packet), the run over a recording host (waiting for a movie, a press during the wait, a movie that does not load, the data gone).
- `test_convert`: `webm_reads_back` (our WebM read back by the player), `repack_movies_read_by_ranges` (the listing's ranges, a group added later, `kp_mpq_stored_at`), `repack_reads_back` (the fixture's SMK repacked as WebM, from the cache the second time), `movies_equal_the_converters` (below). The fixture's SMK comes from `kp_t_smk`, the Smacker writer `test_movies` uses too.
- `test_encoders` encodes through `kp_movie_vp9`.

**Equal to the Python converter.** Before `cinematics_smk.py` went, its SD pack from the 1.17 install was recorded in `tests/golden/cinematics/converter.json` (private): per movie its size, rate and frames, the SHA-256 of its VP9 packets and of its frame table, its Vorbis packets' count and bytes (but the comment header, the encoder's vendor) and its samples. `movies_equal_the_converters` transcodes two movies from `KP_TEST_ORIGINAL`, or compares all 28 of a repack (`KP_TEST_MOVIES`): every VP9 packet and frame table is equal, the Vorbis packets and samples too, their bytes 26 apart over 13.3 MB. The pictures being the pack's, no PSNR threshold is needed.

**The tools.**
- `tools/ci.sh`: the product's cine check per set over the bundle's groups (`KOPRULU_CINEMATICS`); cine checks over the 1.17 install and its repack (`sd WebM, 28 movies`), and over Remastered and its repack with ruRU's subtitles (`--locale ruRU`); the 28 movies against the golden (`KP_TEST_MOVIES=build/repack/original`).
- `tools/product.sh`: `KOPRULU_CINEMATICS=auto|sd|hd2|both|none` picks the repack's groups (`auto`: HD2, else SD, else none); `generated/video/` is no longer staged. The web lists the groups in `web_sizes.txt` (`tools/web_hd_heads.py --ranged`) and fetches them by ranges.
- `Makefile`: `cinematics` and `CINE=` gone; `make repack` carries the movies.
- `tools/defs_c.py` builds `CINM` from `defs/campaign/cinematics.json` itself; `tools/oracle/export_smk_ffmpeg.py` reads the table without the converter.

**Removed:** the `CINE` container and its reader, `converter cinematics`, `converter/emit/cinematics.py`, `cinematics_smk.py`, `sound.py`, `converter/formats/ivf.py`, `srt.py`, `kpf.container.header`, `test_cinematics.py`, `test_cinematics_smk.py`, the Ogg tests of `test_packs.py`, the fixture `cine.kpd`; oggenc is no longer a requirement.

**Verified.**
- `--cine-check`, 0 failures:
  - the 1.17 install (28 Smacker movies), its repack in groups and with `--single` (28 movies, 30,211 frames);
  - Remastered's install, SD (30,211 frames) and HD2 (31,156 frames);
  - Remastered's repack, HD2 and SD, with ruRU's 20 subtitled movies.
- A repack without its cinematics group, or HD2 asked of 1.17's: "no movies", the game without them.
- The web bundle in the browser pane: the intro fetched by a Range request (206) from `koprulu-cinematics.mpq`, played, and the next movie fetched when it was skipped.

**Decisions taken in C4:**
- **The 1.17 movies take Remastered's SD names**, so one reader serves both sources and a bundle cannot tell them apart.
- **A movie is read when it begins**, from a group or by its range, not indexed at start: no pack's index to keep beside the data.
- **The run waits for a movie** (`KP_CINE_LOAD_WAIT`) rather than the host blocking: the web's fetch is asynchronous.

**Open after C4:**
- `generated/video/` from the Python converter stays on disk where it was written; nothing reads it (remove it by hand), nor `generated/cache/repack-ogg/` (the Python repack's cache) and `generated/cache/repack-vorbis/` (C1's, `generated/cache/repack/` since C4). Deleted after C7.
- The web's movies are not covered by `tools/ci.sh` (checked in the browser pane by hand); `--cine-check` cannot run there. Resolved after C7: a headless Chrome plays the first start's movie by its range in `tools/ci.sh`.
- The fixture movies (`cine0.webm`, `cine1.webm`) are still made by FFmpeg (C7).


## C6 as built (2026-11-12)

**The placement table** (`native/kp_convert/kp_hd_ui_table.c`, `defs/scr/hd-ui-bw-1.17.0.1.json`, 17 KB).
- `kp_convert hd-ui-table DIR [--out PATH]` runs the Python converter's heuristics over a Remastered install. They live only here:
  - `place_frame`: where a GRP's HD frame lies when it is not at its usual place;
  - `same_picture`: which PCXs' HD art is the SD picture drawn again;
  - `place_piece`: where a console's centre piece lies.
- It writes, per variant:
  - each HD file the layout takes, with its CASC content key;
  - each moved frame's place;
  - the three consoles' centres.

  On 1.23.10: HD2 has 69 files, 62 frames moved, 128 pictures drawn again and 6 kept in SD. HD has 73 files, 62, 124 and 6. It runs in 2.5 s.
- `tools/defs_c.py` compiles it as `SHUI` into `kp_defs_bw_1_17_0_1.c`.
- `tools/ci.sh` makes the table again from the install and compares it with the committed one.

**The layout from the install** (`native/kp_app/kp_hd_ui_scr.c`).
- `kp_hd_ui_scr_build` lays a variant out by the table: each SD file of `kp_ui_data_art_paths` with HD art, its frames' sizes and formats from their DDS headers (ranged reads), their places. It gives `ui.kpd`'s HEAD, ASET and FRMS as the Python converter wrote them: HD2's 2,180 frames, HD's 2,184.
- A frame's image is read and decoded when the canvas first shows it (`kp_hd_ui_scr_image`, `kp_hd_ui_scr_rgba`).
- `kp_hd_ui_scr_console` builds `console.kpd` (`CONW`) from the install's HD2 consoles and the SD consoles' PCXs: the wings' tiles and the cells at one texel per logical pixel.
- `kp_hd_ui_scr_console_art` returns the repack's `koprulu\console.kpd` where there is one, else builds `CONW` from the install. A 1.17 install has none, so the original's console is drawn.
- `kp_hd_ui_scr.c` and `kp_hd_ui_table.c` build with `-ffp-contract=off`, so a wing tile's crossfade and the heuristics' sums round as Python's doubles did.

**The runtime** (`sdl/kp_app_hd_ui.c`, `sdl/kp_app_files.c`).
- **Opening a variant.** Over a CASC install, the variant is laid out at start. Over a repack, its `ui.kpd` opens from its head in the core (`koprulu\heads\ui\<variant>.kpd`, its bytes up to FRMS's end). The log names the source: `interface art (the install's files)` or `(the repack's ui.kpd)`.
- **Reading frames.**
  - From the install: read and decoded on demand.
  - From the repack: read at the place the listing gives within `koprulu-ui-<variant>.mpq`.
  - On the web: each frame fetched alone by a Range request.
- **Portraits.** A portrait's HD clip is found by its own name, `HD2/portrait/…/….webm` (HD2's, else HD's), as the install and the repack name it:
  - `kp_res_raw_read` on a computer;
  - `kp_res_raw_range` and a fetch on the web.

  Only the portrait set's movies have clips. The glue's movies stay SD: Remastered's are larger than the clip places.
- **Start-up cost.**
  - The layout at start: 70 to 77 ms (HD2 or HD).
  - The consoles' art: 164 ms from the install, 2.4 ms from the repack.

**The repack** (`native/kp_convert/kp_hd_ui_pack.c`, `kp_repack.c`).
- For each variant of the HD sets asked (`hd2-astc`'s is `hd2`), `kp_hd_ui_pack` writes the `ui.kpd` of its layout (`HDUI`): PNGs by `kp_icon_png`, BC blocks as the DDS holds them.
  - It goes in `koprulu-ui-<variant>.mpq`, stored uncompressed and read by ranges, its names under no prefix.
  - Its head goes in the core.
- With any HD set, each portrait's WebM goes in `koprulu-portraits.mpq` under its raw name (HD2's, else HD's): 621 clips, 72.8 MB.
- From any Remastered install with HD2, `koprulu\console.kpd` goes in the core (compressed).
- On 1.23.10:
  - `koprulu-ui-hd2.mpq` is 35.9 MB and `koprulu-ui-hd.mpq` 144.0 MB;
  - the `hd2 hd` `--single` repack takes 50 s (37 s in C5);
  - the `hd2` groups take 18.3 s.
- The report adds `the HD interface: N variants' ui.kpd, M portraits, the consoles' art`.

**The bundles** (`tools/product.sh`).
- A bundle carries the interface groups of its HD sets' variants (`UI_GROUPS`), and `koprulu-portraits.mpq` with any set.
- The console's pieces come from the core: `data/console.kpd` is no longer staged.
- The web lists its ranged groups and their sizes in `web_sizes.txt`, now written by `product.sh` itself. `tools/web_hd_heads.py` is removed, since no heads are preloaded any more.

**Tests.**
- **`test_hd.ui_equals_the_converters`** (`KP_TEST_SCR`): each variant's layout, its packed `ui.kpd` sections, `CONW` and the 621 portraits' clips. They are checked against `tests/golden/hdui/converter.json` (private), recorded from `converter hd` before this stage removed it.
- **`test_hd.repack_ui_equals_the_converters`** (`KP_TEST_HD_REPACK`): each variant's `ui.kpd` opened from its head and read from its group, the core's `console.kpd`, and the portraits' group, against the same golden.
- **`test_hud`'s console tests** lay out over `kp_t_console_art`: `CONW` built once a process from `KP_TEST_SCR`. Without it they skip, as they did without `generated/data/console.kpd`.
- **`tools/ci.sh`:**
  - the table made again equals the committed one;
  - the repack's interface against the golden;
  - `hd_glue.kps` and `hd_game.kps` over the install and its repack, each log checked for the interface's source;
  - each language's `hd_game.kps` walk, now always run, since the interface no longer needs `generated/hd`.

**Verified.**
- `test_hd` over the install and the `hd2 hd` repack: byte for byte the converter's.
- `test_hud`: 82 tests, none skipped, over the install's `CONW`.
- `hd_glue.kps` (89 steps) and `hd_game.kps` on terran02, zerg02 and protoss02 (159 steps) over the install and over the repack: 0 failures, and no SD pixel over HD art. `hd_glue.kps` was also run in HD at 1280 × 960.
- The web bundle in the browser pane: terran02 in HD2, the console and frames fetched by ranges from `koprulu-ui-hd2.mpq`, the portraits from `koprulu-portraits.mpq` (206), with no error.

**Decisions taken in C6:**
- **The table names each HD file by its CASC content key** (the MD5 the index holds), not by the SHA-256 the plan named. The key comes from the index without reading the file, and the install's HD files are hundreds of megabytes in all. A file whose key differs is left in SD, with the log line `regenerate the table with kp_convert hd-ui-table`.
- **`HDPV` (`portraits.kpd`) is gone.** The portraits are Remastered's WebM under their own names, in the install and in the repack alike, so one lookup serves both sources.
- **A group per interface variant, and one for the portraits.** HD's `ui.kpd` is 144 MB and HD2's 36 MB. The phones and the web carry only HD2's; every HD bundle carries the portraits.
- **`console.kpd` in the core, whatever HD sets are asked:** every kind of graphics composes the console from it, the original's art included (ADR 0011).
- **The layout is built at start** (70 to 77 ms) and the consoles' art too (164 ms). Neither waits for a thread.
- **The `ui` group's names have no prefix:** the runtime finds a frame's container and a portrait by their names alone.

**Removed:**
- the Python `converter/emit/hd.py`, `hd_ui.py`, `hd_video.py` and their tests;
- `converter hd` and `converter console`, and `emit/files.py`'s `link_file`;
- the Makefile's `hd`, `console`, `convert-remastered` and `remastered-extras` (`make hd-ui-table` added);
- `HDPV` (`kp_hd_video_*`, `test_present.hd_video_index`);
- the runtime's reads of `generated/hd/<variant>/ui.kpd`, `portraits.kpd` and `generated/data/console.kpd`;
- `tools/web_hd_heads.py`;
- `kp_load_check` and CI's ASan parse of `generated/*.kpd`: nothing writes such files now. The sanitizer suites parse the built containers in `test_hd` and `test_hud`.

**Open after C6:**
- `generated/hd/`, `generated/data/console.kpd`, `generated/locale/` and `generated/video/` stay on disk where the Python converter wrote them. Nothing reads them; remove them by hand.
- The consoles' art from an install costs 164 ms at every start; it could move to a thread. After C7: 81 ms, the nearest colours cached.
- Phones from the interface groups are not run yet (C7's Fold7 run).

## C5 as built (2026-11-12)

**The HD packs from the anims** (`native/kp_present/kp_anim.c`, `kp_pages.c`, `native/kp_app/kp_hd_scr.c`).
- `kp_anim_open` reads an anim's head and frame records. `kp_hd_scr_owners` picks the anim that draws each GRP entry: the first image naming the GRP, or the image `SD/images.rel` relates it to.
- `kp_hd_layout_build` lays out an SD pack's HD pack from the anims' heads alone:
  - each image's frames by their block rectangles (a rectangle two frames share goes in once);
  - the lit images' groups first, on pages of their own;
  - packed by `kp_pages_pack_groups` on 2048 × 2048 pages: a skyline, the lowest then leftmost place on the last `KP_HD_OPEN_PAGES` (4) pages.

  It gives the Python converter's HEAD and FRMS, page for page.
- `kp_hd_layout_page` builds one page's layers from the anims' sheets when the loader asks for it. `kp_hd_pack_build` writes a whole `HDPK`: `kp_kpf_layout` lays out the container, the pages are filled in place, and `kp_kpf_seal` writes the CRCs.
- `kp_hd_scr_tileset` and `kp_hd_scr_water` build `HDTS` and `HDWA` from the same files the converter read.
  - `HDTS`: `TileSet/<name>.dds.vr4`, `.tmsk`, `_mask.dds.grp`, and `.dds.grp` for creep's edges.
  - `HDWA`: `effect/water_normal_1.dds.grp` and `_2`.

**The runtime** (`sdl/kp_app_world.c`, `sdl/kp_app_hd.c`, `sdl/kp_app_flow.c`).
- **Choosing a source.** `kp_world_hd_pick` makes two passes: first for a source whose pages the context takes as they are, then for one it draws decoded.
  - In each pass it tries the repack's sets (`hd2`, then `hd2-astc`, then `hd2-etc2`), then the install's anims (BC).
  - A set's format comes from its terran pack's head.
  - `--graphics auto`, the video options and the gate on a variant all ask it; none of them looks for `generated/hd/` any more.
- **From an install.** At a mission's start the packs are laid out from the anims' heads. On terran02: HD2 in 155 ms, HD in 138 ms; the tileset and water in 66 ms (HD2) and 275 ms (HD). The loader's thread builds each page from the sheets through a reader of its own (`kp_res_raw_place`, `kp_res_reader_*`), because a `KpRes` belongs to one thread.
- **From a repack.** Each pack opens from its head in the core (`koprulu\heads\<set>\<pack>.kpd`). Its pages are read from the set's group:
  - on a computer or a phone, by the loader's reader at the place `kp_res_raw_place` names;
  - on the web, each layer fetched alone by a Range request within `koprulu-<set>.mpq`, at the offset the core's listing gives.
- **Without a loader thread** (the web, or a platform where none starts), a page in a URL's group is fetched by its range; any other page is read or built at once, when it is asked for.

**The repack** (`native/kp_convert/kp_repack.c`, `kp_hd_transcode.c`).
- **Which sets.** `kp_convert repack --hd LIST` picks the sets:
  - from a Remastered install the default is the BC variants it holds (`hd2 hd`); `--hd none` writes none;
  - `make repack` asks for `hd2,hd2-astc,hd2-etc2` for the groups (phones and the web) and takes the default for `--single`;
  - a variant the install lacks stops the repack.
- **Where they go.** Each set is a group, `koprulu-<set>.mpq`, apart with `--single` too. It is stored uncompressed, its files placed by the core's listing (`in` lines, as the movies' are), with the status `converted`. Each pack's head (about 290 KB a set) also goes into the core or root. [The format](formats/repack.md).
- **Phones' sets.** `kp_hd_transcode` (moved out of `kp_hd_phone`) transcodes them:
  - the packs into ETC2 and EAC, or into ASTC; the Arm encoder is now linked into `kp_convert_lib` when built with `KP_ASTC`, which `make convert-tool` turns on;
  - the tilesets and water into ETC2 for both sets;
  - the pages encoded on `kp_work`'s threads;
  - each container cached once as `<BC SHA-256>-<etc2|astc>-t1.kpd` in `generated/cache/repack/`.
- **`kp_hd_phone`** keeps `--report`, `--report-tiles` and `--selftest`; the selftest transcodes a small pack in memory.

**Measurements** (Remastered 1.23.10, macOS on an M-series Mac, an empty cache):

| Sets | Containers | Pages | Groups | Time | Peak memory |
|---|---|---|---|---|---|
| `hd2` | 15 | 50 | 648.8 MB | 16.7 s | 896 MB |
| `hd2 hd` (`--single`) | 30 | 243 | 648.8 MB and 2,449.4 MB | 37 s | 2.6 GB |
| `hd2-etc2 hd2-astc` | 30 | 100 | 606.9 MB and 545.4 MB | 131.6 s | 1.3 GB |

The phones' run transcoded 21 containers: the tilesets and the water once for both sets.

**The bundles** (`tools/product.sh`).
- The HD sets a bundle carries, of those its repack holds:
  - the web: `hd2`;
  - the phones: `hd2-$KOPRULU_PHONE_BLOCKS`, `astc` by default and `etc2` on the iOS simulator;
  - the computers: `hd2` and `hd`.
- The bundles no longer stage `generated/hd/*/packs`, `tilesets` or `water.kpd`, and no longer transcode for phones.
- The web lists the set's group in `web_sizes.txt` (`tools/web_hd_heads.py --ranged`). `ui.kpd` and `portraits.kpd` are still the converter's (C6).

**Tests.**
- **`test_hd`, synthetic:** `packer_keeps_a_group_together`, `packer_fills_only_the_open_pages`, `anim_head_and_frames`, `relations_of_images`, `page_keeps_the_lit_pages_layers`.
- **`test_hd`, over the install (`KP_TEST_SCR`):**
  - `packs_equal_the_converters`, sharded: every pack's HEAD and FRMS and each page's layers, HD2 and HD;
  - `tilesets_and_water_equal_the_converters`.

  The golden is `tests/golden/hd/converter.json` (private): the SHA-256 of each section and page layer that `converter hd` wrote, recorded before this stage removed it.
- **`test_hd.repack_equals_the_converters`** (`KP_TEST_HD_REPACK`): a repack's BC sets, each pack opened from its head as the runtime opens it, against the same golden.
- **`kp_hd_phone --selftest`:** a small pack through ETC2 and ASTC, opened and read back.
- **`tools/ci.sh`:**
  - `hd_glue.kps` and `hd_game.kps` over the install and over its repack, each run's log checked for the source it drew from;
  - the repack's sets against the golden.

**Verified.**
- `test_hd` over the install, and over the `hd2` and `hd2 hd` repacks: byte for byte the converter's.
- The product's HD scripts over Remastered's install and its repack: 0 failures (`tools/ci.sh`).
- `hd_game.kps` on terran02 over the `hd2-etc2` set on macOS (decoded on the CPU): 0 failures.
- The web bundle in the browser pane: terran02 drew its HD2 art lit (16 of 16 lit pages) from `koprulu-hd2.mpq`, its pages fetched by 32 Range requests (206).

**Decisions taken in C5:**
- **The packs are laid out at a mission's start** from the anims' heads. Their pages are built when the loader asks for them; C0's measurements left room for that.
- **The packs' heads go in the core**, so a pack opens without its group and the web fetches only pages.
- **A phone's ASTC set keeps its tiles and water in ETC2**, as `kp_hd_phone` did (ADR 0013's TODO on ASTC tiles).
- **A repack's set is preferred over the install's anims** where both are open; the pages are the same.

**Removed:**
- `converter/emit/hd.py`'s packs, tilesets and water (it keeps `ui.kpd` and `portraits.kpd` until C6);
- `converter/formats/anim.py` and their Python tests;
- `kp_hd_phone`'s `etc2` and `astc` modes, and CI's check of its symbols;
- `tools/product.sh`'s phone transcoding (`stage_hd_phone`, `transcode_hd`);
- the runtime's reads of `generated/hd/<variant>/packs/`, `tilesets/` and `water.kpd`.

**Open after C5:**
- The `hd2 hd` repack peaks at 2.6 GB of memory; not profiled. Measured after C7: 1.2 to 2.9 GB from run to run, during the HD sets; `kp_convert`'s own allocations peak at 826 MB every run, the rest is libmalloc's cache of freed large blocks (0.76 GB with `MallocLargeCache=0`), larger after the machine's memory was under pressure.
- The anims' read with a cold disk cache is still not measured. Measured after C7: about twice the warm layout.
- While the pick looks for a set, `kp_res_raw_place` notes the packs it asks about among the files read, so a `--res-log` lists them before any page is read.
- The ASTC set is not drawn anywhere yet: the computers' build has no ASTC decoder. The Fold7 run is C7's.
- `generated/hd/<variant>/packs/`, `tilesets/` and `water.kpd` stay on disk where the converter wrote them. Nothing reads them but `tools/ci.sh`'s ASan parse of `generated/*.kpd`; remove them by hand.

## C7 as built (2026-11-12)

**The Python converter is gone.** `converter/` no longer exists. What research still reads moved, with its history, to
`tools/research/scdata/` (87 modules: `formats`, `mpq`, `casc`, `kpf`'s container, `kpd` and `kpm` writers, `analyze`,
`derive`'s rules, terrain and unit records, `emit`'s atlas, files and terrain, the catalog, the missions' survey, the
packs' membership, the UI tables, the scenarios, and their 28 test modules). It is imported as `scdata` with
`tools/research` on the path: the emulator, the oracles' exporters, the probes and `tools/defs_c.py` import it so.
- **Deleted:** the command line (`__main__.py`); the derived terrain's Python twins (`derive/walk.py`, `regions.py`,
  `contours.py`, `mission.py`); `emit/pages.py` and `kpf/resources.py` (the packer's twins); `kpf/dump.py`;
  `kpf/fixtures.py`; `tests/test_derive.py` and `tests/test_resources.py`.
- **The research's commands** are scripts of their own, each the old command's port:
  - `tools/research/scr_manifest.py --data DIR [--scr DIR]`: the committed tables of what the game reads
    (`defs/scr/inputs-*.json`, `texts-*.json`, `defs/campaign/packs-*.json`);
  - `tools/research/scr_compare.py --data DIR --scr DIR`: Remastered's DAT tables and scripts against the profile's;
  - `tools/research/scr_digests.py --scr DIR --out PATH`: the twin `tools/ci.sh` compares with `kp_convert digests`.

**The fixtures are committed** (`tests/fixtures/kpf/`, 149 files, 2.2 MB). They are the files the Python converter's
`fixtures.py` wrote, all synthetic (no original byte), with their `expect.json`. `kpf_large_file` (`test_data.c`) builds
its 8 MiB container itself (SHA-256 digests of a counter, `kp_kpf_build`) and checks it against `expect.json`, so the
largest fixture is not committed. CI's `fixtures` step is gone. The Python tests that built inputs with `fixtures.py`
take them from `scdata/tests/synthetic.py` or read the committed files.

**The derived terrain against the evidence, in C.** `test_chk.the_campaign_maps_derived_terrain_is_the_originals`
(`KP_TEST_ORIGINAL`) builds the walk raster, the region graph and the contours of all 68 campaign CHKs as a map's load
does (the 64 catalog maps through `kp_campaign_mission`, the four `VER=57` maps from their first `MTXM`) and compares
their SHA-256 with the native hashes in `docs/research/evidence/` (the walk rasters and graphs of
`native-campaign-regions-probes.json`, the contours of `native-contours-probes.json`), and the 64 catalog maps' `RGNS`
with the graphs the game builds (`native-ingame-regions.json`). It takes 0.9 s.

**The Python tests** run by `tools/python_tests.py` (`tools/converter_tests.py` renamed): `scdata`'s tests and
`tools/tests/` (the runner's cache, the oracle's golden manifests). 230 pass, 2 skip, in 47 s; `tools/ci.sh` runs them
in the background as before (`build/python-tests.log`).

**The bundles** carry `koprulu*.mpq` only since C5 and C6: `tools/product.sh` stages the repack's groups and nothing of
`generated/` but the icon and a debug build's scenarios and scripts.

**Verified.**
- The C suites over the committed fixtures, with the old generated ones moved away: `test_data`, `test_kp_core`,
  `test_cine`, `test_movies`, `test_scr`, `test_ui_data`, `test_chk`.
- `tools/defs_c.py --check` after the defs' comments were pointed at `scdata`.
- `tools/research/scr_manifest.py --data … --scr …` writes `defs/scr/inputs-bw-1.17.0.1.json` and
  `defs/campaign/packs-bw-1.17.0.1.json` byte for byte as committed. `defs/scr/texts-bw-1.17.0.1.json` lost
  `rez\video.bin`'s dialog: C1's run dropped the file from the input manifest (nothing has read it since our own Video
  Options dialog replaced it, 2026-10-27), but the text table was not made again then. The table and the compiled
  `kp_defs_bw_1_17_0_1.c` are now the generator's; `test_locale`'s golden (the Python overlays, which never had that
  dialog) still equals.
- The full `tools/ci.sh` (`KOPRULU_FULL=1`).

**Decisions taken in C7:**
- **The Python test runner stays, renamed.** The plan removed `tools/converter_tests.py`; the research's readers keep
  their tests, and CI still runs them.
- **`scr-manifest` stays in Python** (`tools/research/scr_manifest.py`): it records what the game reads from the
  analyzer's survey, which is research code. `kp_convert manifest` (the reads `kp_res` records itself) is a `TODO:` in
  the script.
- **The fixtures are committed** rather than written by `kp_convert`: they are small and synthetic, and their
  expectations are the Python twins' values, which nothing could recompute once the twins were gone.
- **`test_derive`'s synthetic cases are dropped** (the builders' steps on hand-made maps). The C test covers the 68
  real maps against the native evidence, which is what the twins were checked by.
- **FFmpeg is research only:** `tools/oracle/export_smk_ffmpeg.py` (a CI step when FFmpeg is there) and the research
  readers' Remastered movies.

**The Fold7** (Galaxy Z Fold7, wireless adb), the debug APK (1.1 GB) of a phone's repack of Remastered 1.23.10
(`kp_convert repack /Applications/StarCraft --hd hd2-astc --locales ruRU`, 23.5 s: `koprulu-hd2-astc.mpq` 545 MB,
`koprulu-ui-hd2.mpq` 36 MB, `koprulu-portraits.mpq` 73 MB, `koprulu-locale-ruru.mpq` 71 MB, the HD2 movies 329 MB;
`KOPRULU_LOCALES=ruRU KOPRULU_CINEMATICS=hd2 tools/product.sh android debug`):
- `hd_game.kps` on terran02: 159 steps, 0 failures; the world from the repack's `hd2-astc` (42 page slots in ASTC
  blocks, 16 lit of 16), the interface from its `ui.kpd`, no SD pixel over HD art; the consoles' art in 3.4 ms.
- `hd_glue.kps`: 89 steps, 0 failures (the menus, a briefing, a victory, the score; the portraits' group).
- `hd_game.kps` on terran01 with `--locale ruRU --speech ruRU`: 159 steps, 0 failures; the language's group read, 592
  voices, terran01's 6 actions' TIME; no character without a glyph, no text or speech refused.
- `--cinematics hd2 --cine-check`: 28 movies, 31,156 frames, 0 failures, each read by its range from
  `koprulu-cinematics-hd2.mpq`.

The phone locks itself a second after the app starts unless it is kept unlocked: a run then stalls (`onPause`), and is
run again once the screen is on.

**Open after C7:**
- `generated/hd/`, `generated/locale/`, `generated/video/`, `generated/data/console.kpd` and
  `generated/cache/repack-ogg` stay on disk where the Python converter and C1 wrote them; nothing reads them. Remove them
  by hand. Deleted after C7.
- `kp_convert manifest` (see above).

## After C7: the open items (2026-11-12)

**Closed.**
- **The tests clean up after themselves under wasm too.** `kp_t_temp_dir`'s folders stayed under `build/` when the
  suites ran in wasm under node: musl's `remove()` takes a folder for `rmdir` only when `unlink` says `EISDIR`, and
  macOS says `EPERM`. `remove_tree` (`tests/c/kp_test.c`) now unlinks files and `rmdir`s folders itself.
- **The stale outputs are deleted:** `generated/hd/`, `generated/locale/`, `generated/video/`,
  `generated/data/console.kpd`, `generated/cache/repack-ogg/` and `repack-vorbis/`, the stages' scratch builds under
  `build/`.
- **macOS's duplicate-library warning:** `kp_test` links `kp_oracle_lib` and `kp_gfx_passes` alone; `kp_app_core` (and
  zlib for `test_original`'s PNGs) comes through the oracle's library.
- **CascLib's 1007 lines** were `ERROR_FILE_OFFLINE`: the install names enUS's speech of `expterran/terran02` and
  `terran05b` but has not downloaded it, while a differing `SD/` copy of the same files is read. `kp_casc_open_file`
  says "not downloaded in this install"; `read_casc` says it once per file, only when no other candidate was read.
- **The repack's sizes:** a group's line gives the archive's size on the disk and its files' before the compression
  (`koprulu-core.mpq: 3187 files, 46.7 MB (63.9 MB of files)` for the web's repack of Remastered;
  a group's `archive` in `KpRepackReport`, `kp_fs_size`). Before, the line gave the files' bytes alone, which read as the archive's.
- **`kp_convert extract`** of a Remastered name no manifest lists (`HD2/unit/cmdicons/wirefram.dds.grp`) writes the
  raw file (`kp_res_raw_read`), not the 1.17 rebuild of a name that happens to match.
- **`locale-check`'s `scripts.base`** is the list of the scripts a language keeps the base's for (`script_names`), as
  `scripts.translated` is, the Python converter's form; `kp_locale_script` names a script a language may translate.
- **The web carries the language groups.** `tools/product.sh` stages them beside the page, fetched whole when wanted:
  the host's `language_files` (`kp_app_flow.c`, through `kp_res_group_wanted` and `kp_app_group`) tells the flow whether
  a language's files are there. A language whose group is beside the page is listed with its text and speech; the
  start's language and speech wait for their group before the flow starts (`kp_flow_languages_ready`, asked by
  `SDL_AppIterate` each frame before `kp_app_flow_init`), and one chosen in the main menu stops the glue at the choice
  until it came (`start_over`). A group that cannot be had leaves the base's parts, with a line.
  `test_flow.a_language_waits_for_its_group`.
- **`tools/ci.sh` checks the web's language, movie and HD** (`KOPRULU_WEB` with `KOPRULU_SCR`):
  `kp_convert repack --hd hd2 --locales ruRU` into `build/web_scr` (22 s with the caches), the debug bundle over it, and
  three runs in a headless Chrome by `tools/web_run.py` (the page served with byte ranges on a port of its own, the
  app's arguments by `?args=`, the console's lines into `build/web_<name>.log` until one matches; a profile of its own,
  removed after; `--force-device-scale-factor=2`, since the HD art wants two pixels a logical one):
  - `--locale ruRU`: ruRU's group fetched before the glue starts, its 592 voices, and the first start's intro
    (a new profile's), Blizzard's logo fetched by its range: 171 of 171 frames decoded, 171 pictures shown;
  - `hd_game.kps` on terran02: 159 steps, 0 failures, the world from `koprulu-hd2.mpq` and the interface from
    `koprulu-ui-hd2.mpq` by ranges.
- **The device bench times the glyphs** (`kp_bench --glyphs DATA LOCALE [RUNS]`: the data built from an install or
  a repack, the language's text, then `kp_locale_glyphs` RUNS times; the minimum, median and maximum).
  `tools/native/android_bench.sh --glyphs LOCALE [RUNS]` repacks `KOPRULU_SCR` with `--hd none --locales LOCALE` and
  runs it on the phone over the core and the language's group; `tools/ci.sh` runs it over the install (ruRU, 3 runs).
  The script wakes the phone's screen itself right before the timed runs (`KEYCODE_WAKEUP`, the power key's: a locked
  phone stays locked), since a dozing phone stops the CPU for 150 to 200 ms at a time; it refused to run with the
  screen off before, and the screen went off while the files were pushed. What a run pushed (the data: 50 to 120 MB)
  is removed from the phone after it, the repack written for it from the disk. Its step bench ran on the Fold7 again,
  first since stage D8c: `bench_200` equal to the original at all 761 steps, mean 0.096 ms, max 0.961 ms.
- **The consoles' art from an install: 164 to 81 ms.** Most of it was `art_cells`' nearest-colour search over the 127
  colours for each cell; a mean colour seen before now takes its answer from a small table (`nearest`, 4,096 entries),
  and the console's SD PCX is read once (`kp_files_pcx_pixels`). `CONW` stays equal to the converter's
  (`test_hd.ui_equals_the_converters`). What is left is reading the DDS files (about 43 ms) and decoding them.
- **`kp_convert` writes the movies and the HD sets on Linux and Windows too.** Each repacked the 1.17 install and
  Remastered (`--hd hd2,hd,hd2-astc,hd2-etc2 --locales none`) with a cache of its own; the listings compared with
  macOS's, archive by archive and line by line:
  - Linux arm64 (GCC 15.2 in the image of `tools/native/linux.Dockerfile`; Remastered as an APFS clone in the
    repository, since Docker shares no `/Applications`): 1.17 in 241 s, Remastered in 299 s;
  - Windows x86-64 (mingw-w64, `kp_convert.exe` under Wine): 1.17 in 352 s, Remastered in 330 s;
  - equal: every SD file, the `hd2`, `hd` and `hd2-etc2` sets (`hd2-astc` too since the next item), both
    interfaces' `ui.kpd`, the 621 portraits and `CONW`; the 28 transcoded movies' VP9 packets (`ffmpeg -f framemd5`
    per stream);
  - not equal: the Vorbis bytes, the speech's and the movies' sound alike (1,668 of the 1.17 install's 2,236 Oggs
    apart from macOS's, 50 between Linux and Windows): they follow the build, the compiler and its math library, not
    the processor (Linux and Windows agree on most though one is arm64 and the other x86-64; macOS built with
    `-ffp-contract=off` is as far from both as before). The sound is as good on each: terran2's against its SMK's,
    21.36 dB from all three. A listing's Ogg line keeps the WAV's digest, so the listings stay comparable.
- **The phones' ASTC set is the same on every desktop.** The three wrote three different `hd2-astc` sets: the encoder
  was built without the flags of its own invariant build (`ASTCENC_INVARIANCE`, on by default in its CMake), which
  its documentation says make every compiler and processor write the same blocks. `kp_astcenc.cmake` sets them now
  (`-ffp-contract=off -fno-math-errno`, and `-fno-unsafe-math-optimizations` for clang), and the transcodings' cache
  takes `t2` (`KP_HD_TRANSCODE_SETTINGS`). macOS and Windows then wrote the same set, byte for byte; Linux's GCC
  still differed in 3 of its 16-byte blocks (1 in `effects`, 2 in `protoss`; of about 28 million), from run to run
  alike. Cut out alone, one block kept the difference; GCC wrote the same at -O0 to -O3, and so did clang at -O0, but
  clang at -O1 and above wrote macOS's; with the encoder's sources built at -O0 and one at a time at -O1, the one
  that turned it was `astcenc_weight_align.cpp`. Its `prepare_angular_tables` fills the angles' sine and cosine
  tables with `sinf` and `cosf`, which clang at -O1 joins into Apple's `__sincosf_stret`; those functions are not
  correctly rounded and differ from one library to another: against `sin` and `cos` in double rounded to float,
  macOS's are off in 70 of the 4,096 entries, glibc's in 48, mingw-w64's in 1. A patch of the encoder
  (`tools/toolchain/patches/astc-encoder-5.7.0/0001-angular-tables-in-double-precision.patch`, applied by
  `fetch.sh astc`) fills them in double precision and rounds: then macOS, Linux and Windows write the same
  `hd2-astc` set, byte for byte, and it is the one macOS wrote before, so its quality is unchanged
  (`kp_hd_phone --report` on zerg's page 2 gives ADR 0013's 34.46 / 43.72 / 29.87 dB for 4 x 4).
- **The tools build whole on Linux and Windows again.** Building every target there (not only `kp_test` and
  `kp_convert`) found four breaks: on Windows `kp_fs_size`'s `_stat64` without `<sys/stat.h>`, a maybe-uninitialized
  buffer in `kp_hd_ui_table.c` (`logical`, now zeroed) and `zlib.h` missing from the C++ check (the bundled zlib's
  folder is passed now); with GCC 15 `-Wclobbered` in `kp_bench.c` and `kp_replay.c` (a value read after `setjmp`,
  now `volatile` as those set after it were). Both cross builds take `-DKP_ASTC=ON`: without it `repack` refuses the
  `hd2-astc` set. Neither build is in `tools/ci.sh` (GitHub's runs build Linux with Ubuntu's GCC).

**Measured** (Remastered 1.23.10, macOS on an M-series Mac):
- **The repack's peak memory** (`/usr/bin/time -l`, the caches full): the default (`hd2 hd`, every language) 1.2 to
  2.9 GB over six runs, 48 to 52 s, `--single` or not alike; `hd2` alone with ruRU 0.9 GB in 22 s. The peak comes while
  the HD sets are built (the resident size sampled against the archive being written). Why it differs from run to run:
  not `kp_convert`'s allocations. Their live bytes, counted by an interposed `malloc`/`calloc`/`realloc`/`free`
  (`malloc_size` of each block, `DYLD_INSERT_LIBRARIES` given through `env`, since SIP strips it from `/usr/bin/time`'s
  children), peak at 824 to 826 MB in each of ten runs: the HD sets are built on one thread, the same allocations in
  the same order, and the encoders' threads add 2 MB at most. What lies above them is libmalloc's cache of freed large
  blocks: `vmmap -summary` during the HD sets shows 703 MB of "Malloc Large (empty)" beside 703 MB of live "Malloc
  Large", and with `MallocLargeCache=0` the peak footprint is 0.76 GB (`MallocDeferredReclaim=0`,
  `MallocAggressiveMadvise=1`, `MallocXzoneHugeCacheSize=0` and `MallocXzoneSegmentDeallocate=1` change nothing). How
  much the cache keeps follows the machine's memory: 1.11 to 1.21 GB footprint in ten runs on a quiet machine, 1.7 to
  2.6 GB in the runs during and after a pressure episode (`memory_pressure -p 20`, or a CI run just ended), the same
  826 MB live in each. A cold disk cache does not change it (1.15 and 1.20 GB). Nothing to fix in `kp_convert`: the
  cache is freed memory, which macOS takes back under pressure.
- **The anims with a cold disk cache** (the CASC files' pages dropped by `msync(MS_INVALIDATE)` over a read-only
  mapping, no root needed): terran02's packs laid out in 261 ms in HD2 and 282 ms in HD, against 137 and 138 ms warm;
  the interface's layout 82 against 75 ms (HD2), 102 against 83 ms (HD).
- **The glyphs** (175 characters, the five supplements and `VFNT`) in 30 ms on the Mac; the log says it at each start
  (`glyphs of L: N characters in T ms`; `language L:` lines are refusals, which `tools/ci.sh` fails on). On the Fold7
  46 ms: the medians of two series of 15 runs of `kp_bench --glyphs` 45.9 and 45.4 ms (41.7 to 59.7 ms; the text
  before them 11.6 and 12.0 ms), by `tools/native/android_bench.sh --glyphs ruRU 15` over a repack's core and ruRU's
  group, the phone locked. C7's run
  there had bounded it before: 73 ms between `speech of ruRU: 592 voices` and the mission's TIME in its log, the text,
  the glyphs and the rest of the flow's start in them.
- **A start from the install** to the main menu: about 0.95 s from the window, of it the consoles' art 81 ms.

**Still open:**
- `kp_convert manifest` (the TODO in `tools/research/scr_manifest.py`): its design waits for approval.
