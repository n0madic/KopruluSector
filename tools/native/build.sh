#!/usr/bin/env bash
# Builds the native modules as Lua C modules for LuaJIT and PUC Lua 5.1 outside Defold
# (headless tests, tools/lua/tick_bench.lua): build/native/kp_path.so. The game loads them
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
