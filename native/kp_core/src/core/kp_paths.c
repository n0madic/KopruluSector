/* The path record pool, the search heap, the region graph with its long search and a unit's route
 * (kp_pathing.h). */
#include "kp_pathing_internal.h"

#include <stdlib.h>
#include <string.h>

#define STATUS_PATHED 0x100000
#define STATUS_INTERCEPT 0x80000
#define FLAG_PATHING 0x10
#define FLAG_NEAR_GOAL 0x40
#define NEAR_GOAL 0x10
#define KEEP_DISTANCE 0x10
#define UNTARGETED_FIRST 0xc2
#define UNTARGETED_LAST 0xc7
#define NIL INT64_MIN

static int64_t ext(KpGame *g, int64_t i) { return KP_DAT(g, units, unit_extents, i); }

/* --- shared helpers --- */

int64_t kp_rgns_read(KpGame *g, int64_t off, int n)
{
	const uint8_t *s = g->regions.data.data;
	int64_t len = (int64_t)g->regions.data.size, i = off + 1, j = off + n, v = 0;
	int k;
	if (j < 0) j += len + 1;
	else if (j > len) j = len;
	if (i < 0) {
		i += len + 1;
		if (i < 1) i = 1;
	} else if (i == 0) {
		i = 1;
	}
	if (!s || j - i + 1 < n) kp_game_fail(g, "regions: RGNS read at %lld outside the section", (long long)off);
	for (k = n - 1; k >= 0; k--) v = v * 256 + s[i - 1 + k];
	return v;
}

void kp_sprite_pos(KpGame *g, int64_t n, int64_t *x, int64_t *y)
{
	int32_t s = kp_unit_sprite(g, n);
	*x = kp_s16v(g->w->spr.x[s]);
	*y = kp_s16v(g->w->spr.y[s]);
}

int64_t kp_rg_check(KpGame *g, int64_t id)
{
	if (id < 0 || id >= g->pathing.rg.count) kp_game_fail(g, "regions: region %lld out of range", (long long)id);
	return id;
}

/* --- the search heap --- */

#define PRIO(h, node) (*(int64_t *)(void *)((h)->base + (size_t)(node) * (h)->size + (h)->offset))

static void sift_up(const KpHeap *h, int64_t i, int64_t node)
{
	int64_t p;
	h->slots[0] = h->sentinel;
	p = i / 2;
	while (PRIO(h, h->slots[p]) >= PRIO(h, node)) {
		h->slots[i] = h->slots[p];
		i = p;
		p = i / 2;
	}
	h->slots[i] = node;
}

void kp_heap_insert(KpGame *g, const KpHeap *h, int64_t node)
{
	if (*h->count >= h->capacity) kp_game_fail(g, "heap: full");
	*h->count = *h->count + 1;
	sift_up(h, *h->count, node);
}

void kp_heap_sift_down(const KpHeap *h, int64_t i)
{
	int64_t node = h->slots[i], count = *h->count;
	while (i <= count / 2) {
		int64_t c = i * 2;
		if (c < count && PRIO(h, h->slots[c]) > PRIO(h, h->slots[c + 1])) c = c + 1;
		if (PRIO(h, node) <= PRIO(h, h->slots[c])) {
			h->slots[i] = node;
			return;
		}
		h->slots[i] = h->slots[c];
		i = c;
	}
	h->slots[i] = node;
}

int64_t kp_heap_pop(const KpHeap *h)
{
	int64_t top = h->slots[1];
	h->slots[1] = h->slots[*h->count];
	*h->count = *h->count - 1;
	kp_heap_sift_down(h, 1);
	return top;
}

void kp_heap_change(const KpHeap *h, int64_t node, int64_t prio)
{
	int64_t old = PRIO(h, node), i;
	if (old == prio) return;
	PRIO(h, node) = prio;
	i = *h->count;
	while (i > 0 && h->slots[i] != node) i = i - 1;
	if (i == 0) return;
	if (old <= prio) kp_heap_sift_down(h, i);
	else sift_up(h, i, node);
}

/* --- the path record pool --- */

static int64_t byte_at(KpGame *g, int64_t k, int64_t off)
{
	int64_t at = (k - 1) * KP_PATH_SIZE + off;
	if (at < 0 || at >= (int64_t)KP_PATH_RECORDS * KP_PATH_SIZE)
		kp_game_fail(g, "pool: byte %lld of path record %lld outside the pool", (long long)off, (long long)k);
	return at;
}

/* CreatePathPool 0x48f000. */
void kp_paths_init(KpGame *g)
{
	int64_t k;
	memset(g->pathing.path_bytes, 0, sizeof g->pathing.path_bytes);
	for (k = 1; k <= KP_PATH_RECORDS; k++) {
		kp_path_w32(g, k, 0, k < KP_PATH_RECORDS ? k + 1 : 0);
		kp_path_w32(g, k, 4, k);
	}
	kp_scalar_set(g, KP_PATH_FREE_HEAD, 1);
	kp_scalar_set(g, KP_PATH_SUBJECT, 0);
	kp_scalar_set(g, KP_PATH_ALL_UNITS, 0);
}

int64_t kp_path_u8(KpGame *g, int64_t k, int64_t off) { return g->pathing.path_bytes[byte_at(g, k, off)]; }

int64_t kp_path_s8(KpGame *g, int64_t k, int64_t off) { return kp_s8((uint32_t)kp_path_u8(g, k, off)); }

void kp_path_w8(KpGame *g, int64_t k, int64_t off, int64_t v)
{
	g->pathing.path_bytes[byte_at(g, k, off)] = (uint8_t)kp_mod(v, 256);
}

int64_t kp_path_u16(KpGame *g, int64_t k, int64_t off)
{
	int64_t at = byte_at(g, k, off), at1 = byte_at(g, k, off + 1);
	return g->pathing.path_bytes[at] + g->pathing.path_bytes[at1] * 256;
}

int64_t kp_path_s16(KpGame *g, int64_t k, int64_t off) { return kp_s16v(kp_path_u16(g, k, off)); }

void kp_path_w16(KpGame *g, int64_t k, int64_t off, int64_t v)
{
	int64_t at = byte_at(g, k, off), at1 = byte_at(g, k, off + 1);
	v = kp_mod(v, 65536);
	g->pathing.path_bytes[at] = (uint8_t)(v % 256);
	g->pathing.path_bytes[at1] = (uint8_t)kp_shrv(v, 8);
}

