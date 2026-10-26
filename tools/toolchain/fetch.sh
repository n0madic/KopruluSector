#!/usr/bin/env bash
# Fetches the pinned toolchain into tools/toolchain/ (gitignored): the SDL3 3.4.18 and SDL3_mixer 3.2.4 sources under
# sdl/, checked by SHA-256, which CMake builds as subprojects for every platform (phase S, ADR 0012).
set -euo pipefail

SDL_VERSION=3.4.18
SDL_SHA256=9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3
SDL_MIXER_VERSION=3.2.4
SDL_MIXER_SHA256=182a07c745375e113dc740d43964ff21b0be29f29f59876c4dbc4db3d32f6901

DIR="$(cd "$(dirname "$0")" && pwd)"

sha256() { if command -v sha256sum >/dev/null; then sha256sum "$1"; else shasum -a 256 "$1"; fi | cut -d' ' -f1; }

fetch_source() { # name version url sha256
  local tarball="$DIR/sdl/$1-$2.tar.gz"
  mkdir -p "$DIR/sdl"
  if [ ! -f "$tarball" ]; then
    # into a partial file first: an interrupted download never stands as the archive and is fetched again next time
    curl -sSfL -o "$tarball.part" "$3"
    mv "$tarball.part" "$tarball"
  fi
  if [ "$(sha256 "$tarball")" != "$4" ]; then
    echo "$1-$2.tar.gz checksum mismatch (delete it to fetch it again)" >&2; exit 1
  fi
  [ -d "$DIR/sdl/$1-$2" ] || tar -xzf "$tarball" -C "$DIR/sdl"
}
fetch_source SDL3 "$SDL_VERSION" \
  "https://github.com/libsdl-org/SDL/releases/download/release-${SDL_VERSION}/SDL3-${SDL_VERSION}.tar.gz" \
  "$SDL_SHA256"
fetch_source SDL3_mixer "$SDL_MIXER_VERSION" \
  "https://github.com/libsdl-org/SDL_mixer/releases/download/release-${SDL_MIXER_VERSION}/SDL3_mixer-${SDL_MIXER_VERSION}.tar.gz" \
  "$SDL_MIXER_SHA256"

echo "toolchain ready in $DIR"
