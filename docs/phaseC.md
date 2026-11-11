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
| C2 | Speech: the lines and voices of `locales/<loc>/Assets/` played as they are, TIME merged from the language's CHK when a mission loads; the repack's language group recompressed; the speech overlays go | |
| C3 | Language texts: `kp_locale.c` (the six tables, dialogs by `defs/l10n`, `rez\*.txt`, the maps' STR, subtitles, the glyphs at start, VFNT from TTF); `kp_convert locale-check`; the text overlays and `glyphs.kpd` go | |
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
| `extract DIR NAME OUT` | a file as `kp_res` reads it, or a Remastered file by its CASC name (`kp_res_casc_file`) | `converter extract`, `scr-extract` |
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
- **The listing stays `koprulu repack 2` until C2**: v3's raw names come with the language group, the first files that need them.
- **The bundles carry no `kp_convert`.** A bundle is the converter's output, its repack; a player with an install points `--data` at the install, which needs no conversion. `kp_convert` builds and runs on each desktop on its own (README: mingw-w64 for Windows).
- **`kp_res_raw_stream_open` waits for its consumer**, the HD page thread of C5. `extract` reads a raw CASC name through `kp_res_casc_file`.

**Open after C1:**
- The old `generated/cache/repack-ogg/` is not read any more; it can be deleted by hand.
- macOS's linker warns of `libkp_app_core.a` given twice when it links `kp_test`; harmless.
