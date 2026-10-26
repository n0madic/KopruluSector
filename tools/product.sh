#!/usr/bin/env bash
# The product's builds (plan phase S, stage S6; ADR 0012): kp_app over SDL 3.4 with the converted data.
#   tools/product.sh macos|web|android|ios|ios-sim|linux|windows [release|debug]
# The data staged for a bundle: generated/data, the missions (.kpm, their sounds), the packs; a debug build adds the
# test scenarios and the terrain checks' CRCs (the development checks: --autotest, --play-check). Bundles go to
# dist/product/<platform>; the converter's output (generated/) must be current.
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT="$(pwd)"
PLATFORM="${1:-macos}"
VARIANT="${2:-release}"
BUILD_TYPE=Release
[ "$VARIANT" = debug ] && BUILD_TYPE=RelWithDebInfo
OUT="$ROOT/dist/product/$PLATFORM"
STAGE="$ROOT/build/product/data-$VARIANT"

tools/toolchain/fetch.sh >/dev/null

# The data of a bundle under $STAGE/generated (hard links where the file system allows).
stage_data() {
  rm -rf "$STAGE"
  mkdir -p "$STAGE/generated"
  (cd generated
    {
      find data -name '*.kpd'
      find missions -name '*.kpm' -o -name '*_sounds.kpd' -o -name index.txt
      find packs -name '*.kpd'
      if [ "$VARIANT" = debug ]; then
        find scenarios -name '*.kpm' -o -name index.txt
        find viewer -name '*.mcrc'
      fi
    } | while read -r f; do
      mkdir -p "$STAGE/generated/$(dirname "$f")"
      ln "$f" "$STAGE/generated/$f" 2>/dev/null || cp "$f" "$STAGE/generated/$f"
    done)
  echo "product: data $(du -sh "$STAGE" | cut -f1)"
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
  <key>CFBundleIdentifier</key><string>com.koprulusector.game</string>
  <key>CFBundleName</key><string>KopruluSector</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>0.1</string>
  <key>NSHighResolutionCapable</key><true/>
</dict></plist>
PLIST
    echo "product: $APP" ;;
  web)
    stage_data
    emcmake cmake -S . -B build/app-web -G Ninja -DCMAKE_BUILD_TYPE="$BUILD_TYPE" -DKP_APP=ON \
      "-DKP_WEB_LINK=--preload-file;$STAGE@/data" >/dev/null
    cmake --build build/app-web --target koprulu
    rm -rf "$OUT"
    mkdir -p "$OUT"
    cp build/app-web/koprulu.html "$OUT/index.html"
    cp build/app-web/koprulu.js build/app-web/koprulu.wasm build/app-web/koprulu.data "$OUT/"
    echo "product: $OUT/index.html" ;;
  android)
    stage_data
    tools/native/android_app.sh "$VARIANT" "$STAGE" "$OUT" ;;
  ios|ios-sim)
    stage_data
    SDK=iphoneos
    [ "$PLATFORM" = ios-sim ] && SDK=iphonesimulator
    cmake -S . -B "build/app-$PLATFORM" -G Xcode -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT="$SDK" \
      -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 -DKP_APP=ON -DKP_TOOLS=OFF \
      "-DKP_APP_RESOURCES=$STAGE" >/dev/null
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
    configure_build build/app-windows -DCMAKE_TOOLCHAIN_FILE="$ROOT/tools/toolchain/cross-windows.cmake"
    rm -rf "$OUT"
    mkdir -p "$OUT"
    cp build/app-windows/koprulu.exe "$OUT/"
    cp -R "$STAGE" "$OUT/data"
    echo "product: $OUT" ;;
  *) echo "product: unknown platform $PLATFORM" >&2; exit 2 ;;
esac
