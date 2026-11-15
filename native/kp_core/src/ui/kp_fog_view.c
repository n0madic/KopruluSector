/* The fog of war of the game view (kp_fog_view.h; atlas P23, P24). */
#include "kp_fog_view.h"

#include <string.h>

#define TILE 32
#define BLOCK 8
#define BLOCKS_PER_TILE 4
#define GRID_MARGIN 4 /* the grids reach a tile left of the camera's and past the view's right edge */

void kp_fog_view_init(KpFogView *v)
{
	int a, b, k;
	memset(v, 0, sizeof *v);
	v->tile_x = v->tile_y = -1;
	for (a = 0; a < KP_FOG_LEVELS; a++) {
		for (b = 0; b < KP_FOG_LEVELS; b++) {
			uint32_t entry = 0;
			for (k = 0; k < BLOCKS_PER_TILE; k++) {
				uint32_t byte = (uint32_t)((a * 256 + k * (b - a) * 64) >> 8) & 0xff;
				entry |= byte << (8 * k);
			}
			v->lerp[a * KP_FOG_LEVELS + b] = entry;
		}
	}
}

/* A tile's level (SampleAndSmoothFogGrid 0x46b9e0). */
static uint8_t tile_level(const KpFogVision *vision, uint32_t flags)
{
	if (vision->replay) {
		uint32_t m = (uint32_t)vision->replay_vision & 0xff, open = ~flags;
		if (vision->replay_reveal != 0) return KP_FOG_SEEN;
		if ((open & (m << 8)) == 0) return 0;
		return (m & open & 0xff) != 0 ? KP_FOG_SEEN : KP_FOG_EXPLORED;
	}
	if ((flags & (uint32_t)vision->not_explored) != 0) return 0;
	if ((flags & (uint32_t)vision->not_visible) != 0) return KP_FOG_EXPLORED;
	return KP_FOG_SEEN;
}

static int64_t clamp(int64_t v, int64_t lo, int64_t hi) { return v < lo ? lo : v > hi ? hi : v; }

/* A signed division that truncates toward zero (cdq; and 3; add; sar 2 in 0x46b910). */
static int64_t quarter(int64_t v) { return v / 4; }

