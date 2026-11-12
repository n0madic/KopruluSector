/*
 * GRP files read as the original reads them (scdata/formats/grp.py; docs/research/native-image-geometry.md): the
 * header (u16 frames, width, height), the frame records (u8 x, y, width, height, u32 payload offset masked with
 * 0x7fffffff) at 6 + 8 * frame, a frame's rows of packets (0x80: skip p & 0x7f pixels; 0x40: repeat the next index
 * p & 0x3f times; else p literal indices). Transparency is the packet's, so index 0 drawn by a packet is opaque.
 */
#ifndef KP_GRP_H
#define KP_GRP_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KP_GRP_HEADER 6
#define KP_GRP_RECORD 8
#define KP_GRP_RUN_TRANSPARENT 0x80 /* an opacity run's class bit (grpmeta's OPAC) */
#define KP_GRP_RUN_MAX 0x7f

typedef struct {
	uint8_t x, y, w, h;
	uint32_t offset; /* the payload's, from the file's start */
} KpGrpFrame;

/* The header: 0, or -1 when the file is shorter than it and its frame records. */
int kp_grp_header(const uint8_t *grp, size_t size, uint32_t *frames, uint32_t *width, uint32_t *height);
/* A frame's record (the header checked: frame < frames). */
void kp_grp_frame(const uint8_t *grp, uint32_t frame, KpGrpFrame *f);
/* A frame decoded (grp.py _decode_frame): w * h indices (0 under a transparent pixel) and mask bytes (255 opaque, 0
 * transparent) where not NULL, and whether a packet draws an opaque index 0 (*opaque_zero, may be NULL); -1 when the
 * rows run past the file or a packet that draws has no length. */
int kp_grp_decode(const uint8_t *grp, size_t size, uint32_t frame, uint8_t *indices, uint8_t *mask, int *opaque_zero);
/* A frame's opacity block (grp.py opacity_block: h u16 row offsets from the block's start, then each row's canonical
 * runs): its size, written to out when it fits cap (out may be NULL to size it), -1 when malformed or past 64 KiB. */
int64_t kp_grp_opacity(const uint8_t *grp, size_t size, uint32_t frame, uint8_t *out, size_t cap);
/* A GRP of one frame (grp.py single_frame: the canvas size, the frame's record, its rows' packets copied as they are, a
 * row the original shares copied for each): malloc'd, its size in *out_size; NULL when the frame or a row is outside
 * the file or the rows outgrow u16 offsets. */
uint8_t *kp_grp_single_frame(const uint8_t *grp, size_t size, uint32_t frame, size_t *out_size);

#ifdef __cplusplus
}
#endif

#endif
