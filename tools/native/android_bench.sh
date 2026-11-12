#!/usr/bin/env bash
# The native step bench on an Android phone (plan §10, ADR 0008): builds native/kp_core/tools/kp_bench and kp_replay
# with the Android NDK for arm64 (the repository's CMake over the NDK's toolchain: the tools read the original data
# through kp_res, ADR 0018), pushes them with the repack's core group (make repack), the scenario's CHK and its replay
# (kp_oracle export_replay from the scenario's golden, built on the host by `cmake --build build/cmake`) to
# /data/local/tmp/kp, checks the run against the original (kp_replay) and times it (kp_bench). With --glyphs it times
# the language's glyphs as each start rasterizes them (kp_bench --glyphs; ADR 0019, stage C3) over the core and the
# language's group of a repack of KOPRULU_SCR (the Remastered install) it writes into build/android-bench/glyphs.
# Usage (repository root): tools/native/android_bench.sh [SCENARIO [RUNS]]   (default bench_200, 5 runs)
#                          KOPRULU_SCR=DIR tools/native/android_bench.sh --glyphs LOCALE [RUNS]   (default 9 runs)
# ANDROID_NDK_HOME overrides the NDK (default: the newest under ~/Library/Android/sdk/ndk); ANDROID_SERIAL picks the
# device when adb sees several phones (one phone over wireless debugging may show up under several names: any will do).
# The script wakes the phone's screen just before the timed runs (see awake below); a locked phone stays locked.
set -euo pipefail
cd "$(dirname "$0")/../.."
GLYPHS=""
if [ "${1:-}" = --glyphs ]; then
  GLYPHS="${2:?usage: android_bench.sh --glyphs LOCALE [RUNS]}"
  RUNS="${3:-9}"
  : "${KOPRULU_SCR:?android_bench: --glyphs repacks KOPRULU_SCR, the Remastered install}"
else
  SCENARIO="${1:-bench_200}"
  RUNS="${2:-5}"
fi
NDK="${ANDROID_NDK_HOME:-$(ls -d "$HOME"/Library/Android/sdk/ndk/* | sort -V | tail -1)}"
OUT=build/android-bench
REMOTE=/data/local/tmp/kp
CLEAN="cd / && rm -rf $REMOTE" # what a run pushed is not kept on the phone (the data: 50 to 120 MB)
mkdir -p build/replays
if [ -z "$GLYPHS" ] && [ ! -f generated/repack/koprulu-core.mpq ]; then
  echo "android_bench: no generated/repack/koprulu-core.mpq (make repack)" >&2
  exit 1
fi
if [ -z "${ANDROID_SERIAL:-}" ]; then
  first="" phones=0 seen=" "
  while IFS=$'\t' read -r name state; do
    [ "$state" = device ] || continue
    phone="$(adb -s "$name" shell getprop ro.serialno </dev/null | tr -d '\r')" # adb shell would read the list
    case "$seen" in *" $phone "*) continue ;; esac
    seen="$seen$phone " phones=$((phones + 1))
    [ -n "$first" ] || first="$name"
  done < <(adb devices | tail -n +2)
  if [ "$phones" -ne 1 ]; then
    echo "android_bench: $phones phones connected; set ANDROID_SERIAL" >&2
    exit 1
  fi
  export ANDROID_SERIAL="$first"
fi
# A dozing phone (screen off) suspends the CPU for 150-200 ms at a time: those pauses would pass for slow steps. So the
# screen is woken right before the timed runs (the power key's KEYCODE_WAKEUP: the lock screen shows, the phone stays
# locked; it sleeps again by itself), and the runs are refused if the phone is not awake then.
awake() {
  local wakefulness
  adb shell input keyevent KEYCODE_WAKEUP </dev/null
  wakefulness="$(adb shell dumpsys power </dev/null | tr -d '\r' | sed -n 's/^ *mWakefulness=\([A-Za-z]*\)$/\1/p' | head -1)"
  if [ "$wakefulness" != Awake ]; then
    echo "android_bench: the phone is ${wakefulness:-in an unknown power state}; turn its screen on" >&2
    exit 1
  fi
}
cmake -S . -B "$OUT" -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-26 -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build "$OUT" --target kp_bench kp_replay >/dev/null
if [ -n "$GLYPHS" ]; then # the language's text and fonts: the core and the language's group of a repack, no HD
  cmake --build build/cmake --target kp_convert >/dev/null
  rm -rf "${OUT:?}/glyphs"
  build/cmake/kp_convert repack "$KOPRULU_SCR" --hd none --locales "$GLYPHS" --out "$OUT/glyphs" >/dev/null
  group="koprulu-locale-$(printf '%s' "$GLYPHS" | tr '[:upper:]' '[:lower:]').mpq"
  adb shell "rm -rf $REMOTE/glyphs && mkdir -p $REMOTE/glyphs"
  adb push "$OUT/kp_bench" "$REMOTE/" >/dev/null
  adb push "$OUT/glyphs/koprulu-core.mpq" "$OUT/glyphs/$group" "$REMOTE/glyphs/" >/dev/null
  rm -rf "${OUT:?}/glyphs"
  awake
  adb shell "cd $REMOTE && chmod 755 kp_bench && ./kp_bench --glyphs glyphs $GLYPHS $RUNS; rc=\$?; $CLEAN; exit \$rc"
  exit
fi
cmake --build build/cmake --target kp_oracle >/dev/null
build/cmake/kp_oracle export_replay "$SCENARIO" "build/replays/$SCENARIO.kprp"
adb shell "mkdir -p $REMOTE/generated/repack $REMOTE/generated/scenarios $REMOTE/replays"
adb push "$OUT/kp_bench" "$OUT/kp_replay" "$REMOTE/" >/dev/null
adb push generated/repack/koprulu-core.mpq "$REMOTE/generated/repack/" >/dev/null
adb push generated/scenarios/index.txt "generated/scenarios/$SCENARIO.chk" "$REMOTE/generated/scenarios/" >/dev/null
adb push "build/replays/$SCENARIO.kprp" "$REMOTE/replays/" >/dev/null
awake
adb shell "cd $REMOTE && chmod 755 kp_bench kp_replay && ./kp_replay . replays/$SCENARIO.kprp && ./kp_bench . replays/$SCENARIO.kprp $RUNS; rc=\$?; $CLEAN; exit \$rc"
