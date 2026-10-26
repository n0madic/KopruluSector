/*
 * The native draw list of the simulation's images (plan §7 phase 2 item 3, ADR 0006). Once per tick:
 *   1. the image bounds are the world's (the caller refreshes a game's after its steps, as the original's redraw does:
 *      RefreshAllSpriteBounds 0x415700); the list only reads them;
 *   2. the sprites the local player sees are ordered by the native depth key and each sprite's images taken from the
 *      tail to the head (kp_draw_collect, kp_draw.c);
 *   3. every drawn image gets its rank in that order. A distortion image (cloak, EMP) opens a composition window
 *      (everything ranked before it is composed, then the target is copied) unless it can read the copy of the
 *      current window: no image ranked after that copy meets the pixels it reads and those stay inside the view.
 *      Destination images get a layer inside their window: one more than the highest layer of the earlier
 *      destination images of the window whose rectangle meets it;
 * (Defold's render recorded at most 1024 commands a frame, so the plans then had a budget with degraded fallbacks;
 * stage S8 of phase S removed it: every distortion that needs a copy gets one, every destination layer is drawn.)
 * The simulation's records are only read.
 */
#include "kp_present.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define CELL_FIELDS 5 /* left, top, right, bottom, layer */
#define PACKED_VALUES (KP_SPRITES * KP_DRAW_SPRITE + KP_IMAGES * KP_DRAW_IMAGE)

/* Farthest pixel a distortion reads: the brightness table tops out at 8 ((256 * 255 + 4096) >> 13, 0x445f30); method
 * 8 reads up to the raw index. */
static int32_t distort_reach(int kind) { return kind == KP_KIND_RAW ? 255 : 8; }

typedef struct {
	int64_t key;
	uint32_t stamp;
	int32_t head; /* the first entry of the cell, -1 */
} Cell;

struct KpDrawList {
	int32_t *packed;
	/* the destination overlap grid: cells of the current stamp, their entries */
	Cell *cells;
	uint32_t cell_cap, cell_used, stamp;
	int32_t *entries; /* CELL_FIELDS values + the next entry per entry */
	uint32_t entry_count, entry_cap;
	/* positions of the last two ticks */
	int64_t tick;
	int64_t live[KP_IMAGES];
	int32_t prev_x[KP_IMAGES], prev_y[KP_IMAGES], prev_id[KP_IMAGES], from_x[KP_IMAGES], from_y[KP_IMAGES];
	/* the windows, the rectangles drawn since the window's copy */
	KpWindow *windows;
	int32_t window_cap, window_count;
	int32_t *since;
	int32_t since_cap;
	int32_t distortions, distortions_near;
	int32_t trans50, trans50_start; /* -1: the rows unchanged */
};

int kp_draw_method(int32_t method, int *family, int *kind)
{
	int f, k;
	switch (method) {
	case 0: case 1: case 13: case 14: case 16: f = KP_FAMILY_OVERWRITE; k = KP_KIND_REMAP; break;
	case 12: f = KP_FAMILY_OVERWRITE; k = KP_KIND_WARP; break;
	case 15: f = KP_FAMILY_OVERWRITE; k = KP_KIND_OUTLINE; break;
	case 11: f = KP_FAMILY_OVERWRITE; k = KP_KIND_BAR; break;
	case 6: f = KP_FAMILY_DEST; k = KP_KIND_TRANS50; break;
	case 9: case 10: case 17: f = KP_FAMILY_DEST; k = KP_KIND_TABLE; break;
	case 5: case 7: f = KP_FAMILY_DEST; k = KP_KIND_CLOAK_BLEND; break;
	case 3: f = KP_FAMILY_DISTORT; k = KP_KIND_BRIGHT; break;
	case 8: f = KP_FAMILY_DISTORT; k = KP_KIND_RAW; break;
	case 2: case 4: f = KP_FAMILY_DISTORT; k = KP_KIND_CLOAK; break;
	default: return 0;
	}
	if (family) *family = f;
	if (kind) *kind = k;
	return 1;
}

uint32_t kp_draw_method_mask(void)
{
	uint32_t mask = 0;
	int32_t m;
	for (m = 0; m < 32; m++) {
		if (kp_draw_method(m, NULL, NULL)) mask |= 1u << m;
	}
	return mask;
}

