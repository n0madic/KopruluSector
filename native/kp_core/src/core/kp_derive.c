/* A mission's terrain-derived data (kp_derive.h): converter/derive/terrain.py, walk.py, regions.py and contours.py in
 * C, the same control flow (docs/research/native-region-generation.md, native-contours.md). */
#include "kp_derive.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kp_kpf.h"
#include "kp_terrain.h"

/* --- the import (terrain.py) --- */

#define CV5_FLAG_MASK 0xd8fau
#define FLAG_TILE_HIGH_BIT 0x00400000u
#define INITIAL_LOW_WORD 0xffffu
#define FLAG_PARTIAL_WALK 0x20000000u
#define EDGE_CLEAR 0x20410000u
#define EDGE_SET 0x00800000u

int kp_derive_import(const uint8_t *mtxm, uint32_t w, uint32_t h, const uint8_t *cv5, size_t cv5_size,
	const uint32_t *mflags, uint32_t mflag_count, uint16_t *ids, uint32_t *flags, int64_t *bad_cell)
{
	size_t groups = cv5_size / KP_DERIVE_CV5_GROUP, i, n = (size_t)w * h;
	for (i = 0; i < n; i++) {
		uint32_t raw = kp_le_u16(mtxm + 2 * i), group = (raw >> 4) & 0x7ff, mega, f;
		const uint8_t *g;
		if (group >= groups) {
			*bad_cell = (int64_t)i;
			return -1;
		}
		g = cv5 + (size_t)group * KP_DERIVE_CV5_GROUP;
		mega = kp_le_u16(g + 20 + (raw & 15) * 2);
		if (mflags && (mega & 0x7fff) >= mflag_count) {
			*bad_cell = (int64_t)i;
			return -1;
		}
		f = mflags ? INITIAL_LOW_WORD | (kp_le_u16(g + 2) & CV5_FLAG_MASK) << 16 | mflags[mega & 0x7fff] : 0;
		if (raw & 0x8000) {
			mega |= 0x8000;
			f |= FLAG_TILE_HIGH_BIT;
		}
		ids[i] = (uint16_t)mega;
		if (flags) flags[i] = f;
	}
	return 0;
}

void kp_derive_edge_strips(uint32_t w, uint32_t h, uint32_t *flags)
{
	const int64_t rects[3][4] = { { 0, (int64_t)h - 2, 5, 2 }, { (int64_t)w - 5, (int64_t)h - 2, 5, 2 },
		{ 0, (int64_t)h - 1, w, 1 } };
	int k;
	for (k = 0; k < 3; k++) {
		int64_t x0 = rects[k][0], y0 = rects[k][1], x, y;
		for (y = y0 > 0 ? y0 : 0; y < y0 + rects[k][3] && y < (int64_t)h; y++)
			for (x = x0 > 0 ? x0 : 0; x < x0 + rects[k][2] && x < (int64_t)w; x++) {
				uint32_t *f = &flags[(size_t)y * w + (size_t)x];
				*f = (*f & ~EDGE_CLEAR) | EDGE_SET;
			}
	}
}

/* --- the walk raster (walk.py) --- */

#define BLOCKED 0x80
#define UI_ROWS 8
#define UI_FULL_ROWS 4
#define UI_MARGIN 20

int kp_derive_walk(uint32_t w, uint32_t h, const uint16_t *ids, const uint8_t *vf4, size_t vf4_size, uint8_t *out)
{
	size_t ww = (size_t)w * 4, wh = (size_t)h * 4, records = vf4_size / KP_DERIVE_VF4_RECORD, tx, ty, x, y;
	uint8_t *blocked = (uint8_t *)malloc(ww * wh > 0 ? ww * wh : 1);
	if (!blocked) return -1;
	for (ty = 0; ty < h; ty++)
		for (tx = 0; tx < w; tx++) {
			size_t mega = ids[ty * w + tx] & 0x7fffu, k;
			if (mega >= records) {
				free(blocked);
				return -1;
			}
			for (k = 0; k < 16; k++) /* 0x48ad80: VF4 bit 0 set walkable */
				blocked[(ty * 4 + k / 4) * ww + tx * 4 + k % 4] = (kp_le_u16(vf4 + mega * KP_DERIVE_VF4_RECORD + 2 * k) & 1) ? 0 : 1;
		}
	for (y = wh > UI_ROWS ? wh - UI_ROWS : 0; y < wh; y++)
		for (x = 0; x < ww; x++)
			if (y >= wh - UI_FULL_ROWS || x < UI_MARGIN || x + UI_MARGIN >= ww) blocked[y * ww + x] = 1;
	for (y = 0; y < wh; y++)
		for (x = 0; x < ww; x++) {
			size_t p = y * ww + x;
			if (blocked[p]) {
				out[p] = BLOCKED;
				continue;
			}
			out[p] = (uint8_t)((y == 0 || blocked[p - ww]) | (x + 1 == ww || blocked[p + 1]) << 1
				| (y + 1 == wh || blocked[p + ww]) << 2 | (x == 0 || blocked[p - 1]) << 3);
		}
	free(blocked);
	return 0;
}

/* --- the region graph (regions.py) --- */

#define CLASS_UNWALKABLE 0x1ffd
#define CLASS_MIDDLE 0x1ff9
#define CLASS_HIGH 0x1ffa
#define CLASS_LOW 0x1ffb
#define CLASS_EMPTY 0x1fff /* an empty record; also the outside-map neighbour */
#define REGION_LIMIT KP_DERIVE_REGION_LIMIT
#define SPLIT_BASE 0x2000
#define GROUP_RESERVED 0x4000
#define INITIAL_PREVIOUS_AREA (7 * 8)
#define LOOKAHEAD_CANDIDATES 25
#define MERGE_THRESHOLD 6
#define MERGE_THRESHOLD_STEP 2
#define MERGE_REPEAT_LIVE 2500
#define MAX_CUT_RANK 5
#define RGNS_HEADER 12
#define RGNS_REGION 32
#define SPLIT_RECORD 6

typedef struct {
	int32_t *v;
	int n, cap;
} List;

typedef struct {
	int32_t mask, walkable, blocked;
} Split;