int64_t kp_path_u32(KpGame *g, int64_t k, int64_t off)
{
	return kp_path_u16(g, k, off) + kp_path_u16(g, k, off + 2) * 65536;
}

void kp_path_w32(KpGame *g, int64_t k, int64_t off, int64_t v)
{
	v = kp_u32v(v);
	kp_path_w16(g, k, off, v % 65536);
	kp_path_w16(g, k, off + 2, v / 65536);
}

void kp_path_point(KpGame *g, int64_t k, int64_t off, int64_t *x, int64_t *y)
{
	*x = kp_path_s16(g, k, off);
	*y = kp_path_s16(g, k, off + 2);
}

void kp_path_set_point(KpGame *g, int64_t k, int64_t off, int64_t x, int64_t y)
{
	kp_path_w16(g, k, off, x);
	kp_path_w16(g, k, off + 2, y);
}

/* AllocPathRecord 0x48e8d0. */
int64_t kp_path_alloc(KpGame *g, int64_t size)
{
	int64_t k = kp_scalar(g, KP_PATH_FREE_HEAD);
	if (size > KP_PATH_MAX_REQUEST || k == 0) return 0;
	kp_scalar_set(g, KP_PATH_FREE_HEAD, kp_path_u32(g, k, 0));
	return k;
}

/* FreePathRecord 0x48e910. */
void kp_path_release(KpGame *g, int64_t k)
{
	kp_path_w32(g, k, 4, k);
	kp_path_w32(g, k, 0, kp_scalar(g, KP_PATH_FREE_HEAD));
	kp_scalar_set(g, KP_PATH_FREE_HEAD, k);
}

/* ReleaseUnitPath 0x48e880 / FreeUnitPath 0x42c8a0. */
void kp_path_free(KpGame *g, int64_t n)
{
	int64_t k = g->u.path[kp_game_unit(g, n)];
	if (k == 0) return;
	kp_path_release(g, k);
	g->u.path[n] = 0;
}

/* ClampPathSizeAndAllocate 0x42c840. */
int64_t kp_path_alloc_counts(KpGame *g, int64_t regions, int64_t points, int64_t *out_regions, int64_t *out_points)
{
	if (kp_band((regions + points * 2) * 2 + 0x23, 0xfffc) > KP_PATH_SIZE) {
		if (points < 0x18) {
			regions = (0x18 - points) * 2;
		} else {
			points = KP_PATH_MAX_POINTS;
			regions = 1;
		}
	}
	*out_regions = regions;
	*out_points = points;
	return kp_path_alloc(g, kp_band((regions + points * 2) * 2 + 0x23, 0xfffc));
}

/* FlagPathMoveTargetChanged 0x42c790. */
void kp_path_mark_stale(KpGame *g, int64_t n)
{
	int64_t k = g->u.path[kp_game_unit(g, n)];
	if (k != 0) kp_path_w8(g, k, KP_PATH_FLAGS, kp_bor(kp_path_u8(g, k, KP_PATH_FLAGS), KP_PATH_STALE));
}

/* SetPathRetryTimer 0x48e970. */
void kp_path_set_retry(KpGame *g, int64_t n, int64_t v)
{
	int64_t k = g->u.path[kp_game_unit(g, n)];
	if (k != 0) kp_path_w8(g, k, KP_PATH_RETRY, v);
}

/* PollPathRetryTimer 0x48e950. */
int kp_path_poll_retry(KpGame *g, int64_t n)
{
	int64_t k = g->u.path[kp_game_unit(g, n)], t;
	if (k == 0) return 0;
	t = kp_path_u8(g, k, KP_PATH_RETRY);
	if (t == 0) return 1;
	kp_path_w8(g, k, KP_PATH_RETRY, t - 1);
	return 0;
}

int64_t kp_path_next_packed(KpGame *g, int64_t k) { return kp_path_u32(g, k, KP_PATH_NEXT); }

/* --- the region graph --- */

void kp_pathing_free(KpGame *g)
{
	free(g->pathing.rg.block);
	memset(&g->pathing.rg, 0, sizeof g->pathing.rg);
}

/* The region arrays of the mission's RGNS section. */
void kp_regions_init(KpGame *g)
{
	const KpMissionRegions *r = &g->regions;
	KpRegionGraph *rg = &g->pathing.rg;
	int64_t count = r->count, total = 0, i, k, at, fields = 14;
	int64_t *b;
	kp_pathing_free(g);
	for (i = 0; i < count; i++) total += kp_rgns_read(g, r->regions_at + i * KP_REGION_SIZE + 7, 1);
	b = (int64_t *)calloc((size_t)(count * fields + total + 1), sizeof *b);
	if (!b) kp_game_fail(g, "regions: out of memory");
	rg->block = b;
	rg->count = count;
	rg->klass = b;
	rg->group = b + count;
	rg->ground = b + count * 2;
	rg->nb_count = b + count * 3;
	rg->nb_at = b + count * 4;
	rg->center_x = b + count * 5;
	rg->center_y = b + count * 6;
	rg->left = b + count * 7;
	rg->top = b + count * 8;
	rg->right = b + count * 9;
	rg->bottom = b + count * 10;
	rg->node = b + count * 11;
	rg->mark = b + count * 12;
	/* b + count * 13: spare */
	rg->neighbors = b + count * fields;
	at = 0;
	for (i = 0; i < count; i++) {
		int64_t a = r->regions_at + i * KP_REGION_SIZE, n, offset;
		rg->klass[i] = kp_rgns_read(g, a, 2);
		rg->group[i] = kp_rgns_read(g, a + 2, 2);
		rg->ground[i] = kp_rgns_read(g, a + 6, 1);
		n = kp_rgns_read(g, a + 7, 1);
		offset = kp_rgns_read(g, a + 12, 4);
		rg->nb_at[i] = at;
		for (k = 0; k < n; k++) rg->neighbors[at + k] = kp_rgns_read(g, r->pool_at + 2 * (offset + k), 2);
		rg->nb_count[i] = n;
		at += n;
		rg->center_x[i] = kp_s32v(kp_rgns_read(g, a + 16, 4));
		rg->center_y[i] = kp_s32v(kp_rgns_read(g, a + 20, 4));
		rg->left[i] = kp_s16v(kp_rgns_read(g, a + 24, 2));
		rg->top[i] = kp_s16v(kp_rgns_read(g, a + 26, 2));
		rg->right[i] = kp_s16v(kp_rgns_read(g, a + 28, 2));
		rg->bottom[i] = kp_s16v(kp_rgns_read(g, a + 30, 2));
		rg->node[i] = -1;
		rg->mark[i] = 0;
	}
}

