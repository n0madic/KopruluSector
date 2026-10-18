/* Unit placement and the ground footprint (kp_pathing.h). */
#include "kp_pathing_internal.h"

#define SEARCH_RADIUS 0x80
#define BOTTOM_RESERVE 0x20
#define ERROR_PLACEMENT 0x3c
#define WALK_ANY 0x400000
#define WALK_PARTIAL 0x20000000
#define WALK_ALL 0x10000
#define STATUS_IGNORED 0x200000
#define POWERUP 0x800

static int64_t ext(KpGame *g, int64_t i) { return KP_DAT(g, units, unit_extents, i); }

/* Producers whose own flyers collide with them (0x47ea20). */
static int producer_child(int64_t producer, int64_t t)
{
	switch (producer) {
	case KP_TYPE_STARPORT:
		return t == KP_TYPE_WRAITH || t == KP_TYPE_DROPSHIP || t == KP_TYPE_SCIENCE_VESSEL
			|| t == KP_TYPE_BATTLECRUISER || t == KP_TYPE_VALKYRIE;
	case KP_TYPE_ROBOTICS_FACILITY:
		return t == KP_TYPE_SHUTTLE || t == KP_TYPE_OBSERVER;
	case KP_TYPE_STARGATE:
		return t == KP_TYPE_SCOUT || t == KP_TYPE_CARRIER || t == KP_TYPE_ARBITER || t == KP_TYPE_CORSAIR;
	}
	return 0;
}

/* e[i] of an extents table (0..3 and 8..11 are set; another index fails). */
static int64_t ev(KpGame *g, const int64_t *e, int64_t i)
{
	if (!((i >= 0 && i <= 3) || (i >= 8 && i <= 11))) kp_game_fail(g, "placement: extent %lld", (long long)i);
	return e[i];
}

/* The first record whose axis (doubled, 16-bit) is above `key` (0x48b780). */
static int64_t first_above(const KpContourSide *a, int64_t key)
{
	int64_t count = a->count, low = 0, high = count;
	if (count == 0) return 0;
	do {
		int64_t mid = (high + low) / 2;
		if (kp_s16v(a->axis[mid] * 2) <= kp_s16v(key)) low = mid + 1;
		else high = mid;
	} while (!(low >= high));
	return high;
}

/* SetBoxExtents 0x489160. */
void kp_set_box_extents(int64_t *e, int64_t left, int64_t up, int64_t right, int64_t down)
{
	e[0] = kp_s16v(up);
	e[1] = kp_s16v(-right);
	e[2] = kp_s16v(-down);
	e[3] = kp_s16v(left);
	e[8] = kp_s16v(up + 1);
	e[9] = kp_s16v(-1 - right);
	e[10] = kp_s16v(-1 - down);
	e[11] = kp_s16v(left + 1);
}

/* SetUnitTypeExtents 0x4891c0. */
void kp_set_extents(KpGame *g, int64_t code, int64_t *e)
{
	if (code < 0x8000) {
		kp_set_box_extents(e, kp_s16v(ext(g, code * 4)), kp_s16v(ext(g, code * 4 + 1)), kp_s16v(ext(g, code * 4 + 2)),
			kp_s16v(ext(g, code * 4 + 3)));
	} else {
		int64_t r = kp_mod(code, 0x8000);
		e[0] = r;
		e[3] = r;
		e[1] = kp_s16v(-r);
		e[2] = kp_s16v(-r);
		e[8] = kp_s16v(r + 1);
		e[11] = kp_s16v(r + 1);
		e[9] = kp_s16v(-1 - r);
		e[10] = kp_s16v(-1 - r);
	}
}

/* Region group of a region, from the RGNS record. */
int64_t kp_region_group(KpGame *g, int64_t region)
{
	return kp_rgns_read(g, g->regions.regions_at + region * KP_REGION_SIZE + 2, 2);
}

void kp_contour_extents(KpGame *g, int64_t code) { kp_set_extents(g, code, g->pathing.extent_ctx); }

