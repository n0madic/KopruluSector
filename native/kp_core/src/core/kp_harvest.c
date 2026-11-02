/* Harvesting (kp_game.h), ported from the atlas (P08, P09, P11, P13, P28; docs/research/native-harvesting.md,
 * native-gas-harvesting.md): the worker orders Harvest1 79 and MoveToGas 81 (0x423480), WaitForGas 82 (0x4237a0),
 * HarvestGas 83 (0x4235c0), ReturnGas 84 and ReturnMinerals 90 (0x423ce0), MoveToMinerals 85 (0x4239a0),
 * WaitForMinerals 86 (0x423c30), MiningMinerals 87 (0x423810), Harvest3 88 (0x423c70), Harvest4 89 (0x423cc0) and
 * ResetHarvestCollision 151 (0x42e0d0); the resource's service and waiting list (0x424230, 0x423ed0, 0x4240d0,
 * 0x424130, 0x424170, 0x424750, 0x423400), the extraction and the carried cargo (0x423e40, 0x423fd0, 0x427090,
 * 0x427410, 0x427480), the searches for a depot and a mineral field (0x431f00, 0x431f70, 0x431fe0) and a worker's
 * right click on resources (0x47fc60).
 *
 * The union of a worker: +0xc4/+0xc6 the remembered harvest point, +0xc8 the resource it harvests, +0xce the
 * active-service byte, +0xcf the cargo amount, +0xd0 the resource it is served by or waits at, +0xd4/+0xd8 its
 * neighbours in that resource's waiting list. Of a resource: +0xd0 the amount (WORD), +0xd2 the mineral field's
 * animation, +0xd3 the busy byte, +0xd4 the head of its waiting list. The cargo's type is resource_type (+0xe0):
 * bit 0x1 gas, 0x2 minerals; another nonzero value is a carried powerup. */
#include "kp_game_internal.h"

#define DAT_WORKER 0x8
#define DAT_RESOURCE_DEPOT 0x1000 /* units.dat special ability flags */
#define DAT_RESOURCE 0x2000
#define STATUS_COMPLETED 0x1
#define STATUS_GROUNDED_BUILDING 0x2
#define STATUS_NO_COLLIDE 0x200000
#define STATUS_CANNOT_MOVE 0x80000
#define STATUS_IGNORE_TILE_COLLISION 0x800000
#define CARRY_GAS 0x1
#define CARRY_MINERALS 0x2
#define CARRY_RESOURCE 0x3
#define DAT_POWERUP 0x800
#define CARRIED_LARGE 0 /* the image of 8 or more extracted; the next image is the small one */
#define MINERAL_YIELD 8
#define DEPLETED_GAS_YIELD 2
#define GAS_WARNING_SOUND 20
#define GAS_WARNING_STRING 875
#define HARVEST_GAS_TIME 37 /* the hidden worker's timer in a gas building, the first visible value 36 */
#define GAS_EXIT_RETRY 18
#define GAS_EXIT_SIDE 32
#define MINING_TIME 75
#define MINING_OFFSET 20
#define RETURN_RETRY 75
#define MINERAL_RADIUS 384
#define FREE_MINERAL_RADIUS 256
#define FREE_MINERAL_RADIUS_AI 512
#define NEAR_MINERAL_RANGE 10
#define CARGO_OVERLAY_FIRST 405 /* the overlays a worker draws over its cargo (removed first) */
#define CARGO_OVERLAY_LAST 418
#define CARRIED_FIRST 392
#define CARRIED_LAST 404
#define CARRIED_FLAG_FIRST 958
#define CARRIED_FLAG_LAST 959
#define NO_CARRIED_IMAGE 999
#define TILE_OCCUPIED 0x08000000
#define SPRITE_HIDDEN 0x20
#define DEPOT_SIDE_STEP 25
#define DEPOT_SIDE_LIMIT 100

/* The union fields (byte offsets in the CUnit). */
#define U_HARVEST_POINT 0xc4
#define U_HARVEST_TARGET 0xc8
#define U_SERVICE 0xce
#define U_CARGO 0xcf
#define U_RESOURCE 0xd0
#define U_PREV 0xd4
#define U_NEXT 0xd8
#define R_AMOUNT 0xd0
#define R_BUSY 0xd3
#define R_FIRST 0xd4
#define R_AI_CLAIM 0xd9
/* the union word indexes of the unit references (kp_set_union_unit) */
#define W_HARVEST_TARGET 2
#define W_RESOURCE 4
#define W_PREV 5
#define W_NEXT 6

static int64_t field(KpGame *g, int64_t n, int offset, int size) { return kp_union_get(g, n, offset, size); }
static void set_field(KpGame *g, int64_t n, int offset, int size, int64_t v) { kp_union_set(g, n, offset, size, v); }
static int64_t ref(KpGame *g, int64_t n, int word) { return kp_union_word(g, word)[n]; }
static void set_ref(KpGame *g, int64_t n, int word, int64_t m) { kp_set_union_unit(g, n, word, m); }

static int mineral(KpGame *g, int64_t n) { return n != 0 && kp_mineral_field(g->u.type[n]); }
static int64_t carry(KpGame *g, int64_t n) { return g->u.resource_type[n]; }
/* Something other than minerals or gas (a powerup) in its hands. */
static int carrying_other(KpGame *g, int64_t n) { return carry(g, n) != 0 && kp_band(carry(g, n), CARRY_RESOURCE) == 0; }

