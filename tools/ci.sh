#!/usr/bin/env bash
# Local/CI check (plan phase S, ADR 0012): the Python tests, the goldens' currency, the native build
# (CMakeLists.txt: kp_core, kp_present, kp_app_core, the tools and the C suites, checked as C and C++), the C suites
# on the host, under ASan and UBSan (with an LLVM clang 17 or later: -fsanitize=function and leaks; else with the default
# compiler, whose build otherwise only checks the tools), and in wasm under node when emcmake is on the PATH (the HTML5
# proxy), the loaders over the converted data and the replays of every scenario.
#
# What keeps it short:
#   - The Python tests (the research's readers' and the tools') run in the background, a process a module
#     (tools/python_tests.py), and join before the product's checks, which keep the machine to themselves.
#   - The quick mode, the default: under the sanitizers and in wasm the loops that replay whole games take one item in
#     CI_SAMPLE (kp_t_mine_sampled, seeded from the binary, so another build checks others). The native build always
#     runs them all. KOPRULU_FULL=1 runs all of them everywhere: before a push, and at the end of a stage.
#   - A step that passed on the same binaries and data is not run again (build/ci-passed, tools/ci_cache.py), so a run
#     stopped late goes on where it stopped; a full pass also answers for the quick mode, not the other way round.
#     KOPRULU_NO_CACHE=1 runs everything. The product's checks always run: they depend on the machine's GPU and display.
# Optional:
#   KOPRULU_PUBLIC=1 (the mode is on by itself without tests/golden/oracle, see below);
#   KOPRULU_FULL=1 and KOPRULU_NO_CACHE=1 (above);
#   KOPRULU_CLANG=<clang 17 or later, not Apple's, with its clang++ beside it> for the LLVM sanitizer step (one that
#     does not qualify stops the run; default: Homebrew's llvm, or a clang on the PATH that qualifies; the step is
#     skipped without one);
#   KOPRULU_ORIGINAL=<dir with StarCraft.exe and the profile MPQs> enables the tests that read original data,
#     kp_convert verify's read of every manifest file through StormLib (ADR 0018; KOPRULU_SCR: kp_convert digests'
#     through CascLib, each file's status and SHA-256 equal to tools/research/scr_digests.py's, the Remastered rebuilds
#     included) and kp_convert's test scenarios and terrain checks written from the install into generated/ (from
#     KOPRULU_SCR without it; ADR 0019), and with FFmpeg checks the golden of its cinematics' decodes
#     (tools/oracle/export_smk_ffmpeg.py --check);
#   KOPRULU_SCR=<StarCraft: Remastered install dir> classifies the manifest's files on that install
#     (scdata/tests/test_scr_source.py) and, with KOPRULU_ORIGINAL, compares Remastered's DAT tables and
#     scripts with the profile's (scdata/tests/test_casc.py); it also checks every string of each language the
#     install holds (kp_convert locale-check, its reports in generated/reports; ADR 0019, stage C3) and builds ruRU's
#     glyphs with the device bench (kp_bench --glyphs, tools/native/android_bench.sh --glyphs on a phone), test_locale and
#     test_speech build the languages from the install's files (stages C2 and C3), and KOPRULU_APP's product runs each
#     language with its speech, from the install and from its repack's language groups;
#   KOPRULU_APP=1 builds the product for macOS (tools/product.sh macos debug, needs generated/ and its repack,
#     generated/repack-single: kp_convert repack DIR --single; ADR 0018, stage D8b) and runs its
#     development checks, with no window on the screen and no sound (--headless, APP_WINDOW's size whatever the
#     display): the autotest (every mission's terrain against the converter's CRCs, and with
#     KOPRULU_ORIGINAL its composed views rebuilt on the CPU from the retail files, tools/viewer_compose_check.py),
#     the smoke scripts (--script's exit codes 0 to 3, tests/scripts/smoke_*.kps, ADR 0017; the video dialog's tree and
#     the game's state written by tests/scripts/video_menu.kps, a failure's tree and state, the fixed clock's two runs
#     of tests/scripts/determinism.kps the same and its negative control different) and the checks' scripts: the play
#     checks (play_1..5.kps: box selection, right clicks, the terrain, the fog and the palette pass against their CPU
#     twins), the video options (video.kps: clicked through, the art following them), a save (save.kps: made during a
#     transmission, loaded in its game and from its file, the saved view and minimap in the load's frame and its
#     trigger wait ending), the touch layer (touch.kps: terran01 played by synthetic fingers) and, with KOPRULU_SCR,
#     the HD art (hd_glue.kps, hd_game.kps on each race: the glue's screens and the game's screens and menus with no SD
#     pixel where HD art exists, over Remastered's install, the world's packs laid out from its anims and the
#     interface's frames by the placement table, and over its repack's sets and interface groups; each language's walk
#     on terran01 too); with KOPRULU_ORIGINAL or KOPRULU_SCR the install's repack
#     (stage D8, kp_convert repack into build/repack: the 1.17 install's groups, Remastered's one koprulu.mpq and its
#     HD sets' groups, stage C5: their packs, tilesets and water the Python converter's), the scripts over it writing what they write over the
#     install and over the bundle's repack, and with a mission's group taken out the same over the install beside it,
#     the mission refused with none; and the cine check over the install's own movies and over its repack's (stage D7:
#     the 1.17 install's Smacker files; Remastered's SD WebM and their subtitles, its HD2 set too when
#     KOPRULU_CINEMATICS asks; ADR 0019, stage C4: the repack's koprulu-cinematics*.mpq, the 1.17 movies transcoded
#     into WebM, Remastered's as they are with a language's subtitles from its group);
#   KOPRULU_CINEMATICS=sd|hd2|both, with KOPRULU_APP, builds the product with the cinematics' groups of its repack
#     (generated/repack-single/koprulu-cinematics*.mpq) and runs the cine check (every cinematic decoded to its end,
#     the first pictures also drawn by the GPU); unset or none (the default here, whatever product.sh's auto would
#     take), the products are built without them (the player and its suite, over libvpx, are in every build);
#   KOPRULU_WEB=1 builds the product for the web (tools/product.sh web debug, needs emcmake); with KOPRULU_SCR also a
#     web bundle over Remastered's repack in groups (kp_convert repack --hd hd2 --locales ruRU into build/web_scr), run
#     in a headless Chrome (tools/web_run.py; KOPRULU_CHROME names another): ruRU's group fetched whole before the
#     glue starts in it, the first start's movie fetched by its range and decoded to its end, and hd_game.kps on
#     terran02 over the HD2 art's pages, frames and portraits fetched by their ranges;
#   KOPRULU_ANDROID=1 builds the APK and runs the play scripts, the autotest, the touch script and the frame benchmark
#     on the attached, unlocked phone: only for the changes that need a real phone (docs/phaseS.md, "Device runs");
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

