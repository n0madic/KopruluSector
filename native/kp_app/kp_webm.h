/* A WebM movie's frames as RGBA (ADR 0018, stage D6): StarCraft: Remastered's portraits (VP9) and glue movies (VP8 and
 * VP9 with an alpha stream), the HD portraits, demuxed by nestegg and decoded by libvpx. The picture's colours are
 * converted as the cinematics' player converts them (kp_vpx_rgba); an alpha stream (Matroska's BlockAdditional 1, a
 * second VP8 or VP9 stream whose luma is the opacity, as FFmpeg's libvpx wrapper reads it) gives the alpha, else 255. */
#ifndef KP_WEBM_H
#define KP_WEBM_H

#include <stddef.h>
#include <stdint.h>

#include "kp_present.h"
#include "kp_smk.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct KpWebm KpWebm;

#define KP_WEBM_MAX_SIDE 4096

/* Opens a WebM in memory (the bytes must outlive it): its first video track, VP8 or VP9. NULL with the reason in err
 * for one it cannot play. */
KpWebm *kp_webm_open(const uint8_t *data, size_t size, char *err, size_t err_size);
void kp_webm_close(KpWebm *w);
/* The width and height of a WebM's first video track, its headers read only (no decoder): 0, -1 when it has none
 * (converter/formats/kpw.py webm_size). */
int kp_webm_size(const uint8_t *data, size_t size, uint32_t *width, uint32_t *height);
uint32_t kp_webm_width(const KpWebm *w);
uint32_t kp_webm_height(const KpWebm *w);
/* Frame n as RGBA (width * height * 4 bytes, rows top down; the last frame for n past the end), valid until the next
 * call: decoded on from the frame before, or from the first again for an earlier one. NULL for a malformed movie or
 * one without frames. */
const uint8_t *kp_webm_frame(KpWebm *w, uint32_t n);
/* kp_smk's codec of a KPW1's WebM (kp_smk_set_video): these movies */
extern const KpSmkVideo kp_webm_smk_video;

/* --- libvpx's pictures (const vpx_image_t *), the cinematics' (kp_cine.c) and the WebM's --- */

/* Whether a picture is one the players show: 8-bit planar of width x height, 4:4:4 (sRGB or YCbCr), 4:4:0, 4:2:2 or
 * 4:2:0. */
int kp_vpx_usable(const void *image, uint32_t width, uint32_t height);
/* A usable picture (YCbCr, or 4:4:4 sRGB's G, B, R planes) as RGBA of its size with alpha 255: BT.709 when the picture
 * says so, else BT.601 (unspecified: FFmpeg's guess too), its range; the chroma of the pixel's sample (no
 * interpolation). */
void kp_vpx_rgba(const void *image, uint8_t *rgba);
/* The same picture as its planes and the matrix of kp_vpx_rgba, for the GPU (kp_gfx_video). */
void kp_vpx_picture(const void *image, KpVideoPicture *out);
/* The studio-range (limited: 16..235, chroma 16..240) or full-range YCbCr of a pixel as 8-bit RGB by that conversion;
 * matrix 601 or 709. */
void kp_vpx_yuv_rgb(int y, int cb, int cr, int matrix, int full_range, uint8_t rgb[3]);

#ifdef __cplusplus
}
#endif

#endif
