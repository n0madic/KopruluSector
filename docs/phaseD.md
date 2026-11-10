# The original data at run time

Design: [ADR 0018](adr/0018-original-data-at-run-time.md). The plan was approved on 2026-11-10: StormLib and CascLib, structures built in memory with no KPF between, no cache on disk, a `koprulu*.mpq` repack before the install, Remastered's WebM played as is, Smacker audio of our own, old saves refused, sounds in the repack as Ogg Vorbis where that is smaller.

| Stage | What | State |
|---|---|---|
| D0 | ADR 0018; StormLib, CascLib, zlib, bzip2 fetched and built on every platform; `kp_sha256`; `kp_data_probe` and its measurements | done 2026-11-10 |
| D1 | `kp_res`: the MPQ backends (install, repack) and CASC, the repack's priority, the check against the manifest, `--data`, which source a folder is, `--res-log` | done 2026-11-11 |
| D2 | `defs/` as C tables (`tools/defs_c.py`, `--check` in CI): the DAT layouts, the requirements, the interface's and the campaign's tables, the CHK loader's, `SLOT`, the packs, the manifest, `MOVED` | |
| D3 | The game data from the original files: DAT, iscript, AI scripts, strings, palettes, fonts, tilesets, GRP geometry and opacity, the interface, sounds, portraits, campaign | |
| D4 | Missions from CHK, the walk raster, regions and contours in C | |
| D5 | The pages built from GRPs when a mission starts, WAV sounds, music streamed from the archives | |
| D6 | Remastered with no converter: the rebuilt files in C, WebM palette movies, HD portraits | |
| D7 | Smacker audio, the 1.17 cinematics from the archives, Remastered's WebM cinematics | |
| D8 | `converter repack`, the bundles, the web's groups; the SD containers and their loaders removed | |

## D1 as built

- **`native/kp_app/kp_res.{h,c}`** (in `kp_app_core`): `kp_res_open(dirs)` takes the folders the player names; `kp_res_detect` tells a repack (`koprulu.mpq`, or `koprulu-core.mpq` and the groups its listing names), a 1.17 install (the profile's executable or first archive; a name as given, in lower or in upper case) and a Remastered install (`.build.info`). One install (the first; another is logged and ignored), any number of repack archives (`kp_res_add_repack`: the web adds a group once fetched).
- **Reads** (`kp_res_read`, any case, `\\` or `/`): the manifest's unreadable and absent names first (nothing read); then a repack's copy, equal to its listing, else the install's:
  - 1.17: the first copy in the profile's order, refused (`KP_RES_REFUSED`, the archive, the name, both sizes and digests in the error) when it differs from the manifest; a name the manifest does not hold is read unchecked and logged; a missing campaign archive (the CDs': music, movies, speech) leaves its files missing, a missing core archive refuses the install;
  - Remastered: `ScrSource`'s order and statuses in C: the moved names, the four prefixes, `.vx4ex`; `exact`, `derived` (a grown tileset's 1.17 prefix with the CV5 groups Remastered filled zeroed, `.vx4` from `.vx4ex`: `kp_res_casc_rebuild`), `differs` (the first readable copy), `unlisted`. The rebuilds from DDS, `dds.grp`, XML and WebM are stage D6's (`missing` until then).
