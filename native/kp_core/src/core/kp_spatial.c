/* The unit spatial index (kp_game.h) and the rectangle queries of the detection code over it
 * (kp_units_in_rect, kp_unit_box_within). */
#include "kp_game_internal.h"

#define SUBUNIT 0x10

/* Lower bound of 0x419500 and of the inline searches of 0x4195a0: the first entry whose
 * coordinate is >= v (inclusive) or > v (exclusive), over entries 0..count-1. */
static int64_t search(const int64_t *at, int64_t count, int64_t v, int inclusive)
{
	int64_t key = inclusive ? v * 2 - 1 : v * 2 + 1, low = 0, high = count, sum = count;
	while (low < high) {
		int64_t mid = sum / 2;
		if (key < at[mid] * 2) {
			sum = mid + low;
			high = mid;
		} else {
			low = mid + 1;
			sum = mid + 1 + high;
		}
	}
	return high;
}

static int64_t ext(KpGame *g, int64_t t, int k) { return KP_DAT(g, units, unit_extents, t * 4 + k); }

void kp_spatial_clear(KpGame *g)
{
	KpSpatial *sp = &g->spatial;
	int64_t k, t, w = 0, h = 0;
	sp->count = 0;
	for (k = 0; k < KP_SPATIAL_CAPACITY; k++) sp->x_unit[k] = sp->x_at[k] = sp->y_unit[k] = sp->y_at[k] = -1;
	for (t = 0; t <= 227; t++) {
		int64_t width = kp_s16v(ext(g, t, 0)) + kp_s16v(ext(g, t, 2)) + 1;
		int64_t height = kp_s16v(ext(g, t, 3)) + kp_s16v(ext(g, t, 1)) + 1;
		if (w < width) w = kp_mod(width, 65536);
		if (h < height) h = kp_mod(height, 65536);
	}
	sp->max_w = w;
	sp->max_h = h;
	for (k = 0; k <= KP_UNITS; k++) sp->mark[k] = sp->seen[k] = 0;
	sp->frame_top = 0;
	sp->stamp = 0;
}

/* OpenXEntry 0x419e20 / OpenYEntry 0x419eb0. */
static void open_entry(KpGame *g, int64_t *units_of, int64_t *at_of, int64_t *lo, int64_t *hi, int64_t at, int64_t count)
{
	int64_t k;
	if (count >= KP_SPATIAL_CAPACITY) kp_game_fail(g, "spatial: the index is full");
	for (k = count - 1; k >= at; k--) {
		int64_t n = units_of[k];
		if (lo[n] == k) lo[n] = k + 1;
		else if (hi[n] == k) hi[n] = k + 1;
		units_of[k + 1] = n;
		at_of[k + 1] = at_of[k];
	}
}

void kp_spatial_insert(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	KpSpatial *sp = &g->spatial;
	int64_t t = u->type[n], x, y, left, top, right, bottom, at;
	int32_t s;
	if (kp_band(KP_DAT(g, units, special_ability_flags, t), SUBUNIT) != 0) return;
	s = kp_unit_sprite(g, n);
	x = kp_s16v(g->w->spr.x[s]);
	y = kp_s16v(g->w->spr.y[s]);
	left = x - kp_s16v(ext(g, t, 0));
	top = y - kp_s16v(ext(g, t, 1));
	at = search(sp->x_at, sp->count, left, 1);
	open_entry(g, sp->x_unit, sp->x_at, u->bound_left, u->bound_right, at, sp->count);
	u->bound_left[n] = at;
	sp->x_unit[at] = n;
	sp->x_at[at] = left;
	at = search(sp->y_at, sp->count, top, 1);
	open_entry(g, sp->y_unit, sp->y_at, u->bound_top, u->bound_bottom, at, sp->count);
	u->bound_top[n] = at;
	sp->y_unit[at] = n;
	sp->y_at[at] = top;
	sp->count++;
	right = kp_s16v(ext(g, t, 2)) + x;
	bottom = kp_s16v(ext(g, t, 3)) + y;
	at = search(sp->x_at, sp->count, right, 1);
	open_entry(g, sp->x_unit, sp->x_at, u->bound_left, u->bound_right, at, sp->count);
	u->bound_right[n] = at;
	sp->x_unit[at] = n;
	sp->x_at[at] = right;
	at = search(sp->y_at, sp->count, bottom, 1);
	open_entry(g, sp->y_unit, sp->y_at, u->bound_top, u->bound_bottom, at, sp->count);
	u->bound_bottom[n] = at;
	sp->y_unit[at] = n;
	sp->y_at[at] = bottom;
	sp->count++;
}