/* GetRegionGroup 0x481c90. */
int64_t kp_region_group_of(KpGame *g, int64_t id) { return g->pathing.rg.group[kp_rg_check(g, id)]; }

/* SameRegionGroup 0x481cb0. */
int kp_same_ground_group(KpGame *g, int64_t a, int64_t b)
{
	int64_t ax, ay, bx, by, ga;
	kp_sprite_pos(g, b, &bx, &by);
	ga = kp_region_group_of(g, kp_region_at(g, bx, by));
	kp_sprite_pos(g, a, &ax, &ay);
	return kp_region_group_of(g, kp_region_at(g, ax, ay)) == ga;
}

/* GetRegionGroundNeighbors 0x481ab0. */
int kp_region_ground_neighbors(KpGame *g, int64_t id, int64_t *out, int max)
{
	const KpRegionGraph *rg = &g->pathing.rg;
	int64_t k, ground = rg->ground[kp_rg_check(g, id)];
	int n = 0;
	for (k = 0; k < ground; k++) {
		if (k >= rg->nb_count[id]) continue; /* past the region's list: nothing to append */
		if (n >= max) kp_game_fail(g, "regions: too many neighbours");
		out[n++] = rg->neighbors[rg->nb_at[id] + k];
	}
	return n;
}

/* GetRegionBox 0x481a00. */
void kp_region_box(KpGame *g, int64_t id, int64_t *left, int64_t *top, int64_t *right, int64_t *bottom)
{
	const KpRegionGraph *rg = &g->pathing.rg;
	kp_rg_check(g, id);
	*left = rg->left[id];
	*top = rg->top[id];
	*right = rg->right[id];
	*bottom = rg->bottom[id];
}

/* IsRegionIdInaccessible 0x481c10 (an id without a region is not of the class). */
int kp_region_inaccessible(KpGame *g, int64_t id)
{
	const KpRegionGraph *rg = &g->pathing.rg;
	return id < KP_REGION_SPLIT && id >= 0 && id < rg->count && rg->klass[id] == KP_REGION_UNWALKABLE;
}

/* FindReachableSubstituteRegion 0x48e690. */
int64_t kp_reachable_substitute_region(KpGame *g, int64_t sx, int64_t sy, int64_t dx, int64_t dy)
{
	KpRoute c;
	kp_route_new(&c, 0, sx, sy, dx, dy);
	c.goal_x = dx;
	c.goal_y = dy;
	c.src_region = kp_region_at(g, sx, sy);
	c.dst_region = kp_region_at(g, dx, dy);
	if (c.src_region == c.dst_region) return c.dst_region;
	if (!kp_search_region_path(g, &c)) return c.dst_region;
	if (c.region_count - 1 < 0) kp_game_fail(g, "regions: no region stored");
	return c.regions[c.region_count - 1];
}

/* mission_data.region_cell. */
int64_t kp_region_cell(KpGame *g, int64_t x, int64_t y)
{
	const KpMissionRegions *r = &g->regions;
	return kp_rgns_read(g, r->raster_at + 2 * (y * (int64_t)r->width + x), 2);
}

/* GetTileRegionFirst 0x481c40. */
int64_t kp_tile_region_first(KpGame *g, int64_t x, int64_t y)
{
	int64_t v = kp_region_cell(g, kp_sarv(x, 5), kp_sarv(y, 5));
	if (v < KP_REGION_SPLIT) return v;
	return kp_rgns_read(g, g->regions.splits_at + (v - KP_REGION_SPLIT) * 6 + 2, 2);
}

static int64_t approx(int64_t x1, int64_t y1, int64_t x2, int64_t y2) { return kp_approx_distance(x1 - x2, y1 - y2); }

/* The cost of entering a region outside the source group doubles. */
static int64_t scaled(KpGame *g, int64_t cost) { return kp_idiv64(g, kp_s32v(kp_shlv(cost, 9)), 256); }

/* The node position of a region: the source or destination point for those regions, else its centre. */
static void position(const KpRegionGraph *rg, int64_t id, int64_t src, int64_t target, int64_t sx, int64_t sy,
	int64_t dx, int64_t dy, int64_t *px, int64_t *py)
{
	if (id == src) {
		*px = sx;
		*py = sy;
	} else if (id == target) {
		*px = dx;
		*py = dy;
	} else {
		*px = rg->center_x[id];
		*py = rg->center_y[id];
	}
}

static KpHeap region_heap(KpPathing *p)
{
	KpHeap h;
	h.slots = p->rheap;
	h.count = &p->rheap_count;
	h.base = (char *)(void *)p->rnodes;
	h.size = sizeof(KpRegionNode);
	h.offset = offsetof(KpRegionNode, prio);
	h.sentinel = KP_REGION_ARENA + 1;
	h.capacity = KP_REGION_OPEN_LIMIT + 1;
	return h;
}

