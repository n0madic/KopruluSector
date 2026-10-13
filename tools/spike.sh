#!/usr/bin/env bash
# Builds and runs one phase-0 spike.
#   tools/spike.sh spikes/<name> macos     build, run the windowed engine, collect SPIKE lines
#   tools/spike.sh spikes/<name> web       bundle wasm-web (release) into dist/spikes/<name>/web,
#                                          run it in headless Chrome, collect console SPIKE lines
#   tools/spike.sh spikes/<name> android   bundle arm64 APK (debug: release drops print), install, run,
#                                          collect logcat (ANDROID_SERIAL picks the device)
# A spike is a directory with spike.settings (bob settings overrides, at least
# [bootstrap] main_collection) and prints result lines starting with "SPIKE ".
# SPIKE_SETTINGS_FILE replaces <dir>/spike.settings, e.g. the map viewer:
#   SPIKE_SETTINGS_FILE=viewer/viewer.settings tools/spike.sh viewer web
# SPIKE_NATIVE=1 (macos) runs an engine with the project's native extensions, built by the
# build server (BUILD_SERVER, default build.defold.com).
# Results go to build/spikes/<name>/<platform>.log.
set -euo pipefail
cd "$(dirname "$0")/.."

SPIKE="${1:?spike directory}"
SETTINGS="${SPIKE_SETTINGS_FILE:-$SPIKE/spike.settings}"
PLATFORM="${2:-macos}"
NAME="$(basename "$SPIKE")"
OUT="build/spikes/$NAME"
BUNDLE="dist/spikes/$NAME"  # bob refuses bundle output inside build/
TOOLCHAIN=tools/toolchain
JAVA="${JAVA:-java}"
TIMEOUT="${SPIKE_TIMEOUT:-120}"
# SPIKE_VARIANT=debug shows Lua errors in web builds (release drops print and logs).
# SPIKE_LOG names the result log (default: the platform), e.g. android-device vs android-emulator.
LOG="${SPIKE_LOG:-$PLATFORM}"
mkdir -p "$OUT"
# Each spike gets its own Android package so parallel installs do not replace each other.
PACKAGE="com.koprulusector.spike_$(tr -c 'a-zA-Z0-9\n' '_' <<<"$NAME")"
printf '[android]\npackage = %s\n' "$PACKAGE" >"$OUT/package.settings"
BOB=("$JAVA" -jar "$TOOLCHAIN/bob.jar" --root . --settings "$SETTINGS" --settings "$OUT/package.settings")
# SPIKE_SETTINGS adds one more bob settings file (e.g. a debug variant of a spike).
if [ -n "${SPIKE_SETTINGS:-}" ]; then BOB+=(--settings "$SPIKE_SETTINGS"); fi

case "$PLATFORM" in
  macos)
    ENGINE="$TOOLCHAIN/dmengine"
    if [ -n "${SPIKE_NATIVE:-}" ]; then
      # The engine with the project's native extensions (native/), from the build server.
      "${BOB[@]}" --output "$OUT/macos-build" --build-server "${BUILD_SERVER:-https://build.defold.com}" \
        --binary-output "$OUT/macos-engine" --platform arm64-macos --variant debug build >/dev/null
      ENGINE="$OUT/macos-engine/arm64-osx/dmengine"
      chmod +x "$ENGINE"
    else
      "${BOB[@]}" --output "$OUT/macos-build" --variant debug build >/dev/null
    fi
    set +e
    "$ENGINE" "$OUT/macos-build/game.projectc" >"$OUT/macos.raw.log" 2>&1 &
    pid=$!
    for _ in $(seq "$TIMEOUT"); do kill -0 "$pid" 2>/dev/null || break; sleep 1; done
    kill "$pid" 2>/dev/null
    wait "$pid"
    set -e
    grep -o "SPIKE .*" "$OUT/macos.raw.log" | tee "$OUT/macos.log"
    ;;
  web)
    "${BOB[@]}" --output "$OUT/web-build" --archive --platform wasm-web --architectures wasm-web \
      --variant "${SPIKE_VARIANT:-release}" --bundle-output "$BUNDLE/web" resolve build bundle >/dev/null
    page="$(find "$BUNDLE/web" -name index.html | head -1)"
    dir="$(dirname "$page")"
    port="${SPIKE_PORT:-8790}"
    python3 -m http.server "$port" -d "$dir" >/dev/null 2>&1 &
    server=$!
    # SPIKE_PROFILE keeps the browser profile between runs (persistence checks).
    profile="${SPIKE_PROFILE:-$(mktemp -d)}"
    chrome="${CHROME:-/Applications/Google Chrome.app/Contents/MacOS/Google Chrome}"
    "$chrome" --headless=new --use-angle=metal --enable-logging=stderr --v=0 --no-first-run \
      --user-data-dir="$profile" --window-size=1280,720 "http://localhost:$port/index.html" \
      >"$OUT/web.raw.log" 2>&1 &
    browser=$!
    for _ in $(seq "$TIMEOUT"); do grep -q "SPIKE done" "$OUT/web.raw.log" && break; sleep 1; done
    kill "$browser" "$server" 2>/dev/null || true
    wait "$browser" "$server" 2>/dev/null || true
    [ -n "${SPIKE_PROFILE:-}" ] || rm -rf "$profile"
    grep -o '"SPIKE [^"]*' "$OUT/web.raw.log" | sed 's/^"//' | tee "$OUT/web.log"
    ;;
  android)
    "${BOB[@]}" --output "$OUT/android-build" --archive --platform arm64-android --architectures arm64-android \
      --variant debug --bundle-output "$BUNDLE/android" resolve build bundle >/dev/null
    apk="$(find "$BUNDLE/android" -name '*.apk' | head -1)"
    package="$PACKAGE"
    adb install -r "$apk" >/dev/null
    adb logcat -c
    # A physical device may have dozed off since the last run; a sleeping screen pauses the engine.
    adb shell input keyevent KEYCODE_WAKEUP
    adb shell am start -n "$package/com.dynamo.android.DefoldActivity" >/dev/null
    for _ in $(seq "$TIMEOUT"); do adb logcat -d | grep -q "SPIKE done" && break; sleep 1; done
    adb logcat -d >"$OUT/$LOG.raw.log"
    grep -o "SPIKE .*" "$OUT/$LOG.raw.log" | tee "$OUT/$LOG.log"
    adb shell am force-stop "$package"
    ;;
  *) echo "unknown platform $PLATFORM" >&2; exit 1 ;;
esac