/* RemoveXEntry 0x419d00 / RemoveYEntry 0x419d90. */
static void close_entry(int64_t *units_of, int64_t *at_of, int64_t *lo, int64_t *hi, int64_t at, int64_t last)
{
	int64_t k;
	for (k = at + 1; k <= last; k++) {
		int64_t n = units_of[k];
		if (lo[n] == k) lo[n] = k - 1;
		else if (hi[n] == k) hi[n] = k - 1;
	}
	for (k = at; k <= last - 1; k++) {
		units_of[k] = units_of[k + 1];
		at_of[k] = at_of[k + 1];
	}
}

void kp_spatial_remove(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	KpSpatial *sp = &g->spatial;
	if (u->bound_left[n] < 0) return;
	sp->count--;
	close_entry(sp->x_unit, sp->x_at, u->bound_left, u->bound_right, u->bound_right[n], sp->count);
	close_entry(sp->y_unit, sp->y_at, u->bound_top, u->bound_bottom, u->bound_bottom[n], sp->count);
	u->bound_right[n] = u->bound_bottom[n] = -1;
	sp->count--;
	close_entry(sp->x_unit, sp->x_at, u->bound_left, u->bound_right, u->bound_left[n], sp->count);
	close_entry(sp->y_unit, sp->y_at, u->bound_top, u->bound_bottom, u->bound_top[n], sp->count);
	u->bound_left[n] = u->bound_top[n] = -1;
}

static void swap(int64_t *units_of, int64_t *at_of, int64_t *lo, int64_t *hi, int64_t k, int64_t step)
{
	int64_t n = units_of[k], t;
	if (lo[n] == k) lo[n] = k + step;
	else if (hi[n] == k) hi[n] = k + step;
	t = units_of[k];
	units_of[k] = units_of[k + step];
	units_of[k + step] = t;
	t = at_of[k];
	at_of[k] = at_of[k + step];
	at_of[k + step] = t;
}

static void bubble_up(KpSpatial *sp, int64_t *units_of, int64_t *at_of, int64_t *lo, int64_t *hi, int64_t *field, int64_t n,
	int64_t v)
{
	while (field[n] + 1 < sp->count) {
		int64_t k = field[n];
		if (v <= at_of[k + 1]) break;
		swap(units_of, at_of, lo, hi, k + 1, -1);
		field[n] = k + 1;
	}
}

static void bubble_down(int64_t *units_of, int64_t *at_of, int64_t *lo, int64_t *hi, int64_t *field, int64_t n, int64_t v)
{
	while (field[n] > 0 && v < at_of[field[n] - 1]) {
		int64_t k = field[n];
		swap(units_of, at_of, lo, hi, k - 1, 1);
		field[n] = k - 1;
	}
}

