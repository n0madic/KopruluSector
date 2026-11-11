#!/usr/bin/env bash
# The product's builds (plan phase S, stage S6; ADR 0012): kp_app over SDL 3.4 with the original data's repack.
#   tools/product.sh macos|web|android|ios|ios-sim|linux|windows [release|debug]
# The data staged for a bundle (ADR 0018, stage D8b): the repack of the original files (python3 -m converter repack):
# a computer's one koprulu.mpq (generated/repack-single, repack --single), a phone's and the web's groups
# (generated/repack, but its cinematics' group: the cinematics are the packs below), the web's core preloaded and
# the other groups beside the page, fetched when needed (a mission's before it starts, the music's from the start),
# Android's read in place from the APK (stored uncompressed: tools/native/android_app.sh's noCompress);
# the vector fonts (third_party/fonts as fonts/); the console's pieces (generated/data/console.kpd); the HD art where
# it was imported (generated/hd, not for the web; a phone's HD2 transcoded by kp_hd_phone: its pages in ASTC, or in ETC2 with
# KOPRULU_PHONE_BLOCKS=etc2 for phones without ASTC, which otherwise draw the original's art, and by default on ios-sim,
# whose context takes no ASTC; its tiles in ETC2);
# the cinematics (generated/video, python3 -m converter cinematics; ADR 0015: StarCraft: Remastered's, or the 1.17
# install's transcoded into the SD set) with
# KOPRULU_CINEMATICS=auto|sd|hd2|both|none: the packs staged and the player built in over libvpx (fetched, built from
# C), whatever HD art is staged (a game of HD art with SD movies, or the other way round); auto, the default, takes
# HD2's pack where it was converted, else SD's, else none; none builds none of it; the web fetches the pack beside the
# page as byte ranges (a server with Range requests: tools/web_serve.py), its head preloaded; a debug build adds the
# test scenarios, the terrain checks' CRCs and the scripts (tests/scripts/*.kps as generated/scripts/: the
# development checks, --autotest and --script; a phone reads them from its assets). The languages
# (python3 -m converter locale; ADR 0016) with KOPRULU_LOCALES=auto|none|<ids>: auto, the default, stages every
# language converted (generated/locale/<id>) and the glyphs' pack; none stages none, the game in its base language
# with the base's vector fonts; a list ("ruRU deDE") stages those, each of which must have been converted. The speech
# (python3 -m converter speech) with KOPRULU_SPEECH=auto|none|<ids> the same way (generated/speech/<id>, with the
# glyphs' pack, which lists the languages and replaces the vector fonts); auto stages every speech converted when
# languages are staged, none on the web, whose page preloads its data, nor beside KOPRULU_LOCALES=none. Bundles go to
# dist/product/<platform> (KOPRULU_DIST: another folder for <platform>, such as a second bundle of another
# conversion); the converter's output (generated/, or KOPRULU_GENERATED: another --out directory, such as a
# conversion from StarCraft: Remastered alone, whose repack is generated/repack-single or generated/repack of it)
# must be current.
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT="$(pwd)"
PLATFORM="${1:-macos}"
VARIANT="${2:-release}"
case "$VARIANT" in release | debug) ;; *) echo "product: the variant is release or debug" >&2; exit 2 ;; esac
BUILD_TYPE=Release
[ "$VARIANT" = debug ] && BUILD_TYPE=RelWithDebInfo
DIST="${KOPRULU_DIST:-dist/product}"
case "$DIST" in /*) ;; *) DIST="$ROOT/$DIST" ;; esac # the platforms' scripts take it from other folders
OUT="$DIST/$PLATFORM"
STAGE="$ROOT/build/product/data-$VARIANT"
GENERATED="${KOPRULU_GENERATED:-generated}"
# the icon files of the converter (python3 -m converter icon), when it ran: beside the data or in generated/ (a
# conversion from StarCraft: Remastered alone has no executable to take the icon from)
ICON=""
for dir in "$GENERATED/icon" generated/icon; do
  if [ -z "$ICON" ] && [ -d "$dir" ]; then ICON="$(cd "$dir" && pwd)"; fi
done

tools/toolchain/fetch.sh >/dev/null

CINEMATICS="${KOPRULU_CINEMATICS:-auto}"
if [ "$CINEMATICS" = auto ]; then # the best pack converted (generated/video), none where none is
  CINEMATICS=none
  for v in hd2 sd; do
    if [ "$CINEMATICS" = none ] && [ -f "$GENERATED/video/cinematics-$v.kpd" ]; then CINEMATICS="$v"; fi
  done
  # a product of a converted pack carries it (hundreds of MB) and libvpx: said, so that a build's size is no surprise
  [ "$CINEMATICS" = none ] || echo "product: cinematics $CINEMATICS (auto; KOPRULU_CINEMATICS=none builds none)" >&2
fi
CINE_VARIANTS=""
case "$CINEMATICS" in
  none) ;;
  sd | hd2 | both)
    CINE_VARIANTS="$CINEMATICS"
    [ "$CINEMATICS" = both ] && CINE_VARIANTS="sd hd2"
    for v in $CINE_VARIANTS; do
      if [ ! -f "$GENERATED/video/cinematics-$v.kpd" ]; then
        hint="--scr DIR --variant $v"
        [ "$v" = sd ] && hint="--scr DIR --variant sd, or --data DIR from 1.17"
        echo "product: no $GENERATED/video/cinematics-$v.kpd (python3 -m converter cinematics $hint)" >&2
        exit 1
      fi
    done
    ;;
  *) echo "product: KOPRULU_CINEMATICS is auto, none, sd, hd2 or both" >&2; exit 2 ;;
esac

LOCALES="${KOPRULU_LOCALES:-auto}"
case "$LOCALES" in
  auto) LOCALES=""
    if [ -f "$GENERATED/locale/glyphs.kpd" ]; then
      for d in "$GENERATED"/locale/*/; do
        if [ -f "$d/strings.kpd" ]; then LOCALES="$LOCALES $(basename "$d")"; fi
      done
    fi
    LOCALES="${LOCALES# }" ;;
  none) LOCALES="" ;;
  *) for id in $LOCALES; do
      if [ ! -f "$GENERATED/locale/$id/strings.kpd" ] || [ ! -f "$GENERATED/locale/glyphs.kpd" ]; then
        echo "product: no $GENERATED/locale/$id (python3 -m converter locale --scr DIR --locale $id)" >&2
        exit 2
      fi
    done ;;