/* SearchRegionPath 0x491300. */
int kp_search_region_path(KpGame *g, KpRoute *c)
{
	KpPathing *p = &g->pathing;
	KpRegionGraph *rg = &p->rg;
	KpRegionNode *nodes = p->rnodes;
	KpHeap open = region_heap(p);
	int64_t src = c->src_region, target = c->dst_region, sx, sy, dx, dy, src_group, best = -1, used = 0, n, k;
	int restarted = 0, forward = 0;
	if (src == target) return 0;
	c->partial = 0;
	sx = kp_shlv(c->src_x, 8);
	sy = kp_shlv(c->src_y, 8);
	dx = kp_shlv(c->dest_x, 8);
	dy = kp_shlv(c->dest_y, 8);
	src_group = rg->group[kp_rg_check(g, src)];
	nodes[KP_REGION_ARENA + 1].prio = -1;
	for (;;) {
		int64_t start, goal, gx, gy, found = -1;
		int exhausted;
		c->long_nodes = 0;
		c->long_peak = 0;
		forward = src_group == rg->group[kp_rg_check(g, target)] || restarted;
		if (forward) {
			start = src;
			goal = target;
			gx = dx;
			gy = dy;
		} else {
			start = target;
			goal = src;
			gx = sx;
			gy = sy;
		}
		p->rheap_count = 0;
		nodes[0].closed = 0;
		nodes[0].region = start;
		nodes[0].parent = -1;
		nodes[0].heur = KP_REGION_START_COST;
		nodes[0].cost = 0;
		nodes[0].prio = KP_REGION_START_COST;
		used = 1;
		rg->node[start] = 0;
		kp_heap_insert(g, &open, 0);
		exhausted = p->rheap_count == 0;
		while (!exhausted) {
			int64_t cur = kp_heap_pop(&open), id, count, cx, cy;
			if (nodes[cur].region == goal) {
				found = cur;
				break;
			}
			nodes[cur].closed = 1;
			id = nodes[cur].region;
			count = rg->group[id] == src_group ? rg->ground[id] : rg->nb_count[id];
			position(rg, id, src, target, sx, sy, dx, dy, &cx, &cy);
			for (k = 0; k < count; k++) {
				int64_t nb, node, px, py, cost;
				if (k >= rg->nb_count[id]) kp_game_fail(g, "regions: neighbour %lld of region %lld", (long long)k, (long long)id);
				nb = kp_rg_check(g, rg->neighbors[rg->nb_at[id] + k]);
				node = rg->node[nb];
				if (node == -1) {
					node = used;
					nodes[node].region = nb;
					nodes[node].parent = cur;
					used = used + 1;
					rg->node[nb] = node;
					position(rg, nb, src, target, sx, sy, dx, dy, &px, &py);
					cost = approx(cx, cy, px, py);
					if (rg->group[nb] != src_group) cost = scaled(g, cost);
					nodes[node].heur = approx(px, py, gx, gy);
					nodes[node].closed = 0;
					nodes[node].cost = kp_s32v(nodes[cur].cost + cost);
					nodes[node].prio = kp_s32v(nodes[node].heur + nodes[node].cost);
					kp_heap_insert(g, &open, node);
					if (p->rheap_count == KP_REGION_OPEN_LIMIT || used == KP_REGION_ARENA + 1) break;
				} else if (nodes[cur].parent != node) {
					int64_t nw;
					position(rg, nodes[node].region, src, target, sx, sy, dx, dy, &px, &py);
					cost = approx(px, py, cx, cy);
					if (rg->group[nodes[node].region] != src_group) cost = scaled(g, cost);
					nw = kp_s32v(nodes[cur].cost + cost);
					if (nw < nodes[node].cost) {
						int64_t prio;
						nodes[node].parent = cur;
						nodes[node].cost = nw;
						prio = kp_s32v(nodes[node].heur + nw);
						if (nodes[node].closed == 0) {
							kp_heap_change(&open, node, prio);
						} else {
							nodes[node].prio = prio;
							nodes[node].closed = 0;
							kp_heap_insert(g, &open, node);
						}
						if (p->rheap_count == KP_REGION_OPEN_LIMIT) break;
					}
				}
			}
			if (c->long_peak < p->rheap_count) c->long_peak = p->rheap_count;
			if (p->rheap_count == KP_REGION_OPEN_LIMIT || used == KP_REGION_ARENA + 1) {
				/* Out of capacity: the best open node is the result. */
				if (p->rheap_count != 0) found = kp_heap_pop(&open);
				break;
			}
			exhausted = p->rheap_count == 0;
		}
		best = found;
		if (best == -1) {
			/* No route: the visited node nearest the goal (allocation order on ties). */
			int64_t d;
			c->partial = 1;
			best = 0;
			d = approx(rg->center_x[start], rg->center_y[start], gx, gy);
			nodes[0].heur = d;
			for (k = 1; k <= used - 1; k++) {
				if (nodes[k].heur < d) {
					best = k;
					d = nodes[k].heur;
				}
			}
		}
		if (forward || nodes[best].region == goal) break;
		/* The reverse search did not reach the source: search forward towards the region it reached. */
		for (k = 0; k <= used - 1; k++) rg->node[nodes[k].region] = -1;
		target = nodes[best].region;
		restarted = 1;
	}
	c->long_nodes = used;
	n = 0;
	if (forward) {
		int64_t node = best, last, node2;
		while (node != -1) {
			n = n + 1;
			node = nodes[node].parent;
		}
		last = n - 1;
		if (last >= KP_REGION_MAX_REGIONS) {
			for (k = 1; k <= last - (KP_REGION_MAX_REGIONS - 1); k++) best = nodes[best].parent;
			last = KP_REGION_MAX_REGIONS - 1;
		}
		node2 = best;
		for (k = last; k >= 0; k--) {
			c->regions[k] = nodes[node2].region;
			node2 = nodes[node2].parent;
		}
	} else {
		int64_t node = best;
		while (node != -1 && rg->group[nodes[node].region] == src_group) {
			if (n < KP_REGION_MAX_REGIONS) c->regions[n] = nodes[node].region;
			node = nodes[node].parent;
			n = n + 1;
		}
	}
	c->full_count = n % 256;
	c->region_count = n < KP_REGION_MAX_REGIONS ? n : KP_REGION_MAX_REGIONS;
	c->region_cursor = -1;
	for (k = 0; k <= used - 1; k++) rg->node[nodes[k].region] = -1;
	return n > 0;
}

/* --- a unit's route --- */

/* The spiral steps of 0x492c00 and 0x492a90. */
static const int64_t DX[4] = { 1, 0, -1, 0 };
static const int64_t DY[4] = { 0, 1, 0, -1 };

void kp_route_new(KpRoute *c, int64_t unit, int64_t sx, int64_t sy, int64_t dx, int64_t dy)
{
	memset(c, 0, sizeof *c);
	c->unit = unit;
	c->src_x = sx;
	c->src_y = sy;
	c->dest_x = dx;
	c->dest_y = dy;
}

/* c.regions[i]; NIL outside the route's capacity. */
static int64_t route_region(const KpRoute *c, int64_t i)
{
	if (i < 0 || i >= KP_ROUTE_REGIONS) return NIL;
	return c->regions[i];
}

