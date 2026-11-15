#!/usr/bin/env bash
# libFuzzer's targets (phase T, stage T2; ADR 0022): build/cmake-fuzz with an LLVM clang that has libFuzzer
# (KOPRULU_CLANG=<clang>, else Homebrew's llvm; Apple's has none), the seeds, then each target KOPRULU_FUZZ_TIME seconds
# (default 60) under ASan and UBSan.
#   tools/fuzz.sh [rep|chk|save|commands ...]   (no target: all four)
# The targets' entries are tests/c/kp_fuzz.c's (kp_fuzz.h tells what each feeds). Their corpora:
#   build/fuzz/corpus/<target>   what the fuzzer found, kept between runs (the first directory: new inputs go there)
#   tests/fuzz/seeds/<target>    the synthetic seeds, committed (kp_fuzz_seeds --synthetic tests/fuzz/seeds)
#   build/fuzz/seeds/<target>    the seeds made from the game's data, made anew each run (Blizzard's bytes: never
#                                committed); save and commands need the data and are skipped without it
#   tests/fuzz/regress/<target>  the findings kept (test_fuzz runs them in every build)
# A finding (a crash, a sanitizer's report, a leak, an input running 30 s) is written to build/fuzz/<target>/ and the
# script exits 1 after the other targets. Minimize it:
#   build/cmake-fuzz/fuzz_<target> -minimize_crash=1 -runs=100000 -exact_artifact_path=<out> <the finding>
# and, its cause fixed, keep it in tests/fuzz/regress/<target>/ (koprulu-private/fuzz/regress/<target>/ when it holds
# the data's bytes: a save, a mission's CHK, a recorded replay). A game the game's own checks stop is no finding: the
# end of each target's log counts them ("games refused").
set -euo pipefail
cd "$(dirname "$0")/.."

TIME="${KOPRULU_FUZZ_TIME:-60}"
HANG_SECONDS=30
ALL=(rep chk save commands)
TARGETS=("$@")
[ ${#TARGETS[@]} -gt 0 ] || TARGETS=("${ALL[@]}")
for t in "${TARGETS[@]}"; do
  case " ${ALL[*]} " in *" $t "*) ;; *) echo "fuzz: no target $t (${ALL[*]})" >&2; exit 2 ;; esac
done

CLANG="${KOPRULU_CLANG:-}"
if [ -z "$CLANG" ]; then
  for candidate in /opt/homebrew/opt/llvm/bin/clang /usr/local/opt/llvm/bin/clang; do
    if [ -x "$candidate" ]; then CLANG="$candidate"; break; fi
  done
fi
if [ -z "$CLANG" ] || ! CLANG="$(command -v "$CLANG")"; then
  echo "fuzz: no LLVM clang with libFuzzer (KOPRULU_CLANG=<clang>)" >&2
  exit 2
fi
base="${CLANG##*/}"
CLANGXX="${CLANG%"$base"}clang++${base#clang}" # clang -> clang++, clang-18 -> clang++-18
# exit 3: this clang links no libFuzzer (tools/ci.sh then skips the step)
probe="$(mktemp -d)"
printf 'int LLVMFuzzerTestOneInput(const unsigned char *d, unsigned long n) { (void)d; (void)n; return 0; }\n' >"$probe/p.c"
if ! "$CLANG" -fsanitize=fuzzer "$probe/p.c" -o "$probe/p" >/dev/null 2>&1; then
  rm -rf "$probe"
  echo "fuzz: $CLANG links no libFuzzer (KOPRULU_CLANG=<an LLVM clang with compiler-rt's libFuzzer>)"
  exit 3
fi
rm -rf "$probe"

cmake -S . -B build/cmake-fuzz -G Ninja -DCMAKE_BUILD_TYPE=Release -DKP_SANITIZE=ON -DKP_FUZZ=ON -DKP_CXX_CHECK=OFF \
  -DCMAKE_C_COMPILER="$CLANG" -DCMAKE_CXX_COMPILER="$CLANGXX" >/dev/null
cmake --build build/cmake-fuzz --target kp_fuzz_seeds $(printf 'fuzz_%s ' "${TARGETS[@]}") >build/fuzz_build.log 2>&1 ||
  { tail -n 30 build/fuzz_build.log >&2; exit 1; }

rm -rf build/fuzz/seeds
mkdir -p build/fuzz
build/cmake-fuzz/kp_fuzz_seeds build/fuzz/seeds
# the data's seeds (a save's among them) exist only when the targets' games could start
HAVE_DATA=0
[ -d build/fuzz/seeds/save ] && HAVE_DATA=1

found=0
for t in "${TARGETS[@]}"; do
  if [ "$HAVE_DATA" = 0 ] && { [ "$t" = save ] || [ "$t" = commands ]; }; then
    echo "fuzz_$t: skipped (no game data: make convert)"
    continue
  fi
  mkdir -p "build/fuzz/corpus/$t" "build/fuzz/$t"
  corpora=("build/fuzz/corpus/$t")
  for d in "tests/fuzz/seeds/$t" "build/fuzz/seeds/$t" "tests/fuzz/regress/$t" "koprulu-private/fuzz/regress/$t"; do
    [ -d "$d" ] && corpora+=("$d")
  done
  before="$(ls "build/fuzz/$t" | wc -l)"
  status=0
  build/cmake-fuzz/fuzz_$t -max_total_time="$TIME" -timeout="$HANG_SECONDS" -print_final_stats=1 \
    -artifact_prefix="build/fuzz/$t/" "${corpora[@]}" >"build/fuzz/$t.log" 2>&1 || status=$?
  after="$(ls "build/fuzz/$t" | wc -l)"
  if [ "$status" != 0 ] || [ "$after" != "$before" ]; then
    echo "fuzz_$t: FINDING (exit $status; build/fuzz/$t.log, build/fuzz/$t/)"
    grep -E 'ERROR|SUMMARY|fuzz_'"$t"':|runtime error|Test unit written' "build/fuzz/$t.log" | head -n 12
    found=1
  else
    echo "$(grep -E '^fuzz_'"$t"':' "build/fuzz/$t.log" | tail -n 1) in ${TIME}s; $(grep -E '[[:space:]]DONE[[:space:]]' "build/fuzz/$t.log" | sed 's/^#[0-9]*[[:space:]]*DONE[[:space:]]*//')"
    grep -E '^  refused ' "build/fuzz/$t.log" || true
  fi
done
exit "$found"