# The public mode: tests/golden/ is not in the repository (it is recorded from the original game and not published,
# docs/golden-oracle.md), so a fresh clone, hosted CI included, has none. The steps that read it are left out and
# kp_test skips its tests that need a golden (KP_TEST_PUBLIC); everything else runs. KOPRULU_PUBLIC=1 forces the mode.
PUBLIC=""
if [ -n "${KOPRULU_PUBLIC:-}" ] || [ ! -d tests/golden/oracle ]; then
  PUBLIC=1
  export KP_TEST_PUBLIC=1
  echo "ci: public mode (no tests/golden/oracle): the golden checks are skipped" >&2
fi

TOOLCHAIN=tools/toolchain
PYTHON="${PYTHON:-python3}"
# The C suites and the replays run in this many processes (kp_test -j: the long tests in shards).
JOBS="${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)}"

CI_SAMPLE=4 # the quick mode's one item in this many
CI_PASSES_KEPT=32 # the keys build/ci-passed remembers for a step
CI_MODE=quick
if [ -n "${KOPRULU_FULL:-}" ]; then CI_MODE=full; fi
PYTHON_TESTS="" # the background run's process, until it joins
# Waits for the Python tests and prints their summary, or their failures.
join_python_tests() {
  local pid="$PYTHON_TESTS" status=0
  [ -n "$pid" ] || return 0
  PYTHON_TESTS=""
  wait "$pid" || status=$?
  if [ "$status" -eq 0 ]; then
    tail -n 1 build/python-tests.log
  else
    cat build/python-tests.log >&2
  fi
  return "$status"
}
# A step's title; the Python tests, if they have ended, report here, so that their failure stops the run early.
step() {
  if [ -n "$PYTHON_TESTS" ] && ! kill -0 "$PYTHON_TESTS" 2>/dev/null; then join_python_tests; fi
  printf '\n== %s (%ss so far)\n' "$*" "$SECONDS"
}
# Their cleanup is awaited: a run started right after a stopped one must not find it still removing their temporary files.
trap 'if [ -n "$PYTHON_TESTS" ]; then kill "$PYTHON_TESTS" 2>/dev/null || true; wait "$PYTHON_TESTS" 2>/dev/null || true; fi' EXIT
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
# A script's run (--script, ADR 0017) as app_check runs a check, its exit code the one wanted (0 passed, 1 failed, 2
# refused, 3 the watchdog; a pass also wants its summary with 0 failures): its files in build/script/RUN, emptied
# first (a run removes nothing), its output in build/script_RUN.log, its last script line printed. RUN is the script's
# NAME, or SCRIPT_RUN's for a second run of one script with other flags.
app_script() {
  local name="$1" want="$2" run="${SCRIPT_RUN:-$1}" status=0
  shift 2
  rm -rf "build/script/${run:?}"
  limited "$@" --script "tests/scripts/$name.kps" --script-out "build/script/$run" --headless --window "$APP_WINDOW" \
    >"build/script_$run.log" 2>"build/script_$run.err" || status=$?
  grep -E "^SPIKE script" "build/script_$run.log" | tail -1 || true
  if [ "$status" != "$want" ]; then
    tail -n 40 "build/script_$run.err" >&2
    echo "ci: script $run: exit code $status, not $want" >&2
    return 1
  fi
  if [ "$want" = 0 ] && ! grep -qE "^SPIKE script $name: [0-9]+ steps, 0 failures\$" "build/script_$run.log"; then
    echo "ci: script $run: no summary with 0 failures" >&2
    return 1
  fi
}
# A live run's lines (--script-live, stage SC7) into its FIFO, open as fd 3; live_answer waits up to LIVE_WAIT seconds
# for a fixed string in its log (build/script_live.log), else ends the run (pid LIVE) and fails; a run that ended
# fails it at once.
LIVE_WAIT="${LIVE_WAIT:-60}"
live_send() { printf '%s\n' "$@" >&3; }
live_answer() {
  local i
  for i in $(seq 1 $((LIVE_WAIT * 10))); do
    if grep -qF "$1" build/script_live.log; then return 0; fi
    kill -0 "$LIVE" 2>/dev/null || break
    sleep 0.1
  done
  if grep -qF "$1" build/script_live.log; then return 0; fi # its last line before it ended
  kill "$LIVE" 2>/dev/null || true
  tail -n 20 build/script_live.log >&2
  echo "ci: live: no \"$1\" in the log" >&2
  return 1
}
mkdir -p build

# the products' cinematics (product.sh's KOPRULU_CINEMATICS): only what this run asks for, none by default
CINEMATICS="${KOPRULU_CINEMATICS:-none}"
case "$CINEMATICS" in
  none | sd | hd2 | both) ;;
  *) echo "ci: KOPRULU_CINEMATICS is none, sd, hd2 or both" >&2; exit 2 ;;
esac
unset KOPRULU_DIST # the products' checks run the bundles of dist/product