/* The fields both initializers set. */
static void prepare(KpGame *g, KpRoute *c)
{
	int64_t t;
	c->src_region = kp_region_at(g, c->src_x, c->src_y);
	c->dst_region = kp_region_at(g, c->dest_x, c->dest_y);
	c->goal_x = c->dest_x;
	c->goal_y = c->dest_y;
	c->partial = 0;
	c->local_partial = 0;
	c->reached = 0;
	c->reached_end = 0;
	t = g->u.type[kp_game_unit(g, c->unit)];
	c->ext_left = kp_s16v(-ext(g, t * 4));
	c->ext_up = kp_s16v(-ext(g, t * 4 + 1));
	c->ext_right = kp_s16v(ext(g, t * 4 + 2));
	c->ext_down = kp_s16v(ext(g, t * 4 + 3));
	c->long_nodes = 0;
	c->local_nodes = 0;
	c->long_peak = 0;
	c->local_peak = 0;
}

/* InitUnitPathSearch 0x491210. */
int kp_path_search_init(KpGame *g, KpRoute *c)
{
	prepare(g, c);
	if (c->src_region == c->dst_region) {
		c->regions[0] = c->src_region;
		c->region_cursor = -1;
		c->region_count = 1;
		c->full_count = 1;
		return kp_path_segment(g, c);
	}
	if (!kp_search_region_path(g, c)) return 0;
	return kp_path_segment(g, c);
}

/* ContinueLocalPath 0x4919b0. */
int kp_path_search_continue(KpGame *g, KpRoute *c)
{
	if (c->src_x == c->dest_x && c->src_y == c->dest_y) return 0;
	prepare(g, c);
	return kp_path_segment(g, c);
}

/* FindUnitsAtPoint 0x421c80. */
int kp_units_at_point(KpGame *g, int64_t n, int64_t x, int64_t y, int64_t *out, int max)
{
	KpUnits *u = &g->u;
	int64_t hw = kp_shrv(g->spatial.max_w, 1), hh = kp_shrv(g->spatial.max_h, 1), t = u->type[kp_game_unit(g, n)];
	int count, i, m = 0;
	count = kp_units_in_rect(g, kp_s16v(x - hw - 1), kp_s16v(y - hh - 1), kp_s16v(x + 1 + hw), kp_s16v(y + 1 + hh), out,
		max);
	for (i = 0; i < count; i++) {
		int64_t c = out[i], cx, cy, ct, l, r, tp, b;
		kp_sprite_pos(g, c, &cx, &cy);
		ct = u->type[c];
		l = kp_s16v(cx - ext(g, ct * 4) + (-1 - ext(g, t * 4 + 2)));
		r = kp_s16v(cx + ext(g, ct * 4 + 2) + ext(g, t * 4) + 1);
		tp = kp_s16v(cy - ext(g, ct * 4 + 1) + (-1 - ext(g, t * 4 + 3)));
		b = kp_s16v(cy + ext(g, ct * 4 + 3) + ext(g, t * 4 + 1) + 1);
		if (l <= x && x <= r && tp <= y && y <= b) out[m++] = c;
	}
	return m;
}

/* FindFootprintFitNearPoint 0x492c00. */
int kp_footprint_near(KpGame *g, const KpRoute *c, int64_t x, int64_t y, int64_t *fx, int64_t *fy)
{
	int64_t map_w = g->map_w_px, map_h = g->map_h_px, k = 1, i, d;
	while (k < 10) {
		for (d = 0; d <= 3; d++) {
			for (i = 1; i <= k; i++) {
				int64_t x0 = kp_s16v(x + c->ext_left), y0 = kp_s16v(y + c->ext_up);
				int64_t x1 = kp_s16v(x + c->ext_right), y1 = kp_s16v(y + c->ext_down);
				if (kp_mod(x0, 65536) < map_w && kp_mod(y0, 65536) < map_h && kp_mod(x1, 65536) < map_w
					&& kp_mod(y1, 65536) < map_h) {
					int fits = 1;
					int64_t ty, ty_end = kp_idiv64(g, y1, 32);
					for (ty = kp_idiv64(g, y0, 32); ty <= ty_end; ty++) {
						int64_t tx, tx_end = kp_idiv64(g, x0, 32);
						for (tx = kp_idiv64(g, x1, 32); tx >= tx_end; tx--) {
							if (kp_region_inaccessible(g, kp_region_cell(g, tx, ty))) {
								fits = 0;
								break;
							}
						}
						if (!fits) break;
					}
					if (fits) {
						*fx = x;
						*fy = y;
						return 1;
					}
				}
				x = kp_s16v(x + DX[d] * 8);
				y = kp_s16v(y + DY[d] * 8);
			}
			x = kp_s16v(x - DX[d] * 8);
			y = kp_s16v(y - DY[d] * 8);
		}
		x = kp_s16v(x - 8);
		y = kp_s16v(y - 8);
		k = k + 2;
	}
	*fx = x;
	*fy = y;
	return 0;
}

