/* A Smacker video decoder (plan phase 9B): the portraits and the briefing rooms' SMK files, decoded frame by frame into
 * palette indices the way Smackw32's SmackDoFrame/SmackToBuffer leave them (the video only: the palette records and the
 * audio tracks are skipped). Format: the SMK2/SMK4 header, the four header trees (MMAP, MCLR, FULL, TYPE: two 8-bit
 * Huffman trees recoding a 16-bit big tree with three escape values and the last-three cache), the frames' chunks;
 * libavcodec's smacker decoder and demuxer document the same bit stream.
 *
 * It also plays KPV1, the converter's stand-in for an SMK that only StarCraft: Remastered's WebM copy has (ADR 0013,
 * converter/formats/kpv.py): the SMK header's size, frame count, frame rate and ring flag, one palette, then each
 * stored frame as its byte size and runs of indices. */
#ifndef KP_SMK_H
#define KP_SMK_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KP_SMK_TREES 4

typedef struct {
	int32_t *values; /* SMK_NODE | right offset for a node, else the value */
	int32_t count;
	int32_t last[3];
} KpSmkTree;

typedef struct {
	const uint8_t *data;
	size_t size;
	int v4;
	int kpv;                          /* a KPV1 file */
	uint32_t width, height, frames, flags, ring;
	int32_t frame_rate;               /* the header's: > 0 ms, < 0 in 10 microsecond units, 0 the default */
	const uint8_t *frame_types;
	size_t frames_at, sizes_at, types_at;
	KpSmkTree trees[KP_SMK_TREES];
	uint32_t frame;                   /* the next frame to decode */
	size_t frame_pos;                 /* its data's offset */
	uint8_t *mem;                     /* the one allocation: the trees' entries, then the pixels */
	uint8_t *pixels;                  /* width * height indices, kept between frames */
	uint8_t *target;                  /* kp_smk_set_target: frames decode there instead (SmackToBuffer) */
} KpSmk;

/* Opens an SMK in memory (the bytes must outlive it): 0, or -1 for a malformed file. */
int kp_smk_open(KpSmk *s, const uint8_t *data, size_t size);
void kp_smk_close(KpSmk *s);
/* Decodes the next frame into s->pixels (SmackDoFrame) and advances (SmackNextFrame: after the last frame frame 0, or
 * for a ring movie its extra frame and then frame 1). Returns 0 or -1. */
int kp_smk_decode(KpSmk *s);
/* SmackToBuffer: the frames decode into target (width * height bytes; NULL: the decoder's own pixels). */
void kp_smk_set_target(KpSmk *s, uint8_t *target);
/* SmackGoto(0): back to the first frame, the pixels cleared. */
void kp_smk_rewind(KpSmk *s);
/* The time of a frame in 10 microsecond units (SmackOpen's +0x424). */
uint32_t kp_smk_frame_time(const KpSmk *s);
/* A KPV1's palette (256 x R, G, B; index 0 transparent), NULL for an SMK (its palettes are not decoded). */
const uint8_t *kp_smk_palette(const KpSmk *s);

#ifdef __cplusplus
}
#endif

#endif
