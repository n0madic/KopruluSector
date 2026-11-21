/*
 * The chat room's map preview world (InitGameWorldForLobbyPreview 0x443850, atlas P14; the lobby's minimap of
 * kp_ui_minimap.c draws it): the map's megatiles (ImportMapTiles 0x445720 over MTXM) and the units the minimap of a
 * preview shows, the neutral players' (DrawNeutralMinimapUnits 0x46e5b0 with the preview flag; players 0..7 draw
 * nothing there), from a melee load of the scenario (THG2, then UNIT, by the template's melee filter 0x469600).
 *
 * Not the original's world: the units stand at their records' points (CreateUnit's checks, which keep a map's
 * preplaced units where they are, are left out) and no regions are built (the region overflow's report 0x61 is the
 * game start's).
 */
#ifndef KP_PREVIEW_H
#define KP_PREVIEW_H

#include <stddef.h>
#include <stdint.h>

#include "kp_chk.h"
#include "kp_dat.h"

#ifdef __cplusplus
extern "C" {
#endif

/* A unit's box on the minimap (DrawMinimapUnitBox 0x46cb80's arguments). */
typedef struct KpPreviewUnit {
	uint16_t x, y;      /* the unit's point in map pixels */
	uint8_t w, h;       /* the low bytes of its type's placement WORDs 0x522728, 0x52272a */
	uint8_t building;   /* units.dat's flag 0x1 */
	uint8_t resource;   /* a mineral field or a geyser (the minimap's resource colour) */
	uint8_t owner;      /* 8..11 */
} KpPreviewUnit;

typedef struct {
	uint32_t width, height;  /* DIM */
	int tileset;             /* ERA & 7 */
	uint16_t *megatiles;     /* width * height, owned (bit 15 the tile's high bit) */
	KpPreviewUnit *units;    /* owned, in the minimap's drawing order (players 11..8, each player's newest first) */
	int unit_count;
} KpPreview;

/* The tileset index of a melee load (ERA & 7), or -1 without DIM or ERA. */
int kp_preview_tileset(const KpChk *chk);
/* The preview world of a melee load (kp_chk_load KP_CHK_MELEE) over its tileset's CV5 and the DAT's units: 0, or -1
 * with the reason in err (no DIM, a short MTXM, a tile of a missing CV5 group, no units.dat columns, out of memory).
 * template_record: the lobby's template (its starting units byte selects the melee filter); expansion: whether the
 * expansion-only types are created. */
int kp_preview_build(KpPreview *p, const KpChk *chk, const uint8_t *cv5, size_t cv5_size, const KpDat *dat,
	const uint8_t *template_record, int expansion, char *err, size_t err_size);
void kp_preview_free(KpPreview *p);

#ifdef __cplusplus
}
#endif

#endif
