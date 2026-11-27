/* The fog of war of the presentation (kp_fog_view.h, native/kp_core/src/ui/kp_fog_view.c) on synthetic tile flags
 * (plan phase S, stage S7). The original's own fog passes are the oracle of tools/fog_check.py (the emulator's
 * captures); these cases keep the rules that need no capture: the levels of seen, explored and unexplored tiles, the
 * blur at an edge between them, the uniform and black blocks of the darkening, the blocks a dirty update reports, the
 * minimap's three map sizes and its rows, and a wider view tiled by 640 x 400 views (ADR 0009: the same tiling
 * `tools/fog_check.py --wide` checks against the original's own fog code). */
#include "kp_test.h"

#include <string.h>

#include "../../native/kp_core/src/ui/kp_fog_view.h"

#define LOCAL 0 /* the local player's bit */
#define NOT_SEEN (1 << LOCAL)
#define NOT_EXPLORED (1 << (LOCAL + 8))
#define VIEW_W 640
#define VIEW_H 400

typedef int64_t (*FlagFn)(int64_t x, int64_t y, int64_t side);

/* The tile flags of a side x side map: byte 0 the players that do not see the tile, byte 1 those that have not
 * explored it. */
static uint32_t *tiles(KpT *T, int64_t side, FlagFn flag)
{
	uint32_t *t = (uint32_t *)malloc(sizeof *t * (size_t)(side * side));
	int64_t x, y;
	kp_t_ok(T, t != NULL, "out of memory");
	kp_t_defer(T, free, t);
	for (y = 0; y < side; y++) {
		for (x = 0; x < side; x++) t[y * side + x] = (uint32_t)flag(x, y, side);
	}
	return t;
}

static KpFogVision vision_of(const uint32_t *t, int64_t side)
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
	uint32_t *t = tiles(T, 64, halves);
	int64_t rect[4], cols;
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
	uint32_t *before_tiles = tiles(T, 64, all_seen), *after_tiles = tiles(T, 64, one_hidden);
	int64_t rect[4], bx, by, w, h;
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

/* --- a wider view tiled by the original's views (ADR 0009) ---
 * Each pixel of a wider view equals the same map pixel's in every 640 x 400 view, at a camera of the original's scroll
 * range on the 8-pixel grid, that shows it. The 640 x 400 view is the original's (fog_check.py on the emulator's
 * captures); fog_check.py --wide checks this tiling against the original's code, this case keeps it where the original
 * is not at hand. */

#define ORIGINAL_BOTTOM 24 /* InitScreenScroll's (H - 12) * 32 + 8: the 400 rows reach 24 pixels below the map */

static uint32_t next_random(uint32_t *s)
{
	*s = *s * 1103515245u + 12345u;
	return *s >> 8;
}

/* A map of unexplored, explored and seen rectangles and single tiles, the other players' bits and the high WORD
 * random. */
static uint32_t *patched_map(KpT *T, int64_t w, int64_t h, uint32_t seed)
{
	uint32_t *t = (uint32_t *)malloc(sizeof *t * (size_t)(w * h));
	int64_t k, x, y;
	uint8_t *state;
	int level, n;
	kp_t_ok(T, t != NULL, "out of memory");
	kp_t_defer(T, free, t);
	state = (uint8_t *)calloc((size_t)(w * h), 1);
	kp_t_ok(T, state != NULL, "out of memory");
	kp_t_defer(T, free, state);
	for (level = 1; level <= 2; level++) {
		for (n = 0; n < 24; n++) {
			int64_t x0 = next_random(&seed) % w, y0 = next_random(&seed) % h;
			int64_t rw = 1 + next_random(&seed) % 24, rh = 1 + next_random(&seed) % 24;
			for (y = y0; y < y0 + rh && y < h; y++) {
				for (x = x0; x < x0 + rw && x < w; x++) state[y * w + x] = (uint8_t)level;
			}
		}
	}
	for (n = 0; n < w * h / 64; n++) {
		uint32_t at = next_random(&seed) % (uint32_t)(w * h);
		state[at] = (uint8_t)(next_random(&seed) % 3);
	}
	for (k = 0; k < w * h; k++) {
		int64_t f = (int64_t)next_random(&seed) << 8;
		f = (f | (next_random(&seed) & 0xff)) & ~(int64_t)(NOT_SEEN | NOT_EXPLORED);
		t[k] = (uint32_t)(f | (state[k] == 0 ? NOT_SEEN | NOT_EXPLORED : state[k] == 1 ? NOT_SEEN : 0));
	}
	return t;
}

/* The w x h view at (cam_x, cam_y) under the fog: (7 x + 13 y) mod 251 of each map pixel. */
static void pattern_view(int64_t cam_x, int64_t cam_y, int64_t w, int64_t h, uint8_t *out)
{
	int64_t x, y;
	for (y = 0; y < h; y++) {
		for (x = 0; x < w; x++) out[y * w + x] = (uint8_t)(((cam_x + x) * 7 + (cam_y + y) * 13) % 251);
	}
}