static int64_t sprite_x(KpGame *g, int64_t n) { return kp_s16v(g->w->spr.x[kp_unit_sprite(g, n)]); }
static int64_t sprite_y(KpGame *g, int64_t n) { return kp_s16v(g->w->spr.y[kp_unit_sprite(g, n)]); }

static void stop_done(KpGame *g, int64_t n)
{
	kp_stop_and_face(g, n); /* 0x43a4e0 */
	kp_order_done(g, n);    /* 0x42a5a0 */
}

/* The order 151 that restores the collision after harvesting: first in the queue (0x429de0) or alone (0x4295a0). */
static void ensure_reset_collision(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	if (kp_band(u->status[n], STATUS_IGNORE_TILE_COLLISION) != 0) return;
	u->status[n] = kp_bor(u->status[n], STATUS_IGNORE_TILE_COLLISION);
	if (u->queue_head[n] == KP_NONE) kp_order_insert_after(g, n, KP_ORD_RESET_HARVEST_COLLISION, 0, 0, 0, KP_TYPE_NONE, KP_NONE);
	else kp_order_insert_before(g, n, KP_ORD_RESET_HARVEST_COLLISION, 0, 0, 0, KP_TYPE_NONE, u->queue_head[n]);
}

/* RemoveFirstQueuedOrderOfType 0x42a0e0. */
static void remove_queued(KpGame *g, int64_t n, int64_t order)
{
	int64_t i;
	for (i = g->u.queue_head[n]; i != KP_NONE; i = g->orders.next[i]) {
		if (g->orders.order[i] == order) {
			kp_order_remove(g, n, i);
			return;
		}
	}
}

/* --- the resource's service and waiting list --- */

static void unlink_waiter(KpGame *g, int64_t r, int64_t w)
{
	int64_t prev = ref(g, w, W_PREV), next = ref(g, w, W_NEXT);
	if (prev != 0) set_ref(g, prev, W_NEXT, next);
	else set_ref(g, r, W_PREV, next); /* the resource's head +0xd4 */
	if (next != 0) set_ref(g, next, W_PREV, prev);
	set_ref(g, w, W_PREV, 0);
	set_ref(g, w, W_NEXT, 0);
	set_ref(g, w, W_RESOURCE, 0);
}

/* UnlinkWaitingHarvester 0x424750. */
static void unlink_waiting(KpGame *g, int64_t r, int64_t w) { unlink_waiter(g, r, w); }

/* PopEligibleWaitingHarvester 0x423ed0: the last eligible worker of the chain, the oldest one (head insertion). */
static int64_t pop_waiting(KpGame *g, int64_t r)
{
	KpUnits *u = &g->u;
	int64_t w, chosen = 0;
	for (w = ref(g, r, W_PREV); w != 0; w = ref(g, w, W_NEXT)) {
		if (!kp_unit_disabled(g, w) && (u->order[w] == KP_ORD_WAIT_FOR_GAS || u->order[w] == KP_ORD_WAIT_FOR_MINERALS)) {
			chosen = w;
		}
	}
	if (chosen == 0) return 0;
	unlink_waiter(g, r, chosen);
	remove_queued(g, chosen, KP_ORD_HARVEST4);
	return chosen;
}

/* TryStartHarvesting 0x4240d0. */
static int start_service(KpGame *g, int64_t w, int64_t r)
{
	int64_t order;
	if (field(g, r, R_BUSY, 1) != 0) return 0;
	set_field(g, r, R_BUSY, 1, 1);
	set_field(g, w, U_SERVICE, 1, 1);
	set_ref(g, w, W_RESOURCE, r);
	if (g->u.order[w] == KP_ORD_WAIT_FOR_GAS) {
		order = KP_ORD_HARVEST_GAS;
	} else {
		kp_order_prepend(g, w, KP_ORD_HARVEST3);
		order = KP_ORD_MINING_MINERALS;
	}
	kp_order_prepend_target(g, w, order, r);
	kp_order_done(g, w);
	return 1;
}

/* EnterResourceQueue 0x424230. */
static void enter_queue(KpGame *g, int64_t w, int64_t r)
{
	KpUnits *u = &g->u;
	u->queue_timer[w] = 0;
	u->order_state[w] = 2;
	if (field(g, r, R_BUSY, 1) == 0) {
		start_service(g, w, r);
		return;
	}
	if (ref(g, w, W_RESOURCE) == 0) {
		int64_t head = ref(g, r, W_PREV);
		set_ref(g, w, W_NEXT, head);
		set_ref(g, w, W_PREV, 0);
		if (head != 0) set_ref(g, head, W_PREV, w);
		set_ref(g, r, W_PREV, w);
		set_ref(g, w, W_RESOURCE, r);
	}
	kp_order_prepend(g, w, KP_ORD_HARVEST4);
}

/* ReleaseHarvestResource 0x424130 (and the inline release of the orders 83, 87 and 88). */
static void release_service(KpGame *g, int64_t w, int64_t r)
{
	int64_t next;
	set_field(g, w, U_SERVICE, 1, 0);
	set_ref(g, w, W_RESOURCE, 0);
	if (r == 0) return;
	set_field(g, r, R_BUSY, 1, kp_mod(field(g, r, R_BUSY, 1) - 1, 256));
	next = pop_waiting(g, r);
	if (next != 0) start_service(g, next, r);
}

/* The release of a worker that leaves its service: +0xce and +0xd0 clear, the next waiter starts. */
static void leave_service(KpGame *g, int64_t w)
{
	if (field(g, w, U_SERVICE, 1) != 0) release_service(g, w, ref(g, w, W_RESOURCE));
}