step "toolchain"
"$TOOLCHAIN/fetch.sh"
"$TOOLCHAIN/fetch.sh" astc >/dev/null

step "Python tests start in the background (tools/python_tests.py, build/python-tests/)"
"$PYTHON" tools/python_tests.py ${KOPRULU_NO_CACHE:+--no-cache} >build/python-tests.log 2>&1 &
PYTHON_TESTS=$!

if [ -z "$PUBLIC" ] && [ -d docs/research/evidence ]; then
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
if [ -z "$PUBLIC" ]; then
  step "emulator trace golden data is current"
  "$PYTHON" tools/oracle/export_emulator_traces.py --check
fi

step "kp_core's generated CRC-32 tables are current"
"$PYTHON" tools/native/crc32_table.py --check
step "kp_core's defs tables are current (tools/defs_c.py: the profile and its input manifest, ADR 0018)"
"$PYTHON" tools/defs_c.py --check
step "native build (CMake): kp_core, its tools and the C suites, checked as C and C++"
cmake -S . -B build/cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -DKP_ASTC=ON >/dev/null
cmake --build build/cmake >/dev/null
if [ -n "${KOPRULU_SCR:-}" ]; then
  # a string refused fails the step (its first ones printed); the product's runs below play the languages it lists
  step "the languages: every string of each language the install holds (kp_convert locale-check; ADR 0019, stage C3)"
  logged build/locale_check.log 8 build/cmake/kp_convert locale-check "$KOPRULU_SCR" --out generated
  # the device bench's glyphs (kp_bench --glyphs, as tools/native/android_bench.sh --glyphs times them on a phone)
  step "the glyphs' bench: ruRU's glyphs built from the install as each start builds them (kp_bench --glyphs)"
  logged build/glyphs_bench.log 1 build/cmake/kp_bench --glyphs "$KOPRULU_SCR" ruRU 3
  grep -qE '^kp_bench glyphs ruRU: 3 runs, [0-9]{3} characters; min ' build/glyphs_bench.log
  # the HD interface's placement table made again from the install is the committed one (ADR 0019, stage C6: a
  # Remastered update that changes its interface art fails here; the new table is kp_convert hd-ui-table's)
  step "the HD interface's placement table is the install's (kp_convert hd-ui-table; ADR 0019, stage C6)"
  build/cmake/kp_convert hd-ui-table "$KOPRULU_SCR" --out build/hd-ui-table.json | tail -3
  cmp build/hd-ui-table.json "$(ls defs/scr/hd-ui-*.json)"
fi
if [ -n "${KOPRULU_ORIGINAL:-}" ]; then
  step "data readers (ADR 0018): every file of the input manifest read from the 1.17 install through kp_res, checked"
  build/cmake/kp_convert verify "$KOPRULU_ORIGINAL" 2>/dev/null | tail -4
  if [ -z "$PUBLIC" ] && command -v ffmpeg >/dev/null; then
    # stage D7: test_movies decodes the install's Smacker movies (palette records, sound) as FFmpeg does
    step "the 1.17 cinematics' FFmpeg decodes are current (tools/oracle/export_smk_ffmpeg.py)"
    logged build/smk_ffmpeg_check.log 0 "$PYTHON" tools/oracle/export_smk_ffmpeg.py --data "$KOPRULU_ORIGINAL" --check
  fi
fi
if [ -n "${KOPRULU_SCR:-}" ]; then
  step "data readers (ADR 0018): the input manifest's files read from Remastered's CASC through kp_res"
  build/cmake/kp_convert digests "$KOPRULU_SCR" --out build/scr-digests-c.tsv 2>/dev/null | tail -4
  # the twin (stage D6): each file's status and bytes as scdata/casc/source.py makes them, the rebuilds included
  step "the same files read by the research's twin (tools/research/scr_digests.py): each status and SHA-256 equal to kp_res's"
  "$PYTHON" tools/research/scr_digests.py --scr "$KOPRULU_SCR" --out build/scr-digests-py.tsv 2>&1 | tail -1
  diff build/scr-digests-py.tsv build/scr-digests-c.tsv | head -20 # pipefail: a difference stops the run
fi
if [ -n "${KOPRULU_ORIGINAL:-}${KOPRULU_SCR:-}" ]; then
  # before the C suites and the autotest, which read them: a file that holds its bytes already is left as it is
  step "the test scenarios and the terrain checks from the install (kp_convert scenarios, terrain; ADR 0019)"
  build/cmake/kp_convert scenarios "${KOPRULU_ORIGINAL:-$KOPRULU_SCR}" --out generated 2>/dev/null | tail -1
  build/cmake/kp_convert terrain "${KOPRULU_ORIGINAL:-$KOPRULU_SCR}" --out generated 2>/dev/null | tail -1
