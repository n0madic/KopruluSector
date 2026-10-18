/* The computer's spending (kp_game.h KpPlayerAi), ported from the atlas (P28, P29; docs/research/native-economy.md,
 * native-spending-dispatch.md, native-default-production.md, native-training.md): the spending heap
 * (PushSpendingRequest 0x482040, PopSpendingRequest 0x482310, the morph parents 0x482130), the demand and the
 * budget (0x483680, 0x4836d0, RemoveTopRequestCost 0x482260, RefreshAiBudget 0x4823d0 with 0x487730, the
 * affordability tests 0x4826d0, 0x482720, 0x482770), the requests (RequestUnitProduction 0x482990,
 * RequestTownUnit 0x482ae0, ConvertTownRequest 0x482b60, CompleteTopRequest 0x482bc0, DropTopRequest 0x482c70), the
 * unit limit IsUnitLimitReached 0x4835e0, the building AI RunBuildingAi 0x487110 with the default production
 * ChooseDefaultProduction 0x4849c0 and the consumers of the request kinds, and the training metadata of a
 * producer's queue (EnqueueUnitWithAiTag 0x41b550, 0x4844a0, 0x484f70, 0x484f90).
 *
 * The heap holds at most 63 requests {priority, kind, id, context}, a max-heap on the priority byte; a full heap
 * overwrites its last entry. RefreshAiBudget empties it at every spending pass, so a request lives until the next pass
 * of its player unless a consumer takes it. */
#include "kp_game_internal.h"

#define QUEUE_SLOTS 5
#define LIMIT_FORBIDDEN 255
#define DAT_DOUBLE_SUPPLY 0x400
#define DAT_WORKER 0x8
#define DAT_ADDON 0x2
#define GROUP_ZERG 0x1
#define GROUP_TERRAN 0x2
#define GROUP_PROTOSS 0x4
#define DEFAULT_PRIORITY 50
#define RICH_PRIORITY 160
#define RICH_STOCK 1500
#define DEFAULT_MINERALS 600
#define DEFAULT_GAS 300
#define MORPH_PRIORITY_DROP 1
/* PlayerAi +0x218 */
#define SPEND_COUNTER 9
#define TIMED_REQUEST_PAUSE 30
#define URGENT_PRIORITY 140
#define GUARD_PRIORITY 50
#define MILITARY_PRODUCTION_START 10


static int64_t player_ai(KpGame *g, int64_t p)
{
	if (kp_u32v(p) >= KP_AI_PLAYERS) kp_game_fail(g, "ai: the PlayerAi record of player %lld", (long long)p);
	return p;
}

static int64_t count_all(KpGame *g, int64_t t, int64_t p) { return kp_u32v(g->count_all[t * KP_PLAYERS + p]); }

/* IsUnitLimitReached 0x4835e0: define_max's limit of t (0 none, 255 forbidden) reached by the player's units (both
 * Siege Tank modes count as one type). */
int kp_ai_unit_limit_reached(KpGame *g, int64_t p, int64_t t)
{
	int64_t limit, n;
	if (kp_u32v(t) >= KP_UNIT_TYPES) kp_game_fail(g, "ai: the unit limit of type %lld (0x4835e0)", (long long)t);
	limit = g->pai.limits[player_ai(g, p)][t];
	if (limit == 0) return 0;
	if (limit == LIMIT_FORBIDDEN) return 1;
	n = count_all(g, t, p);
	if (t == KP_TYPE_SIEGE_TANK) n = kp_u32v(n + count_all(g, KP_TYPE_SIEGE_TANK_SIEGED, p));
	return limit <= n;
}

/* GetUnitCost 0x4087c0: minerals, gas and supply (a byte, doubled for a two-slot unit). */
static void unit_price(KpGame *g, int64_t t, int64_t *minerals, int64_t *gas, int64_t *supply)
{
	*minerals = kp_band(KP_DAT(g, units, mineral_cost, t), 0xffff);
	*gas = kp_band(KP_DAT(g, units, vespene_cost, t), 0xffff);
	*supply = kp_band(KP_DAT(g, units, supply_required, t), 0xff);
	if (kp_band(KP_DAT(g, units, special_ability_flags, t), DAT_DOUBLE_SUPPLY) != 0) *supply = kp_band(*supply * 2, 0xff);
}

/* GetRequestCost 0x481ff0 by the request's kind. */
static void request_price(KpGame *g, int64_t p, int64_t kind, int64_t id, int64_t *minerals, int64_t *gas,
	int64_t *supply)
{
	if (kind == KP_AI_KIND_UPGRADE) {
		*supply = 0;
		kp_upgrade_price(g, p, id, minerals, gas);
	} else if (kind == KP_AI_KIND_TECH) {
		*supply = 0;
		kp_tech_price(g, id, minerals, gas);
	} else {
		unit_price(g, id, minerals, gas, supply);
	}
}

static void add_demand(KpGame *g, int64_t p, int64_t minerals, int64_t gas, int64_t supply)
{
	KpPlayerAi *pai = &g->pai;
	pai->demand_minerals[p] = kp_u32v(pai->demand_minerals[p] + minerals);
	pai->demand_gas[p] = kp_u32v(pai->demand_gas[p] + gas);
	pai->demand_supply[p] = kp_u32v(pai->demand_supply[p] + supply);
}

/* AddUnitDemand 0x483680 and AddRequestDemand 0x4836d0. */
static void add_request_demand(KpGame *g, int64_t p, int64_t kind, int64_t id)
{
	int64_t m, gas, s;
	request_price(g, p, kind, id, &m, &gas, &s);
	add_demand(g, p, m, gas, s);
}

int64_t kp_ai_top_kind(KpGame *g, int64_t p) /* GetTopRequestKind 0x483550 */
{
	return g->pai.heap_count[player_ai(g, p)] != 0 ? g->pai.heap_kind[p][0] : KP_AI_KIND_NONE;
}

int64_t kp_ai_top_id(KpGame *g, int64_t p) { return g->pai.heap_id[p][0]; } /* GetTopRequestId 0x483540 */
int64_t kp_ai_top_context(KpGame *g, int64_t p) { return g->pai.heap_context[p][0]; } /* 0x483530 */

