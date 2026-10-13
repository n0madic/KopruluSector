/* The collision checks of a unit's prepared step (kp_collision.h; game/units/collision.lua, the
 * specification): the units swept by the move (FindUnitsSwept 0x4217f0), the first that blocks it
 * (FindFirstUnitBlocker 0x42d900), and the terrain check against the unit's cached safe rectangle
 * (ContractSafeRect 0x48b9e0, CheckTerrainSweepCollision 0x42da50). */
#include "kp_game_internal.h"

#define STATUS_PATHED 0x100000
#define STATUS_IGNORED 0x200000
#define SPRITE_HIDDEN 0x20
#define SAFE_RADIUS 0x80
#define EXTENT_KEYS 12 /* the keys 0..11 of g.extent_ctx */

/* The sizes of kp_collision.h are those of kp_game.h. */
typedef char kp_collision_sizes_match[(KP_COLLISION_ENTRIES == KP_SPATIAL_CAPACITY
	&& KP_COLLISION_QUERY_MAX == KP_UNITS) ? 1 : -1];

/* The slide direction past an obstacle on a side (0 north of the mover, 1 east, 2 south, 3 west, as
 * CardinalSide 0x42cc70 numbers them) for a mover heading into a quadrant; -1 where the Lua table has no
 * entry (no slide; the jump tables of 0x42dc70 and 0x42db60). */
static const int64_t SIDE_TURNS[4][4] = {
	{ 0x40, -1, -1, 0xc0 },
	{ 0, 0x80, -1, -1 },
	{ -1, 0x40, 0xc0, -1 },
	{ -1, -1, 0x80, 0 },
};

static int64_t side_turn(int64_t side, int64_t q)
{
	if (side < 0 || side > 3 || q < 0 || q > 3) return -1;
	return SIDE_TURNS[side][q];
}

static int64_t ext(KpGame *g, int64_t i) { return KP_DAT(g, units, unit_extents, i); }

/* A unit's record (a nil record fails as indexing it does in Lua). */
#define UNIT(c) kp_game_unit(g, (c))

/* Entry k of a spatial array, where the Lua code computes with it (nil outside 0..capacity-1 fails). */
static int64_t at_get(KpGame *g, const int64_t *a, int64_t k)
{
	if (k < 0 || k >= KP_SPATIAL_CAPACITY) kp_game_fail(g, "collision: spatial entry %lld is nil", (long long)k);
	return a[k];
}

/* Entry k of a spatial array for an equality test (nil compares unequal). */
static int64_t at_opt(const int64_t *a, int64_t k)
{
	if (k < 0 || k >= KP_SPATIAL_CAPACITY) return KP_NIL;
	return a[k];
}

static void push(KpGame *g, int64_t *out, int *count, int max, int64_t v)
{
	if (*count >= max) kp_game_fail(g, "collision: a unit list longer than %d", max);
	out[(*count)++] = v;
}

static int64_t sprite_x(KpGame *g, int32_t s) { return kp_s16v(g->w->spr.x[s]); }
static int64_t sprite_y(KpGame *g, int32_t s) { return kp_s16v(g->w->spr.y[s]); }

static int sprite_hidden(KpGame *g, int64_t n)
{
	return kp_band(g->w->spr.flags[kp_unit_sprite(g, n)], SPRITE_HIDDEN) != 0;
}

/* One direction of FindUnitsSwept: entries k, k + step, ... while their coordinate is within lim (<= going
 * up, >= going down), the units other than n whose bounds on the crossing axis (cross_at at the positions
 * c_lo, c_hi) meet [lo, hi]. */
static void swept_scan(KpGame *g, int64_t *out, int *count, int max, int64_t n, int64_t entries, const int64_t *at,
	const int64_t *units, int64_t k, int64_t step, int64_t lim, int64_t lo, int64_t hi, const int64_t *cross_at,
	const int64_t *c_lo, const int64_t *c_hi)
{
	while (k >= 0 && k < entries) {
		int64_t v = at_get(g, at, k), c;
		if (step > 0) {
			if (v > lim) break;
		} else if (v < lim) {
			break;
		}
		c = at_get(g, units, k);
		if (c != n && lo <= at_get(g, cross_at, c_hi[UNIT(c)]) && at_get(g, cross_at, c_lo[UNIT(c)]) <= hi) {
			push(g, out, count, max, c);
		}
		k = k + step;
	}
}