/* CancelHarvesterMembership 0x424170. */
void kp_harvest_cancel(KpGame *g, int64_t r, int64_t w)
{
	int64_t m;
	if (field(g, w, U_SERVICE, 1) != 0) {
		release_service(g, w, r);
		return;
	}
	for (m = ref(g, r, W_PREV); m != 0; m = ref(g, m, W_NEXT)) {
		if (m == w) {
			unlink_waiter(g, r, w);
			return;
		}
	}
}

/* DetachAllWaitingHarvesters 0x423400. */
void kp_harvest_detach_all(KpGame *g, int64_t r)
{
	int64_t w;
	set_field(g, r, R_BUSY, 1, 0);
	while ((w = ref(g, r, W_PREV)) != 0) unlink_waiter(g, r, w);
}

/* TryStartWaitingHarvester 0x4244c0: a waiting worker no longer disabled starts on its resource when it is free. */
void kp_harvest_resume(KpGame *g, int64_t w)
{
	KpUnits *u = &g->u;
	int64_t r = ref(g, w, W_RESOURCE);
	if (kp_unit_disabled(g, w) || r == 0 || kp_band(KP_DAT(g, units, special_ability_flags, u->type[r]), DAT_RESOURCE) == 0
		|| field(g, w, U_SERVICE, 1) != 0 || field(g, r, R_BUSY, 1) != 0) {
		return;
	}
	if ((u->order[w] != KP_ORD_WAIT_FOR_MINERALS && u->order[w] != KP_ORD_WAIT_FOR_GAS) || u->order_state[w] != 2) return;
	unlink_waiting(g, r, w);
	remove_queued(g, w, KP_ORD_HARVEST4);
	start_service(g, w, r);
}

/* --- cargo --- */

/* The first carried-item image of sprite s: a mineral or gas chunk (392..404), else a flag (958..959). */
int32_t kp_carried_image(KpImages *w, int32_t s)
{
	int32_t i = kp_sprite_find_image(w, s, CARRIED_FIRST, CARRIED_LAST);
	return i != KP_NONE ? i : kp_sprite_find_image(w, s, CARRIED_FLAG_FIRST, CARRIED_FLAG_LAST);
}

/* RemoveCargoImages 0x427410: the overlays 405..418, then the carried image 392..404 (else a flag's 958..959). */
int64_t kp_remove_cargo_images(KpGame *g, int64_t n)
{
	KpImages *w = g->w;
	int32_t s = kp_unit_sprite(g, n), i;
	int64_t id;
	i = kp_sprite_find_image(w, s, CARGO_OVERLAY_FIRST, CARGO_OVERLAY_LAST);
	if (i != KP_NONE) kp_image_release(w, i);
	i = kp_carried_image(w, s);
	if (i == KP_NONE) return NO_CARRIED_IMAGE;
	id = w->img.id[i];
	kp_image_release(w, i);
	return id;
}

/* WorkerBeginCarryResource 0x427090. */
static void begin_carry(KpGame *g, int64_t n, int64_t image, int64_t amount, int is_mineral)
{
	int32_t i;
	if (carry(g, n) != 0) return;
	g->u.resource_type[n] = is_mineral ? CARRY_MINERALS : CARRY_GAS;
	i = kp_sprite_image_above_main(g->w, kp_unit_sprite(g, n), (int32_t)image, 0, 0, 0);
	if (i != KP_NONE) kp_image_set_follow(g->w, i, 1);
	set_field(g, n, U_CARGO, 1, kp_mod(amount, 256));
	g->interface_refresh++; /* RefreshStatusUi 0x4a7ed0, for the interface */
}

/* AddAiBudget 0x482f00. */
static void add_ai_budget(KpGame *g, int64_t p, int64_t minerals, int64_t gas)
{
	KpPlayerAi *a = &g->pai;
	if (p < 0 || p >= KP_AI_PLAYERS) kp_game_fail(g, "harvest: the AI budget of player %lld", (long long)p);
	a->minerals[p] = kp_u32v(a->minerals[p] + minerals);
	a->gas[p] = kp_u32v(a->gas[p] + gas);
}

/* WorkerDeliverCargo 0x427480. */
static void deliver_cargo(KpGame *g, int64_t n)
{
	KpPlayers *pl = &g->players;
	int64_t p = g->u.owner[n], amount = field(g, n, U_CARGO, 1);
	g->interface_refresh++; /* RefreshStatusUi 0x4a7ed0, for the interface */
	kp_remove_cargo_images(g, n);
	if (kp_band(carry(g, n), CARRY_MINERALS) != 0) {
		pl->minerals[p] = kp_u32v(pl->minerals[p] + amount);
		pl->cumulative_minerals[p] = kp_u32v(pl->cumulative_minerals[p] + amount);
		add_ai_budget(g, p, amount, 0);
	} else if (kp_band(carry(g, n), CARRY_GAS) != 0) {
		pl->gas[p] = kp_u32v(pl->gas[p] + amount);
		pl->cumulative_gas[p] = kp_u32v(pl->cumulative_gas[p] + amount);
		add_ai_budget(g, p, 0, amount);
	}
	g->u.resource_type[n] = 0;
	set_field(g, n, U_CARGO, 1, 0);
}

/* HasEnoughWorkersForAi 0x482f40: the computer's bonus cargo, for an AI of difficulty 2 with more than 11 completed
 * workers of its race (0x486070, 0x4939e0). Not in 1.17: the difficulty byte (+0x22f) is written only by
 * InitPlayerAiData 0x4830d0 and 0x481e40 with 1 and by 0x482490 from 0x67f7ac, which only 0x4830d0 writes, with 1;
 * the failure keeps the unported count from being taken silently. */