typedef struct {
	int32_t w, h, ww;
	const uint8_t *walk;
	int32_t *raster; /* w * h: a class marker (>= REGION_LIMIT), a region, or SPLIT_BASE + a split */
	int32_t count;
	int32_t cls[REGION_LIMIT], grp[REGION_LIMIT], tiles[REGION_LIMIT], ground[REGION_LIMIT], zero[REGION_LIMIT];
	int32_t cx[REGION_LIMIT], cy[REGION_LIMIT], left[REGION_LIMIT], top[REGION_LIMIT], right[REGION_LIMIT];
	int32_t bottom[REGION_LIMIT], rank[REGION_LIMIT];
	List nb[REGION_LIMIT], found[REGION_LIMIT];
	Split *splits;
	int32_t split_count, split_cap;
	int oom;
} Builder;

static void push(Builder *b, List *l, int32_t v)
{
	if (l->n == l->cap) {
		int cap = l->cap ? 2 * l->cap : 8;
		int32_t *grown = (int32_t *)realloc(l->v, (size_t)cap * sizeof *grown);
		if (!grown) {
			b->oom = 1;
			return;
		}
		l->v = grown;
		l->cap = cap;
	}
	l->v[l->n++] = v;
}

static int has(const List *l, int32_t v)
{
	int k;
	for (k = 0; k < l->n; k++)
		if (l->v[k] == v) return 1;
	return 0;
}

static int32_t min32(int32_t a, int32_t b) { return a < b ? a : b; }
static int32_t max32(int32_t a, int32_t b) { return a > b ? a : b; }

/* signed division by 32 truncating toward zero (cdq/and/add/sar) */
static int32_t div32(int32_t v) { return v < 0 ? -((-v) >> 5) : v >> 5; }

/* 0x490e9f */
static int32_t tile_class(uint32_t f)
{
	uint32_t v;
	if ((f & 0x410000u) == 0) return CLASS_UNWALKABLE;
	v = f & 0x6010000u;
	if (v == 0) return CLASS_UNWALKABLE;
	if (v == 0x2010000u) return CLASS_MIDDLE;
	if (v == 0x4010000u) return CLASS_HIGH;
	return CLASS_LOW;
}

/* 0x490f50 */
static void bottom_region(Builder *b)
{
	int32_t i = b->count++, x;
	b->cls[i] = CLASS_UNWALKABLE;
	b->cx[i] = (b->w * 32 / 2 + 16) * 256; /* 16 pixels right of the bounds' midpoint */
	b->cy[i] = b->h * 0x2000 - 0x1000;
	b->left[i] = 0;
	b->top[i] = b->h * 32 - 32;
	b->right[i] = b->w * 32;
	b->bottom[i] = b->h * 32;
	for (x = 0; x < b->w; x++) b->raster[(b->h - 1) * b->w + x] = i;
	b->tiles[i] = b->w;
	b->grp[i] = GROUP_RESERVED;
}

/* 0x490b80 */
static void find_rectangle(const Builder *b, int32_t x, int32_t y, int32_t cls, int32_t rect[4])
{
	const int32_t *r = b->raster;
	int32_t w = b->w, max_x = min32(x + 8, w), max_y = min32(y + 7, b->h), ex = x + 1, ey = y + 1, cw, ch, k;
	int x_good = 1, y_good = 1, corner_good = 1;
	while (ex < max_x && ey < max_y) {
		if (x_good) { /* column ex, rows y..ey-1 */
			for (k = y; k < ey && x_good; k++) x_good = r[k * w + ex] == cls;
		} else if (!y_good) {
			break;
		}
		if (y_good) /* row ey, columns x..ex-1 */
			for (k = x; k < ex && y_good; k++) y_good = r[ey * w + k] == cls;
		if (r[ey * w + ex] != cls) corner_good = 0;
		if (corner_good) {
			if (y_good) ey++;
			if (x_good) ex++;
		} else if (y_good) {
			ey++;
		} else if (x_good) {
			ex++;
		}
	}
	cw = ex - x;
	ch = ey - y;
	if (ch < cw) cw = min32(cw, ch * 3);
	else ch = min32(ch, cw * 3);
	rect[0] = x;
	rect[1] = y;
	rect[2] = x + cw;
	rect[3] = y + ch;
}

/* 0x48fff0 */
static void expand(Builder *b, int32_t index, int32_t cls, int32_t left, int32_t top, int32_t right, int32_t bottom)
{
	int32_t *r = b->raster, w = b->w, h = b->h, pass, x, y;
	for (pass = 0; pass < 2; pass++)
		for (y = top; y < bottom; y++)
			for (x = left; x < right; x++) {
				int32_t p = y * w + x;
				if (r[p] == cls && ((y > 0 && r[p - w] == index) || (x > 0 && r[p - 1] == index)
					|| (x + 1 < w && r[p + 1] == index) || (y + 1 < h && r[p + w] == index)))
					r[p] = index;
			}
}

static void grow_bounds(const Builder *b, int32_t *left, int32_t *top, int32_t *right, int32_t *bottom)
{
	if (*left > 0) (*left)--;
	if (*right < b->w) (*right)++;
	if (*top > 0) (*top)--;
	if (*bottom < b->h) (*bottom)++;
}

/* 0x48f8e0 over the whole map; 0, or -1 past REGION_LIMIT */
static int initial_rectangles(Builder *b)
{
	int32_t *r = b->raster, w = b->w, h = b->h, initial = b->count, cur_x = 0, cur_y = 0;
	int32_t previous = INITIAL_PREVIOUS_AREA;
	int expanded_all = 0;
	for (;;) {
		int32_t best[4] = { 0, 0, 0, 0 }, best_area = 0, best_cls = 0, candidates = 0, x = cur_x, y = cur_y, sx, sy;
		int32_t left, top, right, bottom, area, index, yy, xx;
		if (x >= w) {
			x = 0;
			if (++y >= h) y = 0;
		}
		sx = x;
		sy = y;
		for (;;) {
			int32_t v = r[y * w + x];
			if (v >= REGION_LIMIT) {
				int32_t rect[4], a;
				find_rectangle(b, x, y, v, rect);
				candidates++;
				a = (rect[3] - rect[1]) * (rect[2] - rect[0]);
				if (best_area < a) {
					memcpy(best, rect, sizeof best);
					best_area = a;
					best_cls = v;
				}
				if (best_area >= previous || candidates > LOOKAHEAD_CANDIDATES) {
					cur_x = best[2];
					cur_y = best[1];
					break;
				}
				x = rect[2] - 1;
			}
			if (++x >= w) {
				x = 0;
				if (++y >= h) y = 0;
			}
			if (x == sx && y == sy) break; /* a full wrap: the next search starts from the same place */
		}
		if (b->count >= REGION_LIMIT) return -1;
		left = best[0];
		top = best[1];
		right = best[2];
		bottom = best[3];
		area = (bottom - top) * (right - left);
		previous = area;
		if (area == 0) return 0;
		index = b->count;
		for (yy = top; yy < bottom; yy++)
			for (xx = left; xx < right; xx++) r[yy * w + xx] = index;
		b->cx[index] = (right - left) / 2 + left;
		b->cy[index] = (bottom - top) / 2 + top;
		grow_bounds(b, &left, &top, &right, &bottom);
		expand(b, index, best_cls, left, top, right, bottom);
		b->left[index] = left;
		b->top[index] = top;
		b->right[index] = right;
		b->bottom[index] = bottom;
		b->cls[index] = best_cls;
		b->count++;
		if (area <= 6 && !expanded_all) {
			int32_t i;
			expanded_all = 1;
			for (i = initial; i < b->count; i++) {
				grow_bounds(b, &b->left[i], &b->top[i], &b->right[i], &b->bottom[i]);
				expand(b, i, b->cls[i], b->left[i], b->top[i], b->right[i], b->bottom[i]);
			}
		}
	}
}

