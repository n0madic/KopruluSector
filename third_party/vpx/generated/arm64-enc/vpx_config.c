/* Copyright (c) 2011 The WebM project authors. All Rights Reserved. */
/*  */
/* Use of this source code is governed by a BSD-style license */
/* that can be found in the LICENSE file in the root of the source */
/* tree. An additional intellectual property rights grant can be found */
/* in the file PATENTS.  All contributing project authors may */
/* be found in the AUTHORS file in the root of the source tree. */
#include "vpx/vpx_codec.h"
static const char* const cfg = "--target=arm64-darwin24-gcc --disable-neon_dotprod --disable-neon_i8mm --disable-sve --disable-sve2 --disable-multithread --disable-vp8-encoder --enable-vp9 --disable-vp9-encoder --disable-vp9-highbitdepth --disable-examples --disable-docs --disable-unit-tests --disable-tools --disable-postproc --disable-webm-io --disable-libyuv --disable-runtime-cpu-detect --enable-static --disable-shared --enable-vp9-encoder";
const char *vpx_codec_build_config(void) {return cfg;}
