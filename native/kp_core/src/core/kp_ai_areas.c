/* The AI's resource areas (atlas P32, P33; docs/research/native-resource-generation.md, native-resource-areas.md,
 * native-region-paths.md#ai-placement-distance): GenerateResourceAreas 0x497a20 at the map's start, the periodic
 * RefreshResourceAreas 0x496c80 and the route distance 0x496930 that they measure with. State: kp_game.h KpAreas and
 * each resource's area byte +0xd8. */
#include "kp_game_internal.h"
#include "kp_pathing.h"
#include "kp_pathing_internal.h"
#include "kp_terrain.h"

#include <string.h>

#define BUILDING 0x1
#define RESOURCE_CONTAINER 0x2000
#define R_AMOUNT 0xd0
#define R_AREA 0xd8
#define AREA_OCCUPIED 1
#define AREA_RESERVED 2
#define TOWN_NONE (-1)
/* the nearest area of a non-campaign player is among the records of the 8 start locations */
#define START_AREAS 8
#define NEAREST_NONE 99999999
/* a worker limit counts a mineral field's share of the remaining minerals, three workers a geyser */
#define LIMIT_MINERALS 500
#define LIMIT_PER_FIELD 2
#define LIMIT_PER_GEYSER 3
#define UNREACHABLE 9999
#define TILE_SHIFT 5
/* a resource joins an area or a cluster within these pixels and route tiles (minerals, gas) */
#define MINERAL_REACH 640
#define GAS_REACH 896
#define MINERAL_ROUTE 20
#define GAS_ROUTE 28
/* the clusters: the box around a seed, the last area they create, the weight of an amount, the reference's score */
#define CLUSTER_RADIUS 960
#define CLUSTER_LAST_AREA 249
#define WEIGHT_AMOUNT 500
#define WEIGHT_MAX 4
#define REFERENCE_NONE 99999999
#define REFERENCE_ROUTE 20
/* the refresh: its period, and the reach of a building that occupies an area */
#define REFRESH_PERIOD 450
#define OCCUPY_REACH 384
#define OCCUPY_ROUTE 12

/* The classes of the generation's array (one byte per active-list position). */
enum { CLASS_NONE, CLASS_MINERAL, CLASS_GAS, CLASS_MINERAL_AREA, CLASS_GAS_AREA };

static int64_t flags_of(KpGame *g, int64_t n) { return KP_DAT(g, units, special_ability_flags, g->u.type[n]); }
static int resource(KpGame *g, int64_t n) { return kp_band(flags_of(g, n), RESOURCE_CONTAINER) != 0; }
static int mineral(KpGame *g, int64_t n) { return kp_mineral_field(g->u.type[n]); }
static int64_t amount(KpGame *g, int64_t n) { return kp_union_get(g, n, R_AMOUNT, 2); }

int64_t kp_ai_resource_area(KpGame *g, int64_t n) { return kp_union_get(g, n, R_AREA, 1); }

static int64_t direct(int64_t sx, int64_t sy, int64_t dx, int64_t dy)
{
	return kp_sarv(kp_approx_distance(dx - sx, dy - sy), TILE_SHIFT);
}

/* 0x496930: the same region measures straight; a route of more regions sums the legs through the centres of its
 * regions after the first and before the last, then on to the point itself. */
int64_t kp_ai_route_distance(KpGame *g, int64_t sx, int64_t sy, int64_t dx, int64_t dy)
{
	KpRoute c;
	int64_t from = kp_region_at(g, sx, sy), to = kp_region_at(g, dx, dy), d, px, py, qx, qy, last, i;
	if (from == to) return direct(sx, sy, dx, dy);
	if (kp_region_group_of(g, from) != kp_region_group_of(g, to)) return UNREACHABLE;
	kp_route_new(&c, 0, sx, sy, dx, dy);
	c.goal_x = dx;
	c.goal_y = dy;
	c.src_region = from;
	c.dst_region = to;
	if (!kp_search_region_path(g, &c)) return UNREACHABLE; /* SearchRegionPath 0x491300 */
	if (c.region_count <= 2) return direct(sx, sy, dx, dy);
	kp_region_center_point(g, c.regions[1], &px, &py); /* GetRegionCenterPoint 0x481a60 */
	d = kp_approx_distance(px - sx, py - sy);
	last = c.region_count - 1;
	for (i = 2; i < last; i++) {
		kp_region_center_point(g, c.regions[i], &qx, &qy);
		d += kp_approx_distance(qx - px, qy - py);
		px = qx;
		py = qy;
	}
	d += kp_approx_distance(dx - px, dy - py);
	return kp_shrv(d, TILE_SHIFT);
}