/* BuildLocalPathSegment 0x491c10. */
int kp_path_segment(KpGame *g, KpRoute *c)
{
	KpUnits *u = &g->u;
	int64_t count = c->region_count, cursor = kp_s16v(c->region_cursor + 1), src, rem, px, py, next_region;
	int final, final_point, ok;
	c->region_cursor = cursor;
	if (cursor >= count) return 0;
	src = c->src_region;
	if (route_region(c, cursor) != src) {
		int64_t k = cursor;
		while (k < count && route_region(c, k) != src) k = k + 1;
		if (k >= count) return 0;
		c->region_cursor = k;
		cursor = k;
	}
	rem = count - cursor;
	if (rem == 1 || rem == 2 || rem <= 3) {
		px = c->goal_x;
		py = c->goal_y;
		final = 1;
		final_point = 1;
	} else {
		int64_t r = kp_rg_check(g, route_region(c, cursor + 2)); /* GetRegionCenter 0x481a60 */
		px = kp_s16v(kp_sarv(g->pathing.rg.center_x[r], 8));
		py = kp_s16v(kp_sarv(g->pathing.rg.center_y[r], 8));
		final = 0;
		final_point = 0;
	}
	if (cursor == count - 1) {
		if (src != c->dst_region) {
			if (!kp_snap_goal_into_region(g, c, src)) kp_game_fail(g, "route: error in GetClosestReachable (0x4cc190)");
			final = 1;
			final_point = 1;
		}
		next_region = KP_LOCAL_FINAL;
	} else {
		next_region = route_region(c, cursor + 1);
		if (next_region == NIL) kp_game_fail(g, "route: no region %lld", (long long)(cursor + 1));
	}
	c->target = 0;
	if (final) {
		int64_t tt;
		c->target = u->move_unit[kp_game_unit(g, c->unit)];
		if (c->target == 0) {
			int64_t cands[KP_UNITS];
			int cn = kp_units_at_point(g, c->unit, c->goal_x, c->goal_y, cands, KP_UNITS), i;
			for (i = 0; i < cn; i++) {
				if (kp_path_obstacle(g, cands[i], c->unit)) {
					c->target = cands[i];
					break;
				}
			}
		}
		if (c->target != 0 && c->target >= 1 && c->target <= KP_UNITS) {
			tt = u->type[c->target];
			if (tt >= UNTARGETED_FIRST && tt <= UNTARGETED_LAST) c->target = 0;
		}
		if (c->target == 0) {
			int64_t fx, fy;
			c->box_left = KP_LOCAL_NO_POINT;
			c->box_top = KP_LOCAL_NO_POINT;
			c->box_right = KP_LOCAL_NO_POINT;
			c->box_bottom = KP_LOCAL_NO_POINT;
			ok = kp_footprint_near(g, c, px, py, &fx, &fy);
			if (ok && (fx != px || fy != py)) {
				px = fx;
				py = fy;
				c->goal_x = fx;
				c->goal_y = fy;
			}
		} else {
			int64_t tt2 = u->type[kp_game_unit(g, c->target)], mt = u->type[c->unit], tx, ty, sx, sy;
			int on_x, on_y;
			kp_sprite_pos(g, c->target, &tx, &ty);
			c->box_left = kp_s16v(tx - ext(g, tt2 * 4) + (-1 - ext(g, mt * 4 + 2)));
			c->box_right = kp_s16v(tx + ext(g, tt2 * 4 + 2) + ext(g, mt * 4) + 1);
			c->box_top = kp_s16v(ty - ext(g, tt2 * 4 + 1) + (-1 - ext(g, mt * 4 + 3)));
			c->box_bottom = kp_s16v(ty + ext(g, tt2 * 4 + 3) + ext(g, mt * 4 + 1) + 1);
			sx = c->src_x;
			sy = c->src_y;
			on_x = sx == c->box_left || sx == c->box_right;
			on_y = sy == c->box_top || sy == c->box_bottom;
			if ((on_x && c->box_top <= sy && sy <= c->box_bottom) || (on_y && c->box_left <= sx && sx <= c->box_right)) {
				c->point_x[0] = sx;
				c->point_y[0] = sy;
				c->goal_x = sx;
				c->goal_y = sy;
				c->point_index = 0;
				c->point_count = 1;
				c->reached = 1;
				c->reached_end = 0;
				return 1;
			}
		}
	}
	ok = kp_local_path_search(g, c, next_region, px, py);
	if (ok) {
		int64_t i = c->point_count - 1, lx, ly;
		if (i < 0 || i >= KP_ROUTE_POINTS) kp_game_fail(g, "route: no point %lld", (long long)i);
		lx = c->point_x[i];
		ly = c->point_y[i];
		if (c->reached != 0) {
			if (!final_point && lx == px && ly == py) {
				c->reached = 0;
				return ok;
			}
		} else {
			if (next_region != KP_LOCAL_FINAL) return ok;
			c->reached = 1;
			c->reached_end = 1;
		}
		c->goal_x = lx;
		c->goal_y = ly;
	}
	return ok;
}

/* SnapGoalIntoRegion 0x492a90. */
int kp_snap_goal_into_region(KpGame *g, KpRoute *c, int64_t region)
{
	int64_t left, top, right, bottom, gx, gy, x, y, tx, ty, k = 1, d, i;
	kp_region_box(g, region, &left, &top, &right, &bottom);
	gx = c->goal_x;
	gy = c->goal_y;
	y = top <= gy ? (bottom < gy ? bottom : gy) : top;
	x = left <= gx ? (right < gx ? right : gx) : left;
	tx = kp_s16v(kp_idiv64(g, x, 32));
	ty = kp_s16v(kp_idiv64(g, y, 32));
	while (k < 16) {
		for (d = 0; d <= 3; d++) {
			for (i = 1; i <= k; i++) {
				if (kp_mod(tx, 65536) < g->map_w && kp_mod(ty, 65536) < g->map_h
					&& kp_tile_region_first(g, tx * 32, ty * 32) == region) {
					c->goal_x = kp_s16v(tx * 32 + 16);
					c->goal_y = kp_s16v(ty * 32 + 16);
					c->reached_end = 1;
					return 1;
				}
				tx = kp_s16v(tx + DX[d]);
				ty = kp_s16v(ty + DY[d]);
			}
			tx = kp_s16v(tx - DX[d]);
			ty = kp_s16v(ty - DY[d]);
		}
		tx = kp_s16v(tx - 1);
		ty = kp_s16v(ty - 1);
		k = k + 2;
	}
	c->goal_x = kp_s16v(kp_sarv(g->pathing.rg.center_x[region], 8));
	c->goal_y = kp_s16v(kp_sarv(g->pathing.rg.center_y[region], 8));
	c->reached_end = 1;
	return 1;
}

/* PackPathRecord 0x48e730 (with ClampPathSizeAndAllocate 0x42c840). */
static int64_t pack(KpGame *g, KpRoute *c)
{
	int64_t regions, points, k, cursor, stored, s, i, l;
	k = kp_path_alloc_counts(g, c->region_count, c->point_count, &regions, &points);
	c->region_count = regions;
	c->point_count = points;
	if (k == 0) return 0;
	kp_path_w8(g, k, KP_PATH_RETRY, 0);
	kp_path_w32(g, k, KP_PATH_FRAME, kp_scalar(g, KP_FRAME));
	kp_path_w8(g, k, KP_PATH_FLAGS, 0);
	kp_path_w8(g, k, KP_PATH_POINTS, points);
	kp_path_w8(g, k, KP_PATH_POINT_INDEX, 0);
	kp_path_w8(g, k, KP_PATH_REGIONS, regions);
	kp_path_w8(g, k, KP_PATH_FULL_REGIONS, c->full_count);
	cursor = kp_s8((uint32_t)kp_mod(c->region_cursor, 256));
	stored = kp_s8((uint32_t)kp_mod(regions, 256));
	kp_path_w8(g, k, KP_PATH_REGION_INDEX, stored < cursor ? stored : cursor);
	s = kp_s8((uint32_t)kp_mod(points, 256));
	for (i = 0; i <= s - 1; i++) {
		if (i >= KP_ROUTE_POINTS) kp_game_fail(g, "route: no point %lld", (long long)i);
		kp_path_set_point(g, k, KP_PATH_PAYLOAD + 4 * i, c->point_x[i], c->point_y[i]);
	}
	l = kp_s8((uint32_t)kp_mod(regions, 256));
	for (i = 0; i <= l - 1; i++) kp_path_w16(g, k, KP_PATH_PAYLOAD + 4 * s + 2 * i, c->regions[i]);
	return k;
}