/* 0x490330 */
static void counts_bounds(Builder *b)
{
	int32_t n = b->count, i, x, y;
	for (i = 0; i < n; i++) {
		b->tiles[i] = 0;
		b->left[i] = b->top[i] = 0x7fff;
		b->right[i] = b->bottom[i] = -1;
	}
	for (y = 0; y < b->h; y++)
		for (x = 0; x < b->w; x++) {
			int32_t v = b->raster[y * b->w + x], px = x * 32, py = y * 32;
			if (v >= REGION_LIMIT) continue;
			b->tiles[v]++;
			if (py < b->top[v]) b->top[v] = py;
			if (px < b->left[v]) b->left[v] = px;
			if (b->bottom[v] < py + 32) b->bottom[v] = py + 32;
			if (b->right[v] < px + 32) b->right[v] = px + 32;
		}
	for (i = 0; i < n; i++) {
		if (b->tiles[i] == 0) b->cls[i] = CLASS_EMPTY;
		b->tiles[i] &= 0xffff;
	}
}

/* 0x490a60: top, left, right, bottom, top-left, top-right, bottom-left, bottom-right */
static void eight(const Builder *b, int32_t x, int32_t y, int32_t out[8])
{
	const int32_t *r = b->raster;
	int32_t w = b->w, p = y * w + x;
	int t = y > 0, lf = x > 0, rt = x + 1 < w, bt = y + 1 < b->h;
	out[0] = t ? r[p - w] : CLASS_EMPTY;
	out[1] = lf ? r[p - 1] : CLASS_EMPTY;
	out[2] = rt ? r[p + 1] : CLASS_EMPTY;
	out[3] = bt ? r[p + w] : CLASS_EMPTY;
	out[4] = t && lf ? r[p - w - 1] : CLASS_EMPTY;
	out[5] = t && rt ? r[p - w + 1] : CLASS_EMPTY;
	out[6] = bt && lf ? r[p + w - 1] : CLASS_EMPTY;
	out[7] = bt && rt ? r[p + w + 1] : CLASS_EMPTY;
}

static int clear2x2(const Builder *b, int32_t p)
{
	const uint8_t *wk = b->walk;
	return !((wk[p] | wk[p + 1] | wk[p + b->ww] | wk[p + b->ww + 1]) & BLOCKED);
}

/* 0x490500: the diagonal walk test between two ordinary regions */
static int diagonal(const Builder *b, int k, int32_t x, int32_t y)
{
	int32_t ww = b->ww, p = y * 4 * ww + x * 4;
	if (k == 4)
		return clear2x2(b, p) && clear2x2(b, p - 2 * ww - 2) && (clear2x2(b, p - 2) || clear2x2(b, p - 2 * ww));
	if (k == 5)
		return clear2x2(b, p + 2) && clear2x2(b, p - 2 * ww + 4) && (clear2x2(b, p + 4) || clear2x2(b, p - 2 * ww + 2));
	if (k == 6)
		return clear2x2(b, p + 2 * ww) && clear2x2(b, p + 4 * ww - 2)
			&& (clear2x2(b, p + 2 * ww - 2) || clear2x2(b, p + 4 * ww));
	return clear2x2(b, p + 2 * ww + 2) && clear2x2(b, p + 4 * ww + 4)
		&& (clear2x2(b, p + 2 * ww + 4) || clear2x2(b, p + 4 * ww + 2));
}

/* 0x4902e0 / 0x490500 / 0x4910c0: each region scans its own tiles row-major, so one row-major pass visits every
 * region's tiles in the native order */
static void neighbor_arrays(Builder *b)
{
	int32_t n = b->count, i, x, y, k;
	for (i = 0; i < n; i++) b->found[i].n = 0;
	for (y = 0; y < b->h; y++)
		for (x = 0; x < b->w; x++) {
			int32_t e[8], same = 0, accept_all;
			List *f;
			i = b->raster[y * b->w + x];
			eight(b, x, y, e);
			for (k = 0; k < 8; k++) same += e[k] == i;
			if (same == 8) continue;
			f = &b->found[i];
			accept_all = b->cls[i] == CLASS_UNWALKABLE;
			for (k = 0; k < 8; k++) {
				int32_t v = e[k];
				if (v == CLASS_EMPTY || v == i || has(f, v)) continue;
				if (k < 4 || accept_all || b->cls[v] == CLASS_UNWALKABLE || diagonal(b, k, x, y)) push(b, f, v);
			}
		}
	for (i = 0; i < n; i++) {
		const List *f = &b->found[i];
		List *nb = &b->nb[i];
		int32_t ground = 0, zeros = 0;
		if (b->tiles[i] == 0) continue; /* an emptied region keeps its old list */
		nb->n = 0;
		for (k = 0; k < f->n; k++)
			if (f->v[k] != 0 && b->cls[f->v[k]] != CLASS_UNWALKABLE) push(b, nb, f->v[k]);
		ground = nb->n;
		for (k = 0; k < f->n; k++)
			if (f->v[k] != 0 && b->cls[f->v[k]] == CLASS_UNWALKABLE) push(b, nb, f->v[k]);
		for (k = 0; k < f->n; k++)
			if (f->v[k] == 0) {
				push(b, nb, 0);
				zeros++;
			}
		b->ground[i] = ground;
		b->zero[i] = zeros;
	}
}