/* Whether resource n is close enough to (x, y) for an area: reachable, at the same height, within the pixels and
 * route tiles of its kind (the shared checks of 0x4955b0, 0x495750 and 0x497480). */
static int near_point(KpGame *g, int64_t n, int64_t x, int64_t y)
{
	int64_t sx, sy, ore = mineral(g, n);
	kp_sprite_pos(g, n, &sx, &sy);
	if (!kp_same_region_group(g, n, x, y)) return 0; /* UnitCanReachPoint 0x481d00 */
	if (kp_terrain_height(g, sx, sy) != kp_terrain_height(g, x, y)) return 0; /* GetTerrainHeightAt 0x445690 */
	if (kp_approx_distance(sx - x, sy - y) > (ore ? MINERAL_REACH : GAS_REACH)) return 0;
	return kp_ai_route_distance(g, x, y, sx, sy) <= (ore ? MINERAL_ROUTE : GAS_ROUTE);
}

static int in_box(KpGame *g, int64_t n, const int64_t box[4])
{
	int64_t sx, sy;
	kp_sprite_pos(g, n, &sx, &sy);
	return sx >= box[0] && sx <= box[2] && sy >= box[1] && sy <= box[3];
}

/* The ±CLUSTER_RADIUS box around (x, y) as WORDs. */
static void cluster_box(int64_t x, int64_t y, int64_t box[4])
{
	box[0] = kp_s16v(x - CLUSTER_RADIUS);
	box[1] = kp_s16v(y - CLUSTER_RADIUS);
	box[2] = kp_s16v(x + CLUSTER_RADIUS);
	box[3] = kp_s16v(y + CLUSTER_RADIUS);
}

/* IsClusterCandidate 0x495750: an unassigned resource in the box near the seed (x, y). */
static int candidate(KpGame *g, int64_t n, int64_t x, int64_t y, const int64_t box[4])
{
	if (!resource(g, n) || kp_ai_resource_area(g, n) != 0 || !in_box(g, n, box)) return 0;
	return near_point(g, n, x, y);
}

/* FindStartAreaForResource 0x4955b0: the first area resource n attaches to, 0 for none. */
static int64_t start_area(KpGame *g, int64_t n)
{
	KpAreas *a = &g->areas;
	int64_t k;
	for (k = 1; k <= a->used; k++) {
		if (near_point(g, n, a->x[k], a->y[k])) return k;
	}
	return 0;
}

/* GetResourceClusterWeight 0x497480: 1 and, for each unassigned resource of the box near the seed, its amount's
 * share (QueryUnitsInRect 0x422330 order). */
static int64_t cluster_weight(KpGame *g, int64_t seed, const int64_t box[4])
{
	int64_t sx, sy, weight = 1, *found;
	int count, i;
	kp_sprite_pos(g, seed, &sx, &sy);
	found = kp_query_rect(g, box[0], box[1], box[2], box[3], &count);
	for (i = 0; i < count; i++) {
		int64_t n = found[i], share;
		if (!candidate(g, n, sx, sy, box)) continue;
		share = amount(g, n) / WEIGHT_AMOUNT + 1;
		weight += share > WEIGHT_MAX ? WEIGHT_MAX : share;
	}
	kp_query_release(g, found);
	return weight;
}

/* PickClusterReferenceMineral 0x495e70: the unassigned mineral field (class 1) of the least route distance from
 * (x, y) within REFERENCE_ROUTE tiles, the later one on a tie; fallback when none. */
static int64_t reference(KpGame *g, int64_t fallback, int64_t x, int64_t y, const uint8_t *klass)
{
	int64_t n = g->lists[KP_LIST_ACTIVE].head, best = REFERENCE_NONE, found = 0;
	int k = 0;
	for (; n != 0; n = g->u.next[n], k++) {
		int64_t sx, sy, score;
		if (klass[k] != CLASS_MINERAL) continue;
		kp_sprite_pos(g, n, &sx, &sy);
		score = kp_ai_route_distance(g, x, y, sx, sy);
		if (score <= best && score <= REFERENCE_ROUTE) {
			best = score;
			found = n;
		}
	}
	return found != 0 ? found : fallback;
}