static void swap_entries(KpPlayerAi *pai, int64_t p, int64_t a, int64_t b)
{
	int64_t t;
#define SWAP(f) t = pai->f[p][a]; pai->f[p][a] = pai->f[p][b]; pai->f[p][b] = t;
	SWAP(heap_priority) SWAP(heap_kind) SWAP(heap_id) SWAP(heap_context)
#undef SWAP
}

/* PushSpendingRequest 0x482040: a unit, guard or worker request (and kind 8) past the unit limit is dropped. */
static void push_request(KpGame *g, int64_t p, int64_t kind, int64_t id, int64_t priority, int64_t context)
{
	KpPlayerAi *pai = &g->pai;
	int64_t k, parent;
	if ((kind == KP_AI_KIND_UNIT || kind == KP_AI_KIND_GUARD || kind == KP_AI_KIND_WORKER || kind == KP_AI_KIND_MILITARY)
		&& kp_ai_unit_limit_reached(g, p, id)) {
		return;
	}
	if (pai->heap_count[p] == KP_AI_HEAP) {
		k = KP_AI_HEAP - 1;
	} else {
		k = pai->heap_count[p];
		pai->heap_count[p]++;
	}
	pai->heap_priority[p][k] = kp_band(priority, 0xff);
	pai->heap_kind[p][k] = kp_band(kind, 0xff);
	pai->heap_id[p][k] = kp_band(id, 0xffff);
	pai->heap_context[p][k] = context;
	while (k > 0) {
		parent = (k - 1) / 2;
		if (pai->heap_priority[p][k] <= pai->heap_priority[p][parent]) break;
		swap_entries(pai, p, k, parent);
		k = parent;
	}
}

/* PopSpendingRequest 0x482310: the last entry replaces the root and sifts down (the right child on a tie). */
static void pop_request(KpGame *g, int64_t p)
{
	KpPlayerAi *pai = &g->pai;
	int64_t n, k = 0;
	/* the original's byte count would wrap to 255 and refill the root from past the 63 entries */
	if (pai->heap_count[p] == 0) kp_game_fail(g, "ai: player %lld pops an empty spending heap (0x482310)", (long long)p);
	pai->heap_count[p] = kp_mod(pai->heap_count[p] - 1, 256);
	n = pai->heap_count[p];
	if (n == 0) return;
	pai->heap_priority[p][0] = pai->heap_priority[p][n];
	pai->heap_kind[p][0] = pai->heap_kind[p][n];
	pai->heap_id[p][0] = pai->heap_id[p][n];
	pai->heap_context[p][0] = pai->heap_context[p][n];
	if (n == 1) return;
	for (;;) {
		int64_t left = 2 * k + 1, right = left + 1, child;
		child = right >= n || pai->heap_priority[p][left] > pai->heap_priority[p][right] ? left : right;
		if (pai->heap_priority[p][k] >= pai->heap_priority[p][child]) break;
		swap_entries(pai, p, k, child);
		k = child;
		if (2 * k + 1 > n - 1) break;
	}
}

/* RemoveTopRequestCost 0x482260: the root's price leaves the demand (saturating) and, when charged, the budget (the
 * morphed units' prices are their parents' requests). */
static void remove_top_cost(KpGame *g, int64_t p, int charge)
{
	KpPlayerAi *pai = &g->pai;
	int64_t id = kp_ai_top_id(g, p), m, gas, s;
	if (id == KP_TYPE_GUARDIAN || id == KP_TYPE_LURKER || id == KP_TYPE_DEVOURER || id == KP_TYPE_ARCHON || id == KP_TYPE_DARK_ARCHON) return;
	request_price(g, p, pai->heap_kind[p][0], id, &m, &gas, &s);
	pai->demand_minerals[p] = kp_u32v(pai->demand_minerals[p]) > kp_u32v(m) ? kp_u32v(pai->demand_minerals[p] - m) : 0;
	pai->demand_gas[p] = kp_u32v(pai->demand_gas[p]) > kp_u32v(gas) ? kp_u32v(pai->demand_gas[p] - gas) : 0;
	pai->demand_supply[p] = kp_u32v(pai->demand_supply[p]) > kp_u32v(s) ? kp_u32v(pai->demand_supply[p] - s) : 0;
	if (!charge) return;
	pai->minerals[p] = kp_u32v(pai->minerals[p] - m);
	pai->gas[p] = kp_u32v(pai->gas[p] - gas);
	pai->supply[p] = kp_u32v(pai->supply[p] - s);
}

void kp_ai_complete_top(KpGame *g, int64_t p) /* CompleteTopRequest 0x482bc0 */
{
	remove_top_cost(g, p, 1);
	pop_request(g, p);
}

void kp_ai_drop_top(KpGame *g, int64_t p) /* DropTopRequest 0x482c70 */
{
	remove_top_cost(g, p, 0);
	pop_request(g, p);
}

/* PushMorphParentRequests 0x482130: a morphed unit also requests its parents at one priority less. */
static void push_morph_parents(KpGame *g, int64_t p, int64_t t, int64_t priority, int64_t context)
{
	int64_t parent, count, k;
	priority = kp_band(priority - MORPH_PRIORITY_DROP, 0xff);
	switch (t) {
	case KP_TYPE_GUARDIAN: case KP_TYPE_DEVOURER: parent = KP_TYPE_MUTALISK; count = 1; break;
	case KP_TYPE_ARCHON: parent = KP_TYPE_HIGH_TEMPLAR; count = 2; break;
	case KP_TYPE_DARK_ARCHON: parent = KP_TYPE_DARK_TEMPLAR; count = 2; break;
	case KP_TYPE_LURKER: parent = KP_TYPE_HYDRALISK; count = 1; break;
	default: return;
	}
	for (k = 0; k < count; k++) {
		add_request_demand(g, p, KP_AI_KIND_UNIT, parent);
		push_request(g, p, KP_AI_KIND_UNIT, parent, priority, context);
	}
}

