#!/usr/bin/env bash
# Builds the native modules as Lua C modules for LuaJIT and PUC Lua 5.1 outside Defold
# (headless tests, tools/lua/tick_bench.lua): build/native/kp_path.so and kp_core.so. The game loads them
# through game/pathing/native.lua; Defold builds the same sources as native extensions.
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
cc -O2 -std=c99 -Wall -Wextra -Wno-unused-parameter -I"$INCLUDE" -Inative/kp_path/src \
  "${LINK[@]}" -o "$OUT/kp_path.so" native/kp_path/src/kp_path_core.c tools/native/kp_path_luaopen.c
echo "built $OUT/kp_path.so"

# kp_core (ADR 0008): C99 in the common subset of C and C++. The Defold build server compiles
# the sources as C++, so they are also checked as C++ here.
CORE=native/kp_core
CORE_SOURCES=("$CORE"/src/core/*.c)
WARN=(-Wall -Wextra -Wshadow -Wconversion -Wno-sign-conversion -Werror)
cc -O2 -std=c99 "${WARN[@]}" -I"$INCLUDE" "${LINK[@]}" -o "$OUT/kp_core.so" \
  "${CORE_SOURCES[@]}" "$CORE/src/lua/kp_core_lua.c"
c++ -std=c++11 -fsyntax-only -x c++ "${WARN[@]}" -I"$INCLUDE" -DKP_CPP_CHECK \
  "${CORE_SOURCES[@]}" "$CORE/src/lua/kp_core_lua.c" "$CORE/tools/kp_load_check.c"
echo "built $OUT/kp_core.so (checked as C and C++)"

# SANITIZE=1: the loader check under AddressSanitizer and UndefinedBehaviorSanitizer.
if [ -n "${SANITIZE:-}" ]; then
  cc -O1 -g -std=c99 "${WARN[@]}" -fsanitize=address,undefined -fno-sanitize-recover=all \
    -o "$OUT/kp_load_check" "${CORE_SOURCES[@]}" "$CORE/tools/kp_load_check.c"
  echo "built $OUT/kp_load_check (ASan, UBSan)"
fi