static int ai_bonus(KpGame *g, int64_t n)
{
	int64_t p = g->u.owner[n];
	if (g->u.ai[n] == 0 || p < 0 || p >= KP_AI_PLAYERS || g->pai.ai_state[p] != 2) return 0;
	kp_game_fail(g, "harvest: the computer's worker bonus (0x482f40)");
	return 0;
}

/* ExtractResource 0x423e40. */
static int64_t extract(KpGame *g, int64_t r, int is_mineral)
{
	int64_t amount = field(g, r, R_AMOUNT, 2);
	if (amount >= MINERAL_YIELD) {
		amount -= MINERAL_YIELD;
		set_field(g, r, R_AMOUNT, 2, amount);
		if (is_mineral) {
			if (amount != 0) kp_update_mineral_animation(g, r);
			else kp_unit_kill(g, r);
		} else if (amount < MINERAL_YIELD) {
			kp_report_error_sound(g, g->u.owner[r], GAS_WARNING_SOUND, GAS_WARNING_STRING); /* 0x4461f0 */
		}
		return MINERAL_YIELD;
	}
	if (is_mineral) {
		kp_unit_kill(g, r);
		return amount;
	}
	set_field(g, r, R_AMOUNT, 2, 0);
	return DEPLETED_GAS_YIELD;
}

/* The carried image of a resource type (large; the small one follows), 0 for none. */
static int64_t carried_image(int64_t t, int *is_mineral)
{
	*is_mineral = kp_mineral_field(t);
	if (*is_mineral) return 397;
	switch (t) {
	case KP_TYPE_REFINERY: return 403;
	case KP_TYPE_EXTRACTOR: return 401;
	case KP_TYPE_ASSIMILATOR: return 399;
	}
	return 0;
}

/* HarvestCargoFromResource 0x423fd0. */
static void harvest_cargo(KpGame *g, int64_t r, int64_t w)
{
	int is_mineral;
	int64_t image = carried_image(g->u.type[r], &is_mineral), q;
	if (image == 0) return;
	q = extract(g, r, is_mineral);
	if (q < MINERAL_YIELD) image++;
	if (q == 0) return;
	if (kp_band(carry(g, w), CARRY_RESOURCE) != 0) {
		kp_remove_cargo_images(g, w);
		g->u.resource_type[w] = 0;
	}
	if (ai_bonus(g, w)) q++;
	begin_carry(g, w, image, q, is_mineral);
}

/* --- searches --- */

/* IsValidResourceDepot 0x424460. */
int kp_valid_depot(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t t = u->type[n];
	if (kp_band(u->status[n], STATUS_COMPLETED) == 0 && t != KP_TYPE_HIVE && t != KP_TYPE_LAIR
		&& !(t == KP_TYPE_HATCHERY && kp_is_morphing_zerg_building(g, n))) {
		return 0;
	}
	return kp_band(u->status[n], STATUS_GROUNDED_BUILDING) != 0
		&& kp_band(KP_DAT(g, units, special_ability_flags, t), DAT_RESOURCE_DEPOT) != 0;
}

static int64_t completed(KpGame *g, int64_t t, int64_t p) { return g->count_completed[t * KP_PLAYERS + p]; }

/* DepotCandidateCallback 0x4323d0. */
static int depot_candidate(KpGame *g, int64_t c, int64_t w)
{
	return kp_valid_depot(g, c) && g->u.owner[c] == g->u.owner[w]
		&& kp_placement_reachable(g, w, sprite_x(g, c), sprite_y(g, c));
}

/* FindNearestResourceDepot 0x431f00. */
int64_t kp_nearest_depot(KpGame *g, int64_t w)
{
	static const int64_t DEPOTS[] = { KP_TYPE_HATCHERY, KP_TYPE_NEXUS, KP_TYPE_COMMAND_CENTER, KP_TYPE_LAIR, KP_TYPE_HIVE };
	int64_t p = g->u.owner[w];
	size_t k;
	for (k = 0; k < sizeof DEPOTS / sizeof DEPOTS[0]; k++) {
		if (completed(g, DEPOTS[k], p) != 0) return kp_nearest_unit_map(g, w, depot_candidate, w);
	}
	return 0;
}

/* IsUnitRingedByOccupiedTiles 0x412c90: every tile around unit n's box (corners aside) is occupied; the box edges
 * (signed 16-bit) divide by 32 truncating toward zero. */
static int ringed(KpGame *g, int64_t n)
{
	int64_t l, t, r, b, cols, rows, x0, y0, top, bottom, lcol, rcol, k;
	kp_type_box(g, g->u.type[n], sprite_x(g, n), sprite_y(g, n), &l, &t, &r, &b);
	cols = kp_s16v(r - l + 1) / 32;
	rows = kp_s16v(b - t + 1) / 32;
	x0 = l / 32;
	y0 = t / 32;
	top = y0 - 1 < 0 ? 0 : y0 - 1;
	bottom = b / 32 + 1 > g->map_h - 1 ? g->map_h - 1 : b / 32 + 1;
	lcol = x0 - 1 < 0 ? 0 : x0 - 1;
	rcol = r / 32 + 1 > g->map_w - 1 ? g->map_w - 1 : r / 32 + 1;
	for (k = 0; k < cols; k++) {
		if (kp_band(g->terrain.tiles[top * g->map_w + x0 + k], TILE_OCCUPIED) == 0) return 0;
		if (kp_band(g->terrain.tiles[bottom * g->map_w + x0 + k], TILE_OCCUPIED) == 0) return 0;
	}
	for (k = 0; k < rows; k++) {
		if (kp_band(g->terrain.tiles[(y0 + k) * g->map_w + lcol], TILE_OCCUPIED) == 0) return 0;
		if (kp_band(g->terrain.tiles[(y0 + k) * g->map_w + rcol], TILE_OCCUPIED) == 0) return 0;
	}
	return 1;
}