int kp_fog_view_update(KpFogView *v, const KpFogVision *vision, int64_t cam_x, int64_t cam_y, int64_t view_w,
	int64_t view_h, int64_t rect[4])
{
	int64_t cols = (view_w + TILE - 1) / TILE + GRID_MARGIN, rows = (view_h + TILE - 1) / TILE + GRID_MARGIN;
	int64_t tx = cam_x / TILE, ty = cam_y / TILE, r, c, k, x0, y0, x1, y1;
	int resized;
	if (cols > KP_FOG_MAX_COLS || rows > KP_FOG_MAX_ROWS || view_w < 1 || view_h < 1) return -1;
	resized = cols != v->cols || rows != v->rows;
	if (resized) { /* a new grid: its border, which the blur never writes, is 0 as allocated (0x46c1e0) */
		memset(v->smooth, 0, sizeof v->smooth);
		memset(v->drawn, 0, sizeof v->drawn);
		v->cols = cols;
		v->rows = rows;
		v->level_w = (cols - 2) * BLOCKS_PER_TILE;
		v->level_h = (rows - 2) * BLOCKS_PER_TILE;
	}
	/* the tiles from (tile - 1) on, clamped to the map: the pointer stops advancing at an edge */
	for (r = 0; r < rows; r++) {
		int64_t my = clamp(ty - 1 + r, 0, vision->map_h - 1);
		for (c = 0; c < cols; c++) {
			int64_t mx = clamp(tx - 1 + c, 0, vision->map_w - 1);
			v->raw[r * cols + c] = tile_level(vision, (uint32_t)vision->tiles[my * vision->map_w + mx]);
		}
	}
	/* the 1 2 1 / 2 4 2 / 1 2 1 blur of the inner cells, / 16 */
	for (r = 1; r < rows - 1; r++) {
		for (c = 1; c < cols - 1; c++) {
			const uint8_t *p = &v->raw[r * cols + c];
			unsigned sum = p[-cols - 1] + 2u * p[-cols] + p[-cols + 1] + 2u * p[-1] + 4u * p[0] + 2u * p[1]
				+ p[cols - 1] + 2u * p[cols] + p[cols + 1];
			v->smooth[r * cols + c] = (uint8_t)(sum >> 4);
		}
	}
	/* the changed blocks: every block when the grid or the camera's tile moved, else those of the changed cells
	 * (UpdateFogMaskDirty 0x46c2d0 compares the blurred levels with the last drawn ones) */
	x0 = y0 = INT64_MAX;
	x1 = y1 = -1;
	if (resized || tx != v->tile_x || ty != v->tile_y) {
		x0 = y0 = 0;
		x1 = v->level_w;
		y1 = v->level_h;
	} else {
		for (r = 1; r < rows - 1; r++) {
			for (c = 1; c < cols - 1; c++) {
				if (v->smooth[r * cols + c] == v->drawn[r * cols + c]) continue;
				/* a cell's change reaches the blocks of the tiles left of and above it too */
				if ((c - 2) * BLOCKS_PER_TILE < x0) x0 = (c - 2) * BLOCKS_PER_TILE;
				if ((r - 2) * BLOCKS_PER_TILE < y0) y0 = (r - 2) * BLOCKS_PER_TILE;
				if (c * BLOCKS_PER_TILE > x1) x1 = c * BLOCKS_PER_TILE;
				if (r * BLOCKS_PER_TILE > y1) y1 = r * BLOCKS_PER_TILE;
			}
		}
		if (x1 < 0) {
			memcpy(v->drawn, v->smooth, (size_t)(rows * cols));
			return 0;
		}
		x0 = clamp(x0, 0, v->level_w);
		y0 = clamp(y0, 0, v->level_h);
		x1 = clamp(x1, 0, v->level_w);
		y1 = clamp(y1, 0, v->level_h);
	}
	memcpy(v->drawn, v->smooth, (size_t)(rows * cols));
	v->tile_x = tx;
	v->tile_y = ty;
	/* BuildFogLevelMap 0x46b910: each tile of blurred rows/columns 1.. as 4 x 4 blocks, its left and right edges
	 * stepped down in 8.8 fixed point and each block row read from the interpolation table */
	for (r = 1; r < rows - 2; r++) {
		for (c = 1; c < cols - 2; c++) {
			int64_t v00 = v->smooth[r * cols + c], v01 = v->smooth[r * cols + c + 1];
			int64_t v10 = v->smooth[(r + 1) * cols + c], v11 = v->smooth[(r + 1) * cols + c + 1];
			int64_t left = v00 * 256, right = v01 * 256;
			int64_t dl = quarter(v10 * 256 - left), dr = quarter(v11 * 256 - right);
			for (k = 0; k < BLOCKS_PER_TILE; k++) {
				uint32_t entry = v->lerp[(left >> 8) * KP_FOG_LEVELS + (right >> 8)];
				uint8_t *out = &v->levels[((r - 1) * BLOCKS_PER_TILE + k) * v->level_w + (c - 1) * BLOCKS_PER_TILE];
				out[0] = (uint8_t)entry;
				out[1] = (uint8_t)(entry >> 8);
				out[2] = (uint8_t)(entry >> 16);
				out[3] = (uint8_t)(entry >> 24);
				left += dl;
				right += dr;
			}
		}
	}
	rect[0] = x0;
	rect[1] = y0;
	rect[2] = x1;
	rect[3] = y1;
	return 1;
}

int64_t kp_fog_view_block(const KpFogView *v, int64_t map_px, int axis)
{
	int64_t tile = axis == 0 ? v->tile_x : v->tile_y;
	return (map_px >> 3) - tile * BLOCKS_PER_TILE;
}

static uint8_t level_at(const KpFogView *v, int64_t bx, int64_t by)
{
	if (bx < 0 || by < 0 || bx >= v->level_w || by >= v->level_h) return 0;
	return v->levels[by * v->level_w + bx];
}