/* DrawSprite 0x415a80 calls 0x416bd0: a sprite whose main image draws with method 5..7 makes trans50 rows 8..15 the
 * rows of its owner's colours, until the next such sprite. */
static int trans50_setter(int32_t method) { return method == 5 || method == 6 || method == 7; }

KpDrawList *kp_draw_list_new(void)
{
	KpDrawList *l = (KpDrawList *)calloc(1, sizeof *l);
	int k;
	if (!l) return NULL;
	l->packed = (int32_t *)malloc(sizeof *l->packed * PACKED_VALUES);
	l->cell_cap = 1024;
	l->cells = (Cell *)calloc(l->cell_cap, sizeof *l->cells);
	if (!l->packed || !l->cells) {
		kp_draw_list_free(l);
		return NULL;
	}
	for (k = 0; k < KP_IMAGES; k++) l->live[k] = INT64_MIN;
	l->trans50 = l->trans50_start = -1;
	return l;
}

void kp_draw_list_free(KpDrawList *l)
{
	if (!l) return;
	free(l->packed);
	free(l->cells);
	free(l->entries);
	free(l->windows);
	free(l->since);
	free(l);
}

void kp_draw_list_restart(KpDrawList *l)
{
	l->tick++;
	l->trans50 = -1;
}

const KpWindow *kp_draw_list_windows(const KpDrawList *l, int32_t *count)
{
	*count = l->window_count;
	return l->windows;
}

void kp_draw_list_counts(const KpDrawList *l, int32_t *distortions, int32_t *near)
{
	if (distortions) *distortions = l->distortions;
	if (near) *near = l->distortions_near;
}

/* A new stamp: an empty overlap grid (the entries of older stamps are dead). */
static void next_stamp(KpDrawList *l)
{
	l->stamp++;
	l->cell_used = 0;
	l->entry_count = 0;
}

static uint32_t cell_hash(int64_t key)
{
	uint64_t h = (uint64_t)key * 0x9e3779b97f4a7c15ull;
	return (uint32_t)(h >> 32);
}

static int64_t cell_key(int32_t r, int32_t c) { return (int64_t)r * 65536 + c; }

static int grow_cells(KpDrawList *l)
{
	uint32_t cap = l->cell_cap * 2, k;
	Cell *cells = (Cell *)calloc(cap, sizeof *cells);
	if (!cells) return -1;
	for (k = 0; k < l->cell_cap; k++) {
		const Cell *c = &l->cells[k];
		uint32_t at;
		if (c->stamp != l->stamp) continue;
		at = cell_hash(c->key) & (cap - 1);
		while (cells[at].stamp == l->stamp) at = (at + 1) & (cap - 1);
		cells[at] = *c;
	}
	free(l->cells);
	l->cells = cells;
	l->cell_cap = cap;
	return 0;
}

/* The cell of the current stamp at key; with make, a new empty one when there is none (NULL out of memory). */
static Cell *find_cell(KpDrawList *l, int64_t key, int make)
{
	uint32_t at;
	if (make && (l->cell_used + 1) * 2 > l->cell_cap && grow_cells(l) != 0) return NULL;
	at = cell_hash(key) & (l->cell_cap - 1);
	while (l->cells[at].stamp == l->stamp) {
		if (l->cells[at].key == key) return &l->cells[at];
		at = (at + 1) & (l->cell_cap - 1);
	}
	if (!make) return NULL;
	l->cells[at].key = key;
	l->cells[at].stamp = l->stamp;
	l->cells[at].head = -1;
	l->cell_used++;
	return &l->cells[at];
}

static int32_t floor_div(int32_t a, int32_t b)
{
	int32_t q = a / b;
	if ((a % b != 0) && ((a < 0) != (b < 0))) q--;
	return q;
}