/* ContourFootprintFits 0x48b780. */
int kp_contour_fits(KpGame *g, int64_t x, int64_t y)
{
	const int64_t *e = g->pathing.extent_ctx;
	const KpContourSide *top = &g->contours[0], *right = &g->contours[1], *bottom = &g->contours[2],
		*left = &g->contours[3];
	int64_t k = first_above(top, y * 2 - 1);
	while (k > 0) {
		int64_t p = k - 1;
		if (kp_s16v(ev(g, e, top->dir[p]) + top->axis[p]) < y) break;
		if (!(x < kp_s16v(top->start[p] + e[1]) || kp_s16v(top->stop[p] + e[3]) < x)) return 0;
		k = p;
	}
	k = first_above(right, x * 2 + 1);
	while (k < right->count) {
		if (x < kp_s16v(ev(g, e, right->dir[k]) + right->axis[k])) break;
		if (kp_s16v(right->start[k] + e[2]) <= y && y <= kp_s16v(right->stop[k] + e[0])) return 0;
		k = k + 1;
	}
	k = first_above(bottom, y * 2 + 1);
	while (k < bottom->count) {
		if (y < kp_s16v(ev(g, e, bottom->dir[k]) + bottom->axis[k])) break;
		if (kp_s16v(bottom->start[k] + e[1]) <= x && x <= kp_s16v(bottom->stop[k] + e[3])) return 0;
		k = k + 1;
	}
	k = first_above(left, x * 2 - 1);
	while (k > 0) {
		int64_t p = k - 1;
		if (kp_s16v(ev(g, e, left->dir[p]) + left->axis[p]) < x) return 1;
		if (!(y < kp_s16v(left->start[p] + e[2]) || kp_s16v(left->stop[p] + e[0]) < y)) return 0;
		k = p;
	}
	return 1;
}

/* IsPointWalkable 0x42ea10. */
int kp_point_walkable(KpGame *g, int64_t x, int64_t y)
{
	int64_t i = kp_sarv(x, 5) + kp_sarv(y, 5) * g->map_w, f, mega;
	if (i < 0 || i >= KP_TILES) kp_game_fail(g, "placement: no tile %lld", (long long)i);
	f = g->terrain.tiles[i];
	if (kp_band(f, WALK_ANY) != 0) return 1;
	if (kp_band(f, WALK_PARTIAL) == 0) return kp_band(f, WALK_ALL) != 0;
	mega = kp_band(g->terrain.megatiles[i], 0x7fff);
	return kp_band(kp_minitile_flags(g, mega, kp_band(kp_sarv(x, 3), 3), kp_band(kp_sarv(y, 3), 3)), 1) != 0;
}

/* UnitTypeFitsAt 0x42ed50. */
int kp_unit_type_fits(KpGame *g, int64_t t, int64_t x, int64_t y)
{
	x = kp_s16v(x);
	y = kp_s16v(y);
	if (!(kp_s16v(ext(g, t * 4)) <= x && kp_s16v(ext(g, t * 4 + 2)) + x < g->map_w_px)) return 0;
	if (!(kp_s16v(ext(g, t * 4 + 1)) <= y && kp_s16v(ext(g, t * 4 + 3)) + y < g->map_h_px)) return 0;
	if (!kp_point_walkable(g, x, y)) return 0;
	kp_contour_extents(g, t);
	return kp_contour_fits(g, x, y);
}

/* IsBoxPlacementLegal 0x42ec70. */
int kp_box_fits(KpGame *g, int64_t x, int64_t y, int64_t w, int64_t h)
{
	int64_t left = kp_s16v(x - kp_idiv64(g, w, 2)), top = kp_s16v(y - kp_idiv64(g, h, 2));
	if (left < 0 || left + w >= g->map_w_px || top < 0 || top + h >= g->map_h_px) return 0;
	if (!kp_point_walkable(g, x, y)) return 0;
	kp_set_box_extents(g->pathing.extent_ctx, kp_idiv64(g, w - 1, 2), kp_idiv64(g, h - 1, 2), w - kp_idiv64(g, w, 2) - 1,
		h - kp_idiv64(g, h, 2) - 1);
	return kp_contour_fits(g, x, y);
}