/* The same view darkened. */
static void shade_view(KpT *T, KpFogView *v, const KpFogVision *vision, const uint8_t *d, int64_t cam_x, int64_t cam_y,
	int64_t w, int64_t h, uint8_t *out)
{
	int64_t rect[4];
	kp_t_ok(T, kp_fog_view_update(v, vision, cam_x, cam_y, w, h, rect) >= 0, "a view of %lld x %lld", (long long)w,
		(long long)h);
	pattern_view(cam_x, cam_y, w, h, out);
	kp_fog_view_shade(v, cam_x, cam_y, d, out, w, h, w);
}

#define COVER_MAX 8 /* the original's views along one side of a wide view */
#define COUNT(a) ((int)(sizeof (a) / sizeof (a)[0]))

static int64_t on_grid(int64_t v) { return v > 0 ? v & ~(int64_t)7 : 0; }

/* The original's view positions (on the 8-pixel grid, 0..top) that cover start..start + size together, at most
 * COVER_MAX of them; fails when they cannot. */
static int cover(KpT *T, int64_t start, int64_t size, int64_t view, int64_t top, int64_t *out)
{
	int n = 0;
	int64_t x = on_grid(start), last = (start + size - view + 7) & ~(int64_t)7;
	while (x + view < start + size) {
		kp_t_ok(T, n < COVER_MAX - 1, "%lld pixels need more than %d views of %lld", (long long)size, COVER_MAX,
			(long long)view);
		out[n++] = x < top ? x : top;
		x += view;
	}
	if (last < 0) last = 0;
	out[n++] = last < top ? last : top;
	kp_t_ok(T, out[0] <= start && out[n - 1] + view >= start + size, "no cover of %lld..%lld by views of %lld up to %lld",
		(long long)start, (long long)(start + size), (long long)view, (long long)top);
	return n;
}

/* A buffer for the largest of n views, each grown by pad pixels on both sides, released when the test ends. */
static uint8_t *view_buffer(KpT *T, const int64_t (*views)[2], int n, int64_t pad)
{
	int64_t area = 0;
	uint8_t *p;
	int k;
	for (k = 0; k < n; k++) {
		if ((views[k][0] + pad) * (views[k][1] + pad) > area) area = (views[k][0] + pad) * (views[k][1] + pad);
	}
	p = (uint8_t *)malloc((size_t)area);
	kp_t_ok(T, p != NULL, "out of memory");
	kp_t_defer(T, free, p);
	return p;
}

static void wide_view_is_the_original_views_tiled(KpT *T)
{
	static const int64_t MAPS[2][2] = { { 64, 64 }, { 128, 96 } };
	static const int64_t VIEWS[5][2] = { { 640, 400 }, { 502, 880 }, { 854, 400 }, { 1001, 517 }, { 1280, 640 } };
	static const int64_t ORIGINAL[1][2] = { { VIEW_W, VIEW_H } };
	const uint8_t *d = dark(T);
	KpFogView *wide = view(T), *orig = view(T);
	uint8_t *mine = view_buffer(T, VIEWS, COUNT(VIEWS), 0), *theirs = view_buffer(T, ORIGINAL, 1, 0);
	int m, k, c, views = 0;
	for (m = 0; m < COUNT(MAPS); m++) {
		int64_t mw = MAPS[m][0], mh = MAPS[m][1];
		KpFogVision vision = vision_of(patched_map(T, mw, mh, (uint32_t)(0x46c3c0 + m)), mw);
		int64_t top_x = mw * 32 - VIEW_W, top_y = mh * 32 - VIEW_H + ORIGINAL_BOTTOM;
		vision.map_h = mh;
		for (k = 0; k < COUNT(VIEWS); k++) {
			int64_t w = VIEWS[k][0], h = VIEWS[k][1];
			int64_t max_x = on_grid(mw * 32 - w), max_y = on_grid(mh * 32 - h + ORIGINAL_BOTTOM);
			int64_t cams[5][2] = { { 0, 0 }, { max_x, 0 }, { 0, max_y }, { max_x, max_y }, { 0, 0 } };
			cams[4][0] = on_grid(max_x / 2 + 13);
			cams[4][1] = on_grid(max_y / 3 + 5);
			for (c = 0; c < COUNT(cams); c++) {
				int64_t cx = cams[c][0], cy = cams[c][1], xs[COVER_MAX], ys[COVER_MAX];
				int nx = cover(T, cx, w, VIEW_W, top_x, xs), ny = cover(T, cy, h, VIEW_H, top_y, ys), i, j;
				shade_view(T, wide, &vision, d, cx, cy, w, h, mine);
				for (j = 0; j < ny; j++) {
					for (i = 0; i < nx; i++) {
						int64_t wx = xs[i], wy = ys[j], x, y;
						shade_view(T, orig, &vision, d, wx, wy, VIEW_W, VIEW_H, theirs);
						views++;
						for (y = 0; y < VIEW_H; y++) {
							int64_t vy = wy + y - cy;
							if (vy < 0 || vy >= h) continue;
							for (x = 0; x < VIEW_W; x++) {
								int64_t vx = wx + x - cx;
								if (vx < 0 || vx >= w || mine[vy * w + vx] == theirs[y * VIEW_W + x]) continue;
								kp_t_fail(T, "map %lld x %lld, view %lld x %lld at (%lld, %lld): map pixel (%lld, %lld) %d, "
									"the 640 x 400 view at (%lld, %lld) %d", (long long)mw, (long long)mh, (long long)w,
									(long long)h, (long long)cx, (long long)cy, (long long)(wx + x), (long long)(wy + y),
									mine[vy * w + vx], (long long)wx, (long long)wy, theirs[y * VIEW_W + x]);
							}
						}
					}
				}
			}
		}
	}
	kp_t_ok(T, views > 100, "the original's views compared: %d", views);
}