/* 0x4901f0 / 0x490e20 */
static void groups(Builder *b, int32_t *stack)
{
	int32_t n = b->count, i, group = 1;
	for (i = 0; i < n; i++)
		if (b->grp[i] < GROUP_RESERVED) b->grp[i] = 0;
	for (i = 0; i < n; i++) {
		int32_t top = 0;
		if (b->grp[i] != 0 || b->tiles[i] == 0) continue;
		b->grp[i] = group;
		stack[top++] = i;
		while (top > 0) {
			int32_t c = stack[--top], k, from, to;
			if (b->cls[c] == CLASS_UNWALKABLE) {
				from = b->ground[c];
				to = b->nb[c].n;
			} else {
				from = 0;
				to = b->ground[c];
			}
			for (k = from; k < to; k++) {
				int32_t v = b->nb[c].v[k];
				if (b->grp[v] == 0) {
					b->grp[v] = group;
					stack[top++] = v;
				}
			}
		}
		group++;
	}
}

/* 0x48f870 */
static void refresh(Builder *b, int32_t first, int32_t *stack)
{
	int32_t i;
	counts_bounds(b);
	neighbor_arrays(b);
	for (i = first; i < b->count; i++) {
		if (b->cx[i] < 0x100) b->cx[i] = b->cx[i] * 0x2000 + 0x1000;
		if (b->cy[i] < 0x100) b->cy[i] = b->cy[i] * 0x2000 + 0x1000;
	}
	groups(b, stack);
}

/* 0x48fc80 */
static void merge(Builder *b)
{
	int32_t threshold = MERGE_THRESHOLD, *r = b->raster, w = b->w;
	for (;;) {
		int32_t s, live = 0, i;
		for (s = b->count - 1; s >= 0; s--) {
			int32_t target = -1, k, x, y, x_lo, x_hi;
			if (b->tiles[s] == 0 || b->grp[s] >= GROUP_RESERVED || b->tiles[s] >= threshold || b->nb[s].n == 0) continue;
			for (k = 0; k < b->nb[s].n; k++) {
				int32_t v = b->nb[s].v[k];
				if (b->tiles[v] != 0 && b->grp[v] < GROUP_RESERVED && b->cls[s] == b->cls[v]
					&& (target < 0 || b->tiles[v] < b->tiles[target]))
					target = v;
			}
			if (target < 0) continue;
			x_lo = div32(b->left[s]);
			x_hi = div32(b->right[s]);
			for (y = div32(b->top[s]); y < div32(b->bottom[s]); y++)
				for (x = x_lo; x < x_hi; x++)
					if (r[y * w + x] == s) r[y * w + x] = target;
			b->tiles[target] = (b->tiles[target] + b->tiles[s]) & 0xffff;
			b->tiles[s] = 0;
			b->cls[s] = CLASS_EMPTY;
			b->top[target] = min32(b->top[target], b->top[s]);
			b->bottom[target] = max32(b->bottom[target], b->bottom[s]);
			b->left[target] = min32(b->left[target], b->left[s]);
			b->right[target] = max32(b->right[target], b->right[s]);
		}
		for (i = 0; i < b->count; i++) live += b->tiles[i] != 0;
		if (b->count < 1 || live < MERGE_REPEAT_LIVE) return;
		threshold += MERGE_THRESHOLD_STEP;
	}
}

/* 0x48feb0 */
static void compact(Builder *b, int32_t *mapping)
{
	int32_t n = b->count, i, j = 0, live = 0, k;
	size_t p;
	for (i = 0; i < n; i++) {
		mapping[i] = live;
		live += b->tiles[i] != 0;
	}
	for (i = 0; i < n; i++) {
		List keep;
		if (b->tiles[i] == 0) {
			free(b->nb[i].v);
			memset(&b->nb[i], 0, sizeof b->nb[i]);
			continue;
		}
		for (k = 0; k < b->nb[i].n; k++) b->nb[i].v[k] = mapping[b->nb[i].v[k]];
		keep = b->nb[i];
		memset(&b->nb[i], 0, sizeof b->nb[i]);
		b->nb[j] = keep;
		b->cls[j] = b->cls[i];
		b->grp[j] = b->grp[i];
		b->tiles[j] = b->tiles[i];
		b->ground[j] = b->ground[i];
		b->zero[j] = b->zero[i];
		b->cx[j] = b->cx[i];
		b->cy[j] = b->cy[i];
		b->left[j] = b->left[i];
		b->top[j] = b->top[i];
		b->right[j] = b->right[i];
		b->bottom[j] = b->bottom[i];
		b->rank[j] = b->rank[i];
		j++;
	}
	b->count = live;
	for (p = 0; p < (size_t)b->w * (size_t)b->h; p++) b->raster[p] = mapping[b->raster[p]];
}

static int blk(const Builder *b, int32_t p) { return b->walk[p] >> 7; }

/* 0x48f1b0: blocked (or walkable) mini-cells just outside side or corner i of a tile */
static int beyond(const Builder *b, int32_t p, int i, int blocked)
{
	int32_t ww = b->ww, cells[4];
	int n = 0, count, k;
	switch (i) {
	case 0: cells[0] = p - ww, cells[1] = p - ww + 1, cells[2] = p - ww + 2, cells[3] = p - ww + 3, count = 4; break;
	case 1: cells[0] = p - 1, cells[1] = p + ww - 1, cells[2] = p + 2 * ww - 1, cells[3] = p + 3 * ww - 1, count = 4; break;
	case 2: cells[0] = p + 4, cells[1] = p + ww + 4, cells[2] = p + 2 * ww + 4, cells[3] = p + 3 * ww + 4, count = 4; break;
	case 3: cells[0] = p + 4 * ww, cells[1] = p + 4 * ww + 1, cells[2] = p + 4 * ww + 2, cells[3] = p + 4 * ww + 3, count = 4; break;
	case 4: cells[0] = p - ww - 1, count = 1; break;
	case 5: cells[0] = p - ww + 4, count = 1; break;
	case 6: cells[0] = p + 4 * ww - 1, count = 1; break;
	default: cells[0] = p + 4 * ww + 4, count = 1; break;
	}
	for (k = 0; k < count; k++) n += blk(b, cells[k]);
	return blocked ? n : count - n;
}