- **Streams** (`kp_res_stream_open`, `_read`, `_close`): handles of their own for another thread; an install's file has the manifest's size and its sectors' CRCs checked as they are read (StormLib's `MPQ_OPEN_CHECK_SECTOR_CRC`).
- **The repack's listing** `(koprulu)`: `koprulu repack 1`, `profile <id>`, `group <file>` lines, then `<sha256> <size> <name>` a file. A copy that differs from its listing, or a repack of another profile, falls back to the install (logged).
- **`native/kp_app/kp_mpq.{h,c}`**: StormLib alone in a file (its `windows.h`); read, read in pieces, and write (`kp_mpq_create`, `kp_mpq_add`: the tests' archives, the repack of D8). `kp_casc` reads in pieces too.
- **`tools/defs_c.py`** writes `native/kp_core/src/defs/` (in `kp_core`): the profile's archives and executable, the input manifest's files (normalized names, sorted; the 16 spellings that normalize alike name the same bytes and are kept once), its absent and unreadable names; `--check` in CI. `kp_defs_input`, `kp_defs_listed` look them up.
- **The app**: `--data DIR` any number of times: a folder with `generated/` is the converted data's root as before, one with a repack or an install goes to `kp_res` (a bundle's `data/` may be both); `--res-log PATH` writes the list of the files read (`name<TAB>status<TAB>source`, by name) when the program ends, and the counts by status go to the log. Nothing in the game reads through `kp_res` yet (stage D3).
- **`kp_data_probe DIR... [--res-log PATH]`** reads every manifest file through `kp_res` and counts each kind's statuses (the CI step "data readers"): the 1.17 install 4,368 files exact (the core's 155.4 MB in 1.5 s with the checks); Remastered 2,380 exact, 29 derived, 11 differ and 823 missing in the core (the converter's statuses for every file both read: its exact, derived and differs are the same names from the same CASC copies; the 823 are its rebuilds), 451 exact and 654 differing of the missions' speech, 20 differing music tracks.
- **`test_res`**: archives the tests write with StormLib: a 1.17 install of a test profile (the first copy, a refused copy, unlisted, absent, unreadable, missing; a missing campaign archive, a missing core archive, a folder with no data), repacks (before the install whatever the folders' order, a broken copy back to the install, another profile's ignored, groups joining the core), streams, the log, the tileset rebuilds; with `KP_TEST_ORIGINAL` and `KP_TEST_SCR` (`tools/ci.sh` passes `KOPRULU_ORIGINAL`, `KOPRULU_SCR`) the real installs: `units.dat`, a music track in pieces, `.vx4` from `.vx4ex`, `desert.cv5`'s prefix, a moved font.

## D0 as built

- **`tools/toolchain/fetch.sh data`**, fetched by default: StormLib 9.40, CascLib 3.0, zlib 1.3.2, bzip2 1.0.8, each pinned by SHA-256.
- **`third_party/data/kp_data.cmake`:** `kp_zlib`, `kp_bzip2`, `kp_jenkins` (StormLib's lookup3), `kp_storm`, `kp_casc`, with the libraries' own source lists at these versions; their bundled zlib and bzip2 left out. CascLib 3.0's CMakeLists.txt asks for CMake 3.2, which CMake 4 refuses: our list avoids it.
- **`native/kp_app/kp_casc.{h,cpp}`** (`kp_casc_c`): CascLib for C (open, has, read).
- **`native/kp_core/src/core/kp_sha256.{h,c}`**, with `tests/c/test_res.c` (FIPS 180-4's examples, a million `a` in pieces of 1 to 130 bytes, the padding's edges at 55, 56 and 64 bytes).
- **`native/kp_core/tools/kp_data_probe.c`**: `mpq DIR` and `casc DIR`, every file of the input manifest read and checked, timed by kind; the CI step "data readers".
- The measurements are in ADR 0018.

### Differences from the plan

- D1: the profile and the input manifest became C tables in D1 (`tools/defs_c.py`), not D2: the check needs them. D2 adds the other tables to the same generator.
- D1: the repack's files are checked against SHA-256 digests in its own listing `(koprulu)`, not against StormLib's `(attributes)` MD5 (`SFileVerifyFile` reads a file a second time; the SHA-256 is already there for the installs). The archives are opened without loading `(attributes)` and the repack is written without one: StormLib reads and writes its 64-bit file times at unaligned addresses (`SFileAttributes.cpp`), undefined behaviour UBSan stops on, and nothing reads it. CascLib's sources are built under ASan only (`-fno-sanitize=undefined`): its code rests on null-pointer idioms UBSan stops on (`CascPort.h`'s `FIELD_OFFSET`, `Path.h`'s pointer arithmetic before its null check); the project's wrapper `kp_casc.cpp` keeps UBSan.

- nestegg, libogg and libvorbis are fetched with the stages that use them (D6, D7), not in D0: no library is built before its code.
- zlib and bzip2 are fetched too: the libraries' bundled copies are old (zlib 1.2.5 and 1.2.11, bzip2 1.0.5) and would be linked twice.
- The GRP pages' and the WebM portraits' timings move to D5 and D6, measured on their C code.
- Not run: CASC on Linux (Docker Desktop does not share `/Applications`), the Windows, Android and iOS builds (linked only).