/* FindUnitsSwept 0x4217f0: the units whose bounds the box of unit n crosses when moved by (dx, dy), scanned
 * from the unit's own bound entries outwards (a unit can appear twice). */
int kp_units_swept(KpGame *g, int64_t n, int64_t dx, int64_t dy, int64_t *out, int max)
{
	KpUnits *u = &g->u;
	KpSpatial *sp = &g->spatial;
	const int64_t *xa = sp->x_at, *ya = sp->y_at, *xu = sp->x_unit, *yu = sp->y_unit;
	int64_t count = sp->count;
	int found = 0;
	if (sprite_hidden(g, n)) dx = dy = 0;
	if (dx >= 1) {
		int64_t r = kp_s16v(at_get(g, xa, u->bound_right[n]));
		int64_t t = kp_s16v(at_get(g, ya, u->bound_top[n])), b = kp_s16v(at_get(g, ya, u->bound_bottom[n]));
		int64_t k = u->bound_right[n];
		while (at_opt(xa, k - 1) == r) k = k - 1;
		swept_scan(g, out, &found, max, n, count, xa, xu, k, 1, kp_s16v(r + dx), kp_s16v(t + dy), kp_s16v(b + dy),
			ya, u->bound_top, u->bound_bottom);
	} else if (dx < 0) {
		int64_t l = kp_s16v(at_get(g, xa, u->bound_left[n]));
		int64_t t = kp_s16v(at_get(g, ya, u->bound_top[n])), b = kp_s16v(at_get(g, ya, u->bound_bottom[n]));
		int64_t k = u->bound_left[n];
		while (at_opt(xa, k + 1) == l) k = k + 1;
		swept_scan(g, out, &found, max, n, count, xa, xu, k, -1, kp_s16v(dx + l), kp_s16v(t + dy), kp_s16v(b + dy),
			ya, u->bound_top, u->bound_bottom);
	}
	if (dy >= 1) {
		int64_t b = kp_s16v(at_get(g, ya, u->bound_bottom[n]));
		int64_t l = kp_s16v(at_get(g, xa, u->bound_left[n])), r = kp_s16v(at_get(g, xa, u->bound_right[n]));
		int64_t k = u->bound_bottom[n];
		while (at_opt(ya, k - 1) == b) k = k - 1;
		swept_scan(g, out, &found, max, n, count, ya, yu, k, 1, kp_s16v(b + dy), kp_s16v(l + dx), kp_s16v(r + dx),
			xa, u->bound_left, u->bound_right);
	} else if (dy < 0) {
		int64_t t = kp_s16v(at_get(g, ya, u->bound_top[n]));
		int64_t l = kp_s16v(at_get(g, xa, u->bound_left[n])), r = kp_s16v(at_get(g, xa, u->bound_right[n]));
		int64_t k = u->bound_top[n];
		while (at_opt(ya, k + 1) == t) k = k + 1;
		swept_scan(g, out, &found, max, n, count, ya, yu, k, -1, kp_s16v(dy + t), kp_s16v(l + dx), kp_s16v(r + dx),
			xa, u->bound_left, u->bound_right);
	}
	return found;
}

/* FindFirstUnitBlocker 0x42d900 for the prepared step (g->mv): the first swept ground unit that unit n
 * collides with and that moves on the ground itself, or 0. */
static int64_t first_blocker(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	KpCollision *col = &g->collision;
	int32_t s = kp_unit_sprite(g, n);
	int64_t dx = g->mv.next_x - sprite_x(g, s);
	int64_t dy = g->mv.next_y - sprite_y(g, s);
	int count = kp_units_swept(g, n, dx, dy, col->swept, KP_COLLISION_SWEPT_MAX), i;
	for (i = 0; i < count; i++) {
		int64_t c = UNIT(col->swept[i]);
		if (kp_band(u->pathing_flags[c], 1) != 0 && kp_band(u->status[c], STATUS_IGNORED) == 0 && c != n
			&& kp_units_collide(g, c, n) && kp_band(u->status[c], STATUS_PATHED) != 0) {
			return c;
		}
	}
	return 0;
}

