#!/usr/bin/env bash
# The product's builds (plan phase S, stage S6; ADR 0012): kp_app over SDL 3.4 with the original data's repack.
#   tools/product.sh macos|web|android|ios|ios-sim|linux|windows [release|debug]
# The data staged for a bundle (ADR 0018, stage D8b): the repack of the original files (kp_convert repack DIR):
# a computer's one koprulu.mpq (generated/repack-single, repack DIR --single), a phone's and the web's groups
# (generated/repack), the cinematics' groups apart in both (below), the web's core preloaded and the other groups
# beside the page, fetched when needed (a mission's before it starts, the music's from the start, a movie's bytes by
# their range as it begins), Android's read in place from the APK (stored uncompressed:
# tools/native/android_app.sh's noCompress);
# the vector fonts (third_party/fonts as fonts/); the HD art's sets the repack holds (ADR 0019, stage C5:
# koprulu-<set>.mpq, kp_convert repack --hd from StarCraft: Remastered): HD2 and HD on a computer, HD2 on the web (its
# pages fetched by their ranges), on a phone HD2's pages in ASTC (koprulu-hd2-astc.mpq), or in ETC2 with
# KOPRULU_PHONE_BLOCKS=etc2 for phones without ASTC, which otherwise draw the original's art, and by default on
# ios-sim, whose context takes no ASTC (koprulu-hd2-etc2.mpq); with them their variants' HD interface
# (koprulu-ui-<variant>.mpq, stage C6) and the HD portraits (koprulu-portraits.mpq), the web fetching a frame or a
# portrait by its range; the console's pieces are the core's (koprulu\console.kpd from a Remastered install);
# the cinematics (ADR 0015; ADR 0019, stage C4: the repack's koprulu-cinematics.mpq, the SD set: StarCraft:
# Remastered's, or the 1.17 install's transcoded into WebM; koprulu-cinematics-hd2.mpq, Remastered's HD2 set) with
# KOPRULU_CINEMATICS=auto|sd|hd2|both|none: the groups staged, whatever HD art is staged (a game of HD art with SD
# movies, or the other way round); auto, the default, takes HD2's group where the repack has it, else SD's, else none;
# the web fetches a movie from its group beside the page by its range (a server with Range requests:
# tools/web_serve.py); a debug build adds the
# test scenarios, the terrain checks' CRCs and the scripts (tests/scripts/*.kps as generated/scripts/: the
# development checks, --autotest and --script; a phone reads them from its assets). The languages (ADR 0016; ADR
# 0019, stages C2 and C3: their texts and speech, which the game builds from Remastered's own files) are the repack's
# groups (koprulu-locale-<id>.mpq, kp_convert repack from StarCraft: Remastered) with KOPRULU_LOCALES=auto|none|<ids>:
# auto, the default, stages every language group of the repack; none stages none, the game in its base language; a
# list ("ruRU deDE") stages those, each of which the repack must hold. The computers and phones carry them; the web
# carries none yet. Bundles go to
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
# the icon files (kp_convert icon DIR), when it ran: beside the data or in generated/ (a conversion from StarCraft:
# Remastered alone has no executable to take the icon from)
ICON=""
for dir in "$GENERATED/icon" generated/icon; do
  if [ -z "$ICON" ] && [ -d "$dir" ]; then ICON="$(cd "$dir" && pwd)"; fi
done

tools/toolchain/fetch.sh >/dev/null

# The repack (ADR 0018): a computer's one file, a phone's and the web's groups (each read or fetched when needed)
case "$PLATFORM" in
  web|android|ios|ios-sim) REPACK=repack; REPACK_HINT="kp_convert repack DIR, make repack SC=DIR|SCR=DIR" ;;
  *) REPACK=repack-single; REPACK_HINT="kp_convert repack DIR --single, make repack SC=DIR|SCR=DIR" ;;
esac
if [ ! -f "$GENERATED/$REPACK/koprulu-core.mpq" ] && [ ! -f "$GENERATED/$REPACK/koprulu.mpq" ]; then
  echo "product: no $GENERATED/$REPACK ($REPACK_HINT)" >&2
  exit 1
