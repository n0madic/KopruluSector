#!/usr/bin/env bash
# Fetches the pinned toolchain into tools/toolchain/ (gitignored):
#   bob.jar and dmengine_headless of Defold 1.13.2, and PUC Lua 5.1.5 built from
#   source (the Lua version of Defold's HTML5 runtime).
set -euo pipefail

DEFOLD_VERSION=1.13.2
DEFOLD_SHA1=055a6e163cb952d023ffe7becf9eab88d7e84a74
LUA_VERSION=5.1.5
LUA_SHA256=2640fc56a795f29d28ef15e13c34a47e223960b0240e8cb0a82d9b0738695333

DIR="$(cd "$(dirname "$0")" && pwd)"
ARCHIVE="https://d.defold.com/archive/stable/${DEFOLD_SHA1}"

case "$(uname -s)-$(uname -m)" in
  Darwin-arm64) PLATFORM=arm64-macos; LUA_TARGET=macosx ;;
  Darwin-x86_64) PLATFORM=x86_64-macos; LUA_TARGET=macosx ;;
  Linux-x86_64) PLATFORM=x86_64-linux; LUA_TARGET=posix ;;
  Linux-aarch64) PLATFORM=arm64-linux; LUA_TARGET=posix ;;
  *) echo "unsupported host $(uname -s)-$(uname -m)" >&2; exit 1 ;;
esac

sha256() { if command -v sha256sum >/dev/null; then sha256sum "$1"; else shasum -a 256 "$1"; fi | cut -d' ' -f1; }

if [ ! -f "$DIR/bob.jar" ]; then
  echo "fetching bob.jar ${DEFOLD_VERSION}"
  curl -sSfL -o "$DIR/bob.jar" "${ARCHIVE}/bob/bob.jar"
fi

if [ ! -x "$DIR/dmengine_headless" ]; then
  echo "fetching dmengine_headless ${DEFOLD_VERSION} (${PLATFORM})"
  curl -sSfL -o "$DIR/dmengine_headless" "${ARCHIVE}/engine/${PLATFORM}/dmengine_headless"
  chmod +x "$DIR/dmengine_headless"
fi

# Windowed engine for local spikes (same build as bob.jar).
if [ ! -x "$DIR/dmengine" ]; then
  unzip -o -q -j "$DIR/bob.jar" "libexec/${PLATFORM}/dmengine" -d "$DIR"
  chmod +x "$DIR/dmengine"
  # macOS kills the extracted binary unless it is (re)signed.
  if [ "$(uname -s)" = Darwin ]; then codesign -s - --force "$DIR/dmengine" 2>/dev/null; fi
fi

if [ ! -x "$DIR/lua5.1" ]; then
  mkdir -p "$DIR/src"
  TARBALL="$DIR/src/lua-${LUA_VERSION}.tar.gz"
  [ -f "$TARBALL" ] || curl -sSfL -o "$TARBALL" "https://www.lua.org/ftp/lua-${LUA_VERSION}.tar.gz"
  if [ "$(sha256 "$TARBALL")" != "$LUA_SHA256" ]; then
    echo "lua-${LUA_VERSION}.tar.gz checksum mismatch" >&2; exit 1
  fi
  tar -xzf "$TARBALL" -C "$DIR/src"
  make -s -C "$DIR/src/lua-${LUA_VERSION}" "$LUA_TARGET" >/dev/null
  cp "$DIR/src/lua-${LUA_VERSION}/src/lua" "$DIR/lua5.1"
fi

echo "toolchain ready in $DIR"