static int add_split(Builder *b, int32_t mask, int32_t walkable, int32_t blocked)
{
	if (b->split_count == b->split_cap) {
		int32_t cap = b->split_cap ? 2 * b->split_cap : 64;
		Split *grown = (Split *)realloc(b->splits, (size_t)cap * sizeof *grown);
		if (!grown) return -1;
		b->splits = grown;
		b->split_cap = cap;
	}
	b->splits[b->split_count].mask = mask;
	b->splits[b->split_count].walkable = walkable;
	b->splits[b->split_count].blocked = blocked;
	b->split_count++;
	return 0;
}

/* 0x48f1b0: 0, or -1 (out of memory, or more splits than the raster's words name) */
static int split_cells(Builder *b, const uint32_t *flags, const uint16_t *ids, const uint8_t *vf4)
{
	int32_t w = b->w, ww = b->ww, x, y;
	for (y = 0; y < b->h; y++)
		for (x = 0; x < w; x++) {
			int32_t cell = y * w + x, cur = b->raster[cell], n8[8], p = y * 4 * ww + x * 4, best = 0, best_i = 0, i;
			int32_t mask = 0, rec[3];
			const uint8_t *words;
			int k;
			if (!(flags[cell] & FLAG_PARTIAL_WALK)) continue;
			words = vf4 + (size_t)(ids[cell] & 0x7fff) * KP_DERIVE_VF4_RECORD;
			for (k = 0; k < 16; k++) mask |= (int32_t)((~kp_le_u16(words + 2 * k) & 1u) << k);
			eight(b, x, y, n8);
			if (b->cls[cur] != CLASS_UNWALKABLE) {
				int c0 = blk(b, p), c3 = blk(b, p + 3), c12 = blk(b, p + 3 * ww), c15 = blk(b, p + 3 * ww + 3);
				int counts[8];
				counts[0] = c0 + blk(b, p + 1) + blk(b, p + 2) + c3;
				counts[1] = c0 + blk(b, p + ww) + blk(b, p + 2 * ww) + c12;
				counts[2] = c3 + blk(b, p + ww + 3) + blk(b, p + 2 * ww + 3) + c15;
				counts[3] = c12 + blk(b, p + 3 * ww + 1) + blk(b, p + 3 * ww + 2) + c15;
				counts[4] = c0;
				counts[5] = c3;
				counts[6] = c12;
				counts[7] = c15;
				for (i = 7; i >= 0; i--) {
					int32_t v = n8[i];
					int ok;
					if (counts[i] <= best || v == cur) continue;
					if (v < SPLIT_BASE) ok = v < REGION_LIMIT && b->cls[v] == CLASS_UNWALKABLE;
					else ok = beyond(b, p, i, 1) != 0;
					if (ok) {
						best = counts[i];
						best_i = i;
					}
				}
				if (best == 0) {
					rec[0] = 0, rec[1] = cur, rec[2] = cur;
				} else {
					int32_t other = n8[best_i];
					if (other >= SPLIT_BASE) other = b->splits[other - SPLIT_BASE].blocked;
					if (other == cur) rec[0] = 0, rec[1] = cur, rec[2] = cur;
					else rec[0] = mask, rec[1] = cur, rec[2] = other;
				}
			} else {
				int counts[4];
				counts[0] = 4 - blk(b, p) - blk(b, p + 1) - blk(b, p + 2) - blk(b, p + 3);
				counts[1] = 4 - blk(b, p) - blk(b, p + ww) - blk(b, p + 2 * ww) - blk(b, p + 3 * ww);
				counts[2] = 4 - blk(b, p + 3) - blk(b, p + ww + 3) - blk(b, p + 2 * ww + 3) - blk(b, p + 3 * ww + 3);
				counts[3] = 4 - blk(b, p + 3 * ww) - blk(b, p + 3 * ww + 1) - blk(b, p + 3 * ww + 2) - blk(b, p + 3 * ww + 3);
				for (i = 3; i >= 0; i--) {
					int32_t v = n8[i];
					int ok;
					if (counts[i] <= best || v == cur) continue;
					if (v < SPLIT_BASE) ok = v < REGION_LIMIT && b->cls[v] != CLASS_UNWALKABLE;
					else ok = beyond(b, p, i, 0) != 0;
					if (ok) {
						best = counts[i];
						best_i = i;
					}
				}
				if (best == 0) {
					rec[0] = 0xffff, rec[1] = cur, rec[2] = cur;
				} else {
					int32_t other = n8[best_i];
					if (other >= SPLIT_BASE) other = b->splits[other - SPLIT_BASE].walkable;
					if (other == cur) rec[0] = 0xffff, rec[1] = cur, rec[2] = cur;
					else if (b->cls[other] != CLASS_UNWALKABLE) rec[0] = mask, rec[1] = other, rec[2] = cur;
					else rec[0] = 0xffff, rec[1] = other, rec[2] = cur;
				}
			}
			if (SPLIT_BASE + b->split_count > 0xffff) return -1;
			b->raster[cell] = SPLIT_BASE + b->split_count;
			if (add_split(b, rec[0], rec[1], rec[2]) != 0) return -1;
		}
	return 0;
}

/* 0x48f0e0 */
static void cut_ranks(Builder *b)
{
	int32_t i;
	for (i = 0; i < b->count; i++) {
		int32_t seen[MAX_CUT_RANK], rank = 0, k;
		int broke = 0;
		for (k = b->ground[i]; k < b->nb[i].n; k++) {
			int32_t v = b->nb[i].v[k], g = b->grp[v], s;
			if (b->tiles[v] <= 3) continue;
			for (s = 0; s < rank && seen[s] != g; s++) {
			}
			if (s < rank) continue;
			rank++;
			if (rank >= MAX_CUT_RANK) {
				broke = 1;
				break;
			}
			seen[rank - 1] = g;
		}
		if (!broke && rank < 2) rank = 0;
		b->rank[i] = rank;
	}
}