/* RequestUnitProduction 0x482990. */
void kp_ai_request_unit(KpGame *g, int64_t p, int64_t context, int64_t t, int64_t priority, int keep, int bypass)
{
	KpPlayerAi *pai = &g->pai;
	if (!bypass && kp_band(pai->flags[p], KP_AIF_SUPPRESS_UNITS) != 0) return;
	if (!keep) pai->flags[p] = kp_band(pai->flags[p], kp_bnot(KP_AIF_BUDGET_REFRESHED)); /* the mask 0xfffe */
	pai->flags[p] = kp_bor(pai->flags[p], KP_AIF_REQUESTED);
	add_request_demand(g, p, KP_AI_KIND_UNIT, t);
	push_request(g, p, KP_AI_KIND_UNIT, t, priority, context);
	push_morph_parents(g, p, t, priority, context);
}

/* PushKind8UnitRequest 0x482930: a support unit (an Observer) without context. */
void kp_ai_request_military_unit(KpGame *g, int64_t p, int64_t t, int64_t priority)
{
	add_request_demand(g, player_ai(g, p), KP_AI_KIND_UNIT, t);
	push_request(g, p, KP_AI_KIND_MILITARY, t, priority, 0);
}

/* RequestGuardReplacement 0x482a40: a lost guard's unit (a sieged tank as a tank) at priority 50 with the guard as its
 * context, and the parents of a morph for the region of its home; held back within 30 of the last timed request. */
void kp_ai_request_guard(KpGame *g, int64_t p, int64_t k)
{
	int64_t stamp = g->pai.timed_request[player_ai(g, p)], t = g->guards.type[k];
	if (stamp != 0 && kp_u32v(kp_scalar(g, KP_ELAPSED) - stamp) < TIMED_REQUEST_PAUSE) return;
	if (t == KP_TYPE_SIEGE_TANK_SIEGED) t = KP_TYPE_SIEGE_TANK;
	add_request_demand(g, p, KP_AI_KIND_UNIT, t);
	push_request(g, p, KP_AI_KIND_GUARD, t, GUARD_PRIORITY, KP_AI_CONTEXT(KP_AI_CONTEXT_GUARD, k));
	push_morph_parents(g, p, t, GUARD_PRIORITY, KP_AI_CONTEXT(KP_AI_CONTEXT_REGION,
		kp_region_at(g, kp_s16v(g->guards.x1[k]), kp_s16v(g->guards.y1[k])))); /* GetGuardHomeRegion 0x4b0930 */
}

/* RunAiSpendingPass 0x483400 for player p (a computer whose script started): the countdowns, the budget, the guards,
 * after the first 10 the regions' military production, and the towns' requests. */
void kp_ai_spending_pass(KpGame *g, int64_t p)
{
	KpPlayerAi *pai = &g->pai;
	if (pai->placement_throttle[p] != 0) pai->placement_throttle[p]--;
	if (pai->countdown0[p] != 0) pai->countdown0[p]--;
	if (pai->countdown1[p] != 0) pai->countdown1[p]--;
	if (pai->countdown2[p] != 0) pai->countdown2[p]--;
	kp_ai_refresh_budget(g, p);
	kp_guard_rebuild(g, p); /* 0x4b2320 */
	if (kp_u32v(kp_scalar(g, KP_ELAPSED)) >= MILITARY_PRODUCTION_START) kp_ai_military_production(g, p); /* 0x4af930 */
	kp_ai_process_town_requests(g, p); /* 0x4865b0 */
}

/* ConvertTownRequest 0x482b60 (town kinds 0 unit, 1 upgrade, else tech) and RequestTownUnit 0x482ae0's kind: a worker
 * 4, an add-on 7, another unit 3. */
static int64_t town_unit_kind(KpGame *g, int64_t t)
{
	int64_t flags = KP_DAT(g, units, special_ability_flags, t);
	if (kp_band(flags, DAT_WORKER) != 0) return KP_AI_KIND_WORKER;
	return kp_band(flags, DAT_ADDON) != 0 ? KP_AI_KIND_ADDON : KP_AI_KIND_BUILDING;
}

void kp_ai_convert_town_request(KpGame *g, int64_t p, int64_t town, int64_t kind, int64_t id, int64_t priority)
{
	int64_t k = kind == 0 ? town_unit_kind(g, id) : kind == 1 ? KP_AI_KIND_UPGRADE : KP_AI_KIND_TECH;
	add_request_demand(g, p, k, id);
	push_request(g, p, k, id, priority, KP_AI_CONTEXT(KP_AI_CONTEXT_TOWN, town));
}

/* RequestTownUnit 0x482ae0: a town's worker, building or add-on, held back within 30 of the last timed request unless
 * it is the race's gas building or urgent (priority 140). */
void kp_ai_request_town_unit(KpGame *g, int64_t p, int64_t town, int64_t t, int64_t priority)
{
	int64_t stamp = g->pai.timed_request[player_ai(g, p)], kind;
	if (stamp != 0 && kp_u32v(kp_scalar(g, KP_ELAPSED) - stamp) < TIMED_REQUEST_PAUSE
		&& t != kp_ai_race_gas_building(g, p) && priority != URGENT_PRIORITY) {
		return;
	}
	kind = town_unit_kind(g, t);
	add_request_demand(g, p, kind, t);
	push_request(g, p, kind, t, priority, KP_AI_CONTEXT(KP_AI_CONTEXT_TOWN, town));
}

/* GetGatherSplitPercent 0x483030: the share of gas among the demand's minerals and gas, all of one when only it is
 * short of the stock. */
