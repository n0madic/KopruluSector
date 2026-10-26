#!/usr/bin/env bash
# Builds spike 16 for one platform: macos | web | ios-sim. Android goes through
# spikes/16-sdl3/android.sh (SDL's Gradle project).
# Output: build/spikes/16-sdl3/<platform>/.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
PLATFORM="${1:?usage: build.sh macos|web|ios-sim}"
OUT="$ROOT/build/spikes/16-sdl3/$PLATFORM"
OGG="${SPIKE_OGG:-$ROOT/generated/sounds/terran/terran_frigate_tvkyes00.ogg}"
SRC="$ROOT/spikes/16-sdl3"

"$ROOT/tools/toolchain/fetch.sh" >/dev/null

case "$PLATFORM" in
  macos)
    cmake -S "$SRC" -B "$OUT" -G Ninja -DCMAKE_BUILD_TYPE=Release -DKP_SPIKE_OGG="$OGG"
    cmake --build "$OUT" --target spike_sdl3 ;;
  web)
    emcmake cmake -S "$SRC" -B "$OUT" -G Ninja -DCMAKE_BUILD_TYPE=Release -DKP_SPIKE_OGG="$OGG"
    cmake --build "$OUT" --target spike_sdl3 ;;
  ios-sim)
    cmake -S "$SRC" -B "$OUT" -G Xcode -DCMAKE_SYSTEM_NAME=iOS -DCMAKE_OSX_SYSROOT=iphonesimulator \
      -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 -DKP_SPIKE_OGG="$OGG"
    cmake --build "$OUT" --target spike_sdl3 --config Release -- -sdk iphonesimulator \
      CODE_SIGNING_ALLOWED=NO ;;
  *) echo "unknown platform $PLATFORM" >&2; exit 1 ;;
esac
echo "built $OUT"
