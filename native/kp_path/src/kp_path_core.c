/*
 * kp_path: the local path search (LocalPathSearch 0x491f50; atlas P30,
 * docs/research/native-local-paths.md) in C, for the time budget of plan section 10 (D19).
 *
 * This is a line-by-line port of game/pathing/local_search.lua, which stays the reference:
 * every value is computed with Lua's integer semantics (64-bit integers here, with the explicit
 * 16- and 32-bit wraps of game/simulation/int.lua where the Lua code has them), and the scan
 * orders, tie rules and the heap are the same. The game state stays in the Lua tables; one call
 * reads what it needs from them:
 *   - the terrain contours, copied once per contour table (they are static during a mission;
 *     a change must bump `contours.version`, TODO: dynamic contours of phase 6);
 *   - the spatial index, copied at the start of each call;
 *   - the fields of the units met, read once per unit and call;
 *   - the region map from the mission's RGNS string.
 * The search scratch (nodes, heap, local edges, swept areas) is static, as the original's.
 * The results go into the search context table as the Lua code writes them.
 *
 * kp_path_register(L) registers the module table `kp_path` with search(game, context,
 * next_region, gx, gy) -> boolean.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "kp_path.h"

typedef int64_t i64;

#define WINDOW 0x40
#define LOCAL_EDGES 0x80
#define MAX_NEIGHBORS 0x20
#define ARENA 249
#define OPEN_LIMIT 0x96
#define GOAL_NODES 150
#define NEAR_NODES 200
#define MAX_CHAIN 0x80
#define MAX_COLUMNS 0x80
#define MAX_INTERVALS 10
#define START_COST 0x400000
#define REGION_IDS 0x9c4
#define MAX_RECTS 64
#define MAX_UNITS 1701
#define HEAP_SIZE 1024
#define NONE (-1)

#define DIR_NE 0x01
#define DIR_SE 0x02
#define DIR_SW 0x04
#define DIR_NW 0x08
#define DIR_N 0x10
#define DIR_E 0x20
#define DIR_S 0x40
#define DIR_W 0x80

/* --- Lua integer semantics (game/simulation/int.lua) --- */

static i64 s16(i64 v) { return (int16_t)(uint16_t)(v & 0xffff); }
static i64 s32(i64 v) { return (int32_t)(uint32_t)(v & 0xffffffff); }
static i64 band(i64 a, i64 b) { return (i64)((uint32_t)a & (uint32_t)b); }
static i64 bor(i64 a, i64 b) { return (i64)((uint32_t)a | (uint32_t)b); }
static i64 lmod(i64 a, i64 m) {
	i64 r = a % m;
	return r < 0 ? r + m : r;
}
static i64 shl(i64 x, int n) { return lmod(x, (i64)1 << (32 - n)) << n; }
static i64 shr(i64 x, int n) { return lmod(x, (i64)1 << 32) >> n; }
static i64 sar(i64 x, int n) { return s32(x) >> n; } /* floor division, as math.floor */
static i64 idiv(lua_State *L, i64 a, i64 b) {
	if (b == 0) luaL_error(L, "integer division by zero");
	return a / b; /* C truncates toward zero, as int.idiv */
}
static i64 iabs(i64 v) { return v < 0 ? -v : v; }

/* ApproxDistance 0x405cf0 (game/vision/detection.lua). */
static i64 approx(i64 x1, i64 y1, i64 x2, i64 y2) {
	i64 big = iabs(x1 - x2), small = iabs(y1 - y2);
	if (big < small) {
		i64 t = big;
		big = small;
		small = t;
	}
	if (sar(big, 2) < small)
		big = big - sar(big, 4) + sar(small * 3, 3) - sar(big, 6) + sar(small * 3, 8);
	return big;
}

/* --- Lua table access --- */

static i64 geti(lua_State *L, int t, i64 k) {
	lua_rawgeti(L, t, (int)k);
	i64 v = (i64)lua_tonumber(L, -1);
	lua_pop(L, 1);
	return v;
}

/* geti that tells a missing value (nil) apart. */
static int geti_opt(lua_State *L, int t, i64 k, i64 *out) {
	lua_rawgeti(L, t, (int)k);
	int ok = !lua_isnil(L, -1);
	*out = ok ? (i64)lua_tonumber(L, -1) : 0;
	lua_pop(L, 1);
	return ok;
}

static void seti(lua_State *L, int t, const char *k, i64 v) {
	lua_pushnumber(L, (lua_Number)v);
	lua_setfield(L, t, k);
}

static i64 getf(lua_State *L, int t, const char *k) {
	lua_getfield(L, t, k);
	i64 v = (i64)lua_tonumber(L, -1);
	lua_pop(L, 1);
	return v;
}

/* Pushes t[k] and returns its absolute stack index; it must be a table. */
static int push_table(lua_State *L, int t, const char *k) {
	lua_getfield(L, t, k);
	if (!lua_istable(L, -1)) luaL_error(L, "kp_path: field %s is not a table", k);
	return lua_gettop(L);
}

/* --- Edge arrays --- */

typedef struct {
	int count, cap;
	int *axis, *start, *stop, *dir, *info;
} Edges;

static void edges_reserve(Edges *a, int cap) {
	if (a->cap >= cap) return;
	a->axis = (int *)realloc(a->axis, sizeof(int) * cap);
	a->start = (int *)realloc(a->start, sizeof(int) * cap);
	a->stop = (int *)realloc(a->stop, sizeof(int) * cap);
	a->dir = (int *)realloc(a->dir, sizeof(int) * cap);
	a->info = (int *)realloc(a->info, sizeof(int) * cap);
	a->cap = cap;
}

static void copy_edge(Edges *a, int k, Edges *b, int j) {
	b->axis[j] = a->axis[k];
	b->start[j] = a->start[k];
	b->stop[j] = a->stop[k];
	b->dir[j] = a->dir[k];
	b->info[j] = a->info[k];
}

/* --- State --- */

typedef struct {
	int x, y, flags, goal;
} Neighbor;

typedef struct {
	int x, y, parent, region, flags, depth, in_next, goal, near, closed;
	i64 heur, cost, prio;
} Node;

typedef struct {
	int count;
	int y0[MAX_INTERVALS + 1], y1[MAX_INTERVALS + 1];
} Intervals;

typedef struct {
	int count;
	int xs[MAX_COLUMNS + 2], colmap[MAX_COLUMNS + 2];
	Intervals lists[MAX_COLUMNS + 1];
} Visited;

typedef struct {
	lua_State *L;
	/* Lua stack indices of the tables read during the call */
	int u_sprite, u_move_x, u_move_y, u_type, u_pathing, u_status, u_owner, u_order;
	int u_bound[4]; /* left, right, top, bottom */
	int spr_x, spr_y, spr_owner, alliance;
	i64 path_subject, path_all_units;
	/* the spatial index */
	int sp_count;
	i64 *x_at, *x_unit, *y_at, *y_unit;
	int sp_cap;
	/* per-unit memo of this call */
	int stamp;
	int memo_stamp[MAX_UNITS], memo_obstacle_stamp[MAX_UNITS];
	i64 memo_bound[MAX_UNITS][4];
	char memo_obstacle[MAX_UNITS];
	/* regions */
	const unsigned char *rgns;
	size_t rgns_len;
	i64 rgns_width, rgns_height, raster_at, splits_at;
	int *mark;
	int mark_cap;
	/* the geometry context 0x69d930 */
	Edges *active[4], *saved[4];
	Edges loc[4];
	Edges contours[4];
	const void *contour_key[4];
	i64 contour_version;
	int ext[12];
	int x, y, top, right, bottom, left;
	int edge[4];
	Neighbor nb[MAX_NEIGHBORS];
	int nb_count;
	int rects[MAX_RECTS][4];
	int rect_count;
	int unit, target, goal_x, goal_y, box[4], goal_found, mask;
	int map_w, map_h;
	Visited v;
	/* the search */
	Node nodes[ARENA + 2]; /* 0: the start node; 1..ARENA: the arena; ARENA + 1: the sentinel */
	int node_count;        /* nodes after the start node (Lua's #nodes) */
	int heap[HEAP_SIZE];
	int heap_count;
	int hash_key_x[512], hash_key_y[512], hash_node[512];
} State;

static State S;

/* --- Units --- */

static int unit_check(State *s, i64 n) {
	if (n < 0 || n >= MAX_UNITS) luaL_error(s->L, "kp_path: unit %d out of range", (int)n);
	return (int)n;
}

/* The four bound positions of unit n in the spatial arrays (+0x13c..+0x148). */
static i64 bound_of(State *s, i64 n, int which) {
	int i = unit_check(s, n);
	if (s->memo_stamp[i] != s->stamp) {
		for (int b = 0; b < 4; b++) s->memo_bound[i][b] = geti(s->L, s->u_bound[b], i);
		s->memo_stamp[i] = s->stamp;
	}
	return s->memo_bound[i][which];
}