void kp_ai_gather_split(KpGame *g, int64_t p, int64_t *mineral_percent, int64_t *gas_percent)
{
	const KpPlayerAi *pai = &g->pai;
	int64_t m = pai->demand_minerals[player_ai(g, p)], gas = pai->demand_gas[p], dm, dg;
	if (gas == 0) {
		*gas_percent = 0;
		*mineral_percent = 100;
		return;
	}
	if (m == 0) {
		*gas_percent = 100;
		*mineral_percent = 0;
		return;
	}
	dm = kp_s32v(m - g->players.minerals[p]);
	dg = kp_s32v(gas - g->players.gas[p]);
	if (dm > 0 && dg <= 0) {
		*mineral_percent = 100;
		*gas_percent = 0;
	} else if (dm <= 0 && dg > 0) {
		*mineral_percent = 0;
		*gas_percent = 100;
	} else {
		*gas_percent = kp_band(kp_udiv64(g, kp_mulv(gas, 100), kp_u32v(m + gas)), 0xff);
		*mineral_percent = kp_band(100 - *gas_percent, 0xff);
	}
}

/* SubtractPendingConstructionCosts 0x487730: the prices of the buildings the towns' workers are about to place. */
static void pending_construction(KpGame *g, int64_t p, int64_t *minerals, int64_t *gas)
{
	const KpTowns *t = &g->towns;
	const KpWorkerAis *w = &g->workers;
	int64_t k, r;
	for (k = t->heads[p]; k != -1; k = t->next[k]) {
		for (r = t->workers[k]; r != -1; r = w->next[r]) {
			int64_t n = w->unit[r], o = g->u.order[n];
			if (o != KP_ORD_PLACE_BUILDING && o != KP_ORD_PLACE_PROTOSS_BUILDING && o != KP_ORD_DRONE_LAND && o != KP_ORD_DRONE_BUILD
				&& o != KP_ORD_DRONE_START_BUILD) {
				continue;
			}
			if (!kp_check_unit_cost(g, p, kp_band(*kp_queue_entry(g, n, kp_mod(g->u.queue_slot[n], QUEUE_SLOTS)), 0xffff), 1, 1)) {
				continue;
			}
			*minerals = kp_u32v(*minerals - g->cost_minerals[p]);
			*gas = kp_u32v(*gas - g->cost_gas[p]);
		}
	}
}

/* RefreshAiBudget 0x4823d0: the budget is the stock less the buildings being placed, the supply the race worker's free
 * supply; the demand and the heap are emptied. */
static void refresh_budget(KpGame *g, int64_t p)
{
	KpPlayerAi *pai = &g->pai;
	pai->minerals[p] = g->players.minerals[p];
	pai->flags[p] = kp_bor(pai->flags[p], KP_AIF_BUDGET_REFRESHED);
	pai->demand_minerals[p] = pai->demand_gas[p] = pai->demand_supply[p] = 0;
	pai->heap_count[p] = 0;
	pai->gas[p] = g->players.gas[p];
	pending_construction(g, p, &pai->minerals[p], &pai->gas[p]);
	pai->supply[p] = kp_u32v(kp_free_supply(g, kp_ai_race_worker(g, p), p));
	pai->spend_counter[p] = SPEND_COUNTER;
}

static int within_budget(KpGame *g, int64_t p)
{
	const KpPlayerAi *pai = &g->pai;
	return kp_u32v(g->cost_minerals[p]) <= kp_u32v(pai->minerals[p]) && kp_u32v(g->cost_gas[p]) <= kp_u32v(pai->gas[p])
		&& kp_u32v(g->cost_supply[p]) <= kp_u32v(pai->supply[p]);
}

/* CanAiAffordUnit 0x4826d0, CanAiAffordTech 0x482720, CanAiAffordUpgrade 0x482770: the stock covers the price
 * (with feedback) and the price fits the budget. */
int kp_ai_afford_unit(KpGame *g, int64_t p, int64_t t) { return kp_check_unit_cost(g, p, t, 1, 1) && within_budget(g, p); }
static int afford_tech(KpGame *g, int64_t p, int64_t t) { return kp_check_tech_cost(g, p, t) && within_budget(g, p); }
static int afford_upgrade(KpGame *g, int64_t p, int64_t t) { return kp_check_upgrade_cost(g, p, t) && within_budget(g, p); }

/* IsProducerRaceCompatible 0x4860a0: the requested type's race group is the producer's; KP_ERR_PRODUCER otherwise. */
int kp_ai_race_compatible(KpGame *g, int64_t n, int64_t t)
{
	int64_t want = KP_DAT(g, units, staredit_group_flags, t), have = KP_DAT(g, units, staredit_group_flags, g->u.type[n]);
	int64_t group = kp_band(want, GROUP_TERRAN) != 0 ? GROUP_TERRAN : kp_band(want, GROUP_ZERG) != 0 ? GROUP_ZERG : GROUP_PROTOSS;
	if (kp_band(have, group) != 0) return 1;
	kp_scalar_set(g, KP_REQUIREMENT_ERROR, KP_ERR_PRODUCER);
	return 0;
}

/* The retriable requirement errors: the request goes and the building AI runs again. */
static int retriable_error(KpGame *g)
{
	int64_t e = kp_scalar(g, KP_REQUIREMENT_ERROR);
	return e == KP_ERR_UNAVAILABLE || e == KP_ERR_NO_RECIPE || e == KP_ERR_DISALLOWED || e == KP_ERR_MISSING
		|| e == KP_ERR_UNREQUIRED || e == KP_ERR_RESEARCHED;
}

/* ---- the training metadata of a producer's queue ---- */

static int building_record(KpGame *g, int64_t n, int64_t *r)
{
	int64_t ai = g->u.ai[n];
	if (ai == 0 || kp_ai_ref_kind(ai) != KP_AI_BUILDING) return 0;
	*r = kp_ai_ref_index(ai);
	return 1;
}

/* ClearTrainingAiMeta 0x484f70. */
void kp_ai_training_clear(KpGame *g, int64_t n, int64_t slot)
{
	int64_t r;
	if (!building_record(g, n, &r)) return;
	g->buildings.tags[r][slot] = g->buildings.contexts[r][slot] = 0;
}

/* CopyTrainingAiMeta 0x484f90: slot `to` takes slot `from`'s tag and context (from keeps them). */
void kp_ai_training_copy(KpGame *g, int64_t n, int64_t to, int64_t from)
{
	int64_t r;
	if (!building_record(g, n, &r)) return;
	g->buildings.tags[r][to] = g->buildings.tags[r][from];
	g->buildings.contexts[r][to] = g->buildings.contexts[r][from];
}