void kp_spatial_move(KpGame *g, int64_t n, int64_t dx, int64_t dy)
{
	KpUnits *u = &g->u;
	KpSpatial *sp = &g->spatial;
	int64_t *xa = sp->x_at, *ya = sp->y_at, left, right, top, bottom;
	if (u->bound_left[n] < 0 || (dx == 0 && dy == 0)) return;
	left = xa[u->bound_left[n]] + dx;
	xa[u->bound_left[n]] = left;
	right = xa[u->bound_right[n]] + dx;
	xa[u->bound_right[n]] = right;
	top = ya[u->bound_top[n]] + dy;
	ya[u->bound_top[n]] = top;
	bottom = ya[u->bound_bottom[n]] + dy;
	ya[u->bound_bottom[n]] = bottom;
	if (dx > 0) {
		bubble_up(sp, sp->x_unit, xa, u->bound_left, u->bound_right, u->bound_right, n, right);
		bubble_up(sp, sp->x_unit, xa, u->bound_left, u->bound_right, u->bound_left, n, left);
	} else if (dx < 0) {
		bubble_down(sp->x_unit, xa, u->bound_left, u->bound_right, u->bound_left, n, left);
		bubble_down(sp->x_unit, xa, u->bound_left, u->bound_right, u->bound_right, n, right);
	}
	if (dy > 0) {
		bubble_up(sp, sp->y_unit, ya, u->bound_top, u->bound_bottom, u->bound_bottom, n, bottom);
		bubble_up(sp, sp->y_unit, ya, u->bound_top, u->bound_bottom, u->bound_top, n, top);
	} else if (dy < 0) {
		bubble_down(sp->y_unit, ya, u->bound_top, u->bound_bottom, u->bound_top, n, top);
		bubble_down(sp->y_unit, ya, u->bound_top, u->bound_bottom, u->bound_bottom, n, bottom);
	}
}

/* QueryUnitsOverlappingRect 0x4224c0 (with its release 0x4222b0). */
int kp_spatial_query(KpGame *g, int64_t left, int64_t top, int64_t right, int64_t bottom, int64_t *out, int max)
{
	KpSpatial *sp = &g->spatial;
	KpUnits *u = &g->u;
	int narrow_x = 0, narrow_y = 0, count = 0;
	int64_t right_search = kp_mod(right, 65536), bottom_search = kp_mod(bottom, 65536), x0, x1, y0, y1, k;
	if (right - left + 1 < sp->max_w) {
		narrow_x = 1;
		right_search = kp_mod(sp->max_w - 1 + left, 65536);
	}
	if (bottom - top + 1 < sp->max_h) {
		narrow_y = 1;
		bottom_search = kp_mod(sp->max_h - 1 + top, 65536);
	}
	x0 = search(sp->x_at, sp->count, left, 1);
	x1 = search(sp->x_at, sp->count, right_search, 1);
	y0 = search(sp->y_at, sp->count, top, 1);
	y1 = search(sp->y_at, sp->count, bottom_search, 1);
	if (x1 - x0 <= 0 || y1 - y0 <= 0) return 0;
	for (k = x0; k <= x1 - 1; k++) {
		int64_t n = sp->x_unit[k];
		if (narrow_x && kp_s16v(g->w->spr.x[kp_unit_sprite(g, n)]) - kp_s16v(ext(g, u->type[n], 0)) > right) sp->mark[n] = 0;
		else sp->mark[n] = 1;
	}
	for (k = y0; k <= y1 - 1; k++) {
		int64_t n = sp->y_unit[k];
		if (!(narrow_y && kp_s16v(g->w->spr.y[kp_unit_sprite(g, n)]) - kp_s16v(ext(g, u->type[n], 1)) > bottom)) {
			if (sp->mark[n] == 1) sp->mark[n] = 3;
		}
	}
	for (k = x0; k <= x1 - 1; k++) {
		int64_t n = sp->x_unit[k];
		if (sp->mark[n] == 3) {
			if (count >= max) kp_game_fail(g, "spatial: more than %d units in a query", max);
			out[count++] = n;
		}
		sp->mark[n] = 0;
	}
	return count;
}

int kp_spatial_overlap(KpGame *g, int64_t a, int64_t b)
{
	KpUnits *u = &g->u;
	int64_t *xa = g->spatial.x_at, *ya = g->spatial.y_at;
	return xa[u->bound_left[b]] <= xa[u->bound_right[a]] && xa[u->bound_left[a]] <= xa[u->bound_right[b]]
		&& ya[u->bound_top[b]] <= ya[u->bound_bottom[a]] && ya[u->bound_top[a]] <= ya[u->bound_bottom[b]];
}