/* UnpackPathRecord 0x48e7e0. */
static void unpack(KpGame *g, int64_t n, int64_t k, KpRoute *c)
{
	int64_t sx, sy, dx, dy, s, l, i;
	kp_sprite_pos(g, n, &sx, &sy);
	kp_path_point(g, k, KP_PATH_DEST, &dx, &dy);
	kp_route_new(c, n, sx, sy, dx, dy);
	s = kp_path_s8(g, k, KP_PATH_POINTS);
	for (i = 0; i <= s - 1; i++) kp_path_point(g, k, KP_PATH_PAYLOAD + 4 * i, &c->point_x[i], &c->point_y[i]);
	c->point_count = s;
	c->point_index = kp_path_s8(g, k, KP_PATH_POINT_INDEX);
	l = kp_path_s8(g, k, KP_PATH_REGIONS);
	for (i = 0; i <= l - 1; i++) c->regions[i] = kp_path_u16(g, k, KP_PATH_PAYLOAD + 4 * s + 2 * i);
	c->region_count = l;
	c->region_cursor = kp_path_s8(g, k, KP_PATH_REGION_INDEX);
	c->full_count = kp_path_u8(g, k, KP_PATH_FULL_REGIONS);
}

/* SetDirectPath 0x48ef10. */
void kp_path_set_direct(KpGame *g, int64_t n, int64_t nx, int64_t ny, int64_t dx, int64_t dy)
{
	KpUnits *u = &g->u;
	int64_t k, sx, sy;
	if (u->path[kp_game_unit(g, n)] != 0) kp_path_release(g, u->path[n]);
	k = kp_scalar(g, KP_PATH_FREE_HEAD);
	if (k != 0) kp_scalar_set(g, KP_PATH_FREE_HEAD, kp_path_u32(g, k, 0));
	u->path[n] = k;
	if (k == 0) return;
	kp_path_w8(g, k, KP_PATH_REGIONS, 1);
	kp_path_w8(g, k, KP_PATH_REGION_INDEX, 0);
	kp_path_w8(g, k, KP_PATH_POINTS, 1);
	kp_path_w8(g, k, KP_PATH_POINT_INDEX, 0);
	kp_path_w8(g, k, KP_PATH_FULL_REGIONS, 1);
	kp_sprite_pos(g, n, &sx, &sy);
	kp_path_set_point(g, k, KP_PATH_SRC, sx, sy);
	kp_path_set_point(g, k, KP_PATH_NEXT, nx, ny);
	kp_path_set_point(g, k, KP_PATH_DEST, dx, dy);
	kp_path_w16(g, k, KP_PATH_FLAGS, 0);
	kp_path_set_point(g, k, KP_PATH_PAYLOAD, nx, ny);
}

/* NextPathPoint 0x48ec50. */
int kp_next_path_point(KpGame *g, int64_t n, int64_t tx, int64_t ty, int64_t *rx, int64_t *ry)
{
	KpUnits *u = &g->u;
	KpRoute c;
	int64_t k, i, px, py, dx, dy, sx, sy;
	int built = 0, have;
	u->movement_flags[kp_game_unit(g, n)] = kp_bor(kp_band(u->movement_flags[n], 0xbf), FLAG_PATHING);
	k = u->path[n];
	if (k != 0) {
		kp_path_w8(g, k, KP_PATH_POINT_INDEX, kp_path_u8(g, k, KP_PATH_POINT_INDEX) + 1);
		if (kp_path_s8(g, k, KP_PATH_POINT_INDEX) >= kp_path_s8(g, k, KP_PATH_POINTS)) {
			int64_t cursor, count;
			unpack(g, n, k, &c);
			kp_path_free(g, n);
			cursor = c.region_cursor;
			count = c.region_count;
			if (!(count <= cursor + 1 || (c.full_count != count && count <= cursor + 4))) {
				int ok = kp_path_search_continue(g, &c);
				if (ok && c.region_count != 0 && c.point_count != 0) built = 1;
			}
			k = 0;
		}
	}
	have = built;
	if (k == 0 && !built) {
		kp_sprite_pos(g, n, &sx, &sy);
		kp_route_new(&c, n, sx, sy, tx, ty);
		if (!kp_path_search_init(g, &c)) return 0;
		if (c.region_count == 0 || c.point_count == 0) return 0;
		have = 1;
	}
	if (have) {
		if (c.reached != 0 && (c.goal_x != tx || c.goal_y != ty)) {
			tx = c.goal_x;
			ty = c.goal_y;
			kp_move_to_intercept(g, n, tx, ty);
			u->status[n] = kp_bxor(u->status[n],
				kp_band(kp_bxor(kp_shlv(c.reached_end, 19), u->status[n]), STATUS_INTERCEPT));
		}
		k = pack(g, &c);
		u->path[n] = k;
		if (k == 0) return 0;
		kp_sprite_pos(g, n, &sx, &sy);
		kp_path_set_point(g, k, KP_PATH_SRC, sx, sy);
		kp_path_set_point(g, k, KP_PATH_DEST, tx, ty);
	}
	i = kp_path_s8(g, k, KP_PATH_POINT_INDEX);
	kp_path_point(g, k, KP_PATH_PAYLOAD + 4 * i, &px, &py);
	kp_path_set_point(g, k, KP_PATH_NEXT, px, py);
	if (px == u->move_x[n] && py == u->move_y[n]) {
		u->movement_flags[n] = kp_band(u->movement_flags[n], 0xef);
		*rx = px;
		*ry = py;
		return 1;
	}
	kp_path_point(g, k, KP_PATH_DEST, &dx, &dy);
	if (approx(px, py, dx, dy) < NEAR_GOAL) u->movement_flags[n] = kp_bor(u->movement_flags[n], FLAG_NEAR_GOAL);
	*rx = px;
	*ry = py;
	return 1;
}