/* EnqueueUnitWithAiTag 0x41b550 with SetTrainingAiMeta 0x4844a0 (the producer's record, whatever its type). */
static int enqueue_tagged(KpGame *g, int64_t n, int64_t t, int64_t tag, int64_t context)
{
	int64_t k, slot = -1, r;
	for (k = 0; k < QUEUE_SLOTS && slot < 0; k++) {
		int64_t s = kp_mod(g->u.queue_slot[n] + k, QUEUE_SLOTS);
		if (kp_band(*kp_queue_entry(g, n, s), 0xffff) == KP_TYPE_NONE) slot = s;
	}
	if (slot < 0 || !kp_enqueue_unit(g, n, t)) return 0;
	if (g->u.ai[n] == 0) kp_game_fail(g, "ai: the training tag of a producer without AI (0x4844a0)");
	r = kp_ai_ref_index(g->u.ai[n]);
	if (kp_ai_ref_kind(g->u.ai[n]) != KP_AI_BUILDING) {
		kp_game_fail(g, "ai: the training tag of a record of type %lld (0x4844a0)", (long long)kp_ai_ref_kind(g->u.ai[n]));
	}
	g->buildings.tags[r][slot] = tag;
	g->buildings.contexts[r][slot] = context;
	return 1;
}

/* ---- the building AI ---- */

static int run_building(KpGame *g, int64_t n);

/* TakeWantedUnit 0x482be0. */
static int take_wanted(KpGame *g, int64_t p, int64_t *t)
{
	if (g->pai.wanted_unit[p] == 0) return 0;
	*t = g->pai.wanted_unit[p] - 1;
	g->pai.wanted_unit[p] = 0;
	return 1;
}

/* IsDefaultUnitBlocked 0x4878d0. */
static int default_blocked(KpGame *g, int64_t n, int64_t t)
{
	if (kp_can_train_unit(g, t, g->u.owner[n], n) != 1) return 1;
	return kp_ai_unit_limit_reached(g, g->u.owner[n], t);
}

/* The producers' default units (table 0x484c3c): the first unless it is blocked, else the second (none -1); a test
 * of its requirements and limit (IsDefaultUnitBlocked), or of its limit only. */
enum { BY_REQUIREMENTS, BY_LIMIT };
static const struct {
	int64_t producer, first, first_test, second, second_test;
} DEFAULT_UNITS[] = {
	{ KP_TYPE_LARVA, KP_TYPE_HYDRALISK, BY_REQUIREMENTS, KP_TYPE_MUTALISK, BY_REQUIREMENTS },
	{ KP_TYPE_BARRACKS, KP_TYPE_MARINE, BY_LIMIT, KP_TYPE_GHOST, BY_REQUIREMENTS },
	{ KP_TYPE_FACTORY, KP_TYPE_GOLIATH, BY_REQUIREMENTS, KP_TYPE_SIEGE_TANK, BY_REQUIREMENTS },
	{ KP_TYPE_STARPORT, KP_TYPE_WRAITH, BY_LIMIT, KP_TYPE_BATTLECRUISER, BY_REQUIREMENTS },
	{ KP_TYPE_ROBOTICS_FACILITY, KP_TYPE_REAVER, BY_REQUIREMENTS, -1, BY_REQUIREMENTS },
	{ KP_TYPE_GATEWAY, KP_TYPE_DRAGOON, BY_REQUIREMENTS, KP_TYPE_ZEALOT, BY_LIMIT },
	{ KP_TYPE_STARGATE, KP_TYPE_SCOUT, BY_LIMIT, KP_TYPE_CARRIER, BY_REQUIREMENTS },
};

static int default_unit_fits(KpGame *g, int64_t p, int64_t n, int64_t t, int64_t test)
{
	return t >= 0 && (test == BY_LIMIT ? !kp_ai_unit_limit_reached(g, p, t) : !default_blocked(g, n, t));
}

/* The default unit of producer n (-1 none). */
static int64_t default_unit(KpGame *g, int64_t p, int64_t n)
{
	size_t k;
	for (k = 0; k < sizeof DEFAULT_UNITS / sizeof DEFAULT_UNITS[0]; k++) {
		if (DEFAULT_UNITS[k].producer != g->u.type[n]) continue;
		if (default_unit_fits(g, p, n, DEFAULT_UNITS[k].first, DEFAULT_UNITS[k].first_test)) return DEFAULT_UNITS[k].first;
		if (default_unit_fits(g, p, n, DEFAULT_UNITS[k].second, DEFAULT_UNITS[k].second_test)) return DEFAULT_UNITS[k].second;
		return -1;
	}
	return -1;
}

/* ChooseDefaultProduction 0x4849c0: an idle producer's default unit or the script's wanted unit is requested; nonzero
 * when the heap then has a request. */
static int default_production(KpGame *g, int64_t p, int64_t n, int64_t priority)
{
	KpUnits *u = &g->u;
	int64_t t = -1, choice;
	int explicit_unit = 0;
	if (kp_ai_producer_busy(g, n) || u->secondary_order[n] != KP_ORD_NOTHING) return 0;
	if (take_wanted(g, p, &t)) {
		if (kp_guard_vacancy(g, p, t)) { /* the guards' retry instead, and the producer's default unit */
			kp_guard_reset_retry(g, p, t);
			t = -1;
		}
	} else if (kp_s32v(g->players.minerals[p]) < DEFAULT_MINERALS || kp_s32v(g->players.gas[p]) < DEFAULT_GAS
		|| kp_band(g->pai.flags[p], KP_AIF_DEFAULT_BUILD_OFF) != 0) {
		return 0;
	}
	if (t != -1) {
		explicit_unit = 1;
		if (t == KP_TYPE_NONE) return 0;
		choice = t;
	} else {
		choice = default_unit(g, p, n);
		if (choice < 0) return 0;
	}
	kp_ai_request_unit(g, p, KP_AI_CONTEXT(KP_AI_CONTEXT_REGION, kp_unit_region(g, n)), choice, priority, 1, explicit_unit);
	return kp_ai_top_kind(g, p) != KP_AI_KIND_NONE;
}