/* FindFirstUnitBlocker 0x42d900 without the ground-mover gate of 0x42da30. */
int64_t kp_ground_unit_collision_any(KpGame *g, int64_t n) { return first_blocker(g, n); }

/* CheckGroundUnitCollision 0x42da30. */
int64_t kp_ground_unit_collision(KpGame *g, int64_t n)
{
	if (kp_band(g->u.status[UNIT(n)], STATUS_PATHED) == 0) return 0;
	return first_blocker(g, n);
}

static int in_rect(const KpUnits *u, int64_t n, int64_t x, int64_t y)
{
	return x >= u->contour_left[n] && x <= u->contour_right[n] && y >= u->contour_top[n] && y <= u->contour_bottom[n];
}

/* CheckTerrainOrBoundsCollision 0x42d990: true when the prepared step leaves the map (a unit without ground
 * pathing) or crosses terrain. */
int kp_terrain_collision(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	KpMove *mv = &g->mv;
	int64_t t = u->type[UNIT(n)];
	int32_t s;
	if (kp_band(u->status[n], STATUS_PATHED) == 0) {
		if (mv->next_x - kp_s16v(ext(g, t * 4)) < 0 || mv->next_y - kp_s16v(ext(g, t * 4 + 1)) < 0) return 1;
		if (kp_s16v(ext(g, t * 4 + 2)) + mv->next_x >= g->map_w_px) return 1;
		return kp_s16v(ext(g, t * 4 + 3)) + mv->next_y >= g->map_h_px;
	}
	s = kp_unit_sprite(g, n);
	return kp_terrain_sweep_collision(g, n, mv->next_x - sprite_x(g, s), mv->next_y - sprite_y(g, s));
}

/* CheckTerrainSweepCollision 0x42da50: true when the move of unit n by (dx, dy) leaves its safe rectangle
 * (rebuilt around the unit when needed) and crosses a terrain contour. */
int kp_terrain_sweep_collision(KpGame *g, int64_t n, int64_t dx, int64_t dy)
{
	KpUnits *u = &g->u;
	int64_t t = u->type[UNIT(n)];
	int32_t s = kp_unit_sprite(g, n);
	int64_t sx = sprite_x(g, s), sy = sprite_y(g, s);
	int64_t nx = kp_s16v(sx + dx), ny = kp_s16v(sy + dy);
	if (in_rect(u, n, nx, ny)) return 0;
	u->contour_top[n] = kp_s16v(sy - SAFE_RADIUS);
	u->contour_left[n] = kp_s16v(sx - SAFE_RADIUS);
	u->contour_bottom[n] = kp_s16v(sy + SAFE_RADIUS);
	u->contour_right[n] = kp_s16v(sx + SAFE_RADIUS);
	kp_contour_extents(g, t);
	kp_contract_safe_rect(g, n, sx, sy);
	if (in_rect(u, n, nx, ny)) return 0;
	return !kp_sweep_contours_clear(g, sx, sy, nx, ny);
}

/* e[i] of the global contour context (nil fails as the Lua arithmetic on it does). */
static int64_t ectx(KpGame *g, const int64_t *e, int64_t i)
{
	if (i < 0 || i >= EXTENT_KEYS || e[i] == KP_NIL) {
		kp_game_fail(g, "collision: extent_ctx[%lld] is nil", (long long)i);
	}
	return e[i];
}

static void contour_check(KpGame *g, const KpContourSide *a, int64_t k)
{
	if (k < 0 || k >= a->count) kp_game_fail(g, "collision: contour record %lld is nil", (long long)k);
}

/* The 16-bit signed binary search of 0x48b9e0 / 0x48bec0: the first record whose axis (doubled, 16-bit) is
 * above `key`. */