esac
echo "product: languages ${LOCALES:-none} (KOPRULU_LOCALES)" >&2

SPEECH="${KOPRULU_SPEECH:-auto}"
case "$SPEECH" in
  auto) SPEECH=""
    if [ "$PLATFORM" != web ] && [ -n "$LOCALES" ]; then # the glyphs' pack it needs comes with the languages
      for d in "$GENERATED"/speech/*/; do
        if [ -f "$d/units.kpd" ]; then SPEECH="$SPEECH $(basename "$d")"; fi
      done
    fi
    SPEECH="${SPEECH# }" ;;
  none) SPEECH="" ;;
  *) for id in $SPEECH; do
      if [ ! -f "$GENERATED/speech/$id/units.kpd" ] || [ ! -f "$GENERATED/locale/glyphs.kpd" ]; then
        echo "product: no $GENERATED/speech/$id (python3 -m converter speech --scr DIR --locale $id)" >&2
        exit 2
      fi
    done ;;
esac
echo "product: speech ${SPEECH:-none} (KOPRULU_SPEECH)" >&2

# The repack (ADR 0018): a computer's one file, a phone's and the web's groups (each read or fetched when needed)
case "$PLATFORM" in
  web|android|ios|ios-sim) REPACK=repack; REPACK_HINT="python3 -m converter repack --data DIR|--scr DIR" ;;
  *) REPACK=repack-single; REPACK_HINT="python3 -m converter repack --single --data DIR|--scr DIR" ;;
esac
if [ ! -f "$GENERATED/$REPACK/koprulu-core.mpq" ] && [ ! -f "$GENERATED/$REPACK/koprulu.mpq" ]; then
  echo "product: no $GENERATED/$REPACK ($REPACK_HINT)" >&2
  exit 1
fi
# the files of the repack a bundle carries (relative to $GENERATED): the cinematics' group goes, the packs show them
GENERATED_DIR="$(cd "$GENERATED" && pwd)"
repack_files() {
  (cd "$GENERATED_DIR" && find "$REPACK" -name 'koprulu*.mpq' ! -name koprulu-cinematics.mpq | sort)
}

# The data of a bundle under $STAGE/generated (hard links where the file system allows).
stage_data() {
  rm -rf "$STAGE"
  mkdir -p "$STAGE/generated"
  (cd "$GENERATED"
    {
      echo data/console.kpd # the console's pieces (Remastered's, whole: python3 -m converter console)
      # the web preloads the core, the other groups lie beside the page (stage_web_fetched)
      if [ "$PLATFORM" = web ]; then repack_files | grep '/koprulu-core\.mpq$'; else repack_files; fi
      if [ -n "$LOCALES$SPEECH" ]; then echo locale/glyphs.kpd; fi # LOCS lists the languages
      for id in $LOCALES; do find "locale/$id" -name '*.kpd' -o -name '*.kpm'; done # each language's overlays
      for id in $SPEECH; do find "speech/$id" -name '*.kpd' -o -name '*.kpm'; done   # each speech's
      if [ "$PLATFORM" != web ]; then # the web's head alone (web_hd_heads.py), the rest beside the page
        for v in $CINE_VARIANTS; do echo "video/cinematics-$v.kpd"; done
      fi
      if [ "$VARIANT" = debug ]; then # the test scenarios (their CHK, the scenarios' catalog)
        find scenarios -name '*.chk' -o -name index.txt
        find viewer -name '*.mcrc'
      fi
      # StarCraft: Remastered's HD art when imported (python3 -m converter hd; ADR 0013): HD2 and HD on a computer,
      # HD2 on a phone (stage_hd_phone), HD2 on the web fetched as byte ranges beside the page (stage_web_fetched)
      case "$PLATFORM" in
        web|android|ios|ios-sim) ;;
        *) if [ -d hd ]; then find hd -name '*.kpd'; fi ;;
      esac
    } | while read -r f; do
      mkdir -p "$STAGE/generated/$(dirname "$f")"
      ln "$f" "$STAGE/generated/$f" 2>/dev/null || cp "$f" "$STAGE/generated/$f"
    done)
  # the vector fonts beside the data (kp_app_files.c read_vector_fonts), with their licences
  for f in exo2/Exo2.ttf exo2/OFL.txt michroma/Michroma-Regular.ttf michroma/OFL.txt; do
    mkdir -p "$STAGE/fonts/$(dirname "$f")"
    cp "third_party/fonts/$f" "$STAGE/fonts/$f"
  done
  # the Linux window's icon (SDL_SetWindowIcon; the other platforms' icons go into their bundles)
  if [ "$PLATFORM" = linux ] && [ -n "$ICON" ]; then
    mkdir -p "$STAGE/generated/icon"
    cp "$ICON/icon-256.png" "$STAGE/generated/icon/"
  fi
  case "$PLATFORM" in
    android|ios|ios-sim) stage_hd_phone ;;
    web) # shellcheck disable=SC2086
      python3 tools/web_hd_heads.py "$GENERATED" "$STAGE/generated" --video $CINE_VARIANTS ;;
  esac
  # a debug build's scripts (ADR 0017): a phone's --script reads generated/scripts/NAME.kps from its assets
  if [ "$VARIANT" = debug ]; then
    mkdir -p "$STAGE/generated/scripts"
    cp tests/scripts/*.kps "$STAGE/generated/scripts/"
  fi
  echo "product: data $(du -sh "$STAGE" | cut -f1)"
}

# The web's HD2 (stage R4b) and cinematics: the page preloads the files' heads and sizes (stage_data,
# tools/web_hd_heads.py) and fetches the rest as byte ranges from the files beside it (a server with Range requests:
# tools/web_serve.py); the repack's groups but its core, fetched whole (ADR 0018, stage D8b).
stage_web_fetched() {
  local src f v
  src="$(cd "$GENERATED" && pwd)"
  { [ -d "$src/hd/hd2" ] && (cd "$src" && find hd/hd2 -name '*.kpd')
    for v in $CINE_VARIANTS; do echo "video/cinematics-$v.kpd"; done
    repack_files | grep -v '/koprulu-core\.mpq$'
  } | while read -r f; do
    mkdir -p "$OUT/generated/$(dirname "$f")"
    ln "$src/$f" "$OUT/generated/$f" 2>/dev/null || cp "$src/$f" "$OUT/generated/$f"
  done
}

# A phone's HD2 (stage R4f): its pages in ASTC (KOPRULU_PHONE_BLOCKS=astc, a little smaller and better in colour: the default but on ios-sim) or ETC2 and EAC (etc2,
# which every GLES 3.0 takes), its tiles and the water's normal maps in ETC2 RGB8 in both; S3TC is an extension phones
# rarely have. Transcoded once by kp_hd_phone (the host's tools) into build/hd-astc and build/hd-etc2, again when the
# source changed. Each cache holds one converter output's transcoding by one kp_hd_phone (its .source: the output's
# directory and the tool's SHA-256): another of either empties it first. The tool links only what decides the blocks
# (the codecs, the HD packs and the containers; tools/ci.sh checks it), so a change elsewhere in kp_core keeps them.
sha256() { if command -v sha256sum >/dev/null; then sha256sum "$1"; else shasum -a 256 "$1"; fi | cut -d' ' -f1; }
transcode_hd() { # <etc2|astc> <cache dir> <files, relative to the source, on stdin>
  local blocks="$1" cache="$2" cpus stamp f files=1 threads=1
  cpus="$(sysctl -n hw.ncpu 2>/dev/null || nproc)"
  # a pack in ASTC encodes its pages on all the cores, one pack at a time; the rest file by file (a tile set is quick)
  if [ "$blocks" = astc ]; then threads="$cpus"; else files="$cpus"; fi
  stamp="$src $(sha256 build/cmake/kp_hd_phone)"
  if [ "$(cat "$cache/.source" 2>/dev/null)" != "$stamp" ]; then
    rm -rf "$cache"
    mkdir -p "$cache"
    printf '%s\n' "$stamp" >"$cache/.source"
  fi
  while read -r f; do
    if [ ! -f "$cache/$f" ] || [ "$src/$f" -nt "$cache/$f" ]; then
      mkdir -p "$cache/$(dirname "$f")"
      echo "$f"
    fi
  done | KP_HD_PHONE_THREADS="$threads" xargs -P "$files" -I{} build/cmake/kp_hd_phone "$blocks" "$src/{}" "$cache/{}"
}
stage_hd_phone() {
  local src blocks default_blocks=astc astc_tool=OFF f from
  [ "$PLATFORM" = ios-sim ] && default_blocks=etc2 # the iOS Simulator's context takes no ASTC
  blocks="${KOPRULU_PHONE_BLOCKS:-$default_blocks}"
  case "$blocks" in astc | etc2) ;; *) echo "product: KOPRULU_PHONE_BLOCKS is astc or etc2" >&2; exit 2 ;; esac
  src="$(cd "$GENERATED" && pwd)"
  [ -d "$src/hd/hd2" ] || return 0
  # etc2 needs no encoder: not fetched for it, though kept in the tool when it is there (one tool, one cache's stamp)
  [ "$blocks" = astc ] && tools/toolchain/fetch.sh astc >/dev/null
  [ -d tools/toolchain/astc ] && astc_tool=ON
  cmake -S . -B build/cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -DKP_ASTC="$astc_tool" >/dev/null
  cmake --build build/cmake --target kp_hd_phone >/dev/null
  # the interface's art and the portraits go as they are: decoded on the CPU into an RGBA atlas anyway (kp_app_hd_ui.c)
  if [ "$blocks" = astc ]; then
    (cd "$src" && find hd/hd2 -path '*/packs/*' -name '*.kpd') | transcode_hd astc "$ROOT/build/hd-astc"
    (cd "$src" && find hd/hd2 -name '*.kpd' ! -path '*/packs/*' ! -name ui.kpd ! -name portraits.kpd) \
      | transcode_hd etc2 "$ROOT/build/hd-etc2"
  else
    (cd "$src" && find hd/hd2 -name '*.kpd' ! -name ui.kpd ! -name portraits.kpd) | transcode_hd etc2 "$ROOT/build/hd-etc2"
  fi
  (cd "$src" && find hd/hd2 -name '*.kpd') | while read -r f; do
    from="$ROOT/build/hd-etc2/$f"
    case "$f" in hd/hd2/packs/*) from="$ROOT/build/hd-$blocks/$f" ;; esac
    case "$(basename "$f")" in ui.kpd | portraits.kpd) from="$src/$f" ;; esac
    mkdir -p "$STAGE/generated/$(dirname "$f")"
    ln "$from" "$STAGE/generated/$f" 2>/dev/null || cp "$from" "$STAGE/generated/$f"
  done
}

# The web page's icons: favicon.ico, the PNG favicons and the touch icon beside it, and the links where the shell
# page (native/kp_app/web/shell.html) has the marker; without the icons the marker goes.
web_icons() {
  local links=""
  if [ -n "$ICON" ]; then
    cp "$ICON"/web/favicon.ico "$ICON"/web/favicon-16x16.png "$ICON"/web/favicon-32x32.png \
      "$ICON"/web/apple-touch-icon.png "$OUT/"
    links='<link rel="icon" href="favicon.ico" sizes="any"><link rel="icon" type="image/png" sizes="32x32" href="favicon-32x32.png"><link rel="icon" type="image/png" sizes="16x16" href="favicon-16x16.png"><link rel="apple-touch-icon" href="apple-touch-icon.png">'
  fi
  sed "s|<!--KP_ICONS-->|$links|" "$OUT/index.html" >"$OUT/index.html.tmp"
  mv "$OUT/index.html.tmp" "$OUT/index.html"
}

configure_build() { # <build dir> <cmake args...>
  local dir="$1"
  shift
  cmake -S . -B "$dir" -G Ninja -DCMAKE_BUILD_TYPE="$BUILD_TYPE" -DKP_APP=ON -DKP_TOOLS=OFF "$@" >/dev/null
  cmake --build "$dir" --target koprulu
}

case "$PLATFORM" in
  macos)
    stage_data
    configure_build build/app-macos
    APP="$OUT/KopruluSector.app"
    rm -rf "$APP"
    mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
    cp build/app-macos/koprulu "$APP/Contents/MacOS/koprulu"
    cp -R "$STAGE" "$APP/Contents/Resources/data"
    cat > "$APP/Contents/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>CFBundleExecutable</key><string>koprulu</string>
  <key>CFBundleIconFile</key><string>KopruluSector</string>
  <key>CFBundleIdentifier</key><string>com.koprulusector.game</string>
  <key>CFBundleName</key><string>Koprulu Sector</string>
  <key>CFBundleDisplayName</key><string>Koprulu Sector</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>0.1</string>
  <key>NSHighResolutionCapable</key><true/>
</dict></plist>
PLIST
    [ -n "$ICON" ] && cp "$ICON/icon.icns" "$APP/Contents/Resources/KopruluSector.icns"
    echo "product: $APP" ;;
  web)
    stage_data
    emcmake cmake -S . -B build/app-web -G Ninja -DCMAKE_BUILD_TYPE="$BUILD_TYPE" -DKP_APP=ON \
      "-DKP_WEB_LINK=--preload-file;$STAGE@/data" >/dev/null
    rm -f build/app-web/koprulu.html # the link packs the staged data, which Ninja does not track
    cmake --build build/app-web --target koprulu
    rm -rf "$OUT"
    mkdir -p "$OUT"
    cp build/app-web/koprulu.html "$OUT/index.html"
    cp build/app-web/koprulu.js build/app-web/koprulu.wasm build/app-web/koprulu.data "$OUT/"
    web_icons
    stage_web_fetched
    echo "product: $OUT/index.html" ;;
  android)
    stage_data
    KOPRULU_ICON="$ICON" tools/native/android_app.sh "$VARIANT" "$STAGE" "$OUT" ;;
  ios|ios-sim)
    stage_data
    SDK=iphoneos
    [ "$PLATFORM" = ios-sim ] && SDK=iphonesimulator
    cmake -S . -B "build/app-$PLATFORM" -G Xcode -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT="$SDK" \
      -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 -DKP_APP=ON -DKP_TOOLS=OFF \
      "-DKP_APP_RESOURCES=$STAGE" "-DKP_APP_ICONS=${ICON:+$ICON/ios}" >/dev/null
    cmake --build "build/app-$PLATFORM" --target koprulu --config "$BUILD_TYPE" -- -sdk "$SDK" CODE_SIGNING_ALLOWED=NO
    mkdir -p "$OUT"
    rm -rf "$OUT/koprulu.app"
    cp -R "build/app-$PLATFORM/$BUILD_TYPE-$SDK/koprulu.app" "$OUT/"
    echo "product: $OUT/koprulu.app" ;;
  linux)
    stage_data
    tools/native/linux_app.sh "$VARIANT" "$STAGE" "$OUT" ;;
  windows)
    stage_data
    configure_build build/app-windows -DCMAKE_TOOLCHAIN_FILE="$ROOT/tools/toolchain/cross-windows.cmake" \
      "-DKP_APP_ICON=${ICON:+$ICON/icon.ico}"
    rm -rf "$OUT"
    mkdir -p "$OUT"
    cp build/app-windows/koprulu.exe "$OUT/"
    cp -R "$STAGE" "$OUT/data"
    echo "product: $OUT" ;;
  *) echo "product: unknown platform $PLATFORM" >&2; exit 2 ;;
esac