static uint8_t *encode_regions(const Builder *b, size_t *size, char *err, size_t err_size)
{
	size_t pool = 0, at, raster = (size_t)b->w * (size_t)b->h, k;
	int32_t i;
	uint8_t *out, *q;
	for (i = 0; i < b->count; i++) {
		if (b->nb[i].n > 0xff) {
			snprintf(err, err_size, "region %d has %d neighbours", (int)i, b->nb[i].n);
			return NULL;
		}
		pool += (size_t)b->nb[i].n;
	}
	*size = RGNS_HEADER + (size_t)b->count * RGNS_REGION + 2 * pool + 2 * raster + (size_t)b->split_count * SPLIT_RECORD;
	if (!(out = (uint8_t *)malloc(*size))) {
		snprintf(err, err_size, "out of memory");
		return NULL;
	}
	kp_le_put16(out, (uint32_t)b->w);
	kp_le_put16(out + 2, (uint32_t)b->h);
	kp_le_put16(out + 4, (uint32_t)b->count);
	kp_le_put16(out + 6, (uint32_t)b->split_count);
	kp_le_put32(out + 8, (uint32_t)pool);
	q = out + RGNS_HEADER;
	for (i = 0, at = 0; i < b->count; i++, q += RGNS_REGION) {
		kp_le_put16(q, (uint32_t)b->cls[i]);
		kp_le_put16(q + 2, (uint32_t)b->grp[i]);
		kp_le_put16(q + 4, (uint32_t)b->tiles[i]);
		q[6] = (uint8_t)b->ground[i];
		q[7] = (uint8_t)b->nb[i].n;
		q[8] = (uint8_t)b->zero[i];
		q[9] = (uint8_t)b->rank[i];
		kp_le_put16(q + 10, 0);
		kp_le_put32(q + 12, (uint32_t)at);
		kp_le_put32(q + 16, (uint32_t)b->cx[i]);
		kp_le_put32(q + 20, (uint32_t)b->cy[i]);
		kp_le_put16(q + 24, (uint32_t)b->left[i]);
		kp_le_put16(q + 26, (uint32_t)b->top[i]);
		kp_le_put16(q + 28, (uint32_t)b->right[i]);
		kp_le_put16(q + 30, (uint32_t)b->bottom[i]);
		at += (size_t)b->nb[i].n;
	}
	for (i = 0; i < b->count; i++)
		for (k = 0; k < (size_t)b->nb[i].n; k++, q += 2) kp_le_put16(q, (uint32_t)b->nb[i].v[k]);
	for (k = 0; k < raster; k++, q += 2) kp_le_put16(q, (uint32_t)b->raster[k]);
	for (i = 0; i < b->split_count; i++, q += SPLIT_RECORD) {
		kp_le_put16(q, (uint32_t)b->splits[i].mask);
		kp_le_put16(q + 2, (uint32_t)b->splits[i].walkable);
		kp_le_put16(q + 4, (uint32_t)b->splits[i].blocked);
	}
	return out;
}

static void builder_free(Builder *b)
{
	int32_t i;
	for (i = 0; i < REGION_LIMIT; i++) {
		free(b->nb[i].v);
		free(b->found[i].v);
	}
	free(b->splits);
	free(b->raster);
	free(b);
}

uint8_t *kp_derive_regions(uint32_t w, uint32_t h, const uint32_t *flags, const uint16_t *ids, const uint8_t *vf4,
	size_t vf4_size, const uint8_t *walk, size_t *size, char *err, size_t err_size)
{
	Builder *b = (Builder *)calloc(1, sizeof *b);
	int32_t *scratch = (int32_t *)malloc(REGION_LIMIT * sizeof *scratch);
	size_t n = (size_t)w * h, p;
	uint8_t *out = NULL;
	*size = 0;
	for (p = 0; p < n; p++)
		if ((size_t)(ids[p] & 0x7fff) >= vf4_size / KP_DERIVE_VF4_RECORD) {
			snprintf(err, err_size, "cell %u: megatile %u past the VF4's records", (unsigned)p, (unsigned)(ids[p] & 0x7fff));
			free(scratch);
			free(b);
			return NULL;
		}
	if (!b || !scratch || w == 0 || h == 0 || !(b->raster = (int32_t *)malloc(n * sizeof *b->raster))) {
		snprintf(err, err_size, w && h ? "out of memory" : "an empty map");
		goto done;
	}
	b->w = (int32_t)w;
	b->h = (int32_t)h;
	b->ww = (int32_t)w * 4;
	b->walk = walk;
	for (p = 0; p < n; p++) b->raster[p] = tile_class(flags[p]);
	bottom_region(b);
	if (initial_rectangles(b) != 0) {
		snprintf(err, err_size, "more than %d regions", REGION_LIMIT);
		goto done;
	}
	refresh(b, 1, scratch);
	merge(b);
	compact(b, scratch);
	refresh(b, 1, scratch);
	if (split_cells(b, flags, ids, vf4) != 0) {
		snprintf(err, err_size, "the split cells: out of memory or more than %d", 0xffff - SPLIT_BASE);
		goto done;
	}
	cut_ranks(b);
	if (b->oom) {
		snprintf(err, err_size, "out of memory");
		goto done;
	}
	out = encode_regions(b, size, err, err_size);
done:
	free(scratch);
	if (b) builder_free(b);
	return out;
}

/* --- the contours (contours.py) --- */

#define CONT_HEADER 16
#define CONT_RECORD 8
#define INITIAL_CAPACITY 5000
#define CAPACITY_STEP 2500
#define QSORT_CUTOFF 8

/* 0x508f08 (start) and 0x508f28 (end): pixel offsets (x, y) from cell * 8, by direction + 4 * mode */
static const int8_t START_OFFSETS[16] = { -1, -1, 8, -1, 8, 8, -1, 8, 0, -1, 8, 0, 7, 8, -1, 7 };
static const int8_t END_OFFSETS[16] = { 8, -1, 8, 8, -1, 8, -1, -1, 7, -1, 8, 7, 0, 8, -1, 0 };
static const int8_t STEPS[4][2] = { { 0, -1 }, { 1, 0 }, { 0, 1 }, { -1, 0 } };     /* 0x508f50 */
static const int8_t DIAGONALS[4][2] = { { 1, -1 }, { 1, 1 }, { -1, 1 }, { -1, -1 } }; /* 0x508f58 */

typedef struct {
	int32_t axis, start, end, dir, info;
} Contour;

typedef struct {
	Contour *v;
	size_t n, cap;
} Contours;

typedef struct {
	uint8_t *g;
	int64_t ww, wh, size;
	int bad;
} Trace;

/* a direction bit clear (cells outside the map read as zero) */
static int free_dir(const Trace *t, int64_t x, int64_t y, int d)
{
	if (x >= 0 && x < t->ww && y >= 0 && y < t->wh) return !(t->g[y * t->ww + x] & (1 << d));
	return 1;
}