/* The query of the mineral searches: the worker, the search point (0x665d14), the radius (0x665d10) and the AI flag
 * (0x665d18), passed to the callbacks through their argument. */
typedef struct {
	int64_t worker, x, y, radius, flag;
} MineralSearch;

static const MineralSearch *search_of(int64_t arg) { return (const MineralSearch *)(intptr_t)arg; }

/* MineralCandidateCallback 0x432420. */
static int mineral_candidate(KpGame *g, int64_t c, int64_t arg)
{
	const MineralSearch *q = search_of(arg);
	int64_t w = q->worker, x = sprite_x(g, c), y = sprite_y(g, c), tile;
	if (!kp_mineral_field(g->u.type[c]) || ringed(g, c) || !kp_placement_reachable(g, w, x, y)) return 0;
	if (kp_terrain_height(g, q->x, q->y) != kp_terrain_height(g, x, y)) return 0;
	tile = g->terrain.tiles[y / 32 * g->map_w + x / 32];
	if (kp_band(tile, kp_shlv(1, g->u.owner[w] + 8)) != 0) return 0;
	return 2 * q->radius >= kp_route_distance(g, c, sprite_x(g, w), sprite_y(g, w));
}

/* FreeMineralCandidateCallback 0x432510. */
static int free_mineral_candidate(KpGame *g, int64_t c, int64_t arg)
{
	const MineralSearch *q = search_of(arg);
	if (!mineral_candidate(g, c, arg)) return 0;
	if (q->flag != 0 && kp_controller(g, g->u.owner[q->worker]) == KP_CTRL_COMPUTER && field(g, c, R_AI_CLAIM, 1) != 0) {
		return 0;
	}
	return field(g, c, R_BUSY, 1) == 0;
}

static int64_t mineral_search(KpGame *g, int64_t w, int64_t x, int64_t y, int64_t radius, int64_t flag,
	KpUnitFilter accept)
{
	MineralSearch q;
	int64_t t;
	int any = 0;
	for (t = KP_TYPE_FIRST_MINERAL; t <= KP_TYPE_LAST_MINERAL; t++) any = any || completed(g, t, KP_PLAYER_NEUTRAL) != 0;
	if (!any) return 0;
	q.worker = w;
	q.x = x;
	q.y = y;
	q.radius = radius;
	q.flag = flag;
	return kp_nearest_unit_at(g, x, y, radius, accept, (int64_t)(intptr_t)&q);
}

/* FindNearestMineralField 0x431f70. */
static int64_t nearest_mineral(KpGame *g, int64_t w, int64_t x, int64_t y)
{
	return mineral_search(g, w, x, y, MINERAL_RADIUS, 0, mineral_candidate);
}

/* FindNearestFreeMineralField 0x431fe0. */
int64_t kp_nearest_free_mineral(KpGame *g, int64_t w, int64_t x, int64_t y, int64_t flag)
{
	return mineral_search(g, w, x, y, flag ? FREE_MINERAL_RADIUS_AI : FREE_MINERAL_RADIUS, flag, free_mineral_candidate);
}

/* IsOwnCompletedGasBuilding 0x424330. */
static int own_gas_building(KpGame *g, int64_t r, int64_t w)
{
	return r != 0 && kp_band(g->u.status[r], STATUS_COMPLETED) != 0 && kp_gas_building(g->u.type[r])
		&& g->u.owner[r] == g->u.owner[w];
}

/* GetHarvestRightClickOrder 0x47fc60. */
int64_t kp_harvest_right_click_order(KpGame *g, int64_t w, int64_t target)
{
	KpUnits *u = &g->u;
	int64_t t = u->type[target];
	if (kp_band(KP_DAT(g, units, special_ability_flags, t), DAT_POWERUP) != 0) return KP_ORD_MOVE;
	if (kp_mineral_field(t)) return carrying_other(g, w) ? KP_ORD_MOVE : KP_ORD_MOVE_TO_MINERALS;
	if (own_gas_building(g, target, w) || t == KP_TYPE_GEYSER) return carrying_other(g, w) ? KP_ORD_MOVE : KP_ORD_MOVE_TO_GAS;
	if (kp_valid_depot(g, target) && u->owner[target] == u->owner[w]) {
		if (kp_band(carry(g, w), CARRY_MINERALS) != 0) return KP_ORD_RETURN_MINERALS;
		if (kp_band(carry(g, w), CARRY_GAS) != 0) return KP_ORD_RETURN_GAS;
	}
	return KP_ORD_NONE;
}

/* --- orders --- */