/* PlacementCollisionCallback 0x47ea20 (IsUnitDead 0x41fe20 is kp_unit_is_dead, the same code). */
int kp_placement_blocks(KpGame *g, int64_t c, int64_t wanted)
{
	KpUnits *u = &g->u;
	int64_t cs, ws, wt, ct;
	if (kp_unit_is_dead(g, c)) return 0;
	cs = u->status[c];
	ws = u->status[kp_game_unit(g, wanted)];
	if (kp_band(cs, STATUS_IGNORED) != 0 || kp_band(ws, STATUS_IGNORED) != 0) return 0;
	wt = u->type[wanted];
	ct = u->type[c];
	if (kp_band(KP_DAT(g, units, special_ability_flags, wt), POWERUP) != 0)
		return kp_band(cs, 2) != 0 || kp_band(KP_DAT(g, units, special_ability_flags, ct), POWERUP) != 0;
	if (producer_child(ct, wt)) return 1;
	return kp_band(kp_shrv(kp_bxor(ws, cs), 2), 1) == 0;
}

/* The first unit in the rectangle that blocks n (0x443110 with 0x47ea20), or 0. */
int64_t kp_placement_blocker(KpGame *g, int64_t n, int64_t left, int64_t top, int64_t right, int64_t bottom)
{
	int64_t units[KP_UNITS];
	int count = kp_spatial_query(g, left, top, right, bottom, units, KP_UNITS), i;
	for (i = 0; i < count; i++) {
		if (kp_placement_blocks(g, units[i], n)) return units[i];
	}
	return 0;
}

/* RegionAt 0x481b10. */
int64_t kp_region_at(KpGame *g, int64_t x, int64_t y)
{
	int64_t v = kp_region_cell(g, kp_sarv(x, 5), kp_sarv(y, 5)), at, mask, bit;
	if (v < 0x2000) return v;
	at = g->regions.splits_at + (v - 0x2000) * 6;
	mask = kp_rgns_read(g, at, 2);
	bit = kp_band(kp_sarv(y, 3), 3) * 4 + kp_band(kp_sarv(x, 3), 3);
	if (kp_band(mask, (int64_t)1 << bit) != 0) return kp_rgns_read(g, at + 4, 2);
	return kp_rgns_read(g, at + 2, 2);
}

/* UnitCanReachPoint 0x481d00: unit n's region and the point's are in one ground group. */
int kp_same_region_group(KpGame *g, int64_t n, int64_t x, int64_t y)
{
	int64_t sx, sy, here, there;
	kp_sprite_pos(g, n, &sx, &sy);
	here = kp_region_at(g, sx, sy);
	there = kp_region_at(g, kp_s16v(x), kp_s16v(y));
	return kp_region_group(g, here) == kp_region_group(g, there);
}

/* Reachable 0x48e980: a unit that does not path reaches any point, another as 0x481d00. */
int kp_placement_reachable(KpGame *g, int64_t n, int64_t x, int64_t y)
{
	if (kp_band(g->u.pathing_flags[kp_game_unit(g, n)], 1) == 0) return 1;
	return kp_same_region_group(g, n, x, y);
}

/* GetUnitBoundsAt 0x409d20. */
void kp_type_box(KpGame *g, int64_t t, int64_t x, int64_t y, int64_t *left, int64_t *top, int64_t *right, int64_t *bottom)
{
	*left = kp_s16v(x - ext(g, t * 4));
	*top = kp_s16v(y - ext(g, t * 4 + 1));
	*right = kp_s16v(x + ext(g, t * 4 + 2));
	*bottom = kp_s16v(y + ext(g, t * 4 + 3));
}

/* IsBoxInValidBounds 0x47e300. */
static int valid(KpGame *g, int64_t l, int64_t t, int64_t r, int64_t b)
{
	return l >= 0 && r < g->map_w_px && t >= 0 && b < g->map_h_px - BOTTOM_RESERVE;
}