/* Layer of a destination rectangle in the current stamp's grid (from 1); -2 out of memory. */
static int32_t assign_layer(KpDrawList *l, int32_t left, int32_t top, int32_t right, int32_t bottom)
{
	int32_t c0 = floor_div(left, KP_DRAW_LAYER_CELL), c1 = floor_div(right - 1, KP_DRAW_LAYER_CELL);
	int32_t r0 = floor_div(top, KP_DRAW_LAYER_CELL), r1 = floor_div(bottom - 1, KP_DRAW_LAYER_CELL);
	int32_t layer = 0, r, c;
	for (r = r0; r <= r1; r++) {
		for (c = c0; c <= c1; c++) {
			const Cell *cell = find_cell(l, cell_key(r, c), 0);
			int32_t e;
			for (e = cell ? cell->head : -1; e >= 0; e = l->entries[e * (CELL_FIELDS + 1) + CELL_FIELDS]) {
				const int32_t *v = &l->entries[e * (CELL_FIELDS + 1)];
				if (v[0] < right && left < v[2] && v[1] < bottom && top < v[3] && v[4] > layer) layer = v[4];
			}
		}
	}
	layer++;
	for (r = r0; r <= r1; r++) {
		for (c = c0; c <= c1; c++) {
			Cell *cell = find_cell(l, cell_key(r, c), 1);
			int32_t *v;
			if (!cell) return -2;
			if (l->entry_count >= l->entry_cap) {
				uint32_t cap2 = l->entry_cap ? l->entry_cap * 2 : 4096;
				int32_t *grown = (int32_t *)realloc(l->entries, sizeof *grown * (CELL_FIELDS + 1) * cap2);
				if (!grown) return -2;
				l->entries = grown;
				l->entry_cap = cap2;
			}
			v = &l->entries[l->entry_count * (CELL_FIELDS + 1)];
			v[0] = left;
			v[1] = top;
			v[2] = right;
			v[3] = bottom;
			v[4] = layer;
			v[5] = cell->head;
			cell->head = (int32_t)l->entry_count++;
		}
	}
	return layer;
}

static int reserve_windows(KpDrawList *l, int32_t n)
{
	if (n > l->window_cap) {
		int32_t cap = l->window_cap ? l->window_cap * 2 : 64;
		KpWindow *w;
		while (cap < n) cap *= 2;
		w = (KpWindow *)realloc(l->windows, sizeof *w * (size_t)cap);
		if (!w) return -1;
		l->windows = w;
		l->window_cap = cap;
	}
	return 0;
}

static int reserve_since(KpDrawList *l, int32_t n)
{
	if (n > l->since_cap) {
		int32_t cap = l->since_cap ? l->since_cap * 2 : 4096;
		int32_t *s;
		while (cap < n) cap *= 2;
		s = (int32_t *)realloc(l->since, sizeof *s * (size_t)cap);
		if (!s) return -1;
		l->since = s;
		l->since_cap = cap;
	}
	return 0;
}

