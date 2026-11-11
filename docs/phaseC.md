# Everything from an install or from `koprulu*.mpq`, the converter in C

Design: [ADR 0019](adr/0019-install-or-repack-converter-in-c.md). The user approved the plan on 2026-11-11.

The folder `--data` names is the only source:
- **an install:** every asset in its original format, from MPQ or CASC;
- **a folder of `koprulu*.mpq`:** `kp_convert`'s compact forms.

The converter becomes `kp_convert`, in C, sharing its builders with the runtime. The Python `converter/` is deleted; Python stays for research only.

| Stage | What | State |
|---|---|---|
| C0 | ADR 0019; libogg, libvorbis with vorbisenc and libvpx's VP9 encoder built for the host's tools (`KP_ENCODERS`); the spikes | done 2026-11-11 |
| C1 | `kp_convert`: `repack`, `verify`, `digests`, `manifest` (`kp_data_probe` folded in), `icon`, `scenarios`, `terrain`; `kp_res`: raw CASC and repack names, `kp_res_raw_stream_open`; the Makefile, README, `tools/product.sh` and `tools/ci.sh` on `kp_convert` | |
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
- On Linux with GCC 15.2, `tests/c/test_res.c`'s `dir_path` trips `-Werror=restrict` at `-O3`. This is a false overlap between two members of one structure, and it predates phase C. The Linux tools build is not part of `tools/ci.sh`, so it went unseen. The C0 check was built with `-Wno-error=restrict`.
- `kp_test` does not build for Windows (`mkdir` with a mode, among others). The user decided `kp_convert` is built for every desktop, Windows, Linux and macOS (2026-11-11), so C1 makes `kp_test` build there too.