void kp_unit_rect(KpGame *g, int64_t n, int64_t *left, int64_t *top, int64_t *right, int64_t *bottom)
{
	int64_t t = g->u.type[n];
	int32_t s = kp_unit_sprite(g, n);
	int64_t x = kp_s16v(g->w->spr.x[s]), y = kp_s16v(g->w->spr.y[s]);
	*left = kp_s16v(x - ext(g, t, 0));
	*top = kp_s16v(y - ext(g, t, 1));
	*right = kp_s16v(x + ext(g, t, 2));
	*bottom = kp_s16v(y + ext(g, t, 3));
}

void kp_update_overlap_flags(KpGame *g, int64_t n)
{
	int64_t *status = g->u.status, found[KP_UNITS], l, t, r, b;
	int count, k;
	kp_unit_rect(g, n, &l, &t, &r, &b);
	count = kp_spatial_query(g, l, t, r, b, found, KP_UNITS);
	for (k = 0; k < count; k++) {
		int64_t c = found[k], cs = status[c];
		if (kp_band(cs, 2) != 0) {
			status[n] = kp_bor(status[n], 0x40000);
		} else if (kp_band(cs, 4) == 0 && (kp_band(cs, 0x800000) == 0 || kp_band(status[n], 2) != 0)) {
			if (kp_spatial_overlap(g, c, n)) status[c] = kp_bor(cs, 0x40000);
		}
	}
}

int kp_units_in_rect(KpGame *g, int64_t left, int64_t top, int64_t right, int64_t bottom, int64_t *out, int max)
{
	KpSpatial *sp = &g->spatial;
	KpUnits *u = &g->u;
	int64_t stamp = sp->stamp + 1, count = sp->count, y0, y1, x;
	int k = 0;
	if (stamp >= 4294967296LL) stamp = 1;
	sp->stamp = stamp;
	y0 = search(sp->y_at, count, top, 1);
	/* the first entry at or after `bottom`: the native walk up from y0, as a lower bound */
	y1 = search(sp->y_at, count, bottom, 1);
	if (y1 <= y0) return 0;
	x = search(sp->x_at, count, left, 1);
	while (x < count && sp->x_at[x] < right) {
		int64_t n = sp->x_unit[x];
		if (sp->seen[n] != stamp) {
			sp->seen[n] = stamp;
			if (y0 <= u->bound_bottom[n] && u->bound_top[n] < y1) {
				if (k >= max) kp_game_fail(g, "spatial: more than %d units in a rectangle", max);
				out[k++] = n;
			}
		}
		x++;
	}
	return k;
}

/* The bounds of unit n (its parent's for a subunit when parent is set), right and bottom exclusive. */
static void distance_box(KpGame *g, int64_t n, int64_t *l, int64_t *tp, int64_t *r, int64_t *b)
{
	int64_t t = g->u.type[kp_game_unit(g, n)];
	int32_t s = kp_unit_sprite(g, n);
	int64_t x = kp_s16v(g->w->spr.x[s]), y = kp_s16v(g->w->spr.y[s]);
	*l = kp_s16v(x - ext(g, t, 0));
	*tp = kp_s16v(y - ext(g, t, 1));
	*r = kp_s16v(x + ext(g, t, 2) + 1);
	*b = kp_s16v(y + ext(g, t, 3) + 1);
}

/* The unit whose box measures the distance: a subunit's parent. */
static int64_t distance_source(KpGame *g, int64_t n)
{
	if (kp_band(KP_DAT(g, units, special_ability_flags, g->u.type[n]), 0x10) != 0) return g->u.subunit[n];
	return n;
}