/* OrderMoveToGas 0x423480 (orders 79 and 81). */
void kp_order_move_to_gas(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t t = u->order_unit[n], arrival;
	if (carrying_other(g, n)) {
		stop_done(g, n);
		return;
	}
	ensure_reset_collision(g, n);
	if (u->order_state[n] == 0) {
		if (t == 0) {
			stop_done(g, n);
			return;
		}
		if (mineral(g, t)) {
			kp_order_prepend_at(g, n, KP_ORD_MOVE_TO_MINERALS, u->order_x[n], u->order_y[n], t);
			kp_order_done(g, n);
			return;
		}
		if (kp_request_move_to_unit(g, n, t)) {
			set_field(g, n, U_HARVEST_POINT, 4, kp_band(u->order_x[n], 0xffff) + kp_band(u->order_y[n], 0xffff) * 65536);
			set_ref(g, n, W_HARVEST_TARGET, t);
			u->order_state[n] = 3;
		}
	} else if (u->order_state[n] == 3) {
		arrival = kp_move_arrival(g, n);
		if (arrival == 0) return;
		if (t == 0 || !own_gas_building(g, t, n)) {
			stop_done(g, n);
			return;
		}
		if (arrival == 2) {
			u->order_state[n] = 0;
		} else if (kp_band(carry(g, n), CARRY_GAS) == 0) {
			kp_order_prepend_target(g, n, KP_ORD_WAIT_FOR_GAS, t);
			kp_order_done(g, n);
		} else {
			kp_issue_order(g, n, KP_ORD_RETURN_GAS);
		}
	}
}

/* The gas building R of the orders 82 and 83 is valid: a completed one of the worker's owner, the worker's hands free
 * or with minerals or gas. */
static int valid_gas_target(KpGame *g, int64_t n, int64_t r)
{
	return own_gas_building(g, r, n) && (carry(g, n) == 0 || kp_band(carry(g, n), CARRY_RESOURCE) != 0);
}

/* OrderWaitForGas 0x4237a0 (order 82). */
void kp_order_wait_for_gas(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t r = u->order_unit[n];
	if (!valid_gas_target(g, n, r)) {
		stop_done(g, n);
		return;
	}
	if (u->order_state[n] == 0) enter_queue(g, n, r);
}

/* FindGasExitPosition 0x424590: where the worker leaves its gas building, toward the nearest depot along the segment
 * from the building to the depot (both moved 32 pixels to the side) clipped to the building's box grown by the
 * worker's; 0 when no place is found. The heading is written only with a depot. */
static int gas_exit(KpGame *g, int64_t n, int64_t r, int64_t *x, int64_t *y, int64_t *heading)
{
	int64_t d = kp_nearest_depot(g, n);
	if (d != 0 && r != 0) {
		int64_t rx = sprite_x(g, r), ry = sprite_y(g, r), a[2], b[2], rect[4], ox, oy, l, t, rr, bb, wl, wt, wr, wb;
		*heading = kp_direction_to(g, rx, ry, sprite_x(g, d), sprite_y(g, d));
		kp_direction_vector(kp_mod(*heading - 64, 256), GAS_EXIT_SIDE, &ox, &oy);
		a[0] = kp_s16v(rx + ox);
		a[1] = kp_s16v(ry + oy);
		b[0] = kp_s16v(sprite_x(g, d) + ox);
		b[1] = kp_s16v(sprite_y(g, d) + oy);
		kp_type_box(g, g->u.type[r], rx, ry, &l, &t, &rr, &bb);
		kp_type_box(g, g->u.type[n], 0, 0, &wl, &wt, &wr, &wb); /* -left, -up, right, down */
		rect[0] = kp_s16v(l - 1 - wr);
		rect[1] = kp_s16v(t - 1 - wb);
		rect[2] = kp_s16v(rr - wl + 1);
		rect[3] = kp_s16v(bb - wt + 1);
		kp_clip_segment(g, a, b, rect);
		if (kp_find_unit_placement(g, n, b[0], b[1], rect, 1, x, y)) return 1;
		return kp_find_unit_placement(g, n, b[0], b[1], NULL, 1, x, y);
	}
	return kp_find_unit_placement(g, n, sprite_x(g, n), sprite_y(g, n), NULL, 1, x, y);
}

/* OrderHarvestGas 0x4235c0 (order 83): the worker hidden inside its gas building. */
void kp_order_harvest_gas(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t r = u->order_unit[n], x, y, heading;
	int hidden = kp_band(g->w->spr.flags[kp_unit_sprite(g, n)], SPRITE_HIDDEN) != 0;
	if (!hidden && !valid_gas_target(g, n, r)) {
		kp_stop_and_face(g, n);
		kp_order_unlock_done(g, n);
		leave_service(g, n);
		return;
	}
	ensure_reset_collision(g, n);
	if (u->order_state[n] == 0) {
		kp_unit_detach(g, n, 1); /* 0x430390 */
		u->order_state[n] = 5;
		u->order_timer[n] = HARVEST_GAS_TIME;
		return;
	}
	if (u->order_state[n] != 5 || u->order_timer[n] != 0) return;
	heading = u->heading[n];
	if (!gas_exit(g, n, r, &x, &y, &heading)) {
		u->order_timer[n] = GAS_EXIT_RETRY;
		return;
	}
	kp_flingy_set_heading(g, n, heading);
	kp_unit_relocate(g, n, x, y);
	kp_unit_show(g, n);
	if (r != 0) harvest_cargo(g, r, n);
	leave_service(g, n);
	remove_queued(g, n, KP_ORD_RESET_HARVEST_COLLISION);
	if (u->queue_head[n] == KP_NONE) {
		kp_order_append(g, n, kp_band(carry(g, n), CARRY_GAS) != 0 ? KP_ORD_RETURN_GAS
			: KP_DAT(g, units, return_to_idle, u->type[n]), 0, 0, 0, KP_TYPE_NONE, 0);
	}
	kp_order_insert_before(g, n, KP_ORD_RESET_HARVEST_COLLISION, 0, 0, 0, KP_TYPE_NONE, u->queue_head[n]);
	kp_order_unlock_done(g, n);
}

