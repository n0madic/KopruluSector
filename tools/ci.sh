#!/usr/bin/env bash
# Local/CI check (plan phase S, ADR 0012): the converter's tests, the goldens' currency, the native build
# (CMakeLists.txt: kp_core, kp_present, kp_app_core, the tools and the C suites, checked as C and C++), the C suites
# on the host, under ASan and UBSan (with an LLVM clang 17 or later: -fsanitize=function and leaks; else with the default
# compiler, whose build otherwise only checks the tools), and in wasm under node when emcmake is on the PATH (the HTML5
# proxy), the loaders over the converted data and the replays of every scenario.
#
# What keeps it short:
#   - The converter's tests run in the background, a process a module (tools/converter_tests.py), and join before the
#     product's checks, which keep the machine to themselves.
#   - The quick mode, the default: under the sanitizers and in wasm the loops that replay whole games take one item in
#     CI_SAMPLE (kp_t_mine_sampled, seeded from the binary, so another build checks others). The native build always
#     runs them all. KOPRULU_FULL=1 runs all of them everywhere: before a push, and at the end of a stage.
#   - A step that passed on the same binaries and data is not run again (build/ci-passed, tools/ci_cache.py), so a run
#     stopped late goes on where it stopped; a full pass also answers for the quick mode, not the other way round.
#     KOPRULU_NO_CACHE=1 runs everything. The product's checks always run: they depend on the machine's GPU and display.
# Optional:
#   KOPRULU_FULL=1 and KOPRULU_NO_CACHE=1 (above);
#   KOPRULU_CLANG=<clang 17 or later, not Apple's, with its clang++ beside it> for the LLVM sanitizer step (one that
#     does not qualify stops the run; default: Homebrew's llvm, or a clang on the PATH that qualifies; the step is
#     skipped without one);
#   KOPRULU_ORIGINAL=<dir with StarCraft.exe and the profile MPQs> enables the tests that read original data;
#   KOPRULU_SCR=<StarCraft: Remastered install dir> classifies the manifest's files on that install
#     (converter/tests/test_scr_source.py) and, with KOPRULU_ORIGINAL, compares Remastered's DAT tables and
#     scripts with the profile's (converter/tests/test_casc.py); with generated/data/ui.kpd it also converts the
#     install's languages (every check of converter locale on each it holds) and their speech (FFmpeg, every check
#     of converter speech), and KOPRULU_APP's product runs each language with its speech;
#   KOPRULU_APP=1 builds the product for macOS (tools/product.sh macos debug, needs generated/) and runs its
#     development checks, with no window on the screen and no sound (--headless, APP_WINDOW's size whatever the
#     display): the autotest (every mission's terrain against the converter's CRCs, and with
#     KOPRULU_ORIGINAL its composed views rebuilt on the CPU from the retail files, tools/viewer_compose_check.py)
#     the play checks (box selection, right clicks, the fog and the palette pass against their CPU twins) and the
#     video check (the in-game video options clicked through, the art following them), the save check (a save made
#     during a transmission, loaded in its game and from its file, shows the saved view and minimap in the load's frame
#     and its trigger wait ends), the touch check (terran01 played by synthetic fingers through the touch layer) and,
#     with the HD art imported,
#     the HD check (the glue's screens and each race's game screens and menus with no SD pixel where HD art exists);
#   KOPRULU_CINEMATICS=sd|hd2|both builds the cinematics' player over libvpx (fetched) and runs its suite (also under
#     ASan and UBSan) and, with KOPRULU_APP, builds the product with the packs from generated/video and runs the cine
#     check (every cinematic decoded to its end, the first pictures also drawn by the GPU); unset or none (the default
#     here, whatever product.sh's auto would take), the products are built without them;
#   KOPRULU_WEB=1 builds the product for the web (tools/product.sh web debug, needs emcmake);
#   KOPRULU_ANDROID=1 builds the APK and runs the play checks, the autotest and the frame benchmark on the attached,
#     unlocked phone: only for the changes that need a real phone (docs/phaseS.md, "Device runs");
#   KOPRULU_CAPTURES=<dir> with KOPRULU_ORIGINAL composes the full-game emulator's captured frames
#     (tools/research/sc_emulator/capture.py) and compares them with the original's screens, and its fog captures
#     under <dir>/fog (sc_emulator/fogcap.py) with Koprulu's fog (tools/fog_check.py);
#   KOPRULU_HUD=<dir> (the emulator's console runs with screen captures) checks kp_ui's console against them
#     (tools/ui_check.py) and replays their input through the interface (tools/input_check.py);
#   KOPRULU_INPUT=<dir> (the test scenarios' runs with the input log): the same input check over every scenario;
#     KOPRULU_SCENARIOS=<dir> (the scenarios' first state dumps, <dir>/<run>/states/s00001.state) gives each run's cheat
#     word: the emulator starts them with Ophelia's cheat on its command line, which the cheat_typed run's first word has;
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

