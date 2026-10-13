/*
 * grpmeta.kpd (kind GRPM; docs/formats/kpd.md), the C counterpart of game/data/grpmeta.lua:
 * GRP frame geometry, the opacity runs of the native hit test and the LO files, keyed by
 * images.tbl index. The loader copies the file; everything points into the copy.
 *
 * Entries keep the Lua reader's 1-based numbering (image records store them, 0 = none), so
 * grps[0] and los[0] are unused. Frames and images.tbl indices keep their native numbering.
 */
#ifndef KP_GRPMETA_H
#define KP_GRPMETA_H

#include <stddef.h>
#include <stdint.h>

#include "kp_kpf.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KP_GRPMETA_KIND "GRPM"
#define KP_GRPMETA_SCHEMA 1
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
	uint8_t *blob; /* owned copy of the file */
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

/* expect may be NULL. Checks that every GRP's frame records lie inside FRMS, so
 * kp_grpmeta_frame needs no bounds checks for valid entries and frames. The opacity runs are
 * the hit test's (presentation) and stay in Lua. Returns -1 with err set. */
int kp_grpmeta_load(KpGrpMeta *g, const uint8_t *data, size_t size, const char *name, const KpKpfExpect *expect,
	KpError *err);
void kp_grpmeta_free(KpGrpMeta *g);

/* grpmeta built from parts instead of loaded (the Lua binding builds one from Lua tables):
 * zeroed tables for the given counts and `bytes` of storage in blob for FRMS and the LO files,
 * which the caller fills; kp_grpmeta_check then validates the frame records as the loader
 * does. Both return -1 with err set. */
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
int kp_grpmeta_lo_point(const KpLoEntry *lo, uint32_t frame, int32_t point, int *x, int *y);

#ifdef __cplusplus
}
#endif

#endif