/* MoveToInterceptMoveTarget 0x420db0. */
void kp_move_to_intercept(KpGame *g, int64_t n, int64_t x, int64_t y)
{
	KpUnits *u = &g->u;
	int64_t t = u->move_unit[kp_game_unit(g, n)];
	if (t != 0 && kp_band(u->movement_flags[kp_game_unit(g, t)], 2) != 0) {
		int64_t dir = u->velocity_direction[t], dist = kp_flingy_halt_distance(g, n) * 3, ax, ay;
		int32_t s = kp_unit_sprite(g, t);
		if (dir < 0 || dir > 255) kp_game_fail(g, "route: direction %lld", (long long)dir);
		ax = kp_s16v(g->w->spr.x[s]) + kp_sarv(kp_sarv(kp_mulv(KP_DIR_X[dir], dist), 8), 8);
		ay = kp_s16v(g->w->spr.y[s]) + kp_sarv(kp_sarv(kp_mulv(KP_DIR_Y[dir], dist), 8), 8);
		if (kp_u32v(ax) < g->map_w_px && kp_u32v(ay) < g->map_h_px) {
			x = ax;
			y = ay;
		}
	}
	kp_flingy_set_move_target(g, n, x, y);
	u->move_unit[n] = t;
}

/* CanKeepCurrentWaypoint 0x48ee40. */
int kp_path_keeps_waypoint(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t k = u->path[kp_game_unit(g, n)], sx, sy, nx, ny, d, s, r;
	if (k == 0) return 0;
	kp_sprite_pos(g, n, &sx, &sy);
	kp_path_point(g, k, KP_PATH_NEXT, &nx, &ny);
	if (sx == nx && sy == ny) return 0;
	if (nx == u->move_x[n] && ny == u->move_y[n]) return 1;
	if (u->movement_type[n] != 0) return 1;
	d = approx(sx, sy, nx, ny);
	if (d > KEEP_DISTANCE) return 1;
	if (d * 2 >= kp_heading_difference(u->velocity_direction[n], u->velocity_direction2[n])) return 1;
	s = kp_path_s8(g, k, KP_PATH_POINTS);
	if (kp_path_s8(g, k, KP_PATH_POINT_INDEX) < s - 1) return 0;
	r = kp_path_u16(g, k, KP_PATH_PAYLOAD + 4 * s + 2 * kp_path_s8(g, k, KP_PATH_REGION_INDEX));
	return kp_region_at(g, sx, sy) != r;
}

/* SetPathWaypoint 0x40cd30. */
void kp_set_path_waypoint(KpGame *g, int64_t n, int64_t x, int64_t y)
{
	KpUnits *u = &g->u;
	if (x != u->target_waypoint_x[kp_game_unit(g, n)] || y != u->target_waypoint_y[n]) {
		u->target_waypoint_x[n] = kp_s16v(x);
		u->target_waypoint_y[n] = kp_s16v(y);
	}
	if (x != u->waypoint_x[n] || y != u->waypoint_y[n]) {
		u->movement_flags[n] = kp_bor(u->movement_flags[n], 1);
		u->waypoint_x[n] = kp_s16v(x);
		u->waypoint_y[n] = kp_s16v(y);
	}
}

/* AdvancePathWaypoint 0x48eb90. */
int kp_advance_path_waypoint(KpGame *g, int64_t n, int64_t tx, int64_t ty)
{
	KpUnits *u = &g->u;
	int64_t px = 0, py = 0;
	int ok;
	if (kp_path_keeps_waypoint(g, n)) return 1;
	if (kp_band(u->status[n], STATUS_PATHED) == 0) {
		kp_set_move_target_point(g, n, tx, ty);
		kp_path_set_direct(g, n, tx, ty, tx, ty);
		return 1;
	}
	ok = kp_next_path_point(g, n, tx, ty, &px, &py);
	if (!ok && u->path[n] == 0) return 0;
	/* a waypoint needs a point */
	if (!ok) kp_game_fail(g, "route: waypoint without a point");
	kp_set_path_waypoint(g, n, px, py);
	kp_path_keeps_waypoint(g, n);
	return 1;
}

/* GetRouteDistance 0x48e9b0: the walking distance from unit n's sprite to (x, y) over the centers of the regions
 * between; 0x7fff when the point is unreachable, 0x7ffe when no region path is found. */
int64_t kp_route_distance(KpGame *g, int64_t n, int64_t x, int64_t y)
{
	KpRoute c;
	int64_t sx, sy, d = 0, px, py, k;
	kp_sprite_pos(g, n, &sx, &sy);
	x = kp_s16v(x);
	y = kp_s16v(y);
	if (kp_band(g->u.pathing_flags[n], 1) == 0) return kp_approx_distance(x - sx, y - sy);
	if (!kp_placement_reachable(g, n, x, y)) return 0x7fff; /* UnitCanReachPoint 0x481d00 */
	kp_route_new(&c, n, sx, sy, x, y);
	c.goal_x = x;
	c.goal_y = y;
	c.src_region = kp_region_at(g, sx, sy);
	c.dst_region = kp_region_at(g, x, y);
	if (c.src_region == c.dst_region) return kp_approx_distance(x - sx, y - sy);
	if (!kp_search_region_path(g, &c)) return 0x7ffe;
	if (c.region_count <= 2) return kp_approx_distance(x - sx, y - sy); /* 0x48ec20 */
	/* GetRegionCenterPoint 0x481a60 */
#define CENTER(r, v) kp_s16v(kp_sarv(g->pathing.rg.v[kp_rg_check(g, (r))], 8))
	px = CENTER(c.regions[0], center_x);
	py = CENTER(c.regions[0], center_y);
	for (k = 1; k < c.region_count; k++) {
		int64_t qx = CENTER(c.regions[k], center_x), qy = CENTER(c.regions[k], center_y);
		d += kp_approx_distance(qx - px, qy - py);
		px = qx;
		py = qy;
	}
#undef CENTER
	return kp_s32v(d);
}
