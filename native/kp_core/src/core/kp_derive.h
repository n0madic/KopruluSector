/*
 * A mission's terrain-derived data (ADR 0018, stage D4; scdata/derive): what the terrain gives the game before its
 * doodads and units, built from the map's MTXM and the tileset's CV5 and VF4 as the original builds it when a map
 * loads (0x490d80): the imported megatiles and tile flags (ImportMapTiles 0x445720, the console's strips of
 * LoadTileMapSection 0x469170), the walk raster (0x48b1c0), the region graph (0x48f060, then the cut ranks 0x48f0e0) and
 * the collision contours (0x48af10, sorted by the CRT's qsort). The payloads are the mission container's WALK, RGNS and
 * CONT bytes (docs/formats/kpm.md), which kp_mission reads.
 */
#ifndef KP_DERIVE_H
#define KP_DERIVE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KP_DERIVE_REGION_LIMIT 5000
#define KP_DERIVE_CV5_GROUP 52
#define KP_DERIVE_VF4_RECORD 32

/* ImportMapTiles 0x445720 over MTXM (w * h words): each cell's megatile (bit 15 the tile's high bit) and its tile flags
 * (the low word 0xffff, as the terrain's allocation leaves it); mflags: each megatile's flags
 * (kp_terrain_megatile_flags), mflag_count of them, or NULL for the megatiles alone (flags NULL too). 0, or -1 with
 * *bad_cell when a tile selects a CV5 group or a megatile the tileset lacks (the original stops there and the map does
 * not load). */
int kp_derive_import(const uint8_t *mtxm, uint32_t w, uint32_t h, const uint8_t *cv5, size_t cv5_size,
	const uint32_t *mflags, uint32_t mflag_count, uint16_t *ids, uint32_t *flags, int64_t *bad_cell);
/* LoadTileMapSection 0x469170's strips behind the console: the bottom corners (5 x 2) and the bottom row clear
 * 0x20410000, then set 0x00800000. The game builds its regions from these flags. */
void kp_derive_edge_strips(uint32_t w, uint32_t h, uint32_t *flags);
/* The walk raster 0x48b1c0 into out, (w * 4) * (h * 4) bytes, a byte per 8x8-pixel mini-cell: 0x80 blocked (VF4 bit 0
 * clear, or under the console), else bits 1/2/4/8 a blocked or outside neighbour at top/right/bottom/left. 0, or -1
 * when a megatile is past the VF4's records. */
int kp_derive_walk(uint32_t w, uint32_t h, const uint16_t *ids, const uint8_t *vf4, size_t vf4_size, uint8_t *out);
/* RGNS: the region graph of the stripped tile flags and the walk raster, malloc'd; NULL with the reason (more than
 * 5000 regions, which the original refuses too). */
uint8_t *kp_derive_regions(uint32_t w, uint32_t h, const uint32_t *flags, const uint16_t *ids, const uint8_t *vf4,
	size_t vf4_size, const uint8_t *walk, size_t *size, char *err, size_t err_size);
/* CONT: the walk raster's contours, sorted, with the capacities the original's arrays grew to; malloc'd, NULL with the
 * reason. */
uint8_t *kp_derive_contours(uint32_t w, uint32_t h, const uint8_t *walk, size_t *size, char *err, size_t err_size);

/* The three payloads of a map (kp_derive_import, the strips, the walk raster, the regions, the contours), malloc'd. */
typedef struct {
	uint8_t *walk, *regions, *contours;
	size_t walk_size, regions_size, contours_size;
} KpDerived;

/* 0, or -1 with the reason (a bad tile, too many regions). vf4's megatile flags are computed here. */
int kp_derive_terrain(uint32_t w, uint32_t h, const uint8_t *mtxm, const uint8_t *cv5, size_t cv5_size,
	const uint8_t *vf4, size_t vf4_size, KpDerived *out, char *err, size_t err_size);
void kp_derived_free(KpDerived *d);

#ifdef __cplusplus
}
#endif

#endif