CI_SAMPLE=4 # the quick mode's one item in this many
CI_PASSES_KEPT=32 # the keys build/ci-passed remembers for a step
CI_MODE=quick
if [ -n "${KOPRULU_FULL:-}" ]; then CI_MODE=full; fi
CONVERTER_TESTS="" # the background run's process, until it joins
# Waits for the converter's tests and prints their summary, or their failures.
join_converter_tests() {
  local pid="$CONVERTER_TESTS" status=0
  [ -n "$pid" ] || return 0
  CONVERTER_TESTS=""
  wait "$pid" || status=$?
  if [ "$status" -eq 0 ]; then
    tail -n 1 build/converter-tests.log
  else
    cat build/converter-tests.log >&2
  fi
  return "$status"
}
# A step's title; the converter's tests, if they have ended, report here, so that their failure stops the run early.
step() {
  if [ -n "$CONVERTER_TESTS" ] && ! kill -0 "$CONVERTER_TESTS" 2>/dev/null; then join_converter_tests; fi
  printf '\n== %s (%ss so far)\n' "$*" "$SECONDS"
}
# Their cleanup is awaited: a run started right after a stopped one must not find it still removing their temporary files.
trap 'if [ -n "$CONVERTER_TESTS" ]; then kill "$CONVERTER_TESTS" 2>/dev/null || true; wait "$CONVERTER_TESTS" 2>/dev/null || true; fi' EXIT
# The product's development checks under a time limit: a run that hangs (a driver, a stopped display link) fails the
# check instead of blocking CI (perl's alarm: macOS has no timeout(1)).
APP_TIMEOUT="${APP_TIMEOUT:-600}"
limited() { perl -e 'alarm shift; exec @ARGV or die "exec: $!"' "$APP_TIMEOUT" "$@"; }
# A step's command with its output in LOG: the log's last N lines are printed, or its last 40 when it fails.
logged() {
  local log="$1" n="$2"
  shift 2
  if "$@" >"$log" 2>&1; then
    tail -n "$n" "$log"
  else
    local status=$?
    tail -n 40 "$log" >&2
    return "$status"
  fi
}
# An app's development check under the time limit, in a hidden window of a set size (a shown one is fitted to the
# display, so its view would depend on the machine): its output in build/NAME.log (grepped by the step), its errors in
# build/NAME.err, printed when the app fails.
APP_WINDOW="${APP_WINDOW:-1280x720}"
app_check() {
  local name="$1"
  shift
  if limited "$@" --headless --window "$APP_WINDOW" >"build/$name.log" 2>"build/$name.err"; then
    return 0
  else
    local status=$?
    tail -n 40 "build/$name.err" >&2
    return "$status"
  fi
}
mkdir -p build

# the products' cinematics (product.sh's KOPRULU_CINEMATICS): only what this run asks for, none by default
CINEMATICS="${KOPRULU_CINEMATICS:-none}"
case "$CINEMATICS" in
  none | sd | hd2 | both) ;;
  *) echo "ci: KOPRULU_CINEMATICS is none, sd, hd2 or both" >&2; exit 2 ;;
