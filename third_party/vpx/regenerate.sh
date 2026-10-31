#!/usr/bin/env bash
# Regenerates third_party/vpx/generated/ from the fetched libvpx source (tools/toolchain/fetch.sh vpx): the headers
# configure writes for the generic-gnu target, the C-only decoder build of CMakeLists.txt. Run after changing the pinned
# libvpx version, then check the diff (HAVE_PTHREAD_H and HAVE_UNISTD_H are set to 0 by hand below: the build uses
# neither) and the source list in kp_vpx.cmake against the objects the build makes.
set -euo pipefail
cd "$(dirname "$0")/../.."
SRC="$(pwd)/tools/toolchain/vpx/libvpx-1.17.0"
OUT="$(pwd)/third_party/vpx/generated"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
(cd "$WORK" && "$SRC/configure" --target=generic-gnu --disable-multithread --disable-vp8 --enable-vp9 \
  --disable-vp9-encoder --disable-vp9-highbitdepth --disable-examples --disable-docs --disable-unit-tests \
  --disable-tools --disable-postproc --disable-webm-io --disable-libyuv --disable-runtime-cpu-detect \
  --enable-static --disable-shared >/dev/null && make -j4 vpx_config.h vp9_rtcd.h vpx_dsp_rtcd.h vpx_scale_rtcd.h \
  vpx_version.h >/dev/null && cp vpx_config.h vpx_config.c vp9_rtcd.h vpx_dsp_rtcd.h vpx_scale_rtcd.h vpx_version.h "$OUT/")
sed -i.bak 's/#define HAVE_PTHREAD_H 1/#define HAVE_PTHREAD_H 0/; s/#define HAVE_UNISTD_H 1/#define HAVE_UNISTD_H 0/' "$OUT/vpx_config.h"
rm -f "$OUT/vpx_config.h.bak"
echo "regenerated $OUT"
