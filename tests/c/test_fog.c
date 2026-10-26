/* The fog of war of the presentation (kp_fog_view.h, native/kp_core/src/core/kp_fog_view.c) on synthetic tile flags
 * (plan phase S, stage S7). The original's own fog passes are the oracle of tools/fog_check.py (the emulator's
 * captures); these cases keep the rules that need no capture: the levels of seen, explored and unexplored tiles, the
 * blur at an edge between them, the uniform and black blocks of the darkening, the blocks a dirty update reports, and
 * the minimap's three map sizes and its rows. */
#include "kp_test.h"

#include <string.h>

#include "../../native/kp_core/src/core/kp_fog_view.h"

#define LOCAL 0 /* the local player's bit */
#define NOT_SEEN (1 << LOCAL)
#define NOT_EXPLORED (1 << (LOCAL + 8))
#define VIEW_W 640
#define VIEW_H 400

typedef int64_t (*FlagFn)(int64_t x, int64_t y, int64_t side);

/* The tile flags of a side x side map: byte 0 the players that do not see the tile, byte 1 those that have not
 * explored it. */
static int64_t *tiles(KpT *T, int64_t side, FlagFn flag)
{
	int64_t *t = (int64_t *)malloc(sizeof *t * (size_t)(side * side)), x, y;
	kp_t_ok(T, t != NULL, "out of memory");
	kp_t_defer(T, free, t);
	for (y = 0; y < side; y++) {
		for (x = 0; x < side; x++) t[y * side + x] = flag(x, y, side);
	}
	return t;
}

static KpFogVision vision_of(const int64_t *t, int64_t side)
{
	KpFogVision v;
	memset(&v, 0, sizeof v);
	v.tiles = t;
	v.map_w = v.map_h = side;
	v.not_visible = NOT_SEEN;
	v.not_explored = NOT_EXPLORED;
	return v;
}

/* dark.pcx rows: level l maps index i to (i + l) % 256. */
static uint8_t *dark(KpT *T)
{
	uint8_t *d = (uint8_t *)malloc(KP_FOG_LEVELS * 256);
	int level, i;
	kp_t_ok(T, d != NULL, "out of memory");
	kp_t_defer(T, free, d);
	for (level = 0; level < KP_FOG_LEVELS; level++) {
		for (i = 0; i < 256; i++) d[level * 256 + i] = (uint8_t)((i + level) % 256);
	}
	return d;
}

static KpFogView *view(KpT *T)
{
	KpFogView *v = (KpFogView *)malloc(sizeof *v);
	kp_t_ok(T, v != NULL, "out of memory");
	kp_t_defer(T, free, v);
	kp_fog_view_init(v);
	return v;
}

static int update(KpFogView *v, const KpFogVision *vision, int64_t cam_x, int64_t cam_y, int64_t rect[4])
{
	return kp_fog_view_update(v, vision, cam_x, cam_y, VIEW_W, VIEW_H, rect);
}

/* The left half of a 64 x 64 map seen, the right half explored, the bottom rows unexplored. */
static int64_t halves(int64_t x, int64_t y, int64_t side)
{
	(void)side;
	if (y >= 48) return NOT_SEEN + NOT_EXPLORED;
	return x < 32 ? 0 : NOT_SEEN;
}

/* The pixel at the view's middle after a darkening of uniform pixels 100 with the camera at (cam_x, cam_y). */
static int shaded(KpT *T, KpFogView *v, const KpFogVision *vision, const uint8_t *d, int64_t cam_x, int64_t cam_y)
{
	uint8_t *pixels = (uint8_t *)malloc(VIEW_W * VIEW_H);
	int64_t rect[4];
	int got;
	kp_t_ok(T, pixels != NULL, "out of memory");
	memset(pixels, 100, VIEW_W * VIEW_H);
	update(v, vision, cam_x, cam_y, rect);
	kp_fog_view_shade(v, cam_x, cam_y, d, pixels, VIEW_W, VIEW_H, VIEW_W);
	got = pixels[VIEW_W * 200 + 320];
	free(pixels);
	return got;
}

static void levels_of_seen_explored_and_unexplored_tiles(KpT *T)
{
	int64_t *t = tiles(T, 64, halves), rect[4], cols;
	KpFogVision vision = vision_of(t, 64);
	KpFogView *v = view(T);
	const uint8_t *d = dark(T);
	int k, edge[22];
	kp_t_eq(T, update(v, &vision, 0, 0, rect), 1, "a first update changes the map");
	cols = v->cols;
	kp_t_eq(T, cols, 24, "the grids' columns");
	kp_t_eq(T, v->rows, 17, "the grids' rows");
	kp_t_eq(T, v->raw[1 * cols + 1], KP_FOG_SEEN, "a seen tile");
	kp_t_eq(T, v->raw[1 * cols + 22], KP_FOG_SEEN, "column 22 is tile 21: seen");
	kp_t_eq(T, v->level_w, 88, "the level map's width");
	kp_t_eq(T, v->level_h, 60, "the level map's height");
	kp_t_eq(T, v->levels[0], KP_FOG_SEEN, "the camera's first block");
	kp_t_eq(T, update(v, &vision, 0, 0, rect), 0, "nothing changed");
	/* a view over the seen and explored halves: the blur steps from 31 to 15 across the edge */
	update(v, &vision, 800, 0, rect);
	for (k = 0; k < 22; k++) edge[k] = v->smooth[5 * cols + k + 1];
	kp_t_eq(T, edge[0], KP_FOG_SEEN, "the edge's seen side");
	kp_t_eq(T, edge[21], KP_FOG_EXPLORED, "the edge's explored side");
	for (k = 1; k < 22; k++) kp_t_ok(T, edge[k] <= edge[k - 1], "the levels fall across the edge (column %d)", k + 1);
	/* the darkening: a seen block keeps its pixels, an explored one takes dark row 15, an unexplored one is black */
	kp_t_eq(T, shaded(T, v, &vision, d, 0, 0), 100, "a seen block");
	kp_t_eq(T, shaded(T, v, &vision, d, 1200, 0), 100 + KP_FOG_EXPLORED, "an explored block through dark row 15");
	kp_t_eq(T, shaded(T, v, &vision, d, 0, 1536 + 64), 0, "an unexplored block is black");
}