esac

step "toolchain"
"$TOOLCHAIN/fetch.sh"
"$TOOLCHAIN/fetch.sh" astc >/dev/null

step "fixtures"
logged build/fixtures.log 0 "$PYTHON" -m converter.kpf.fixtures

step "converter tests start in the background (tools/converter_tests.py, build/converter-tests/)"
"$PYTHON" tools/converter_tests.py ${KOPRULU_NO_CACHE:+--no-cache} >build/converter-tests.log 2>&1 &
CONVERTER_TESTS=$!

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
"$PYTHON" tools/oracle/export_emulator_traces.py --check

step "kp_core's generated CRC-32 tables are current"
"$PYTHON" tools/native/crc32_table.py --check
step "native build (CMake): kp_core, its tools and the C suites, checked as C and C++"
cmake -S . -B build/cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -DKP_ASTC=ON >/dev/null
cmake --build build/cmake >/dev/null
if [ -n "${KOPRULU_SCR:-}" ] && [ -f generated/data/ui.kpd ] && [ -f generated/missions/index.txt ]; then
  # before the C suites: test_locale reads what this writes, and an unchanged language leaves the files as they are
  step "the languages' packs: every check of converter locale on each language the install holds (ADR 0016)"
  "$PYTHON" -m converter locale --scr "$KOPRULU_SCR" --locale all --out generated 2>&1 | tail -5
  # test_speech plays the campaign with what this writes (FFmpeg decodes the lines; the encoded ones are cached)
  step "the languages' speech: every check of converter speech on each language converted above (ADR 0016, stages L5, L6)"
  for strings in generated/locale/*/strings.kpd; do
    [ -f "$strings" ] || continue
    "$PYTHON" -m converter speech --scr "$KOPRULU_SCR" --locale "$(basename "$(dirname "$strings")")" --out generated 2>&1 | tail -1
  done
fi
step "kp_hd_phone: small packs through etc2 and astc, opened and read back"
KP_HD_PHONE_THREADS=2 build/cmake/kp_hd_phone --selftest build/cmake | tail -1
step "kp_replay: every scenario against the original's CRCs (the native tools)"
mkdir -p build/replays
for golden in tests/golden/oracle/scenarios/*.json; do
  name="$(basename "$golden" .json)"
  [ "$name" = index ] || echo "$name"
done | logged build/replays.log 3 xargs -P "$JOBS" -n 1 sh -c \
  'build/cmake/kp_oracle export_replay "$0" "build/replays/$0.kprp" && build/cmake/kp_replay . "build/replays/$0.kprp"'
# test_gfx compiles kp_gfx's shaders with glslang's validator, the only check of them before a context runs them: the
# native suites fail without it (KP_TEST_GLSLANG), the wasm ones under node, which cannot run it, skip it
if ! command -v glslangValidator >/dev/null; then
  echo "ci: glslangValidator (glslang) is needed for test_gfx's shader checks (brew install glslang)" >&2
  exit 2
fi
# The C suites of one build, unless they passed on the same binaries and data (their files' bytes: the suites read the
# goldens, the fixtures and the converted data, and the rest of what they test is in the binary).
#   c_suites LABEL RUNNER long|all "OTHER_BINARIES" "VERSIONS" [VAR=VALUE ...]
# LABEL names its memory under build/ci-passed; RUNNER is the kp_test that starts the jobs; `long` samples the long loops
# in the quick mode, `all` never; OTHER_BINARIES are those the jobs run instead of RUNNER; VERSIONS are what the binaries
# need that is no file of theirs; the rest is kp_test's environment.
c_suites_key() { # "BINARIES" "VERSIONS" "ENVIRONMENT"
  # shellcheck disable=SC2086 # the binaries' words
  "$PYTHON" tools/ci_cache.py key --content $1 tests/golden tests/fixtures --meta generated \
    --text "$2" "$3" "$(uname -sr)" "$(glslangValidator --version 2>/dev/null | head -n 1)"
}
c_suites() {
  local label="$1" runner="$2" sampling="$3" binaries="$2 $4" versions="$5" environment key mode=full memory
  shift 5
  environment="$*"
  memory="build/ci-passed/$label"
  key="$(c_suites_key "$binaries" "$versions" "$environment")"
  if [ "$sampling" = long ] && [ "$CI_MODE" = quick ]; then
    mode="quick-$CI_SAMPLE"
    set -- "$@" "KP_TEST_SAMPLE=$CI_SAMPLE" "KP_TEST_SEED=$((16#${key:0:7}))" # 7 hex digits: within kp_test's unsigned
  fi
  if [ -z "${KOPRULU_NO_CACHE:-}" ] && [ -f "$memory" ] &&
    { grep -qxF "$key full" "$memory" || grep -qxF "$key $mode" "$memory"; }; then
    echo "$label: passed earlier on these binaries and data"
    return 0
  fi
  env "$@" "$runner" -j "$JOBS" | tail -20
  if [ "$(c_suites_key "$binaries" "$versions" "$environment")" != "$key" ]; then
    echo "$label: its inputs changed during the run, this pass is not remembered"
    return 0
  fi
  mkdir -p build/ci-passed
  { tail -n "$((CI_PASSES_KEPT - 1))" "$memory" 2>/dev/null || true; echo "$key $mode"; } >"$memory.tmp"
  mv "$memory.tmp" "$memory"
}
step "C suites (kp_test)"
if [ -d generated/data ] && [ ! -f generated/data/console.kpd ]; then
  # the HUD's tests lay out over the console's pieces, which only a Remastered install has (ADR 0011)
  echo "ci: no generated/data/console.kpd (python3 -m converter hd --scr DIR): the HUD's tests in test_hud SKIP" >&2
fi
c_suites native build/cmake/kp_test all "" "" KP_TEST_GLSLANG=1
# A current LLVM clang (17 or later, not Apple's) for what the default compiler's sanitizers lack: -fsanitize=function
# (in `undefined` since clang 17, a call through a pointer of another function type) and LeakSanitizer on macOS.
# KOPRULU_CLANG=<clang> names one; else Homebrew's llvm, else a clang on the PATH that qualifies.
llvm_clang_ok() {
  [ -x "$1" ] || return 1
  local macros
  macros="$("$1" -dM -E -x c /dev/null 2>/dev/null)" || return 1
  ! grep -q '__apple_build_version__' <<<"$macros" &&
    [ "$(sed -n 's/^#define __clang_major__ //p' <<<"$macros")" -ge 17 ] 2>/dev/null
}
# The C++ compiler beside it: clang -> clang++, clang-18 -> clang++-18.
llvm_clangxx() { local base="${1##*/}"; printf '%s\n' "${1%"$base"}clang++${base#clang}"; }
LLVM_CLANG=""
if [ -n "${KOPRULU_CLANG:-}" ]; then
  LLVM_CLANG="$(command -v "$KOPRULU_CLANG" || true)"
  if [ -z "$LLVM_CLANG" ] || ! llvm_clang_ok "$LLVM_CLANG" || [ ! -x "$(llvm_clangxx "$LLVM_CLANG")" ]; then
    echo "ci: KOPRULU_CLANG=$KOPRULU_CLANG is not an LLVM clang 17 or later with its clang++ beside it" >&2
    exit 2
  fi
else
  for candidate in /opt/homebrew/opt/llvm/bin/clang /usr/local/opt/llvm/bin/clang "$(command -v clang || true)"; do
    if [ -n "$candidate" ] && llvm_clang_ok "$candidate" && [ -x "$(llvm_clangxx "$candidate")" ]; then
      LLVM_CLANG="$candidate"
      break
    fi
  done
fi
# The default compiler's sanitizer build: the suites only without an LLVM clang, whose build below checks more; the
# ASTC encoder's half of kp_hd_phone and the loaders over the data always.
cmake -S . -B build/cmake-asan -G Ninja -DKP_SANITIZE=ON -DKP_CXX_CHECK=OFF -DKP_ASTC=ON >/dev/null
if [ -n "$LLVM_CLANG" ]; then
  step "the tools under ASan and UBSan (the C suites run in the LLVM build below)"
  cmake --build build/cmake-asan --target kp_hd_phone kp_load_check >/dev/null
else
  step "C suites under ASan and UBSan (no clang 17 or later for -fsanitize=function and leaks; KOPRULU_CLANG=<clang>)"
  cmake --build build/cmake-asan >/dev/null
  c_suites asan build/cmake-asan/kp_test long "" "$(cc --version | head -n 1)" \
    KP_TEST_GLSLANG=1 KP_TEST_LOGS=build/c-tests-asan
fi
KP_HD_PHONE_THREADS=2 build/cmake-asan/kp_hd_phone --selftest build/cmake-asan | tail -1
if [ -n "$LLVM_CLANG" ]; then
  step "C suites under ASan, LSan and UBSan with $LLVM_CLANG (-fsanitize=function, leaks)"
  cmake -S . -B build/cmake-asan-llvm -G Ninja -DKP_SANITIZE=ON -DKP_CXX_CHECK=OFF \
    -DCMAKE_C_COMPILER="$LLVM_CLANG" -DCMAKE_CXX_COMPILER="$(llvm_clangxx "$LLVM_CLANG")" >/dev/null
  cmake --build build/cmake-asan-llvm >/dev/null
  # kp_test -j counts a job whose process fails after its tests passed (a leak report at exit) as a failure
  c_suites asan-llvm build/cmake-asan-llvm/kp_test long "" "$("$LLVM_CLANG" --version | head -n 1)" \
    ASAN_OPTIONS=detect_leaks=1:strict_string_checks=1:detect_stack_use_after_return=1:check_initialization_order=1 \
    KP_TEST_GLSLANG=1 KP_TEST_LOGS=build/c-tests-asan-llvm
  build/cmake-asan-llvm/kp_hd_phone --selftest build/cmake-asan-llvm | tail -1 # the tool without the ASTC encoder
fi
if [ -d generated/data ]; then
  step "kp_core loaders over all converted data under ASan and UBSan"
  # a language's packs and speech are partial: test_locale and test_speech read them as the overlays they are
  find generated \( -path generated/locale -o -path generated/speech \) -prune -o \( -name "*.kpd" -o -name "*.kpm" \) -print \
    | sort \
    | xargs build/cmake-asan/kp_load_check | tail -1
fi
if [ "$CINEMATICS" != none ]; then
  # the cinematics' player over libvpx (ADR 0015): the default build above has none of it
  step "cinematics: libvpx (fetched) and the player's suite, on the host and under ASan and UBSan"
  tools/toolchain/fetch.sh vpx >/dev/null
  cmake -S . -B build/cmake-cine -G Ninja -DKP_CINEMATICS=ON >/dev/null
  cmake --build build/cmake-cine >/dev/null
  c_suites cine build/cmake-cine/kp_test all "" "" KP_TEST_GLSLANG=1 KP_TEST_LOGS=build/c-tests-cine
  cmake -S . -B build/cmake-asan-cine -G Ninja -DKP_CINEMATICS=ON -DKP_SANITIZE=ON -DKP_CXX_CHECK=OFF >/dev/null
  cmake --build build/cmake-asan-cine >/dev/null
  c_suites asan-cine build/cmake-asan-cine/kp_test long "" "$(cc --version | head -n 1)" \
    KP_TEST_GLSLANG=1 KP_TEST_LOGS=build/c-tests-asan-cine
fi
if command -v emcmake >/dev/null && command -v node >/dev/null; then
  # the jobs are wasm's under node, their dispatch the host's runner
  step "C suites in wasm under node (the HTML5 proxy: the deterministic core in WebAssembly)"
  emcmake cmake -S . -B build/cmake-wasm -G Ninja -DCMAKE_BUILD_TYPE=Release >/dev/null
  cmake --build build/cmake-wasm >/dev/null
  c_suites wasm build/cmake/kp_test long "build/cmake-wasm/kp_test.js build/cmake-wasm/kp_test.wasm" \
    "$(node --version)" "KP_TEST_EXEC=node build/cmake-wasm/kp_test.js" KP_TEST_LOGS=build/c-tests-wasm
  if [ "$CINEMATICS" != none ]; then
    step "cinematics in wasm under node (libvpx from C, as the web's page has it)"
    emcmake cmake -S . -B build/cmake-wasm-cine -G Ninja -DCMAKE_BUILD_TYPE=Release -DKP_CINEMATICS=ON >/dev/null
    cmake --build build/cmake-wasm-cine >/dev/null
    node build/cmake-wasm-cine/kp_test.js test_cine | tail -20
  fi
else
  step "C suites in wasm: skipped (no emcmake or node on the PATH)"
fi
step "converter tests join (the product's checks keep the machine to themselves)"
join_converter_tests
# The development checks' passing lines (the app's output; on the phone after logcat's prefix).
AUTOTEST_OK='SPIKE autotest: [0-9]+ missions, [1-9][0-9]* megatiles checked, 0 mismatches; composed views: [1-9][0-9]*; load failures: 0$'
FOG_OK='SPIKE fog check: [1-9][0-9]* pixels, [0-9]+ darkened, 0 mismatches$'
PALETTE_OK='SPIKE palette check: [1-9][0-9]* pixels, 0 mismatches$'
CREEP_OK='SPIKE creep check: [0-9]{2,} creep tiles when the box was drawn, [0-9] now, 0 receding$'
VIDEO_OK='SPIKE video check: [0-9]+ steps, Cancel and OK, 0 failures$'
HD_OK='SPIKE hd check: [0-9]+ screens, 0 with SD over HD art$'
SAVE_OK='SPIKE save check: 2 loads, 0 failures$'
TOUCH_OK='SPIKE touch check: [0-9]+ steps, 0 failures$'
CINE_OK='SPIKE cine check: [0-9]+ movies, [1-9][0-9]* frames, 0 failures$'
PLAY_OK='SPIKE play check .*, running; .*native core; [0-9]+ sounds started, [0-9]+ streams, music track [0-9]+$'
TERRAIN_OK='SPIKE check [^ ]+ after the play: view [0-9]+x[0-9]+ at [0-9]+,[0-9]+, [1-9][0-9]* megatiles \([0-9]+ under creep.s edges\), 0 mismatches$'

if [ -n "${KOPRULU_APP:-}" ]; then
  unset SDL_AUDIO_DRIVER # --headless leaves the sound driver to the environment, and these checks run with no sound
  step "product (macOS, debug)"
  KOPRULU_CINEMATICS="$CINEMATICS" tools/product.sh macos debug >/dev/null
  APP=dist/product/macos/KopruluSector.app/Contents/MacOS/koprulu
  step "product autotest: every mission's terrain and composed views"
  app_check autotest "$APP" --autotest
  tail -1 build/autotest.log
  grep -qE "^$AUTOTEST_OK" build/autotest.log
  grep -q 'headless: window hidden, sound driver dummy' build/autotest.err
  if [ -n "${KOPRULU_ORIGINAL:-}" ]; then
    step "product composition against the CPU kernels"
    "$PYTHON" tools/viewer_compose_check.py --data "$KOPRULU_ORIGINAL" --log build/autotest.log \
      --missions "${KOPRULU_GENERATED:-generated}/missions" | tail -1
  fi
  for n in 1 2 3 4 5; do
    step "product play check $n: box selection, right clicks, the terrain, the fog and the palette pass"
    app_check play_check "$APP" --play-check "$n"
    grep -E "^SPIKE ((play|fog|palette|creep) check|check)" build/play_check.log
    grep -qE "^$PLAY_OK" build/play_check.log
    grep -qE "^$TERRAIN_OK" build/play_check.log
    grep -qE "^$FOG_OK" build/play_check.log
    grep -qE "^$PALETTE_OK" build/play_check.log
    # play check 5 kills a Creep Colony: its creep recedes (the pool's few tiles stay)
    if [ "$n" = 5 ]; then grep -qE "^$CREEP_OK" build/play_check.log; fi
  done
  step "product video check: the in-game video options clicked through (Cancel, OK) and the world's art following them"
  mkdir -p build/video_check
  app_check video_check "$APP" --mission zerg/zerg01 --play --video-check build/video_check
  grep -E "^SPIKE video check: " build/video_check.log
  grep -qE "^$VIDEO_OK" build/video_check.log
  step "product save check: a save loaded in its game and from its file shows the saved view and minimap at once, its trigger wait ends"
  mkdir -p build/save_check
  app_check save_check "$APP" --mission terran/terran01 --play --save-check build/save_check
  grep -E "^SPIKE save check" build/save_check.log
  grep -qE "^$SAVE_OK" build/save_check.log
  step "product touch check: the game played by synthetic fingers (a tap's selection and order, a box, the pad's group, a two-finger pan), then a layout's text keys"
  mkdir -p build/touch_check
  app_check touch_check "$APP" --mission terran/terran01 --play --touch-check build/touch_check
  grep -E "^SPIKE touch check" build/touch_check.log
  grep -qE "^$TOUCH_OK" build/touch_check.log
  # each converted language from the source tree's generated/ (the bundle holds none until stage L4): the menu and the walk's
  # in-game screens, none of them reaching a character with no glyph; ruRU also plays with its speech
  for strings in generated/locale/*/strings.kpd; do
    [ -f "$strings" ] || continue
    loc=$(basename "$(dirname "$strings")")
    step "product in $loc: the menu, a mission's screens, no character without a glyph (ADR 0016)"
    mkdir -p "build/locale_check/$loc"
    speech=""
    if [ -f "generated/speech/$loc/units.kpd" ]; then speech="--speech $loc"; fi
    app_check "locale_menu_$loc" "$APP" --data . --locale "$loc" --after 2 --screenshot "build/locale_check/$loc/menu.png"
    # shellcheck disable=SC2086 # $speech is two words or none
    app_check "locale_hd_$loc" "$APP" --data . --locale "$loc" $speech --mission terran/terran01 --play --hd-check "build/locale_check/$loc"
    if grep -hE "no glyph|language $loc:|speech $loc:|the base.s voices" "build/locale_menu_$loc.err" "build/locale_hd_$loc.err" \
      "build/locale_menu_$loc.log" "build/locale_hd_$loc.log"; then
      echo "ci: the product's language $loc: a character without a glyph, or a language's or speech's pack refused" >&2
      exit 1
    fi
    [ -s "build/locale_check/$loc/menu.png" ]
    ls "build/locale_check/$loc"/*.png | wc -l
  done
  if [ "$CINEMATICS" != none ]; then
    for variant in sd hd2; do
      [ -f "${KOPRULU_GENERATED:-generated}/video/cinematics-$variant.kpd" ] || continue
      case "$CINEMATICS" in both | "$variant") ;; *) continue ;; esac
      step "product cine check ($variant): every cinematic decoded to its end, frame counts as the pack's index"
      mkdir -p "build/cine_check/$variant"
      app_check cine_check "$APP" --cinematics "$variant" --cine-check "build/cine_check/$variant"
      grep -E "^SPIKE cine check: " build/cine_check.log
      grep -qE "^$CINE_OK" build/cine_check.log
    done
  fi
  if [ -d "${KOPRULU_GENERATED:-generated}/hd" ]; then
    for mission in glue terran/terran02 zerg/zerg02 protoss/protoss02; do
      step "product HD check on $mission: the screens and menus with no SD pixel where HD art exists"
      mkdir -p "build/hd_check/${mission#*/}"
      if [ "$mission" = glue ]; then # the glue's screens by clicks: the menus, a briefing, a victory, the score
        app_check hd_check "$APP" --hd-check build/hd_check/glue
      else
        app_check hd_check "$APP" --mission "$mission" --play --hd-check "build/hd_check/${mission#*/}"
      fi
      grep -E "^SPIKE hd check" build/hd_check.log | grep -vE ": [0-9]+ HD pixels; SD over HD art: 0 lost, 0 outside, 0 through, 0 waiting, 0 unreadable; 0 drawn outside the log; 0 log drops;" || true
      grep -qE "^$HD_OK" build/hd_check.log
    done
  fi
