/*
 * GRP metadata: the frame geometry, the opacity runs of the native hit test and the LO files, keyed by images.tbl
 * index, built from the original files (kp_grpmeta_from_files); everything points into the owned blob.
 *
 * Entries are numbered from 1 (image records store them, 0 = none), so grps[0] and los[0]
 * are unused. Frames and images.tbl indices keep their native numbering.
 */
#ifndef KP_GRPMETA_H
#define KP_GRPMETA_H

#include <stddef.h>
#include <stdint.h>

#include "kp_dat.h"
#include "kp_files.h"
#include "kp_kpf.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KP_GRPMETA_FRAME_RECORD 8

typedef struct {
	const uint8_t *text; /* not terminated */
	uint32_t size;
} KpLabel;

typedef struct {
	KpLabel path, animation;
	uint16_t frames, width, height;
	uint32_t atlas; /* 1-based */
	uint32_t first_frame;
} KpGrpEntry;

typedef struct {
	KpLabel path;
	const uint8_t *data;
	uint32_t size, frames, points;
} KpLoEntry;

typedef struct {
	KpLabel name, pack;
	int mask;
} KpAtlas;

typedef struct {
	uint8_t *blob; /* owned: FRMS, OPAC and the LO files */
	size_t size;
	uint32_t tbl_count;
	uint16_t *grp_of_tbl, *lo_of_tbl; /* [images.tbl index 1..tbl_count] -> entry, 0 for none */
	uint32_t atlas_count, grp_count, lo_count;
	KpAtlas *atlases; /* [1..atlas_count] */
	KpGrpEntry *grps; /* [1..grp_count] */
	KpLoEntry *los; /* [1..lo_count] */
	const uint8_t *frms, *opac;
	uint32_t frms_size, opac_size;
} KpGrpMeta;

/* grpmeta built from the original files (ADR 0018, stage D3) by the converter's rules (kpd.grp_set, build_grpmeta):
 * the GRP and LO files images.dat names through images.tbl ("unit\\" + the entry, deduplicated in any
 * case), their frame records and opacity runs, the atlases the packs are cut into (scdata/packs.py). Every GRP's
 * frame records lie inside FRMS (kp_grpmeta_check), so kp_grpmeta_frame needs no bounds checks for valid entries and
 * frames; the opacity runs are not checked (the hit test, kp_grpmeta_pixel_opaque, reads them). -1 with err set. */
int kp_grpmeta_from_files(KpGrpMeta *g, const KpFiles *files, const KpDat *dat, KpError *err);
void kp_grpmeta_free(KpGrpMeta *g);

/* grpmeta built from parts (kp_grpmeta_from_files; the C suites build one, tests/c/test_present.c): zeroed tables for
 * the given counts and `bytes` of storage in blob for FRMS and the LO files, which the caller fills;
 * kp_grpmeta_check then validates the frame records. Both return -1 with err set. */
int kp_grpmeta_alloc(KpGrpMeta *g, uint32_t tbl_count, uint32_t grp_count, uint32_t lo_count, size_t bytes,
	KpError *err);
int kp_grpmeta_check(const KpGrpMeta *g, KpError *err);

/* Entry of images.tbl index `tbl` (GRP or LO), 0 for none or an index outside the table. */
static inline uint32_t kp_grpmeta_grp_of_tbl(const KpGrpMeta *g, uint32_t tbl)
{
	return tbl >= 1 && tbl <= g->tbl_count ? g->grp_of_tbl[tbl] : 0;
}

static inline uint32_t kp_grpmeta_lo_of_tbl(const KpGrpMeta *g, uint32_t tbl)
{
	return tbl >= 1 && tbl <= g->tbl_count ? g->lo_of_tbl[tbl] : 0;
}

/* Frame rectangle as stored in the GRP (frame < the entry's frame count). */
void kp_grpmeta_frame(const KpGrpMeta *g, uint32_t entry, uint32_t frame, int *x, int *y, int *w, int *h);

/* Signed LO point `point` of `frame`, read unchecked like the native getters: the point may
 * be negative (IScript passes signed bytes). Returns 0 when the bytes lie outside the file. */
int kp_grpmeta_pixel_opaque(const KpGrpMeta *g, uint32_t entry, uint32_t frame, int64_t x, int64_t y);
int kp_grpmeta_lo_point(const KpLoEntry *lo, uint32_t frame, int32_t point, int *x, int *y);

#ifdef __cplusplus
}
#endif

#endif
