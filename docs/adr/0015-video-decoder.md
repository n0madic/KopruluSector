# ADR 0015: The VP9 decoder for the cinematics, an optional build dependency

Status: accepted (cinematics, 2026-10-31; the user approved it after the C0 measurements). It changes the dependency policy of [ADR 0012](0012-sdl3-runtime.md) item 5 and the line of [ADR 0013](0013-remastered-data-source-and-hd.md) "the runtime gains no dependency" for the builds that ask for the cinematics. The user chose libvpx, the cinematics as Remastered ships them (VP9 as is), the SD and HD2 variants selectable apart from the art, desktops and Android first, the library optional at build time (2026-10-31).

## Context

The product plays no full-screen movie. `kp_hud.c` skips a campaign's cinematic rows and the main menu's View Intro does nothing. The original played 28 cinematics (`smk\blizzard`, `starintr`, `PreTerran_1..4`, `Terran1..4`, …, `xZerg`; `docs/research/atlas/p28.md`, `PlayCinematic` `0x47ec60`) from Smacker files in `StarCraft.mpq` and `BroodWar.mpq` (610 MB).

The product's only data source is Remastered (ADR 0013). Its storage holds the same 28 as WebM: `SD/smk/*.webm` (VP9 profile 1, 4:4:4 `gbrp`, 320 × 308, 15 fps, Vorbis 22.05 kHz stereo, 198 MB) and `HD2/Smk/*.webm` (VP9 profile 0, 4:2:0, 960 × 540, 15 fps, Vorbis 48 kHz stereo, 329 MB). The SD frames are stored half as wide as they are shown: the content of HD2 (960 × 458 of 960 × 540) is 1.5 × 640 × 308, so an SD frame is shown 640 × 308, a pixel of 2:1.

A palette movie format like KPV1 (portraits, glue movies) cannot carry them: full colour, 2000 to 3600 frames per movie, one soundtrack. The runtime has no video decoder, and ADR 0012 and 0013 say it should not gain one.

## Decision

1. **Decoder: libvpx 1.17.0, VP9 only, as an optional build dependency.**
   - `-DKP_CINEMATICS=ON` fetches the pinned source (`tools/toolchain/fetch.sh vpx`, SHA-256 `1020f184046187baa2985dbde38e0691f49c44088bca7a1842b0236c6081dc0a` of the GitHub tag archive) and links it statically. The default is OFF: no library, no module, no file in the bundle, the behaviour of today. ADR 0012 item 5 and ADR 0013's line hold for the default product.
   - Built without threads (`--disable-multithread --disable-vp8 --disable-vp9-encoder --disable-vp9-highbitdepth`; on arm64 and in C without an assembler or runtime CPU detection, `--disable-runtime-cpu-detect`), its sources listed in a CMake file of ours (`third_party/vpx/kp_vpx.cmake`, C11: libvpx 1.17 uses `static_assert`), its few generated headers kept beside it (`third_party/vpx/generated/`, `regenerate.sh`). On arm64 (macOS, Android, Linux) with libvpx's NEON intrinsics (`arm64-darwin24-gcc`, `generated/arm64/`; NEON is part of every AArch64 CPU, so it needs no runtime check; dotprod, i8mm and SVE would, and are off). On x86-64 (Windows, Linux, an Intel Mac) with SSE2..AVX2 when NASM 2.14 or later is on the PATH at the configure (`x86_64-darwin24-gcc` with runtime CPU detection, `generated/x86_64/`: libvpx's cpuid picks each function's best at the start; a part of them is in libvpx's `.asm` only, assembled by CMake's `ASM_NASM`; 2026-11-04), else from C. Elsewhere from C (`generic-gnu`, `generated/generic/`). 360 KB of static archive. BSD-3 with the WebM patent grant.