/* g[y * ww + x] ^= bit, indexed as the Python twin indexes its bytearray (a negative index from the end) */
static void toggle(Trace *t, int64_t x, int64_t y, int d)
{
	int64_t i = y * t->ww + x;
	if (i < 0) i += t->size;
	if (i < 0 || i >= t->size) {
		t->bad = 1;
		return;
	}
	t->g[i] ^= (uint8_t)(1 << d);
}

static int append(Contours *c, const Contour *r)
{
	if (c->n == c->cap) {
		size_t cap = c->cap ? 2 * c->cap : 256;
		Contour *grown = (Contour *)realloc(c->v, cap * sizeof *grown);
		if (!grown) return -1;
		c->v = grown;
		c->cap = cap;
	}
	c->v[c->n++] = *r;
	return 0;
}

/* 0x48af50's scan: from the 4-cell group of the last start, the first byte (wrapping) with one of its low 7 bits set;
 * then from that byte's group the first signed byte >= 1. -1 when no direction bit is left. */
static int64_t next_start(const Trace *t, int64_t pos)
{
	int64_t group = pos & ~(int64_t)3, i, m = -1;
	for (i = group; i < t->size && m < 0; i++)
		if (t->g[i] & 0x7f) m = i;
	for (i = 0; i < group && m < 0; i++)
		if (t->g[i] & 0x7f) m = i;
	if (m < 0) return -1;
	for (i = m & ~(int64_t)3; i < t->size; i++)
		if (t->g[i] >= 1 && t->g[i] <= 0x7f) return i;
	return -2;
}

/* the records in the original's append order and the arrays' common capacity; 0, or -1 */
static int trace(Trace *t, Contours out[4], int64_t *capacity)
{
	int64_t pos = 0, longest = 0;
	*capacity = INITIAL_CAPACITY;
	for (;;) {
		int64_t x, y, cx, cy, sx, sy;
		int mode, d, k;
		pos = next_start(t, pos);
		if (pos == -1) return 0;
		if (pos < 0) return -1;
		y = pos / t->ww;
		x = pos % t->ww;
		if (free_dir(t, x, y, 0) && free_dir(t, x, y, 1) && !free_dir(t, x, y, 2)) {
			x--;
			y++;
		}
		mode = !free_dir(t, x, y, 0) && !free_dir(t, x, y, 3) ? 0 : 1;
		for (d = 0; d < 4 && free_dir(t, x, y, d); d++) {
		}
		if (d == 4) return -1;
		k = d + 4 * mode;
		cx = x * 8 + START_OFFSETS[k * 2];
		cy = y * 8 + START_OFFSETS[k * 2 + 1];
		sx = cx;
		sy = cy;
		toggle(t, x, y, d);
		for (;;) {
			int nd = (d + 1) & 3, next_walk = 0, f0, f1;
			int64_t dx = STEPS[nd][0], dy = STEPS[nd][1], ex, ey;
			Contour rec;
			toggle(t, x, y, d);
			if (free_dir(t, x, y, nd)) {
				next_walk = 1;
				while (!free_dir(t, x + dx, y + dy, d)) {
					x += dx;
					y += dy;
					toggle(t, x, y, d);
					if (!free_dir(t, x, y, nd)) {
						next_walk = 0;
						break;
					}
				}
			}
			k = d + 4 * next_walk;
			ex = x * 8 + END_OFFSETS[k * 2];
			ey = y * 8 + END_OFFSETS[k * 2 + 1];
			f0 = (d ^ 2 * mode ^ (~(2 * d) & 3)) & 3;
			f1 = (d ^ 2 * (next_walk ^ (d & 1)) ^ 1) & 3;
			if (longest == *capacity) *capacity += CAPACITY_STEP; /* 0x48b040: an array is full before this append */
			rec.dir = d;
			if (d == 0) {
				rec.axis = (int32_t)cy, rec.start = (int32_t)cx, rec.end = (int32_t)ex;
				rec.info = f0 | f1 << 2 | mode << 4 | next_walk << 5;
			} else if (d == 1) {
				rec.axis = (int32_t)cx, rec.start = (int32_t)cy, rec.end = (int32_t)ey;
				rec.info = f0 | f1 << 2 | mode << 4 | next_walk << 5;
			} else if (d == 2) {
				rec.axis = (int32_t)cy, rec.start = (int32_t)ex, rec.end = (int32_t)cx;
				rec.info = f1 | f0 << 2 | next_walk << 4 | mode << 5;
			} else {
				rec.axis = (int32_t)cx, rec.start = (int32_t)ey, rec.end = (int32_t)cy;
				rec.info = f1 | f0 << 2 | next_walk << 4 | mode << 5;
			}
			if (append(&out[d], &rec) != 0) return -1;
			if ((int64_t)out[d].n > longest) longest = (int64_t)out[d].n;
			if (!next_walk) {
				d = nd;
			} else {
				x += DIAGONALS[d][0];
				y += DIAGONALS[d][1];
				d = (d - 1) & 3;
			}
			cx = ex;
			cy = ey;
			mode = next_walk;
			if (t->bad) return -1;
			if (cx == sx && cy == sy) break;
		}
		toggle(t, x, y, d);
		if (t->bad) return -1;
	}
}

static int64_t key(const Contour *r) { return (int64_t)r->axis * 65536 + r->start; }

static void swap_at(Contour *a, int64_t i, int64_t j)
{
	Contour t = a[i];
	a[i] = a[j];
	a[j] = t;
}

/* MSVC CRT qsort 0x4d4490 (shortsort 0x4d47e0) with comparator 0x48ad40 on (axis, start), signed: the same partitions,
 * median of three, swaps and stack, so equal keys keep the original's order (signed indices, as the CRT's pointers
 * may step below the partition) */
