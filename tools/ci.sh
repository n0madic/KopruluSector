#!/usr/bin/env bash
# Full local/CI check (plan phase S, ADR 0012): the converter's tests, the goldens' currency, the native build
# (CMakeLists.txt: kp_core, kp_present, kp_app_core, the tools and the C suites, checked as C and C++), the C suites
# on the host, under ASan and UBSan, and in wasm under node when emcmake is on the PATH (the HTML5 proxy), the
# loaders over the converted data and the replays of every scenario.
# Optional:
#   KOPRULU_ORIGINAL=<dir with StarCraft.exe and the profile MPQs> enables the tests that read original data;
#   KOPRULU_SCR=<StarCraft: Remastered install dir> with KOPRULU_ORIGINAL compares Remastered's DAT tables and
#     scripts with the profile's (converter/tests/test_casc.py);
#   KOPRULU_APP=1 builds the product for macOS (tools/product.sh macos debug, needs generated/) and runs its
#     development checks: the autotest (every mission's terrain against the converter's CRCs, and with
#     KOPRULU_ORIGINAL its composed views rebuilt on the CPU from the retail files, tools/viewer_compose_check.py)
#     and the play checks (box selection, right clicks, the fog and the palette pass against their CPU twins);
#   KOPRULU_WEB=1 builds the product for the web (tools/product.sh web debug, needs emcmake);
#   KOPRULU_ANDROID=1 builds the APK and runs the play checks, the autotest and the frame benchmark on the attached,
#     unlocked phone: only for the changes that need a real phone (docs/phaseS.md, "Device runs");
#   KOPRULU_CAPTURES=<dir> with KOPRULU_ORIGINAL composes the full-game emulator's captured frames
#     (tools/research/sc_emulator/capture.py) and compares them with the original's screens, and its fog captures
#     under <dir>/fog (sc_emulator/fogcap.py) with Koprulu's fog (tools/fog_check.py);
#   KOPRULU_HUD=<dir> (the emulator's console runs with screen captures) checks kp_ui's console against them
#     (tools/ui_check.py) and replays their input through the interface (tools/input_check.py);
#   KOPRULU_INPUT=<dir> (the test scenarios' runs with the input log): the same input check over every scenario;
#   KOPRULU_PLAYS=<dir> (the emulator's recorded campaign plays, `play ... --fog F`) checks the fog captures of every
#     play under it; with KOPRULU_PLAYS_UI=<dir> too (the plays re-recorded with the input log) every play's input
#     replays through the interface (tools/input_check.py --kind plays, the cheat word from KOPRULU_PLAYS's first
#     state dumps).
set -euo pipefail
cd "$(dirname "$0")/.."

TOOLCHAIN=tools/toolchain
PYTHON="${PYTHON:-python3}"
# The C suites and the replays run in this many processes (kp_test -j: the long tests in shards).
JOBS="${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}"

step() { printf '\n== %s (%ss so far)\n' "$*" "$SECONDS"; }
# The product's development checks under a time limit: a run that hangs (a driver, a stopped display link) fails the
# check instead of blocking CI (perl's alarm: macOS has no timeout(1)).
APP_TIMEOUT="${APP_TIMEOUT:-600}"
limited() { perl -e 'alarm shift; exec @ARGV or die "exec: $!"' "$APP_TIMEOUT" "$@"; }

step "toolchain"
"$TOOLCHAIN/fetch.sh"

step "converter tests (unittest)"
"$PYTHON" -m unittest discover -q -s converter/tests -t .

step "fixtures"
"$PYTHON" -m converter.kpf.fixtures >/dev/null 2>&1

if [ -d docs/research/evidence ]; then
  step "golden vectors are current"
  "$PYTHON" tools/oracle/export_leaf_vectors.py --check
  step "M0E golden manifest is current"
  "$PYTHON" tools/oracle/export_m0e_cases.py --check
  step "requirement golden cases are current"
  "$PYTHON" tools/oracle/export_requirement_cases.py --check
  step "AIScript VM golden cases are current"
  "$PYTHON" tools/oracle/export_aiscript_cases.py --check
  step "campaign chain golden data is current"
  "$PYTHON" tools/oracle/export_campaign_chains.py --check
  step "UI timer golden data is current (where the emulator's input logs are)"
  "$PYTHON" tools/oracle/export_ui_timers.py --check
  step "text engine golden cases are current"
  "$PYTHON" tools/oracle/export_text_cases.py --check
  step "UI panel golden data is current (where the emulator's screen captures are)"
  "$PYTHON" tools/oracle/export_ui_panels.py --check
  step "console panel golden data is current (where the emulator's screen captures are)"
  "$PYTHON" tools/oracle/export_ui_hud.py --check