#define STATUS_COMPLETED 0x1
#define STATUS_IN_TRANSPORT 0x40
#define MERGE_ENERGY 75 /* a military High Templar's preferred energy (the energy's high byte) */
#define NO_ENERGY 9999

/* A military unit's region state (its record's region). */
static int64_t military_state(KpGame *g, int64_t n)
{
	int64_t mp, mr;
	kp_ai_military_record(g, n, &mp, &mr);
	return g->air.state[mp][mr];
}

/* SelectHydraliskParent 0x4b0350 and SelectMutaliskParent 0x4b03c0 (native-morph-selection.md): player p's first
 * guard unit of type t, or first military one in a region of state 0, 4 or 5, else the last military one; without a
 * previous attacker or a target. */
static int64_t morph_parent(KpGame *g, int64_t p, int64_t t)
{
	KpUnits *u = &g->u;
	int64_t n, fallback = 0, state;
	for (n = g->player_units[p]; n != 0; n = u->player_next[n]) {
		if (kp_unit_inactive(g, n) || u->type[n] != t || u->auto_target[n] != 0 || u->order_unit[n] != 0 || u->ai[n] == 0) continue;
		if (kp_ai_unit_record_type(g, n) == KP_AI_GUARD) return n;
		if (kp_ai_unit_record_type(g, n) != KP_AI_MILITARY) continue;
		fallback = n;
		state = military_state(g, n);
		if (state == KP_RSTATE_IDLE || state == KP_RSTATE_WATCH || state == KP_RSTATE_BASE) return n;
	}
	return fallback;
}

/* SelectDarkTemplarPair 0x4b0250 and SelectHighTemplarPair 0x4b0430 (native-morph-selection.md): the first two
 * guard units, or military ones in a region of state 0, 4, 5, 8 or 9 (a High Templar with energy 75), that are not
 * merging yet; else the military fallback pair (Dark Templar: the first and the last; High Templar: the ordered
 * lowest-energy updates). The outputs are written even when no pair follows. */
static int merge_pair(KpGame *g, int64_t p, int64_t t, int64_t merge, int64_t *first, int64_t *second)
{
	KpUnits *u = &g->u;
	int64_t n, f1 = 0, f2 = 0, e1 = NO_ENERGY, e2 = NO_ENERGY, state, energy, kind;
	int counter = 1;
	for (n = g->player_units[p]; n != 0; n = u->player_next[n]) {
		if (kp_unit_inactive(g, n) || u->type[n] != t || kp_band(u->status[n], STATUS_COMPLETED) == 0
			|| kp_band(u->status[n], STATUS_IN_TRANSPORT) != 0 || u->order[n] == merge || u->ai[n] == 0) {
			continue;
		}
		kind = kp_ai_unit_record_type(g, n);
		if (kind != KP_AI_GUARD && kind != KP_AI_MILITARY) continue;
		if (kind == KP_AI_MILITARY) {
			energy = kp_band(u->energy[n] >> 8, 0xff);
			if (t == KP_TYPE_DARK_TEMPLAR) {
				if (f1 == 0) f1 = n;
				else f2 = n;
			} else if (f1 == 0 || energy < e1) {
				f1 = n;
				e1 = energy;
			} else if (f2 == 0 || energy < e2) {
				f2 = n;
				e2 = energy;
			}
			state = military_state(g, n);
			if (state != KP_RSTATE_IDLE && state != KP_RSTATE_WATCH && state != KP_RSTATE_BASE && state != KP_RSTATE_GATHER && state != KP_RSTATE_ATTACK) continue;
			if (t == KP_TYPE_HIGH_TEMPLAR && energy < MERGE_ENERGY) continue;
		}
		if (counter == 1) { /* 0x4adb10 */
			*first = n;
			counter = 2;
		} else {
			*second = n;
			return 1;
		}
	}
	if (f1 == 0 || f2 == 0) return 0;
	*first = f1;
	*second = f2;
	return 1;
}

/* ConsumeTrainRequest 0x487de0 (kinds 0..2): the producer trains the top unit with its kind and context as the slot's
 * training tag; a campaign AI keeps a request it cannot afford, any other failure drops it (a retriable error then
 * runs the building AI again unless noRetry). */
static int consume_train(KpGame *g, int64_t p, int64_t n, int no_retry)
{
	int64_t t = kp_ai_top_id(g, p), producer = n, second = 0;
	if (t == KP_TYPE_ARCHON || t == KP_TYPE_DARK_ARCHON) {
		int64_t merge = t == KP_TYPE_ARCHON ? KP_ORD_ARCHON_WARP : KP_ORD_DARK_ARCHON_MELD;
		if (!merge_pair(g, p, t == KP_TYPE_ARCHON ? KP_TYPE_HIGH_TEMPLAR : KP_TYPE_DARK_TEMPLAR, merge, &producer, &second)) {
			kp_ai_drop_top(g, p);
			return 0;
		}
		kp_issue_order_target(g, producer, merge, second);
		kp_issue_order_target(g, second, merge, producer);
		kp_ai_complete_top(g, p);
		return 0;
	}
	if (t == KP_TYPE_GUARDIAN || t == KP_TYPE_DEVOURER || t == KP_TYPE_LURKER) {
		producer = morph_parent(g, p, t == KP_TYPE_LURKER ? KP_TYPE_HYDRALISK : KP_TYPE_MUTALISK);
		if (producer == 0) {
			kp_ai_drop_top(g, p);
			return 0;
		}
	}
	if (!kp_ai_race_compatible(g, producer, t) || kp_can_train_unit(g, t, p, producer) != 1) {
		if (retriable_error(g)) {
			kp_ai_drop_top(g, p);
			if (!no_retry) run_building(g, n);
		}
		return 0;
	}
	if (!kp_ai_afford_unit(g, p, t)) {
		if (kp_ai_campaign(g, p)) return 0;
		kp_scalar(g, KP_REQUIREMENT_ERROR); /* read and discarded (0x4b38a0) */
		kp_ai_drop_top(g, p);
		if (!no_retry) run_building(g, n);
		return 0;
	}
	if (kp_ai_unit_limit_reached(g, p, t)) {
		kp_ai_drop_top(g, p);
		return 0;
	}
	if (producer == n) {
		if (!enqueue_tagged(g, producer, t, kp_ai_top_kind(g, p), kp_ai_top_context(g, p))) return 0;
	} else { /* the parent leaves its AI for the request's guard or region */
		if (!kp_enqueue_unit(g, producer, t)) return 0;
		kp_remove_unit_ai(g, producer, 0); /* 0x48dbc0 */
		if (kp_ai_top_kind(g, p) == KP_AI_KIND_GUARD) kp_guard_attach_unit(g, kp_ai_ref_index(kp_ai_top_context(g, p)), producer);
		else kp_ai_military_add(g, p, kp_ai_ref_index(kp_ai_top_context(g, p)), producer, 1); /* 0x4ae760 */
	}
	kp_ai_complete_top(g, p);
	{
		int64_t pt = g->u.type[producer];
		if (pt == KP_TYPE_LARVA || pt == KP_TYPE_MUTALISK || pt == KP_TYPE_HYDRALISK) kp_issue_order(g, producer, KP_ORD_ZERG_UNIT_MORPH);
		else kp_set_secondary_order(g, producer, KP_ORD_TRAIN);
	}
	return 1;
}