/* A camera between the 8-pixel grid's positions (a finger's pan; the original's never is) keeps the fog on the map: an
 * all-seen map stays clear to the view's last pixels (the level map reaches the block after them, which the darkening
 * interpolates towards), and each pixel equals the same map pixel's in a view on the grid a block larger. */
static void off_grid_camera_keeps_the_fog_on_the_map(KpT *T)
{
	static const int64_t VIEWS[4][2] = { { 640, 400 }, { 1280, 640 }, { 1001, 517 }, { 502, 880 } };
	static const int64_t PHASES[6] = { 1, 7, 25, 28, 30, 31 };
	const uint8_t *d = dark(T);
	uint32_t *seen = tiles(T, 64, all_seen);
	KpFogVision clear = vision_of(seen, 64), patched = vision_of(patched_map(T, 64, 64, 0x46b910), 64);
	KpFogView *v = view(T), *grid = view(T);
	uint8_t *mine = view_buffer(T, VIEWS, COUNT(VIEWS), 0), *plain = view_buffer(T, VIEWS, COUNT(VIEWS), 0);
	uint8_t *ref = view_buffer(T, VIEWS, COUNT(VIEWS), 8);
	int k, p, q;
	for (k = 0; k < COUNT(VIEWS); k++) {
		int64_t w = VIEWS[k][0], h = VIEWS[k][1];
		for (p = 0; p < COUNT(PHASES); p++) {
			for (q = 0; q < 3; q++) {
				/* the phase in x (q 0), in y (1) or in both (2), 25..31 the view's last block one past a grid camera's */
				int64_t cx = 320 + (q == 1 ? 0 : PHASES[p]), cy = 288 + (q == 0 ? 0 : PHASES[COUNT(PHASES) - 1 - p]);
				int64_t gx = cx & ~(int64_t)7, gy = cy & ~(int64_t)7, x, y;
				shade_view(T, v, &clear, d, cx, cy, w, h, mine);
				pattern_view(cx, cy, w, h, plain); /* a seen block keeps its pixels */
				kp_t_ok(T, memcmp(mine, plain, (size_t)(w * h)) == 0,
					"%lld x %lld at (%lld, %lld): an all-seen map stays clear", (long long)w, (long long)h, (long long)cx,
					(long long)cy);
				shade_view(T, v, &patched, d, cx, cy, w, h, mine);
				shade_view(T, grid, &patched, d, gx, gy, w + 8, h + 8, ref);
				for (y = 0; y < h; y++) {
					for (x = 0; x < w; x++) {
						uint8_t a = mine[y * w + x], b = ref[(cy - gy + y) * (w + 8) + cx - gx + x];
						if (a == b) continue;
						kp_t_fail(T, "%lld x %lld at (%lld, %lld): map pixel (%lld, %lld) %d, the view on the grid %d",
							(long long)w, (long long)h, (long long)cx, (long long)cy, (long long)(cx + x),
							(long long)(cy + y), a, b);
					}
				}
			}
		}
	}
}

static const KpTest TESTS[] = {
	{ "levels_of_seen_explored_and_unexplored_tiles", levels_of_seen_explored_and_unexplored_tiles, 0 },
	{ "dirty_update_reports_the_changed_blocks", dirty_update_reports_the_changed_blocks, 0 },
	{ "minimap_sizes_and_rows", minimap_sizes_and_rows, 0 },
	{ "wide_view_is_the_original_views_tiled", wide_view_is_the_original_views_tiled, 0 },
	{ "off_grid_camera_keeps_the_fog_on_the_map", off_grid_camera_keeps_the_fog_on_the_map, 0 },
};
KP_SUITE(kp_suite_fog, "test_fog", TESTS);