/* RecordDepotApproachSide 0x43da30: the worker traffic on each side of a Hatchery (left +0xd0, top +0xd2, right +0xd4,
 * bottom +0xd6), decayed, then the worker's side gains 25 below 100 (a signed WORD that wraps). */
static void record_depot_side(KpGame *g, int64_t d, int64_t w)
{
	static const int DECAY[4] = { 0xd2, 0xd6, 0xd0, 0xd4 };
	int64_t l, t, r, b, wx = sprite_x(g, w), wy = sprite_y(g, w), v;
	int side, k;
	for (k = 0; k < 4; k++) {
		v = field(g, d, DECAY[k], 2);
		if (v != 0) set_field(g, d, DECAY[k], 2, v - 1);
	}
	kp_type_box(g, g->u.type[d], sprite_x(g, d), sprite_y(g, d), &l, &t, &r, &b);
	if (wy < t) side = 0xd2;
	else if (wy > b) side = 0xd6;
	else if (wx < l) side = 0xd0;
	else side = 0xd4;
	v = field(g, d, side, 2);
	if (kp_s16v(v) < DEPOT_SIDE_LIMIT) set_field(g, d, side, 2, kp_mod(v + DEPOT_SIDE_STEP, 65536));
}

/* OrderReturnCargo 0x423ce0 (orders 84 and 90). */
void kp_order_return_cargo(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t d, arrival, order;
	if (kp_band(carry(g, n), CARRY_RESOURCE) == 0) {
		stop_done(g, n);
		return;
	}
	ensure_reset_collision(g, n);
	d = u->order_unit[n];
	if (d == 0 || !kp_valid_depot(g, d)) {
		d = 0;
		u->order_state[n] = 0;
	}
	if (u->order_state[n] == 0) {
		if (d == 0) {
			d = kp_nearest_depot(g, n);
			if (d == 0) {
				if (u->ai[n] != 0) kp_issue_order(g, n, KP_ORD_COMPUTER_AI);
				else u->queue_timer[n] = RETURN_RETRY;
				return;
			}
		}
		u->order_unit[n] = d;
		if (kp_request_move_to_unit(g, n, d)) u->order_state[n] = 1;
		return;
	}
	if (u->order_state[n] != 1) return;
	arrival = kp_move_arrival(g, n);
	if (arrival == 0) return;
	if (arrival == 2) {
		u->order_unit[n] = 0;
		u->order_state[n] = 0;
		return;
	}
	if (u->ai[n] != 0) order = KP_ORD_COMPUTER_AI;
	else order = kp_band(carry(g, n), CARRY_GAS) != 0 ? KP_ORD_MOVE_TO_GAS : KP_ORD_MOVE_TO_MINERALS;
	if (ref(g, n, W_HARVEST_TARGET) != 0) kp_order_prepend_target(g, n, order, ref(g, n, W_HARVEST_TARGET));
	else kp_order_prepend(g, n, order);
	kp_order_done(g, n);
	deliver_cargo(g, n);
	if (u->type[d] == KP_TYPE_HATCHERY || u->type[d] == KP_TYPE_LAIR || u->type[d] == KP_TYPE_HIVE) record_depot_side(g, d, n);
}

/* The worker follows mineral field m and remembers it (the stores of 0x4239a0). */
static int follow_mineral(KpGame *g, int64_t n, int64_t m)
{
	KpUnits *u = &g->u;
	if (!kp_request_move_to_unit(g, n, m)) return 0;
	set_ref(g, n, W_HARVEST_TARGET, m);
	u->order_unit[n] = m;
	u->order_x[n] = sprite_x(g, m);
	u->order_y[n] = sprite_y(g, m);
	set_field(g, n, U_HARVEST_POINT, 2, kp_mod(u->order_x[n], 65536));
	set_field(g, n, U_HARVEST_POINT + 2, 2, kp_mod(u->order_y[n], 65536));
	return 1;
}