fi
step "the phones' HD transcoding (kp_hd_transcode.c): small packs through etc2 and astc, opened and read back"
build/cmake/kp_hd_phone --selftest | tail -1
if [ -z "$PUBLIC" ]; then
  step "kp_replay: every scenario against the original's CRCs (the native tools)"
  mkdir -p build/replays
  for golden in tests/golden/oracle/scenarios/*.json; do
    name="$(basename "$golden" .json)"
    [ "$name" = index ] || echo "$name"
  done | logged build/replays.log 3 xargs -P "$JOBS" -n 1 sh -c \
    'build/cmake/kp_oracle export_replay "$0" "build/replays/$0.kprp" && build/cmake/kp_replay . "build/replays/$0.kprp"'
fi
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
# the installs test_res reads through kp_res (ADR 0018; empty: its tests of them skip)
RES_ORIGINAL="KP_TEST_ORIGINAL=${KOPRULU_ORIGINAL:-}"
RES_SCR="KP_TEST_SCR=${KOPRULU_SCR:-}"
step "C suites (kp_test)"
if [ -z "${KOPRULU_SCR:-}" ]; then
  # the HUD's tests lay out over the consoles' art, which only a Remastered install has (ADR 0011; kp_t_console_art)
  echo "ci: no KOPRULU_SCR: the HUD's tests of the console in test_hud SKIP" >&2
fi
c_suites native build/cmake/kp_test all "" "" KP_TEST_GLSLANG=1 "$RES_ORIGINAL" "$RES_SCR"
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
# phones' HD transcoding with the ASTC encoder (kp_hd_phone's selftest) and the loaders over the data always.
cmake -S . -B build/cmake-asan -G Ninja -DKP_SANITIZE=ON -DKP_CXX_CHECK=OFF -DKP_ASTC=ON >/dev/null
if [ -n "$LLVM_CLANG" ]; then
  step "the tools under ASan and UBSan (the C suites run in the LLVM build below)"
  cmake --build build/cmake-asan --target kp_hd_phone >/dev/null
else
  step "C suites under ASan and UBSan (no clang 17 or later for -fsanitize=function and leaks; KOPRULU_CLANG=<clang>)"
  cmake --build build/cmake-asan >/dev/null
  c_suites asan build/cmake-asan/kp_test long "" "$(cc --version | head -n 1)" \
    KP_TEST_GLSLANG=1 KP_TEST_LOGS=build/c-tests-asan "$RES_ORIGINAL" "$RES_SCR"
fi
build/cmake-asan/kp_hd_phone --selftest | tail -1
if [ -n "$LLVM_CLANG" ]; then
  step "C suites under ASan, LSan and UBSan with $LLVM_CLANG (-fsanitize=function, leaks)"
  cmake -S . -B build/cmake-asan-llvm -G Ninja -DKP_SANITIZE=ON -DKP_CXX_CHECK=OFF \
    -DCMAKE_C_COMPILER="$LLVM_CLANG" -DCMAKE_CXX_COMPILER="$(llvm_clangxx "$LLVM_CLANG")" >/dev/null
  cmake --build build/cmake-asan-llvm >/dev/null
  # kp_test -j counts a job whose process fails after its tests passed (a leak report at exit) as a failure
  c_suites asan-llvm build/cmake-asan-llvm/kp_test long "" "$("$LLVM_CLANG" --version | head -n 1)" \
    ASAN_OPTIONS=detect_leaks=1:strict_string_checks=1:detect_stack_use_after_return=1:check_initialization_order=1 \
    KP_TEST_GLSLANG=1 KP_TEST_LOGS=build/c-tests-asan-llvm "$RES_ORIGINAL" "$RES_SCR"
  build/cmake-asan-llvm/kp_hd_phone --selftest | tail -1 # the codecs without the ASTC encoder
fi
if command -v emcmake >/dev/null && command -v node >/dev/null; then
  # the jobs are wasm's under node, their dispatch the host's runner
  step "C suites in wasm under node (the HTML5 proxy: the deterministic core in WebAssembly)"
  emcmake cmake -S . -B build/cmake-wasm -G Ninja -DCMAKE_BUILD_TYPE=Release >/dev/null
  cmake --build build/cmake-wasm >/dev/null
  c_suites wasm build/cmake/kp_test long "build/cmake-wasm/kp_test.js build/cmake-wasm/kp_test.wasm" \
    "$(node --version)" "KP_TEST_EXEC=node build/cmake-wasm/kp_test.js" KP_TEST_LOGS=build/c-tests-wasm "$RES_ORIGINAL" "$RES_SCR"
else
  step "C suites in wasm: skipped (no emcmake or node on the PATH)"
fi
step "Python tests join (the product's checks keep the machine to themselves)"
join_python_tests
# The development checks' passing lines (the app's output; on the phone after logcat's prefix).
AUTOTEST_OK='SPIKE autotest: [0-9]+ missions, [1-9][0-9]* megatiles checked, 0 mismatches; composed views: [1-9][0-9]*; load failures: 0$'
FOG_OK='SPIKE fog check: [1-9][0-9]* pixels, [0-9]+ darkened, 0 mismatches$'
PALETTE_OK='SPIKE palette check: [1-9][0-9]* pixels, 0 mismatches$'
CINE_OK='SPIKE cine check: [0-9]+ movies, [1-9][0-9]* frames, 0 failures$'
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
    "$PYTHON" tools/viewer_compose_check.py --data "$KOPRULU_ORIGINAL" --log build/autotest.log | tail -1
  fi
  step "product scripts (ADR 0017): a run that passes, a wait that times out, a misspelt key refused, a hung frame's watchdog"
  # a check's flag beside a script, first: the passing run after it keeps its files in build/script/smoke_pass
  app_script smoke_pass 2 "$APP" --autotest
  grep -qF 'SPIKE script: --autotest cannot run with --script' build/script_smoke_pass.log
  app_script smoke_pass 0 "$APP"
  [ -s build/script/smoke_pass/menu.png ]
  [ -d build/script/smoke_pass/pref ] # the run's profile, not the player's
  app_script smoke_timeout 1 "$APP"
  grep -qE '^SPIKE script smoke_timeout\.kps:[0-9]+: wait dialog gamemenu: no change in [0-9]+ ms: open: .* FAILED$' \
    build/script_smoke_timeout.log
  [ -s build/script/smoke_timeout/failure.png ]
  [ -s build/script/smoke_timeout/failure.ui ]
  [ -s build/script/smoke_timeout/failure.state ]
  # the start of the video check as a script: the video dialog's tree in video.ui, the game's state in start.state
  app_script video_menu 0 "$APP"
  grep -qE '^dialog video box .* \(modeless\) ' build/script/video_menu/video.ui
  grep -qE '^  video\.-2 default-button at .* = ok$' build/script/video_menu/video.ui
  [ -s build/script/video_menu/start.state ]
  [ -s build/script/video_menu/video.png ]
  app_script smoke_syntax 2 "$APP"
  grep -qF 'SPIKE script: smoke_syntax.kps:5: unknown key "F1O"' build/script_smoke_syntax.log
  app_script smoke_hang 3 "$APP"
  grep -qE '^SPIKE script smoke_hang: no frame in [0-9]+ ms: the watchdog ends the program FAILED$' \
    build/script_smoke_hang.log
  # a live run (stage SC7): live.kps's start, then lines through the FIFO. Each is answered; the game stands while the
  # run waits for a line (the fixed clock), a misspelt key is refused, a failed expect is counted, the lines after it run
  step "product live script: lines from a FIFO, each answered, a failure counted and the run going on"
  rm -rf build/script/live
  mkdir -p build/script/live
  mkfifo build/script/live/in
  limited "$APP" --script tests/scripts/live.kps --script-live build/script/live/in --script-out build/script/live \
    --headless --window "$APP_WINDOW" >build/script_live.log 2>build/script_live.err &
  LIVE=$!
  exec 3<>build/script/live/in # read and write: the open waits for no reader
  live_send "log start tick"
  live_answer "SPIKE script live:1: log start tick: done"
  sleep 0.5
  live_send "log held tick" "key F10" "wait dialog gamemenu" "shot menu"
  live_answer "SPIKE script live:5: shot menu: done"
  live_send "key F1O" "expect tick < 0" "log skipped"
  live_answer "SPIKE script live: 1 steps after it skipped"
  live_send "click control gamemenu.return" "wait nodialog" "quit"
  exec 3>&-
  LIVE_STATUS=0
  wait "$LIVE" || LIVE_STATUS=$?
  grep -E "^SPIKE script live" build/script_live.log | tail -1
  if [ "$LIVE_STATUS" != 1 ]; then
    tail -n 40 build/script_live.err >&2
    echo "ci: script live: exit code $LIVE_STATUS, not 1 (its one failure)" >&2
    exit 1
  fi
  [ "$(sed -n 's/^SPIKE script live:1: start: //p' build/script_live.log)" = \
    "$(sed -n 's/^SPIKE script live:2: held: //p' build/script_live.log)" ] # the game stood half a second
  grep -qF 'SPIKE script live:6: unknown key "F1O": refused' build/script_live.log
  grep -qF 'SPIKE script live:7: expect tick < 0: ' build/script_live.log
  grep -qF 'SPIKE script live:10: wait nodialog: done' build/script_live.log
  grep -qxF 'SPIKE script live: 9 steps, 1 failures' build/script_live.log
  [ -s build/script/live/menu.png ]
  [ -s build/script/live/failure.png ]
  # the fixed clock (stage SC4): two runs of one script write the same files, byte for byte (each state and frame);
  # its negative control, the marines sent elsewhere, the same start and then other files
  step "product scripts' fixed clock: two runs the same, a different order different"
  app_script determinism 0 "$APP"
  rm -rf build/script/determinism_first
  mv build/script/determinism build/script/determinism_first
  app_script determinism 0 "$APP"
  diff -r build/script/determinism_first build/script/determinism
  # diff -r compares the sets; the last state and frame make sure they are not both empty (one test a line: set -e
  # ignores a failure inside an && list but its last)
  [ -s build/script/determinism/fight.state ]
  [ -s build/script/determinism/fight.png ]
  app_script determinism_other 0 "$APP"
  cmp build/script/determinism/start.state build/script/determinism_other/start.state
  # (set -e ignores a command under !)
  if cmp -s build/script/determinism/fight.state build/script/determinism_other/fight.state ||
    cmp -s build/script/determinism/fight.png build/script/determinism_other/fight.png; then
    echo "ci: determinism_other's fight is determinism's: the order changed nothing" >&2
    exit 1
  fi
  # the checks' scripts (stages SC5, SC6): a check's line ("SPIKE check ...", "fog check", "palette check") and the
  # run's last line and exit code say how it went
  step "product checks' scripts: the video options, the touch layer, a save's loads"
  for name in video touch save; do
    app_script "$name" 0 "$APP"
  done
  [ -s build/script/video/kept.png ]
  [ -s build/script/touch/key_option.png ]
  [ -s build/script/save/from-file.png ]
  [ ! -e "build/script/save/pref/save_-koprulu_20save_20check.kpsave" ] # check save-file took it
  for n in 1 2 3 4 5; do
    step "product play script $n: box selection, right clicks, the terrain, the fog and the palette pass"
    app_script "play_$n" 0 "$APP"
    grep -E "^SPIKE ((fog|palette) check|check)" "build/script_play_$n.log"
    grep -qE "^$TERRAIN_OK" "build/script_play_$n.log"
    grep -qE "^$FOG_OK" "build/script_play_$n.log"
    grep -qE "^$PALETTE_OK" "build/script_play_$n.log"
  done
  if [ -n "${KOPRULU_ORIGINAL:-}" ]; then
    # the original data (ADR 0018, stages D5, D8b): the game's data, the tileset, the packs packed from the GRPs, the
    # interface, the sounds' WAVs, the triggers' WAVs and the music read from the 1.17 install itself; the same runs
    # write the same files (the states, the frames, the profile) and play the same sounds as over the bundle's repack
    # (its one koprulu.mpq, the WAVs as Ogg Vorbis where that is smaller)
    step "product scripts over the 1.17 install: the fixed clock's run and a play's files and sounds the bundle's"
    SCRIPT_RUN=determinism_original app_script determinism 0 "$APP" --data "$KOPRULU_ORIGINAL"
    grep -qF 'data: the original data from the 1.17 install' build/script_determinism_original.err
    grep -qF 'data: the original data from a repack' build/script_determinism.err
    diff -r build/script/determinism build/script/determinism_original
    SCRIPT_RUN=play_4_original app_script play_4 0 "$APP" --data "$KOPRULU_ORIGINAL"
    diff -r build/script/play_4 build/script/play_4_original
    diff <(grep -E '^SPIKE script play_tail\.kps:[0-9]+: sound:' build/script_play_4.log) \
      <(grep -E '^SPIKE script play_tail\.kps:[0-9]+: sound:' build/script_play_4_original.log)
    grep -qE '^SPIKE script play_tail\.kps:[0-9]+: sound: .*sounds\.streamed [1-9]' build/script_play_4_original.log
    # the repack (stage D8): the install's files in koprulu-*.mpq groups, the WAVs as Ogg Vorbis where that is smaller;
    # the same runs over it with no install beside it write the same files and play the same sounds
    step "product scripts over the 1.17 install's repack (its groups): the install's files, frames and sounds"
    logged build/repack_original.log 1 build/cmake/kp_convert repack "$KOPRULU_ORIGINAL" --out build/repack/original
    [ -f build/repack/original/koprulu-core.mpq ] && [ -f build/repack/original/koprulu-music.mpq ]
    SCRIPT_RUN=determinism_repack app_script determinism 0 "$APP" --data build/repack/original
    grep -qF 'data: the original data from a repack' build/script_determinism_repack.err
    diff -r build/script/determinism build/script/determinism_repack
    SCRIPT_RUN=play_4_repack app_script play_4 0 "$APP" --data build/repack/original
    diff -r build/script/play_4 build/script/play_4_repack
    diff <(grep -E '^SPIKE script play_tail\.kps:[0-9]+: sound:' build/script_play_4.log) \
      <(grep -E '^SPIKE script play_tail\.kps:[0-9]+: sound:' build/script_play_4_repack.log)
    grep -qF 'res: repack build/repack/original/koprulu-music.mpq' build/script_play_4_repack.err
    # a mission's group missing from the repack (kp_app_group): its files are the install's when one is open beside it,
    # the same run; with none the mission is refused with the group's name
    step "a repack without terran01's group: the install's files, else the mission refused"
    rm -rf build/repack/no_group
    mkdir -p build/repack/no_group
    ln build/repack/original/*.mpq build/repack/no_group/
    rm build/repack/no_group/koprulu-terran-terran01.mpq
    SCRIPT_RUN=determinism_no_group app_script determinism 0 "$APP" --data build/repack/no_group --data "$KOPRULU_ORIGINAL"
    grep -qF 'koprulu-terran-terran01.mpq is not there (the repack'"'"'s group beside koprulu-core.mpq) (its files fall back to the install)' \
      build/script_determinism_no_group.err
    diff -r build/script/determinism build/script/determinism_no_group
    SCRIPT_RUN=determinism_no_group_alone app_script determinism 2 "$APP" --data build/repack/no_group
    grep -qF 'SPIKE script: build/repack/no_group/koprulu-terran-terran01.mpq is not there' \
      build/script_determinism_no_group_alone.log
  fi
  if [ -n "${KOPRULU_SCR:-}" ]; then
    # Remastered's repack in one koprulu.mpq (the desktops'): its copies and rebuilds with their statuses; a run over
    # it writes what the same run over the install writes
    step "product script over Remastered and its repack (one koprulu.mpq): the same files"
    logged build/repack_scr.log 1 build/cmake/kp_convert repack "$KOPRULU_SCR" --single --out build/repack/scr
    SCRIPT_RUN=determinism_scr app_script determinism 0 "$APP" --data "$KOPRULU_SCR"
    SCRIPT_RUN=determinism_scr_repack app_script determinism 0 "$APP" --data build/repack/scr
    grep -qF 'data: the original data from a repack' build/script_determinism_scr_repack.err
    diff -r build/script/determinism_scr build/script/determinism_scr_repack
    # the repack's HD sets (ADR 0019, stage C5: hd2 and hd, kp_convert repack's default over a CASC install): each
    # pack opened from its head in the root as the runtime opens it, its pages, the tilesets and the water read from
    # the set's group, the Python converter's (tests/golden/hd/converter.json, recorded before stage C5 deleted it);
    # the HD interface (stage C6): each variant's ui.kpd opened from its head and read from its group, the consoles'
    # art and the portraits' clips, the Python converter's (tests/golden/hdui/converter.json)
    step "Remastered's repack's HD sets and interface: their packs, tilesets, water, frames, consoles and portraits the Python converter's"
    env KP_TEST_HD_REPACK=build/repack/scr build/cmake/kp_test test_hd:repack_equals_the_converters \
      test_hd:repack_ui_equals_the_converters | tail -1
    # the languages' groups (ADR 0019, stages C2 and C3): their texts, maps' strings and TIME and voices' records are
    # the install's
    step "the languages from Remastered's repack: its language groups give what the install gives"
    env KP_TEST_SCR=build/repack/scr "$RES_ORIGINAL" build/cmake/kp_test test_locale:the_languages_equal_the_converters \
      test_speech:the_speech_equals_the_converters test_speech:the_speech_sound_records_replace_the_bases | tail -1
    # each language locale-check listed (ADR 0016; ADR 0019, stage C3: built from the install's files): the menu, from
    # the install and from the repack's language group, and terran01's screens (hd_game.kps's walk in the install's HD
    # art) with the language's speech, none of them reaching a character with no glyph
    for loc in $(sed -n 's/^\([a-z][a-z][A-Z][A-Z]\): [0-9]* tables, .*/\1/p' build/locale_check.log); do
      step "product in $loc: the menu, a mission's screens and speech, no character without a glyph (ADR 0016)"
      mkdir -p "build/locale_check/$loc"
      language=(--data . --data "$KOPRULU_SCR" --locale "$loc")
      app_check "locale_menu_$loc" "$APP" "${language[@]}" --after 2 --screenshot "build/locale_check/$loc/menu.png"
      app_check "locale_menu_repack_$loc" "$APP" --data build/repack/scr --locale "$loc" --after 2 \
        --screenshot "build/locale_check/$loc/menu_repack.png"
      logs=("build/locale_menu_$loc.err" "build/locale_menu_$loc.log" "build/locale_menu_repack_$loc.err"
        "build/locale_menu_repack_$loc.log")
      SCRIPT_RUN="locale_$loc" app_script hd_game 0 "$APP" "${language[@]}" --speech "$loc" --mission terran/terran01
      logs+=("build/script_locale_$loc.err" "build/script_locale_$loc.log")
      ls "build/script/locale_$loc"/*.png | wc -l
      if grep -hE "no glyph|language $loc:|speech $loc:|the base.s voices" "${logs[@]}"; then
        echo "ci: the product's language $loc: a character without a glyph, or the language's text or speech refused" >&2
        exit 1
      fi
      if ! grep -qhF "speech of $loc: terran/terran01:" "${logs[@]}"; then
        echo "ci: the product's language $loc: terran01 did not take the speech's TIME" >&2
        exit 1
      fi
      [ -s "build/locale_check/$loc/menu.png" ]
      [ -s "build/locale_check/$loc/menu_repack.png" ]
    done
  fi
  # the bundle's movies (the cinematics' groups of its repack, ADR 0019, stage C4)
  if [ "$CINEMATICS" != none ]; then
    for variant in sd hd2; do
      case "$CINEMATICS" in both | "$variant") ;; *) continue ;; esac
      step "product cine check ($variant): the bundle's movies decoded to their end, the first pictures drawn"
      mkdir -p "build/cine_check/$variant"
      app_check "cine_check_$variant" "$APP" --cinematics "$variant" --cine-check "build/cine_check/$variant"
      grep -E "^SPIKE cine check: " "build/cine_check_$variant.log"
      grep -qE "^$CINE_OK" "build/cine_check_$variant.log"
    done
  fi
  # cine_check NAME ARGS...: every movie of a source decoded to its end (28), its log build/cine_check_NAME.log
  cine_check() {
    local name="$1"
    shift
    mkdir -p "build/cine_check/$name"
    app_check "cine_check_$name" "$APP" "$@" --cine-check "build/cine_check/$name"
    grep -E "^SPIKE cine check: " "build/cine_check_$name.log"
    grep -qE "^SPIKE cine check: 28 movies, [1-9][0-9]* frames, 0 failures$" "build/cine_check_$name.log"
  }
  # an install's movies played as they are (ADR 0018, stage D7), and its repack's (ADR 0019, stage C4): the 1.17
  # install's Smacker files (each checked against the reference copy; kp_smk's palette records and sound) and the same
  # movies transcoded into WebM; Remastered's WebM with the subtitles of a language, from the install and from the
  # repack's cinematics' and language's groups (HD2's too when KOPRULU_CINEMATICS asks for it)
  if [ -n "${KOPRULU_ORIGINAL:-}" ]; then
    step "product cine check over the 1.17 install: its 28 Smacker movies decoded to their end"
    cine_check original --data "$KOPRULU_ORIGINAL"
    step "product cine check over the 1.17 install's repack: its 28 movies as WebM decoded to their end"
    [ -f build/repack/original/koprulu-cinematics.mpq ]
    grep -E "^[0-9]+ movies as WebM " build/repack_original.log
    cine_check original_repack --data build/repack/original
    grep -qF 'cinematics: sd WebM, 28 movies' build/cine_check_original_repack.err
    # all 28 against the Python converter's pack (tests/golden/cinematics/converter.json, recorded before stage C4 deleted
    # it): the same VP9 packets and frame tables, the Vorbis within a thousandth of oggenc's bytes
    step "the 1.17 repack's movies: the VP9 packets the Python converter's, the Vorbis within a thousandth"
    env KP_TEST_MOVIES=build/repack/original "$RES_ORIGINAL" build/cmake/kp_test test_convert:movies_equal_the_converters \
      | tail -1
  fi
  if [ -n "${KOPRULU_SCR:-}" ]; then
    for variant in sd hd2; do
      [ "$variant" = sd ] || case "$CINEMATICS" in both | hd2) ;; *) continue ;; esac
      step "product cine check over Remastered ($variant) and its repack: its WebM played as they are, subtitled"
      cine_check "scr_$variant" --data "$KOPRULU_SCR" --cinematics "$variant"
      grep -qE "^SPIKE cine check smk.starintr: subtitle 0 at " "build/cine_check_scr_$variant.log"
      cine_check "scr_repack_$variant" --data build/repack/scr --cinematics "$variant" --locale ruRU
      grep -qE "^SPIKE cine check smk.starintr: subtitle 0 at " "build/cine_check_scr_repack_$variant.log"
    done
  fi
  # the HD art (ADR 0019, stages C5 and C6): the world's packs laid out from Remastered's anims at a mission's start
  # and the interface's frames by the placement table, and the repack's sets (koprulu-hd2.mpq, koprulu-hd.mpq) and
  # interface groups (koprulu-ui-<variant>.mpq, koprulu-portraits.mpq) read as they are; each run's log names the
  # source its art came from
  if [ -n "${KOPRULU_SCR:-}" ]; then
    for source in install repack; do
      if [ "$source" = install ]; then
        data="$KOPRULU_SCR" art="the install's anims" ui_art="the install's files"
      else
        data=build/repack/scr art="the repack's hd2?" ui_art="the repack's ui.kpd"
      fi
      for mission in glue terran/terran02 zerg/zerg02 protoss/protoss02; do
        step "product HD script on $mission from Remastered's $source: the screens and menus with no SD pixel where HD art exists"
        if [ "$mission" = glue ]; then # the glue's screens by clicks: the menus, a briefing, a victory, the score
          run="hd_glue_$source"
          SCRIPT_RUN="$run" app_script hd_glue 0 "$APP" --data "$data"
        else
          run="hd_game_${mission#*/}_$source"
          SCRIPT_RUN="$run" app_script hd_game 0 "$APP" --data "$data" --mission "$mission"
          grep -qE "Remastered's hd2? art \($art\)" "build/script_$run.err"
        fi
        grep -qE "Remastered's hd2? interface art \($ui_art\)" "build/script_$run.err"
        grep -E "^SPIKE hd check" "build/script_$run.log" | grep -vE ": [0-9]+ HD pixels; SD over HD art: 0 lost, 0 outside, 0 through, 0 waiting, 0 unreadable; 0 drawn outside the log; 0 log drops;" || true
      done
    done
  fi