fi

if [ -n "${KOPRULU_WEB:-}" ]; then
  step "product (web, debug)"
  KOPRULU_CINEMATICS="$CINEMATICS" tools/product.sh web debug >/dev/null
fi

if [ -n "${KOPRULU_ANDROID:-}" ]; then
  # Only for the changes that need a real phone (docs/phaseS.md, "Device runs"): the APK on the attached phone,
  # unlocked; its play checks against their CPU twins, the autotest, the touch check and the frame benchmark.
  step "product (Android, debug) on the attached phone: play checks, autotest, touch check, frame benchmark"
  KOPRULU_CINEMATICS="$CINEMATICS" tools/product.sh android debug >/dev/null
  adb wait-for-device # a wireless connection may have dropped during the build
  adb install -r dist/product/android/KopruluSector.apk >/dev/null
  for n in 1 2 3 4 5; do
    tools/native/android_app.sh launch --play-check "$n" >build/android_check.log
    grep -E "SPIKE ((play|fog|palette|creep) check|check)" build/android_check.log | sed -E 's/^.*SPIKE /SPIKE /'
    grep -qE "$PLAY_OK" build/android_check.log
    grep -qE "$TERRAIN_OK" build/android_check.log
    grep -qE "$FOG_OK" build/android_check.log
    grep -qE "$PALETTE_OK" build/android_check.log
    if [ "$n" = 5 ]; then grep -qE "$CREEP_OK" build/android_check.log; fi
  done
  tools/native/android_app.sh launch --autotest >build/android_check.log
  grep -E "SPIKE autotest" build/android_check.log | sed -E 's/^.*SPIKE /SPIKE /'
  grep -qE "$AUTOTEST_OK" build/android_check.log
  # The touch check's screenshots stay on the phone, in the app's external files.
  adb shell mkdir -p "/sdcard/Android/data/com.koprulusector.game/files/touch_check"
  tools/native/android_app.sh launch --mission terran/terran01 --play \
    --touch-check /sdcard/Android/data/com.koprulusector.game/files/touch_check >build/android_check.log
  grep -E "SPIKE touch check" build/android_check.log | sed -E 's/^.*SPIKE /SPIKE /'
  grep -qE "$TOUCH_OK" build/android_check.log
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
  "$PYTHON" tools/input_check.py --root "$KOPRULU_INPUT" ${KOPRULU_SCENARIOS:+--states "$KOPRULU_SCENARIOS"} --jobs "$JOBS" | tail -1
fi

if [ -n "${KOPRULU_PLAYS:-}" ]; then
  step "fog of war and the minimap's fog against the fog passes captured in the campaign plays"
  "$PYTHON" tools/fog_check.py --root "$KOPRULU_PLAYS" | tail -1
fi

if [ -n "${KOPRULU_PLAYS_UI:-}" ] && [ -n "${KOPRULU_PLAYS:-}" ]; then
  step "the campaign plays' input through the interface: the same commands, releases and skips as the original's"
  "$PYTHON" tools/input_check.py --kind plays --root "$KOPRULU_PLAYS_UI" --states "$KOPRULU_PLAYS" --jobs "$JOBS" | tail -1
fi

if [ "$CI_MODE" = quick ]; then
  echo "ci: the quick mode: the long loops' 1 in $CI_SAMPLE under the sanitizers and in wasm; KOPRULU_FULL=1 runs them all"
fi
step "all checks passed"
