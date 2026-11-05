#!/usr/bin/env bash
# Fetches the pinned toolchain into tools/toolchain/ (gitignored): the SDL3 3.4.18 and SDL3_mixer 3.2.4 sources under
# sdl/, checked by SHA-256, which CMake builds as subprojects for every platform (phase S, ADR 0012), with the
# patches of patches/SDL3-3.4.18/ applied (tracked; patch(1) applies them).
# `fetch.sh vpx` fetches the libvpx 1.17.0 source under vpx/ instead (the optional cinematics decoder, ADR 0015,
# built with -DKP_CINEMATICS=ON); `fetch.sh astc` the Arm ASTC encoder 5.7.0 under astc/ (the host's kp_hd_phone, with
# -DKP_ASTC=ON; ADR 0013); `fetch.sh all` fetches them all. Without an argument only SDL is fetched.
set -euo pipefail

WHAT="${1:-sdl}"
case "$WHAT" in sdl | vpx | astc | all) ;; *) echo "usage: fetch.sh [sdl|vpx|astc|all]" >&2; exit 2 ;; esac

SDL_VERSION=3.4.18
SDL_SHA256=9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3
SDL_MIXER_VERSION=3.2.4
SDL_MIXER_SHA256=182a07c745375e113dc740d43964ff21b0be29f29f59876c4dbc4db3d32f6901
VPX_VERSION=1.17.0
VPX_SHA256=1020f184046187baa2985dbde38e0691f49c44088bca7a1842b0236c6081dc0a
ASTC_VERSION=5.7.0
ASTC_SHA256=7c1b28ece59c9c2737e297123a9910c1c548676c7aed87a09d5734e2fa7bdaf0

DIR="$(cd "$(dirname "$0")" && pwd)"
PATCH_STAMP=.kp-patches

sha256() { if command -v sha256sum >/dev/null; then sha256sum "$1"; else shasum -a 256 "$1"; fi | cut -d' ' -f1; }

fetch_source() { # subdirectory name version url sha256
  local tarball="$DIR/$1/$2-$3.tar.gz"
  mkdir -p "$DIR/$1"
  if [ ! -f "$tarball" ]; then
    # into a partial file first: an interrupted download never stands as the archive and is fetched again next time
    curl -sSfL -o "$tarball.part" "$4"
    mv "$tarball.part" "$tarball"
  fi
  if [ "$(sha256 "$tarball")" != "$5" ]; then
    echo "$2-$3.tar.gz checksum mismatch (delete it to fetch it again)" >&2; exit 1
  fi
  # the source carries the patches under patches/<name>-<version>/ (in their names' order), and its stamp names them by
  # their SHA-256: a source extracted with other patches, or before them, is extracted again
  local source="$DIR/$1/$2-$3" patches="$DIR/patches/$2-$3" want=none
  local found=("$patches"/*.patch)
  if [ -f "${found[0]}" ]; then
    command -v patch >/dev/null || { echo "fetch.sh needs patch to apply $patches" >&2; exit 1; }
    want="$(cat "${found[@]}" | sha256 /dev/stdin)"
  fi
  if [ -d "$source" ] && [ "$(cat "$source/$PATCH_STAMP" 2>/dev/null || echo none)" != "$want" ]; then
    rm -rf "$source"
  fi
  if [ ! -d "$source" ]; then
    # into a scratch directory first: an interrupted extraction never stands as the source and is done again next time
    local scratch="$DIR/$1/.extract-$2-$3"
    rm -rf "$scratch"
    mkdir "$scratch"
    tar -xzf "$tarball" -C "$scratch"
    if [ "$want" != none ]; then
      local p
      for p in "${found[@]}"; do
        patch -p1 -N -s -t -d "$scratch/$2-$3" <"$p" || { echo "$p does not apply to $2-$3" >&2; exit 1; }
      done
    fi
    echo "$want" >"$scratch/$2-$3/$PATCH_STAMP"
    mv "$scratch/$2-$3" "$source"
    rm -rf "$scratch"
  fi
}
if [ "$WHAT" = sdl ] || [ "$WHAT" = all ]; then
  fetch_source sdl SDL3 "$SDL_VERSION" \
    "https://github.com/libsdl-org/SDL/releases/download/release-${SDL_VERSION}/SDL3-${SDL_VERSION}.tar.gz" \
    "$SDL_SHA256"
  fetch_source sdl SDL3_mixer "$SDL_MIXER_VERSION" \
    "https://github.com/libsdl-org/SDL_mixer/releases/download/release-${SDL_MIXER_VERSION}/SDL3_mixer-${SDL_MIXER_VERSION}.tar.gz" \
    "$SDL_MIXER_SHA256"
fi
if [ "$WHAT" = vpx ] || [ "$WHAT" = all ]; then
  # GitHub's tag archive unpacks to libvpx-<version> (without the v)
  fetch_source vpx libvpx "$VPX_VERSION" \
    "https://github.com/webmproject/libvpx/archive/refs/tags/v${VPX_VERSION}.tar.gz" \
    "$VPX_SHA256"
fi
if [ "$WHAT" = astc ] || [ "$WHAT" = all ]; then
  fetch_source astc astc-encoder "$ASTC_VERSION" \
    "https://github.com/ARM-software/astc-encoder/archive/refs/tags/${ASTC_VERSION}.tar.gz" \
    "$ASTC_SHA256"
fi

echo "toolchain ready in $DIR"