fi

if [ -n "${KOPRULU_WEB:-}" ]; then
  step "product (web, debug)"
  KOPRULU_CINEMATICS="$CINEMATICS" tools/product.sh web debug >/dev/null
  if [ -n "${KOPRULU_SCR:-}" ]; then
    # Remastered's repack in groups beside the page (ADR 0019), the page in a headless Chrome: a language's group
    # fetched whole (kp_app_flow_languages), a movie by its range (kp_app_cine.c), the HD2 art's pages, interface
    # frames and portraits by theirs (kp_app_hd.c, kp_app_hd_ui.c); a run's console in build/web_<name>.log
    step "the web over Remastered's repack in groups (headless Chrome): ruRU's group, a movie, the HD2 art"
    rm -rf build/web_scr
    mkdir -p build/web_scr/scenarios build/web_scr/viewer # a debug bundle's test scenarios: none here
    logged build/repack_web_scr.log 1 build/cmake/kp_convert repack "$KOPRULU_SCR" --hd hd2 --locales ruRU \
      --out build/web_scr/repack
    KOPRULU_GENERATED=build/web_scr KOPRULU_CINEMATICS=hd2 KOPRULU_LOCALES=ruRU KOPRULU_DIST=build/web_scr/dist \
      tools/product.sh web debug >/dev/null
    web_run() { # NAME UNTIL ARG...: the bundle's page until a console line matches UNTIL
      local name="$1" until="$2"
      shift 2
      "$PYTHON" tools/web_run.py build/web_scr/dist/web "build/web_$name.log" --until "$until" --seconds 300 -- "$@"
    }
    # a new profile (the browser's is the run's own): the glue starts with the first start's intro, Blizzard's logo first
    web_run language '^cinematics: [^ ]+ ended after ' --locale ruRU
    grep -qE '^data: fetching koprulu-locale-ruru\.mpq$' build/web_language.log
    grep -qE '^language ruRU, speech ruRU: waiting for their files$' build/web_language.log
    grep -qE '^speech of ruRU: [0-9]+ voices$' build/web_language.log
    grep -E '^cinematics: [^ ]+ ended after ' build/web_language.log
    sed -nE 's/^cinematics: [^ ]+ ended after .*; ([0-9]+) of ([0-9]+) frames decoded, ([0-9]+) pictures shown$/\1 \2 \3/p' \
      build/web_language.log | awk '$1 == $2 && $3 > 0 { ok = 1 } END { exit !ok }' # every frame, pictures shown
    web_run hd_game '^SPIKE script hd_game: [0-9]+ steps, [0-9]+ failures$' --script generated/scripts/hd_game.kps --script-out /tmp/hd_game \
      --mission terran/terran02
    grep -E '^SPIKE script hd_game: [0-9]+ steps, [0-9]+ failures$' build/web_hd_game.log
    grep -qE '^SPIKE script hd_game: [0-9]+ steps, 0 failures$' build/web_hd_game.log
    grep -qE "Remastered's hd2 art \(" build/web_hd_game.log
    grep -qE "Remastered's hd2 interface art \(" build/web_hd_game.log
  fi