static int64_t first_above(KpGame *g, const KpContourSide *a, int64_t key)
{
	int64_t low = 0, high = a->count;
	while (low < high) {
		int64_t mid = kp_shrv(low + high, 1);
		contour_check(g, a, mid);
		if (kp_s16v(key) >= kp_s16v(a->axis[mid] * 2)) low = mid + 1;
		else high = mid;
	}
	return high;
}

/* Record k of a contour side with the prepared extents: its axis, start and stop (and info). */
static void contour_span(KpGame *g, const int64_t *e, const KpContourSide *a, int64_t k, int64_t *axis,
	int64_t *start, int64_t *stop)
{
	contour_check(g, a, k);
	*axis = kp_s16v(ectx(g, e, a->dir[k]) + a->axis[k]);
	*start = kp_s16v(ectx(g, e, kp_band(a->info[k], 3)) + a->start[k]);
	*stop = kp_s16v(ectx(g, e, kp_band(kp_shrv(a->info[k], 2), 3)) + a->stop[k]);
}

/* ContractSafeRect 0x48b9e0: unit n's safe rectangle shrinks around (x, y) until no terrain contour (with
 * the prepared extents of the global contour context) lies inside. */
void kp_contract_safe_rect(KpGame *g, int64_t n, int64_t x, int64_t y)
{
	KpUnits *u = &g->u;
	const KpContourSide *ta = &g->contours[0], *ra = &g->contours[1], *ba = &g->contours[2], *la = &g->contours[3];
	const int64_t *e = g->pathing.extent_ctx;
	int64_t left = u->contour_left[UNIT(n)], top = u->contour_top[n], right = u->contour_right[n];
	int64_t bottom = u->contour_bottom[n];
	int64_t tk = ta->count == 0 ? 0 : first_above(g, ta, (y - ectx(g, e, 0)) * 2 + 1);
	int64_t rk = ra->count == 0 ? 0 : first_above(g, ra, (x - ectx(g, e, 1)) * 2 - 1);
	int64_t bk = ba->count == 0 ? 0 : first_above(g, ba, (y - ectx(g, e, 2)) * 2 - 1);
	int64_t lk = la->count == 0 ? 0 : first_above(g, la, (x - ectx(g, e, 3)) * 2 + 1);
	for (;;) {
		int progress;
		for (;;) {
			int64_t A, S, T;
			progress = 0;
			if (tk > 0) {
				progress = 1;
				contour_span(g, e, ta, tk - 1, &A, &S, &T);
				if (A < top) {
					tk = 0;
				} else {
					tk = tk - 1;
					if (T >= left && S <= right) {
						if (x < S && y - A < S - x) {
							right = S - 1;
						} else if (T < x && y - A < x - T) {
							left = T + 1;
						} else {
							top = A + 1;
						}
					}
				}
			}
			if (rk < ra->count) {
				progress = 1;
				contour_span(g, e, ra, rk, &A, &S, &T);
				if (A > right) {
					rk = ra->count;
				} else {
					rk = rk + 1;
					if (T >= top && S <= bottom) {
						if (y < S && A - x < S - y) {
							bottom = S - 1;
						} else if (T < y && A - x < y - T) {
							top = T + 1;
						} else {
							right = A - 1;
						}
					}
				}
			}
			if (bk < ba->count) {
				progress = 1;
				contour_span(g, e, ba, bk, &A, &S, &T);
				if (A > bottom) {
					bk = ba->count;
				} else {
					bk = bk + 1;
					if (T >= left && S <= right) {
						if (x < S && A - y < S - x) {
							right = S - 1;
						} else if (T < x && A - y < x - T) {
							left = T + 1;
						} else {
							bottom = A - 1;
						}
					}
				}
			}
			if (lk <= 0) break;
			contour_span(g, e, la, lk - 1, &A, &S, &T);
			if (A < left) {
				lk = 0;
			} else {
				lk = lk - 1;
				if (T >= top && S <= bottom) {
					if (y < S && x - A < S - y) {
						bottom = S - 1;
					} else if (T < y && x - A < y - T) {
						top = T + 1;
					} else {
						left = A + 1;
					}
				}
			}
		}
		if (!progress) break;
	}
	u->contour_left[n] = kp_s16v(left);
	u->contour_top[n] = kp_s16v(top);
	u->contour_right[n] = kp_s16v(right);
	u->contour_bottom[n] = kp_s16v(bottom);
}