/* ClusterUnassignedResources 0x495890: new areas around the unassigned resources of a class, the heaviest seed
 * first; each area takes the unassigned resources of both kinds around its reference. */
static void cluster(KpGame *g, int category, uint8_t *klass)
{
	KpAreaSeed *cands = g->ai_scratch.area_seeds;
	KpAreas *a = &g->areas;
	int64_t n, count = 0, round;
	int k;
	for (n = g->lists[KP_LIST_ACTIVE].head, k = 0; n != 0; n = g->u.next[n], k++) {
		int64_t sx, sy;
		if (klass[k] != category) continue;
		kp_sprite_pos(g, n, &sx, &sy);
		cluster_box(sx, sy, cands[count].box);
		cands[count].unit = n;
		cands[count].weight = cluster_weight(g, n, cands[count].box);
		count++;
	}
	for (round = 0; round < count; round++) {
		int64_t best = 0, i, seen = 0, sum_x = 0, sum_y = 0, sx, sy, ref, rx, ry, box[4], area;
		int64_t min_x, max_x, min_y, max_y;
		for (i = 1; i < count; i++) {
			if (cands[i].weight > cands[best].weight) best = i;
		}
		cands[best].weight = 0;
		kp_sprite_pos(g, cands[best].unit, &sx, &sy);
		for (n = g->lists[KP_LIST_ACTIVE].head; n != 0; n = g->u.next[n]) {
			int64_t ux, uy;
			if (!candidate(g, n, sx, sy, cands[best].box)) continue;
			kp_sprite_pos(g, n, &ux, &uy);
			sum_x = kp_u32v(sum_x + ux);
			sum_y = kp_u32v(sum_y + uy);
			seen++;
		}
		if (seen == 0) continue;
		if (a->used >= CLUSTER_LAST_AREA) return;
		area = ++a->used;
		ref = reference(g, cands[best].unit, sum_x / seen, sum_y / seen, klass);
		cands[best].unit = ref;
		kp_sprite_pos(g, ref, &rx, &ry);
		a->x[area] = rx;
		a->y[area] = ry;
		cluster_box(rx, ry, box);
		memcpy(cands[best].box, box, sizeof box);
		min_x = max_x = rx;
		min_y = max_y = ry;
		for (n = g->lists[KP_LIST_ACTIVE].head, k = 0; n != 0; n = g->u.next[n], k++) {
			int64_t ux, uy;
			if (!candidate(g, n, rx, ry, box)) continue;
			if (klass[k] == CLASS_MINERAL) {
				klass[k] = CLASS_MINERAL_AREA;
				a->minerals[area] = kp_band(a->minerals[area] + 1, 0xff);
				a->mineral_total[area] = kp_u32v(a->mineral_total[area] + amount(g, n));
			} else if (klass[k] == CLASS_GAS) {
				klass[k] = CLASS_GAS_AREA;
				a->gas[area] = kp_band(a->gas[area] + 1, 0xff);
				a->gas_total[area] = kp_u32v(a->gas_total[area] + amount(g, n));
			} else {
				continue;
			}
			kp_union_set(g, n, R_AREA, 1, area);
			kp_sprite_pos(g, n, &ux, &uy);
			if (ux < min_x) min_x = ux;
			if (ux > max_x) max_x = ux;
			if (uy < min_y) min_y = uy;
			if (uy > max_y) max_y = uy;
		}
		a->x[area] = kp_s16v((min_x + max_x) / 2);
		a->y[area] = kp_s16v((min_y + max_y) / 2);
	}
}

