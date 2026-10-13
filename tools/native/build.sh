#!/usr/bin/env bash
# Builds the native core as a Lua C module for LuaJIT and PUC Lua 5.1 outside Defold
# (headless tests, tools/lua/tick_bench.lua): build/native/kp_core.so, which game/native.lua finds; Defold builds
# the same sources as a native extension.
# LUA_INCLUDE overrides the Lua header directory (default: LuaJIT's from Homebrew or pkg-config).
set -euo pipefail
cd "$(dirname "$0")/../.."
OUT=build/native
mkdir -p "$OUT"
INCLUDE="${LUA_INCLUDE:-}"
if [ -z "$INCLUDE" ]; then
  INCLUDE="$(pkg-config --variable=includedir luajit 2>/dev/null || echo /opt/homebrew/include/luajit-2.1)"
fi
case "$(uname -s)" in
  Darwin) LINK=(-bundle -undefined dynamic_lookup) ;;
  *) LINK=(-shared -fPIC) ;;
esac
# kp_core (ADR 0008): C99 in the common subset of C and C++. The Defold build server compiles
# the sources as C++, so they are also checked as C++ here.
CORE=native/kp_core
CORE_SOURCES=("$CORE"/src/core/*.c)
WARN=(-Wall -Wextra -Wshadow -Wconversion -Wno-sign-conversion -Werror)
LUA_SOURCES=("$CORE"/src/lua/*.c)
cc -O2 -std=c99 "${WARN[@]}" -I"$INCLUDE" "${LINK[@]}" -o "$OUT/kp_core.so" \
  "${CORE_SOURCES[@]}" "${LUA_SOURCES[@]}"
c++ -std=c++11 -fsyntax-only -x c++ "${WARN[@]}" -I"$INCLUDE" -DKP_CPP_CHECK \
  "${CORE_SOURCES[@]}" "${LUA_SOURCES[@]}" "$CORE/tools/kp_load_check.c"
echo "built $OUT/kp_core.so (checked as C and C++)"
# The command-line tools over a scenario's replay (tools/lua/export_replay.lua): kp_replay compares the dump CRCs with
# the original's, kp_bench times the steps.
TOOLS=("$CORE"/tools/kp_runner.c)
for tool in kp_replay kp_bench; do
  cc -O2 -std=c99 "${WARN[@]}" -o "$OUT/$tool" "${CORE_SOURCES[@]}" "${TOOLS[@]}" "$CORE/tools/$tool.c"
  c++ -std=c++11 -fsyntax-only -x c++ "${WARN[@]}" "${TOOLS[@]}" "$CORE/tools/$tool.c"
done
echo "built $OUT/kp_replay and $OUT/kp_bench"

# SANITIZE=1: the loader check and the Lua module under AddressSanitizer and
# UndefinedBehaviorSanitizer. build/native/asan/kp_core.so needs the ASan runtime preloaded
# into the Lua process (tools/ci.sh).
if [ -n "${SANITIZE:-}" ]; then
  SAN=(-O1 -g -std=c99 "${WARN[@]}" -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
  cc "${SAN[@]}" -o "$OUT/kp_load_check" "${CORE_SOURCES[@]}" "$CORE/tools/kp_load_check.c"
  echo "built $OUT/kp_load_check (ASan, UBSan)"
  mkdir -p "$OUT/asan"
  cc "${SAN[@]}" -I"$INCLUDE" "${LINK[@]}" -o "$OUT/asan/kp_core.so" "${CORE_SOURCES[@]}" "${LUA_SOURCES[@]}"
  echo "built $OUT/asan/kp_core.so (ASan, UBSan)"
fi