/* units_adjacent's overlap tests of unit c with the box (l, t, r, b) widened by ry (y) or not (x). */
static int y_overlap(KpGame *g, int64_t c, int64_t t, int64_t b, int64_t ry)
{
	KpUnits *u = &g->u;
	const int64_t *ya = g->spatial.y_at;
	return kp_s16v(t - ry) <= at_get(g, ya, u->bound_bottom[UNIT(c)])
		&& at_get(g, ya, u->bound_top[c]) < kp_s16v(b + ry);
}

static int x_overlap(KpGame *g, int64_t c, int64_t l, int64_t r)
{
	KpUnits *u = &g->u;
	const int64_t *xa = g->spatial.x_at;
	return l <= at_get(g, xa, u->bound_right[UNIT(c)]) && at_get(g, xa, u->bound_left[c]) < r;
}

/* FindUnitsAdjacentMoving 0x421dc0: the units next to unit n within (rx, ry), scanned from its bound
 * entries outwards (right, left, below, above). */
int kp_units_adjacent(KpGame *g, int64_t n, int64_t rx, int64_t ry, int64_t *out, int max)
{
	KpUnits *u = &g->u;
	KpSpatial *sp = &g->spatial;
	const int64_t *xa = sp->x_at, *ya = sp->y_at, *xu = sp->x_unit, *yu = sp->y_unit;
	int64_t l, r, t, b, k, last;
	int found = 0;
	if (sprite_hidden(g, n)) return 0;
	l = kp_s16v(at_get(g, xa, u->bound_left[n]));
	r = kp_s16v(at_get(g, xa, u->bound_right[n]));
	t = kp_s16v(at_get(g, ya, u->bound_top[n]));
	b = kp_s16v(at_get(g, ya, u->bound_bottom[n]));
	last = sp->count - 1;
	for (k = u->bound_right[n] + 1; k <= last; k++) {
		int64_t c;
		if (kp_s16v(rx + r) <= at_get(g, xa, k)) break;
		c = at_get(g, xu, k);
		if (c != n && y_overlap(g, c, t, b, ry)) push(g, out, &found, max, c);
	}
	for (k = u->bound_left[n] - 1; k >= 0; k--) {
		int64_t c;
		if (at_get(g, xa, k) < kp_s16v(l - rx)) break;
		c = at_get(g, xu, k);
		if (c != n && y_overlap(g, c, t, b, ry)) push(g, out, &found, max, c);
	}
	for (k = u->bound_bottom[n] + 1; k <= last; k++) {
		int64_t c;
		if (kp_s16v(ry + b) <= at_get(g, ya, k)) break;
		c = at_get(g, yu, k);
		if (c != n && x_overlap(g, c, l, r)) push(g, out, &found, max, c);
	}
	for (k = u->bound_top[n] - 1; k >= 0; k--) {
		int64_t c;
		if (at_get(g, ya, k) < kp_s16v(t - ry)) break;
		c = at_get(g, yu, k);
		if (c != n && x_overlap(g, c, l, r)) push(g, out, &found, max, c);
	}
	return found;
}

/* CollectMovingNeighbors 0x42e1c0: up to `limit` moving ground units next to unit n that it collides with. */
int kp_moving_neighbors(KpGame *g, int64_t n, int64_t radius, int64_t limit, int64_t *out, int out_max)
{
	KpUnits *u = &g->u;
	KpCollision *col = &g->collision;
	int count = kp_units_adjacent(g, n, radius, radius, col->adjacent, KP_COLLISION_ADJACENT_MAX), i, found = 0;
	for (i = 0; i < count; i++) {
		int64_t c = UNIT(col->adjacent[i]);
		int32_t s = kp_unit_sprite(g, c);
		int at_rest = u->move_x[c] == sprite_x(g, s) && u->move_y[c] == sprite_y(g, s);
		if (!at_rest && kp_band(u->status[c], STATUS_PATHED) != 0 && kp_band(u->pathing_flags[c], 1) != 0
			&& kp_band(u->status[c], STATUS_IGNORED) == 0 && (n == 0 || (c != n && kp_units_collide(g, c, n)))) {
			push(g, out, &found, out_max, c);
			if (found == limit) break;
		}
	}
	return found;
}

