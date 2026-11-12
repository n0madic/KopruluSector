/*
 * The HD art of BC blocks written again for phones (ADR 0013, stage R4f; ADR 0019, stage C5: kp_convert repack's sets
 * "hd2-astc" and "hd2-etc2", KP_HD_REPACK_*): S3TC is an extension phones rarely have.
 *
 *   ETC2   an HD pack's (HDPK) pages: the diffuse in ETC2 RGBA8 (EAC alpha), the team mask in EAC R11, a lit page's
 *          lighting layers in EAC RG11 and ETC2 RGB8 (kp_hd_light_to_etc2); an HD tileset's (HDTS) tiles and the
 *          water's normal maps (HDWA) as ETC2 RGB8 from their BC1 (kp_etc.c)
 *   ASTC   an HD pack's pages in kp_hd_layer_codec's blocks (the Arm encoder: built with KP_ASTC), its normal maps and
 *          occlusion in EAC as ETC2's (no ASTC block reaches them); the tiles and normal maps as ETC2's
 *
 * The frames, the water masks, creep's edges (RGBA8) and the headers' other fields stay. kp_hd_phone measures the
 * blocks' choice (--report, --report-tiles) through the same codecs.
 *
 * TODO: the tiles and the water's normal maps in ASTC too? A 64-pixel tile is a whole number of blocks only at 4 x 4
 * (twice ETC2's bytes) and 8 x 8, which kp_gfx's terrain atlas needs. Measured by --report-tiles (ADR 0013): 8 x 8 mostly
 * falls below ETC2, softer, and saves 36 MB; 4 x 4 is near BC1 itself and costs 72 MB.
 */
#ifndef KP_HD_TRANSCODE_H
#define KP_HD_TRANSCODE_H

#include <stddef.h>
#include <stdint.h>

#include "kp_present.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The codecs' settings in kp_convert's cache of the transcodings (<BC SHA-256>-<etc2|astc>-<settings>.kpd): a change
 * of what a transcoding writes takes another. */
#define KP_HD_TRANSCODE_SETTINGS "t2" /* t2: the ASTC encoder invariant (kp_astcenc.cmake and its patch) */

/* A container's bytes (an HD pack, tileset or the water's normal maps, of BC blocks) in format (KP_HD_FORMAT_ETC2 or
 * _ASTC), its pages encoded on up to threads threads: malloc'd, or NULL with err (ASTC where built without KP_ASTC). */
uint8_t *kp_hd_transcode(const uint8_t *file, size_t size, int format, int threads, size_t *out_size, KpError *err);
/* Whether this build encodes ASTC (KP_ASTC). */
int kp_hd_transcode_astc(void);

/* The codecs' pieces kp_hd_phone measures with: */
/* Whether a BC pack's layer is in BC3 (else BC1). */
int kp_hd_transcode_bc3(int layer);
/* The file offset of a page's layer (0: a page not lit has no lighting). */
uint32_t kp_hd_transcode_offset(const KpHdPack *p, int32_t page, int layer);
/* One page's layer of BC blocks in ETC2's format; -1 out of memory. */
int kp_hd_transcode_etc2_layer(int layer, const uint8_t *bc, int32_t size, uint8_t *out);
/* count images of size squared, BC1 blocks, into ETC2 RGB8 ones (the same size); -1 out of memory. */
int kp_hd_transcode_bc1_etc2(const uint8_t *in, int32_t count, int32_t size, uint8_t *out);

#ifdef KP_ASTC
/* An encoder and decoder of a layer's blocks, for one thread (which then needs no reset between pages). */
typedef struct KpHdAstc KpHdAstc;
/* A codec of the layer's input (what the shader reads of it) in side x side blocks at quality (astcenc.h's
 * ASTCENC_PRE_*); NULL failed. */
KpHdAstc *kp_hd_astc_open(int layer, int side, float quality);
void kp_hd_astc_close(KpHdAstc *c);
/* RGBA8 of size squared into blocks (out_size bytes); -1 failed. */
int kp_hd_astc_encode(KpHdAstc *c, uint8_t *rgba, int32_t size, uint8_t *out, size_t out_size);
/* The blocks back into RGBA8, as the GPU reads them (no swizzle); -1 failed. */
int kp_hd_astc_decode(KpHdAstc *c, const uint8_t *blocks, size_t blocks_size, int32_t size, uint8_t *rgba);
/* The bytes of a size-squared image in side x side blocks. */
size_t kp_hd_astc_bytes(int32_t size, int side);
#endif

#ifdef __cplusplus
}
#endif

#endif