fi
# the cinematics' groups (the repack's, kept apart with --single too): a set's group by its name
cine_group() { if [ "$1" = sd ]; then echo koprulu-cinematics.mpq; else echo "koprulu-cinematics-$1.mpq"; fi; }
CINEMATICS="${KOPRULU_CINEMATICS:-auto}"
if [ "$CINEMATICS" = auto ]; then # the best set the repack has, none where it has none
  CINEMATICS=none
  for v in hd2 sd; do
    if [ "$CINEMATICS" = none ] && [ -f "$GENERATED/$REPACK/$(cine_group "$v")" ]; then CINEMATICS="$v"; fi
  done
  # a product of the movies carries hundreds of MB: said, so that a build's size is no surprise
  [ "$CINEMATICS" = none ] || echo "product: cinematics $CINEMATICS (auto; KOPRULU_CINEMATICS=none stages none)" >&2
fi
CINE_GROUPS="" # each name with a space before it (repack_files looks for " NAME ")
case "$CINEMATICS" in
  none) ;;
  sd | hd2 | both)
    variants="$CINEMATICS"
    [ "$CINEMATICS" = both ] && variants="sd hd2"
    for v in $variants; do
      if [ ! -f "$GENERATED/$REPACK/$(cine_group "$v")" ]; then
        hint="kp_convert repack DIR"
        [ "$v" = hd2 ] && hint="kp_convert repack DIR from StarCraft: Remastered"
        echo "product: no $GENERATED/$REPACK/$(cine_group "$v") ($hint)" >&2
        exit 1
      fi
      CINE_GROUPS="$CINE_GROUPS $(cine_group "$v")"
    done
    ;;
  *) echo "product: KOPRULU_CINEMATICS is auto, none, sd, hd2 or both" >&2; exit 2 ;;
esac
# the HD art's sets of the platform (ADR 0019, stage C5), the groups of them the repack has
case "$PLATFORM" in
  web) HD_SETS="hd2" ;;
  android | ios | ios-sim)
    default_blocks=astc
    [ "$PLATFORM" = ios-sim ] && default_blocks=etc2 # the iOS Simulator's context takes no ASTC
    blocks="${KOPRULU_PHONE_BLOCKS:-$default_blocks}"
    case "$blocks" in astc | etc2) ;; *) echo "product: KOPRULU_PHONE_BLOCKS is astc or etc2" >&2; exit 2 ;; esac
    HD_SETS="hd2-$blocks" ;;
  *) HD_SETS="hd2 hd" ;;
esac
HD_GROUPS="" # each name with a space before it (repack_files looks for " NAME ")
UI_GROUPS=""  # the HD interface's of the sets' variants (stage C6) and the portraits' with any set
for set in $HD_SETS; do
  if [ -f "$GENERATED/$REPACK/koprulu-$set.mpq" ]; then
    HD_GROUPS="$HD_GROUPS koprulu-$set.mpq"
    ui="koprulu-ui-${set%%-*}.mpq" # hd2-astc's interface is hd2's
    if [ -f "$GENERATED/$REPACK/$ui" ]; then UI_GROUPS="$UI_GROUPS $ui"; fi
  fi
done
if [ -n "$HD_GROUPS" ] && [ -f "$GENERATED/$REPACK/koprulu-portraits.mpq" ]; then
  UI_GROUPS="$UI_GROUPS koprulu-portraits.mpq"
