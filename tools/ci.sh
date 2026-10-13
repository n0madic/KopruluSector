#!/usr/bin/env bash
# Full local/CI check. Optional: KOPRULU_ORIGINAL=<dir with StarCraft.exe and the
# profile MPQs> enables the tests that read original data; KOPRULU_VIEWER=1 runs the map
# viewer's HTML5 autotest (needs generated/ and Chrome, a few minutes; with KOPRULU_ORIGINAL
# its composed views are also rebuilt on the CPU from the retail files); KOPRULU_CAPTURES=<dir>
# with KOPRULU_ORIGINAL composes the full-game emulator's captured frames
# (tools/research/sc_emulator/capture.py) and compares them with the original's screens.
# The project has native extensions (native/): every bob build of an engine goes through the
# Defold build server (BUILD_SERVER, default build.defold.com), so the run needs the network.
# KOPRULU_NATIVE_ENGINE=1 also runs the Defold tests on a headless engine with the extensions
# and, with KOPRULU_VIEWER, the viewer autotest on macOS (Vulkan) through native/kp_capture.
set -euo pipefail
cd "$(dirname "$0")/.."

TOOLCHAIN=tools/toolchain
JAVA="${JAVA:-java}"
PYTHON="${PYTHON:-python3}"
HEADLESS_TIMEOUT="${HEADLESS_TIMEOUT:-120}"

step() { printf '\n== %s (%ss so far)\n' "$*" "$SECONDS"; }

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
fi
step "emulator trace golden data is current"
(cd tools/oracle && "$PYTHON" export_emulator_traces.py --check)

step "native modules as Lua C modules (build/native), kp_core checked as C and C++"
SANITIZE=1 tools/native/build.sh
step "kp_core loaders over all converted data under ASan and UBSan"
find generated -path generated/spikes -prune -o \( -name "*.kpd" -o -name "*.kpm" \) -print | sort \
  | xargs build/native/kp_load_check | tail -1
step "Lua suites: LuaJIT (JIT on), native and Lua path search compared on every search"
KP_PATH=check luajit tests/lua/run.lua
if [ "$(uname -s)" = Darwin ]; then
  step "kp_core under ASan and UBSan: the C world under the image suites and the hybrid games"
  DYLD_INSERT_LIBRARIES="$(cc -print-file-name=libclang_rt.asan_osx_dynamic.dylib)" \
    LUA_CPATH="./build/native/asan/?.so;;" KP_CORE_REQUIRED=1 KP_CORE_SANITIZED=1 \
    luajit tests/lua/run.lua tests.lua.test_kp_core
fi
step "Lua suites: LuaJIT (JIT off, as on iOS), native path search"
luajit -joff tests/lua/run.lua
step "Lua suites: PUC Lua 5.1, Lua path search"
KP_PATH=lua "$TOOLCHAIN/lua5.1" tests/lua/run.lua

if [ -d generated/missions ]; then
  largest="$(ls -S generated/missions/*/*.kpm | head -1)"
  step "Lua heap of the largest mission ($largest)"
  luajit -joff tools/lua/mission_heap.lua "$largest"
  "$TOOLCHAIN/lua5.1" tools/lua/mission_heap.lua "$largest"
fi

step "bob build (game)"
"$JAVA" -jar "$TOOLCHAIN/bob.jar" --root . --variant debug build >/dev/null
step "bob build (tests) and headless Defold run"
"$JAVA" -jar "$TOOLCHAIN/bob.jar" --root . --output build/tests --settings tests/defold/tests.settings \
  --variant debug build >/dev/null
set +e
# The engine never exits if the test script dies before sys.exit; bound the run.
output="$(perl -e 'alarm shift; exec @ARGV' "$HEADLESS_TIMEOUT" "$TOOLCHAIN/dmengine_headless" build/tests/game.projectc 2>&1)"
status=$?
set -e
grep -E "SCRIPT|ERROR|FAIL" <<<"$output" || true
[ "$status" -eq 0 ]

