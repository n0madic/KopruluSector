#!/usr/bin/env bash
# Fetches the pinned toolchain into tools/toolchain/ (gitignored): the SDL3 3.4.18 and SDL3_mixer 3.2.4 sources under
# sdl/, checked by SHA-256, which CMake builds as subprojects for every platform (phase S, ADR 0012); a source's patches
# under patches/<name>-<version>/ (tracked; patch(1) applies them) are applied when it is extracted: SDL3's (AAudio),
# StormLib's (an archive within another file: Android reads the repack in place from its APK).
# `fetch.sh data` fetches the readers of the original data under data/ (ADR 0018): StormLib 9.40 (the 1.17 install's
# MPQ archives), CascLib 3.0 (StarCraft: Remastered's CASC storage) and the zlib 1.3.2 and bzip2 1.0.8 they share.
# `fetch.sh vpx` fetches the libvpx 1.17.0 source and nestegg (the WebM demuxer, a commit of mozilla/nestegg: it has no
# releases) under vpx/ (VP8 and VP9 decoders: the cinematics, ADR 0015, and Remastered's WebM, ADR 0018);
# `fetch.sh vorbis` fetches libogg 1.3.6 and libvorbis 1.3.7 under vorbis/ (the Ogg Vorbis encoder of the host's
# kp_convert: the repack's sounds and speech, the 1.17 cinematics' sound; ADR 0019);
# `fetch.sh astc` the Arm ASTC encoder 5.7.0 under astc/ (the host's kp_hd_phone, with -DKP_ASTC=ON; ADR 0013);
# `fetch.sh all` fetches them all. Without an argument SDL, the data readers, the video decoders and the Vorbis encoder
# are fetched.
set -euo pipefail

WHAT="${1:-default}"
case "$WHAT" in default | sdl | data | vpx | vorbis | astc | all) ;; *) echo "usage: fetch.sh [sdl|data|vpx|vorbis|astc|all]" >&2; exit 2 ;; esac

SDL_VERSION=3.4.18
SDL_SHA256=9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3
SDL_MIXER_VERSION=3.2.4
SDL_MIXER_SHA256=182a07c745375e113dc740d43964ff21b0be29f29f59876c4dbc4db3d32f6901
VPX_VERSION=1.17.0
VPX_SHA256=1020f184046187baa2985dbde38e0691f49c44088bca7a1842b0236c6081dc0a
NESTEGG_COMMIT=767aab25013acefbdcc6d68a2a7a2c9081303e3f
NESTEGG_SHA256=bd597e86e5a7c00171aa56d914501e3f597ea46dbc7d432324e97da94a8a4b5e
OGG_VERSION=1.3.6
OGG_SHA256=83e6704730683d004d20e21b8f7f55dcb3383cdf84c0daedf30bde175f774638
VORBIS_VERSION=1.3.7
VORBIS_SHA256=0e982409a9c3fc82ee06e08205b1355e5c6aa4c36bca58146ef399621b0ce5ab
ASTC_VERSION=5.7.0
STORM_VERSION=9.40
STORM_SHA256=f80b08bae168888702a4251312d6c2279f487673df611c2b5ef4f395e810c3b1
CASC_VERSION=3.0
CASC_SHA256=2bbd1b62c3e161a7204345d89afdcf5160649c220e1a043460ba72ff0ab1b070
ZLIB_VERSION=1.3.2
ZLIB_SHA256=bb329a0a2cd0274d05519d61c667c062e06990d72e125ee2dfa8de64f0119d16
BZIP2_VERSION=1.0.8
BZIP2_SHA256=ab5a03176ee106d3f0fa90e381da478ddae405918153cca248e682cd0c4a2269
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
if [ "$WHAT" = default ] || [ "$WHAT" = sdl ] || [ "$WHAT" = all ]; then
  fetch_source sdl SDL3 "$SDL_VERSION" \
    "https://github.com/libsdl-org/SDL/releases/download/release-${SDL_VERSION}/SDL3-${SDL_VERSION}.tar.gz" \
    "$SDL_SHA256"
  fetch_source sdl SDL3_mixer "$SDL_MIXER_VERSION" \
    "https://github.com/libsdl-org/SDL_mixer/releases/download/release-${SDL_MIXER_VERSION}/SDL3_mixer-${SDL_MIXER_VERSION}.tar.gz" \
    "$SDL_MIXER_SHA256"
fi
if [ "$WHAT" = default ] || [ "$WHAT" = data ] || [ "$WHAT" = all ]; then
  # GitHub's tag archives unpack to StormLib-<version> and CascLib-<version> (the tags' names without a v)
  fetch_source data StormLib "$STORM_VERSION" \
    "https://github.com/ladislav-zezula/StormLib/archive/refs/tags/v${STORM_VERSION}.tar.gz" \
    "$STORM_SHA256"
  fetch_source data CascLib "$CASC_VERSION" \
    "https://github.com/ladislav-zezula/CascLib/archive/refs/tags/${CASC_VERSION}.tar.gz" \
    "$CASC_SHA256"
  fetch_source data zlib "$ZLIB_VERSION" \
    "https://github.com/madler/zlib/releases/download/v${ZLIB_VERSION}/zlib-${ZLIB_VERSION}.tar.gz" \
    "$ZLIB_SHA256"
  fetch_source data bzip2 "$BZIP2_VERSION" \
    "https://sourceware.org/pub/bzip2/bzip2-${BZIP2_VERSION}.tar.gz" \
    "$BZIP2_SHA256"
fi
if [ "$WHAT" = default ] || [ "$WHAT" = vpx ] || [ "$WHAT" = all ]; then
  # GitHub's tag archive unpacks to libvpx-<version> (without the v), a commit's to nestegg-<commit>
  fetch_source vpx libvpx "$VPX_VERSION" \
    "https://github.com/webmproject/libvpx/archive/refs/tags/v${VPX_VERSION}.tar.gz" \
    "$VPX_SHA256"
  fetch_source vpx nestegg "$NESTEGG_COMMIT" \
    "https://github.com/mozilla/nestegg/archive/${NESTEGG_COMMIT}.tar.gz" \
    "$NESTEGG_SHA256"
fi
if [ "$WHAT" = default ] || [ "$WHAT" = vorbis ] || [ "$WHAT" = all ]; then
  # xiph's release archives on GitHub unpack to libogg-<version> and libvorbis-<version>
  fetch_source vorbis libogg "$OGG_VERSION" \
    "https://github.com/xiph/ogg/releases/download/v${OGG_VERSION}/libogg-${OGG_VERSION}.tar.gz" \
    "$OGG_SHA256"
  fetch_source vorbis libvorbis "$VORBIS_VERSION" \
    "https://github.com/xiph/vorbis/releases/download/v${VORBIS_VERSION}/libvorbis-${VORBIS_VERSION}.tar.gz" \
    "$VORBIS_SHA256"
fi
if [ "$WHAT" = astc ] || [ "$WHAT" = all ]; then
  fetch_source astc astc-encoder "$ASTC_VERSION" \
    "https://github.com/ARM-software/astc-encoder/archive/refs/tags/${ASTC_VERSION}.tar.gz" \
    "$ASTC_SHA256"
fi

echo "toolchain ready in $DIR"