/* The additional point of 0x47dc30 and 0x47e340 never matters: CheckAdditionalPlacementPoint 0x4928f0 returns 0. */
static int accept(KpGame *g, int64_t n, int64_t x, int64_t y)
{
	if (!kp_placement_reachable(g, n, x, y)) return 0;
	return kp_band(g->u.status[n], 4) != 0 || kp_unit_type_fits(g, g->u.type[n], x, y);
}

static int64_t align8(int64_t pos, int64_t step)
{
	int64_t r = kp_band(pos, 7);
	if (r != 0) step = step + 8 - r;
	return step;
}

/* PlacementRingScan 0x47e340. rect: {left, top, right, bottom}. */
int kp_placement_ring(KpGame *g, int64_t n, int64_t x, int64_t y, int64_t w, int64_t h, const int64_t *rect, int64_t *px,
	int64_t *py)
{
	int64_t t = g->u.type[kp_game_unit(g, n)], width, x0, x1, y0, y1, start, cx, cy, l, tp, r, b;
	width = kp_s16v(ext(g, t * 4 + 2) + 1 + ext(g, t * 4));
	x0 = kp_s16v(kp_band(x, 0xfff8));
	if (x0 < rect[0]) x0 = rect[0];
	x1 = kp_s16v(kp_band(x + w + 7, 0xfff8));
	if (x1 > rect[2]) x1 = rect[2];
	y0 = kp_s16v(kp_band(y, 0xfff8));
	if (y0 < rect[1]) y0 = rect[1];
	y1 = kp_s16v(kp_band(y + h + 7, 0xfff8));
	if (y1 > rect[3]) y1 = rect[3];
	start = x0;
	if (w > width * 2) start = kp_s16v(kp_band(width + 7, 0xfffffff8) + x0);
	/* Bottom side. */
	cx = start;
	kp_type_box(g, t, cx, y1, &l, &tp, &r, &b);
	while (cx <= x1) {
		int64_t step = 8;
		if (valid(g, l, tp, r, b)) {
			int64_t c = kp_placement_blocker(g, n, l, tp, r, b);
			if (c == 0) {
				if (accept(g, n, cx, y1)) {
					*px = cx;
					*py = y1;
					return 1;
				}
			} else {
				int64_t cl, ct, cr, cb;
				kp_unit_rect(g, c, &cl, &ct, &cr, &cb);
				step = align8(cx + (cr - l + 1), cr - l + 1);
			}
		}
		cx = kp_s16v(cx + step);
		l = kp_s16v(l + step);
		r = kp_s16v(r + step);
	}
	/* Right side. */
	cy = y1;
	kp_type_box(g, t, x1, cy, &l, &tp, &r, &b);
	while (cy >= y0) {
		int64_t step = 8;
		if (valid(g, l, tp, r, b)) {
			int64_t c = kp_placement_blocker(g, n, l, tp, r, b);
			if (c == 0) {
				if (accept(g, n, x1, cy)) {
					*px = x1;
					*py = cy;
					return 1;
				}
			} else {
				int64_t cl, ct, cr, cb;
				kp_unit_rect(g, c, &cl, &ct, &cr, &cb);
				step = align8(cy - (b - ct + 1), b - ct + 1);
			}
		}
		cy = kp_s16v(cy - step);
		tp = kp_s16v(tp - step);
		b = kp_s16v(b - step);
	}
	/* Top side. */
	cx = x1;
	kp_type_box(g, t, cx, y0, &l, &tp, &r, &b);
	while (cx >= x0) {
		int64_t step = 8;
		if (valid(g, l, tp, r, b)) {
			int64_t c = kp_placement_blocker(g, n, l, tp, r, b);
			if (c == 0) {
				if (accept(g, n, cx, y0)) {
					*px = cx;
					*py = y0;
					return 1;
				}
			} else {
				int64_t cl, ct, cr, cb;
				kp_unit_rect(g, c, &cl, &ct, &cr, &cb);
				step = align8(cx - (r - cl + 1), r - cl + 1);
			}
		}
		cx = kp_s16v(cx - step);
		l = kp_s16v(l - step);
		r = kp_s16v(r - step);
	}
	/* Left side. */
	cy = y0;
	kp_type_box(g, t, x0, cy, &l, &tp, &r, &b);
	while (cy <= y1) {
		int64_t step = 8;
		if (valid(g, l, tp, r, b)) {
			int64_t c = kp_placement_blocker(g, n, l, tp, r, b);
			if (c == 0) {
				if (accept(g, n, x0, cy)) {
					*px = x0;
					*py = cy;
					return 1;
				}
			} else {
				int64_t cl, ct, cr, cb;
				kp_unit_rect(g, c, &cl, &ct, &cr, &cb);
				step = align8(cy + (cb - tp + 1), cb - tp + 1);
			}
		}
		cy = kp_s16v(cy + step);
		tp = kp_s16v(tp + step);
		b = kp_s16v(b + step);
	}
	return 0;
}