void kp_fog_view_shade(const KpFogView *v, int64_t cam_x, int64_t cam_y, const uint8_t *dark, uint8_t *pixels,
	int64_t w, int64_t h, int64_t stride)
{
	int64_t y, x;
	for (y = 0; y < h; y++) {
		int64_t my = cam_y + y, by = kp_fog_view_block(v, my, 1), row = my & (BLOCK - 1);
		for (x = 0; x < w; x++) {
			int64_t mx = cam_x + x, bx = kp_fog_view_block(v, mx, 0), k = mx & (BLOCK - 1);
			int64_t tl = level_at(v, bx, by), tr = level_at(v, bx + 1, by), bl = level_at(v, bx, by + 1);
			int64_t br = level_at(v, bx + 1, by + 1), level;
			uint8_t *p = &pixels[y * stride + x];
			if (tl == tr && tl == bl && tl == br) {
				if (tl == 0) *p = 0;
				else if (tl != KP_FOG_SEEN) *p = dark[tl * 256 + *p];
				continue;
			}
			{
				int64_t l = tl * 256 + row * (bl - tl) * 32, r = tr * 256 + row * (br - tr) * 32;
				int64_t step = (l - r) >> 3; /* l - r is a multiple of 32 */
				level = (r + (BLOCK - 1 - k) * step) >> 8;
			}
			*p = dark[level * 256 + *p];
		}
	}
}

#define MINIMAP_EXPLORED_ROW 15 /* 0x66ec20: dark.pcx row 15 */

/* A tile's minimap row (vis * 2 | explored, 0x46cd10's classification). */
static int minimap_row(const KpFogVision *vision, uint32_t f)
{
	if (vision->replay) {
		uint32_t m = (uint32_t)vision->replay_vision & 0xff, open = ~f;
		if (vision->replay_reveal != 0) return 2;
		if ((open & (m << 8)) == 0) return 0;
		return (m & open & 0xff) == 0 ? 1 : 2;
	}
	return ((f & (uint32_t)vision->not_visible) == 0) * 2 + ((f & (uint32_t)vision->not_explored) == 0);
}

int kp_fog_minimap(const KpFogVision *vision, const uint8_t *terrain, const uint8_t *dark, int unexplored_color,
	uint8_t *out, int64_t *w, int64_t *h)
{
	int64_t side = vision->map_w > vision->map_h ? vision->map_w : vision->map_h, x, y;
	const uint8_t *row_of[4];
	uint8_t unexplored[256];
	memset(unexplored, unexplored_color, sizeof unexplored);
	row_of[0] = unexplored;
	row_of[1] = dark + MINIMAP_EXPLORED_ROW * 256;
	row_of[2] = row_of[3] = NULL; /* the identity */
	if (side == 64) { /* ComposeMinimapFog2x 0x46cea0: a tile's class for its 2 x 2 pixels */
		*w = 2 * vision->map_w;
		*h = 2 * vision->map_h;
		for (y = 0; y < *h; y++) {
			for (x = 0; x < *w; x++) {
				int r = minimap_row(vision, (uint32_t)vision->tiles[(y / 2) * vision->map_w + x / 2]);
				uint8_t c = terrain[y * *w + x];
				out[y * *w + x] = row_of[r] ? row_of[r][c] : c;
			}
		}
		return 0;
	}
	if (side == 96 || side == 128) { /* ComposeMinimapFog1x 0x46cd10 */
		*w = vision->map_w;
		*h = vision->map_h;
	} else if (side == 192 || side == 256) { /* ComposeMinimapFogHalf 0x46cdc0: every second tile and row */
		*w = vision->map_w / 2;
		*h = vision->map_h / 2;
	} else {
		return -1;
	}
	for (y = 0; y < *h; y++) {
		for (x = 0; x < *w; x++) {
			int64_t tx = side >= 192 ? 2 * x : x, ty = side >= 192 ? 2 * y : y;
			int r = minimap_row(vision, (uint32_t)vision->tiles[ty * vision->map_w + tx]);
			uint8_t c = terrain[y * *w + x];
			out[y * *w + x] = row_of[r] ? row_of[r][c] : c;
		}
	}
	return 0;
}
