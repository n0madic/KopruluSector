#!/usr/bin/env bash
# Regenerates third_party/vpx/generated/ from the fetched libvpx source (tools/toolchain/fetch.sh vpx): the headers
# configure writes for the decoder build of kp_vpx.cmake, generic/ for the C of the generic-gnu target and arm64/ for
# AArch64's NEON (configured as arm64-darwin24-gcc, so run it on an arm64 Mac; the headers are the same for Android and
# Linux: nothing in them names the OS), vpx_version.h shared. Run after changing the pinned libvpx version, then check
# the diff (HAVE_PTHREAD_H and HAVE_UNISTD_H are set to 0 by hand below: the build uses neither) and kp_vpx.cmake's
# source lists against the objects each build makes.
set -euo pipefail
cd "$(dirname "$0")/../.."
SRC="$(pwd)/tools/toolchain/vpx/libvpx-1.17.0"
OUT="$(pwd)/third_party/vpx/generated"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
COMMON=(--disable-multithread --disable-vp8 --enable-vp9 --disable-vp9-encoder --disable-vp9-highbitdepth
  --disable-examples --disable-docs --disable-unit-tests --disable-tools --disable-postproc --disable-webm-io
  --disable-libyuv --disable-runtime-cpu-detect --enable-static --disable-shared)
generate() { # generate DIR CONFIGURE_ARGS...
  local dir="$1"
  shift
  mkdir -p "$WORK/$dir" "$OUT/$dir"
  (cd "$WORK/$dir" && "$SRC/configure" "$@" "${COMMON[@]}" >/dev/null && make -j4 vpx_config.h vp9_rtcd.h vpx_dsp_rtcd.h \
    vpx_scale_rtcd.h vpx_version.h >/dev/null && cp vpx_config.h vpx_config.c vp9_rtcd.h vpx_dsp_rtcd.h vpx_scale_rtcd.h \
    "$OUT/$dir/" && cp vpx_version.h "$OUT/")
  sed -i.bak 's/#define HAVE_PTHREAD_H 1/#define HAVE_PTHREAD_H 0/; s/#define HAVE_UNISTD_H 1/#define HAVE_UNISTD_H 0/' \
    "$OUT/$dir/vpx_config.h"
  rm -f "$OUT/$dir/vpx_config.h.bak"
}
generate generic --target=generic-gnu
generate arm64 --target=arm64-darwin24-gcc --disable-neon_dotprod --disable-neon_i8mm --disable-sve --disable-sve2
echo "regenerated $OUT"