void kp_ai_areas_generate(KpGame *g)
{
	uint8_t *klass = g->ai_scratch.area_class;
	KpAreas *a = &g->areas;
	int64_t p, n;
	int k;
	memset(a, 0, sizeof *a);
	for (p = 0; p < KP_AI_PLAYERS; p++) {
		int64_t x = g->players.start_x[p], y = g->players.start_y[p], area, controller;
		if (x == 0 && y == 0) continue;
		area = ++a->used;
		a->x[area] = kp_s16v(x);
		a->y[area] = kp_s16v(y);
		a->start[area] = 1;
		controller = kp_controller(g, p);
		if (controller == KP_CTRL_HUMAN || controller == KP_CTRL_COMPUTER) a->flags[area] = AREA_OCCUPIED;
	}
	memset(klass, 0, sizeof g->ai_scratch.area_class);
	for (n = g->lists[KP_LIST_ACTIVE].head, k = 0; n != 0; n = g->u.next[n], k++) {
		int64_t area;
		if (k >= KP_UNITS) kp_game_fail(g, "ai: more active units than the area generation holds");
		if (kp_unit_inactive(g, n) || !resource(g, n)) continue; /* IsUnitInactive 0x41fe40 */
		kp_union_set(g, n, R_AREA, 1, 0);
		klass[k] = mineral(g, n) ? CLASS_MINERAL : CLASS_GAS;
		area = start_area(g, n);
		if (area == 0) continue;
		if (mineral(g, n)) {
			a->mineral_total[area] = kp_u32v(a->mineral_total[area] + amount(g, n));
			a->minerals[area] = kp_band(a->minerals[area] + 1, 0xff);
			klass[k] = CLASS_MINERAL_AREA;
		} else {
			a->gas_total[area] = kp_u32v(a->gas_total[area] + amount(g, n));
			a->gas[area] = kp_band(a->gas[area] + 1, 0xff);
			klass[k] = CLASS_GAS_AREA;
		}
		kp_union_set(g, n, R_AREA, 1, area);
	}
	cluster(g, CLASS_GAS, klass);
	cluster(g, CLASS_MINERAL, klass);
}

void kp_ai_areas_refresh(KpGame *g)
{
	KpAreas *a = &g->areas;
	int64_t n, k;
	if (a->used == 0) return;
	if (a->countdown != 0) {
		a->countdown--;
		return;
	}
	a->countdown = REFRESH_PERIOD;
	for (k = 1; k <= a->used; k++) {
		a->flags[k] = kp_band(a->flags[k], kp_bnot(AREA_OCCUPIED)) & 0xff;
		a->minerals[k] = a->gas[k] = 0;
		a->mineral_total[k] = a->gas_total[k] = 0;
	}
	for (n = g->lists[KP_LIST_ACTIVE].head; n != 0; n = g->u.next[n]) {
		int64_t sx, sy;
		if (kp_unit_inactive(g, n)) continue;
		if (resource(g, n)) {
			int64_t area = kp_ai_resource_area(g, n);
			if (area == 0) continue;
			if (mineral(g, n)) {
				a->mineral_total[area] = kp_u32v(a->mineral_total[area] + amount(g, n));
				a->minerals[area] = kp_band(a->minerals[area] + 1, 0xff);
			} else {
				a->gas_total[area] = kp_u32v(a->gas_total[area] + amount(g, n));
				a->gas[area] = kp_band(a->gas[area] + 1, 0xff);
			}
			continue;
		}
		if (kp_band(flags_of(g, n), BUILDING) == 0) continue;
		kp_sprite_pos(g, n, &sx, &sy);
		for (k = 1; k <= a->used; k++) {
			if (kp_band(a->flags[k], AREA_OCCUPIED) != 0) continue;
			if (!kp_same_region_group(g, n, a->x[k], a->y[k])) continue; /* 0x481d00 */
			if (kp_approx_distance(sx - a->x[k], sy - a->y[k]) > OCCUPY_REACH) continue;
			if (kp_ai_route_distance(g, sx, sy, a->x[k], a->y[k]) > OCCUPY_ROUTE) continue;
			a->flags[k] = kp_bor(a->flags[k], AREA_OCCUPIED);
			break;
		}
	}
}

static void check_area(KpGame *g, int64_t area)
{
	if (area < 0 || area >= KP_AREAS) kp_game_fail(g, "ai: resource area %lld", (long long)area);
}

/* GetAreaMineralCount 0x496f20 (0 outside 1..249) and GetAreaGasCount 0x496f40 (unchecked in the original). */
int64_t kp_ai_area_minerals(KpGame *g, int64_t area)
{
	return area > 0 && area < KP_AREAS ? g->areas.minerals[area] : 0;
}

int64_t kp_ai_area_gas(KpGame *g, int64_t area)
{
	check_area(g, area);
	return g->areas.gas[area];
}