/* OrderMoveToMinerals 0x4239a0 (order 85). */
void kp_order_move_to_minerals(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t t = u->order_unit[n], x, y, f;
	if (carrying_other(g, n)) {
		stop_done(g, n);
		return;
	}
	ensure_reset_collision(g, n);
	if (!mineral(g, t)) {
		int64_t point = field(g, n, U_HARVEST_POINT, 4);
		if (point == 0) {
			x = sprite_x(g, n);
			y = sprite_y(g, n);
		} else {
			x = kp_s16v(point);
			y = kp_s16v(kp_shrv(point, 16));
			set_field(g, n, U_HARVEST_POINT, 4, 0);
		}
		t = nearest_mineral(g, n, x, y);
		if (t == 0) {
			stop_done(g, n);
			return;
		}
		u->order_unit[n] = t;
		u->order_x[n] = sprite_x(g, t);
		u->order_y[n] = sprite_y(g, t);
		u->order_state[n] = 0;
	}
	if (u->order_state[n] == 0) {
		if (!kp_request_move_to_unit(g, n, t)) return;
		set_field(g, n, U_HARVEST_POINT, 4, kp_band(u->order_x[n], 0xffff) + kp_band(u->order_y[n], 0xffff) * 65536);
		set_ref(g, n, W_HARVEST_TARGET, t);
		u->order_state[n] = 1;
		return;
	}
	if (u->order_state[n] == 1) {
		if (!kp_unit_box_within(g, n, NEAR_MINERAL_RANGE, t) && kp_move_arrival(g, n) == 0) return;
		if (field(g, t, R_BUSY, 1) != 0) {
			f = kp_nearest_free_mineral(g, n, sprite_x(g, n), sprite_y(g, n), 0);
			if (f != 0 && f != t) {
				follow_mineral(g, n, f);
				return;
			}
		}
		u->order_state[n] = 3;
	} else if (u->order_state[n] != 3) {
		return;
	}
	if (u->move_x[n] != sprite_x(g, n) || u->move_y[n] != sprite_y(g, n)) return;
	if (kp_band(u->status[n], STATUS_CANNOT_MOVE) != 0) {
		f = nearest_mineral(g, n, sprite_x(g, n), sprite_y(g, n));
		if (f != 0) {
			follow_mineral(g, n, f);
			return;
		}
	} else if (kp_band(carry(g, n), CARRY_MINERALS) != 0) {
		kp_issue_order(g, n, KP_ORD_RETURN_MINERALS);
		return;
	} else {
		kp_flingy_set_target_waypoint(g, n, u->order_x[n], u->order_y[n]);
		kp_order_prepend_target(g, n, KP_ORD_WAIT_FOR_MINERALS, t);
	}
	kp_order_done(g, n);
}

/* OrderWaitForMinerals 0x423c30 (order 86). */
void kp_order_wait_for_minerals(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t t = u->order_unit[n];
	if (!mineral(g, t)) kp_issue_order(g, n, KP_ORD_MOVE_TO_MINERALS);
	else if (u->order_state[n] == 0) enter_queue(g, n, t);
}

/* OrderMiningMinerals 0x423810 (order 87). */
void kp_order_mining_minerals(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t m = u->order_unit[n], dx, dy;
	if (!mineral(g, m)) {
		leave_service(g, n);
		remove_queued(g, n, KP_ORD_HARVEST3);
		kp_issue_order(g, n, KP_ORD_MOVE_TO_MINERALS);
		return;
	}
	ensure_reset_collision(g, n);
	switch (u->order_state[n]) {
	case 0:
		if (!kp_facing_target_waypoint(g, n)) return;
		kp_direction_vector(u->heading[n], MINING_OFFSET, &dx, &dy);
		u->order_x[n] = kp_s16v(sprite_x(g, n) + dx);
		u->order_y[n] = kp_s16v(sprite_y(g, n) + dy);
		u->order_state[n] = 4;
		/* fall through */
	case 4:
		kp_sprite_play(g->w, kp_unit_sprite(g, n), KP_ANIM_ALMOST_BUILT, 1);
		u->order_timer[n] = MINING_TIME;
		u->order_state[n] = 5;
		return;
	case 5:
		if (u->order_timer[n] != 0) return;
		kp_sprite_play(g->w, kp_unit_sprite(g, n), KP_ANIM_GND_ATTK_TO_IDLE, 1);
		harvest_cargo(g, m, n);
		release_service(g, n, m);
		remove_queued(g, n, KP_ORD_HARVEST3);
		kp_issue_order(g, n, kp_band(carry(g, n), CARRY_MINERALS) != 0 ? KP_ORD_RETURN_MINERALS : KP_ORD_MOVE_TO_MINERALS);
		return;
	}
}

/* OrderHarvest3 0x423c70 (order 88): the served resource goes. */
void kp_order_harvest3(KpGame *g, int64_t n)
{
	leave_service(g, n);
	kp_order_unlock_done(g, n);
}

/* OrderHarvest4 0x423cc0 (order 89): the worker leaves the waiting list. */
void kp_order_harvest4(KpGame *g, int64_t n)
{
	int64_t r = ref(g, n, W_RESOURCE);
	if (r != 0) unlink_waiting(g, r, n);
	kp_order_unlock_done(g, n);
}

/* HasHarvestOrderQueued 0x424550. */
static int harvest_queued(KpGame *g, int64_t n)
{
	int64_t head = g->u.queue_head[n], o;
	if (head == KP_NONE) return 0;
	o = g->orders.order[head];
	return o == KP_ORD_HARVEST1 || o == KP_ORD_MOVE_TO_GAS || o == KP_ORD_WAIT_FOR_GAS || o == KP_ORD_HARVEST_GAS
		|| o == KP_ORD_MOVE_TO_MINERALS || o == KP_ORD_WAIT_FOR_MINERALS || o == KP_ORD_MINING_MINERALS
		|| o == KP_ORD_HARVEST3 || o == KP_ORD_HARVEST4;
}

/* Order_ResetHarvestCollision 0x42e0d0 (order 151). */
void kp_order_reset_harvest_collision(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	if (harvest_queued(g, n)) {
		kp_order_append(g, n, KP_ORD_RESET_HARVEST_COLLISION, 0, 0, 0, KP_TYPE_NONE, 0);
		kp_order_unlock_done(g, n);
		return;
	}
	u->status[n] = kp_band(u->status[n], kp_bnot(STATUS_IGNORE_TILE_COLLISION | STATUS_NO_COLLIDE));
	kp_update_overlap_flags(g, n);
	if (u->queue_head[n] == KP_NONE) kp_order_append(g, n, KP_DAT(g, units, return_to_idle, u->type[n]), 0, 0, 0, KP_TYPE_NONE, 0);
	kp_reset_movement(g, n);
	kp_order_unlock_done(g, n);
}
