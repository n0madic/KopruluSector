#!/usr/bin/env bash
# Full local/CI check. Optional: KOPRULU_ORIGINAL=<dir with StarCraft.exe and the
# profile MPQs> enables the tests that read original data; KOPRULU_VIEWER=1 runs the map
# viewer's HTML5 autotest (needs generated/ and Chrome, a few minutes; with KOPRULU_ORIGINAL
# its composed views are also rebuilt on the CPU from the retail files); KOPRULU_CAPTURES=<dir>
# with KOPRULU_ORIGINAL composes the full-game emulator's captured frames
# (tools/research/sc_emulator/capture.py) and compares them with the original's screens, and its
# fog captures under <dir>/fog (sc_emulator/fogcap.py) with Koprulu's fog (tools/fog_check.py);
# KOPRULU_HUD=<dir> (the emulator's console runs with screen captures) checks kp_ui's console against them
# (tools/ui_check.py) and replays their input through the interface (tools/input_check.py), KOPRULU_INPUT=<dir> (the
# test scenarios' runs with the input log) the same input check over every scenario;
# KOPRULU_PLAYS=<dir> (the emulator's recorded campaign plays, `play ... --fog F`) checks the fog
# captures of every play under it the same way; with KOPRULU_PLAYS_UI=<dir> too (the plays re-recorded with the input
# log) every play's input replays through the interface (tools/input_check.py --kind plays, the cheat word from
# KOPRULU_PLAYS's first state dumps).
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
DATA_TIMEOUT="${DATA_TIMEOUT:-600}" # the Defold suites with the oracle suites (about 80 s on an M-series Mac)
# The Lua suites and the replays run in this many processes (tests/lua/run.lua -j: the long tests in shards).
JOBS="${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}"

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
step "native modules as Lua C modules (build/native), kp_core checked as C and C++"
SANITIZE=1 tools/native/build.sh
step "kp_core loaders over all converted data under ASan and UBSan"
find generated -path generated/spikes -prune -o \( -name "*.kpd" -o -name "*.kpm" \) -print | sort \
  | xargs build/native/kp_load_check | tail -1
step "kp_replay: every scenario against the original's CRCs (the native tools)"
mkdir -p build/replays
for golden in tests/golden/oracle/scenarios/*.lua; do
  name="$(basename "$golden" .lua)"
  [ "$name" = index ] || echo "$name"
done | xargs -P "$JOBS" -n 1 sh -c \
  'luajit tools/lua/export_replay.lua "$0" "build/replays/$0.kprp" && build/native/kp_replay . "build/replays/$0.kprp"' \
  | tail -3
step "Lua suites: LuaJIT (JIT on)"
luajit tests/lua/run.lua -j "$JOBS"
if [ "$(uname -s)" = Darwin ]; then
  step "Lua suites under ASan and UBSan: the native games, the C world, the bridge"
  asan_runtime="$(cc -print-file-name=libclang_rt.asan_osx_dynamic.dylib)"
  DYLD_INSERT_LIBRARIES="$asan_runtime" KP_TEST_PRELOAD="$asan_runtime" \
    LUA_CPATH="./build/native/asan/?.so;;" KP_CORE_REQUIRED=1 \
    luajit tests/lua/run.lua -j "$JOBS"
fi
step "Lua suites: LuaJIT (JIT off, as on iOS)"
luajit -joff tests/lua/run.lua -j "$JOBS"
step "Lua suites: PUC Lua 5.1"
"$TOOLCHAIN/lua5.1" tests/lua/run.lua -j "$JOBS"

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
  # With the converted data (the custom resources of game.project plus the generated data, scenarios and missions:
  # a settings key replaces, it does not extend) the oracle suites run on the extension's build of kp_core too; the
  # build has its own output, so the data never reaches the run on the engine without the extension.
  step "Defold tests on an engine with the native extensions"
  data_settings=""
  if [ -d generated/data ] && [ -d generated/missions ]; then
    base="$(sed -n 's/^custom_resources *= *//p' game.project)"
    printf '[project]\ncustom_resources = %s,/generated/data,/generated/scenarios,/generated/missions\n' "$base" \
      > build/tests_data.settings
    data_settings="--settings build/tests_data.settings"
  fi
  "$JAVA" -jar "$TOOLCHAIN/bob.jar" --root . --output build/tests-native --settings tests/defold/tests.settings \
    $data_settings --build-server "${BUILD_SERVER:-https://build.defold.com}" --binary-output build/tests-engine \
    --platform arm64-macos --variant debug build >/dev/null
  chmod +x build/tests-engine/arm64-osx/dmengine
  set +e
  output="$(perl -e 'alarm shift; exec @ARGV' "$DATA_TIMEOUT" build/tests-engine/arm64-osx/dmengine build/tests-native/game.projectc 2>&1)"
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
  grep -qE "^SPIKE play check .*: 5 selected by the box, step 140, running; .*native core; [0-9]+ sounds started, [0-9]+ streams, music track [0-9]+$" \
    build/spikes/viewer/web.log
  grep -E "^SPIKE fog check" build/spikes/viewer/web.log
  grep -qE "^SPIKE fog check: [1-9][0-9]* pixels, [1-9][0-9]* darkened, 0 mismatches$" build/spikes/viewer/web.log
  step "map viewer combat check (HTML5: bench_combat, 600 game steps, both sides lose units)"
  SPIKE_SETTINGS_FILE=viewer/viewer.settings SPIKE_SETTINGS=viewer/play_combat.settings SPIKE_TIMEOUT=300 \
    tools/spike.sh viewer web >/dev/null
  grep -E "^SPIKE play check" build/spikes/viewer/web.log
  grep -qE "^SPIKE play check scenarios/bench_combat: 12 selected by the box, step 600, running; .*units by owner 0: 100 -> [0-9]{1,2}, 1: 100 -> [0-9]{1,2}(, [0-9]+: [0-9]+ -> [0-9]+)*;.*native core; [0-9]+ sounds started, [0-9]+ streams, music track [0-9]+$" \
    build/spikes/viewer/web.log
  step "map viewer sound check (HTML5: terran01's music and its triggers' WAVs from the mission assets)"
  SPIKE_SETTINGS_FILE=viewer/viewer.settings SPIKE_SETTINGS=viewer/play_streams.settings SPIKE_TIMEOUT=300 \
    tools/spike.sh viewer web >/dev/null
  grep -E "^SPIKE play check" build/spikes/viewer/web.log
  grep -qE "^SPIKE play check terran/terran01: .*, step 600, running; .*native core; [0-9]+ sounds started, [1-9][0-9]* streams, music track 5$" \
    build/spikes/viewer/web.log
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
