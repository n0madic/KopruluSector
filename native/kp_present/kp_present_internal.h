/*
 * kp_present's helpers shared by its files, not part of its interface (kp_present.h).
 */
#ifndef KP_PRESENT_INTERNAL_H
#define KP_PRESENT_INTERNAL_H

#include "kp_present.h"

/* Opens a TrueType font: 0, or -1 when the bytes are none (stbtt_InitFont alone reads out of bounds then; kp_vtext.c). */
struct stbtt_fontinfo;
int kp_ttf_open(struct stbtt_fontinfo *info, const uint8_t *ttf, size_t size);

/* "name: msg" into err; returns -1 (kp_present_common.c). */
int kp_present_fail(KpError *err, const char *name, const char *msg);
/* The end of a container's sections names[0..count-1] from its first bytes; 0 when one is missing or the bytes are not
 * a container's (kp_present_common.c). */
size_t kp_present_head_size(const uint8_t *first, size_t n, size_t file_size, const char *const *names, int count);

/* An HD pack's FRMS record: u16 page, u16 x, y, width, height, s16 x and y offsets (kp_hd.c, kp_hd_render.c) */
#define KP_HD_FRAME_RECORD 14
/* A square of s texels' BC1 or ETC2 bytes: an HD tile's, a water normal map's */
#define KP_HD_TILE_BC1(s) ((size_t)(s) * (size_t)(s) / 2)

#endif
