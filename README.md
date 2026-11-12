# Koprulu Sector

A from-scratch reimplementation of the single-player game of **StarCraft + Brood War 1.17**, written in C over SDL3.
It plays the six original campaigns (Terran, Zerg, Protoss and the three Brood War ones, 64 missions) from the first
mission to the epilog, on your own copy of the original data.

- **Faithful to 1.17.0.1.** The simulation, triggers, AI scripts, fog of war and interface follow the original
  executable. They are checked against the original game, which runs in an emulator, step by step. See
  [docs/implementation-status.md](docs/implementation-status.md).
- **The cinematics** from either install: played as they are when the game runs from an install (the 1.17 Smacker
  movies, Remastered's WebM with its subtitles), or converted into the video player's packs for the other builds.
- **Optional StarCraft: Remastered content.** A Remastered install can be the only data source. It also gives the
  HD and HD2 art, lighting, smooth fog, animated water, the cinematics with subtitles, and the deDE, frFR, plPL and
  ruRU text and speech.
- **Every platform:** macOS, Windows, Linux, Android, iOS and the web (WebAssembly). Phones have touch controls and
  zoom.

Not included: multiplayer, Battle.net, replays and custom (UMS) maps.

> **No game data is included.** This repository holds only code and its own definitions. You need a legal copy of
> StarCraft: Brood War 1.17.0.1 or StarCraft: Remastered. The converter turns it into the game's data on your
> machine. Builds contain that data, so they are for your own use and must not be shared.

## Requirements

| What | For |
|---|---|
| Python 3.10+ with [Pillow](https://pypi.org/project/Pillow/) | the converter's steps not yet in `kp_convert`: the console, the HD art, the cinematics and the languages' text (`python3 -m pip install Pillow`; [ADR 0019](docs/adr/0019-install-or-repack-converter-in-c.md)) |
| [StormLib](https://github.com/ladislav-zezula/StormLib) (`brew install stormlib`, Debian/Ubuntu `libstorm-dev`) | the Python converter's reading of the 1.17 install's MPQ archives (`cinematics --data`; Remastered's `--scr` does without it). `kp_convert` and the game build their own from the sources `tools/toolchain/fetch.sh` downloads. `KOPRULU_STORMLIB` names the library when the loader does not find it (on Windows `StormLib.dll` or `storm.dll`) |
| GNU make (optional) | the shortcuts of the [Makefile](#quick-start-with-make) |
| FFmpeg (`ffmpeg`, `ffprobe` on the `PATH`) | the portraits (`data` decodes their movies, from either install) and the cinematics; the 1.17 cinematics need its `libvpx-vp9` encoder |
| `oggenc` (vorbis-tools) | the 1.17 cinematics' sound (Ogg Vorbis); the repack's sounds, music and speech are encoded by `kp_convert` itself |
| CMake 3.20+, Ninja, a C compiler (Clang or GCC) | every build |
| curl, `patch` | `tools/toolchain/fetch.sh` downloads the pinned SDL 3.4 sources, the readers of the original data (StormLib, CascLib, zlib, bzip2), the video decoders (libvpx, nestegg) and the Vorbis encoder of the tools (libogg, libvorbis) and checks them by SHA-256 |
| Xcode | macOS and iOS |
| Emscripten (`emcmake` on the `PATH`) | the web |
| Android SDK, NDK 29, a JDK | Android (`ANDROID_HOME`, default `~/Library/Android/sdk`) |
| mingw-w64 (`x86_64-w64-mingw32-gcc`) | Windows, cross-compiled from macOS or Linux |
| Docker | Linux (the build runs in the container of `tools/native/linux.Dockerfile`) |
| NASM 2.14+ (optional) | faster cinematics decoding on x86-64 |

## Quick start with make

The [Makefile](Makefile) chains the steps below. Run `make` alone to see every target.

```bash
make convert SC=/path/to/StarCraft          # from a 1.17.0.1 folder, with the repack and its cinematics
make convert-remastered SCR=/path/to/StarCraft   # from Remastered, with HD, cinematics and languages
make macos                                   # or windows, linux, web, android, ios, ios-sim
make web serve                               # build the web version and serve it on http://localhost:8793/
make run ARGS="--graphics hd2"               # a development build, started from the repository root
```

Settings are passed as variables: `HD=hd2|hd|all`, `CINE=sd|hd2|all` (Remastered's sets; the 1.17 install has
only `sd`; `none` converts no cinematics), `LOCALE=all|ruRU|...`, `VARIANT=release|debug`, `OUT=<folder>` (default
`generated`). The sections below show what each target runs.

## 1. Convert the data

Run every command from the repository root. The output goes to `generated/`. Use `--out DIR` to write somewhere
else, and then pass `KOPRULU_GENERATED=DIR` to the builds.

`kp_convert` is the converter in C ([ADR 0019](docs/adr/0019-install-or-repack-converter-in-c.md)): it checks an
install, writes the repack, the test scenarios, the terrain checks and the icon, on macOS, Linux and Windows alike.
The bundles do not carry it: a bundle is its output, the repack. Build it once:

```bash
tools/toolchain/fetch.sh
cmake -S . -B build/cmake -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/cmake --target kp_convert
```

`kp_convert.exe` for Windows is built with mingw-w64 on macOS or Linux:

```bash
cmake -S . -B build/convert-windows -G Ninja -DCMAKE_BUILD_TYPE=Release -DKP_TOOLS=OFF -DKP_ENCODERS=ON \
  -DCMAKE_TOOLCHAIN_FILE=tools/toolchain/cross-windows.cmake && cmake --build build/convert-windows --target kp_convert
```

The rest (the console, the HD art, the cinematics, the languages' text) is still `python3 -m converter`, until
`kp_convert` takes it over ([phase C](docs/phaseC.md)).

### From StarCraft: Brood War 1.17.0.1

The install folder must hold `StarCraft.exe`, `StarDat.mpq`, `BrooDat.mpq`, `Patch_rt.mpq`, `StarCraft.mpq` and
`BroodWar.mpq`. These are the CD's archives copied to disk. The archives are checked by the files the conversion
reads, not as whole files: copies from other discs or installers are packed differently, so their sizes and
hashes may differ from the reference copies in [defs/profile/bw-1.17.0.1.json](defs/profile/bw-1.17.0.1.json).
Each file must match the SHA-256 in [defs/scr/inputs-bw-1.17.0.1.json](defs/scr/inputs-bw-1.17.0.1.json).

```bash
SC=/path/to/StarCraft
build/cmake/kp_convert verify    "$SC"        # checks StarCraft.exe and every file the game reads
build/cmake/kp_convert scenarios "$SC"        # the test scenarios' CHKs: the tests and a debug bundle
build/cmake/kp_convert terrain   "$SC"        # the terrain checks' CRCs: koprulu --autotest
build/cmake/kp_convert icon      "$SC"        # optional: the game's icon for the bundles
python3 -m converter cinematics --data "$SC"  # optional: the cinematics
```

The game itself reads the original files, from the install or from the repack below. The cinematics are the original
Smacker movies converted to the video player's format: VP9 and Ogg Vorbis, at the
size and quality of Remastered's SD set, without subtitles. Each movie is checked against the SHA-256 in
[defs/campaign/cinematics.json](defs/campaign/cinematics.json). The conversion takes a few minutes; its results are
kept in `generated/cache/cinematics/`, so a second run is quick. `make convert SC=…` adds `--optional`: without
FFmpeg's `libvpx-vp9` encoder or `oggenc` it warns and goes on without the cinematics. Where Remastered's SD set was
already converted into the folder, it is kept: it is the newer copy of the same movies.

### From StarCraft: Remastered

Point `--scr` to the Remastered install, the folder that holds `.build.info` and `Data/`. Each file is checked
against the 1.17 manifest in `defs/scr/`. The few files Remastered changed are rebuilt or approximated: the game
logs them when it reads the install, and the repack's listing records each file's status.

```bash
SCR="/Applications/StarCraft"   # or C:\Program Files (x86)\StarCraft
python3 -m converter console --scr "$SCR"     # the widescreen console's pieces
build/cmake/kp_convert scenarios "$SCR"
build/cmake/kp_convert terrain   "$SCR"
```

These steps are optional. Each needs a Remastered install, and each can follow a conversion from either source
(Remastered's SD set wins over the 1.17 one in the same folder, whichever was converted first):

```bash
# HD art (hd2 = x2, hd = x4, or all). It also writes the widescreen console's pieces.
python3 -m converter hd --scr "$SCR" --variant hd2

# Cinematics and their subtitles (sd, hd2 or all)
python3 -m converter cinematics --scr "$SCR" --variant hd2

# Languages: the text needs the font tool built first (see "Desktop build" below)
cmake -S . -B build/cmake -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/cmake --target kp_fnt_make
python3 -m converter locale --scr "$SCR" --locale all    # or ruRU, deDE, frFR, plPL
```

The languages' speech needs no conversion: the game plays it from the install, or from the repack's language groups
below.

### The repack

The game runs from the original files themselves ([ADR 0018](docs/adr/0018-original-data-at-run-time.md)):
`--data` can name the install. `kp_convert repack` copies the files the game reads, in their own formats, into
`koprulu*.mpq` archives, with a listing of each file's SHA-256 that the game checks. A WAV goes in as Ogg Vorbis when
that is smaller (libvorbis, on the processor's cores; each kept in `generated/cache/repack-vorbis/`; `--wav` keeps the
WAVs). The Ogg files' bytes can differ between processors, never what they decode to within the encoder's rounding. The repack has priority over an install in `--data`. The bundles
carry the repack: a computer's the one file of `--single`, a phone's and the web's the groups (`make repack` writes
both). From Remastered each language the install holds gets a group of its own, `koprulu-locale-<loc>.mpq`, even with
`--single`: its maps, spoken lines and unit voices, a sound in Ogg Vorbis at 22,050 Hz mono when that is smaller
(`--locales "ruRU deDE"` or `none` picks them). No converted SD data remains: the languages' packs compute their base
from the install they are converted from.

```bash
build/cmake/kp_convert repack "$SC"             # generated/repack: koprulu-core.mpq, a group per mission, the music, the cinematics
build/cmake/kp_convert repack "$SC" --single    # generated/repack-single: one koprulu.mpq, the cinematics apart
build/cmake/kp_convert repack "$SCR" --single   # from Remastered (its rebuilt 1.17 files; its cinematics stay in the install)
```

You need to convert only once. Run the steps again after you update the repository, because the formats can change
between versions and the game refuses data of an older schema.

## 2. Build and run

`tools/product.sh <platform> [release|debug]` builds a complete bundle: the repack (`koprulu.mpq` on a computer, the
groups on a phone and the web, which fetches a mission's group before the mission starts) and what was converted
beside it (the console's pieces, the HD art, the cinematics, the languages with their repack groups). The result goes to
`dist/product/<platform>/`.

```bash
tools/product.sh macos      # dist/product/macos/KopruluSector.app
tools/product.sh windows    # dist/product/windows/koprulu.exe + data/
tools/product.sh linux      # dist/product/linux/ (built in Docker)
tools/product.sh web        # dist/product/web/index.html
tools/product.sh android    # dist/product/android/KopruluSector.apk
tools/product.sh ios        # dist/product/ios/koprulu.app (unsigned)
tools/product.sh ios-sim    # dist/product/ios-sim/koprulu.app for the iOS Simulator
```

What goes into a bundle is chosen by environment variables. Each defaults to everything that was converted.

| Variable | Values | Meaning |
|---|---|---|
| `KOPRULU_CINEMATICS` | `auto`, `none`, `sd`, `hd2`, `both` | the cinematics packs and the video player (libvpx is downloaded and built) |
| `KOPRULU_LOCALES` | `auto`, `none`, `"ruRU deDE"` | the languages, with their speech where the repack has their group (not on the web yet) |
| `KOPRULU_PHONE_BLOCKS` | `astc`, `etc2` | the texture format of a phone's HD art. ASTC is the default. Use ETC2 for devices without ASTC, which otherwise draw the original art |
| `KOPRULU_GENERATED` | a folder | the converter's output, if it is not `generated/` |
| `KOPRULU_DIST` | a folder | where the bundles go instead of `dist/product/`, such as a second bundle of another conversion |

### Platform notes

- **macOS:** open `KopruluSector.app`.
- **Windows:** copy `koprulu.exe` together with its `data` folder and run the exe. It is a static build, so no DLLs
  are needed.
- **Linux:** run `koprulu` from `dist/product/linux/`. The folder also holds a `.desktop` entry and an icon if you want
  to install it.
- **Web:** the page needs a server that answers HTTP Range requests, because the HD art and the cinematics are read
  in pieces. `python3 -m http.server` does not do this. Use:

  ```bash
  python3 tools/web_serve.py --dir dist/product/web --port 8793
  ```

  Then open `http://localhost:8793/`.
- **Android:** install the APK with `adb install -r dist/product/android/KopruluSector.apk`. The data is packed inside the APK.
- **iOS:** the build is unsigned. Sign it in Xcode (`build/app-ios`) to install it on a device, or run the `ios-sim`
  build in the Simulator.

### Desktop build for development

```bash
tools/toolchain/fetch.sh
cmake -S . -B build/app -G Ninja -DCMAKE_BUILD_TYPE=Release -DKP_APP=ON -DKP_TOOLS=OFF
cmake --build build/app --target koprulu
build/app/koprulu            # from the repository root it finds generated/ by itself
```

Every build has the cinematics player and the WebM decoder (libvpx and nestegg, fetched by `tools/toolchain/fetch.sh`; ADR 0015, ADR 0018).

## Command-line options

The game is played from its menus, and the in-game Video Options hold the graphics settings. These options override
the settings for one run:

| Option | Meaning |
|---|---|
| `--data DIR` | the data folder (default: the current folder when it holds `generated/`, else `data/` beside the executable; with no source named, its `generated/repack-single`, else `generated/repack`, is the original data); any number of times: a folder with a 1.17 install, StarCraft: Remastered's install or a repack (`koprulu.mpq`, or `koprulu-core.mpq` and its groups: `kp_convert repack`) is the original data, read and checked at run time ([ADR 0018](docs/adr/0018-original-data-at-run-time.md)): a 1.17 install or StarCraft: Remastered's runs the game, its interface, sounds and music with no conversion (Remastered's files that only its own formats hold, DDS, XML and WebM, rebuilt when read; its HD portraits read from it too; the cinematics played from it as they are, the 1.17 Smacker files or Remastered's WebM with the subtitles of its language; the languages' speech played from it or from the repack's language groups; the rest of the HD art and the languages' text still come from `generated/` when it is there) |
| `--res-log PATH` | the original files read, their status and source, written when the program ends |
| `--graphics original\|hd2\|hd` | the art (HD needs `converter hd`) |
| `--lighting on\|off` | the HD sprites' lighting |
| `--fog original\|smooth\|off` | the fog of war's look |
| `--water on\|off` | animated water |
| `--font original\|vector` | the original bitmap text or smooth vector text |
| `--locale LOC`, `--speech LOC` | the language of the text and of the speech (also in the main menu's Language) |
| `--window WxH` | the window's size |
| `--scale-mode integer\|sharp` | how the picture is scaled to the window |
| `--console edges\|ornaments` | the console's shape on wide screens |
| `--no-vsync` | turns vertical sync off |

Your profile, saves and settings are kept in the system's per-user application folder.

## Status

All 64 campaign missions can be played and won. Each one's state matches the original game, which runs in an
emulator. The details are in [docs/implementation-status.md](docs/implementation-status.md), the decisions are in
[docs/adr/](docs/adr/) and the reverse-engineering notes are in [docs/research/](docs/research/).

Known gaps:

- Only the 1.17.0.1 data (or Remastered presented as 1.17) is supported, not other versions.
- The Remastered lighting is an approximation, because Remastered's code is encrypted.

## Legal

StarCraft and StarCraft: Brood War are trademarks of Blizzard Entertainment. This is an unofficial fan project and is
not affiliated with or endorsed by Blizzard. No original game data, art, sound or code is part of this repository.

The project's own code and documentation are under the [MIT License](LICENSE); third-party components keep their own licenses (see [third_party/README.md](third_party/README.md)). The golden oracle data (`tests/golden/`) is recorded from the original game and is not published, so it is not part of this repository.