static int64_t building_town(KpGame *g, int64_t n)
{
	return g->buildings.town[kp_ai_ref_index(g->u.ai[n])];
}

/* ConsumeWorkerTrainRequest 0x487d30 (kind 4): the town's producer trains a worker. */
static void consume_worker(KpGame *g, int64_t p, int64_t n)
{
	int64_t t;
	if (kp_ai_top_context(g, p) != KP_AI_CONTEXT(KP_AI_CONTEXT_TOWN, building_town(g, n))) return;
	t = kp_ai_top_id(g, p);
	if (!kp_ai_race_compatible(g, n, t) || kp_can_train_unit(g, t, p, n) != 1 || !kp_ai_afford_unit(g, p, t)) return;
	if (kp_ai_unit_limit_reached(g, p, t)) {
		kp_ai_drop_top(g, p);
		return;
	}
	if (!kp_enqueue_unit(g, n, t)) return;
	if (g->u.type[n] == KP_TYPE_LARVA) kp_issue_order(g, n, KP_ORD_ZERG_UNIT_MORPH);
	else kp_set_secondary_order(g, n, KP_ORD_TRAIN);
	kp_ai_complete_top(g, p);
}

/* ConsumeMorphRequest 0x488100 (kind 3): a Larva's Overlord, or the town's building morphs. */
static int consume_morph(KpGame *g, int64_t p, int64_t n)
{
	int64_t t = kp_ai_top_id(g, p);
	if (!kp_ai_afford_unit(g, p, t)) return 0;
	if (t == KP_TYPE_OVERLORD) {
		if (g->u.type[n] != KP_TYPE_LARVA) return 0;
		if (!enqueue_tagged(g, n, t, KP_AI_CONTEXT_TOWN, KP_AI_CONTEXT(KP_AI_CONTEXT_TOWN, building_town(g, n)))) return 0;
		kp_issue_order(g, n, KP_ORD_ZERG_UNIT_MORPH);
		kp_ai_complete_top(g, p);
		return 0;
	}
	if (t != KP_TYPE_LAIR && t != KP_TYPE_HIVE && t != KP_TYPE_SUNKEN_COLONY && t != KP_TYPE_SPORE_COLONY && t != KP_TYPE_GREATER_SPIRE) return 0;
	if (kp_ai_top_context(g, p) != KP_AI_CONTEXT(KP_AI_CONTEXT_TOWN, building_town(g, n))) return 0;
	if (kp_ai_race_compatible(g, n, t) && kp_can_train_unit(g, t, p, n) == 1) {
		if (!kp_enqueue_unit(g, n, t)) return 0;
		kp_issue_order(g, n, KP_ORD_ZERG_BUILDING_MORPH);
		kp_ai_complete_top(g, p);
		return 1;
	}
	if (retriable_error(g)) {
		kp_ai_drop_top(g, p);
		run_building(g, n);
	}
	return 0;
}

#define ADDON_SIZE 64 /* a campaign AI's second search for an add-on's place */
#define U_ADDON_TYPE 0xc4 /* the add-on type a producer is to place (WORD) */

/* ConsumeAddonRequest 0x485b70 (kind 7): the town's producer places the requested add-on where the AI's search finds
 * room beside it (a campaign AI also searches around it); no room drops the request. */
static void consume_addon(KpGame *g, int64_t p, int64_t n)
{
	int64_t t = kp_ai_top_id(g, p), k = building_town(g, n), x, y;
	if (kp_ai_top_context(g, p) != KP_AI_CONTEXT(KP_AI_CONTEXT_TOWN, k)) return;
	if (!kp_ai_race_compatible(g, n, t) || kp_can_train_unit(g, t, p, n) != 1) {
		if (retriable_error(g)) {
			kp_ai_drop_top(g, p);
			run_building(g, n);
		}
		return;
	}
	if (!kp_ai_afford_unit(g, p, t)) return;
	if (!kp_ai_place(g, n, t, 0, &x, &y) /* 0x496240 */
		&& (!kp_ai_campaign(g, p)
			|| !kp_ai_place_from(g, n, t, kp_s16v(g->w->spr.x[kp_unit_sprite(g, n)]), kp_s16v(g->w->spr.y[kp_unit_sprite(g, n)]), 1,
				ADDON_SIZE, &x, &y))) {
		kp_scalar(g, KP_REQUIREMENT_ERROR); /* read and discarded (0x4b38a0) */
		kp_ai_drop_top(g, p);
		run_building(g, n);
		return;
	}
	kp_ai_complete_top(g, g->towns.owner[k]); /* 0x482bc0 */
	kp_issue_order_at(g, n, KP_ORD_PLACE_ADDON, x, y);
	kp_cancel_all_queued(g, n);
	kp_union_set(g, n, U_ADDON_TYPE, 2, t);
}