static int64_t all_seen(int64_t x, int64_t y, int64_t side)
{
	(void)x;
	(void)y;
	(void)side;
	return 0;
}

static int64_t one_hidden(int64_t x, int64_t y, int64_t side)
{
	(void)side;
	return x == 5 && y == 5 ? NOT_SEEN + NOT_EXPLORED : 0; /* the tile that becomes unexplored */
}

/* A change of the tiles under an unmoved camera reports the blocks it changed (UpdateFogMaskDirty 0x46c2d0), which are
 * all the presentation uploads: every level outside them stays as it was, and the whole map equals a fresh view's. */
static void dirty_update_reports_the_changed_blocks(KpT *T)
{
	int64_t *before_tiles = tiles(T, 64, all_seen), *after_tiles = tiles(T, 64, one_hidden), rect[4], bx, by, w, h;
	KpFogVision before_vision = vision_of(before_tiles, 64), after_vision = vision_of(after_tiles, 64);
	KpFogView *v = view(T), *fresh = view(T);
	uint8_t *before;
	int outside = 0, differ = 0;
	update(v, &before_vision, 0, 0, rect);
	w = v->level_w;
	h = v->level_h;
	before = (uint8_t *)malloc((size_t)(w * h));
	kp_t_ok(T, before != NULL, "out of memory");
	kp_t_defer(T, free, before);
	memcpy(before, v->levels, (size_t)(w * h));
	kp_t_eq(T, update(v, &after_vision, 0, 0, rect), 1, "the changed tile changes the map");
	/* the tile is grid cell 6 (the grids start a tile left of the camera's); the blur reaches cells 5..7, and a cell's
	 * change reaches the blocks of the tiles left of and above it: blocks 12..27 on both axes */
	kp_t_ok(T, rect[0] == 12 && rect[1] == 12 && rect[2] == 28 && rect[3] == 28, "the changed blocks: %lld %lld %lld %lld",
		(long long)rect[0], (long long)rect[1], (long long)rect[2], (long long)rect[3]);
	{
		int64_t fresh_rect[4];
		update(fresh, &after_vision, 0, 0, fresh_rect);
	}
	kp_t_ok(T, fresh->level_w == w && fresh->level_h == h && memcmp(fresh->levels, v->levels, (size_t)(w * h)) == 0,
		"the levels equal a fresh view's");
	for (by = 0; by < h; by++) {
		for (bx = 0; bx < w; bx++) {
			int changed = before[by * w + bx] != v->levels[by * w + bx];
			if (changed) differ++;
			if (changed && !(bx >= rect[0] && bx < rect[2] && by >= rect[1] && by < rect[3])) outside++;
		}
	}
	kp_t_eq(T, outside, 0, "no level outside the changed blocks changed");
	kp_t_ok(T, differ > 0, "levels inside them did");
}

/* A quarter seen, a quarter explored, the right half unexplored. */
static int64_t quarters(int64_t x, int64_t y, int64_t side)
{
	(void)y;
	if (x < side / 4) return 0;
	if (x < side / 2) return NOT_SEEN;
	return NOT_SEEN + NOT_EXPLORED;
}

static void minimap_sizes_and_rows(KpT *T)
{
	static const int64_t SIDES[3] = { 64, 128, 256 };
	const uint8_t *d = dark(T);
	int k;
	for (k = 0; k < 3; k++) {
		int64_t side = SIDES[k], w = side == 64 ? 2 * side : side == 128 ? side : side / 2, ow = 0, oh = 0;
		KpFogVision vision = vision_of(tiles(T, side, quarters), side);
		uint8_t *terrain = (uint8_t *)malloc((size_t)(w * w)), *out = (uint8_t *)malloc((size_t)(w * w));
		int r;
		kp_t_ok(T, terrain != NULL && out != NULL, "out of memory");
		kp_t_defer(T, free, terrain);
		kp_t_defer(T, free, out);
		memset(terrain, 40, (size_t)(w * w));
		r = kp_fog_minimap(&vision, terrain, d, 7, out, &ow, &oh);
		kp_t_eq(T, r, 0, "a minimap for a side of %lld", (long long)side);
		kp_t_eq(T, ow, w, "the minimap's width for a side of %lld", (long long)side);
		kp_t_eq(T, oh, w, "the minimap's height for a side of %lld", (long long)side);
		kp_t_eq(T, out[0], 40, "a seen tile keeps its colour");
		kp_t_eq(T, out[w * 3 / 8], 40 + KP_FOG_EXPLORED, "an explored tile through dark row 15");
		kp_t_eq(T, out[w - 1], 7, "an unexplored tile takes the unexplored colour");
	}
}

static const KpTest TESTS[] = {
	{ "levels_of_seen_explored_and_unexplored_tiles", levels_of_seen_explored_and_unexplored_tiles, 0 },
	{ "dirty_update_reports_the_changed_blocks", dirty_update_reports_the_changed_blocks, 0 },
	{ "minimap_sizes_and_rows", minimap_sizes_and_rows, 0 },
};
KP_SUITE(kp_suite_fog, "test_fog", TESTS);
