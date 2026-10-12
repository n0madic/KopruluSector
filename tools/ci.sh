#!/usr/bin/env bash
# Full local/CI check. Optional: KOPRULU_ORIGINAL=<dir with StarCraft.exe and the
# profile MPQs> enables the tests that read original data; KOPRULU_VIEWER=1 runs the map
# viewer's HTML5 autotest (needs generated/ and Chrome, a few minutes).
set -euo pipefail
cd "$(dirname "$0")/.."

TOOLCHAIN=tools/toolchain
JAVA="${JAVA:-java}"
PYTHON="${PYTHON:-python3}"
HEADLESS_TIMEOUT="${HEADLESS_TIMEOUT:-120}"

step() { printf '\n== %s (%ss so far)\n' "$*" "$SECONDS"; }

step "toolchain"
"$TOOLCHAIN/fetch.sh"

step "converter tests (pytest)"
"$PYTHON" -m pytest -q converter/tests

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

step "Lua suites: LuaJIT (JIT on)"
luajit tests/lua/run.lua
step "Lua suites: LuaJIT (JIT off, as on iOS)"
luajit -joff tests/lua/run.lua
step "Lua suites: PUC Lua 5.1 (as on HTML5)"
"$TOOLCHAIN/lua5.1" tests/lua/run.lua

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

if [ -n "${KOPRULU_VIEWER:-}" ]; then
  step "map viewer autotest (HTML5 terrain and sprite readback)"
  SPIKE_SETTINGS_FILE=viewer/viewer.settings SPIKE_SETTINGS=viewer/autotest.settings SPIKE_TIMEOUT=600 \
    tools/spike.sh viewer web >/dev/null
  grep -E "^SPIKE autotest" build/spikes/viewer/web.log
  grep -qE "^SPIKE autotest: [0-9]+ missions, [1-9][0-9]* megatiles checked, 0 mismatches; sprites: [1-9][0-9]* images .*, 0 mismatches$" \
    build/spikes/viewer/web.log
fi

step "all checks passed"