/* 0x497380: the area is no longer reserved. */
void kp_ai_area_release(KpGame *g, int64_t area)
{
	check_area(g, area);
	g->areas.flags[area] = kp_band(g->areas.flags[area], kp_bnot(AREA_RESERVED)) & 0xff;
}

/* TryReserveArea 0x497360: a free area becomes reserved (nonzero then). */
int kp_ai_area_reserve(KpGame *g, int64_t area)
{
	check_area(g, area);
	if (kp_band(g->areas.flags[area], AREA_OCCUPIED | AREA_RESERVED) != 0) return 0;
	g->areas.flags[area] = kp_bor(g->areas.flags[area], AREA_RESERVED);
	return 1;
}

#define EXPANSION_CATEGORIES 9
#define EXPANSION_ROW 10
#define EXPANSION_END 9
#define EXPANSION_NONE 99999999
#define EXPANSION_ANY 8 /* the category every free area enters first */
#define RICH_AREA_GAS 1000
#define RICH_AREA_MINERALS 2000
#define BALANCED_STOCKS 500

/* The categories' order by row (0x508fc8): row 0 for mode 1, row 1 when minerals lead gas by more than 500, else
 * row 2. */
static const int EXPANSION_ORDER[3][EXPANSION_ROW] = {
	{ 8, 0, 1, 3, 2, 5, 4, 6, 9, 0 },
	{ 0, 3, 5, 1, 2, 4, 7, 6, 9, 0 },
	{ 0, 1, 2, 3, 4, 5, 6, 7, 9, 0 },
};

typedef struct {
	int64_t ground[EXPANSION_CATEGORIES], air[EXPANSION_CATEGORIES];
	int64_t ground_area[EXPANSION_CATEGORIES], air_area[EXPANSION_CATEGORIES];
} Expansion;

/* IsRouteInteriorSafe 0x496ac0 (native-ai-routes.md): the regions strictly inside the route from src to dst are not
 * abandoned, targeted or under attack; other groups, no route and short routes pass. */
int kp_ai_route_interior_safe(KpGame *g, int64_t p, int64_t src, int64_t dst)
{
	KpAiRegions *a = &g->air;
	KpRoute c;
	int64_t sx, sy, dx, dy, i, r;
	if (src == dst || kp_region_group_of(g, src) != kp_region_group_of(g, dst)) return 1;
	kp_ai_region_center(g, src, &sx, &sy); /* GetRegionCenter 0x481a30 */
	kp_ai_region_center(g, dst, &dx, &dy);
	kp_route_new(&c, 0, sx, sy, dx, dy);
	c.goal_x = dx;
	c.goal_y = dy;
	c.src_region = src;
	c.dst_region = dst;
	if (!kp_search_region_path(g, &c) || c.region_count <= 2) return 1; /* SearchRegionPath 0x491300 */
	for (i = 1; i < c.region_count - 1; i++) {
		r = c.regions[i];
		if (a->state[p][r] == KP_RSTATE_ABANDONED || a->target_air[p][r] != 0 || a->target_ground[p][r] != 0
			|| kp_band(a->flags[p][r], KP_RFLAG_UNDER_ATTACK) != 0) {
			return 0; /* IsRegionUnthreatened 0x4aea70 */
		}
	}
	return 1;
}

/* RecordExpansionCandidate 0x497390: the nearest area of each category, by route tiles when the military reaches
 * it (over a safe route or without transports), else by straight pixels with transports; ties go to the later. */
static void record_candidate(KpGame *g, Expansion *e, int64_t p, int64_t area, int64_t home, int transport, int cat)
{
	KpAreas *ar = &g->areas;
	int64_t hx = kp_s16v(ar->x[home]), hy = kp_s16v(ar->y[home]), cx = kp_s16v(ar->x[area]), cy = kp_s16v(ar->y[area]);
	int64_t from = kp_region_at(g, hx, hy), to = kp_region_at(g, cx, cy), d;
	int ground;
	if (kp_ai_military_reachable(g, p, from, to)) ground = !transport || kp_ai_route_interior_safe(g, p, from, to); /* 0x4ab2f0 */
	else if (transport) ground = 0;
	else return;
	if (ground) {
		d = kp_ai_route_distance(g, hx, hy, cx, cy); /* 0x496930 */
		if (kp_u32v(d) > kp_u32v(e->ground[cat])) return;
		e->ground[cat] = d;
		e->ground_area[cat] = area;
	} else {
		d = kp_approx_distance(cx - hx, cy - hy);
		if (kp_u32v(d) > kp_u32v(e->air[cat])) return;
		e->air[cat] = d;
		e->air_area[cat] = area;
	}
}