int64_t kp_unit_distance(KpGame *g, int64_t n, int64_t target)
{
	int64_t l1, t1, r1, b1, l2, t2, r2, b2, dx, dy;
	distance_box(g, distance_source(g, n), &l1, &t1, &r1, &b1);
	distance_box(g, target, &l2, &t2, &r2, &b2);
	dx = l1 - r2;
	if (dx < 0) {
		dx = l2 - r1;
		if (dx < 0) dx = 0;
	}
	dy = t1 - b2;
	if (dy < 0) {
		dy = t2 - b1;
		if (dy < 0) dy = 0;
	}
	return kp_approx_distance(dx, dy);
}

int64_t kp_unit_distance_to_point(KpGame *g, int64_t n, int64_t x, int64_t y)
{
	int64_t l, t, r, b, dx, dy;
	distance_box(g, distance_source(g, n), &l, &t, &r, &b);
	x = kp_s16v(x);
	y = kp_s16v(y);
	dx = l - x;
	if (dx < 0) {
		dx = x - r;
		if (dx < 0) dx = 0;
	}
	dy = t - y;
	if (dy < 0) {
		dy = y - b;
		if (dy < 0) dy = 0;
	}
	return kp_approx_distance(dx, dy);
}

int64_t *kp_query_rect(KpGame *g, int64_t left, int64_t top, int64_t right, int64_t bottom, int *count)
{
	KpSpatial *sp = &g->spatial;
	int64_t *frame = &sp->frames[sp->frame_top];
	*count = kp_units_in_rect(g, left, top, right, bottom, frame, (int)(KP_QUERY_ARENA - sp->frame_top));
	sp->frame_top += *count;
	return frame;
}

void kp_query_release(KpGame *g, int64_t *frame)
{
	KpSpatial *sp = &g->spatial;
	if (frame < sp->frames || frame > sp->frames + sp->frame_top) kp_game_fail(g, "spatial: a query frame out of order");
	sp->frame_top = frame - sp->frames;
}

int kp_unit_box_within(KpGame *g, int64_t n, int64_t range, int64_t target)
{
	return kp_u32v(range) >= kp_unit_distance(g, n, target);
}

/* One step of a sweep in NearestUnitSweep 0x442d90: candidate c at sprite (x, y) is taken when it lies within the
 * other axis' span [lo, hi), is not the excluded unit, passes accept and is strictly nearer than the best so far. */
typedef struct {
	KpGame *g;
	int64_t excluded, cx, cy, best, distance, found;
	KpUnitFilter accept;
	int64_t arg;
} Sweep;

static void sweep_candidate(Sweep *s, int64_t c, int64_t other, int64_t lo, int64_t hi, int64_t x, int64_t y)
{
	int64_t d;
	if (other < lo || other >= hi || c == s->excluded || !s->accept(s->g, c, s->arg)) return;
	d = kp_approx_distance(x - s->cx, y - s->cy);
	if (d < s->distance) {
		s->distance = d;
		s->best = c;
		s->found = 1;
	}
}

static int64_t sprite_x(KpGame *g, int64_t n) { return kp_s16v(g->w->spr.x[kp_unit_sprite(g, n)]); }
static int64_t sprite_y(KpGame *g, int64_t n) { return kp_s16v(g->w->spr.y[kp_unit_sprite(g, n)]); }

/* NearestUnitSweep 0x442d90: the four scans outward from the start entries, the rectangle (signed 16-bit) shrinking
 * around the center to the best distance found. */