int32_t kp_draw_list_build(KpDrawList *l, KpImages *w, KpFrameOf frame_of, KpDrawEmit emit, void *ud,
	const KpRect *view, int replan, int32_t *layers_out)
{
	int32_t first_row = 0, count, at = 0, rank = 0, layers = 0, window = 1, drawn_since = 0;
	int32_t distortions = 0, near = 0, trans50;
	int32_t ll = 0, lt = 0, lr = 0, lb = 0;
	int64_t tick;
	KpDrawItem d;
	if (view) {
		first_row = floor_div(view->top, KP_WORLD_TILE) - KP_DRAW_ROWS_ABOVE;
		if (first_row < 0) first_row = 0;
		ll = view->left - KP_DRAW_LAYER_MARGIN;
		lt = view->top - KP_DRAW_LAYER_MARGIN;
		lr = view->right + KP_DRAW_LAYER_MARGIN;
		lb = view->bottom + KP_DRAW_LAYER_MARGIN;
	}
	count = kp_draw_collect(w, first_row, kp_draw_method_mask(), l->packed, PACKED_VALUES);
	if (count < 0) return -1;
	if (!replan) l->tick++;
	next_stamp(l);
	tick = l->tick;
	if (!replan) l->trans50_start = l->trans50;
	trans50 = l->trans50_start;
	memset(&d, 0, sizeof d);
	while (at < count) {
		int32_t owner = l->packed[at + 1], main_method = l->packed[at + 2], images = l->packed[at + 3], n;
		at += KP_DRAW_SPRITE;
		if (main_method >= 0 && trans50_setter(main_method)) trans50 = owner;
		for (n = 0; n < images; n++, at += KP_DRAW_IMAGE) {
			const int32_t *v = &l->packed[at];
			int32_t i = v[0], method = v[1], param = v[2], flags = v[3], g = v[4], grp_frame = v[5];
			int32_t left = v[6], top = v[7], id = v[8], fw = v[9], fh = v[10], frame, layer = 0;
			int32_t x, y, px, py;
			int family, kind, base_kind, flip;
			if (!kp_draw_method(method, &family, &kind)) continue;
			base_kind = kind;
			if (method == KP_DRAW_REMAP && param == KP_DRAW_TRANS50) kind = KP_KIND_TRANS50;
			frame = frame_of(ud, g, grp_frame);
			if (frame < 0 || fw <= 0 || fh <= 0) continue;
			rank++;
			if (rank >= KP_RANK_RANGE) kp_images_fail(w, "draw_list: more ranks than the depth range");
			/* the frame centre in 1/256 world pixels: (left + fw / 2, TOP - (top + fh / 2)) */
			x = left * 256 + fw * 128;
			y = (KP_WORLD_TOP - top) * 256 - fh * 128;
			px = x;
			py = y;
			/* interpolate only from the same image's position on the previous tick */
			if (replan) {
				if (l->live[i] == tick && l->prev_id[i] == id) {
					px = l->from_x[i];
					py = l->from_y[i];
				}
			} else {
				if (l->live[i] == tick - 1 && l->prev_id[i] == id) {
					px = l->prev_x[i];
					py = l->prev_y[i];
				}
				l->from_x[i] = px;
				l->from_y[i] = py;
			}
			l->prev_x[i] = x;
			l->prev_y[i] = y;
			l->prev_id[i] = id;
			l->live[i] = tick;
			if (family == KP_FAMILY_DEST && (!view || (left < lr && ll < left + fw && top < lb && lt < top + fh))) {
				layer = assign_layer(l, left, top, left + fw, top + fh);
				if (layer == -2) return -1;
				if (layer > layers) layers = layer;
			}
			flip = (flags & KP_F_MIRROR) != 0;
			d.key = i;
			d.family = family;
			d.kind = kind;
			d.method = method;
			d.owner = owner;
			d.param = param;
			d.trans50 = trans50;
			d.left = left;
			d.top = top;
			d.w = fw;
			d.h = fh;
			d.x256 = x;
			d.y256 = y;
			d.px256 = px;
			d.py256 = py;
			d.frame = frame;
			d.grp = g;
			d.grp_frame = grp_frame;
			d.flip = flip;
			d.virtual_ = v[11];
			if (family == KP_FAMILY_DISTORT) {
				/* the pixels read: the frame rows, reach pixels ahead (behind when mirrored) */
				int32_t reach = distort_reach(base_kind);
				int32_t rl = flip ? left - reach : left, rr = flip ? left + fw : left + fw + reach, o;
				/* outside the view a distortion changes nothing visible: it reads any copy */
				int visible = view == NULL || (left < view->right && view->left < left + fw && top < view->bottom
					&& view->top < top + fh);
				int join;
				distortions++;
				if (view == NULL || (rl < lr && ll < rr && top < lb && lt < top + fh)) near++;
				join = window > 1 && (!visible || (view != NULL && rl >= view->left && rr <= view->right));
				if (join && visible) {
					for (o = 0; o < drawn_since; o += 4) {
						const int32_t *s = &l->since[o];
						if (s[0] < rr && rl < s[2] && s[1] < top + fh && top < s[3]) {
							join = 0;
							break;
						}
					}
				}
				if (join) {
					l->windows[window - 2].last = rank;
				} else {
					KpWindow *opened;
					/* a new copy: the next window starts with a fresh overlap grid */
					if (reserve_windows(l, window) != 0) return -1;
					opened = &l->windows[window - 1];
					opened->rank = rank;
					opened->last = rank;
					opened->layers = layers;
					window++;
					layers = 0;
					drawn_since = 0;
					next_stamp(l);
				}
			}
			if (reserve_since(l, drawn_since + 4) != 0) return -1;
			l->since[drawn_since] = left;
			l->since[drawn_since + 1] = top;
			l->since[drawn_since + 2] = left + fw;
			l->since[drawn_since + 3] = top + fh;
			drawn_since += 4;
			/* a distortion's window is the copy it reads */
			d.rank = rank;
			d.layer = layer;
			d.window = family == KP_FAMILY_DISTORT ? window - 1 : window;
			emit(ud, &d);
		}
	}
	l->window_count = window - 1;
	l->distortions = distortions;
	l->trans50 = trans50;
	l->distortions_near = near;
	if (layers_out) *layers_out = layers;
	return rank;
}