/* ChooseExpansionArea 0x4970e0: player p's next free area from home: each area's nearest by category (its resources
 * left), then the first category of the row (mode 1, or the player's stocks) with a candidate; the chosen area is
 * reserved. 0 for none. */
int64_t kp_ai_choose_expansion(KpGame *g, int64_t p, int64_t home, int64_t mode)
{
	KpAreas *ar = &g->areas;
	Expansion e;
	int64_t a, m, gas, minerals = kp_s32v(g->players.minerals[p]), stock_gas = kp_s32v(g->players.gas[p]), found;
	int transport, cat, row, k;
	for (k = 0; k < EXPANSION_CATEGORIES; k++) e.ground[k] = e.air[k] = EXPANSION_NONE;
	transport = kp_ai_has_transports(g, p); /* 0x4aa8a0 */
	for (a = 1; a <= ar->used; a++) {
		if (kp_band(ar->flags[a], AREA_OCCUPIED | AREA_RESERVED) != 0) continue;
		record_candidate(g, &e, p, a, home, transport, EXPANSION_ANY);
		m = kp_u32v(ar->mineral_total[a]);
		gas = kp_u32v(ar->gas_total[a]);
		if (gas > RICH_AREA_GAS) cat = m > RICH_AREA_MINERALS ? 0 : m != 0 ? 3 : 5;
		else if (ar->gas[a] != 0) cat = m > RICH_AREA_MINERALS ? 1 : m != 0 ? 4 : 7;
		else if (m == 0) continue;
		else cat = m > RICH_AREA_MINERALS ? 2 : 6;
		record_candidate(g, &e, p, a, home, transport, cat);
	}
	if (mode == 1) row = 0;
	else if (kp_u32v(minerals > stock_gas ? minerals - stock_gas : stock_gas - minerals) <= BALANCED_STOCKS) row = 2;
	else row = minerals > stock_gas ? 1 : 2;
	for (k = 0; EXPANSION_ORDER[row][k] != EXPANSION_END; k++) {
		cat = EXPANSION_ORDER[row][k];
		if (e.ground[cat] != EXPANSION_NONE) found = e.ground_area[cat];
		else if (e.air[cat] != EXPANSION_NONE) found = e.air_area[cat];
		else continue;
		if (found == 0) return 0;
		ar->flags[found] = kp_bor(ar->flags[found], AREA_RESERVED);
		return found;
	}
	return 0;
}

/* FindNearestArea 0x496bf0: the area nearest to (x, y), the later one on a tie, 0 for none; a non-campaign player
 * measures from its start location over the first START_AREAS records. */
int64_t kp_ai_nearest_area(KpGame *g, int64_t p, int64_t x, int64_t y)
{
	int64_t count = g->areas.used, best = NEAREST_NONE, found = 0, k;
	if (!kp_ai_campaign(g, p)) { /* 0x4831e0 */
		x = kp_s16v(g->players.start_x[p]);
		y = kp_s16v(g->players.start_y[p]);
		count = START_AREAS;
	}
	for (k = 1; k <= count; k++) {
		int64_t d = kp_approx_distance(x - g->areas.x[k], y - g->areas.y[k]);
		if (d <= best) {
			best = d;
			found = k;
		}
	}
	return found;
}

/* FindFirstMineralInArea 0x4964b0: the first live mineral field of the area in the active list, 0 for none. */
int64_t kp_ai_area_mineral(KpGame *g, int64_t area)
{
	int64_t n;
	for (n = g->lists[KP_LIST_ACTIVE].head; n != 0; n = g->u.next[n]) {
		if (!kp_unit_inactive(g, n) && mineral(g, n) && kp_ai_resource_area(g, n) == area) return n;
	}
	return 0;
}

/* FindAreaGasObjects 0x496510: the area's live resources other than mineral fields, the first two kept and the third
 * slot overwritten by every later one, then sorted by their distance from the area (a stable sort of three). */