/* IsPositionBlocked 0x42eab0: -1 outside the map or where unit n's type does not fit, else the largest
 * ground unit at (x, y) that n collides with (callback 0x42e130), else 0. */
int64_t kp_position_blocked(KpGame *g, int64_t n, int64_t x, int64_t y)
{
	KpUnits *u = &g->u;
	KpSpatial *sp = &g->spatial;
	KpCollision *col = &g->collision;
	int64_t t = u->type[UNIT(n)], l, tp, r, b, best = 0, area = 0;
	int count, i;
	x = kp_s16v(x);
	y = kp_s16v(y);
	if (x - kp_s16v(ext(g, t * 4)) < 0 || kp_s16v(ext(g, t * 4 + 2)) + x >= g->map_w_px) return -1;
	if (y - kp_s16v(ext(g, t * 4 + 1)) < 0 || kp_s16v(ext(g, t * 4 + 3)) + y >= g->map_h_px) return -1;
	kp_type_box(g, t, x, y, &l, &tp, &r, &b);
	count = kp_spatial_query(g, l, tp, r, b, col->query, KP_COLLISION_QUERY_MAX);
	for (i = 0; i < count; i++) {
		int64_t c = UNIT(col->query[i]);
		if (kp_band(u->pathing_flags[c], 1) != 0 && kp_band(u->status[c], STATUS_IGNORED) == 0 && c != n
			&& kp_units_collide(g, c, n)
			&& l <= at_get(g, sp->x_at, u->bound_right[c]) && at_get(g, sp->x_at, u->bound_left[c]) <= r
			&& tp <= at_get(g, sp->y_at, u->bound_bottom[c]) && at_get(g, sp->y_at, u->bound_top[c]) <= b) {
			int64_t ct = u->type[c];
			int64_t a = kp_s16v(ext(g, ct * 4 + 3) + 1 + ext(g, ct * 4 + 1))
				* kp_s16v(ext(g, ct * 4 + 2) + 1 + ext(g, ct * 4));
			if (a > area) {
				best = c;
				area = a;
			}
		}
	}
	if (best != 0) return best;
	return kp_unit_type_fits(g, t, x, y) ? 0 : -1;
}

/* IsFootprintTouchingBoxAt 0x42e400 (and IsFootprintTouchingUnit 0x42e360 at the blocker's position): a
 * type-t box at (x, y) touches blocker c standing at (cx, cy). */
int kp_footprint_touches(KpGame *g, int64_t c, int64_t cx, int64_t cy, int64_t t, int64_t x, int64_t y)
{
	int64_t ct = g->u.type[UNIT(c)];
	return kp_s16v(cx - ext(g, ct * 4) - ext(g, t * 4 + 2)) <= x && x <= kp_s16v(ext(g, ct * 4 + 2) + cx + ext(g, t * 4))
		&& kp_s16v(cy - ext(g, ct * 4 + 1) - ext(g, t * 4 + 3)) <= y
		&& y <= kp_s16v(ext(g, ct * 4 + 3) + cy + ext(g, t * 4 + 1));
}

static void unit_box(KpGame *g, int64_t k, int64_t *l, int64_t *t, int64_t *r, int64_t *b)
{
	int64_t type = g->u.type[UNIT(k)];
	int32_t s = kp_unit_sprite(g, k);
	int64_t x = sprite_x(g, s), y = sprite_y(g, s);
	*l = kp_s16v(x - ext(g, type * 4));
	*t = kp_s16v(y - ext(g, type * 4 + 1));
	*r = kp_s16v(ext(g, type * 4 + 2) + x);
	*b = kp_s16v(ext(g, type * 4 + 3) + y);
}

static int64_t abs64(int64_t v) { return v < 0 ? -v : v; }

