# The original data at run time

Design: [ADR 0018](adr/0018-original-data-at-run-time.md). The plan was approved on 2026-11-10: StormLib and CascLib, structures built in memory with no KPF between, no cache on disk, a `koprulu*.mpq` repack before the install, Remastered's WebM played as is, Smacker audio of our own, old saves refused, sounds in the repack as Ogg Vorbis where that is smaller.

| Stage | What | State |
|---|---|---|
| D0 | ADR 0018; StormLib, CascLib, zlib, bzip2 fetched and built on every platform; `kp_sha256`; `kp_data_probe` and its measurements | done 2026-11-10 |
| D1 | `kp_res`: the MPQ backends (install, repack) and CASC, the repack's priority, the check against the manifest, `--data`, which source a folder is, `--res-log` | |
| D2 | `defs/` as C tables (`tools/defs_c.py`, `--check` in CI): the DAT layouts, the requirements, the interface's and the campaign's tables, the CHK loader's, `SLOT`, the packs, the manifest, `MOVED` | |
| D3 | The game data from the original files: DAT, iscript, AI scripts, strings, palettes, fonts, tilesets, GRP geometry and opacity, the interface, sounds, portraits, campaign | |
| D4 | Missions from CHK, the walk raster, regions and contours in C | |
| D5 | The pages built from GRPs when a mission starts, WAV sounds, music streamed from the archives | |
| D6 | Remastered with no converter: the rebuilt files in C, WebM palette movies, HD portraits | |
| D7 | Smacker audio, the 1.17 cinematics from the archives, Remastered's WebM cinematics | |
| D8 | `converter repack`, the bundles, the web's groups; the SD containers and their loaders removed | |

## D0 as built

- **`tools/toolchain/fetch.sh data`**, fetched by default: StormLib 9.40, CascLib 3.0, zlib 1.3.2, bzip2 1.0.8, each pinned by SHA-256.
- **`third_party/data/kp_data.cmake`:** `kp_zlib`, `kp_bzip2`, `kp_jenkins` (StormLib's lookup3), `kp_storm`, `kp_casc`, with the libraries' own source lists at these versions; their bundled zlib and bzip2 left out. CascLib 3.0's CMakeLists.txt asks for CMake 3.2, which CMake 4 refuses: our list avoids it.
- **`native/kp_app/kp_casc.{h,cpp}`** (`kp_casc_c`): CascLib for C (open, has, read).
- **`native/kp_core/src/core/kp_sha256.{h,c}`**, with `tests/c/test_res.c` (FIPS 180-4's examples, a million `a` in pieces of 1 to 130 bytes, the padding's edges at 55, 56 and 64 bytes).
- **`native/kp_core/tools/kp_data_probe.c`**: `mpq DIR` and `casc DIR`, every file of the input manifest read and checked, timed by kind; the CI step "data readers".
- The measurements are in ADR 0018.

### Differences from the plan

- nestegg, libogg and libvorbis are fetched with the stages that use them (D6, D7), not in D0: no library is built before its code.
- zlib and bzip2 are fetched too: the libraries' bundled copies are old (zlib 1.2.5 and 1.2.11, bzip2 1.0.5) and would be linked twice.
- The GRP pages' and the WebM portraits' timings move to D5 and D6, measured on their C code.
- Not run: CASC on Linux (Docker Desktop does not share `/Applications`), the Windows, Android and iOS builds (linked only).