fi

if [ -n "${KOPRULU_ANDROID:-}" ]; then
  # Only for the changes that need a real phone (docs/phaseS.md, "Device runs"): the APK on the attached phone,
  # unlocked; its play scripts against their CPU twins, the autotest, the touch script and the frame benchmark. The
  # scripts are the debug APK's assets (generated/scripts/), their files stay on the phone, in the app's external
  # files (a run removes nothing: each is emptied first).
  step "product (Android, debug) on the attached phone: play scripts, autotest, touch script, frame benchmark"
  KOPRULU_CINEMATICS="$CINEMATICS" tools/product.sh android debug >/dev/null
  adb wait-for-device # a wireless connection may have dropped during the build
  adb install -r dist/product/android/KopruluSector.apk >/dev/null
  PHONE_SCRIPTS=/sdcard/Android/data/com.koprulusector.game/files/script
  # made by the shell, as the touch check's was: the product never asks for its external files (getExternalFilesDir),
  # so a fresh install may have no such directory, and Android may not let the app make it under Android/data
  adb shell mkdir -p "$PHONE_SCRIPTS"
  phone_script() {
    local name="$1"
    adb shell rm -rf "$PHONE_SCRIPTS/${name:?}"
    tools/native/android_app.sh launch --script "generated/scripts/$name.kps" --script-out "$PHONE_SCRIPTS/$name" \
      >build/android_check.log
    grep -E "SPIKE ((fog|palette) check|check|script)" build/android_check.log | sed -E 's/^.*SPIKE /SPIKE /'
    grep -qE "SPIKE script $name: [0-9]+ steps, 0 failures\$" build/android_check.log
  }
  for n in 1 2 3 4 5; do
    phone_script "play_$n"
    grep -qE "$TERRAIN_OK" build/android_check.log
    grep -qE "$FOG_OK" build/android_check.log
    grep -qE "$PALETTE_OK" build/android_check.log
  done
  tools/native/android_app.sh launch --autotest >build/android_check.log
  grep -E "SPIKE autotest" build/android_check.log | sed -E 's/^.*SPIKE /SPIKE /'
  grep -qE "$AUTOTEST_OK" build/android_check.log
  phone_script touch
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
