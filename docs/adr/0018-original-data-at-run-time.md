# ADR 0018: The game reads the original data at run time

Status: accepted (phase D, 2026-11-10; the user approved the plan and its choices). It replaces the plan's rule "no MPQ reading and no parsing of the original formats at run time" (the implementation plan's "Consequences", kept outside the repository) and changes the converter's role ([ADR 0004](0004-resource-packs.md), [ADR 0013](0013-remastered-data-source-and-hd.md)). Stages and their state: [docs/phaseD.md](../phaseD.md).

## Context

The offline conversion into `generated/*.kpd`, `*.kpm` and packs came from Defold: bob built the atlases, the engine's resource counts were limits, Lua could not afford parsing (ADR 0003, 0004). Since ADR 0012 the runtime is C over SDL3 and none of that holds.

A large part of the containers is the original files unchanged: the DAT tables, `iscript.bin`, `aiscript.bin` and `bwscript.bin`, CV5, VF4 and WPE, the LO files, the dialog GRPs, PCX and SMK files, the CHK chunks. The rest is deterministic and can be built in C: the original itself builds the walk raster, the regions and the contours when a map loads (`0x490d80`). A user needs Python, Pillow, StormLib through ctypes, FFmpeg and oggenc today, and a build carries the data, so it cannot be shared.

## Decision

1. **The game reads an install directly**, into its own structures in memory, with no KPF sections between and no cache on disk:
   - a 1.17.0.1 install through **StormLib 9.40** (its MPQ archives in the profile's order, `defs/profile/bw-1.17.0.1.json`);
   - a StarCraft: Remastered install through **CascLib 3.0** (the converter's `ScrSource` rules: prefixes, moved files, `.vx4` from `.vx4ex`, the tilesets' 1.17 prefix, Remastered's rebuilt files computed when read).
   Both are MIT; they share one **zlib 1.3.2** and one **bzip2 1.0.8** instead of their bundled 1.2.5 / 1.2.11 and 1.0.5, and StormLib's Jenkins hash (`third_party/data/kp_data.cmake`). `tools/toolchain/fetch.sh` fetches them by default (`data`), pinned by SHA-256.
2. **Every file read from an install is checked** against the input manifest (`defs/scr/inputs-<profile>.json`: size and SHA-256, `kp_sha256.c`): a 1.17 file that differs stops the game with its name; a Remastered file keeps the converter's statuses (exact, derived, differs, approximation). Nothing is cached: the check costs less than the read (below).
3. **A repack has priority over the install.** The converter writes `koprulu*.mpq` (StormLib), the files the game reads in their original formats plus Remastered's rebuilt 1.17 files, with a `(koprulu)` listing that names the profile and each file's size and SHA-256 (stage D1: SHA-256 rather than `(attributes)`' MD5, which StormLib checks by reading a file a second time). A file that differs from its listing, or a repack of another profile, falls back to the install. The repack is split into groups for the web (core, a pack per race and the neutral and effect art, a mission's chunks and speech, music, cinematics), one file (`--single`) for desktops. **Sounds may be stored as Ogg Vorbis** when that is smaller than the WAV (the user's rule, 2026-11-10); the runtime tells them apart by their bytes (`RIFF`, `OggS`).
4. **Videos without conversion:** the Smacker movies through `kp_smk.c` with an audio decoder of our own (libsmacker is LGPL-2.1: no copyleft dependency); Remastered's WebM played as is (nestegg and libvorbis, both permissive, with the libvpx already built), its palette movies mapped to the screen's palette as KPV1 was. KPV1 goes away.
5. **The converter stays** for the repack and what a platform needs built for it: the HD pages (BC, ETC2, ASTC), the phones' and the web's bundles.
6. **Old saves are refused.** Only their data check breaks: it compares CRC-32s of the KPF files (`kp_host.c`, `kp_session.c`). The new identity is the profile and the CRC-32s of the original inputs the simulation reads, so a save carries over between a 1.17 install, a Remastered one and a repack.

## Measurements (stage D0)

`kp_data_probe` reads every file of the input manifest (4,384) from an install and checks it; macOS on an M-series Mac, one thread, release build:

| Source | Kind | Files | Equal to the manifest | MB | Read | SHA-256 |
|---|---|---:|---:|---:|---:|---:|
| 1.17 MPQ | core (everything but the two below) | 3,259 | 3,259 | 155.5 | 1.07 s | 0.39 s |
| 1.17 MPQ | missions' speech | 1,105 | 1,105 | 415.2 | 4.17 s | 1.09 s |
| 1.17 MPQ | music | 20 | 20 | 340.8 | 3.85 s | 0.85 s |
| Remastered CASC | core | 3,259 | 2,380 | 149.7 | 0.66 s | 0.34 s |
| Remastered CASC | missions' speech | 1,105 | 451 | 415.3 | 1.64 s | 0.94 s |
| Remastered CASC | music | 20 | 0 | 279.9 | 0.90 s | 0.66 s |

- The CASC counts are the copies under the converter's prefixes with the manifest's digest (its "exact"); the others are moved, rebuilt or Remastered's own (stages D1 and D6).
- A spike read the 4,286 CASC names the converter reads through CascLib and through `converter/casc/storage.py`: all equal byte for byte. CascLib opens the storage in 0.04 s.
- In wasm under node: MPQ core 1.21 s, CASC core 0.80 s. On Linux (GCC 15.2, Docker on the same Mac) the MPQ read passes; SHA-256 runs at about 185 MB/s there against about 400 MB/s with clang.
- The game reads the core in parts (its start, then a mission's packs), so these are bounds.
- The 1.17 WAVs (2,248) are all PCM, 16 bits: 22,050 Hz mono 2,189, stereo 30; 44,100 Hz mono 21, stereo 1; 11,025 Hz mono 7. SDL3_mixer's WAVE decoder plays them as they are.
- A repack in the original formats, without the music, compressed file by file: 598.5 MB raw, 512.7 MB with zlib, 476.5 MB with bzip2. The WAVs hardly shrink (the missions' speech 435.4 to 359.3 MB, the sounds 95.5 to 84.1 MB), hence item 3's Ogg Vorbis.
- Built: macOS (clang), Linux (GCC 15.2), Windows (mingw-w64, linked), Android (NDK 29, API 24, linked), iOS (arm64, linked), wasm (run under node).
- Not measured yet: a mission's GRP decoding and page packing in C (stage D5), a WebM portrait's decoding and palette mapping (stage D6).

## Consequences

- CascLib.h includes C++ headers: C reaches it through `native/kp_app/kp_casc.cpp` (`kp_casc.h`). StormLib.h includes `windows.h` on Windows, whose macros rename fields of the project's headers (`near`): it is included last, and stage D1 keeps it in files of its own.
- StormLib and CascLib handles are not shared between threads: the music, the movies and the HD page thread open their own.
- The CI step "data readers" runs `kp_data_probe` with `KOPRULU_ORIGINAL` and `KOPRULU_SCR`.