/* IsEnemyUnit 0x4290e0: player 11's units count as their sprite's owner. */
static int is_enemy(State *s, i64 n, i64 target) {
	lua_State *L = s->L;
	i64 owner = geti(L, s->u_owner, target);
	if (owner == 11) owner = geti(L, s->spr_owner, geti(L, s->u_sprite, target));
	lua_rawgeti(L, s->alliance, (int)geti(L, s->u_owner, n));
	if (!lua_istable(L, -1)) {
		lua_pop(L, 1);
		return 0;
	}
	i64 v;
	int ok = geti_opt(L, lua_gettop(L), owner, &v);
	lua_pop(L, 1);
	return ok && v == 0;
}

/* CanUnitsCollide 0x42ebf0. */
static int units_collide(State *s, i64 a, i64 b) {
	lua_State *L = s->L;
	i64 sa = geti(L, s->u_status, a), sb = geti(L, s->u_status, b);
	if (band(sa, 0x400000) != 0 && is_enemy(s, b, a)) return 0;
	if (geti(L, s->u_type, b) == 0x23) {
		if (band(sa, 2) == 0) return 0;
	} else if (geti(L, s->u_type, a) == 0x23) {
		return 0;
	}
	if (band(sb, 0x800000) == 0 || band(sa, 2) != 0) return 1;
	if (band(sa, 0x800000) == 0) return 0;
	if (geti(L, s->u_order, b) == 0x54) return 0;
	return geti(L, s->u_order, a) == 0x52;
}

/* IsPathObstacleUnit 0x42e490 against the search's unit (memoized for the call). */
static int path_obstacle(State *s, i64 c, i64 mover) {
	lua_State *L = s->L;
	int i = unit_check(s, c);
	if (s->memo_obstacle_stamp[i] == s->stamp) return s->memo_obstacle[i];
	int result;
	i64 sp = geti(L, s->u_sprite, c);
	int at_rest = geti(L, s->u_move_x, c) == s16(geti(L, s->spr_x, sp)) &&
	              geti(L, s->u_move_y, c) == s16(geti(L, s->spr_y, sp));
	i64 t = geti(L, s->u_type, c);
	if (!(at_rest || c == s->path_subject || s->path_all_units != 0)) {
		result = 0;
	} else if (t >= 0xcd && t <= 0xd0) {
		result = 0;
	} else if (band(geti(L, s->u_pathing, c), 1) == 0 || band(geti(L, s->u_status, c), 0x200000) != 0) {
		result = 0;
	} else if (mover == 0) {
		result = 1;
	} else {
		result = c != mover && units_collide(s, c, mover);
	}
	s->memo_obstacle_stamp[i] = s->stamp;
	s->memo_obstacle[i] = (char)result;
	return result;
}

/* --- Regions --- */

static i64 rgns_u16(State *s, i64 at) {
	if (at < 0 || (size_t)(at + 2) > s->rgns_len) luaL_error(s->L, "kp_path: RGNS read out of range");
	return s->rgns[at] + s->rgns[at + 1] * 256;
}

/* The region of map point (x, y) (G:region_at, game/units/placement.lua). */
static int region_at(State *s, i64 x, i64 y) {
	i64 tx = sar(x, 5), ty = sar(y, 5);
	if (tx < 0 || ty < 0 || tx >= s->rgns_width || ty >= s->rgns_height)
		luaL_error(s->L, "kp_path: region of a point outside the map");
	i64 v = rgns_u16(s, s->raster_at + 2 * (ty * s->rgns_width + tx));
	if (v < 0x2000) return (int)v;
	i64 at = s->splits_at + (v - 0x2000) * 6;
	i64 mask = rgns_u16(s, at);
	int bit = (int)(band(sar(y, 3), 3) * 4 + band(sar(x, 3), 3));
	if (band(mask, (i64)1 << bit) != 0) return (int)rgns_u16(s, at + 4);
	return (int)rgns_u16(s, at + 2);
}

static int *mark_at(State *s, int region) {
	if (region < 0 || region >= s->mark_cap) luaL_error(s->L, "kp_path: region %d out of range", region);
	return &s->mark[region];
}

/* --- Local edges --- */

static int lo_ext(State *s, Edges *a, int k) { return s->ext[band(a->info[k], 3)]; }
static int hi_ext(State *s, Edges *a, int k) { return s->ext[band(shr(a->info[k], 2), 3)]; }

/* InsertLocalEdge 0x488f00. */
static int insert_edge(State *s, Edges *a, int cursor, int axis, int start, int stop, int dir, int info) {
	int *e = s->ext;
	int rlo = e[band(info, 3)] + start;
	int p = cursor;
	while (p < a->count) {
		if (a->axis[p] > axis || (a->axis[p] == axis && hi_ext(s, a, p) + a->stop[p] >= rlo)) {
			if (a->axis[p] == axis && hi_ext(s, a, p) + 1 + a->stop[p] >= rlo &&
			    lo_ext(s, a, p) - 1 + a->start[p] <= e[band(shr(info, 2), 3)] + stop) {
				if (a->start[p] > start) a->start[p] = start;
				if (a->stop[p] < stop) a->stop[p] = stop;
				return p;
			}
			for (int k = a->count; k >= p + 1; k--) copy_edge(a, k - 1, a, k);
			break;
		}
		p++;
	}
	if (a->count >= LOCAL_EDGES) luaL_error(s->L, "local search: local edge array full (0x488f00)");
	a->axis[p] = axis;
	a->start[p] = start;
	a->stop[p] = stop;
	a->dir[p] = dir;
	a->info[p] = info;
	a->count++;
	return p;
}

/* SearchEdgeAxis 0x48b720 (flag 1): the first record whose axis is >= v. */
static int first_from(Edges *a, i64 v) {
	if (a->count == 0) return 0;
	i64 key = s16(v * 2 - 1);
	int low = 0, high = a->count;
	while (low < high) {
		int mid = (low + high) / 2;
		if (key >= s16((i64)a->axis[mid] * 2)) low = mid + 1;
		else high = mid;
	}
	return high;
}

static int x_range(State *s, i64 a, i64 b) { return a <= s->right && b >= s->left; }
static int y_range(State *s, i64 a, i64 b) { return a <= s->bottom && b >= s->top; }

/* CollectLocalTerrainContours 0x488600. */
static void terrain_side(State *s, int d, i64 from, i64 limit, int (*keep)(State *, i64, i64)) {
	Edges *src = s->active[d], *dst = &s->loc[d];
	int *e = s->ext;
	int cursor = 0;
	for (int k = first_from(src, from); k < src->count; k++) {
		if (src->axis[k] > limit) break;
		if (keep(s, e[band(src->info[k], 3)] + src->start[k], e[band(shr(src->info[k], 2), 3)] + src->stop[k]))
			cursor = insert_edge(s, dst, cursor, src->axis[k], src->start[k], src->stop[k], src->dir[k], src->info[k]);
	}
}

static void collect_terrain(State *s) {
	int *e = s->ext;
	terrain_side(s, 0, s->top - e[0] - 1, s16(s->y - e[0] - 1), x_range);
	terrain_side(s, 1, s->x - e[1] + 1, s16(s->right - e[1] + 1), y_range);
	terrain_side(s, 2, s->y - e[2] + 1, s16(s->bottom - e[2] + 1), x_range);
	terrain_side(s, 3, s->left - e[3] - 1, s16(s->x - e[3] - 1), y_range);
}