fi
echo "product: HD sets${HD_GROUPS:- none}${UI_GROUPS:+, interface$UI_GROUPS} (the repack's of $HD_SETS: kp_convert repack --hd from StarCraft: Remastered)" >&2
# the files of the repack a bundle carries (relative to $GENERATED): the cinematics' groups KOPRULU_CINEMATICS
# stages; the HD sets' groups of the platform and their interface's; the languages' groups (their texts and speech) KOPRULU_LOCALES stages,
# not to the web
# TODO: the web fetches a language's group when the language is chosen
GENERATED_DIR="$(cd "$GENERATED" && pwd)"
LANGUAGE_GROUPS="" # each name with a space before it (repack_files looks for " NAME ")
LOCALES="${KOPRULU_LOCALES:-auto}"
if [ "$PLATFORM" = web ]; then LOCALES=none; fi
case "$LOCALES" in
  auto) for group in "$GENERATED_DIR/$REPACK"/koprulu-locale-*.mpq; do
      if [ -f "$group" ]; then LANGUAGE_GROUPS="$LANGUAGE_GROUPS $(basename "$group")"; fi
    done ;;
  none) ;;
  *) for id in $LOCALES; do
      group="koprulu-locale-$(printf %s "$id" | tr '[:upper:]' '[:lower:]').mpq"
      if [ ! -f "$GENERATED_DIR/$REPACK/$group" ]; then
        echo "product: no $GENERATED/$REPACK/$group (kp_convert repack DIR from StarCraft: Remastered, --locales $id)" >&2
        exit 2
      fi
      LANGUAGE_GROUPS="$LANGUAGE_GROUPS $group"
    done ;;
esac
repack_files() {
  local f
  (cd "$GENERATED_DIR" && find "$REPACK" -name 'koprulu*.mpq' | sort) | while read -r f; do
    case "$(basename "$f")" in
      koprulu-locale-*) case "$LANGUAGE_GROUPS " in *" $(basename "$f") "*) echo "$f" ;; esac ;;
      koprulu-cinematics*) case "$CINE_GROUPS " in *" $(basename "$f") "*) echo "$f" ;; esac ;;
      koprulu-ui-* | koprulu-portraits.mpq) case "$UI_GROUPS " in *" $(basename "$f") "*) echo "$f" ;; esac ;;
      koprulu-hd*) case "$HD_GROUPS " in *" $(basename "$f") "*) echo "$f" ;; esac ;;
      *) echo "$f" ;;
    esac
  done
}
languages="$(repack_files | sed -n 's/.*koprulu-locale-\(.*\)\.mpq$/\1/p' | tr '\n' ' ')"
echo "product: languages ${languages:-none} (KOPRULU_LOCALES: the repack's language groups)" >&2

# The data of a bundle under $STAGE/generated (hard links where the file system allows).
stage_data() {
  rm -rf "$STAGE"
  mkdir -p "$STAGE/generated"
  (cd "$GENERATED"
    {
      # the web preloads the core, the other groups lie beside the page (stage_web_fetched)
      if [ "$PLATFORM" = web ]; then repack_files | grep '/koprulu-core\.mpq$'; else repack_files; fi
      if [ "$VARIANT" = debug ]; then # the test scenarios (their CHK, the scenarios' catalog)
        find scenarios -name '*.chk' -o -name index.txt
        find viewer -name '*.mcrc'
      fi
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
  if [ "$PLATFORM" = web ]; then # the groups fetched by ranges and their sizes ("path size": kp_app_web_beside)
    for g in $CINE_GROUPS $HD_GROUPS $UI_GROUPS; do
      echo "generated/$REPACK/$g $(wc -c <"$GENERATED/$REPACK/$g" | tr -d ' ')"
    done >"$STAGE/generated/web_sizes.txt"
  fi
  # a debug build's scripts (ADR 0017): a phone's --script reads generated/scripts/NAME.kps from its assets
  if [ "$VARIANT" = debug ]; then
    mkdir -p "$STAGE/generated/scripts"
    cp tests/scripts/*.kps "$STAGE/generated/scripts/"
  fi
  echo "product: data $(du -sh "$STAGE" | cut -f1)"
}

# The web's groups beside the page (stage R4b; a server with Range requests: tools/web_serve.py): the repack's groups
# but its core, fetched whole (ADR 0018, stage D8b), the cinematics' by each movie's range (ADR 0019, stage C4), the
# HD set's by each page's (stage C5), the interface's by each frame's and the portraits' by each clip's (stage C6);
# web_sizes.txt (stage_data) names the ranged ones and their sizes.
stage_web_fetched() {
  local src f
  src="$(cd "$GENERATED" && pwd)"
  repack_files | grep -v '/koprulu-core\.mpq$' | while read -r f; do
    mkdir -p "$OUT/generated/$(dirname "$f")"
    ln "$src/$f" "$OUT/generated/$f" 2>/dev/null || cp "$src/$f" "$OUT/generated/$f"
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