static void native_qsort(Contour *a, int64_t n)
{
	int64_t lo_stack[64], hi_stack[64], lo, hi;
	int depth = 0;
	if (n < 2) return;
	lo = 0;
	hi = n - 1;
	for (;;) {
		int64_t size = hi - lo + 1;
		if (size <= QSORT_CUTOFF) {
			int64_t h = hi;
			while (h > lo) {
				int64_t m = lo, q;
				for (q = lo + 1; q <= h; q++)
					if (key(&a[q]) > key(&a[m])) m = q;
				if (m != h) swap_at(a, m, h);
				h--;
			}
		} else {
			int64_t mid = lo + size / 2, loguy, higuy;
			if (key(&a[lo]) > key(&a[mid])) swap_at(a, lo, mid);
			if (key(&a[lo]) > key(&a[hi])) swap_at(a, lo, hi);
			if (key(&a[mid]) > key(&a[hi])) swap_at(a, mid, hi);
			loguy = lo;
			higuy = hi;
			for (;;) {
				if (mid > loguy) {
					loguy++;
					while (loguy < mid && key(&a[loguy]) <= key(&a[mid])) loguy++;
				}
				if (mid <= loguy) {
					loguy++;
					while (loguy <= hi && key(&a[loguy]) <= key(&a[mid])) loguy++;
				}
				higuy--;
				while (higuy > mid && key(&a[higuy]) > key(&a[mid])) higuy--;
				if (higuy < loguy) break;
				swap_at(a, loguy, higuy);
				if (mid == higuy) mid = loguy;
			}
			higuy++;
			if (mid < higuy) {
				higuy--;
				while (higuy > mid && key(&a[higuy]) == key(&a[mid])) higuy--;
			}
			if (mid >= higuy) {
				higuy--;
				while (higuy > lo && key(&a[higuy]) == key(&a[mid])) higuy--;
			}
			if (higuy - lo >= hi - loguy) {
				if (lo < higuy) {
					lo_stack[depth] = lo;
					hi_stack[depth++] = higuy;
				}
				if (loguy < hi) {
					lo = loguy;
					continue;
				}
			} else {
				if (loguy < hi) {
					lo_stack[depth] = loguy;
					hi_stack[depth++] = hi;
				}
				if (lo < higuy) {
					hi = higuy;
					continue;
				}
			}
		}
		if (depth == 0) return;
		depth--;
		lo = lo_stack[depth];
		hi = hi_stack[depth];
	}
}

uint8_t *kp_derive_contours(uint32_t w, uint32_t h, const uint8_t *walk, size_t *size, char *err, size_t err_size)
{
	Trace t;
	Contours c[4];
	int64_t capacity = 0;
	size_t total = 0;
	uint8_t *out = NULL, *q;
	int d;
	memset(c, 0, sizeof c);
	memset(&t, 0, sizeof t);
	*size = 0;
	t.ww = (int64_t)w * 4;
	t.wh = (int64_t)h * 4;
	t.size = t.ww * t.wh;
	if (!(t.g = (uint8_t *)malloc(t.size ? (size_t)t.size : 1))) {
		snprintf(err, err_size, "out of memory");
		return NULL;
	}
	memcpy(t.g, walk, (size_t)t.size);
	if (trace(&t, c, &capacity) != 0) {
		snprintf(err, err_size, "the contours: a walk raster the original's traversal does not finish");
		goto done;
	}
	for (d = 0; d < 4; d++) {
		if (c[d].n > 0xffff || capacity > 0xffff) {
			snprintf(err, err_size, "the contours: more than 65535 in a direction");
			goto done;
		}
		native_qsort(c[d].v, (int64_t)c[d].n);
		total += c[d].n;
	}
	*size = CONT_HEADER + total * CONT_RECORD;
	if (!(out = (uint8_t *)malloc(*size))) {
		snprintf(err, err_size, "out of memory");
		goto done;
	}
	for (d = 0; d < 4; d++) {
		kp_le_put16(out + 2 * d, (uint32_t)c[d].n);
		kp_le_put16(out + 8 + 2 * d, (uint32_t)capacity);
	}
	q = out + CONT_HEADER;
	for (d = 0; d < 4; d++) {
		size_t k;
		for (k = 0; k < c[d].n; k++, q += CONT_RECORD) {
			kp_le_put16(q, (uint32_t)c[d].v[k].axis);
			kp_le_put16(q + 2, (uint32_t)c[d].v[k].start);
			kp_le_put16(q + 4, (uint32_t)c[d].v[k].end);
			q[6] = (uint8_t)c[d].v[k].dir;
			q[7] = (uint8_t)c[d].v[k].info;
		}
	}
done:
	for (d = 0; d < 4; d++) free(c[d].v);
	free(t.g);
	return out;
}

/* --- the whole terrain --- */

int kp_derive_terrain(uint32_t w, uint32_t h, const uint8_t *mtxm, const uint8_t *cv5, size_t cv5_size,
	const uint8_t *vf4, size_t vf4_size, KpDerived *out, char *err, size_t err_size)
{
	size_t n = (size_t)w * h, records = vf4_size / KP_DERIVE_VF4_RECORD;
	uint32_t *mflags = (uint32_t *)malloc((records ? records : 1) * sizeof *mflags);
	uint32_t *flags = (uint32_t *)malloc((n ? n : 1) * sizeof *flags);
	uint16_t *ids = (uint16_t *)malloc((n ? n : 1) * sizeof *ids);
	int64_t bad = 0;
	int rc = -1;
	memset(out, 0, sizeof *out);
	if (!mflags || !flags || !ids) {
		snprintf(err, err_size, "out of memory");
		goto done;
	}
	kp_terrain_megatile_flags(vf4, records * KP_DERIVE_VF4_RECORD, mflags);
	if (kp_derive_import(mtxm, w, h, cv5, cv5_size, mflags, (uint32_t)records, ids, flags, &bad) != 0) {
		snprintf(err, err_size, "the terrain cannot be loaded: cell %lld selects a CV5 group or megatile the tileset lacks",
			(long long)bad);
		goto done;
	}
	kp_derive_edge_strips(w, h, flags);
	out->walk_size = n * 16;
	if (!(out->walk = (uint8_t *)malloc(out->walk_size ? out->walk_size : 1))
		|| kp_derive_walk(w, h, ids, vf4, vf4_size, out->walk) != 0) {
		snprintf(err, err_size, "the walk raster: out of memory");
		goto done;
	}
	if (!(out->regions = kp_derive_regions(w, h, flags, ids, vf4, vf4_size, out->walk, &out->regions_size, err, err_size))
		|| !(out->contours = kp_derive_contours(w, h, out->walk, &out->contours_size, err, err_size)))
		goto done;
	rc = 0;
done:
	free(mflags);
	free(flags);
	free(ids);
	if (rc != 0) kp_derived_free(out);
	return rc;
}

void kp_derived_free(KpDerived *d)
{
	free(d->walk);
	free(d->regions);
	free(d->contours);
	memset(d, 0, sizeof *d);
}
