/* The fog of war as the original draws it over the game view (mask.cpp; atlas P23 0x46b910..0x46c050, P24
 * 0x46c1e0..0x46c560): presentation state, outside the game and its saves.
 *
 * Each frame the visibility of the tiles around the camera becomes a level (0 unexplored, 15 explored, 31 seen),
 * a 3x3 blur smooths the levels and a bilinear expansion turns them into one level per 8x8-pixel block (the level
 * map); a pixel of the view is then darkened through the tileset's dark.pcx row of its level, interpolated between
 * its block's four corner levels. The original's view is 640x400 pixels (grids of 24 x 17 tiles, a level map of
 * 88 x 60 blocks); a wider view grows the grids by its size in tiles (ADR 0009, an approximation outside 640x400). */
#ifndef KP_FOG_VIEW_H
#define KP_FOG_VIEW_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KP_FOG_LEVELS 32       /* dark.pcx rows */
#define KP_FOG_SEEN 31         /* 0x671a80 */
#define KP_FOG_EXPLORED 15     /* 0x671a80 >> 1 */
#define KP_FOG_MAX_COLS 132    /* a view of up to 4096 x 4096 pixels */
#define KP_FOG_MAX_ROWS 132
#define KP_FOG_GRID (KP_FOG_MAX_COLS * KP_FOG_MAX_ROWS)
#define KP_FOG_MAX_LEVELS ((KP_FOG_MAX_COLS - 2) * 4 * (KP_FOG_MAX_ROWS - 2) * 4)

typedef struct {
	int64_t cols, rows;   /* the tile grids: the view's size in tiles + 4 (24 x 17 for 640 x 400) */
	int64_t level_w, level_h; /* the level map: (cols - 2) * 4 x (rows - 2) * 4 (88 x 60) */
	int64_t tile_x, tile_y; /* the camera's tile at the last update (its top-left) */
	uint8_t raw[KP_FOG_GRID];    /* 0x670a78: the tiles' levels, from the camera tile - 1 */
	uint8_t smooth[KP_FOG_GRID]; /* 0x670a70: blurred (its border is never written) */
	uint8_t drawn[KP_FOG_GRID];  /* 0x670a74: the blurred levels the level map was last built from */
	uint8_t levels[KP_FOG_MAX_LEVELS]; /* 0x670a7c: one level per 8x8 block, row stride level_w */
	uint32_t lerp[KP_FOG_LEVELS * KP_FOG_LEVELS]; /* 0x670a80 (BuildFogLerpTable 0x46bfc0) */
} KpFogView;

/* The vision the levels come from: the tile flag DWORDs (byte 0 not visible, byte 1 not explored, one bit per
 * player), the map's size, the local masks (0x672668 not visible, 0x67266c not explored: 1 << local player and
 * that << 8) and in a replay its vision byte and reveal-all (0x67cc84, 0x67cc88). */
typedef struct {
	const int64_t *tiles;
	int64_t map_w, map_h;
	int64_t not_visible, not_explored;
	int replay;
	int64_t replay_vision, replay_reveal;
} KpFogVision;

/* InitFogOfWar 0x46c1e0's part: empty grids and the interpolation table. */
void kp_fog_view_init(KpFogView *v);
/* The levels of a view of view_w x view_h pixels with its top-left at the map pixel (cam_x, cam_y)
 * (SampleAndSmoothFogGrid 0x46b9e0, BuildFogLevelMap 0x46b910). Returns 1 when the level map changed since the last
 * update (the grids or the camera's tile moved), with the changed blocks' bounding box in rect (x0, y0, x1, y1,
 * exclusive), else 0. Fails (returns -1) for a view larger than the grids allow. */
int kp_fog_view_update(KpFogView *v, const KpFogVision *vision, int64_t cam_x, int64_t cam_y, int64_t view_w,
	int64_t view_h, int64_t rect[4]);
/* The level-map block of a map pixel coordinate: the camera tile's first block is 0 (BuildFogLevelMap starts at
 * the camera's tile; DrawFogRect's camera phase (cam >> 3) & 3 is the block's offset inside that tile). */
int64_t kp_fog_view_block(const KpFogView *v, int64_t map_px, int axis);
/* DrawFogOverlay 0x46c3c0 on the CPU, the shader's reference: darkens the view's pixels (palette indices, w x h at
 * stride) through dark (32 x 256 bytes, the tileset's dark.pcx) by the level map (DrawFogRect 0x46bcc0,
 * FogBlackBlock 0x46bdc0, FogUniformBlock 0x46be30, FogGradientBlock 0x46bed0). */
void kp_fog_view_shade(const KpFogView *v, int64_t cam_x, int64_t cam_y, const uint8_t *dark, uint8_t *pixels,
	int64_t w, int64_t h, int64_t stride);

/* The minimap's fog (BuildMinimapFogTables 0x46e840 and the composers 0x46cd10, 0x46cdc0, 0x46cea0 that
 * LayoutMinimap 0x46d850 picks by the larger map side): each minimap pixel's terrain colour through one of four
 * rows, by its tile's (visible, explored): row 0 every byte the unexplored colour (0x6d2377), row 1 dark.pcx row 15,
 * rows 2 and 3 the identity. terrain and out are the minimap's w x h pixels (2W x 2H for a side of 64, W x H for
 * 96 and 128, W/2 x H/2 for 192 and 256); dark is the tileset's 32 x 256 rows. Returns -1 for another side. */
int kp_fog_minimap(const KpFogVision *vision, const uint8_t *terrain, const uint8_t *dark, int unexplored_color,
	uint8_t *out, int64_t *w, int64_t *h);

#ifdef __cplusplus
}
#endif

#endif