/* The lower bound of the spatial index (game/units/spatial.lua search, inclusive). */
static int spatial_search(const i64 *at, int count, i64 v) {
	i64 key = v * 2 - 1;
	int low = 0, high = count, sum = count;
	while (low < high) {
		int mid = sum / 2;
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

static int unit_x_range(State *s, i64 a, i64 b) {
	return s16(s->ext[1] + a) <= s->right && s16(s->ext[3] + b) >= s->left;
}
static int unit_y_range(State *s, i64 a, i64 b) {
	return s16(s->ext[2] + a) <= s->bottom && s16(s->ext[0] + b) >= s->top;
}

/* bound indices: 0 left, 1 right, 2 top, 3 bottom */
static void unit_side(State *s, int d, const i64 *at, const i64 *units, i64 from, i64 limit, int inclusive,
                      int field, const i64 *cross_at, int c_lo, int c_hi, int (*keep)(State *, i64, i64)) {
	static const int unit_edges[4][2] = { { 0, 0x3d }, { 1, 0x32 }, { 2, 0x3d }, { 3, 0x32 } };
	Edges *dst = &s->loc[d];
	int count = s->sp_count;
	for (int k = spatial_search(at, count, from); k < count; k++) {
		if (inclusive ? at[k] > limit : at[k] >= limit) break;
		i64 n = units[k];
		if (bound_of(s, n, field) == k) {
			i64 lo = bound_of(s, n, c_lo), hi = bound_of(s, n, c_hi);
			if (lo < 0 || lo >= count || hi < 0 || hi >= count) luaL_error(s->L, "kp_path: unit bound out of range");
			i64 start = s16(cross_at[lo]), stop = s16(cross_at[hi]);
			if (keep(s, start, stop) && (n == s->target || path_obstacle(s, n, s->unit)))
				insert_edge(s, dst, 0, (int)s16(at[k]), (int)start, (int)stop, unit_edges[d][0], unit_edges[d][1]);
		}
	}
}

/* AddLocalUnitEdges 0x488910. */
static void collect_units(State *s) {
	int *e = s->ext;
	unit_side(s, 0, s->y_at, s->y_unit, s->top - e[0] - 1, s16(s->y - e[0]), 0, 3, s->x_at, 0, 1, unit_x_range);
	unit_side(s, 1, s->x_at, s->x_unit, s->x - e[1], s16(s->right - e[1] + 1), 1, 0, s->y_at, 2, 3, unit_y_range);
	unit_side(s, 2, s->y_at, s->y_unit, s->y - e[2], s16(s->bottom - e[2] + 1), 1, 2, s->x_at, 0, 1, unit_x_range);
	unit_side(s, 3, s->x_at, s->x_unit, s->left - e[3] - 1, s16(s->x - e[3]), 0, 1, s->y_at, 2, 3, unit_y_range);
}

static int overlaps(State *s, Edges *a, int k, i64 a1, i64 a2) {
	i64 l = s16(lo_ext(s, a, k) + a->start[k]);
	i64 h = s16(hi_ext(s, a, k) + a->stop[k]);
	if (l < a1) l = a1;
	if (h > a2) h = a2;
	return l <= h;
}

/* FindTopEdge 0x48c5e0 / FindLeftEdge 0x48c400 (before), FindRightEdge 0x48c4e0 /
 * FindBottomEdge 0x48c300 (after): the record index or NONE. */
static int edge_before(State *s, int d, i64 pos, i64 a1, i64 a2, i64 limit) {
	Edges *a = s->active[d];
	int e = s->ext[d];
	if (a->count == 0) return NONE;
	i64 key = s16((pos - e) * 2 + 1);
	int low = 0, high = a->count;
	while (low < high) {
		int mid = (low + high) / 2;
		if (key >= s16((i64)a->axis[mid] * 2)) low = mid + 1;
		else high = mid;
	}
	limit = s16(limit - e);
	for (int k = high - 1; k >= 0; k--) {
		if (a->axis[k] < limit) return NONE;
		if (overlaps(s, a, k, a1, a2)) return k;
	}
	return NONE;
}

static int edge_after(State *s, int d, i64 pos, i64 a1, i64 a2, i64 limit) {
	Edges *a = s->active[d];
	int e = s->ext[d];
	int low = 0, high = a->count;
	if (a->count != 0) {
		i64 key = s16((pos - e) * 2 - 1);
		while (low < high) {
			int mid = (low + high) / 2;
			if (key >= s16((i64)a->axis[mid] * 2)) low = mid + 1;
			else high = mid;
		}
	}
	if (high == a->count) return NONE;
	limit = s16(limit - e);
	for (int k = high; k < a->count; k++) {
		if (a->axis[k] > limit) return NONE;
		if (overlaps(s, a, k, a1, a2)) return k;
	}
	return NONE;
}

static int top_edge(State *s, i64 y, i64 x1, i64 x2, i64 limit) { return edge_before(s, 0, y, x1, x2, limit); }
static int right_edge(State *s, i64 x, i64 y1, i64 y2, i64 limit) { return edge_after(s, 1, x, y1, y2, limit); }
static int bottom_edge(State *s, i64 y, i64 x1, i64 x2, i64 limit) { return edge_after(s, 2, y, x1, x2, limit); }
static int left_edge(State *s, i64 x, i64 y1, i64 y2, i64 limit) { return edge_before(s, 3, x, y1, y2, limit); }

/* AddPathNeighbor 0x488220. */
static void add(State *s, i64 x64, i64 y64, i64 flags) {
	int x = (int)s16(x64), y = (int)s16(y64);
	if (x == s->x && y == s->y) return;
	int goal = 0;
	if (s->target != 0) {
		int l = s->box[0], t = s->box[1], r = s->box[2], b = s->box[3];
		if ((x == l || x == r) && y >= t && y <= b) goal = 1;
		else if ((y == t || y == b) && x >= l && x <= r) goal = 1;
	}
	if (!goal && x == s->goal_x && y == s->goal_y) goal = 1;
	if (goal) {
		flags = 0xff;
		s->goal_found = 1;
	}
	if (s->nb_count >= MAX_NEIGHBORS) return;
	if (!goal) {
		for (int k = 0; k < s->nb_count; k++) {
			Neighbor *q = &s->nb[k];
			if (q->x == x && q->y == y) {
				q->flags = (int)band(q->flags, flags);
				return;
			}
		}
	}
	Neighbor *q = &s->nb[s->nb_count++];
	q->x = x;
	q->y = y;
	q->flags = (int)flags;
	q->goal = goal;
}

/* CheckGoalInArea 0x489070. */
static void check_goal(State *s, i64 l, i64 t, i64 r, i64 b) {
	if (l > r || t > b) return;
	if (s->goal_x > r || s->goal_x < l || s->goal_y > b || s->goal_y < t) return;
	if (s->nb_count >= MAX_NEIGHBORS) luaL_error(s->L, "local search: neighbour list overflow (0x489070)");
	Neighbor *q = &s->nb[s->nb_count++];
	q->x = s->goal_x;
	q->y = s->goal_y;
	q->flags = 0xff;
	q->goal = 1;
	s->goal_found = 1;
}

/* AppendVisitedArea 0x4890e0, after CheckGoalInArea. */
static void sweep(State *s, i64 l, i64 t, i64 r, i64 b) {
	check_goal(s, l, t, r, b);
	if (s->rect_count >= MAX_RECTS) luaL_error(s->L, "kp_path: too many swept areas");
	int *rect = s->rects[s->rect_count++];
	rect[0] = (int)s16(l);
	rect[1] = (int)s16(t);
	rect[2] = (int)s16(r);
	rect[3] = (int)s16(b);
}

/* MakeOrderedRect 0x48a7a0. */
static void sweep_points(State *s, i64 x1, i64 y1, i64 x2, i64 y2) {
	if (x1 > x2) {
		i64 t = x1;
		x1 = x2;
		x2 = t;
	}
	if (y1 > y2) {
		i64 t = y1;
		y1 = y2;
		y2 = t;
	}
	sweep(s, x1, y1, x2, y2);
}

/* ExpandEdgeOuter 0x489320. */
static void expand(State *s, Edges *a, int k, i64 *A, i64 *S_, i64 *T, i64 *info) {
	int *w = s->ext;
	*A = s16(a->axis[k] + w[8 + a->dir[k]]);
	*S_ = s16(a->start[k] + w[8 + band(a->info[k], 3)]);
	*T = s16(a->stop[k] + w[8 + band(shr(a->info[k], 2), 3)]);
	*info = a->info[k];
}

/* GenerateNeighborsNorthEast 0x489880. */
static void north_east(State *s) {
	int *e = s->ext;
	i64 cx = s->x, cy = s->y, top = s->top, right = s->right;
	int e_top = s->edge[0], e_right = s->edge[1];
	i64 y_cur = cy, x_right = right;
	i64 A = 0, S_ = 0, T = 0, info = 0;
	if (cx < right) {
		i64 ylim = top, re_y = top;
		if (e_right != NONE) {
			Edges *a = s->active[1];
			re_y = s16(e[band(a->info[e_right], 3)] - 1 + a->start[e_right]);
			if (re_y > top) ylim = re_y;
		}
		i64 rx = right;
		int k = top_edge(s, s16(cy - 1), cx, right, s16(ylim - 1));
		i64 flags = 0x3b;
		if (k != NONE) {
			expand(s, s->active[0], k, &A, &S_, &T, &info);
			ylim = A;
			if (right < T) flags = 0x22;
			else if (right == T) flags = band(info, 0x20) != 0 ? 0x33 : 0;
		}
		if (e_right != NONE) {
			flags = band(flags, 0xfd);
			if (re_y < ylim) flags = band(flags, 0xde);
		}
		if (ylim < cy) add(s, right, cy, 0xe7);
		else flags = bor(flags, 0x44);
		add(s, right, ylim, flags);
		if (ylim == top) add(s, cx, ylim, 0xdd);
		if (k != NONE) {
			if (right > T) add(s, T, A, 0x11);
			if (S_ < cx) {
				rx = cx;
			} else {
				for (;;) {
					rx = S_;
					add(s, S_, A, 0x18);
					k = k - 1;
					if (k < 0) break;
					expand(s, s->active[0], k, &A, &S_, &T, &info);
					if (A != ylim || T < cx) break;
					if (right >= T) add(s, T, A, 0x11);
					if (S_ < cx) {
						rx = cx;
						break;
					}
				}
			}
		}
		sweep_points(s, cx, ylim, right, cy);
		y_cur = ylim;
		x_right = rx;
		if (!(ylim > top)) return;
	} else if (!(cy > top)) {
		return;
	}
	i64 xlim = x_right, te_x = x_right;
	if (e_top != NONE) {
		Edges *a = s->active[0];
		te_x = s16(e[band(shr(a->info[e_top], 2), 3)] + 1 + a->stop[e_top]);
		if (te_x < x_right) xlim = te_x;
	}
	int k = right_edge(s, s16(cx + 1), top, y_cur, s16(xlim + 1));
	i64 flags = 0x3b;
	if (k != NONE) {
		expand(s, s->active[1], k, &A, &S_, &T, &info);
		xlim = A;
		if (top > S_) flags = 0x18;
		else if (top == S_) flags = band(info, 0x10) != 0 ? 0x39 : 0;
	}
	if (e_top != NONE) {
		flags = band(flags, 0xf7);
		if (te_x > xlim) flags = band(flags, 0xde);
	}
	if (xlim > cx) add(s, cx, top, 0xdd);
	else flags = bor(flags, 0x84);
	add(s, xlim, top, flags);
	if (xlim < x_right) add(s, xlim, y_cur, 0x01);
	if (k != NONE) {
		if (top < S_) add(s, A, S_, 0x21);
		Edges *a = s->active[1];
		while (T <= y_cur) {
			add(s, A, T, 0x22);
			k = k + 1;
			if (k >= a->count) break;
			expand(s, a, k, &A, &S_, &T, &info);
			if (A != xlim || S_ > y_cur) {
				add(s, xlim, y_cur, 0x21);
				break;
			}
			if (top <= S_) add(s, A, S_, 0x21);
		}
	}
	sweep_points(s, cx, top, xlim, y_cur);
}

/* GenerateNeighborsSouthEast 0x48a240. */
static void south_east(State *s) {
	int *e = s->ext;
	i64 cx = s->x, cy = s->y, bottom = s->bottom, right = s->right;
	int e_bottom = s->edge[2], e_right = s->edge[1];
	i64 y_cur = cy, x_right = right;
	i64 A = 0, S_ = 0, T = 0, info = 0;
	if (cx < right) {
		i64 ylim = bottom, re_y = bottom;
		if (e_right != NONE) {
			Edges *a = s->active[1];
			re_y = s16(e[band(shr(a->info[e_right], 2), 3)] + 1 + a->stop[e_right]);
			if (re_y < bottom) ylim = re_y;
		}
		i64 x_after = right;
		int k = bottom_edge(s, s16(cy + 1), cx, right, s16(ylim + 1));
		i64 flags = 0x67;
		if (k != NONE) {
			Edges *a = s->active[2];
			expand(s, a, k, &A, &S_, &T, &info);
			if (cx < S_) add(s, S_, A, 0x44);
			ylim = A;
			x_after = S_;
			while (T < right) {
				add(s, T, A, 0x42);
				k = k + 1;
				if (k >= a->count) break;
				if (e[a->dir[k]] + a->axis[k] - 1 != ylim) break;
				if (e[band(a->info[k], 3)] + a->start[k] - 1 >= right) break;
				expand(s, a, k, &A, &S_, &T, &info);
				if (right >= S_) add(s, S_, A, 0x44);
			}
			if (right < T) flags = 0x21;
			else if (right == T) flags = band(info, 0x20) != 0 ? 0x63 : 0;
		}
		if (e_right != NONE) {
			flags = band(flags, 0xfe);
			if (re_y > ylim) flags = band(flags, 0xdd);
		}
		if (ylim > cy) add(s, right, cy, 0xbb);
		else flags = bor(flags, 0x18);
		add(s, right, ylim, flags);
		if (ylim == bottom) add(s, cx, ylim, 0xde);
		sweep_points(s, cx, cy, right, ylim);
		x_right = x_after;
		y_cur = ylim;
		if (ylim >= bottom) return;
	} else if (cy >= bottom) {
		return;
	}
	i64 xlim = x_right, te_x = x_right;
	if (e_bottom != NONE) {
		Edges *a = s->active[2];
		te_x = s16(e[band(shr(a->info[e_bottom], 2), 3)] + 1 + a->stop[e_bottom]);
		if (te_x < x_right) xlim = te_x;
	}
	int k = right_edge(s, s16(cx + 1), y_cur, bottom, s16(xlim + 1));
	i64 flags = 0x67;
	if (k != NONE) {
		Edges *a = s->active[1];
		expand(s, a, k, &A, &S_, &T, &info);
		if (A < x_right) add(s, A, y_cur, 0x02);
		if (S_ > y_cur) add(s, A, S_, 0x21);
		xlim = A;
		while (T < bottom) {
			add(s, A, T, 0x22);
			k = k + 1;
			if (k >= a->count) break;
			if (e[a->dir[k]] + a->axis[k] - 1 != xlim) break;
			if (e[band(a->info[k], 3)] + a->start[k] - 1 >= bottom) break;
			expand(s, a, k, &A, &S_, &T, &info);
			if (y_cur <= S_ && S_ <= bottom) add(s, A, S_, 0x21);
		}
		if (bottom < T) flags = 0x44;
		else if (bottom == T) flags = band(info, 0x20) != 0 ? 0x66 : 0;
	}
	if (e_bottom != NONE) {
		flags = band(flags, 0xfb);
		if (te_x > xlim) flags = band(flags, 0xbd);
	}
	if (xlim > cx) add(s, cx, bottom, 0xde);
	else flags = bor(flags, 0x88);
	add(s, xlim, bottom, flags);
	sweep_points(s, cx, y_cur, xlim, bottom);
}

/* GenerateNeighborsSouthWest 0x489d50. */
static void south_west(State *s) {
	int *e = s->ext;
	i64 cx = s->x, cy = s->y, bottom = s->bottom, left = s->left;
	int e_bottom = s->edge[2], e_left = s->edge[3];
	i64 y_cur = cy, x_left = left;
	i64 A = 0, S_ = 0, T = 0, info = 0;
	if (cx > left) {
		i64 ylim = bottom, le_y = bottom;
		if (e_left != NONE) {
			Edges *a = s->active[3];
			le_y = s16(e[band(shr(a->info[e_left], 2), 3)] + 1 + a->stop[e_left]);
			if (le_y < bottom) ylim = le_y;
		}
		i64 rx = left;
		int k = bottom_edge(s, s16(cy + 1), left, cx, s16(ylim + 1));
		i64 flags = 0xce;
		if (k != NONE) {
			expand(s, s->active[2], k, &A, &S_, &T, &info);
			ylim = A;
			if (left > S_) flags = 0x88;
			else if (left == S_) flags = band(info, 0x10) != 0 ? 0xcc : 0;
		}
		if (e_left != NONE) {
			flags = band(flags, 0xf7);
			if (le_y > ylim) flags = band(flags, 0x7b);
		}
		if (ylim > cy) add(s, left, cy, 0xbd);
		else flags = bor(flags, 0x11);
		add(s, left, ylim, flags);
		if (ylim == bottom) add(s, cx, ylim, 0x77);
		if (k != NONE) {
			Edges *a = s->active[2];
			if (left < S_) add(s, S_, A, 0x44);
			int done = 0;
			while (T <= cx) {
				add(s, T, A, 0x42);
				rx = T;
				k = k + 1;
				if (k >= a->count) {
					done = 1;
					break;
				}
				expand(s, a, k, &A, &S_, &T, &info);
				if (A != ylim || S_ > cx) {
					done = 1;
					break;
				}
				add(s, S_, A, 0x44);
			}
			if (!done) {
				add(s, cx, A, 0x21);
				rx = cx;
			}
		}
		sweep_points(s, left, cy, cx, ylim);
		y_cur = ylim;
		x_left = rx;
		if (ylim >= bottom) return;
	} else if (cy >= bottom) {
		return;
	}
	i64 xlim = x_left, te = x_left;
	if (e_bottom != NONE) {
		Edges *a = s->active[2];
		te = s16(e[band(a->info[e_bottom], 3)] - 1 + a->start[e_bottom]);
		if (te > x_left) xlim = te;
	}
	int k = left_edge(s, s16(cx - 1), y_cur, bottom, s16(xlim - 1));
	i64 flags = 0xce;
	if (k != NONE) {
		expand(s, s->active[3], k, &A, &S_, &T, &info);
		xlim = A;
		if (bottom < T) flags = 0x42;
		else if (bottom == T) flags = band(info, 0x20) != 0 ? 0xc6 : 0;
	}
	if (e_bottom != NONE) {
		flags = band(flags, 0xfd);
		if (te < xlim) flags = band(flags, 0xbb);
	}
	if (xlim < cx) add(s, cx, bottom, 0x77);
	else flags = bor(flags, 0x22);
	add(s, xlim, bottom, flags);
	if (xlim > x_left) add(s, xlim, y_cur, 0x04);
	if (k != NONE) {
		Edges *a = s->active[3];
		if (bottom > T) add(s, A, T, 0x84);
		while (S_ >= y_cur) {
			add(s, A, S_, 0x88);
			k = k - 1;
			if (k < 0) break;
			expand(s, a, k, &A, &S_, &T, &info);
			if (A != xlim || T < y_cur) {
				add(s, xlim, y_cur, 0x84);
				break;
			}
			if (bottom >= T) add(s, A, T, 0x84);
		}
	}
	sweep_points(s, xlim, y_cur, cx, bottom);
}

/* GenerateNeighborsNorthWest 0x489360. */
static void north_west(State *s) {
	int *e = s->ext;
	i64 cx = s->x, cy = s->y, top = s->top, left = s->left;
	int e_top = s->edge[0], e_left = s->edge[3];
	i64 y_cur = cy, x_left = left;
	i64 A = 0, S_ = 0, T = 0, info = 0;
	if (cx > left) {
		i64 ylim = top, le = top;
		if (e_left != NONE) {
			Edges *a = s->active[3];
			le = s16(e[band(a->info[e_left], 3)] - 1 + a->start[e_left]);
			if (le > top) ylim = le;
		}
		i64 x_after = left;
		int k = top_edge(s, s16(cy - 1), left, cx, s16(ylim - 1));
		i64 flags = 0x9d;
		if (k != NONE) {
			Edges *a = s->active[0];
			expand(s, a, k, &A, &S_, &T, &info);
			if (cx > T) add(s, T, A, 0x11);
			else add(s, cx, A, 0x22);
			ylim = A;
			x_after = T;
			while (left < S_) {
				add(s, S_, A, 0x18);
				k = k - 1;
				if (k < 0) break;
				if (e[a->dir[k]] + a->axis[k] + 1 != ylim) break;
				if (e[band(shr(a->info[k], 2), 3)] + a->stop[k] + 1 <= left) break;
				expand(s, a, k, &A, &S_, &T, &info);
				if (left <= T) add(s, T, A, 0x11);
			}
			if (left > S_) flags = 0x84;
			else if (left == S_) flags = band(info, 0x10) != 0 ? 0x9c : 0;
		}
		if (e_left != NONE) {
			flags = band(flags, 0xfb);
			if (le < ylim) flags = band(flags, 0x77);
		}
		if (ylim < cy) add(s, left, cy, 0xee);
		else flags = bor(flags, 0x44);
		add(s, left, ylim, flags);
		if (ylim == top) add(s, cx, ylim, 0x7b);
		sweep_points(s, left, ylim, cx, cy);
		y_cur = ylim;
		x_left = x_after;
		if (ylim <= top) return;
	} else if (cy <= top) {
		return;
	}
	i64 xlim = x_left, te = x_left;
	if (e_top != NONE) {
		Edges *a = s->active[0];
		te = s16(e[band(a->info[e_top], 3)] - 1 + a->start[e_top]);
		if (te > x_left) xlim = te;
	}
	int k = left_edge(s, s16(cx - 1), top, y_cur, s16(xlim - 1));
	i64 flags = 0x9d;
	if (k != NONE) {
		Edges *a = s->active[3];
		expand(s, a, k, &A, &S_, &T, &info);
		if (A > x_left) add(s, A, y_cur, 0x08);
		if (T < y_cur) add(s, A, T, 0x84);
		xlim = A;
		while (S_ > top) {
			add(s, A, S_, 0x88);
			k = k - 1;
			if (k < 0) break;
			if (e[a->dir[k]] + a->axis[k] + 1 != xlim) break;
			if (e[band(shr(a->info[k], 2), 3)] + a->stop[k] + 1 <= top) break;
			expand(s, a, k, &A, &S_, &T, &info);
			if (top <= T) add(s, A, T, 0x84);
		}
		if (top > S_) flags = 0x11;
		else if (top == S_) flags = band(info, 0x10) != 0 ? 0x99 : 0;
	}
	if (e_top != NONE) {
		flags = band(flags, 0xfe);
		if (te < xlim) flags = band(flags, 0xe7);
	}
	if (xlim < cx) add(s, cx, top, 0x7b);
	else flags = bor(flags, 0x21);
	add(s, xlim, top, flags);
	sweep_points(s, xlim, top, cx, y_cur);
}

/* CollectLocalPathGeometry 0x488320 at (x, y) for the open directions `mask`. */
static void collect(State *s, int x, int y, i64 mask) {
	int map_w = s->map_w, map_h = s->map_h;
	int *e = s->ext;
	s->x = x;
	s->y = y;
	s->nb_count = 0;
	s->rect_count = 0;
	s->top = (int)s16(y - WINDOW);
	s->right = (int)s16(x + WINDOW);
	s->left = (int)s16(x - WINDOW);
	s->bottom = (int)s16(y + WINDOW);
	if (s->top < 0) s->top = 0;
	if (s->left < 0) s->left = 0;
	if (s->bottom >= map_h) s->bottom = map_h - 1;
	if (s->right >= map_w) s->right = map_w - 1;
	if (band(mask, 0x19) == 0) s->top = y;
	if (band(mask, 0x23) == 0) s->right = x;
	if (band(mask, 0x46) == 0) s->bottom = y;
	if (band(mask, 0x8c) == 0) s->left = x;
	collect_terrain(s);
	collect_units(s);
	for (int d = 0; d < 4; d++) s->active[d] = &s->loc[d];
	int k = top_edge(s, y, x, x, s16(s->top - 1));
	s->edge[0] = k;
	if (k != NONE) s->top = (int)s16(e[0] + 1 + s->active[0]->axis[k]);
	k = right_edge(s, x, y, y, s16(s->right + 1));
	s->edge[1] = k;
	if (k != NONE) s->right = (int)s16(e[1] - 1 + s->active[1]->axis[k]);
	k = bottom_edge(s, y, x, x, s16(s->bottom + 1));
	s->edge[2] = k;
	if (k != NONE) s->bottom = (int)s16(e[2] - 1 + s->active[2]->axis[k]);
	k = left_edge(s, x, y, y, s16(s->left - 1));
	s->edge[3] = k;
	if (k != NONE) s->left = (int)s16(e[3] + 1 + s->active[3]->axis[k]);
	int top = s->top, right = s->right, bottom = s->bottom, left = s->left;
	if (top == y && right == x) mask = band(mask, 0xfe);
	if (bottom == y) {
		if (right == x) mask = band(mask, 0xfd);
		if (left == x) mask = band(mask, 0xfb);
	}
	if (top == y) {
		if (left == x) mask = band(mask, 0xf7);
		mask = band(mask, 0xef);
	}
	if (right == x) mask = band(mask, 0xdf);
	if (bottom == y) mask = band(mask, 0xbf);
	if (left == x) mask = band(mask, 0x7f);
	if (band(mask, DIR_N) != 0) {
		mask = bor(mask, 9);
		add(s, x, top, s->edge[0] != NONE ? 0xa6 : 0xbf);
	}
	if (band(mask, DIR_E) != 0) {
		mask = bor(mask, 3);
		add(s, right, y, s->edge[1] != NONE ? 0x5c : 0x7f);
	}
	if (band(mask, DIR_S) != 0) {
		mask = bor(mask, 6);
		add(s, x, bottom, s->edge[2] != NONE ? 0xa9 : 0xef);
	}
	if (band(mask, DIR_W) != 0) {
		mask = bor(mask, 0xc);
		add(s, left, y, s->edge[3] != NONE ? 0x53 : 0xdf);
	}
	s->mask = (int)mask;
}

/* --- Swept areas (0x69ec08, 0x69ee10, 0x69f018) --- */

static void visited_reset(State *s) {
	Visited *v = &s->v;
	v->count = 1;
	v->xs[0] = 0;
	v->xs[1] = s->map_w;
	v->colmap[0] = 0;
	v->colmap[1] = -1;
	v->lists[0].count = 0;
}

static int column_of(State *s, int x) {
	Visited *v = &s->v;
	int c = 0;
	while (x >= v->xs[c + 1]) {
		c++;
		if (c + 1 > v->count) luaL_error(s->L, "kp_path: swept column beyond the map");
	}
	return c;
}

static Intervals *list_of(State *s, int slot) {
	if (slot < 0 || slot > MAX_COLUMNS) luaL_error(s->L, "kp_path: swept list %d out of range", slot);
	return &s->v.lists[slot];
}

static void clone_list(State *s, int slot) {
	Visited *v = &s->v;
	Intervals *src = list_of(s, slot), *dst = list_of(s, v->count);
	dst->count = src->count;
	for (int i = 0; i < src->count; i++) {
		dst->y0[i] = src->y0[i];
		dst->y1[i] = src->y1[i];
	}
}

static void open_column(Visited *v, int c) {
	for (int i = v->count; i >= c; i--) {
		v->xs[i + 1] = v->xs[i];
		v->colmap[i + 1] = v->colmap[i];
	}
}

/* AddVisitedArea 0x492de0. */
static int visited_add(State *s, const int *r) {
	Visited *v = &s->v;
	int x0 = r[0], y0 = r[1], x1 = r[2], y1 = r[3];
	int c = column_of(s, x0);
	if (v->xs[c] < x0) {
		if (v->count == MAX_COLUMNS) return 0;
		int slot = v->colmap[c];
		open_column(v, c);
		v->colmap[c] = v->count;
		clone_list(s, slot);
		v->count++;
		c++;
		v->xs[c] = x0;
	}
	if (x0 > x1) return 1;
	for (;;) {
		Intervals *list = list_of(s, v->colmap[c]);
		if (x1 < v->xs[c + 1] - 1) {
			if (v->count == MAX_COLUMNS) return 0;
			int slot = v->colmap[c];
			open_column(v, c);
			v->colmap[c + 1] = v->count;
			clone_list(s, slot);
			v->count++;
			v->xs[c + 1] = x1 + 1;
		}
		int n = list->count;
		int i = 0, k = n;
		while (k > 0 && y0 > list->y1[i] + 1) {
			k--;
			i++;
		}
		if (k == 0) {
			if (n == MAX_INTERVALS) return 0;
			list->y0[i] = y0;
			list->y1[i] = y1;
			list->count = n + 1;
		} else if (y1 < list->y0[i] - 1 || y0 > list->y1[i] + 1) {
			if (n == MAX_INTERVALS) return 0;
			for (int j = n; j >= i + 1; j--) {
				list->y0[j] = list->y0[j - 1];
				list->y1[j] = list->y1[j - 1];
			}
			list->y0[i] = y0;
			list->y1[i] = y1;
			list->count = n + 1;
		} else {
			if (list->y0[i] > y0) list->y0[i] = y0;
			if (list->y1[i] < y1) list->y1[i] = y1;
			/* Coalesce over the whole list, from its first interval. */
			int j = 0, rounds = list->count - 1;
			for (int t = 0; t < rounds; t++) {
				if (list->y0[j + 1] - 1 <= list->y1[j]) {
					if (list->y1[j] < list->y1[j + 1]) list->y1[j] = list->y1[j + 1];
					for (int m = j + 1; m <= list->count - 2; m++) {
						list->y0[m] = list->y0[m + 1];
						list->y1[m] = list->y1[m + 1];
					}
					list->count--;
				} else {
					j++;
				}
			}
		}
		c++;
		if (v->xs[c] > x1) return 1;
	}
}

static int interval_at(Intervals *list, int y) {
	for (int i = 0; i < list->count; i++)
		if (y <= list->y1[i]) return i;
	return NONE;
}

/* FilterFlagsByVisitedAreas 0x4930b0. */
static i64 visited_filter(State *s, int x, int y, i64 flags) {
	Visited *v = &s->v;
	int c = column_of(s, x);
	Intervals *list = list_of(s, v->colmap[c]);
	if (list->count == 0) return flags;
	int i = interval_at(list, y);
	if (i == NONE) return flags;
	i64 m = list->y0[i] >= y ? 0x19 : 0;
	if (list->y1[i] <= y) m = bor(m, 0x46);
	if (v->xs[c] == x) {
		m = bor(m, 0x8c);
		if (c > 0) {
			Intervals *l2 = list_of(s, v->colmap[c - 1]);
			int j = l2->count > 0 ? interval_at(l2, y) : NONE;
			if (j != NONE && l2->y0[j] <= y && l2->y1[j] >= y) {
				m = band(m, 0x7f);
				if (l2->y0[j] < y) m = band(m, 0xf7);
				if (l2->y1[j] > y) m = band(m, 0xfb);
			}
		}
	}
	if (v->xs[c + 1] - 1 == x) {
		m = bor(m, 0x23);
		if (c + 1 < v->count) {
			Intervals *l2 = list_of(s, v->colmap[c + 1]);
			if (l2->count > 0) {
				int j = interval_at(l2, y);
				if (j == NONE) return band(flags, m);
				if (l2->y0[j] <= y && l2->y1[j] >= y) {
					m = band(m, 0xdf);
					if (l2->y0[j] < y) m = band(m, 0xfe);
					if (l2->y1[j] > y) m = band(m, 0xfd);
				}
			}
		}
	}
	return band(flags, m);
}

/* ExpandLocalNode 0x491ba0. */
static void expand_node(State *s, Node *node) {
	for (int d = 0; d < 4; d++) s->active[d] = s->saved[d];
	for (int d = 0; d < 4; d++) s->loc[d].count = 0;
	s->goal_found = 0;
	collect(s, node->x, node->y, node->flags);
	if (band(s->mask, DIR_NE) != 0) north_east(s);
	if (band(s->mask, DIR_SE) != 0) south_east(s);
	if (band(s->mask, DIR_SW) != 0) south_west(s);
	if (band(s->mask, DIR_NW) != 0) north_west(s);
	for (int k = 0; k < s->rect_count; k++) visited_add(s, s->rects[k]);
}

/* --- The heap (game/pathing/heap.lua) --- */

#define SENTINEL (ARENA + 1)

static i64 prio_of(State *s, int i) { return s->nodes[i].prio; }

static void sift_up(State *s, int i, int node) {
	s->heap[0] = SENTINEL;
	int p = i / 2;
	while (prio_of(s, s->heap[p]) >= s->nodes[node].prio) {
		s->heap[i] = s->heap[p];
		i = p;
		p = i / 2;
	}
	s->heap[i] = node;
}

static void heap_insert(State *s, int node) {
	if (s->heap_count + 1 >= HEAP_SIZE) luaL_error(s->L, "kp_path: heap full");
	s->heap_count++;
	sift_up(s, s->heap_count, node);
}

static void sift_down(State *s, int i) {
	int node = s->heap[i];
	while (i <= s->heap_count / 2) {
		int c = i * 2;
		if (c < s->heap_count && prio_of(s, s->heap[c]) > prio_of(s, s->heap[c + 1])) c++;
		if (s->nodes[node].prio <= prio_of(s, s->heap[c])) {
			s->heap[i] = node;
			return;
		}
		s->heap[i] = s->heap[c];
		i = c;
	}
	s->heap[i] = node;
}

static int heap_pop(State *s) {
	int top = s->heap[1];
	s->heap[1] = s->heap[s->heap_count];
	s->heap_count--;
	sift_down(s, 1);
	return top;
}

static void heap_change(State *s, int node, i64 prio) {
	i64 old = s->nodes[node].prio;
	if (old == prio) return;
	s->nodes[node].prio = prio;
	int i = s->heap_count;
	while (i > 0 && s->heap[i] != node) i--;
	if (i == 0) return;
	if (old <= prio) sift_down(s, i);
	else sift_up(s, i, node);
}

/* --- Position index of the nodes (the first node at each position) --- */

static void hash_reset(State *s) {
	for (int i = 0; i < 512; i++) s->hash_node[i] = NONE;
}

static int hash_slot(State *s, int x, int y) {
	unsigned h = ((unsigned)x * 73856093u) ^ ((unsigned)y * 19349663u);
	int i = (int)(h & 511);
	while (s->hash_node[i] != NONE && (s->hash_key_x[i] != x || s->hash_key_y[i] != y)) i = (i + 1) & 511;
	return i;
}

/* --- Inputs --- */

static void load_contours(State *s, int contours) {
	lua_State *L = s->L;
	i64 version = getf(L, contours, "version");
	for (int d = 0; d < 4; d++) {
		lua_rawgeti(L, contours, d);
		int t = lua_gettop(L);
		if (!lua_istable(L, t)) luaL_error(L, "kp_path: contours[%d] is not a table", d);
		const void *key = lua_topointer(L, t);
		int count = (int)getf(L, t, "count");
		Edges *c = &s->contours[d];
		if (key != s->contour_key[d] || version != s->contour_version || count != c->count) {
			edges_reserve(c, count + 1);
			int ax = push_table(L, t, "axis"), st = push_table(L, t, "start"), sp = push_table(L, t, "stop");
			int di = push_table(L, t, "dir"), in = push_table(L, t, "info");
			for (int k = 0; k < count; k++) {
				c->axis[k] = (int)geti(L, ax, k);
				c->start[k] = (int)geti(L, st, k);
				c->stop[k] = (int)geti(L, sp, k);
				c->dir[k] = (int)geti(L, di, k);
				c->info[k] = (int)geti(L, in, k);
			}
			lua_pop(L, 5);
			c->count = count;
			s->contour_key[d] = key;
		}
		lua_pop(L, 1);
		s->saved[d] = c;
	}
	s->contour_version = version;
}

static void load_spatial(State *s, int sp) {
	lua_State *L = s->L;
	int count = (int)getf(L, sp, "count");
	if (count > s->sp_cap) {
		s->x_at = (i64 *)realloc(s->x_at, sizeof(i64) * count);
		s->x_unit = (i64 *)realloc(s->x_unit, sizeof(i64) * count);
		s->y_at = (i64 *)realloc(s->y_at, sizeof(i64) * count);
		s->y_unit = (i64 *)realloc(s->y_unit, sizeof(i64) * count);
		s->sp_cap = count;
	}
	int xa = push_table(L, sp, "x_at"), xu = push_table(L, sp, "x_unit");
	int ya = push_table(L, sp, "y_at"), yu = push_table(L, sp, "y_unit");
	for (int k = 0; k < count; k++) {
		s->x_at[k] = geti(L, xa, k);
		s->x_unit[k] = geti(L, xu, k);
		s->y_at[k] = geti(L, ya, k);
		s->y_unit[k] = geti(L, yu, k);
	}
	lua_pop(L, 4);
	s->sp_count = count;
}

/* The points of a chain into c.points (StoreLocalPathPoints 0x492860; chain[0] is the goal,
 * chain[n - 1] the source). */
static void store_points(lua_State *L, int c, int points, const int *cx, const int *cy, int n) {
	int k;
	seti(L, c, "point_index", 0);
#define PUT_POINT(slot, i)                     \
	do {                                       \
		lua_createtable(L, 2, 0);              \
		lua_pushnumber(L, cx[i]);              \
		lua_rawseti(L, -2, 1);                 \
		lua_pushnumber(L, cy[i]);              \
		lua_rawseti(L, -2, 2);                 \
		lua_rawseti(L, points, (slot));        \
	} while (0)
	if (n < 3) {
		if (n == 2) PUT_POINT(0, 0);
		seti(L, c, "point_count", n - 1);
		return;
	}
	k = 0;
	int j = n - 2; /* chain[j] is the node after the source */
	while (j >= 1 && k <= 0x30) {
		PUT_POINT(k, j);
		k++;
		j--;
	}
	PUT_POINT(k, 0);
	seti(L, c, "point_count", k + 1);
#undef PUT_POINT
}

/* search(game, c, next_region, gx, gy) -> boolean: LocalPathSearch 0x491f50. */
static int l_search(lua_State *L) {
	State *s = &S;
	s->L = L;
	luaL_checktype(L, 1, LUA_TTABLE);
	luaL_checktype(L, 2, LUA_TTABLE);
	int g = 1, c = 2;
	i64 next_region = (i64)luaL_checknumber(L, 3);
	i64 gx = (i64)luaL_checknumber(L, 4), gy = (i64)luaL_checknumber(L, 5);
	lua_settop(L, 5);
	luaL_checkstack(L, 64, "kp_path");

	int u = push_table(L, g, "u");
	s->u_sprite = push_table(L, u, "sprite");
	s->u_move_x = push_table(L, u, "move_x");
	s->u_move_y = push_table(L, u, "move_y");
	s->u_type = push_table(L, u, "type");
	s->u_pathing = push_table(L, u, "pathing_flags");
	s->u_status = push_table(L, u, "status");
	s->u_owner = push_table(L, u, "owner");
	s->u_order = push_table(L, u, "order");
	s->u_bound[0] = push_table(L, u, "bound_left");
	s->u_bound[1] = push_table(L, u, "bound_right");
	s->u_bound[2] = push_table(L, u, "bound_top");
	s->u_bound[3] = push_table(L, u, "bound_bottom");
	int img = push_table(L, g, "img");
	int spr = push_table(L, img, "spr");
	s->spr_x = push_table(L, spr, "x");
	s->spr_y = push_table(L, spr, "y");
	s->spr_owner = push_table(L, spr, "owner");
	s->alliance = push_table(L, g, "alliance");
	s->path_subject = getf(L, g, "path_subject");
	s->path_all_units = getf(L, g, "path_all_units");
	s->map_w = (int)getf(L, g, "map_w_px");
	s->map_h = (int)getf(L, g, "map_h_px");
	load_spatial(s, push_table(L, g, "spatial"));
	load_contours(s, push_table(L, g, "contours"));
	int regions = push_table(L, g, "regions");
	lua_getfield(L, regions, "data");
	s->rgns = (const unsigned char *)lua_tolstring(L, -1, &s->rgns_len);
	if (!s->rgns) luaL_error(L, "kp_path: regions.data is not a string");
	s->rgns_width = getf(L, regions, "width");
	s->rgns_height = getf(L, regions, "height");
	s->raster_at = getf(L, regions, "raster_at");
	s->splits_at = getf(L, regions, "splits_at");
	int rg = push_table(L, g, "rg");
	int rg_class = push_table(L, rg, "class"), rg_ground = push_table(L, rg, "ground");
	int rg_neighbors = push_table(L, rg, "neighbors");
	int rcount = (int)getf(L, regions, "count");
	if (rcount + 1 > s->mark_cap) {
		s->mark = (int *)realloc(s->mark, sizeof(int) * (rcount + 1));
		s->mark_cap = rcount + 1;
	}
	memset(s->mark, 0, sizeof(int) * s->mark_cap);
	int dat = push_table(L, g, "dat");
	int dat_units = push_table(L, dat, "units");
	int extents = push_table(L, dat_units, "unit_extents");
	int points = push_table(L, c, "points");

	s->stamp++;
	if (s->stamp == 0x7fffffff) {
		memset(s->memo_stamp, 0, sizeof s->memo_stamp);
		memset(s->memo_obstacle_stamp, 0, sizeof s->memo_obstacle_stamp);
		s->stamp = 1;
	}

	i64 c_unit = getf(L, c, "unit");
	i64 goal_cx = getf(L, c, "goal_x"), goal_cy = getf(L, c, "goal_y");
	int final = gx == goal_cx && gy == goal_cy;
	int scale_other = 0;
	if (next_region < REGION_IDS) scale_other = geti(L, rg_class, next_region) != 0x1ffd;
	seti(L, c, "local_nodes", 0);
	seti(L, c, "local_peak", 0);
	i64 local_peak = 0;
	int src_region = (int)getf(L, c, "src_region");
	int goal_region = region_at(s, goal_cx, goal_cy);
	int around = next_region < REGION_IDS ? (int)next_region : goal_region;
	lua_rawgeti(L, rg_neighbors, around);
	int list = lua_gettop(L);
	if (!lua_istable(L, list)) luaL_error(L, "kp_path: no neighbour list of region %d", around);
	int near_count = (int)geti(L, rg_ground, around);
	int neighbor_ids[256];
	if (near_count > 256) luaL_error(L, "kp_path: too many region neighbours");
	for (int k = 0; k < near_count; k++) {
		neighbor_ids[k] = (int)geti(L, list, k);
		if (neighbor_ids[k] != src_region) *mark_at(s, neighbor_ids[k]) = 1;
	}
	i64 gx8 = shl(gx, 8), gy8 = shl(gy, 8);
	/* SetUnitTypeExtents 0x4891c0 (unit types are below 0x8000) */
	i64 t = geti(L, s->u_type, c_unit);
	{
		i64 left = s16(geti(L, extents, t * 4)), up = s16(geti(L, extents, t * 4 + 1));
		i64 right = s16(geti(L, extents, t * 4 + 2)), down = s16(geti(L, extents, t * 4 + 3));
		s->ext[0] = (int)s16(up);
		s->ext[1] = (int)s16(-right);
		s->ext[2] = (int)s16(-down);
		s->ext[3] = (int)s16(left);
		s->ext[8] = (int)s16(up + 1);
		s->ext[9] = (int)s16(-1 - right);
		s->ext[10] = (int)s16(-1 - down);
		s->ext[11] = (int)s16(left + 1);
	}
	for (int d = 0; d < 4; d++) s->active[d] = s->saved[d];
	for (int d = 0; d < 4; d++) edges_reserve(&s->loc[d], LOCAL_EDGES + 1);
	s->unit = (int)c_unit;
	s->goal_x = (int)gx;
	s->goal_y = (int)gy;
	s->target = (int)getf(L, c, "target");
	s->box[0] = (int)getf(L, c, "box_left");
	s->box[1] = (int)getf(L, c, "box_top");
	s->box[2] = (int)getf(L, c, "box_right");
	s->box[3] = (int)getf(L, c, "box_bottom");
	visited_reset(s);
	hash_reset(s);
	s->heap_count = 0;
	s->nodes[SENTINEL].prio = -1;
	Node *start = &s->nodes[0];
	start->x = (int)getf(L, c, "src_x");
	start->y = (int)getf(L, c, "src_y");
	start->region = src_region;
	start->near = start->in_next = start->goal = start->closed = 0;
	start->flags = 0xff;
	start->depth = 0;
	start->heur = START_COST;
	start->cost = 0;
	start->prio = START_COST;
	start->parent = NONE;
	s->node_count = 0;
	heap_insert(s, 0);
	int goal_hit = 0, done = 0, goal_count = 0, near_open = 0;
	int best = NONE, reached_goal = 0, finished = 0;
	for (;;) {
		int ci = heap_pop(s);
		Node *cur = &s->nodes[ci];
		if (cur->goal != 0) {
			reached_goal = 1;
			best = ci;
			break;
		}
		if (cur->flags != 0) cur->flags = (int)visited_filter(s, cur->x, cur->y, cur->flags);
		int used = s->node_count + 1;
		if (goal_count > 0 && !goal_hit && (!final || used >= GOAL_NODES)) done = 1;
		if (near_open != 0 && !goal_hit && used >= NEAR_NODES) done = 1;
		if (cur->in_next != 0 && done) {
			best = ci;
			break;
		}
		cur->closed = 1;
		if (cur->in_next != 0) goal_count--;
		if (cur->near != 0) {
			near_open--;
			if (done) {
				finished = 1;
				break;
			}
		}
		if (!goal_hit && !done && cur->flags != 0) {
			expand_node(s, cur);
			for (int k = 0; k < s->nb_count; k++) {
				Neighbor *q = &s->nb[k];
				int region = region_at(s, q->x, q->y);
				int in_next = region == next_region;
				int near = *mark_at(s, region) != 0;
				int skip = 0;
				if (q->goal != 0) {
					goal_hit = 1;
				} else if (q->flags != 0) {
					q->flags = (int)visited_filter(s, q->x, q->y, q->flags);
					if (q->flags == 0) skip = 1;
				}
				if (skip) continue;
				int qflags = q->flags;
				int slot = hash_slot(s, q->x, q->y);
				int ni = s->hash_node[slot];
				if (ni != NONE) {
					Node *node = &s->nodes[ni];
					if (cur->parent != ni) {
						i64 cost = approx(shl(q->x, 8), shl(q->y, 8), shl(cur->x, 8), shl(cur->y, 8));
						if (scale_other && region != next_region && region != src_region)
							cost = idiv(L, s32(shl(cost, 9)), 256);
						i64 nw = s32(cur->cost + cost);
						if (nw < node->cost) {
							node->parent = ci;
							node->depth = (cur->depth + 1) % 256;
							node->cost = nw;
							i64 prio = s32(node->heur + nw);
							if (node->closed == 0) {
								heap_change(s, ni, prio);
							} else {
								node->prio = prio;
								node->flags = qflags;
								node->closed = 0;
								node->goal = q->goal;
								heap_insert(s, ni);
								if (in_next) goal_count++;
								if (near) near_open++;
							}
							if (s->heap_count == OPEN_LIMIT) break;
						} else if (node->closed == 0) {
							node->flags = (int)band(node->flags, qflags);
						}
					}
				} else {
					if (s->node_count >= ARENA) luaL_error(L, "kp_path: node arena full");
					ni = ++s->node_count;
					Node *node = &s->nodes[ni];
					node->x = q->x;
					node->y = q->y;
					node->parent = ci;
					node->region = region;
					node->flags = qflags;
					node->depth = (cur->depth + 1) % 256;
					s->hash_key_x[slot] = q->x;
					s->hash_key_y[slot] = q->y;
					s->hash_node[slot] = ni;
					i64 cost = approx(shl(cur->x, 8), shl(cur->y, 8), shl(q->x, 8), shl(q->y, 8));
					if (scale_other && region != next_region && region != src_region)
						cost = idiv(L, s32(shl(cost, 9)), 256);
					node->heur = approx(shl(q->x, 8), shl(q->y, 8), gx8, gy8);
					node->cost = s32(cur->cost + cost);
					node->prio = s32(node->heur + node->cost);
					node->in_next = in_next;
					node->goal = q->goal;
					node->near = near;
					node->closed = qflags == 0 ? 1 : 0;
					if (node->closed == 0 || q->goal != 0) {
						node->closed = 0;
						heap_insert(s, ni);
						if (in_next) goal_count++;
						if (near) near_open++;
					}
					if (s->heap_count == OPEN_LIMIT || s->node_count == ARENA) break;
				}
			}
			if (local_peak < s->heap_count) local_peak = s->heap_count;
			if ((s->heap_count == OPEN_LIMIT || s->node_count == ARENA) && !goal_hit && goal_count == 0) {
				finished = 1;
				break;
			}
		}
		if (s->heap_count == 0) {
			finished = 1;
			break;
		}
	}
	seti(L, c, "local_peak", local_peak);
	if (finished) {
		/* The search ended without reaching the goal: choose the node to head for. */
		seti(L, c, "local_partial", 1);
		i64 reach = 0, near_hits = 0;
		for (int i = 1; i <= s->node_count; i++) {
			Node *n = &s->nodes[i];
			int *m = mark_at(s, n->region);
			if (*m != 0) (*m)++;
			if (n->closed == 0) {
				i64 f = n->flags;
				if (f != 0) f = visited_filter(s, n->x, n->y, f);
				if (f == 0) {
					n->flags = 0;
					n->closed = 1;
					if (n->in_next != 0) goal_count--;
				} else {
					n->flags = (int)f;
					reach++;
					if (n->region == goal_region) near_hits++;
				}
			}
		}
		if (final) {
			reach = near_hits;
			for (int k = 0; k < near_count; k++) {
				int id = neighbor_ids[k];
				if (id != src_region) {
					i64 m = *mark_at(s, id);
					if (m < 2) {
						reach++;
						break;
					}
					reach = reach - idiv(L, m, 2);
					if (reach < 0) reach = 0;
				}
			}
		}
		int reachable = reach == 0 && goal_count == 0;
		start->heur = approx(shl(start->x, 8), shl(start->y, 8), gx8, gy8);
		best = 0;
		if (reachable) {
			i64 limit = goal_count != 0 ? START_COST : start->heur;
			for (int i = 1; i <= s->node_count; i++) {
				Node *n = &s->nodes[i];
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
			seti(L, c, "reached", 1);
			seti(L, c, "reached_end", 1);
		} else {
			i64 limit = START_COST;
			for (int i = 1; i <= s->node_count; i++) {
				Node *n = &s->nodes[i];
				i64 score;
				if (n->region == goal_region || n->region == next_region) score = idiv(L, n->cost, 2) + n->heur;
				else if (*mark_at(s, n->region) != 0) score = idiv(L, n->heur * 3, 2);
				else if (n->region == src_region) score = n->heur * 2;
				else score = n->heur * 4;
				if (n->closed != 0) score = shl(score, 5);
				if (score < limit) {
					best = i;
					limit = score;
				}
			}
		}
	} else if (reached_goal) {
		seti(L, c, "reached", 1);
	}
	seti(L, c, "local_nodes", s->node_count + 1);
	if (best != NONE) {
		int cx[MAX_CHAIN], cy[MAX_CHAIN];
		int n = 0, i = best;
		while (i != NONE && n < MAX_CHAIN) {
			cx[n] = s->nodes[i].x;
			cy[n] = s->nodes[i].y;
			n++;
			i = s->nodes[i].parent;
		}
		store_points(L, c, points, cx, cy, n);
	}
	i64 point_count = getf(L, c, "point_count");
	lua_settop(L, 5);
	lua_pushboolean(L, point_count > 0);
	return 1;
}

static const luaL_Reg FUNCTIONS[] = {
	{ "search", l_search },
	{ NULL, NULL },
};

int kp_path_register(lua_State *L) {
	luaL_register(L, "kp_path", FUNCTIONS);
	return 1;
}