/* GetCardinalSide 0x42cc70: the side of unit n on which blocker c lies (0 above, 1 right, 2 below, 3 left),
 * by separation or else by the smallest overlap. */
int64_t kp_cardinal_side(KpGame *g, int64_t n, int64_t c)
{
	int64_t bl, bt, br, bb, sl, st, sr, sb, side, d;
	unit_box(g, c, &bl, &bt, &br, &bb);
	unit_box(g, n, &sl, &st, &sr, &sb);
	if (br < sl) return 3;
	if (sr < bl) return 1;
	if (bb < st) return 0;
	if (sb < bt) return 2;
	side = 1;
	d = abs64(bl - sr);
	if (abs64(br - sl) < d) {
		side = 3;
		d = abs64(br - sl);
	}
	if (abs64(bt - sb) < d) {
		side = 2;
		d = abs64(bt - sb);
	}
	if (abs64(bb - st) < d) side = 0;
	return side;
}

static int64_t heading_difference(int64_t a, int64_t b)
{
	int64_t d = kp_band(b - a, 0xff);
	if (d > 0x80) return 0x100 - d;
	return d;
}

/* IsBlockerHeadingAway 0x42cde0: blocker c's movement direction (to the nearest quarter) turns more than
 * 0x40 from the direction from unit n to it. */
int kp_blocker_heading_away(KpGame *g, int64_t n, int64_t c)
{
	int32_t sn = kp_unit_sprite(g, n), sc = kp_unit_sprite(g, c);
	int64_t d = kp_direction_to(g, sprite_x(g, sn), sprite_y(g, sn), sprite_x(g, sc), sprite_y(g, sc));
	int64_t v = g->u.velocity_direction[c], q;
	if (v < 0x20) q = 0;
	else if (v < 0x60) q = 0x40;
	else if (v < 0xa0) q = 0x80;
	else if (v < 0xe0) q = 0xc0;
	else q = 0;
	return heading_difference(d, q) > 0x40;
}

/* GetTerrainSlideDirection 0x42db60: the side direction along which unit n slides past the terrain that
 * blocks its prepared step, or -1. The blocked side is horizontal when the step's x part alone is blocked
 * (or the step is horizontal), vertical otherwise. */
int64_t kp_terrain_slide_direction(KpGame *g, int64_t n)
{
	KpMove *mv = &g->mv;
	int32_t s = kp_unit_sprite(g, n);
	int64_t dx = mv->next_x - sprite_x(g, s);
	int64_t dy = mv->next_y - sprite_y(g, s);
	int64_t side;
	if (dx != 0 && (dy == 0 || kp_terrain_sweep_collision(g, n, dx, 0))) side = dx < 0 ? 3 : 1;
	else side = dy < 0 ? 0 : 2;
	return side_turn(side, kp_shrv(g->u.heading2[n], 6));
}

/* GetUnitSlideFreeDirection 0x42dc70: the side direction (0, 0x40, 0x80, 0xc0) along which unit n can slide
 * past blocker c towards its waypoint, or -1. */
