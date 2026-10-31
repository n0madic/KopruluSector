#!/usr/bin/env bash
# The product's builds (plan phase S, stage S6; ADR 0012): kp_app over SDL 3.4 with the converted data.
#   tools/product.sh macos|web|android|ios|ios-sim|linux|windows [release|debug]
# The data staged for a bundle: generated/data, the missions (.kpm, their sounds), the packs, the HD art where it was
# imported (generated/hd, not for the web; a phone's HD2 in ETC2, transcoded by kp_hd_etc2 into build/hd-etc2);
# StarCraft: Remastered's cinematics (generated/video, python3 -m converter cinematics; ADR 0015) with
# KOPRULU_CINEMATICS=auto|sd|hd2|both|none: the packs staged and the player built in over libvpx (fetched, built from
# C), whatever HD art is staged (a game of HD art with SD movies, or the other way round); auto, the default, takes
# HD2's pack where it was converted, else SD's, else none; none builds none of it; not for the web and iOS; a debug build adds the
# test scenarios and the terrain checks' CRCs (the development checks: --autotest, --play-check). Bundles go to
# dist/product/<platform>; the converter's output (generated/, or KOPRULU_GENERATED: another --out directory, such
# as a conversion from StarCraft: Remastered alone) must be current.
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT="$(pwd)"
PLATFORM="${1:-macos}"
VARIANT="${2:-release}"
BUILD_TYPE=Release
[ "$VARIANT" = debug ] && BUILD_TYPE=RelWithDebInfo
OUT="$ROOT/dist/product/$PLATFORM"
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
if [ "$CINEMATICS" = auto ]; then # the best pack converted (generated/video), none for the web and iOS or where none is
  CINEMATICS=none
  case "$PLATFORM" in
    web | ios | ios-sim) ;;
    *) for v in hd2 sd; do
         if [ "$CINEMATICS" = none ] && [ -f "$GENERATED/video/cinematics-$v.kpd" ]; then CINEMATICS="$v"; fi
       done ;;
  esac
fi
CINE_VARIANTS=""
KP_CINE_CMAKE="-DKP_CINEMATICS=OFF" # a build directory's earlier choice is not kept
case "$CINEMATICS" in
  none) ;;
  sd | hd2 | both)
    case "$PLATFORM" in
      web | ios | ios-sim) echo "product: KOPRULU_CINEMATICS is for macos, windows, linux and android" >&2; exit 2 ;;
    esac
    CINE_VARIANTS="$CINEMATICS"
    [ "$CINEMATICS" = both ] && CINE_VARIANTS="sd hd2"
    for v in $CINE_VARIANTS; do
      if [ ! -f "$GENERATED/video/cinematics-$v.kpd" ]; then
        echo "product: no $GENERATED/video/cinematics-$v.kpd (python3 -m converter cinematics --scr DIR --variant $v)" >&2
        exit 1
      fi
    done
    tools/toolchain/fetch.sh vpx >/dev/null
    KP_CINE_CMAKE="-DKP_CINEMATICS=ON" ;;
  *) echo "product: KOPRULU_CINEMATICS is auto, none, sd, hd2 or both" >&2; exit 2 ;;
esac
export KP_CINE_CMAKE