fi
step "campaign compatibility matrix is current (docs/compatibility.md)"
"$PYTHON" tools/compatibility.py --check
step "emulator trace golden data is current"
(cd tools/oracle && "$PYTHON" export_emulator_traces.py --check)

step "kp_core's generated CRC-32 tables are current"
"$PYTHON" tools/native/crc32_table.py --check
step "native build (CMake): kp_core, its tools and the C suites, checked as C and C++"
cmake -S . -B build/cmake -G Ninja -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build build/cmake >/dev/null
step "kp_replay: every scenario against the original's CRCs (the native tools)"
mkdir -p build/replays
for golden in tests/golden/oracle/scenarios/*.json; do
  name="$(basename "$golden" .json)"
  [ "$name" = index ] || echo "$name"
done | xargs -P "$JOBS" -n 1 sh -c \
  'build/cmake/kp_oracle export_replay "$0" "build/replays/$0.kprp" && build/cmake/kp_replay . "build/replays/$0.kprp"' \
  | tail -3
step "C suites (kp_test)"
build/cmake/kp_test -j "$JOBS" | tail -20
step "C suites under ASan and UBSan"
cmake -S . -B build/cmake-asan -G Ninja -DKP_SANITIZE=ON -DKP_CXX_CHECK=OFF >/dev/null
cmake --build build/cmake-asan >/dev/null
KP_TEST_LOGS=build/c-tests-asan build/cmake-asan/kp_test -j "$JOBS" | tail -20
if [ -d generated/data ]; then
  step "kp_core loaders over all converted data under ASan and UBSan"
  find generated \( -name "*.kpd" -o -name "*.kpm" \) -print | sort | xargs build/cmake-asan/kp_load_check | tail -1
fi
if command -v emcmake >/dev/null && command -v node >/dev/null; then
  # the jobs are wasm's under node, their dispatch the host's runner
  step "C suites in wasm under node (the HTML5 proxy: the deterministic core in WebAssembly)"
  emcmake cmake -S . -B build/cmake-wasm -G Ninja -DCMAKE_BUILD_TYPE=Release >/dev/null
  cmake --build build/cmake-wasm >/dev/null
  KP_TEST_EXEC="node build/cmake-wasm/kp_test.js" KP_TEST_LOGS=build/c-tests-wasm build/cmake/kp_test -j "$JOBS" | tail -20
else
  step "C suites in wasm: skipped (no emcmake or node on the PATH)"
fi
# The development checks' passing lines (the app's output; on the phone after logcat's prefix).
AUTOTEST_OK='SPIKE autotest: [0-9]+ missions, [1-9][0-9]* megatiles checked, 0 mismatches; composed views: [1-9][0-9]*; load failures: 0$'
FOG_OK='SPIKE fog check: [1-9][0-9]* pixels, [0-9]+ darkened, 0 mismatches$'
PALETTE_OK='SPIKE palette check: [1-9][0-9]* pixels, 0 mismatches$'

if [ -n "${KOPRULU_APP:-}" ]; then
  step "product (macOS, debug)"
  tools/product.sh macos debug >/dev/null
  APP=dist/product/macos/KopruluSector.app/Contents/MacOS/koprulu
  step "product autotest: every mission's terrain and composed views"
  limited "$APP" --autotest >build/autotest.log 2>/dev/null
  tail -1 build/autotest.log
  grep -qE "^$AUTOTEST_OK" build/autotest.log
  if [ -n "${KOPRULU_ORIGINAL:-}" ]; then
    step "product composition against the CPU kernels"
    "$PYTHON" tools/viewer_compose_check.py --data "$KOPRULU_ORIGINAL" --log build/autotest.log | tail -1
  fi
  for n in 1 2 3 4; do
    step "product play check $n: box selection, right clicks, the fog and the palette pass"
    limited "$APP" --play-check "$n" >build/play_check.log 2>/dev/null
    grep -E "^SPIKE (play|fog|palette) check" build/play_check.log
    grep -qE "^SPIKE play check .*, running; .*native core; [0-9]+ sounds started, [0-9]+ streams, music track [0-9]+$" \
      build/play_check.log
    grep -qE "^$FOG_OK" build/play_check.log
    grep -qE "^$PALETTE_OK" build/play_check.log
  done
fi

if [ -n "${KOPRULU_WEB:-}" ]; then
  step "product (web, debug)"
  tools/product.sh web debug >/dev/null
fi

if [ -n "${KOPRULU_ANDROID:-}" ]; then
  # Only for the changes that need a real phone (docs/phaseS.md, "Device runs"): the APK on the attached phone,
  # unlocked; its play checks against their CPU twins, the autotest and the frame benchmark.
  step "product (Android, debug) on the attached phone: play checks, autotest, frame benchmark"
  tools/product.sh android debug >/dev/null
  adb wait-for-device # a wireless connection may have dropped during the build
  adb install -r dist/product/android/KopruluSector.apk >/dev/null
  for n in 1 2 3 4; do
    tools/native/android_app.sh launch --play-check "$n" >build/android_check.log
    grep -E "SPIKE (play|fog|palette) check" build/android_check.log | sed -E 's/^.*SPIKE /SPIKE /'
    grep -qE "$FOG_OK" build/android_check.log
    grep -qE "$PALETTE_OK" build/android_check.log
  done
  tools/native/android_app.sh launch --autotest >build/android_check.log
  grep -E "SPIKE autotest" build/android_check.log | sed -E 's/^.*SPIKE /SPIKE /'
  grep -qE "$AUTOTEST_OK" build/android_check.log
  for m in scenarios/bench_combat terran/terran01; do
    tools/native/android_app.sh launch --mission "$m" --bench-frames 600 --no-vsync >build/android_check.log
    grep -E "SPIKE frame bench" build/android_check.log | sed -E 's/^.*SPIKE /SPIKE /'
    grep -qE "SPIKE frame bench $m: 600 frames " build/android_check.log
  done
fi

if [ -n "${KOPRULU_CAPTURES:-}" ] && [ -n "${KOPRULU_ORIGINAL:-}" ]; then
  step "draw list and kernels against the original renderer's captured frames"
  "$PYTHON" tools/compose_capture.py --data "$KOPRULU_ORIGINAL" --root "$KOPRULU_CAPTURES" | tail -1
  if [ -d "$KOPRULU_CAPTURES/fog" ]; then
    step "fog of war and the minimap's fog against the original's captured fog passes"
    "$PYTHON" tools/fog_check.py --root "$KOPRULU_CAPTURES/fog" | tail -1
  fi
fi

if [ -n "${KOPRULU_HUD:-}" ]; then
  step "the console's panels and text lines against the original's captured screens"
  "$PYTHON" tools/ui_check.py --root "$KOPRULU_HUD" | tail -1
  step "the interface's selections and commands from the console runs' input against the original's"
  "$PYTHON" tools/input_check.py --root "$KOPRULU_HUD" --jobs "$JOBS" | tail -1
fi

if [ -n "${KOPRULU_INPUT:-}" ]; then
  step "the interface's selections, commands and sounds from the test scenarios' input against the original's"
  "$PYTHON" tools/input_check.py --root "$KOPRULU_INPUT" --jobs "$JOBS" | tail -1
fi

if [ -n "${KOPRULU_PLAYS:-}" ]; then
  step "fog of war and the minimap's fog against the fog passes captured in the campaign plays"
  "$PYTHON" tools/fog_check.py --root "$KOPRULU_PLAYS" | tail -1
fi

if [ -n "${KOPRULU_PLAYS_UI:-}" ] && [ -n "${KOPRULU_PLAYS:-}" ]; then
  step "the campaign plays' input through the interface: the same commands, releases and skips as the original's"
  "$PYTHON" tools/input_check.py --kind plays --root "$KOPRULU_PLAYS_UI" --states "$KOPRULU_PLAYS" --jobs "$JOBS" | tail -1
fi

step "all checks passed"