void kp_ai_area_gas_objects(KpGame *g, int64_t area, int64_t out[3])
{
	int64_t n, d[3], e, t;
	check_area(g, area);
	out[0] = out[1] = out[2] = 0;
	for (n = g->lists[KP_LIST_ACTIVE].head; n != 0; n = g->u.next[n]) {
		if (kp_unit_inactive(g, n) || !resource(g, n) || mineral(g, n) || kp_ai_resource_area(g, n) != area) continue;
		if (out[0] == 0) out[0] = n;
		else if (out[1] == 0) out[1] = n;
		else out[2] = n;
	}
	for (e = 0; e < 3; e++) {
		int64_t sx, sy;
		if (out[e] == 0) {
			d[e] = NEAREST_NONE;
			continue;
		}
		kp_sprite_pos(g, out[e], &sx, &sy);
		d[e] = kp_approx_distance(sx - g->areas.x[area], sy - g->areas.y[area]);
	}
#define SWAP(a, b) (t = out[a], out[a] = out[b], out[b] = t, t = d[a], d[a] = d[b], d[b] = t)
	if (d[0] > d[1]) SWAP(0, 1);
	if (d[1] > d[2]) SWAP(1, 2);
	if (d[0] > d[1]) SWAP(0, 1);
#undef SWAP
}

/* 0x486130: the mineral fields of the areas of player p's towns (an area as often as towns have it). */
static int64_t town_mineral_fields(KpGame *g, int64_t p)
{
	int64_t k, sum = 0;
	for (k = g->towns.heads[p]; k != TOWN_NONE; k = g->towns.next[k]) {
		if (g->towns.area[k] != 0) sum += kp_ai_area_minerals(g, g->towns.area[k]);
	}
	return sum;
}

/* GetAreaWorkerLimit 0x496690 (with_gas) and 0x4966f0: the workers player p wants at the area, 0 outside 1..249. While
 * a field's share of the remaining minerals stays below the fields of the player's towns, one worker a field. */
int64_t kp_ai_area_worker_limit(KpGame *g, int64_t area, int64_t p, int with_gas)
{
	const KpAreas *a = &g->areas;
	int64_t share, limit;
	if (area <= 0 || area >= KP_AREAS) return 0;
	share = a->mineral_total[area] / LIMIT_MINERALS;
	if (share < town_mineral_fields(g, p)) {
		limit = a->minerals[area];
	} else {
		limit = share > LIMIT_PER_FIELD * a->minerals[area] ? LIMIT_PER_FIELD * a->minerals[area] : share;
	}
	return with_gas ? limit + LIMIT_PER_GEYSER * a->gas[area] : limit;
}

#define HATCHERY_SEED_STEP 192

/* GetHatcherySeedForArea 0x496f50: a point 192 from the area's farthest resource towards the area's point when that
 * is nearer the resource than the area's point is, else the area's point. */
void kp_ai_hatchery_seed(KpGame *g, int64_t area, int64_t *x, int64_t *y)
{
	const KpAreas *a = &g->areas;
	int64_t n, far = 0, best = 0, rx, ry, dx, dy, cx, cy;
	check_area(g, area);
	for (n = g->lists[KP_LIST_ACTIVE].head; n != 0; n = g->u.next[n]) {
		int64_t sx, sy, d;
		if (kp_unit_inactive(g, n) || !resource(g, n) || kp_ai_resource_area(g, n) != area) continue;
		kp_sprite_pos(g, n, &sx, &sy);
		d = kp_u32v(kp_approx_distance(sx - a->x[area], sy - a->y[area]));
		if (d >= best) {
			best = d;
			far = n;
		}
	}
	*x = a->x[area];
	*y = a->y[area];
	if (far == 0) return;
	kp_sprite_pos(g, far, &rx, &ry);
	kp_direction_vector(kp_direction_to(g, rx, ry, a->x[area], a->y[area]), HATCHERY_SEED_STEP, &dx, &dy); /* 0x40ba10 */
	cx = kp_s32v(rx + dx);
	cy = kp_s32v(ry + dy);
	if (kp_u32v(kp_approx_distance(cx - rx, cy - ry)) < kp_u32v(kp_approx_distance(a->x[area] - rx, a->y[area] - ry))) {
		*x = kp_s16v(cx);
		*y = kp_s16v(cy);
	}
}