/* ConsumeObserverRequest 0x488030 (kind 8): the producer trains the requested Observer. */
static int consume_observer(KpGame *g, int64_t p, int64_t n)
{
	int64_t t = kp_ai_top_id(g, p);
	if (t != KP_TYPE_OBSERVER) return 0;
	if (!kp_ai_race_compatible(g, n, t) || kp_can_train_unit(g, t, p, n) != 1) {
		if (retriable_error(g)) {
			kp_ai_drop_top(g, p);
			run_building(g, n);
		}
		return 0;
	}
	if (!kp_ai_afford_unit(g, p, t)) return 0;
	if (kp_ai_unit_limit_reached(g, p, t)) {
		kp_ai_drop_top(g, p);
		return 0;
	}
	if (!enqueue_tagged(g, n, t, KP_AI_TAG_OBSERVER, 0)) return 0;
	kp_set_secondary_order(g, n, KP_ORD_TRAIN);
	kp_ai_complete_top(g, p);
	return 1;
}

/* ConsumeResearchRequest 0x485ca0 (kind 6) and ConsumeUpgradeRequest 0x485d30 (kind 5). */
static void consume_research(KpGame *g, int64_t p, int64_t n, int upgrade)
{
	int64_t id = kp_ai_top_id(g, p);
	int64_t gate = upgrade ? kp_can_upgrade(g, id, p, n) : kp_can_research(g, id, p, n);
	if (gate != 1) {
		if (retriable_error(g)) {
			kp_ai_drop_top(g, p);
			run_building(g, n);
		}
		return;
	}
	if (!(upgrade ? afford_upgrade(g, p, id) : afford_tech(g, p, id))) return;
	if (!(upgrade ? kp_start_upgrade(g, n, id) : kp_start_research(g, n, id))) return;
	kp_issue_order(g, n, upgrade ? KP_ORD_UPGRADE : KP_ORD_RESEARCH);
	kp_ai_complete_top(g, p);
}

/* ServiceOverlord 0x486bf0: an Overlord standing at its move target wanders to a defended region's center (its
 * 0x80000 status test never rejects). */
static void service_overlord(KpGame *g, int64_t n)
{
	int64_t s = kp_unit_sprite(g, n), x, y;
	if (g->u.move_x[n] != kp_s16v(g->w->spr.x[s]) || g->u.move_y[n] != kp_s16v(g->w->spr.y[s])) return;
	kp_ai_patrol_point(g, n, &x, &y); /* 0x4afb20 */
	kp_request_move_to_point(g, n, x, y); /* 0x43a070 */
}


/* ServiceNuclearSilo 0x485390: a Silo trains a missile when it can (any nonzero requirement result) and afford it;
 * the spending heap is not touched. */
static void service_silo(KpGame *g, int64_t n)
{
	int64_t p = g->u.owner[n];
	if (kp_ai_unit_limit_reached(g, p, KP_TYPE_NUCLEAR_MISSILE) || !kp_ai_race_compatible(g, n, KP_TYPE_NUCLEAR_MISSILE)
		|| kp_can_train_unit(g, KP_TYPE_NUCLEAR_MISSILE, p, n) == 0 || !kp_ai_afford_unit(g, p, KP_TYPE_NUCLEAR_MISSILE)
		|| !enqueue_tagged(g, n, KP_TYPE_NUCLEAR_MISSILE, KP_AI_TAG_NUKE, 0)) {
		return;
	}
	kp_issue_order(g, n, KP_ORD_NUKE_TRAIN);
	kp_set_secondary_order(g, n, KP_ORD_TRAIN);
}

/* RunBuildingAi 0x487110: the Overlord, Comsat and Silo services, then a town building's spending: with rich stocks
 * an idle producer first adds its default unit (no retry then), then the top request's consumer. */
static int run_building(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t t = u->type[n], p, kind;
	int no_retry = 0;
	if (t == KP_TYPE_OVERLORD) service_overlord(g, n);
	else if (t == KP_TYPE_COMSAT) kp_ai_reaction_spell(g, n, 0); /* 0x48d3a0 */
	else if (t == KP_TYPE_NUCLEAR_SILO) service_silo(g, n);
	if (u->ai[n] == 0 || kp_ai_unit_record_type(g, n) != KP_AI_BUILDING) return 1;
	p = player_ai(g, u->owner[n]);
	if (kp_s32v(g->players.minerals[p]) > RICH_STOCK && kp_s32v(g->players.gas[p]) > RICH_STOCK
		&& !kp_unit_producing(g, n)) {
		no_retry = default_production(g, p, n, RICH_PRIORITY) != 0;
	}
	kind = kp_ai_top_kind(g, p);
	switch (kind) {
	case KP_AI_KIND_NONE:
		if (!default_production(g, p, n, DEFAULT_PRIORITY)) return 1;
		return consume_train(g, p, n, 1);
	case KP_AI_KIND_UNIT: case KP_AI_KIND_GUARD: return consume_train(g, p, n, no_retry);
	case KP_AI_KIND_BUILDING: return consume_morph(g, p, n);
	case KP_AI_KIND_WORKER: consume_worker(g, p, n); return 1;
	case KP_AI_KIND_UPGRADE: consume_research(g, p, n, 1); return 1;
	case KP_AI_KIND_TECH: consume_research(g, p, n, 0); return 1;
	case KP_AI_KIND_ADDON: consume_addon(g, p, n); return 1;
	case KP_AI_KIND_MILITARY: return consume_observer(g, p, n);
	default: return 1;
	}
}

int kp_ai_run_building(KpGame *g, int64_t n) { return run_building(g, n); }

/* The spending pass's budget (RunAiSpendingPass 0x483400 calls it through the player's cursor). */
void kp_ai_refresh_budget(KpGame *g, int64_t p) { refresh_budget(g, player_ai(g, p)); }
