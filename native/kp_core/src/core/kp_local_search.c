/* The local path search (LocalPathSearch 0x491f50) and its geometry context, path obstacles and unit collision
 * (kp_pathing.h). */
#include "kp_pathing_internal.h"

#include <string.h>

#define NONE (-1)
#define DIR_NE 0x01
#define DIR_SE 0x02
#define DIR_SW 0x04
#define DIR_NW 0x08
#define DIR_N 0x10
#define DIR_E 0x20
#define DIR_S 0x40
#define DIR_W 0x80
#define SENTINEL (KP_LOCAL_ARENA + 1)

/* A read view of an edge array: the global contours g->contours[d] or a local array. */
typedef struct {
	int64_t count;
	const int64_t *axis, *start, *stop, *dir, *info;
} Edges;

static Edges view_local(const KpEdgeArray *a)
{
	Edges v;
	v.count = a->count;
	v.axis = a->axis;
	v.start = a->start;
	v.stop = a->stop;
	v.dir = a->dir;
	v.info = a->info;
	return v;
}

/* lg.active[d]. */
static Edges act(KpGame *g, int d)
{
	Edges v;
	const KpContourSide *c;
	if (g->pathing.lg.active_local[d]) return view_local(&g->pathing.lg.loc[d]);
	c = &g->contours[d];
	v.count = c->count;
	v.axis = c->axis;
	v.start = c->start;
	v.stop = c->stop;
	v.dir = c->dir;
	v.info = c->info;
	return v;
}

/* lg.ext[i] (0..3 and 8..11 are set; another index fails). */
static int64_t E(KpGame *g, int64_t i)
{
	if (!((i >= 0 && i <= 3) || (i >= 8 && i <= 11))) kp_game_fail(g, "local search: extent %lld", (long long)i);
	return g->pathing.lg.ext[i];
}

static int64_t approx(int64_t x1, int64_t y1, int64_t x2, int64_t y2) { return kp_approx_distance(x1 - x2, y1 - y2); }

/* rg.mark[region] (0 outside the graph). */
static int64_t mark_get(KpGame *g, int64_t region)
{
	const KpRegionGraph *rg = &g->pathing.rg;
	if (region < 0 || region >= rg->count) return 0;
	return rg->mark[region];
}

static void mark_set(KpGame *g, int64_t region, int64_t v) { g->pathing.rg.mark[kp_rg_check(g, region)] = v; }

/* The geometry context and the swept areas (local_geometry_init). */
void kp_local_geometry_init(KpGame *g)
{
	KpPathing *p = &g->pathing;
	memset(&p->lg, 0, sizeof p->lg);
	memset(&p->visited, 0, sizeof p->visited);
	p->visited.xs_hw = -1;
	p->visited.colmap_hw = -1;
	p->visited.lists_hw = -1;
}

static void copy_edge(KpEdgeArray *a, int64_t k, int64_t j)
{
	a->axis[j] = a->axis[k];
	a->start[j] = a->start[k];
	a->stop[j] = a->stop[k];
	a->dir[j] = a->dir[k];
	a->info[j] = a->info[k];
}

/* The extent of record k's ends. */
static int64_t lo_e(const int64_t *e, const int64_t *info, int64_t k) { return e[kp_band(info[k], 3)]; }
static int64_t hi_e(const int64_t *e, const int64_t *info, int64_t k) { return e[kp_band(kp_shrv(info[k], 2), 3)]; }

/* InsertLocalEdge 0x488f00. */
static int64_t insert_edge(KpGame *g, KpEdgeArray *a, int64_t cursor, int64_t axis, int64_t start, int64_t stop,
	int64_t dir, int64_t info)
{
	const int64_t *e = g->pathing.lg.ext;
	int64_t rlo = e[kp_band(info, 3)] + start, p = cursor, k;
	while (p < a->count) {
		if (a->axis[p] > axis || (a->axis[p] == axis && hi_e(e, a->info, p) + a->stop[p] >= rlo)) {
			if (a->axis[p] == axis && hi_e(e, a->info, p) + 1 + a->stop[p] >= rlo
				&& lo_e(e, a->info, p) - 1 + a->start[p] <= e[kp_band(kp_shrv(info, 2), 3)] + stop) {
				if (a->start[p] > start) a->start[p] = start;
				if (a->stop[p] < stop) a->stop[p] = stop;
				return p;
			}
			for (k = a->count; k >= p + 1; k--) copy_edge(a, k - 1, k);
			break;
		}
		p = p + 1;
	}
	if (a->count >= KP_LOCAL_EDGES) kp_game_fail(g, "local search: local edge array full (0x488f00)");
	a->axis[p] = axis;
	a->start[p] = start;
	a->stop[p] = stop;
	a->dir[p] = dir;
	a->info[p] = info;
	a->count = a->count + 1;
	return p;
}

/* SearchEdgeAxis 0x48b720 (with flag 1). */
static int64_t first_from(const Edges *a, int64_t v)
{
	int64_t key, low = 0, high = a->count;
	if (a->count == 0) return 0;
	key = kp_s16v(v * 2 - 1);
	while (low < high) {
		int64_t mid = (low + high) / 2;
		if (key >= kp_s16v(a->axis[mid] * 2)) low = mid + 1;
		else high = mid;
	}
	return high;
}

/* CollectLocalTerrainContours 0x488600. keep_y: y_range, else x_range. */
static void terrain_side(KpGame *g, int d, int64_t from, int64_t limit, int keep_y)
{
	KpLocalGeometry *lg = &g->pathing.lg;
	Edges src = act(g, d);
	KpEdgeArray *dst = &lg->loc[d];
	const int64_t *e = lg->ext;
	int64_t cursor = 0, k;
	for (k = first_from(&src, from); k <= src.count - 1; k++) {
		int64_t s, t;
		int keep;
		if (src.axis[k] > limit) break;
		s = e[kp_band(src.info[k], 3)] + src.start[k];
		t = e[kp_band(kp_shrv(src.info[k], 2), 3)] + src.stop[k];
		keep = keep_y ? (s <= lg->bottom && t >= lg->top) : (s <= lg->right && t >= lg->left);
		if (keep) cursor = insert_edge(g, dst, cursor, src.axis[k], src.start[k], src.stop[k], src.dir[k], src.info[k]);
	}
}

static void collect_terrain(KpGame *g)
{
	KpLocalGeometry *lg = &g->pathing.lg;
	const int64_t *e = lg->ext;
	terrain_side(g, 0, lg->top - e[0] - 1, kp_s16v(lg->y - e[0] - 1), 0);
	terrain_side(g, 1, lg->x - e[1] + 1, kp_s16v(lg->right - e[1] + 1), 1);
	terrain_side(g, 2, lg->y - e[2] + 1, kp_s16v(lg->bottom - e[2] + 1), 0);
	terrain_side(g, 3, lg->left - e[3] - 1, kp_s16v(lg->x - e[3] - 1), 1);
}

/* IsPathObstacleUnit 0x42e490. */
int kp_path_obstacle(KpGame *g, int64_t c, int64_t mover)
{
	KpUnits *u = &g->u;
	int32_t s = kp_unit_sprite(g, c);
	int at_rest = u->move_x[c] == kp_s16v(g->w->spr.x[s]) && u->move_y[c] == kp_s16v(g->w->spr.y[s]);
	int64_t t;
	if (!(at_rest || c == kp_scalar(g, KP_PATH_SUBJECT) || kp_scalar(g, KP_PATH_ALL_UNITS) != 0)) return 0;
	t = u->type[c];
	if (kp_door_type(t)) return 0;
	if (kp_band(u->pathing_flags[c], 1) == 0 || kp_band(u->status[c], 0x200000) != 0) return 0;
	if (mover == 0) return 1;
	return c != mover && kp_units_collide(g, c, mover);
}

