#!/usr/bin/env bash
# The native step bench on an Android phone (plan §10, ADR 0008): builds native/kp_core/tools/kp_bench and kp_replay
# with the Android NDK for arm64, pushes them with the data, the scenario and its replay (kp_oracle export_replay from
# the scenario's golden, built on the host by `cmake --build build/cmake`) to /data/local/tmp/kp, checks the run
# against the original (kp_replay) and times it (kp_bench).
# Usage (repository root): tools/native/android_bench.sh [SCENARIO [RUNS]]   (default bench_200, 5 runs)
# ANDROID_NDK_HOME overrides the NDK (default: the newest under ~/Library/Android/sdk/ndk); ANDROID_SERIAL picks the
# device when adb sees several phones (one phone over wireless debugging may show up under several names: any will do).
# The phone's screen must be on (see the wakefulness check below).
set -euo pipefail
cd "$(dirname "$0")/../.."
SCENARIO="${1:-bench_200}"
RUNS="${2:-5}"
NDK="${ANDROID_NDK_HOME:-$(ls -d "$HOME"/Library/Android/sdk/ndk/* | sort -V | tail -1)}"
CC="$(ls "$NDK"/toolchains/llvm/prebuilt/*/bin/aarch64-linux-android26-clang | head -1)"
OUT=build/android
REMOTE=/data/local/tmp/kp
mkdir -p "$OUT" build/replays
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
# A dozing phone (screen off) suspends the CPU for 150-200 ms at a time: those pauses would pass for slow steps.
wakefulness="$(adb shell dumpsys power </dev/null | tr -d '\r' | sed -n 's/^ *mWakefulness=\([A-Za-z]*\)$/\1/p' | head -1)"
if [ "$wakefulness" != Awake ]; then
  echo "android_bench: the phone is ${wakefulness:-in an unknown power state}; turn its screen on" >&2
  exit 1
fi
CORE=native/kp_core
for tool in kp_bench kp_replay; do
  "$CC" -O2 -std=c99 -Wall -Wextra -Wshadow -Wconversion -Wno-sign-conversion -Werror -o "$OUT/$tool" \
    "$CORE"/src/core/*.c "$CORE/tools/kp_runner.c" "$CORE/tools/kp_json.c" "$CORE/tools/$tool.c"
done
cmake --build build/cmake --target kp_oracle >/dev/null
build/cmake/kp_oracle export_replay "$SCENARIO" "build/replays/$SCENARIO.kprp"
adb shell "mkdir -p $REMOTE/generated/data/tilesets $REMOTE/generated/scenarios $REMOTE/replays"
adb push "$OUT/kp_bench" "$OUT/kp_replay" "$REMOTE/" >/dev/null
adb push generated/data/dat.kpd generated/data/grpmeta.kpd generated/data/iscript.kpd generated/data/aiscript.kpd "$REMOTE/generated/data/" >/dev/null
adb push generated/data/tilesets/. "$REMOTE/generated/data/tilesets/" >/dev/null
adb push "generated/scenarios/$SCENARIO.kpm" "$REMOTE/generated/scenarios/" >/dev/null
adb push "build/replays/$SCENARIO.kprp" "$REMOTE/replays/" >/dev/null
adb shell "cd $REMOTE && chmod 755 kp_bench kp_replay && ./kp_replay . replays/$SCENARIO.kprp && ./kp_bench . replays/$SCENARIO.kprp $RUNS"