# The data of a bundle under $STAGE/generated (hard links where the file system allows).
stage_data() {
  rm -rf "$STAGE"
  mkdir -p "$STAGE/generated"
  (cd "$GENERATED"
    {
      find data -name '*.kpd'
      find missions -name '*.kpm' -o -name '*_sounds.kpd' -o -name index.txt
      find packs -name '*.kpd'
      for v in $CINE_VARIANTS; do echo "video/cinematics-$v.kpd"; done
      if [ "$VARIANT" = debug ]; then
        find scenarios -name '*.kpm' -o -name index.txt
        find viewer -name '*.mcrc'
      fi
      # StarCraft: Remastered's HD art when imported (python3 -m converter hd; ADR 0013): HD2 and HD on a computer,
      # HD2 on a phone (stage_hd_etc2), HD2 on the web fetched as byte ranges beside the page (stage_hd_web)
      case "$PLATFORM" in
        web|android|ios|ios-sim) ;;
        *) if [ -d hd ]; then find hd -name '*.kpd'; fi ;;
      esac
    } | while read -r f; do
      mkdir -p "$STAGE/generated/$(dirname "$f")"
      ln "$f" "$STAGE/generated/$f" 2>/dev/null || cp "$f" "$STAGE/generated/$f"
    done)
  # the Linux window's icon (SDL_SetWindowIcon; the other platforms' icons go into their bundles)
  if [ "$PLATFORM" = linux ] && [ -n "$ICON" ]; then
    mkdir -p "$STAGE/generated/icon"
    cp "$ICON/icon-256.png" "$STAGE/generated/icon/"
  fi
  case "$PLATFORM" in
    android|ios|ios-sim) stage_hd_etc2 ;;
    web) [ -d "$GENERATED/hd/hd2" ] && python3 tools/web_hd_heads.py "$GENERATED" "$STAGE/generated" ;;
  esac
  echo "product: data $(du -sh "$STAGE" | cut -f1)"
}

# The web's HD2 (stage R4b): the page preloads the files' heads and sizes (stage_data, tools/web_hd_heads.py) and
# fetches the rest as byte ranges from the files beside it (a server with Range requests: tools/web_serve.py).
stage_hd_web() {
  local src f
  [ -d "$GENERATED/hd/hd2" ] || return 0
  src="$(cd "$GENERATED" && pwd)"
  (cd "$src" && find hd/hd2 -name '*.kpd') | while read -r f; do
    mkdir -p "$OUT/generated/$(dirname "$f")"
    ln "$src/$f" "$OUT/generated/$f" 2>/dev/null || cp "$src/$f" "$OUT/generated/$f"
  done
}

# A phone's HD2 (stage R4f): its pages and tiles in ETC2 and EAC, which every GLES 3.0 takes (S3TC is an extension
# phones rarely have); transcoded once by kp_hd_etc2 (the host's tools) into build/hd-etc2, again when the source
# changed.
stage_hd_etc2() {
  local src cache="$ROOT/build/hd-etc2" f
  src="$(cd "$GENERATED" && pwd)"
  [ -d "$src/hd/hd2" ] || return 0
  cmake -S . -B build/cmake -G Ninja -DCMAKE_BUILD_TYPE=Release >/dev/null
  cmake --build build/cmake --target kp_hd_etc2 >/dev/null
  # the interface's art and the portraits go as they are: decoded on the CPU into an RGBA atlas anyway (kp_app_hd_ui.c)
  (cd "$src" && find hd/hd2 -name '*.kpd' ! -name ui.kpd ! -name portraits.kpd) | while read -r f; do
    if [ ! -f "$cache/$f" ] || [ "$src/$f" -nt "$cache/$f" ]; then
      mkdir -p "$cache/$(dirname "$f")"
      echo "$f"
    fi
  done | xargs -P "$(sysctl -n hw.ncpu 2>/dev/null || nproc)" -I{} build/cmake/kp_hd_etc2 "$src/{}" "$cache/{}"
  (cd "$src" && find hd/hd2 -name '*.kpd') | while read -r f; do
    local from="$cache/$f"
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
    configure_build build/app-macos $KP_CINE_CMAKE
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
  <key>CFBundleName</key><string>KopruluSector</string>
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
    stage_hd_web
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
      "-DKP_APP_ICON=${ICON:+$ICON/icon.ico}" $KP_CINE_CMAKE
    rm -rf "$OUT"
    mkdir -p "$OUT"
    cp build/app-windows/koprulu.exe "$OUT/"
    cp -R "$STAGE" "$OUT/data"
    echo "product: $OUT" ;;
  *) echo "product: unknown platform $PLATFORM" >&2; exit 2 ;;
esac