if [ -n "${KOPRULU_NATIVE_ENGINE:-}" ]; then
  # The debug variant, as the viewer's: the build cache (.internal/cache) does not tell
  # engine variants apart.
  step "Defold tests on an engine with the native extensions"
  "$JAVA" -jar "$TOOLCHAIN/bob.jar" --root . --output build/tests --settings tests/defold/tests.settings \
    --build-server "${BUILD_SERVER:-https://build.defold.com}" --binary-output build/tests-engine \
    --platform arm64-macos --variant debug build >/dev/null
  chmod +x build/tests-engine/arm64-osx/dmengine
  set +e
  output="$(perl -e 'alarm shift; exec @ARGV' "$HEADLESS_TIMEOUT" build/tests-engine/arm64-osx/dmengine build/tests/game.projectc 2>&1)"
  status=$?
  set -e
  grep -E "SCRIPT|ERROR|FAIL" <<<"$output" || true
  [ "$status" -eq 0 ]
fi

if [ -n "${KOPRULU_VIEWER:-}" ]; then
  step "map viewer autotest (HTML5 terrain and sprite readback)"
  SPIKE_SETTINGS_FILE=viewer/viewer.settings SPIKE_SETTINGS=viewer/autotest.settings SPIKE_TIMEOUT=600 \
    tools/spike.sh viewer web >/dev/null
  grep -E "^SPIKE autotest" build/spikes/viewer/web.log
  grep -qE "^SPIKE autotest: [0-9]+ missions, [1-9][0-9]* megatiles checked, 0 mismatches; sprites: [1-9][0-9]* images .*, 0 mismatches; composed views: [1-9][0-9]*; load failures: 0$" \
    build/spikes/viewer/web.log
  if [ -n "${KOPRULU_ORIGINAL:-}" ]; then
    step "map viewer composition against the CPU kernels"
    "$PYTHON" tools/viewer_compose_check.py --data "$KOPRULU_ORIGINAL" | tail -1
  fi
  if [ -n "${KOPRULU_NATIVE_ENGINE:-}" ]; then
    step "map viewer autotest on macOS (Vulkan) with the native extensions"
    SPIKE_NATIVE=1 SPIKE_SETTINGS_FILE=viewer/viewer.settings SPIKE_SETTINGS=viewer/autotest_native.settings \
      SPIKE_TIMEOUT=900 tools/spike.sh viewer macos >/dev/null
    grep -E "^SPIKE autotest" build/spikes/viewer/macos.log
    grep -qE "^SPIKE autotest: [0-9]+ missions, [1-9][0-9]* megatiles checked, 0 mismatches; sprites: [1-9][0-9]* images .*, 0 mismatches; composed views: [1-9][0-9]*; load failures: 0$" \
      build/spikes/viewer/macos.log
    if [ -n "${KOPRULU_ORIGINAL:-}" ]; then
      step "macOS composition against the CPU kernels"
      "$PYTHON" tools/viewer_compose_check.py --data "$KOPRULU_ORIGINAL" --log build/spikes/viewer/macos.log | tail -1
    fi
  fi
  # After the composition check, which reads the autotest's web.log.
  step "map viewer play check (HTML5: box selection, right click, 140 game steps)"
  SPIKE_SETTINGS_FILE=viewer/viewer.settings SPIKE_SETTINGS=viewer/play_check.settings SPIKE_TIMEOUT=300 \
    tools/spike.sh viewer web >/dev/null
  grep -E "^SPIKE play check" build/spikes/viewer/web.log
  grep -qE "^SPIKE play check .*: 5 selected by the box, step 140, running; .*native path search on$" \
    build/spikes/viewer/web.log
fi

if [ -n "${KOPRULU_CAPTURES:-}" ] && [ -n "${KOPRULU_ORIGINAL:-}" ]; then
  step "draw list and kernels against the original renderer's captured frames"
  "$PYTHON" tools/compose_capture.py --data "$KOPRULU_ORIGINAL" --root "$KOPRULU_CAPTURES" | tail -1
fi

step "all checks passed"