/* CanUnitsCollide 0x42ebf0. */
int kp_units_collide(KpGame *g, int64_t a, int64_t b)
{
	KpUnits *u = &g->u;
	kp_game_unit(g, a);
	kp_game_unit(g, b);
	if (kp_band(u->status[a], 0x400000) != 0 && kp_is_enemy(g, b, a)) return 0;
	if (u->type[b] == KP_TYPE_LARVA) {
		if (kp_band(u->status[a], 2) == 0) return 0;
	} else if (u->type[a] == KP_TYPE_LARVA) {
		return 0;
	}
	if (kp_band(u->status[b], 0x800000) == 0 || kp_band(u->status[a], 2) != 0) return 1;
	if (kp_band(u->status[a], 0x800000) == 0) return 0;
	if (u->order[b] == KP_ORD_RETURN_GAS) return 0;
	return u->order[a] == KP_ORD_WAIT_FOR_GAS;
}

/* spatial.search (inclusive): the first entry whose coordinate is >= v. */
static int64_t spatial_search(const int64_t *at, int64_t count, int64_t v)
{
	int64_t key = v * 2 - 1, low = 0, high = count, sum = count;
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

/* cross_at[i] of the spatial index (an index outside the arrays fails). */
static int64_t spatial_at(KpGame *g, const int64_t *cross_at, int64_t i)
{
	if (i < 0 || i >= KP_SPATIAL_CAPACITY) kp_game_fail(g, "local search: spatial entry %lld", (long long)i);
	return cross_at[i];
}

/* One side of AddLocalUnitEdges 0x488910. keep_y: y_range, else x_range. */
static void unit_side(KpGame *g, int d, const int64_t *at, const int64_t *units, int64_t from, int64_t limit,
	int inclusive_limit, const int64_t *field, const int64_t *cross_at, const int64_t *c_lo, const int64_t *c_hi,
	int keep_y)
{
	static const int64_t UNIT_EDGES[4][2] = { { 0, 0x3d }, { 1, 0x32 }, { 2, 0x3d }, { 3, 0x32 } };
	KpLocalGeometry *lg = &g->pathing.lg;
	KpEdgeArray *dst = &lg->loc[d];
	const int64_t *e = lg->ext;
	int64_t count = g->spatial.count, k;
	for (k = spatial_search(at, count, from); k <= count - 1; k++) {
		int64_t n;
		if ((inclusive_limit && at[k] > limit) || (!inclusive_limit && at[k] >= limit)) break;
		n = units[k];
		if (n >= 1 && n <= KP_UNITS && field[n] == k) { /* n outside 1..KP_UNITS matches no entry */
			int64_t start = kp_s16v(spatial_at(g, cross_at, c_lo[n])), stop = kp_s16v(spatial_at(g, cross_at, c_hi[n]));
			int keep = keep_y ? (kp_s16v(e[2] + start) <= lg->bottom && kp_s16v(e[0] + stop) >= lg->top)
							  : (kp_s16v(e[1] + start) <= lg->right && kp_s16v(e[3] + stop) >= lg->left);
			if (keep && (n == lg->target || kp_path_obstacle(g, n, lg->unit)))
				insert_edge(g, dst, 0, kp_s16v(at[k]), start, stop, UNIT_EDGES[d][0], UNIT_EDGES[d][1]);
		}
	}
}

static void collect_units(KpGame *g)
{
	KpLocalGeometry *lg = &g->pathing.lg;
	const KpSpatial *sp = &g->spatial;
	const KpUnits *u = &g->u;
	const int64_t *e = lg->ext;
	unit_side(g, 0, sp->y_at, sp->y_unit, lg->top - e[0] - 1, kp_s16v(lg->y - e[0]), 0, u->bound_bottom, sp->x_at,
		u->bound_left, u->bound_right, 0);
	unit_side(g, 1, sp->x_at, sp->x_unit, lg->x - e[1], kp_s16v(lg->right - e[1] + 1), 1, u->bound_left, sp->y_at,
		u->bound_top, u->bound_bottom, 1);
	unit_side(g, 2, sp->y_at, sp->y_unit, lg->y - e[2], kp_s16v(lg->bottom - e[2] + 1), 1, u->bound_top, sp->x_at,
		u->bound_left, u->bound_right, 0);
	unit_side(g, 3, sp->x_at, sp->x_unit, lg->left - e[3] - 1, kp_s16v(lg->x - e[3]), 0, u->bound_right, sp->y_at,
		u->bound_top, u->bound_bottom, 1);
}

/* The record k of array a overlaps [a1, a2] on the cross axis. */
static int overlaps(KpGame *g, const Edges *a, int64_t k, int64_t a1, int64_t a2)
{
	const int64_t *e = g->pathing.lg.ext;
	int64_t l = kp_s16v(lo_e(e, a->info, k) + a->start[k]);
	int64_t h = kp_s16v(hi_e(e, a->info, k) + a->stop[k]);
	if (l < a1) l = a1;
	if (h > a2) h = a2;
	return l <= h;
}

/* FindTopEdge 0x48c5e0 / FindLeftEdge 0x48c400 (before), FindRightEdge 0x48c4e0 / FindBottomEdge 0x48c300
 * (after): the record index or NONE. */
static int64_t edge_before(KpGame *g, int d, int64_t pos, int64_t a1, int64_t a2, int64_t limit)
{
	Edges a = act(g, d);
	int64_t e = g->pathing.lg.ext[d], key, low = 0, high = a.count, k;
	if (a.count == 0) return NONE;
	key = kp_s16v((pos - e) * 2 + 1);
	while (low < high) {
		int64_t mid = (low + high) / 2;
		if (key >= kp_s16v(a.axis[mid] * 2)) low = mid + 1;
		else high = mid;
	}
	limit = kp_s16v(limit - e);
	for (k = high - 1; k >= 0; k--) {
		if (a.axis[k] < limit) return NONE;
		if (overlaps(g, &a, k, a1, a2)) return k;
	}
	return NONE;
}

static int64_t edge_after(KpGame *g, int d, int64_t pos, int64_t a1, int64_t a2, int64_t limit)
{
	Edges a = act(g, d);
	int64_t e = g->pathing.lg.ext[d], low = 0, high = a.count, k;
	if (a.count != 0) {
		int64_t key = kp_s16v((pos - e) * 2 - 1);
		while (low < high) {
			int64_t mid = (low + high) / 2;
			if (key >= kp_s16v(a.axis[mid] * 2)) low = mid + 1;
			else high = mid;
		}
	}
	if (high == a.count) return NONE;
	limit = kp_s16v(limit - e);
	for (k = high; k <= a.count - 1; k++) {
		if (a.axis[k] > limit) return NONE;
		if (overlaps(g, &a, k, a1, a2)) return k;
	}
	return NONE;
}

static int64_t top_edge(KpGame *g, int64_t y, int64_t x1, int64_t x2, int64_t limit)
{
	return edge_before(g, 0, y, x1, x2, limit);
}
static int64_t right_edge(KpGame *g, int64_t x, int64_t y1, int64_t y2, int64_t limit)
{
	return edge_after(g, 1, x, y1, y2, limit);
}
static int64_t bottom_edge(KpGame *g, int64_t y, int64_t x1, int64_t x2, int64_t limit)
{
	return edge_after(g, 2, y, x1, x2, limit);
}
static int64_t left_edge(KpGame *g, int64_t x, int64_t y1, int64_t y2, int64_t limit)
{
	return edge_before(g, 3, x, y1, y2, limit);
}

/* AddPathNeighbor 0x488220. */
static void add(KpGame *g, int64_t x, int64_t y, int64_t flags)
{
	KpLocalGeometry *lg = &g->pathing.lg;
	int goal = 0;
	int64_t k;
	x = kp_s16v(x);
	y = kp_s16v(y);
	if (x == lg->x && y == lg->y) return;
	if (lg->target != 0) {
		int64_t l = lg->box[0], t = lg->box[1], r = lg->box[2], b = lg->box[3];
		if ((x == l || x == r) && y >= t && y <= b) goal = 1;
		else if ((y == t || y == b) && x >= l && x <= r) goal = 1;
	}
	if (!goal && x == lg->goal_x && y == lg->goal_y) goal = 1;
	if (goal) {
		flags = 0xff;
		lg->goal_found = 1;
	}
	if (lg->nb_count >= KP_LOCAL_MAX_NEIGHBORS) return;
	if (!goal) {
		for (k = 0; k <= lg->nb_count - 1; k++) {
			KpNeighbor *q = &lg->nb[k];
			if (q->x == x && q->y == y) {
				q->flags = kp_band(q->flags, flags);
				return;
			}
		}
	}
	lg->nb[lg->nb_count].x = x;
	lg->nb[lg->nb_count].y = y;
	lg->nb[lg->nb_count].flags = flags;
	lg->nb[lg->nb_count].goal = goal;
	lg->nb_count = lg->nb_count + 1;
}

/* CheckGoalInArea 0x489070. */
static void check_goal(KpGame *g, int64_t l, int64_t t, int64_t r, int64_t b)
{
	KpLocalGeometry *lg = &g->pathing.lg;
	if (l > r || t > b) return;
	if (lg->goal_x > r || lg->goal_x < l || lg->goal_y > b || lg->goal_y < t) return;
	if (lg->nb_count >= KP_LOCAL_MAX_NEIGHBORS) kp_game_fail(g, "local search: neighbour list overflow (0x489070)");
	lg->nb[lg->nb_count].x = lg->goal_x;
	lg->nb[lg->nb_count].y = lg->goal_y;
	lg->nb[lg->nb_count].flags = 0xff;
	lg->nb[lg->nb_count].goal = 1;
	lg->nb_count = lg->nb_count + 1;
	lg->goal_found = 1;
}

/* AppendVisitedArea 0x4890e0, after CheckGoalInArea. */
static void sweep(KpGame *g, int64_t l, int64_t t, int64_t r, int64_t b)
{
	KpLocalGeometry *lg = &g->pathing.lg;
	check_goal(g, l, t, r, b);
	if (lg->rect_count >= KP_LOCAL_MAX_RECTS) kp_game_fail(g, "local search: too many swept areas");
	lg->rects[lg->rect_count][0] = kp_s16v(l);
	lg->rects[lg->rect_count][1] = kp_s16v(t);
	lg->rects[lg->rect_count][2] = kp_s16v(r);
	lg->rects[lg->rect_count][3] = kp_s16v(b);
	lg->rect_count = lg->rect_count + 1;
}

/* MakeOrderedRect 0x48a7a0. */
static void sweep_points(KpGame *g, int64_t x1, int64_t y1, int64_t x2, int64_t y2)
{
	if (x1 > x2) {
		int64_t t = x1;
		x1 = x2;
		x2 = t;
	}
	if (y1 > y2) {
		int64_t t = y1;
		y1 = y2;
		y2 = t;
	}
	sweep(g, x1, y1, x2, y2);
}

/* ExpandEdgeOuter 0x489320: axis, start, end and the info byte of a record moved out by the widened extents. */
static void expand(KpGame *g, const Edges *a, int64_t k, int64_t *A, int64_t *S, int64_t *T, int64_t *info)
{
	*A = kp_s16v(a->axis[k] + E(g, 8 + a->dir[k]));
	*S = kp_s16v(a->start[k] + E(g, 8 + kp_band(a->info[k], 3)));
	*T = kp_s16v(a->stop[k] + E(g, 8 + kp_band(kp_shrv(a->info[k], 2), 3)));
	*info = a->info[k];
}

/* GenerateNeighborsNorthEast 0x489880. */
static void north_east(KpGame *g)
{
	KpLocalGeometry *lg = &g->pathing.lg;
	const int64_t *e = lg->ext;
	int64_t cx = lg->x, cy = lg->y, top = lg->top, right = lg->right;
	int64_t e_top = lg->edge[0], e_right = lg->edge[1];
	int64_t y_cur = cy, x_right = right, A = 0, S = 0, T = 0, info = 0, xlim, te_x, k, flags;
	if (cx < right) {
		int64_t ylim = top, re_y = top, rx = right;
		if (e_right != NONE) {
			Edges a = act(g, 1);
			re_y = kp_s16v(e[kp_band(a.info[e_right], 3)] - 1 + a.start[e_right]);
			if (re_y > top) ylim = re_y;
		}
		k = top_edge(g, kp_s16v(cy - 1), cx, right, kp_s16v(ylim - 1));
		flags = 0x3b;
		if (k != NONE) {
			Edges a = act(g, 0);
			expand(g, &a, k, &A, &S, &T, &info);
			ylim = A;
			if (right < T) flags = 0x22;
			else if (right == T) flags = kp_band(info, 0x20) != 0 ? 0x33 : 0;
		}
		if (e_right != NONE) {
			flags = kp_band(flags, 0xfd);
			if (re_y < ylim) flags = kp_band(flags, 0xde);
		}
		if (ylim < cy) add(g, right, cy, 0xe7);
		else flags = kp_bor(flags, 0x44);
		add(g, right, ylim, flags);
		if (ylim == top) add(g, cx, ylim, 0xdd);
		if (k != NONE) {
			if (right > T) add(g, T, A, 0x11);
			if (S < cx) {
				rx = cx;
			} else {
				Edges a = act(g, 0);
				for (;;) {
					rx = S;
					add(g, S, A, 0x18);
					k = k - 1;
					if (k < 0) break;
					expand(g, &a, k, &A, &S, &T, &info);
					if (A != ylim || T < cx) break;
					if (right >= T) add(g, T, A, 0x11);
					if (S < cx) {
						rx = cx;
						break;
					}
				}
			}
		}
		sweep_points(g, cx, ylim, right, cy);
		y_cur = ylim;
		x_right = rx;
		if (!(ylim > top)) return;
	} else if (!(cy > top)) {
		return;
	}
	xlim = x_right;
	te_x = x_right;
	if (e_top != NONE) {
		Edges a = act(g, 0);
		te_x = kp_s16v(e[kp_band(kp_shrv(a.info[e_top], 2), 3)] + 1 + a.stop[e_top]);
		if (te_x < x_right) xlim = te_x;
	}
	k = right_edge(g, kp_s16v(cx + 1), top, y_cur, kp_s16v(xlim + 1));
	flags = 0x3b;
	if (k != NONE) {
		Edges a = act(g, 1);
		expand(g, &a, k, &A, &S, &T, &info);
		xlim = A;
		if (top > S) flags = 0x18;
		else if (top == S) flags = kp_band(info, 0x10) != 0 ? 0x39 : 0;
	}
	if (e_top != NONE) {
		flags = kp_band(flags, 0xf7);
		if (te_x > xlim) flags = kp_band(flags, 0xde);
	}
	if (xlim > cx) add(g, cx, top, 0xdd);
	else flags = kp_bor(flags, 0x84);
	add(g, xlim, top, flags);
	if (xlim < x_right) add(g, xlim, y_cur, 0x01);
	if (k != NONE) {
		Edges a = act(g, 1);
		if (top < S) add(g, A, S, 0x21);
		while (T <= y_cur) {
			add(g, A, T, 0x22);
			k = k + 1;
			if (k >= a.count) break;
			expand(g, &a, k, &A, &S, &T, &info);
			if (A != xlim || S > y_cur) {
				add(g, xlim, y_cur, 0x21);
				break;
			}
			if (top <= S) add(g, A, S, 0x21);
		}
	}
	sweep_points(g, cx, top, xlim, y_cur);
}

/* GenerateNeighborsSouthEast 0x48a240. */
static void south_east(KpGame *g)
{
	KpLocalGeometry *lg = &g->pathing.lg;
	const int64_t *e = lg->ext;
	int64_t cx = lg->x, cy = lg->y, bottom = lg->bottom, right = lg->right;
	int64_t e_bottom = lg->edge[2], e_right = lg->edge[1];
	int64_t y_cur = cy, x_right = right, A = 0, S = 0, T = 0, info = 0, xlim, te_x, k, flags;
	if (cx < right) {
		int64_t ylim = bottom, re_y = bottom, x_after = right;
		if (e_right != NONE) {
			Edges a = act(g, 1);
			re_y = kp_s16v(e[kp_band(kp_shrv(a.info[e_right], 2), 3)] + 1 + a.stop[e_right]);
			if (re_y < bottom) ylim = re_y;
		}
		k = bottom_edge(g, kp_s16v(cy + 1), cx, right, kp_s16v(ylim + 1));
		flags = 0x67;
		if (k != NONE) {
			Edges a = act(g, 2);
			expand(g, &a, k, &A, &S, &T, &info);
			if (cx < S) add(g, S, A, 0x44);
			ylim = A;
			x_after = S;
			while (T < right) {
				add(g, T, A, 0x42);
				k = k + 1;
				if (k >= a.count) break;
				if (E(g, a.dir[k]) + a.axis[k] - 1 != ylim) break;
				if (e[kp_band(a.info[k], 3)] + a.start[k] - 1 >= right) break;
				expand(g, &a, k, &A, &S, &T, &info);
				if (right >= S) add(g, S, A, 0x44);
			}
			if (right < T) flags = 0x21;
			else if (right == T) flags = kp_band(info, 0x20) != 0 ? 0x63 : 0;
		}
		if (e_right != NONE) {
			flags = kp_band(flags, 0xfe);
			if (re_y > ylim) flags = kp_band(flags, 0xdd);
		}
		if (ylim > cy) add(g, right, cy, 0xbb);
		else flags = kp_bor(flags, 0x18);
		add(g, right, ylim, flags);
		if (ylim == bottom) add(g, cx, ylim, 0xde);
		sweep_points(g, cx, cy, right, ylim);
		x_right = x_after;
		y_cur = ylim;
		if (ylim >= bottom) return;
	} else if (cy >= bottom) {
		return;
	}
	xlim = x_right;
	te_x = x_right;
	if (e_bottom != NONE) {
		Edges a = act(g, 2);
		te_x = kp_s16v(e[kp_band(kp_shrv(a.info[e_bottom], 2), 3)] + 1 + a.stop[e_bottom]);
		if (te_x < x_right) xlim = te_x;
	}
	k = right_edge(g, kp_s16v(cx + 1), y_cur, bottom, kp_s16v(xlim + 1));
	flags = 0x67;
	if (k != NONE) {
		Edges a = act(g, 1);
		expand(g, &a, k, &A, &S, &T, &info);
		if (A < x_right) add(g, A, y_cur, 0x02);
		if (S > y_cur) add(g, A, S, 0x21);
		xlim = A;
		while (T < bottom) {
			add(g, A, T, 0x22);
			k = k + 1;
			if (k >= a.count) break;
			if (E(g, a.dir[k]) + a.axis[k] - 1 != xlim) break;
			if (e[kp_band(a.info[k], 3)] + a.start[k] - 1 >= bottom) break;
			expand(g, &a, k, &A, &S, &T, &info);
			if (y_cur <= S && S <= bottom) add(g, A, S, 0x21);
		}
		if (bottom < T) flags = 0x44;
		else if (bottom == T) flags = kp_band(info, 0x20) != 0 ? 0x66 : 0;
	}
	if (e_bottom != NONE) {
		flags = kp_band(flags, 0xfb);
		if (te_x > xlim) flags = kp_band(flags, 0xbd);
	}
	if (xlim > cx) add(g, cx, bottom, 0xde);
	else flags = kp_bor(flags, 0x88);
	add(g, xlim, bottom, flags);
	sweep_points(g, cx, y_cur, xlim, bottom);
}

/* GenerateNeighborsSouthWest 0x489d50. */
static void south_west(KpGame *g)
{
	KpLocalGeometry *lg = &g->pathing.lg;
	const int64_t *e = lg->ext;
	int64_t cx = lg->x, cy = lg->y, bottom = lg->bottom, left = lg->left;
	int64_t e_bottom = lg->edge[2], e_left = lg->edge[3];
	int64_t y_cur = cy, x_left = left, A = 0, S = 0, T = 0, info = 0, xlim, te, k, flags;
	if (cx > left) {
		int64_t ylim = bottom, le_y = bottom, rx = left;
		if (e_left != NONE) {
			Edges a = act(g, 3);
			le_y = kp_s16v(e[kp_band(kp_shrv(a.info[e_left], 2), 3)] + 1 + a.stop[e_left]);
			if (le_y < bottom) ylim = le_y;
		}
		k = bottom_edge(g, kp_s16v(cy + 1), left, cx, kp_s16v(ylim + 1));
		flags = 0xce;
		if (k != NONE) {
			Edges a = act(g, 2);
			expand(g, &a, k, &A, &S, &T, &info);
			ylim = A;
			if (left > S) flags = 0x88;
			else if (left == S) flags = kp_band(info, 0x10) != 0 ? 0xcc : 0;
		}
		if (e_left != NONE) {
			flags = kp_band(flags, 0xf7);
			if (le_y > ylim) flags = kp_band(flags, 0x7b);
		}
		if (ylim > cy) add(g, left, cy, 0xbd);
		else flags = kp_bor(flags, 0x11);
		add(g, left, ylim, flags);
		if (ylim == bottom) add(g, cx, ylim, 0x77);
		if (k != NONE) {
			Edges a = act(g, 2);
			int done = 0;
			if (left < S) add(g, S, A, 0x44);
			while (T <= cx) {
				add(g, T, A, 0x42);
				rx = T;
				k = k + 1;
				if (k >= a.count) {
					done = 1;
					break;
				}
				expand(g, &a, k, &A, &S, &T, &info);
				if (A != ylim || S > cx) {
					done = 1;
					break;
				}
				add(g, S, A, 0x44);
			}
			if (!done) {
				add(g, cx, A, 0x21);
				rx = cx;
			}
		}
		sweep_points(g, left, cy, cx, ylim);
		y_cur = ylim;
		x_left = rx;
		if (ylim >= bottom) return;
	} else if (cy >= bottom) {
		return;
	}
	xlim = x_left;
	te = x_left;
	if (e_bottom != NONE) {
		Edges a = act(g, 2);
		te = kp_s16v(e[kp_band(a.info[e_bottom], 3)] - 1 + a.start[e_bottom]);
		if (te > x_left) xlim = te;
	}
	k = left_edge(g, kp_s16v(cx - 1), y_cur, bottom, kp_s16v(xlim - 1));
	flags = 0xce;
	if (k != NONE) {
		Edges a = act(g, 3);
		expand(g, &a, k, &A, &S, &T, &info);
		xlim = A;
		if (bottom < T) flags = 0x42;
		else if (bottom == T) flags = kp_band(info, 0x20) != 0 ? 0xc6 : 0;
	}
	if (e_bottom != NONE) {
		flags = kp_band(flags, 0xfd);
		if (te < xlim) flags = kp_band(flags, 0xbb);
	}
	if (xlim < cx) add(g, cx, bottom, 0x77);
	else flags = kp_bor(flags, 0x22);
	add(g, xlim, bottom, flags);
	if (xlim > x_left) add(g, xlim, y_cur, 0x04);
	if (k != NONE) {
		Edges a = act(g, 3);
		if (bottom > T) add(g, A, T, 0x84);
		while (S >= y_cur) {
			add(g, A, S, 0x88);
			k = k - 1;
			if (k < 0) break;
			expand(g, &a, k, &A, &S, &T, &info);
			if (A != xlim || T < y_cur) {
				add(g, xlim, y_cur, 0x84);
				break;
			}
			if (bottom >= T) add(g, A, T, 0x84);
		}
	}
	sweep_points(g, xlim, y_cur, cx, bottom);
}

/* GenerateNeighborsNorthWest 0x489360. */
static void north_west(KpGame *g)
{
	KpLocalGeometry *lg = &g->pathing.lg;
	const int64_t *e = lg->ext;
	int64_t cx = lg->x, cy = lg->y, top = lg->top, left = lg->left;
	int64_t e_top = lg->edge[0], e_left = lg->edge[3];
	int64_t y_cur = cy, x_left = left, A = 0, S = 0, T = 0, info = 0, xlim, te, k, flags;
	if (cx > left) {
		int64_t ylim = top, le = top, x_after = left;
		if (e_left != NONE) {
			Edges a = act(g, 3);
			le = kp_s16v(e[kp_band(a.info[e_left], 3)] - 1 + a.start[e_left]);
			if (le > top) ylim = le;
		}
		k = top_edge(g, kp_s16v(cy - 1), left, cx, kp_s16v(ylim - 1));
		flags = 0x9d;
		if (k != NONE) {
			Edges a = act(g, 0);
			expand(g, &a, k, &A, &S, &T, &info);
			if (cx > T) add(g, T, A, 0x11);
			else add(g, cx, A, 0x22);
			ylim = A;
			x_after = T;
			while (left < S) {
				add(g, S, A, 0x18);
				k = k - 1;
				if (k < 0) break;
				if (E(g, a.dir[k]) + a.axis[k] + 1 != ylim) break;
				if (e[kp_band(kp_shrv(a.info[k], 2), 3)] + a.stop[k] + 1 <= left) break;
				expand(g, &a, k, &A, &S, &T, &info);
				if (left <= T) add(g, T, A, 0x11);
			}
			if (left > S) flags = 0x84;
			else if (left == S) flags = kp_band(info, 0x10) != 0 ? 0x9c : 0;
		}
		if (e_left != NONE) {
			flags = kp_band(flags, 0xfb);
			if (le < ylim) flags = kp_band(flags, 0x77);
		}
		if (ylim < cy) add(g, left, cy, 0xee);
		else flags = kp_bor(flags, 0x44);
		add(g, left, ylim, flags);
		if (ylim == top) add(g, cx, ylim, 0x7b);
		sweep_points(g, left, ylim, cx, cy);
		y_cur = ylim;
		x_left = x_after;
		if (ylim <= top) return;
	} else if (cy <= top) {
		return;
	}
	xlim = x_left;
	te = x_left;
	if (e_top != NONE) {
		Edges a = act(g, 0);
		te = kp_s16v(e[kp_band(a.info[e_top], 3)] - 1 + a.start[e_top]);
		if (te > x_left) xlim = te;
	}
	k = left_edge(g, kp_s16v(cx - 1), top, y_cur, kp_s16v(xlim - 1));
	flags = 0x9d;
	if (k != NONE) {
		Edges a = act(g, 3);
		expand(g, &a, k, &A, &S, &T, &info);
		if (A > x_left) add(g, A, y_cur, 0x08);
		if (T < y_cur) add(g, A, T, 0x84);
		xlim = A;
		while (S > top) {
			add(g, A, S, 0x88);
			k = k - 1;
			if (k < 0) break;
			if (E(g, a.dir[k]) + a.axis[k] + 1 != xlim) break;
			if (e[kp_band(kp_shrv(a.info[k], 2), 3)] + a.stop[k] + 1 <= top) break;
			expand(g, &a, k, &A, &S, &T, &info);
			if (top <= T) add(g, A, T, 0x84);
		}
		if (top > S) flags = 0x11;
		else if (top == S) flags = kp_band(info, 0x10) != 0 ? 0x99 : 0;
	}
	if (e_top != NONE) {
		flags = kp_band(flags, 0xfe);
		if (te < xlim) flags = kp_band(flags, 0xe7);
	}
	if (xlim < cx) add(g, cx, top, 0x7b);
	else flags = kp_bor(flags, 0x21);
	add(g, xlim, top, flags);
	sweep_points(g, xlim, top, cx, y_cur);
}

/* CollectLocalPathGeometry 0x488320 at (x, y) for the open directions `mask`. */
static void collect(KpGame *g, int64_t x, int64_t y, int64_t mask)
{
	KpLocalGeometry *lg = &g->pathing.lg;
	const int64_t *e = lg->ext;
	int64_t map_w = g->map_w_px, map_h = g->map_h_px, k, top, right, bottom, left;
	int d;
	lg->x = x;
	lg->y = y;
	lg->nb_count = 0;
	lg->rect_count = 0;
	lg->top = kp_s16v(y - KP_LOCAL_WINDOW);
	lg->right = kp_s16v(x + KP_LOCAL_WINDOW);
	lg->left = kp_s16v(x - KP_LOCAL_WINDOW);
	lg->bottom = kp_s16v(y + KP_LOCAL_WINDOW);
	if (lg->top < 0) lg->top = 0;
	if (lg->left < 0) lg->left = 0;
	if (lg->bottom >= map_h) lg->bottom = map_h - 1;
	if (lg->right >= map_w) lg->right = map_w - 1;
	if (kp_band(mask, 0x19) == 0) lg->top = y;
	if (kp_band(mask, 0x23) == 0) lg->right = x;
	if (kp_band(mask, 0x46) == 0) lg->bottom = y;
	if (kp_band(mask, 0x8c) == 0) lg->left = x;
	collect_terrain(g);
	collect_units(g);
	for (d = 0; d <= 3; d++) lg->active_local[d] = 1;
	k = top_edge(g, y, x, x, kp_s16v(lg->top - 1));
	lg->edge[0] = k;
	if (k != NONE) lg->top = kp_s16v(e[0] + 1 + lg->loc[0].axis[k]);
	k = right_edge(g, x, y, y, kp_s16v(lg->right + 1));
	lg->edge[1] = k;
	if (k != NONE) lg->right = kp_s16v(e[1] - 1 + lg->loc[1].axis[k]);
	k = bottom_edge(g, y, x, x, kp_s16v(lg->bottom + 1));
	lg->edge[2] = k;
	if (k != NONE) lg->bottom = kp_s16v(e[2] - 1 + lg->loc[2].axis[k]);
	k = left_edge(g, x, y, y, kp_s16v(lg->left - 1));
	lg->edge[3] = k;
	if (k != NONE) lg->left = kp_s16v(e[3] + 1 + lg->loc[3].axis[k]);
	top = lg->top;
	right = lg->right;
	bottom = lg->bottom;
	left = lg->left;
	if (top == y && right == x) mask = kp_band(mask, 0xfe);
	if (bottom == y) {
		if (right == x) mask = kp_band(mask, 0xfd);
		if (left == x) mask = kp_band(mask, 0xfb);
	}
	if (top == y) {
		if (left == x) mask = kp_band(mask, 0xf7);
		mask = kp_band(mask, 0xef);
	}
	if (right == x) mask = kp_band(mask, 0xdf);
	if (bottom == y) mask = kp_band(mask, 0xbf);
	if (left == x) mask = kp_band(mask, 0x7f);
	if (kp_band(mask, DIR_N) != 0) {
		mask = kp_bor(mask, 9);
		add(g, x, top, lg->edge[0] != NONE ? 0xa6 : 0xbf);
	}
	if (kp_band(mask, DIR_E) != 0) {
		mask = kp_bor(mask, 3);
		add(g, right, y, lg->edge[1] != NONE ? 0x5c : 0x7f);
	}
	if (kp_band(mask, DIR_S) != 0) {
		mask = kp_bor(mask, 6);
		add(g, x, bottom, lg->edge[2] != NONE ? 0xa9 : 0xef);
	}
	if (kp_band(mask, DIR_W) != 0) {
		mask = kp_bor(mask, 0xc);
		add(g, left, y, lg->edge[3] != NONE ? 0x53 : 0xdf);
	}
	lg->mask = mask;
}

/* --- the swept areas (0x69ec08: column starts; 0x69ee10: column -> interval list; 0x69f018: the lists) --- */

static int64_t xs_get(KpGame *g, int64_t i)
{
	const KpVisited *v = &g->pathing.visited;
	if (i < 0 || i > v->xs_hw) kp_game_fail(g, "local search: swept column %lld", (long long)i);
	return v->xs[i];
}

static void xs_set(KpGame *g, int64_t i, int64_t x)
{
	KpVisited *v = &g->pathing.visited;
	if (i < 0 || i >= KP_LOCAL_MAX_COLUMNS + 2) kp_game_fail(g, "local search: swept column %lld", (long long)i);
	v->xs[i] = x;
	if (i > v->xs_hw) v->xs_hw = i;
}

static int64_t colmap_get(KpGame *g, int64_t i)
{
	const KpVisited *v = &g->pathing.visited;
	if (i < 0 || i > v->colmap_hw) kp_game_fail(g, "local search: swept column map %lld", (long long)i);
	return v->colmap[i];
}

static void colmap_set(KpGame *g, int64_t i, int64_t slot)
{
	KpVisited *v = &g->pathing.visited;
	if (i < 0 || i >= KP_LOCAL_MAX_COLUMNS + 2) kp_game_fail(g, "local search: swept column map %lld", (long long)i);
	v->colmap[i] = slot;
	if (i > v->colmap_hw) v->colmap_hw = i;
}

/* v.lists[slot] (a list never made fails). */
static KpIntervals *list_of(KpGame *g, int64_t slot)
{
	KpVisited *v = &g->pathing.visited;
	if (slot < 0 || slot > v->lists_hw) kp_game_fail(g, "local search: swept list %lld", (long long)slot);
	return &v->lists[slot];
}

/* A new, empty list at slot. */
static KpIntervals *list_new(KpGame *g, int64_t slot)
{
	KpVisited *v = &g->pathing.visited;
	if (slot < 0 || slot > KP_LOCAL_MAX_COLUMNS) kp_game_fail(g, "local search: swept list %lld", (long long)slot);
	if (slot > v->lists_hw) v->lists_hw = slot;
	v->lists[slot].count = 0;
	return &v->lists[slot];
}

static void visited_reset(KpGame *g)
{
	KpVisited *v = &g->pathing.visited;
	v->count = 1;
	xs_set(g, 0, 0);
	xs_set(g, 1, g->map_w_px);
	colmap_set(g, 0, 0);
	colmap_set(g, 1, -1);
	list_new(g, 0);
}

static int64_t column_of(KpGame *g, int64_t x)
{
	int64_t c = 0;
	while (x >= xs_get(g, c + 1)) c = c + 1;
	return c;
}

/* A copy of list `slot` as the new list v.count. Only the list's entries are copied: the new list's entries past
 * them keep what they held (a whole-record copy would bring indeterminate stack bytes into the game, and so into
 * its saves). */
static void clone_list(KpGame *g, int64_t slot)
{
	KpIntervals *src = list_of(g, slot), *dst;
	int64_t i, count = src->count;
	if (slot == g->pathing.visited.count) return; /* the list is its own copy */
	dst = list_new(g, g->pathing.visited.count);
	dst->count = count;
	for (i = 0; i <= count - 1; i++) {
		dst->y0[i] = src->y0[i];
		dst->y1[i] = src->y1[i];
	}
}

/* Shifts the column entries c..count one up. */
static void open_column(KpGame *g, int64_t c)
{
	int64_t i;
	for (i = g->pathing.visited.count; i >= c; i--) {
		xs_set(g, i + 1, xs_get(g, i));
		colmap_set(g, i + 1, colmap_get(g, i));
	}
}

/* AddVisitedArea 0x492de0: rectangle {left, top, right, bottom}; 0 when full. */
static int visited_add(KpGame *g, const int64_t *r)
{
	KpVisited *v = &g->pathing.visited;
	int64_t x0 = r[0], y0 = r[1], x1 = r[2], y1 = r[3], c = column_of(g, x0);
	if (xs_get(g, c) < x0) {
		int64_t slot;
		if (v->count == KP_LOCAL_MAX_COLUMNS) return 0;
		slot = colmap_get(g, c);
		open_column(g, c);
		colmap_set(g, c, v->count);
		clone_list(g, slot);
		v->count = v->count + 1;
		c = c + 1;
		xs_set(g, c, x0);
	}
	if (x0 > x1) return 1;
	for (;;) {
		KpIntervals *list = list_of(g, colmap_get(g, c));
		int64_t n, i, k;
		if (x1 < xs_get(g, c + 1) - 1) {
			int64_t slot;
			if (v->count == KP_LOCAL_MAX_COLUMNS) return 0;
			slot = colmap_get(g, c);
			open_column(g, c);
			colmap_set(g, c + 1, v->count);
			clone_list(g, slot);
			v->count = v->count + 1;
			xs_set(g, c + 1, x1 + 1);
		}
		n = list->count;
		i = 0;
		k = n;
		while (k > 0 && y0 > list->y1[i] + 1) {
			k = k - 1;
			i = i + 1;
		}
		if (k == 0) {
			if (n == KP_LOCAL_MAX_INTERVALS) return 0;
			list->y0[i] = y0;
			list->y1[i] = y1;
			list->count = n + 1;
		} else if (y1 < list->y0[i] - 1 || y0 > list->y1[i] + 1) {
			int64_t j;
			if (n == KP_LOCAL_MAX_INTERVALS) return 0;
			for (j = n; j >= i + 1; j--) {
				list->y0[j] = list->y0[j - 1];
				list->y1[j] = list->y1[j - 1];
			}
			list->y0[i] = y0;
			list->y1[i] = y1;
			list->count = n + 1;
		} else {
			int64_t j = 0, t, rounds;
			if (list->y0[i] > y0) list->y0[i] = y0;
			if (list->y1[i] < y1) list->y1[i] = y1;
			/* Coalesce over the whole list, from its first interval. */
			rounds = list->count - 1;
			for (t = 1; t <= rounds; t++) {
				if (list->y0[j + 1] - 1 <= list->y1[j]) {
					int64_t m;
					if (list->y1[j] < list->y1[j + 1]) list->y1[j] = list->y1[j + 1];
					for (m = j + 1; m <= list->count - 2; m++) {
						list->y0[m] = list->y0[m + 1];
						list->y1[m] = list->y1[m + 1];
					}
					list->count = list->count - 1;
				} else {
					j = j + 1;
				}
			}
		}
		c = c + 1;
		if (xs_get(g, c) > x1) return 1;
	}
}

/* The first interval of `list` that ends at or after y, or NONE. */
static int64_t interval_at(const KpIntervals *list, int64_t y)
{
	int64_t i;
	for (i = 0; i <= list->count - 1; i++) {
		if (y <= list->y1[i]) return i;
	}
	return NONE;
}

/* FilterFlagsByVisitedAreas 0x4930b0. */
static int64_t visited_filter(KpGame *g, int64_t x, int64_t y, int64_t flags)
{
	const KpVisited *v = &g->pathing.visited;
	int64_t c = column_of(g, x), i, m;
	const KpIntervals *list = list_of(g, colmap_get(g, c));
	if (list->count == 0) return flags;
	i = interval_at(list, y);
	if (i == NONE) return flags;
	m = list->y0[i] >= y ? 0x19 : 0;
	if (list->y1[i] <= y) m = kp_bor(m, 0x46);
	if (xs_get(g, c) == x) {
		m = kp_bor(m, 0x8c);
		if (c > 0) {
			const KpIntervals *l2 = list_of(g, colmap_get(g, c - 1));
			int64_t j = l2->count > 0 ? interval_at(l2, y) : NONE;
			if (j != NONE && l2->y0[j] <= y && l2->y1[j] >= y) {
				m = kp_band(m, 0x7f);
				if (l2->y0[j] < y) m = kp_band(m, 0xf7);
				if (l2->y1[j] > y) m = kp_band(m, 0xfb);
			}
		}
	}
	if (xs_get(g, c + 1) - 1 == x) {
		m = kp_bor(m, 0x23);
		if (c + 1 < v->count) {
			const KpIntervals *l2 = list_of(g, colmap_get(g, c + 1));
			if (l2->count > 0) {
				int64_t j = interval_at(l2, y);
				if (j == NONE) return kp_band(flags, m);
				if (l2->y0[j] <= y && l2->y1[j] >= y) {
					m = kp_band(m, 0xdf);
					if (l2->y0[j] < y) m = kp_band(m, 0xfe);
					if (l2->y1[j] > y) m = kp_band(m, 0xfd);
				}
			}
		}
	}
	return kp_band(flags, m);
}

/* ExpandLocalNode 0x491ba0. */
static void expand_node(KpGame *g, int64_t node)
{
	KpLocalGeometry *lg = &g->pathing.lg;
	const KpLocalNode *nd = &g->pathing.lnodes[node];
	int64_t k;
	int d;
	for (d = 0; d <= 3; d++) lg->active_local[d] = 0; /* RestoreGlobalContours 0x489110 */
	for (d = 0; d <= 3; d++) lg->loc[d].count = 0;    /* ResetLocalEdges 0x488ee0 */
	lg->goal_found = 0;
	collect(g, nd->x, nd->y, nd->flags);
	/* GenerateLocalNeighbors 0x48a750. */
	if (kp_band(lg->mask, DIR_NE) != 0) north_east(g);
	if (kp_band(lg->mask, DIR_SE) != 0) south_east(g);
	if (kp_band(lg->mask, DIR_SW) != 0) south_west(g);
	if (kp_band(lg->mask, DIR_NW) != 0) north_west(g);
	for (k = 0; k <= lg->rect_count - 1; k++) visited_add(g, lg->rects[k]);
}

/* StoreLocalPathPoints 0x492860: chain[0] is the goal, chain[n - 1] the source. */
static void store_points(KpRoute *c, const int64_t *cx, const int64_t *cy, int64_t n)
{
	int64_t k, j;
	c->point_index = 0;
	if (n < 3) {
		if (n == 2) {
			c->point_x[0] = cx[0];
			c->point_y[0] = cy[0];
		}
		c->point_count = n - 1;
		return;
	}
	k = 0;
	j = n - 2; /* chain[j] is the node after the source */
	while (j >= 1 && k <= 0x30) {
		c->point_x[k] = cx[j];
		c->point_y[k] = cy[j];
		k = k + 1;
		j = j - 1;
	}
	c->point_x[k] = cx[0];
	c->point_y[k] = cy[0];
	c->point_count = k + 1;
}

static KpHeap local_heap(KpPathing *p)
{
	KpHeap h;
	h.slots = p->lheap;
	h.count = &p->lheap_count;
	h.base = (char *)(void *)p->lnodes;
	h.size = sizeof(KpLocalNode);
	h.offset = offsetof(KpLocalNode, prio);
	h.sentinel = SENTINEL;
	h.capacity = KP_LOCAL_OPEN_LIMIT + 1;
	return h;
}

/* The first node at a position (the original finds it with a scan of the arena). */
static int64_t hash_slot(const KpPathing *p, int64_t x, int64_t y)
{
	uint32_t h = ((uint32_t)x * 73856093u) ^ ((uint32_t)y * 19349663u);
	int64_t i = (int64_t)(h & (KP_LOCAL_HASH - 1));
	while (p->hash_node[i] != NONE && (p->hash_x[i] != x || p->hash_y[i] != y)) i = (i + 1) & (KP_LOCAL_HASH - 1);
	return i;
}

/* The cost of a step into `region`. */
static int64_t step_cost(KpGame *g, int64_t cost, int scale_other, int64_t region, int64_t next_region, int64_t src_region)
{
	if (scale_other && region != next_region && region != src_region) cost = kp_idiv64(g, kp_s32v(kp_shlv(cost, 9)), 256);
	return cost;
}

/* LocalPathSearch 0x491f50 on search context c towards (gx, gy), the segment leading into region `next_region`
 * (KP_LOCAL_FINAL for the goal). */
int kp_local_path_search(KpGame *g, KpRoute *c, int64_t next_region, int64_t gx, int64_t gy)
{
	KpPathing *p = &g->pathing;
	KpRegionGraph *rg = &p->rg;
	KpLocalGeometry *lg = &p->lg;
	KpLocalNode *nodes = p->lnodes;
	KpHeap open = local_heap(p);
	int final = gx == c->goal_x && gy == c->goal_y, scale_other = 0, goal_hit = 0, done = 0, reached_goal = 0;
	int finished = 0, d;
	int64_t src_region, goal_region, around, near_count, list_at, gx8, gy8, k, goal_count = 0, near_open = 0;
	int64_t best = NONE, cur;
	if (next_region < KP_LOCAL_REGION_IDS)
		scale_other = !(next_region >= 0 && next_region < rg->count) || rg->klass[next_region] != 0x1ffd;
	c->local_nodes = 0;
	c->local_peak = 0;
	src_region = c->src_region;
	goal_region = kp_region_at(g, c->goal_x, c->goal_y);
	around = next_region < KP_LOCAL_REGION_IDS ? next_region : goal_region;
	kp_rg_check(g, around);
	list_at = rg->nb_at[around];
	near_count = rg->ground[around];
	if (near_count > rg->nb_count[around]) kp_game_fail(g, "local search: region %lld ground neighbours", (long long)around);
	for (k = 0; k <= near_count - 1; k++) {
		if (rg->neighbors[list_at + k] != src_region) mark_set(g, rg->neighbors[list_at + k], 1);
	}
	gx8 = kp_shlv(gx, 8);
	gy8 = kp_shlv(gy, 8);
	for (d = 0; d <= 3; d++) lg->active_local[d] = 0; /* 0x488e10 */
	kp_set_extents(g, g->u.type[kp_game_unit(g, c->unit)], lg->ext); /* 0x4891c0 */
	/* SetSearchSubjects 0x4892d0. */
	lg->unit = c->unit;
	lg->goal_x = gx;
	lg->goal_y = gy;
	lg->target = c->target;
	lg->box[0] = c->box_left;
	lg->box[1] = c->box_top;
	lg->box[2] = c->box_right;
	lg->box[3] = c->box_bottom;
	visited_reset(g);
	p->lheap_count = 0;
	nodes[SENTINEL].prio = -1;
	nodes[0].x = c->src_x;
	nodes[0].y = c->src_y;
	nodes[0].region = src_region;
	nodes[0].near = 0;
	nodes[0].in_next = 0;
	nodes[0].goal = 0;
	nodes[0].closed = 0;
	nodes[0].flags = 0xff;
	nodes[0].depth = 0;
	nodes[0].heur = KP_LOCAL_START_COST;
	nodes[0].cost = 0;
	nodes[0].prio = KP_LOCAL_START_COST;
	nodes[0].parent = NONE;
	p->lnode_count = 0;
	for (k = 0; k < KP_LOCAL_HASH; k++) p->hash_node[k] = NONE;
	kp_heap_insert(g, &open, 0);
	for (;;) {
		KpLocalNode *cn;
		int64_t used;
		cur = kp_heap_pop(&open);
		cn = &nodes[cur];
		if (cn->goal != 0) {
			reached_goal = 1;
			best = cur;
			break;
		}
		if (cn->flags != 0) cn->flags = visited_filter(g, cn->x, cn->y, cn->flags);
		used = p->lnode_count + 1; /* nodes in use, the start node included */
		if (goal_count > 0 && !goal_hit && (!final || used >= KP_LOCAL_GOAL_NODES)) done = 1;
		if (near_open != 0 && !goal_hit && used >= KP_LOCAL_NEAR_NODES) done = 1;
		if (cn->in_next != 0 && done) {
			best = cur;
			break;
		}
		cn->closed = 1;
		if (cn->in_next != 0) goal_count = goal_count - 1;
		if (cn->near != 0) {
			near_open = near_open - 1;
			if (done) {
				finished = 1;
				break;
			}
		}
		if (!goal_hit && !done && cn->flags != 0) {
			expand_node(g, cur);
			for (k = 0; k <= lg->nb_count - 1; k++) {
				KpNeighbor *q = &lg->nb[k];
				int64_t region = kp_region_at(g, q->x, q->y), qflags, slot, node;
				int in_next = region == next_region, near = mark_get(g, region) != 0;
				if (q->goal != 0) {
					goal_hit = 1;
				} else if (q->flags != 0) {
					q->flags = visited_filter(g, q->x, q->y, q->flags);
					if (q->flags == 0) continue;
				}
				qflags = q->flags;
				slot = hash_slot(p, q->x, q->y);
				node = p->hash_node[slot];
				if (node != NONE) {
					KpLocalNode *nd = &nodes[node];
					if (cn->parent != node) {
						int64_t cost = approx(kp_shlv(q->x, 8), kp_shlv(q->y, 8), kp_shlv(cn->x, 8), kp_shlv(cn->y, 8)), nw;
						cost = step_cost(g, cost, scale_other, region, next_region, src_region);
						nw = kp_s32v(cn->cost + cost);
						if (nw < nd->cost) {
							int64_t prio;
							nd->parent = cur;
							nd->depth = (cn->depth + 1) % 256;
							nd->cost = nw;
							prio = kp_s32v(nd->heur + nw);
							if (nd->closed == 0) {
								kp_heap_change(&open, node, prio);
							} else {
								nd->prio = prio;
								nd->flags = qflags;
								nd->closed = 0;
								nd->goal = q->goal;
								kp_heap_insert(g, &open, node);
								if (in_next) goal_count = goal_count + 1;
								if (near) near_open = near_open + 1;
							}
							if (p->lheap_count == KP_LOCAL_OPEN_LIMIT) break;
						} else if (nd->closed == 0) {
							nd->flags = kp_band(nd->flags, qflags);
						}
					}
				} else {
					KpLocalNode *nd;
					int64_t cost;
					if (p->lnode_count >= KP_LOCAL_ARENA) kp_game_fail(g, "local search: node arena full");
					node = p->lnode_count + 1;
					p->lnode_count = node;
					nd = &nodes[node];
					nd->x = q->x;
					nd->y = q->y;
					nd->parent = cur;
					nd->region = region;
					nd->flags = qflags;
					nd->depth = (cn->depth + 1) % 256;
					p->hash_x[slot] = q->x;
					p->hash_y[slot] = q->y;
					p->hash_node[slot] = node;
					cost = approx(kp_shlv(cn->x, 8), kp_shlv(cn->y, 8), kp_shlv(q->x, 8), kp_shlv(q->y, 8));
					cost = step_cost(g, cost, scale_other, region, next_region, src_region);
					nd->heur = approx(kp_shlv(q->x, 8), kp_shlv(q->y, 8), gx8, gy8);
					nd->cost = kp_s32v(cn->cost + cost);
					nd->prio = kp_s32v(nd->heur + nd->cost);
					nd->in_next = in_next;
					nd->goal = q->goal;
					nd->near = near;
					nd->closed = qflags == 0 ? 1 : 0;
					if (nd->closed == 0 || q->goal != 0) {
						nd->closed = 0;
						kp_heap_insert(g, &open, node);
						if (in_next) goal_count = goal_count + 1;
						if (near) near_open = near_open + 1;
					}
					if (p->lheap_count == KP_LOCAL_OPEN_LIMIT || p->lnode_count == KP_LOCAL_ARENA) break;
				}
			}
			if (c->local_peak < p->lheap_count) c->local_peak = p->lheap_count;
			if ((p->lheap_count == KP_LOCAL_OPEN_LIMIT || p->lnode_count == KP_LOCAL_ARENA) && !goal_hit
				&& goal_count == 0) {
				finished = 1;
				break;
			}
		}
		if (p->lheap_count == 0) {
			finished = 1;
			break;
		}
	}
	if (finished) {
		/* The search ended without reaching the goal: choose the node to head for. */
		int64_t reach = 0, near_hits = 0, i;
		int reachable;
		c->local_partial = 1;
		for (i = 1; i <= p->lnode_count; i++) {
			KpLocalNode *n = &nodes[i];
			int64_t m = mark_get(g, n->region);
			if (m != 0) mark_set(g, n->region, m + 1);
			if (n->closed == 0) {
				int64_t f = n->flags;
				if (f != 0) f = visited_filter(g, n->x, n->y, f);
				if (f == 0) {
					n->flags = 0;
					n->closed = 1;
					if (n->in_next != 0) goal_count = goal_count - 1;
				} else {
					n->flags = f;
					reach = reach + 1;
					if (n->region == goal_region) near_hits = near_hits + 1;
				}
			}
		}
		if (final) {
			reach = near_hits;
			for (k = 0; k <= near_count - 1; k++) {
				int64_t id = rg->neighbors[list_at + k];
				if (id != src_region) {
					int64_t m = mark_get(g, id);
					if (m < 2) {
						reach = reach + 1;
						break;
					}
					reach = reach - kp_idiv64(g, m, 2);
					if (reach < 0) reach = 0;
				}
			}
		}
		reachable = reach == 0 && goal_count == 0;
		nodes[0].heur = approx(kp_shlv(nodes[0].x, 8), kp_shlv(nodes[0].y, 8), gx8, gy8);
		best = 0;
		if (reachable) {
			int64_t limit = goal_count != 0 ? KP_LOCAL_START_COST : nodes[0].heur;
			for (i = 1; i <= p->lnode_count; i++) {
				const KpLocalNode *n = &nodes[i];
				if (n->heur < limit) {
					int ok;
					if (final) ok = 1;
					else if (goal_count != 0) ok = n->region == goal_region;
					else ok = n->region == src_region;
					if (ok) {
						best = i;
						limit = n->heur;
					}
				}
			}
			c->reached = 1;
			c->reached_end = 1;
		} else {
			int64_t limit = KP_LOCAL_START_COST;
			for (i = 1; i <= p->lnode_count; i++) {
				const KpLocalNode *n = &nodes[i];
				int64_t score;
				if (n->region == goal_region || n->region == next_region) score = kp_idiv64(g, n->cost, 2) + n->heur;
				else if (mark_get(g, n->region) != 0) score = kp_idiv64(g, n->heur * 3, 2);
				else if (n->region == src_region) score = n->heur * 2;
				else score = n->heur * 4;
				if (n->closed != 0) score = kp_shlv(score, 5);
				if (score < limit) {
					best = i;
					limit = score;
				}
			}
		}
	} else if (reached_goal) {
		c->reached = 1;
	}
	c->local_nodes = p->lnode_count + 1;
	if (best != NONE) {
		int64_t chain_x[KP_LOCAL_MAX_CHAIN], chain_y[KP_LOCAL_MAX_CHAIN], n = 0, i = best;
		while (i != NONE && n < KP_LOCAL_MAX_CHAIN) {
			chain_x[n] = nodes[i].x;
			chain_y[n] = nodes[i].y;
			n = n + 1;
			i = nodes[i].parent;
		}
		store_points(c, chain_x, chain_y, n);
	}
	for (k = 0; k <= near_count - 1; k++) {
		if (rg->neighbors[list_at + k] != src_region) mark_set(g, rg->neighbors[list_at + k], 0);
	}
	return c->point_count > 0;
}
