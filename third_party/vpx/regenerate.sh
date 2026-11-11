#!/usr/bin/env bash
# Regenerates third_party/vpx/generated/ from the fetched libvpx source (tools/toolchain/fetch.sh vpx): the headers
# configure writes and the list of sources it builds (sources.txt: libvpx's own libvpx_srcs.txt, its C and assembly
# less vpx_config.c, which each directory holds, and the assembly that is only included), for kp_vpx.cmake. Each
# target twice: <target>/ the VP8 and VP9 decoders (the runtime), <target>-enc/ those and VP9's encoder too (the host's
# tools: kp_convert's cinematics, ADR 0019). generic/ for the C of the generic-gnu target, arm64/ for AArch64's NEON
# (configured as arm64-darwin24-gcc, so run it on an arm64 Mac; the headers are the same for Android and Linux: nothing
# in them names the OS) and x86_64/ for x86-64's SSE2..AVX512 chosen at run time (configured as x86_64-darwin24-gcc
# with runtime CPU detection, which needs NASM on the PATH; the same for Linux and Windows: nothing in them names the OS
# either), vpx_version.h shared. Run after changing the pinned libvpx version, then check the diff (HAVE_PTHREAD_H and
# HAVE_UNISTD_H are set to 0 by hand below: the build uses neither).
set -euo pipefail
cd "$(dirname "$0")/../.."
SRC="$(pwd)/tools/toolchain/vpx/libvpx-1.17.0"
OUT="$(pwd)/third_party/vpx/generated"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
COMMON=(--disable-multithread --disable-vp8-encoder --enable-vp9 --disable-vp9-encoder --disable-vp9-highbitdepth
  --disable-examples --disable-docs --disable-unit-tests --disable-tools --disable-postproc --disable-webm-io
  --disable-libyuv --disable-runtime-cpu-detect --enable-static --disable-shared)
# the assembly libvpx's libs.mk names ASM_INCLUDES: included by the others, it defines nothing of its own
ASM_INCLUDES='^(third_party/x86inc/x86inc\.asm|vpx_config\.asm|vpx_ports/x86_abi_support\.asm|vpx_dsp/x86/bitdepth_conversion_sse2\.asm)$'
generate() { # [LATE="ARGS"] generate DIR CONFIGURE_ARGS...: LATE comes after COMMON (options of COMMON's it turns over)
  local dir="$1" files=(vpx_config.h vpx_config.c vp8_rtcd.h vp9_rtcd.h vpx_dsp_rtcd.h vpx_scale_rtcd.h) late=()
  shift
  [ -n "${LATE:-}" ] && read -r -a late <<<"$LATE"
  mkdir -p "$WORK/$dir" "$OUT/$dir"
  (cd "$WORK/$dir" && "$SRC/configure" "$@" "${COMMON[@]}" ${late[@]+"${late[@]}"} >/dev/null \
    && make -j4 vpx_config.h vp8_rtcd.h vp9_rtcd.h vpx_dsp_rtcd.h vpx_scale_rtcd.h vpx_version.h libvpx_srcs.txt \
      >/dev/null \
    && cp "${files[@]}" "$OUT/$dir/" && cp vpx_version.h "$OUT/" \
    && grep -E '\.(c|asm)$' libvpx_srcs.txt | grep -vE "$ASM_INCLUDES|^vpx_config\.c$" >"$OUT/$dir/sources.txt" \
    && if [ -f vpx_config.asm ]; then cp vpx_config.asm "$OUT/$dir/"; fi)
  sed -i.bak 's/#define HAVE_PTHREAD_H 1/#define HAVE_PTHREAD_H 0/; s/#define HAVE_UNISTD_H 1/#define HAVE_UNISTD_H 0/' \
    "$OUT/$dir/vpx_config.h"
  rm -f "$OUT/$dir/vpx_config.h.bak"
  if [ -f "$OUT/$dir/vpx_config.asm" ]; then
    sed -i.bak 's/^HAVE_PTHREAD_H equ 1/HAVE_PTHREAD_H equ 0/; s/^HAVE_UNISTD_H equ 1/HAVE_UNISTD_H equ 0/' \
      "$OUT/$dir/vpx_config.asm"
    rm -f "$OUT/$dir/vpx_config.asm.bak"
  fi
}
command -v nasm >/dev/null || { echo "regenerate.sh: x86_64/ needs NASM on the PATH" >&2; exit 1; }
ARM64=(--target=arm64-darwin24-gcc --disable-neon_dotprod --disable-neon_i8mm --disable-sve --disable-sve2)
generate generic --target=generic-gnu
generate arm64 "${ARM64[@]}"
LATE=--enable-runtime-cpu-detect generate x86_64 --target=x86_64-darwin24-gcc
LATE=--enable-vp9-encoder generate generic-enc --target=generic-gnu
LATE=--enable-vp9-encoder generate arm64-enc "${ARM64[@]}"
LATE="--enable-runtime-cpu-detect --enable-vp9-encoder" generate x86_64-enc --target=x86_64-darwin24-gcc
echo "regenerated $OUT"