2. **The movies as Remastered ships them.** The converter copies the VP9 packets (`ffmpeg -c:v copy -f ivf`) and the Vorbis stream (`-c:a copy -f ogg`) into a pack of our own (`CINE`, `docs/formats/packs.md`), with a frame table. No second lossy pass, no Matroska parser in C. The audio plays through the SDL3_mixer already built (Ogg Vorbis through stb_vorbis) and also keeps the movie's clock.
3. **The quality is chosen apart from the art.** `generated/video/cinematics-sd.kpd` and `cinematics-hd2.kpd` are independent packs; `tools/product.sh` takes `KOPRULU_CINEMATICS=auto|none|sd|hd2|both` (auto, the default, takes the HD2 pack, else SD's, else none) whatever HD art it stages; the runtime flag `--cinematics auto|sd|hd2|off`.
4. **Targets:** macOS, Windows (mingw cross), Linux and Android. iOS and the web stay without the library (the configuration refuses `KP_CINEMATICS=ON` there): the web needs byte-range streaming and a wasm build of the library, iOS a check of the Xcode build, each a stage of its own.

## Why this decoder

Measured in C0 (libvpx 1.17.0, `generic-gnu`, `-O2`, one thread, the intro `starintr`, 2082 frames):

| | macOS arm64 (M-series) | Galaxy Z Fold7 (arm64, NDK 29) |
|---|---|---|
| SD 320 × 308 4:4:4 | 0.97 ms a frame (max 6.1) | 1.7 ms (max 11; the phone was dozing) |
| HD2 960 × 540 4:2:0 | 3.5 ms (max 9.6) | 5.2 ms (the 255 ms maximum is the dozing CPU) |

An SD frame is due every 66.7 ms (15 fps), an HD2 frame every 33.4 ms (29.97 fps); what counts is the main thread's frame of 16.7 ms at 60 Hz, since the decoder runs in the main thread (the web build has no threads, ADR 0013 R4b). With NEON (10H, after the second review) the product's `--cine-check` over all 28 movies of each pack (mean and longest `kp_cine_advance` a frame: the pack's reads and the decoding; the Mac's release build, the Fold7's debug APK, `RelWithDebInfo`):

| | C (`generic-gnu`) | NEON | the planes |
|---|---|---|---|
| macOS M-series, SD (30,211 frames) | 1.08 ms (max 10.1) | 0.59 ms (max 8.9) | the same CRC-32 |
| macOS M-series, HD2 (31,156 frames) | 2.65 ms (max 13.2) | 1.19 ms (max 12.7) | the same CRC-32 |
| Fold7, SD | 1.04 ms (max 7.8) | 0.56 ms (max 7.7) | the same CRC-32 |
| Fold7, HD2 | 2.78 ms (max 11.7) | 1.04 ms (max 10.4) | the same CRC-32 |

(the intro alone, HD2: 3.56 → 1.96 ms on the Mac, 3.59 → 1.71 ms on the Fold7). NEON's output is libvpx's C output bit for bit (the CRC-32 of every picture's planes of each movie, the same on both machines), so FFmpeg's bit-exactness below still holds (`test_cine.ycc_movie_planes_are_ffmpegs` checks the 4:2:0 planes against FFmpeg's byte for byte, in whichever build runs it). No decoder thread: an HD2 frame takes a sixteenth of the 60 Hz frame on the phone, and the longest call (which frames they are, and how much of them is the pack's reads, was not measured) still fits in it. The output of both movies is **bit-identical** to FFmpeg's decode of the same files (`cmp` of the whole raw output: 615,605,760 bytes of `gbrp` and 1,618,185,600 of `yuv420p`), so the pack's frames can be checked against `ffmpeg` in a test.

Colour as libvpx reports it: the SD movies are `VPX_IMG_FMT_I444`, `cs = VPX_CS_SRGB`, full range, planes in G, B, R order (a direct copy to RGBA); the HD2 movies are `VPX_IMG_FMT_I420`, `cs = unknown`, studio range. C3 chose BT.601 for a picture whose matrix is unspecified (FFmpeg's own guess; BT.709 only when the stream says so): on the test movie the player's frames differ from FFmpeg's `rgba` by 1 at most (`tests/c/test_cine.c`), the chroma copied where FFmpeg filters it. In play the planes go to the GPU and its shader converts them with the same matrix (`kp_gfx_video`, the chroma sampled between its texels); the CPU's conversion stays as the checks' reference (`--cine-check` compares the two).

## Rejected

- **dav1d (AV1) with a transcode.** The source is already VP9: a second lossy pass for no gain, and dav1d builds only with meson, which nothing in the project's build uses.
- **The original SMK files.** The decoder (`kp_smk.c`) is video-only and palette-bound, the files come from the 1.17 archives (610 MB, not Remastered), and ADR 0013 made Remastered the data source.
- **A VP9 decoder of our own.** A large specification and a bit-exactness check against libvpx for a movie format that libvpx decodes in 3.5 ms.
- **FFmpeg or the platforms' decoders** (VideoToolbox, MediaFoundation, MediaCodec, WebCodecs). A different one on each target, most with no VP9 profile 1, and FFmpeg's size and licence for one codec.
- **An MPEG-1 single-header decoder (pl_mpeg).** Builds anywhere but looks far worse than the source.

## Consequences

- A build with cinematics has one third-party library more; the default build has none. `tools/ci.sh` checks both.
- The packs are large (198 MB SD, 329 MB HD2). `tools/product.sh` stages the best pack converted (`KOPRULU_CINEMATICS=auto`, its default: HD2, else SD, else none; none for the web and iOS), so a bundle of converted data carries the movies and, on Android, the APK grows by the pack; `KOPRULU_CINEMATICS=none` leaves them out. The plain CMake build has no player (`KP_CINEMATICS` off).
- Remastered's subtitle files (`locales/<locale>/Assets/Smk/*.srt`, only the install's own locale downloaded) go into the packs' `SUBS` (schema 2, 2026-11-04) and are drawn over the movies in `ui.kpd`'s TrueType text font (`docs/phase10.md`).
- The libvpx archive pinned is GitHub's tag archive, whose bytes GitHub does not promise to keep: a changed checksum stops the fetch, and the source is then taken from the library's own releases page.