int64_t kp_slide_direction(KpGame *g, int64_t n, int64_t c)
{
	KpUnits *u = &g->u;
	KpSpatial *sp = &g->spatial;
	int32_t s;
	int64_t d, q, t, L, R, T, B, side, dir, px = 0, py = 0;
	if (sprite_hidden(g, c)) return -1;
	s = kp_unit_sprite(g, n);
	d = kp_direction_to(g, sprite_x(g, s), sprite_y(g, s), u->waypoint_x[n], u->waypoint_y[n]);
	q = kp_shrv(d, 6);
	if (heading_difference(u->heading2[n], d) >= 0x50) return -1;
	t = u->type[n];
	L = kp_s16v(at_get(g, sp->x_at, u->bound_left[c]) - ext(g, t * 4 + 2) - 1);
	R = kp_s16v(at_get(g, sp->x_at, u->bound_right[c]) + 1 + ext(g, t * 4));
	T = kp_s16v(at_get(g, sp->y_at, u->bound_top[c]) - ext(g, t * 4 + 3) - 1);
	B = kp_s16v(at_get(g, sp->y_at, u->bound_bottom[c]) + 1 + ext(g, t * 4 + 1));
	side = kp_cardinal_side(g, n, c);
	dir = side_turn(side, q);
	if (dir < 0) return -1;
	/* Each side sets the coordinate its two slide directions use (px for 0 / 0x80, py for 0x40 / 0xc0). */
	if (side == 0) py = B;
	else if (side == 1) px = L;
	else if (side == 2) py = T;
	else px = R;
	if (dir == 0) {
		if (kp_position_blocked(g, n, px, T) == 0) return 0;
		return kp_position_blocked(g, n, px, B) != 0 ? -1 : 0x80;
	} else if (dir == 0x40) {
		if (kp_position_blocked(g, n, R, py) == 0) return 0x40;
		return kp_position_blocked(g, n, L, py) != 0 ? -1 : 0xc0;
	} else if (dir == 0x80) {
		if (kp_position_blocked(g, n, px, B) == 0) return 0x80;
		return kp_position_blocked(g, n, px, T) != 0 ? -1 : 0;
	}
	if (kp_position_blocked(g, n, L, py) == 0) return 0xc0;
	return kp_position_blocked(g, n, R, py) != 0 ? -1 : 0x40;
}

/* sweep_contours_clear's test: a contour spanning [lo, hi] blocks the step's span [a, b] unless its corner
 * bits (info 0x10, 0x20) open both ends past it. */
static int blocks(int64_t lo, int64_t hi, int64_t info, int64_t a, int64_t b)
{
	if (lo > b || a > hi) return 0;
	return (lo < a && b < hi) || kp_band(info, 0x10) == 0 || lo <= a || kp_band(info, 0x20) == 0 || b <= hi;
}

/* SweepContoursClear 0x48bec0: no terrain contour (with the prepared extents of the global contour context)
 * crosses the step from (x1, y1) to (x2, y2). */
int kp_sweep_contours_clear(KpGame *g, int64_t x1, int64_t y1, int64_t x2, int64_t y2)
{
	const KpContourSide *c = g->contours;
	const int64_t *e = g->pathing.extent_ctx;
	int64_t xlo = x1, xhi = x2, ylo = y2, yhi = y1, k, axis, lo, hi;
	if (x1 >= x2) {
		xlo = x2;
		xhi = x1;
	}
	if (y1 < y2) {
		ylo = y1;
		yhi = y2;
	}
	if (y2 < y1) {
		const KpContourSide *a = &c[0];
		for (k = first_above(g, a, (y1 - ectx(g, e, 0)) * 2 + 1) - 1; k >= 0; k--) {
			contour_span(g, e, a, k, &axis, &lo, &hi);
			if (axis < y2) break;
			if (blocks(lo, hi, a->info[k], xlo, xhi)) return 0;
		}
	} else if (y1 < y2) {
		const KpContourSide *a = &c[2];
		int64_t last = a->count - 1;
		for (k = first_above(g, a, (y1 - ectx(g, e, 2)) * 2 - 1); k <= last; k++) {
			contour_span(g, e, a, k, &axis, &lo, &hi);
			if (axis > y2) break;
			if (blocks(lo, hi, a->info[k], xlo, xhi)) return 0;
		}
	}
	if (x2 < x1) {
		const KpContourSide *a = &c[3];
		for (k = first_above(g, a, (x1 - ectx(g, e, 3)) * 2 + 1) - 1; k >= 0; k--) {
			contour_span(g, e, a, k, &axis, &lo, &hi);
			if (axis < x2) return 1;
			if (blocks(lo, hi, a->info[k], ylo, yhi)) return 0;
		}
	} else if (x1 < x2) {
		const KpContourSide *a = &c[1];
		int64_t last = a->count - 1;
		for (k = first_above(g, a, (x1 - ectx(g, e, 1)) * 2 - 1); k <= last; k++) {
			contour_span(g, e, a, k, &axis, &lo, &hi);
			if (axis > x2) break;
			if (blocks(lo, hi, a->info[k], ylo, yhi)) return 0;
		}
	}
	return 1;
}