/* FindUnitPlacement 0x47dc30. */
int kp_find_unit_placement(KpGame *g, int64_t n, int64_t x, int64_t y, int64_t *rect, int displace, int64_t *px,
	int64_t *py)
{
	KpUnits *u = &g->u;
	int64_t own[4], t, top, bottom, left, right, blocker, w = 8, h = 8, h2;
	if (!rect) {
		own[0] = kp_s16v(x - SEARCH_RADIUS);
		own[1] = kp_s16v(y - SEARCH_RADIUS);
		own[2] = kp_s16v(x + 0x7f);
		own[3] = kp_s16v(y + 0x7f);
		rect = own;
	}
	if (rect[1] < 0) rect[1] = 0;
	if (g->map_h_px - BOTTOM_RESERVE <= rect[3]) rect[3] = kp_s16v(g->map_h_px - BOTTOM_RESERVE - 1);
	if (rect[0] < 0) rect[0] = 0;
	if (g->map_w_px <= rect[2]) rect[2] = kp_s16v(g->map_w_px - 1);
	t = u->type[kp_game_unit(g, n)];
	top = kp_s16v(y - ext(g, t * 4 + 1));
	bottom = kp_s16v(ext(g, t * 4 + 3) + y);
	left = kp_s16v(x - ext(g, t * 4));
	right = kp_s16v(ext(g, t * 4 + 2) + x);
	blocker = kp_placement_blocker(g, n, left, top, right, bottom);
	if (blocker == 0 && left >= 0 && right < g->map_w_px && top >= 0 && bottom < g->map_h_px - BOTTOM_RESERVE) {
		if (kp_band(u->status[n], 4) != 0 || kp_unit_type_fits(g, t, x, y)) {
			*px = x;
			*py = y;
			return 1;
		}
		if (!displace) {
			kp_scalar_set(g, KP_ERROR_CODE, ERROR_PLACEMENT);
			return 0;
		}
	}
	if (blocker != 0) {
		int64_t bt = u->type[blocker];
		int64_t bw = kp_s16v(ext(g, bt * 4 + 2)) + kp_s16v(ext(g, t * 4)) + 2;
		int64_t bh = kp_s16v(ext(g, bt * 4 + 3)) + kp_s16v(ext(g, t * 4 + 1)) + 2;
		w = bw < 9 ? 8 : kp_mod(bw, 65536);
		h = bh < 9 ? 8 : kp_mod(bh, 65536);
	}
	h2 = kp_mod(h * 2, 65536);
	for (;;) {
		int64_t sw = kp_s16v(w), sh = kp_s16v(h), rx, ry;
		int found;
		if (x - sw < rect[0] && rect[2] < sw + x && y - sh < rect[1] && rect[3] < sh + y) break;
		found = kp_placement_ring(g, n, kp_s16v(x - w), kp_s16v(y - h), kp_s16v(w * 2), kp_s16v(h2), rect, &rx, &ry);
		h2 = kp_mod(h2 + 0x20, 65536);
		w = kp_mod(w + 16, 65536);
		h = kp_mod(h + 16, 65536);
		if (found) {
			*px = rx;
			*py = ry;
			return 1;
		}
	}
	kp_scalar_set(g, KP_ERROR_CODE, ERROR_PLACEMENT);
	return 0;
}