static int64_t nearest_sweep(KpGame *g, int64_t excluded, int64_t cx, int64_t cy, int64_t x_back, int64_t y_back,
	int64_t x_fwd, int64_t y_fwd, const int64_t rect[4], KpUnitFilter accept, int64_t arg)
{
	KpSpatial *sp = &g->spatial;
	int64_t l = rect[0], t = rect[1], r = rect[2], b = rect[3], dx, dy;
	int active;
	Sweep s;
	s.g = g;
	s.excluded = excluded;
	s.cx = cx;
	s.cy = cy;
	s.accept = accept;
	s.arg = arg;
	s.best = 0;
	dx = cx - kp_s16v(l) > kp_s16v(r) - cx ? cx - kp_s16v(l) : kp_s16v(r) - cx;
	dy = cy - kp_s16v(t) > kp_s16v(b) - cy ? cy - kp_s16v(t) : kp_s16v(b) - cy;
	s.distance = kp_approx_distance(dx, dy);
	do {
		int64_t c, x, y;
		active = 0;
		s.found = 0;
		if (x_back >= 0) {
			c = sp->x_unit[x_back];
			x = sprite_x(g, c);
			y = sprite_y(g, c);
			if (x < kp_s16v(l)) {
				x_back = -1;
			} else {
				sweep_candidate(&s, c, y, kp_s16v(t), kp_s16v(b), x, y);
			}
			x_back--;
			active = 1;
		}
		if (x_fwd < sp->count) {
			c = sp->x_unit[x_fwd];
			x = sprite_x(g, c);
			y = sprite_y(g, c);
			if (x < kp_s16v(r)) {
				sweep_candidate(&s, c, y, kp_s16v(t), kp_s16v(b), x, y);
			} else {
				x_fwd = sp->count;
			}
			x_fwd++;
			active = 1;
		}
		if (y_back >= 0) {
			c = sp->y_unit[y_back];
			x = sprite_x(g, c);
			y = sprite_y(g, c);
			if (y < kp_s16v(t)) {
				y_back = -1;
			} else {
				sweep_candidate(&s, c, x, kp_s16v(l), kp_s16v(r), x, y);
			}
			y_back--;
			active = 1;
		}
		if (y_fwd < sp->count) {
			c = sp->y_unit[y_fwd];
			x = sprite_x(g, c);
			y = sprite_y(g, c);
			if (y < kp_s16v(b)) {
				sweep_candidate(&s, c, x, kp_s16v(l), kp_s16v(r), x, y);
			} else {
				y_fwd = sp->count;
			}
			y_fwd++;
			active = 1;
		}
		if (s.found) {
			if (kp_s16v(l) < cx - s.distance) l = cx - s.distance;
			if (kp_s16v(r) > cx + s.distance) r = cx + s.distance;
			if (kp_s16v(t) < cy - s.distance) t = cy - s.distance;
			if (kp_s16v(b) > cy + s.distance) b = cy + s.distance;
		}
	} while (active);
	return s.best;
}

/* FindNearestUnitFromUnit 0x442c20: the sweep starts at unit n's own index entries (at its sprite position when
 * the sprite is hidden) and never returns n. */
int64_t kp_nearest_unit_from(KpGame *g, int64_t n, const int64_t rect[4], KpUnitFilter accept, int64_t arg)
{
	KpUnits *u = &g->u;
	KpSpatial *sp = &g->spatial;
	int32_t s = kp_unit_sprite(g, n);
	int64_t x = kp_s16v(g->w->spr.x[s]), y = kp_s16v(g->w->spr.y[s]), xb, yb, xf, yf;
	if (kp_band(g->w->spr.flags[s], 0x20) != 0) {
		xf = search(sp->x_at, sp->count, x, 1);
		yf = search(sp->y_at, sp->count, y, 1);
		xb = xf - 1;
		yb = yf - 1;
	} else {
		xb = u->bound_right[n] - 1;
		yb = u->bound_bottom[n] - 1;
		xf = u->bound_left[n] + 1;
		yf = u->bound_top[n] + 1;
	}
	return nearest_sweep(g, n, x, y, xb, yb, xf, yf, rect, accept, arg);
}

/* FindNearestUnitAroundUnit 0x442bc0: within radius of unit n's sprite. */
int64_t kp_nearest_unit_around(KpGame *g, int64_t n, int64_t radius, KpUnitFilter accept, int64_t arg)
{
	int32_t s = kp_unit_sprite(g, n);
	int64_t x = g->w->spr.x[s], y = g->w->spr.y[s], rect[4];
	rect[0] = x - radius;
	rect[1] = y - radius;
	rect[2] = x + radius;
	rect[3] = y + radius;
	return kp_nearest_unit_from(g, n, rect, accept, arg);
}
