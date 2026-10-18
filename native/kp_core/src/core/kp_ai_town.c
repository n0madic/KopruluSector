/* The computer's towns (kp_game.h KpTowns, KpWorkerAis, KpBuildingAis), ported from the atlas (P29; docs/research/
 * native-towns.md, native-workers.md): the pools (InitTownAiPools 0x486a20 with 0x483ca0, 0x483da0, 0x483bd0), a
 * town's allocation AllocTown 0x485770, a unit's assignment AssignUnitToTown 0x484cd0 (AllocBuildingAi 0x483e40)
 * and the scripts' StartTown 0x487030 and StartAreaTown 0x486e80.
 *
 * Every pool is a doubly linked free list over fixed records; taking a record pops the free head and prepends it to
 * its list, releasing one prepends it to the free list, and neither clears the payload. Links and references are
 * record indexes, -1 for none. */
#include "kp_game_internal.h"

#include <string.h>

#define NONE (-1)
#define DAT_BUILDING 0x1
#define DAT_WORKER 0x8
#define STATUS_COMPLETED 0x1
#define W_HARVEST_TARGET 2 /* the union word +0xc8 of a worker */
#define U_HARVEST_TARGET 0xc8 /* a worker's resource (its union word W_HARVEST_TARGET) */
#define U_RESEARCH 0xc8 /* the tech a producer researches */
#define U_UPGRADE 0xc9 /* the upgrade a producer upgrades */
#define STATUS_GROUNDED_BUILDING 0x2
/* a worker's assignment: minerals, then the gas references 2..4 taken in groups of four workers */
#define ASSIGN_MINERALS 1
#define ASSIGN_GAS_FIRST 2
#define ASSIGN_GAS_LAST 4
#define ASSIGN_GROUP 4
#define RACE_ZERG 0
#define RACE_TERRAN 1

/* Defense buildings a campaign town's region bookkeeping skips (0x484cd0). */
static int static_defense(int64_t t)
{
	return t == KP_TYPE_MISSILE_TURRET || t == KP_TYPE_BUNKER || t == KP_TYPE_CREEP_COLONY || t == KP_TYPE_SPORE_COLONY
		|| t == KP_TYPE_SUNKEN_COLONY || t == KP_TYPE_PYLON || t == KP_TYPE_PHOTON_CANNON;
}

static int64_t race_of(KpGame *g, int64_t p) { return g->players.race[p]; }

/* GetRaceMainBuilding 0x485ef0. */
int64_t kp_ai_race_main_building(KpGame *g, int64_t p)
{
	int64_t r = race_of(g, p);
	return r == RACE_TERRAN ? KP_TYPE_COMMAND_CENTER : r == RACE_ZERG ? KP_TYPE_HATCHERY : KP_TYPE_NEXUS;
}

/* GetRaceWorker 0x486070. */
int64_t kp_ai_race_worker(KpGame *g, int64_t p)
{
	int64_t r = race_of(g, p);
	return r == RACE_TERRAN ? KP_TYPE_SCV : r == RACE_ZERG ? KP_TYPE_DRONE : KP_TYPE_PROBE;
}

/* GetRaceGasBuilding 0x486100. */
int64_t kp_ai_race_gas_building(KpGame *g, int64_t p)
{
	int64_t r = race_of(g, p);
	return r == RACE_TERRAN ? KP_TYPE_REFINERY : r == RACE_ZERG ? KP_TYPE_EXTRACTOR : KP_TYPE_ASSIMILATOR;
}

/* InitTownAiPools 0x486a20: the WorkerAI, BuildingAI and Town pools' free lists, no towns. */
void kp_towns_init(KpGame *g)
{
	KpTowns *t = &g->towns;
	KpWorkerAis *w = &g->workers;
	KpBuildingAis *b = &g->buildings;
	int64_t k;
	memset(t, 0, sizeof *t);
	memset(w, 0, sizeof *w);
	memset(b, 0, sizeof *b);
	for (k = 0; k < KP_WORKER_AIS; k++) {
		w->next[k] = k + 1 < KP_WORKER_AIS ? k + 1 : NONE;
		w->prev[k] = k > 0 ? k - 1 : NONE;
		w->unit[k] = 0;
		w->town[k] = NONE;
	}
	w->free = 0;
	for (k = 0; k < KP_BUILDING_AIS; k++) {
		b->next[k] = k + 1 < KP_BUILDING_AIS ? k + 1 : NONE;
		b->prev[k] = k > 0 ? k - 1 : NONE;
		b->town[k] = NONE;
	}
	b->free = 0;
	for (k = 0; k < KP_TOWNS; k++) {
		t->next[k] = k + 1 < KP_TOWNS ? k + 1 : NONE;
		t->prev[k] = k > 0 ? k - 1 : NONE;
		t->workers[k] = t->buildings[k] = NONE;
	}
	t->free = 0;
	for (k = 0; k < KP_AI_PLAYERS; k++) t->heads[k] = NONE;
}

/* AllocTown 0x485770: an empty town of player p at (x, y) (low WORDs), its references and requests cleared; its
 * under-attack countdown keeps what the record held. NONE when the pool is empty. */
static int64_t alloc_town(KpGame *g, int64_t p, int64_t x, int64_t y)
{
	KpTowns *t = &g->towns;
	int64_t k = kp_pool_take(t->next, t->prev, &t->free, &t->heads[p]);
	if (k == NONE) return NONE;
	t->owner[k] = p;
	t->initialized[k] = t->worker_count[k] = 0;
	t->x[k] = kp_s16v(x);
	t->y[k] = kp_s16v(y);
	t->mineral[k] = t->repair[k] = t->main[k] = 0;
	t->gas[k][0] = t->gas[k][1] = t->gas[k][2] = 0;
	t->area[k] = t->gas_refresh[k] = 0;
	t->worker_limit[k] = 0;
	memset(t->requests[k], 0, sizeof t->requests[k]);
	return k;
}

/* AssignUnitToTown 0x484cd0: a worker gets a WorkerAI record of town k, a building (not a Geyser), a Larva, an Egg
 * or an Overlord a BuildingAI record; the town learns its main building and gas references. */
void kp_ai_town_assign(KpGame *g, int64_t k, int64_t n)
{
	KpUnits *u = &g->u;
	KpTowns *t = &g->towns;
	int64_t type = u->type[n], flags = KP_DAT(g, units, special_ability_flags, type), r, p = t->owner[k];
	if (kp_band(flags, DAT_WORKER) != 0) {
		KpWorkerAis *w = &g->workers;
		r = kp_pool_take(w->next, w->prev, &w->free, &t->workers[k]);
		if (r == NONE) return;
		w->unit[r] = n;
		w->town[r] = k;
		w->type[r] = KP_AI_WORKER;
		w->assignment[r] = ASSIGN_MINERALS;
		w->time[r] = kp_scalar(g, KP_ELAPSED);
		w->reassign[r] = w->wait[r] = 0;
		kp_set_union_unit(g, n, W_HARVEST_TARGET, 0); /* +0xc8, the resource it harvests */
		u->ai[n] = KP_AI_REF(KP_AI_WORKER, r);
		t->worker_count[k] = kp_mod(t->worker_count[k] + 1, 256);
		return;
	}
	if (!((kp_band(flags, DAT_BUILDING) != 0 && type != KP_TYPE_GEYSER) || type == KP_TYPE_LARVA || type == KP_TYPE_EGG || type == KP_TYPE_OVERLORD)) {
		return;
	}
	{
		KpBuildingAis *b = &g->buildings;
		int e;
		r = kp_pool_take(b->next, b->prev, &b->free, &t->buildings[k]); /* AllocBuildingAi 0x483e40 */
		if (r == NONE) return;
		b->type[r] = KP_AI_BUILDING;
		b->unit[r] = n;
		b->town[r] = k;
		for (e = 0; e < KP_TRAINING_TAGS; e++) b->tags[r][e] = b->contexts[r][e] = 0;
		u->ai[n] = KP_AI_REF(KP_AI_BUILDING, r);
	}
	if ((type == KP_TYPE_HIVE || type == KP_TYPE_LAIR || type == KP_TYPE_HATCHERY) && kp_band(u->status[n], STATUS_COMPLETED) != 0) {
		kp_issue_order(g, n, KP_ORD_COMPUTER_AI);
		kp_set_secondary_order(g, n, KP_ORD_SPREAD_CREEP);
	}
	if (!kp_ai_campaign(g, p)
		|| (kp_band(u->status[n], STATUS_GROUNDED_BUILDING) != 0 && !static_defense(type))) {
		kp_ai_building_region_state(g, p, n);
	}
	if (t->initialized[k] == 0) {
		t->initialized[k] = 1;
		t->mineral[k] = t->gas[k][0] = t->gas[k][1] = t->gas[k][2] = 0;
	}
	if (type == kp_ai_race_main_building(g, u->owner[n]) || kp_valid_depot(g, n)) {
		if (t->main[k] == 0) t->main[k] = n;
	} else if (type == kp_ai_race_gas_building(g, u->owner[n])) {
		int e;
		for (e = 0; e < KP_TOWN_GAS; e++) {
			if (t->gas[k][e] == 0) {
				t->gas[k][e] = n;
				break;
			}
		}
		if (t->area[k] != 0) t->gas_refresh[k] = 1;
	}
}

/* AssignNewUnitToActiveTown 0x484f40: a computer's new unit of a town type (units.dat AI byte bit 0x2) joins its
 * owner's first town, when it has one. */
void kp_ai_assign_to_first_town(KpGame *g, int64_t n)
{
	int64_t p = g->u.owner[n];
	if (kp_controller(g, p) != KP_CTRL_COMPUTER || g->towns.heads[p] == NONE) return;
	kp_ai_town_assign(g, g->towns.heads[p], n); /* AssignUnitToTown 0x484cd0 */
}

/* IsProducerBusy 0x4b14d0: incomplete, or in CompletingArchonSummon 106 or ResetCollision 150. */
int kp_ai_producer_busy(KpGame *g, int64_t n)
{
	int64_t o = g->u.order[n];
	return kp_band(g->u.status[n], STATUS_COMPLETED) == 0 || o == KP_ORD_ARCHON_SUMMON || o == KP_ORD_RESET_COLLISION;
}

/* StartTown 0x487030: player p's town at (x, y) with every live, completed, unoccupied unit without AI; NONE when
 * the player has no units or the pool is empty. A player's first town first frees its parentless guards. */
int64_t kp_ai_start_town(KpGame *g, int64_t p, int64_t x, int64_t y, int64_t area)
{
	KpUnits *u = &g->u;
	KpTowns *t = &g->towns;
	int64_t n = g->player_units[p], k;
	int first;
	if (n == 0) return NONE;
	first = t->heads[p] == NONE;
	if (first) kp_guard_free_parentless(g, p); /* 0x4b21f0 */
	k = alloc_town(g, p, x, y);
	if (k == NONE) return NONE;
	t->area[k] = kp_band(area, 0xff);
	t->gas_refresh[k] = area != 0;
	for (; n != 0; n = u->player_next[n]) {
		if (!kp_unit_inactive(g, n) && !kp_ai_producer_busy(g, n) && u->ai[n] == 0) kp_ai_town_assign(g, k, n);
	}
	if (!kp_ai_campaign(g, t->owner[k])) {
		/* TODO: a non-campaign town's territory 0x4979b0 and strategic targets 0x4b0be0 (outside the campaigns) */
		kp_game_fail(g, "ai: a non-campaign town's territory (0x4979b0, 0x4b0be0)");
	}
	return k;
}

/* StartAreaTown 0x486e80: player p's town over its units in the rectangle (their low WORDs) without AI; its centre is
 * the last main building's (Command Center, Hatchery, Lair, Hive, Nexus), else the first worker's, else the
 * rectangle's middle. NONE when no unit of p is there or the pool is empty. */
int64_t kp_ai_start_area_town(KpGame *g, int64_t p, const int64_t *rect, int64_t area)
{
	KpUnits *u = &g->u;
	KpTowns *t = &g->towns;
	int count, i;
	int64_t *found = kp_query_rect(g, kp_s16v(rect[0]), kp_s16v(rect[1]), kp_s16v(rect[2]), kp_s16v(rect[3]), &count);
	int64_t k = NONE, centre = 0, any = 0;
	for (i = 0; i < count && !any; i++) any = u->owner[found[i]] == p; /* UnitOwnerEquals 0x485760 */
	if (any) k = alloc_town(g, p, 0, 0);
	if (k != NONE) {
		t->area[k] = kp_band(area, 0xff);
		t->gas_refresh[k] = area != 0;
		for (i = 0; i < count; i++) {
			int64_t n = found[i], type = u->type[n];
			if (u->owner[n] != p || u->ai[n] != 0) continue;
			kp_ai_town_assign(g, k, n);
			if (type == KP_TYPE_SCV || type == KP_TYPE_DRONE || type == KP_TYPE_PROBE) {
				if (centre == 0) centre = n;
			} else if (type == KP_TYPE_COMMAND_CENTER || type == KP_TYPE_HATCHERY || type == KP_TYPE_LAIR || type == KP_TYPE_HIVE || type == KP_TYPE_NEXUS) {
				centre = n;
			}
		}
		if (centre != 0) {
			int32_t s = kp_unit_sprite(g, centre);
			t->x[k] = kp_s16v(g->w->spr.x[s]);
			t->y[k] = kp_s16v(g->w->spr.y[s]);
		} else {
			t->x[k] = kp_s16v(rect[0] + kp_s32v(kp_s32v(rect[2] - rect[0]) / 2));
			t->y[k] = kp_s16v(rect[1] + kp_s32v(kp_s32v(rect[3] - rect[1]) / 2));
		}
	}
	kp_query_release(g, found);
	return k;
}

/* ---- the workers ---- */

#define CARRY_GAS 0x1
#define CARRY_MINERALS 0x2
#define R_AI_CLAIM 0xd9 /* a resource's claim by an AI town's worker */
#define REPAIR_RADIUS 2048
#define CROWDED_TOWN 12 /* workers beyond which a free mineral is searched near the worker only */
/* the gas classes of ClassifyGasUnit 0x485e80 */
#define GAS_NONE 0
#define GAS_GEYSER 1
#define GAS_DEPLETED 2
#define GAS_FULL 3

static int64_t town_of_record(KpGame *g, int64_t n)
{
	int64_t ai = g->u.ai[n], k = kp_ai_ref_index(ai);
	switch (kp_ai_ref_kind(ai)) {
	case KP_AI_WORKER: return g->workers.town[k];
	case KP_AI_BUILDING: return g->buildings.town[k];
	}
	kp_game_fail(g, "ai: the town of an AI record of type %lld", (long long)kp_ai_ref_kind(ai));
	return NONE;
}

/* GetUnitTownRepairScv 0x487530: the repair SCV of the unit's AI town (+0x14 of its record), 0 without AI. */
int64_t kp_ai_town_repair_scv(KpGame *g, int64_t n)
{
	if (g->u.ai[n] == 0) return 0;
	return g->towns.repair[town_of_record(g, n)];
}

/* ClassifyGasUnit 0x485e80: a live Geyser, or player p's race's gas building, depleted or not. */
static int64_t gas_class(KpGame *g, int64_t p, int64_t n)
{
	int64_t t;
	if (n == 0 || kp_unit_inactive(g, n)) return GAS_NONE;
	t = g->u.type[n];
	if (t == KP_TYPE_GEYSER) return GAS_GEYSER;
	if (t != kp_ai_race_gas_building(g, p)) return GAS_NONE;
	return kp_band(g->u.union_0xd0[n], 0xffff) != 0 ? GAS_FULL : GAS_DEPLETED;
}

/* FindTownGas 0x485de0: the occurrence-th (1-based) gas reference of a class. */
static int64_t town_gas(KpGame *g, int64_t k, int64_t cls, int64_t occurrence)
{
	const KpTowns *t = &g->towns;
	int64_t e, seen = 0;
	for (e = 0; e < KP_TOWN_GAS; e++) {
		if (t->gas[k][e] == 0 || gas_class(g, t->owner[k], t->gas[k][e]) != cls) continue;
		if (++seen == occurrence) return t->gas[k][e];
	}
	return 0;
}

/* IssueWorkerHarvest 0x485f20: carried resources go back first; else the preferred resource, or the fallback, when it
 * is a completed one other than a Geyser, is harvested and claimed. */
static void issue_harvest(KpGame *g, int64_t w, int64_t preferred, int64_t fallback)
{
	KpUnits *u = &g->u;
	int64_t carry = u->resource_type[w], r = preferred;
	if (kp_band(carry, CARRY_MINERALS) != 0) {
		kp_issue_order(g, w, KP_ORD_RETURN_MINERALS);
		return;
	}
	if (kp_band(carry, CARRY_GAS) != 0) {
		kp_issue_order(g, w, KP_ORD_RETURN_GAS);
		return;
	}
	if (r == 0 || u->type[r] == KP_TYPE_GEYSER || kp_band(u->status[r], STATUS_COMPLETED) == 0) r = fallback;
	if (r == 0 || u->type[r] == KP_TYPE_GEYSER || kp_band(u->status[r], STATUS_COMPLETED) == 0) return;
	kp_issue_order_target(g, w, KP_ORD_HARVEST1, r);
	kp_union_set(g, r, R_AI_CLAIM, 1, 1);
}

/* RepairTargetCallback 0x432870: a unit of the worker's owner, neither worker nor building, whose order targets the
 * worker, on its ground and repairable. */
static int repair_target(KpGame *g, int64_t c, int64_t w)
{
	KpUnits *u = &g->u;
	int64_t flags = KP_DAT(g, units, special_ability_flags, u->type[c]);
	return u->owner[c] == u->owner[w] && kp_band(flags, DAT_WORKER | DAT_BUILDING) == 0 && u->order_unit[c] == w
		&& kp_same_ground_group(g, w, c) && kp_repairable(g, c);
}

/* FindRepairJob 0x4852b0: an SCV repairs its town's first incomplete building, else its most damaged one, that no one
 * repairs in an unthreatened region; without one the town's repair SCV (this worker when the town has none) repairs
 * the nearest unit waiting for it (FindRepairJobForWorker 0x432e00). */
static int repair_job(KpGame *g, int64_t w, int64_t k)
{
	KpUnits *u = &g->u;
	KpTowns *t = &g->towns;
	int64_t r, target = 0, best = 0;
	for (r = t->buildings[k]; r != NONE; r = g->buildings.next[r]) {
		int64_t n = g->buildings.unit[r], max, hp, score;
		if (u->connected[n] != 0 || !kp_ai_unit_region_unthreatened(g, n)) continue;
		if (kp_band(u->status[n], STATUS_COMPLETED) == 0) {
			target = n;
			break;
		}
		hp = kp_sarv(u->hp[n] + 0xff, 8);
		max = kp_sarv(kp_s32v(KP_DAT(g, units, hit_points, u->type[n])), 8);
		if (max == 0) max = hp != 0 ? hp : 1;
		score = kp_u32v(max - hp);
		if (best < score) {
			best = score;
			target = n;
		}
	}
	if (target == 0) {
		if (t->repair[k] == 0) t->repair[k] = w;
		if (t->repair[k] != w) return 0;
		target = kp_nearest_unit_around(g, w, REPAIR_RADIUS, repair_target, w);
		if (target == 0) return 0;
	}
	kp_issue_order_target(g, w, KP_ORD_REPAIR, target);
	u->connected[target] = w;
	return 1;
}

/* RunWorkerAi 0x487580: a worker's wait counts down; then an SCV looks for repairs, the worker for its town's
 * construction, and with a valid main building it harvests by its assignment: minerals (its target, the town's
 * unclaimed mineral, a free one near it) with gas as the fallback, or the town's gas by its code. */
void kp_ai_run_worker(KpGame *g, int64_t w)
{
	KpUnits *u = &g->u;
	KpWorkerAis *wk = &g->workers;
	int64_t r, k, main, assignment;
	if (u->ai[w] == 0) return;
	r = kp_ai_ref_index(u->ai[w]);
	k = wk->town[r];
	if (wk->wait[r] != 0) {
		wk->wait[r]--;
		return;
	}
	wk->time[r] = kp_scalar(g, KP_ELAPSED);
	if (kp_unit_race(g, w) == RACE_TERRAN && repair_job(g, w, k)) return;
	if (kp_ai_town_construction(g, k, w)) return;
	main = g->towns.main[k];
	if (main == 0 || !kp_valid_depot(g, main)) return;
	assignment = wk->assignment[r];
	if (assignment == ASSIGN_MINERALS) {
		int64_t target = kp_union_get(g, w, U_HARVEST_TARGET, 4), mineral, gas;
		int32_t s;
		if (target != 0 && kp_mineral_field(u->type[target])) {
			issue_harvest(g, w, target, 0);
			return;
		}
		mineral = g->towns.mineral[k];
		if (mineral == 0 || kp_union_get(g, mineral, R_AI_CLAIM, 1) != 0) {
			s = kp_unit_sprite(g, w);
			mineral = kp_nearest_free_mineral(g, w, kp_s16v(g->w->spr.x[s]), kp_s16v(g->w->spr.y[s]),
				g->towns.worker_count[k] <= CROWDED_TOWN);
			if (mineral == 0) mineral = g->towns.mineral[k];
		}
		gas = town_gas(g, k, GAS_FULL, 1);
		if (gas == 0) gas = town_gas(g, k, GAS_DEPLETED, 1);
		issue_harvest(g, w, mineral, gas);
		return;
	}
	{
		int64_t gas = town_gas(g, k, GAS_FULL,
			assignment == ASSIGN_GAS_FIRST ? 1 : assignment == ASSIGN_GAS_FIRST + 1 ? 2 : KP_TOWN_GAS), e;
		for (e = 1; gas == 0 && e <= 3; e++) gas = town_gas(g, k, GAS_FULL, e);
		for (e = 1; gas == 0 && e <= 3; e++) gas = town_gas(g, k, GAS_DEPLETED, e);
		issue_harvest(g, w, gas, g->towns.mineral[k]);
	}
}

#define MAX_REASSIGN 3
#define PLACE_SIZE 64 /* the search window of a town's construction (FindBuildingPlacementFrom 0x4962c0) */
#define BUNKER_ANCHOR_CUTS 1 /* ProduceDefenseAnchors 0x496e10's cuts for a Bunker, a Creep Colony or Photon Cannon */
#define DEFENSE_ANCHOR_CUTS 4

/* TakeBunkerRegionPoint 0x4ae380 / TakeTurretRegionPoint 0x4b1470: the centre of player p's first region with the
 * flag, which it loses; 0 for none. */
static int take_region_point(KpGame *g, int64_t p, int64_t flag, int64_t *x, int64_t *y)
{
	int64_t r;
	for (r = 1; r < (int64_t)g->regions.count; r++) {
		if (kp_band(g->air.flags[p][r], flag) == 0) continue;
		g->air.flags[p][r] = kp_band(g->air.flags[p][r], kp_bnot(flag));
		kp_region_center_point(g, r, x, y); /* GetRegionCenterPoint 0x481a60 */
		return 1;
	}
	return 0;
}

/* DropRequestAndRetry 0x485dc0: a failed placement drops the request (the building AI of a worker does nothing
 * more). */
static void drop_and_retry(KpGame *g, int64_t w)
{
	kp_scalar(g, KP_REQUIREMENT_ERROR); /* read and discarded (0x4b38a0) */
	kp_ai_drop_top(g, g->u.owner[w]);
	kp_ai_run_building(g, w);
}

/* ServiceTownConstruction 0x485400: worker w of town k starts the top building request (its town's, or any Supply
 * Depot) when it may build it and the AI can afford it: a gas building over the town's first Geyser, another at the
 * place the AI's search finds from its seed (a Bunker's or Turret's flagged region, an expansion's area, the town's
 * centre). */
int kp_ai_town_construction(KpGame *g, int64_t k, int64_t w)
{
	KpUnits *u = &g->u;
	KpTowns *t = &g->towns;
	int64_t p = u->owner[w], r = kp_ai_ref_index(u->ai[w]), id, x, y, order;
	if (kp_ai_top_kind(g, p) != KP_AI_KIND_BUILDING) return 0;
	id = kp_ai_top_id(g, p);
	if (kp_ai_top_context(g, p) != KP_AI_CONTEXT(KP_AI_CONTEXT_TOWN, g->workers.town[r]) && id != KP_TYPE_SUPPLY_DEPOT) return 0;
	if (g->workers.reassign[r] >= MAX_REASSIGN) return 0;
	if (!kp_ai_race_compatible(g, w, id) || kp_can_train_unit(g, id, p, w) != 1 || !kp_ai_afford_unit(g, p, id)) return 0;
	if (id == kp_ai_race_gas_building(g, t->owner[k])) {
		int64_t geyser = town_gas(g, k, GAS_GEYSER, 1);
		int32_t s;
		if (geyser == 0) return 0;
		s = kp_unit_sprite(g, geyser);
		x = kp_s16v(g->w->spr.x[s]);
		y = kp_s16v(g->w->spr.y[s]);
	} else { /* the search from the seed: a flagged region's centre for a Bunker or Missile Turret, a new Hatchery's
	          * expansion point, else the town's centre (with defense anchors for the defenses) */
		int64_t sx = kp_s16v(t->x[k]), sy = kp_s16v(t->y[k]);
		if (id == KP_TYPE_BUNKER) {
			if (!take_region_point(g, p, KP_RFLAG_WANTS_BUNKER, &sx, &sy)) {
				sx = kp_s16v(t->x[k]);
				sy = kp_s16v(t->y[k]);
			}
			kp_ai_defense_anchors(g, p, kp_band(kp_region_at(g, sx, sy), 0xffff), BUNKER_ANCHOR_CUTS);
		} else if (id == KP_TYPE_MISSILE_TURRET) {
			if (!take_region_point(g, p, KP_RFLAG_WANTS_TURRET, &sx, &sy)) {
				sx = kp_s16v(t->x[k]);
				sy = kp_s16v(t->y[k]);
			}
		} else if (id == KP_TYPE_HATCHERY && t->main[k] != 0) {
			kp_ai_hatchery_seed(g, t->area[k], &sx, &sy); /* GetHatcherySeedForArea 0x496f50 */
		} else if (id == KP_TYPE_CREEP_COLONY || id == KP_TYPE_PHOTON_CANNON) {
			kp_ai_defense_anchors(g, p, kp_band(kp_region_at(g, sx, sy), 0xffff), DEFENSE_ANCHOR_CUTS);
		}
		if (!kp_ai_place_from(g, w, id, sx, sy, 1, PLACE_SIZE, &x, &y)) {
			drop_and_retry(g, w);
			return 0;
		}
		if (id == KP_TYPE_BUNKER) kp_ai_mark_defense_region(g, p, kp_band(kp_region_at(g, x, y), 0xffff), 1); /* 0x4b0030 */
	}
	kp_ai_complete_top(g, t->owner[k]);
	switch (u->type[w]) {
	case KP_TYPE_SCV: order = KP_ORD_PLACE_BUILDING; break;
	case KP_TYPE_DRONE: order = KP_ORD_DRONE_START_BUILD; break;
	case KP_TYPE_PROBE: order = KP_ORD_PLACE_PROTOSS_BUILDING; break;
	default: return 0;
	}
	kp_issue_order_at(g, w, order, x, y);
	kp_cancel_all_queued(g, w);
	kp_enqueue_unit(g, w, id);
	return 1;
}

/* ---- the towns' persistent requests ---- */

#define REQUEST_KIND_UNIT 0
#define REQUEST_KIND_UPGRADE 1
#define REQUEST_KIND_TECH 2
#define REQUEST_COUNT_MAX 30

/* AddTownRequest 0x485fa0: a computer's town keeps the request in its first free slot (count bits clear). */
static void add_request(KpGame *g, int64_t k, int64_t kind, int64_t count, int64_t id, int64_t priority, int64_t special)
{
	KpTowns *t = &g->towns;
	int64_t e = 0, flags;
	if (kp_controller(g, t->owner[k]) != KP_CTRL_COMPUTER) return;
	while (kp_band(t->requests[k][e], 0xf8) != 0) {
		if (e == KP_TOWN_REQUESTS - 1) {
			/* the original then tests and may write the slot past the last, the next record's first bytes */
			kp_game_fail(g, "ai: a town's 101st request (0x485fa0)");
		}
		e++;
	}
	flags = kp_band(kp_band(kp_band(count << 2, 0xff) | kp_band(kind, 3), 0xff) << 1, 0xff) | kp_band(special, 1);
	t->requests[k][e] = flags | kp_band(priority, 0xff) << 8 | kp_band(id, 0xffff) << 16;
}

/* AddTownBuildRequest 0x487550 (build, player_need): count units of type id. */
void kp_ai_town_build_request(KpGame *g, int64_t k, int64_t count, int64_t id, int64_t priority, int64_t special)
{
	if (kp_u32v(count) > REQUEST_COUNT_MAX || kp_u32v(id) >= KP_UNIT_TYPES) return;
	add_request(g, k, REQUEST_KIND_UNIT, count, id, priority, special);
}

/* AddTownUpgradeRequest 0x4878b0 (upgrade id to level count) and AddTownTechRequest 0x487890 (tech). */
void kp_ai_town_upgrade_request(KpGame *g, int64_t k, int64_t level, int64_t id, int64_t priority)
{
	add_request(g, k, REQUEST_KIND_UPGRADE, level, id, priority, 0);
}

void kp_ai_town_tech_request(KpGame *g, int64_t k, int64_t id, int64_t priority)
{
	add_request(g, k, REQUEST_KIND_TECH, 1, id, priority, 0);
}

/* ---- the towns' units, their removal and the worker transfers ---- */

#define RESOURCE_CONTAINER 0x2000
#define FAR_AWAY 99999999 /* MigrateTownIfEmpty 0x485840's initial distance */

/* The morph extras a building of the requested type also counts (tables 0x486a04, 0x487500, 0x487d20): a Hatchery its
 * Lair and Hive, a Lair its Hive, a Spire its Greater Spire, a Creep Colony its Sunken and Spore Colonies. */
static int morph_extras(int64_t t, int64_t *extra)
{
	switch (t) {
	case KP_TYPE_HATCHERY: extra[0] = KP_TYPE_LAIR; extra[1] = KP_TYPE_HIVE; return 2;
	case KP_TYPE_LAIR: extra[0] = KP_TYPE_HIVE; return 1;
	case KP_TYPE_SPIRE: extra[0] = KP_TYPE_GREATER_SPIRE; return 1;
	case KP_TYPE_CREEP_COLONY: extra[0] = KP_TYPE_SUNKEN_COLONY; extra[1] = KP_TYPE_SPORE_COLONY; return 2;
	}
	return 0;
}

/* A town unit's count of type t: 1 when it is that type (and completed when asked), else what it is making. */
static int64_t unit_counts(KpGame *g, int64_t n, int64_t t, int completed)
{
	if (completed && kp_band(g->u.status[n], STATUS_COMPLETED) == 0) return 0;
	if (g->u.type[n] == t) return 1;
	return completed ? 0 : kp_count_queued_type(g, n, t);
}

/* CountTownUnits 0x4872f0: town k's workers and buildings of type t (completed only, or with what they are making;
 * the buildings also count the morph extras). */
int64_t kp_ai_count_town_units(KpGame *g, int64_t k, int64_t t, int completed)
{
	const KpTowns *tw = &g->towns;
	int64_t r, sum = 0, extra[2];
	int e, extras = morph_extras(t, extra);
	for (r = tw->workers[k]; r != NONE; r = g->workers.next[r]) sum = kp_u32v(sum + unit_counts(g, g->workers.unit[r], t, completed));
	for (r = tw->buildings[k]; r != NONE; r = g->buildings.next[r]) {
		int64_t n = g->buildings.unit[r];
		sum = kp_u32v(sum + unit_counts(g, n, t, completed));
		for (e = 0; e < extras; e++) sum = kp_u32v(sum + unit_counts(g, n, extra[e], completed));
	}
	return sum;
}

/* CountPlayerTownUnits 0x487ba0. */
static int64_t count_player_town_units(KpGame *g, int64_t p, int64_t t)
{
	int64_t k, sum = 0;
	for (k = g->towns.heads[p]; k != NONE; k = g->towns.next[k]) sum = kp_u32v(sum + kp_ai_count_town_units(g, k, t, 0));
	return sum;
}

/* ClearResourceReferences 0x486000: every town forgets a dying resource; a town with an area refreshes its gas. */
void kp_ai_clear_resource_refs(KpGame *g, int64_t n)
{
	KpTowns *t = &g->towns;
	int64_t p, k;
	int e;
	if (kp_band(KP_DAT(g, units, special_ability_flags, g->u.type[n]), RESOURCE_CONTAINER) == 0) return;
	for (p = 0; p < KP_AI_PLAYERS; p++) {
		for (k = t->heads[p]; k != NONE; k = t->next[k]) {
			if (t->mineral[k] == n) t->mineral[k] = 0;
			for (e = 0; e < KP_TOWN_GAS; e++) {
				if (t->gas[k][e] == n) t->gas[k][e] = 0;
			}
			if (t->area[k] != 0) t->gas_refresh[k] = 1;
		}
	}
}

/* MigrateTownIfEmpty 0x485840: an empty town is gone; one left with Overlords and workers only hands them to its
 * owner's nearest other town (the first of equals), each through RemoveUnitAi 0x48dbc0. */
static int migrate_town(KpGame *g, int64_t k)
{
	KpTowns *t = &g->towns;
	int64_t r, best = NONE, best_d = FAR_AWAY, o;
	if (g->ai.migrating) return 0;
	if (t->buildings[k] == NONE && t->workers[k] == NONE) return 1;
	for (r = t->buildings[k]; r != NONE; r = g->buildings.next[r]) {
		if (g->u.type[g->buildings.unit[r]] != KP_TYPE_OVERLORD) return 0;
	}
	for (o = t->heads[t->owner[k]]; o != NONE; o = t->next[o]) {
		int64_t d;
		if (o == k) continue;
		d = kp_approx_distance(t->x[o] - t->x[k], t->y[o] - t->y[k]);
		if (kp_u32v(d) < kp_u32v(best_d)) {
			best_d = d;
			best = o;
		}
	}
	if (best == NONE) return 0;
	g->ai.migrating = 1;
	for (r = t->buildings[k]; r != NONE;) {
		int64_t next = g->buildings.next[r], n = g->buildings.unit[r];
		kp_remove_unit_ai(g, n, 0);
		kp_ai_town_assign(g, best, n);
		if (t->buildings[k] == NONE) break;
		r = next;
	}
	for (r = t->workers[k]; r != NONE;) {
		int64_t next = g->workers.next[r], n = g->workers.unit[r];
		kp_remove_unit_ai(g, n, 0);
		kp_ai_town_assign(g, best, n);
		if (t->workers[k] == NONE) break;
		r = next;
	}
	g->ai.migrating = 0;
	return 1;
}

/* DeleteTownIfEmpty 0x4856a0: an empty (or migrated) town's scripts stop, no resource stays claimed, and the town goes
 * back to the pool. */
static void delete_town_if_empty(KpGame *g, int64_t k)
{
	KpTowns *t = &g->towns;
	int64_t n, p = t->owner[k];
	if (!migrate_town(g, k)) return;
	kp_aiscript_kill_town(g, k); /* KillTownScripts 0x493860 */
	for (n = g->lists[KP_LIST_ACTIVE].head; n != 0; n = g->u.next[n]) {
		if (!kp_unit_inactive(g, n) && kp_band(KP_DAT(g, units, special_ability_flags, g->u.type[n]), RESOURCE_CONTAINER) != 0) {
			kp_union_set(g, n, R_AI_CLAIM, 1, 0);
		}
	}
	if (t->area[k] != 0) kp_ai_area_release(g, t->area[k]);
	kp_pool_release(t->next, t->prev, &t->heads[p], &t->free, k);
}

/* A released record's town (a record in a town's list always has one). */
static int64_t record_town(KpGame *g, int64_t k, const char *what)
{
	if (k < 0 || k >= KP_TOWNS) kp_game_fail(g, "ai: a %s record without its town", what);
	return k;
}

/* RemoveUnitFromTown 0x486af0 (its forced-building argument is never used): the unit's building or worker record
 * goes back to its pool; the town forgets it as its repair SCV and main building and goes when empty. 0 when nothing
 * was released. */
int kp_ai_remove_from_town(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t ai = u->ai[n], t = u->type[n], flags, k, r;
	kp_ai_clear_resource_refs(g, n);
	if (ai == 0) return 0;
	flags = KP_DAT(g, units, special_ability_flags, t);
	r = kp_ai_ref_index(ai);
	if ((kp_band(flags, DAT_BUILDING) != 0 && t != KP_TYPE_GEYSER) || t == KP_TYPE_LARVA || t == KP_TYPE_EGG
		|| t == KP_TYPE_OVERLORD) {
		if (kp_ai_ref_kind(ai) != KP_AI_BUILDING) {
			kp_game_fail(g, "ai: a record of type %lld released as a building's (0x483b10)", (long long)kp_ai_ref_kind(ai));
		}
		k = record_town(g, g->buildings.town[r], "building");
		kp_pool_release(g->buildings.next, g->buildings.prev, &g->towns.buildings[k], &g->buildings.free, r);
	} else if (kp_band(flags, DAT_WORKER) != 0) {
		if (kp_ai_ref_kind(ai) != KP_AI_WORKER) {
			kp_game_fail(g, "ai: a record of type %lld released as a worker's (0x483b70)", (long long)kp_ai_ref_kind(ai));
		}
		k = record_town(g, g->workers.town[r], "worker");
		kp_pool_release(g->workers.next, g->workers.prev, &g->towns.workers[k], &g->workers.free, r);
		g->towns.worker_count[k] = kp_mod(g->towns.worker_count[k] - 1, 256);
	} else {
		return 0;
	}
	if (g->towns.repair[k] == n) g->towns.repair[k] = 0;
	if (g->towns.main[k] == n) g->towns.main[k] = 0;
	delete_town_if_empty(g, k);
	u->ai[n] = 0;
	return 1;
}

/* TransferWorkersToTown 0x486480: up to count workers (no more than the town lacks) come from the owner's other towns,
 * the last first, that have more than one and a valid main building (and more than their limit when the town is not
 * empty); no worker that harvests or builds moves. */
void kp_ai_transfer_workers(KpGame *g, int64_t k, int64_t count)
{
	KpTowns *t = &g->towns;
	int64_t d;
	if (t->under_attack[k] != 0) return;
	if (t->worker_count[k] != 0) {
		if (t->worker_count[k] >= t->worker_limit[k]) return;
		if (t->worker_count[k] + count > t->worker_limit[k]) count = t->worker_limit[k] - t->worker_count[k];
	}
	if (count == 0) return;
	d = t->heads[t->owner[k]];
	if (d == NONE) return;
	while (t->next[d] != NONE) d = t->next[d];
	for (; d != NONE; d = t->prev[d]) {
		int64_t r;
		if (d == k || t->worker_count[d] <= 1 || t->main[d] == 0 || !kp_valid_depot(g, t->main[d])) continue;
		if (t->worker_count[k] != 0 && t->worker_count[d] <= t->worker_limit[d]) continue;
		for (r = t->workers[d]; r != NONE;) {
			int64_t next = g->workers.next[r], n = g->workers.unit[r], o = g->u.order[n];
			if (o != KP_ORD_HARVEST_GAS && o != KP_ORD_MINING_MINERALS && o != KP_ORD_HARVEST3 && o != KP_ORD_PLACE_BUILDING
				&& o != KP_ORD_CONSTRUCTING && o != KP_ORD_PLACE_PROTOSS_BUILDING && o != KP_ORD_DRONE_LAND
				&& o != KP_ORD_DRONE_BUILD && o != KP_ORD_DRONE_START_BUILD
				&& kp_band(g->u.status[n], STATUS_COMPLETED) != 0) {
				kp_ai_remove_from_town(g, n);
				kp_ai_town_assign(g, k, n);
				if (--count == 0) return;
			}
			if (t->workers[d] == NONE) break;
			r = next;
		}
	}
}

/* ---- the towns' requests to spending ---- */

#define FARM_SUPPLY_MARGIN 13
#define FARM_URGENT 8
#define FARM_PRIORITY_URGENT 140
#define FARM_PRIORITY 120
#define GAS_PRIORITY_MORE 80
#define GAS_PRIORITY_FIRST 90
#define GAS_WORKERS 15
#define GAS_MINERALS 500
#define TOWER_PRIORITY 80
#define MAIN_PRIORITY_POOR 150
#define MAIN_POOR_MINERALS 600
#define SUPPLY_DISABLED 0x400

static void set_requests_first(KpGame *g, int64_t p, int on)
{
	g->pai.flags[p] = on ? kp_bor(g->pai.flags[p], KP_AIF_REQUESTS_FIRST) : kp_band(g->pai.flags[p], kp_bnot(KP_AIF_REQUESTS_FIRST));
}

/* IsTechDoneOrResearching 0x4863b0. */
static int tech_done(KpGame *g, int64_t k, int64_t tech)
{
	int64_t p = g->towns.owner[k], n;
	if (kp_tech_researched(g, p, tech)) return 1;
	for (n = g->player_units[p]; n != 0; n = g->u.player_next[n]) {
		if (!kp_unit_inactive(g, n) && g->u.order[n] == KP_ORD_RESEARCH && kp_union_get(g, n, U_RESEARCH, 1) == tech) return 1;
	}
	return 0;
}

/* NeedsPylonForDisabledUnit 0x484fc0. */
static int needs_pylon(KpGame *g, int64_t p)
{
	int64_t n;
	if (kp_u32v(g->count_all[KP_TYPE_PYLON * KP_PLAYERS + p]) > kp_u32v(g->count_completed[KP_TYPE_PYLON * KP_PLAYERS + p])) return 0;
	for (n = g->player_units[p]; n != 0; n = g->u.player_next[n]) {
		if (!kp_unit_inactive(g, n) && kp_band(g->u.status[n], SUPPLY_DISABLED) != 0 && kp_ai_unit_region_unthreatened(g, n)) {
			return 1;
		}
	}
	return 0;
}

/* RequestTownFarms 0x485970: the race's supply provider when the free supply runs short (or a disabled unit wants a
 * Pylon), as many as the supply in use plus 13 needs beyond the main building's. */
static void request_farms(KpGame *g, int64_t k)
{
	int64_t p = g->towns.owner[k], race = g->players.race[p], farm, worker, avail, wanted;
	farm = race == RACE_TERRAN ? KP_TYPE_SUPPLY_DEPOT : race == RACE_ZERG ? KP_TYPE_OVERLORD : KP_TYPE_PYLON;
	worker = race == RACE_TERRAN ? KP_TYPE_SCV : race == RACE_ZERG ? KP_TYPE_DRONE : KP_TYPE_PROBE;
	avail = kp_s32v(kp_free_supply(g, worker, p));
	if (needs_pylon(g, p)) {
		wanted = count_player_town_units(g, p, farm);
	} else {
		int64_t used, main_supply;
		if (avail >= FARM_SUPPLY_MARGIN) return;
		used = kp_u32v(kp_supply_used(g, kp_type_race_index(g, farm), p) + FARM_SUPPLY_MARGIN);
		main_supply = kp_band(KP_DAT(g, units, supply_provided, kp_ai_race_main_building(g, p)), 0xff);
		if (used <= main_supply) return;
		wanted = kp_udiv64(g, used - main_supply, kp_band(KP_DAT(g, units, supply_provided, farm), 0xff));
	}
	wanted = kp_u32v(wanted + 1);
	if (avail != 0 && kp_band(g->pai.flags[p], KP_AIF_FARMS_NO_TIMING) != 0 && !needs_pylon(g, p)) return;
	if (wanted <= count_player_town_units(g, p, farm)) return;
	kp_ai_request_town_unit(g, p, k, farm, avail <= FARM_URGENT ? FARM_PRIORITY_URGENT : FARM_PRIORITY);
}

/* RequestTownGasBuilding 0x485ab0: a gas building over a free Geyser, the first one when the town has none (and
 * wants gas or has no mineral), another once its workers are many and minerals plenty. */
static void request_gas(KpGame *g, int64_t k)
{
	KpTowns *t = &g->towns;
	int64_t p = t->owner[k], mineral_percent, gas_percent, priority;
	kp_ai_gather_split(g, p, &mineral_percent, &gas_percent);
	if (gas_percent == 0 && t->mineral[k] != 0) return;
	if (town_gas(g, k, GAS_FULL, 1) != 0) {
		if (town_gas(g, k, GAS_GEYSER, 1) == 0) return;
		if (t->worker_count[k] < t->worker_limit[k] && t->worker_count[k] < GAS_WORKERS) return;
		if (kp_s32v(g->players.minerals[p]) < GAS_MINERALS) return;
		priority = GAS_PRIORITY_MORE;
	} else {
		if (town_gas(g, k, GAS_GEYSER, 1) == 0) return;
		priority = GAS_PRIORITY_FIRST;
	}
	kp_ai_request_town_unit(g, p, k, kp_ai_race_gas_building(g, p), priority);
}

/* The first region of player p with region flag `flag` (0 for none). */
static int64_t flagged_region(KpGame *g, int64_t p, int64_t flag)
{
	int64_t r;
	for (r = 1; r < (int64_t)g->regions.count; r++) {
		if (kp_band(g->air.flags[p][r], flag) != 0) return r;
	}
	return 0;
}

#define CREW_PRIORITY 100

/* ForEachCargoUnit 0x437da0 with 0x4aa4f0: a Firebat among the Bunker's passengers (the slots it provides). */
static int holds_firebat(KpGame *g, int64_t n)
{
	int64_t k, slots = KP_DAT(g, units, space_provided, g->u.type[n]);
	for (k = 0; k < slots; k++) {
		int64_t c = kp_cargo_unit(g, n, (int)k);
		if (c != 0 && g->u.type[c] == KP_TYPE_FIREBAT) return 1;
	}
	return 0;
}

/* RequestBunkersAndCrews 0x4ae220: a Bunker for a region that wants one, and crews for the completed Bunkers with
 * room: recruited, else requested (and a request at priority 100 ends the pass). */
static void request_bunkers(KpGame *g, int64_t k, int64_t p)
{
	int64_t n;
	if (flagged_region(g, p, KP_RFLAG_WANTS_BUNKER) != 0) kp_ai_convert_town_request(g, p, k, 0, KP_TYPE_BUNKER, TOWER_PRIORITY);
	for (n = g->player_units[p]; n != 0; n = g->u.player_next[n]) {
		int64_t crew, r, priority;
		if (kp_unit_inactive(g, n) || g->u.type[n] != KP_TYPE_BUNKER || kp_band(g->u.status[n], STATUS_COMPLETED) == 0) continue;
		if (kp_cargo_space(g, n) >= KP_DAT(g, units, space_provided, KP_TYPE_BUNKER)) continue; /* TransportHasFreeSpace 0x4382f0 */
		/* a Firebat while the Bunker holds none and the Firebats are fewer than the Bunkers, else a Marine */
		if (kp_ai_defense_use_has(g, p, KP_TYPE_FIREBAT) && !holds_firebat(g, n)
			&& g->count_completed[KP_TYPE_FIREBAT * KP_PLAYERS + p] < g->count_completed[KP_TYPE_BUNKER * KP_PLAYERS + p]) {
			crew = KP_TYPE_FIREBAT;
		} else if (kp_ai_defense_use_has(g, p, KP_TYPE_MARINE)) {
			crew = KP_TYPE_MARINE;
		} else {
			return;
		}
		r = kp_band(kp_unit_region(g, n), 0xffff);
		priority = kp_ai_campaign(g, p) && kp_cargo_space(g, n) != 0 ? KP_AI_MANNED_CREW_PRIORITY : CREW_PRIORITY;
		if (kp_ai_recruit(g, p, r, crew, priority)) continue; /* RecruitForRegion 0x4ae960 */
		kp_ai_request_unit(g, p, KP_AI_CONTEXT(KP_AI_CONTEXT_REGION, r), crew, priority, 1, 1); /* 0x482990 */
		if (priority == CREW_PRIORITY) return;
	}
}

/* RequestMissileTurrets 0x4b1420: a Missile Turret for a region that wants one. */
static void request_turrets(KpGame *g, int64_t k, int64_t p)
{
	if (flagged_region(g, p, KP_RFLAG_WANTS_TURRET) != 0) kp_ai_convert_town_request(g, p, k, 0, KP_TYPE_MISSILE_TURRET, TOWER_PRIORITY);
}

/* ProcessTownRequests 0x4865b0: each town's producers keep only their current job, an empty town pulls a worker, and
 * every persistent request short of its count becomes spending requests; then the gas buildings, the worker
 * transfers to short towns, and for the last town the supply, the Bunkers and the Missile Turrets. */
void kp_ai_process_town_requests(KpGame *g, int64_t p)
{
	KpTowns *t = &g->towns;
	int64_t k, last = NONE, decrement = 0;
	int excess = 0;
	set_requests_first(g, p, 1);
	for (k = t->heads[p]; k != NONE; k = t->next[k]) {
		int64_t r, e;
		for (r = t->buildings[k]; r != NONE; r = g->buildings.next[r]) {
			int64_t n = g->buildings.unit[r], slot;
			if (g->u.owner[n] != t->owner[k]) continue;
			for (slot = 1; slot < 5; slot++) kp_cancel_queued(g, n, slot);
		}
		if (t->workers[k] == NONE) {
			int64_t main = t->main[k];
			if (!(main != 0 && kp_band(g->u.status[main], STATUS_COMPLETED) == 0 && g->u.type[main] == KP_TYPE_HATCHERY)) {
				kp_ai_transfer_workers(g, k, 1);
			}
		} else if (t->worker_count[k] > t->worker_limit[k]) {
			excess = 1;
		}
		last = k;
		for (e = 0; e < KP_TOWN_REQUESTS; e++) {
			int64_t req = t->requests[k][e], kind, id, have, want;
			if (kp_band(req, 0xf8) == 0) break;
			kind = kp_band(kp_shrv(req, 1), 3);
			id = kp_band(kp_shrv(req, 16), 0xffff);
			if (kind == REQUEST_KIND_UNIT) {
				if (kp_band(req, 1) != 0) have = kp_u32v(g->count_all[id * KP_PLAYERS + p]);
				else have = kp_ai_count_town_units(g, k, id, 0);
				if (t->under_attack[k] != 0) continue;
			} else if (kind == REQUEST_KIND_UPGRADE) {
				int64_t n;
				have = kp_band(kp_upgrade_level(g, t->owner[k], id), 0xff);
				for (n = g->player_units[t->owner[k]]; n != 0; n = g->u.player_next[n]) {
					if (!kp_unit_inactive(g, n) && g->u.order[n] == KP_ORD_UPGRADE && kp_union_get(g, n, U_UPGRADE, 1) == id) have++;
				}
			} else {
				have = tech_done(g, k, id);
			}
			want = kp_band(req, 0xff) >> 3;
			if (id == kp_ai_race_worker(g, p) && want > t->worker_limit[k]) want = t->worker_limit[k];
			for (; have < want; have++) {
				int64_t priority = kp_band(kp_shrv(req, 8), 0xff);
				if ((kind == REQUEST_KIND_UPGRADE || kind == REQUEST_KIND_TECH) && kp_ai_campaign(g, p)) {
					if (decrement > priority) {
						priority = 0;
					} else {
						priority -= decrement;
						decrement++;
					}
					set_requests_first(g, p, 0);
				}
				if (!kp_ai_campaign(g, t->owner[k]) && kp_band(req, 6) == 0 && id == kp_ai_race_main_building(g, t->owner[k])
					&& kp_s32v(g->players.minerals[t->owner[k]]) < MAIN_POOR_MINERALS) {
					priority = MAIN_PRIORITY_POOR;
				}
				kp_ai_convert_town_request(g, t->owner[k], k, kind, id, priority);
			}
		}
		if (t->worker_count[k] > 1 || (t->worker_count[k] == 1 && t->main[k] != 0 && g->players.race[t->owner[k]] == RACE_ZERG)) {
			request_gas(g, k);
		}
	}
	if (excess) {
		for (k = t->heads[p]; k != NONE; k = t->next[k]) {
			int64_t main = t->main[k];
			if (main != 0 && kp_band(g->u.status[main], STATUS_COMPLETED) != 0 && t->worker_count[k] < t->worker_limit[k]) {
				kp_ai_transfer_workers(g, k, 1);
			}
		}
	}
	if (last == NONE) return;
	request_farms(g, last);
	request_bunkers(g, last, p);
	request_turrets(g, last, p);
}

#define STATUS_IN_TRANSPORT 0x40
#define WORKER_STALE 60 /* seconds without a job after which a worker is sent to its town again */
#define WORKER_REASSIGN_WAIT 10
#define GAS_PER_GEYSER 4
#define GAS_LOW 800
#define PERCENT 100
/* the completed workers from which a town short of gas sends at least 1..4 of its workers to gas */
static const int64_t GAS_WORKER_STEPS[] = { 5, 10, 15, 20 };

/* AllocateTownWorkers 0x485020: the town finds its area and resources, its worker limit, and gives each worker its
 * job; a worker that has had none for a while gets ComputerAI again. */
static void allocate_workers(KpGame *g, int64_t k)
{
	KpTowns *t = &g->towns;
	KpWorkerAis *wk = &g->workers;
	int64_t p = t->owner[k], gas = ASSIGN_GAS_FIRST, other = ASSIGN_MINERALS, mineral_percent, gas_percent;
	int64_t quota, cap, done, w, group = 0, e;
	if (t->initialized[k] == 0) return;
	if (t->area[k] == 0) {
		t->area[k] = kp_band(kp_ai_nearest_area(g, p, t->x[k], t->y[k]), 0xff);
		t->gas_refresh[k] = 1;
	}
	if (t->gas_refresh[k] != 0) {
		t->gas_refresh[k] = 0;
		kp_ai_area_gas_objects(g, t->area[k], t->gas[k]);
		for (e = 0; e < KP_TOWN_GAS; e++) {
			if (t->gas[k][e] != 0) kp_ai_building_region_state(g, p, t->gas[k][e]); /* 0x4b00a0 */
		}
	}
	if (t->mineral[k] == 0) {
		t->mineral[k] = kp_ai_area_mineral(g, t->area[k]);
		if (t->mineral[k] != 0) kp_ai_building_region_state(g, p, t->mineral[k]);
	}
	if (t->area[k] != 0) t->worker_limit[k] = kp_band(kp_ai_area_worker_limit(g, t->area[k], p, 1), 0xff);
	if (town_gas(g, k, GAS_FULL, 1) != 0 || town_gas(g, k, GAS_DEPLETED, 1) != 0) {
		if (t->mineral[k] == 0) other = ASSIGN_GAS_FIRST;
	} else {
		gas = ASSIGN_MINERALS;
	}
	kp_ai_gather_split(g, p, &mineral_percent, &gas_percent); /* GetGatherSplitPercent 0x483030 */
	quota = t->worker_count[k] * gas_percent / PERCENT;
	cap = GAS_PER_GEYSER * kp_ai_area_gas(g, t->area[k]);
	done = g->count_completed[kp_ai_race_worker(g, p) * KP_PLAYERS + p]; /* GetUnitCountCompleted 0x4143e0 */
	if (quota > cap) {
		quota = cap;
	} else if (kp_s32v(g->players.gas[p]) < GAS_LOW) {
		for (e = (int64_t)(sizeof GAS_WORKER_STEPS / sizeof GAS_WORKER_STEPS[0]) - 1; e >= 0; e--) {
			if (done < GAS_WORKER_STEPS[e]) continue;
			if (quota < e + 1) quota = e + 1;
			break;
		}
	}
	for (w = t->workers[k]; w != NONE; w = wk->next[w]) {
		int64_t n = wk->unit[w], o = g->u.order[n], now = kp_scalar(g, KP_ELAPSED), code;
		if (o == KP_ORD_CONSTRUCTING || o == KP_ORD_TRANSPORT_REQUEST || o == KP_ORD_DRONE_BUILD
			|| kp_band(g->u.status[n], STATUS_IN_TRANSPORT) != 0) {
			wk->time[w] = now;
		} else if (kp_u32v(now - wk->time[w]) >= WORKER_STALE) {
			kp_issue_order(g, n, KP_ORD_COMPUTER_AI);
			wk->wait[w] = WORKER_REASSIGN_WAIT;
			wk->reassign[w] = kp_band(wk->reassign[w] + 1, 0xff);
			wk->time[w] = now;
			kp_ai_transfer_workers(g, wk->town[w], 1);
		}
		if (quota != 0) {
			quota--;
			code = gas;
		} else {
			code = other;
		}
		wk->assignment[w] = code;
		if (code == ASSIGN_MINERALS || ++group < ASSIGN_GROUP) continue;
		group = 0;
		if (gas == code) gas++;
		if (other == code) other++;
		if (gas > ASSIGN_GAS_LAST) gas = ASSIGN_GAS_FIRST;
		if (other > ASSIGN_GAS_LAST) other = ASSIGN_GAS_FIRST;
	}
}

#define EXPAND_FEW_WORKERS 16
#define EXPAND_WORKERS 20
#define EXPAND_GAS_TOWNS 2
#define EXPAND_FIELDS 24
#define RICH_MINERALS 2000
#define RICH_MINERALS_MAX 5000
#define RICH_GAS 1000
#define RICH_GAS_MAX 3000
#define PANIC_MODE 2 /* ChooseExpansionArea's mode for the panic block */

/* StartPanicExpansion 0x494c60: the panic block runs at a new resource area near the town. */
static void panic_expansion(KpGame *g, int64_t p, int64_t k)
{
	int64_t area;
	if (g->pai.panic[p] == 0) return; /* GetPanicBlock 0x482c10 */
	if (kp_s32v(g->players.minerals[p]) >= RICH_MINERALS_MAX && kp_s32v(g->players.gas[p]) >= RICH_GAS_MAX
		&& !kp_ai_campaign(g, p)) {
		return;
	}
	if (g->towns.area[k] == 0) return; /* GetTownArea 0x487b70 */
	area = kp_ai_choose_expansion(g, p, g->towns.area[k], PANIC_MODE); /* 0x4970e0 */
	/* the pseudo-script's flags are 0: the panic block runs from the original binary */
	if (area != 0) kp_aiscript_spawn_area_town(g, p, 0, area, g->pai.panic[p]); /* 0x494e80 */
}

/* MaintainNextPlayerTowns 0x486c80 for player p: its towns allocate their workers; when they all stand and want few
 * workers, lack gas, or the player is rich, the panic block expands from its first town. */
void kp_ai_maintain_towns(KpGame *g, int64_t p)
{
	KpTowns *t = &g->towns;
	int64_t k, towns = 0, gas_towns = 0, wanted = 0, fields = 0, first = t->heads[p];
	int not_ready = 0;
	if (first == NONE) return;
	for (k = first; k != NONE; k = t->next[k]) {
		int64_t main = t->main[k];
		towns++;
		if (town_gas(g, k, GAS_FULL, 1) != 0 || town_gas(g, k, GAS_GEYSER, 1) != 0) gas_towns++;
		if (t->initialized[k] == 0 || main == 0 || kp_band(g->u.status[main], STATUS_COMPLETED) == 0) not_ready = 1;
		allocate_workers(g, k);
		if (t->area[k] != 0) wanted = kp_u32v(wanted + kp_ai_area_worker_limit(g, t->area[k], t->owner[k], 0));
		if (t->under_attack[k] != 0) t->under_attack[k]--;
	}
	if (not_ready) return;
	if (wanted != 0) {
		if (wanted < EXPAND_FEW_WORKERS) {
			panic_expansion(g, p, first);
			return;
		}
		if (towns > 1 && wanted < EXPAND_WORKERS) {
			panic_expansion(g, p, first);
			return;
		}
	}
	if (towns > 1 && gas_towns < EXPAND_GAS_TOWNS) {
		panic_expansion(g, p, first);
		return;
	}
	if (kp_s32v(g->players.minerals[p]) < RICH_MINERALS || kp_s32v(g->players.minerals[p]) >= RICH_MINERALS_MAX) return;
	if (kp_s32v(g->players.gas[p]) < RICH_GAS || kp_s32v(g->players.gas[p]) >= RICH_GAS_MAX) return;
	if (kp_band(g->pai.flags[p], KP_AIF_SUPPRESS_UNITS) == 0) return;
	for (k = first; k != NONE; k = t->next[k]) {
		if (t->area[k] != 0) fields += kp_ai_area_minerals(g, t->area[k]);
	}
	if (fields < EXPAND_FIELDS) panic_expansion(g, p, first);
}

#define STATUS_HALLUCINATION 0x40000000
/* the guards a computer's town places at a Bunker (after its first minute) and at its fourth Missile Turret */
#define BUNKER_GUARD_SECONDS 60
#define BUNKER_GUARD_TYPE 5
#define DEFENSE_GUARD_TYPE 1
#define TURRET_GUARDS_FROM 4

/* RegisterFreeMedic 0x482520: a computer player's first Medic. */
void kp_ai_register_medic(KpGame *g, int64_t n)
{
	int64_t p = g->u.owner[n];
	if (kp_controller(g, p) != KP_CTRL_COMPUTER || g->u.type[n] != KP_TYPE_MEDIC || p < 0 || p >= KP_AI_PLAYERS) return;
	if (g->pai.medic[p] == 0) g->pai.medic[p] = n;
}

/* 0x483220: the player's registered Medic that goes is replaced by the first Medic of the player's units. */
void kp_ai_release_medic(KpGame *g, int64_t n)
{
	int64_t p = g->u.owner[n], c;
	if (g->u.type[n] != KP_TYPE_MEDIC || p < 0 || p >= KP_AI_PLAYERS || g->pai.medic[p] != n) return;
	g->pai.medic[p] = 0;
	for (c = g->player_units[p]; c != 0; c = g->u.player_next[c]) {
		if (g->u.type[c] == KP_TYPE_MEDIC) {
			g->pai.medic[p] = c;
			return;
		}
	}
}

/* A unit that becomes its own product leaves its BuildingAI (ClearResourceReferences 0x486000, ReleaseBuildingAi
 * 0x483b10); its town forgets it as its repair SCV and main building. */
static void drop_own_building_ai(KpGame *g, int64_t n)
{
	int64_t ai, r, k;
	kp_ai_clear_resource_refs(g, n);
	ai = g->u.ai[n];
	if (ai == 0) return;
	if (kp_ai_ref_kind(ai) != KP_AI_BUILDING) {
		kp_game_fail(g, "ai: a record of type %lld released as a building's (0x483b10)", (long long)kp_ai_ref_kind(ai));
	}
	r = kp_ai_ref_index(ai);
	k = g->buildings.town[r];
	k = record_town(g, k, "building");
	kp_pool_release(g->buildings.next, g->buildings.prev, &g->towns.buildings[k], &g->buildings.free, r);
	if (g->towns.repair[k] == n) g->towns.repair[k] = 0;
	if (g->towns.main[k] == n) g->towns.main[k] = 0;
	g->u.ai[n] = 0;
}

/* The Missile Turrets of town k, standing or queued (CountQueuedType 0x41bde0) by its workers and buildings. */
static int64_t town_turrets(KpGame *g, int64_t k)
{
	int64_t r, sum = 0;
	for (r = g->towns.workers[k]; r != NONE; r = g->workers.next[r]) {
		int64_t n = g->workers.unit[r];
		sum += g->u.type[n] == KP_TYPE_MISSILE_TURRET ? 1 : kp_count_queued_type(g, n, KP_TYPE_MISSILE_TURRET);
	}
	for (r = g->towns.buildings[k]; r != NONE; r = g->buildings.next[r]) {
		int64_t n = g->buildings.unit[r];
		sum += g->u.type[n] == KP_TYPE_MISSILE_TURRET ? 1 : kp_count_queued_type(g, n, KP_TYPE_MISSILE_TURRET);
	}
	return sum;
}

void kp_ai_add_building(KpGame *g, int64_t builder, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t k, x, y;
	int32_t s;
	if (kp_controller(g, u->owner[builder]) != KP_CTRL_COMPUTER || u->ai[builder] == 0) return;
	k = town_of_record(g, builder);
	if (builder == n) drop_own_building_ai(g, n);
	kp_ai_town_assign(g, k, n); /* AssignUnitToTown 0x484cd0 */
	s = kp_unit_sprite(g, n);
	x = kp_s16v(g->w->spr.x[s]);
	y = kp_s16v(g->w->spr.y[s]);
	if (u->type[n] == KP_TYPE_BUNKER) {
		if (kp_u32v(kp_scalar(g, KP_ELAPSED)) <= BUNKER_GUARD_SECONDS) return;
		kp_guard_precreate(g, u->owner[n], BUNKER_GUARD_TYPE, x, y); /* PrecreateGuard 0x4b22a0 */
	} else if (u->type[n] != KP_TYPE_MISSILE_TURRET || town_turrets(g, k) < TURRET_GUARDS_FROM) {
		return;
	}
	kp_guard_precreate(g, u->owner[n], DEFENSE_GUARD_TYPE, x, y);
}

/* AddWorkerToProducerTown 0x486340: the new worker joins its producer's town (a Drone of itself first leaves its
 * BuildingAI). */
static void add_worker(KpGame *g, int64_t producer, int64_t n)
{
	int64_t k;
	if (g->u.ai[producer] == 0) return;
	k = town_of_record(g, producer);
	if (producer == n) drop_own_building_ai(g, n);
	kp_ai_town_assign(g, k, n);
}

/* GetUnitAiRegion 0x4b0960: the region of player p's AI where unit n stands. */
static int64_t unit_ai_region(KpGame *g, int64_t n) { return kp_band(kp_unit_region(g, n), 0xffff); }

/* AssignNewUnitAi 0x487990: a hallucination joins the military of its region; another new unit takes the training
 * tag of its producer's queue slot (the head, or slot 0 for a unit of the producer's own type or the producer
 * itself): a guard waiting for it, the military of a region (an occupied guard's unit goes to its own region), or a
 * guard record of its own. */
static void assign_new_unit(KpGame *g, int64_t producer, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t t = u->type[n], ai, r, slot, tag, context, p = u->owner[n];
	if (t == KP_TYPE_GUARDIAN || t == KP_TYPE_DEVOURER) return;
	if (kp_band(u->status[n], STATUS_HALLUCINATION) != 0) {
		kp_ai_military_add(g, p, unit_ai_region(g, n), n, 1);
		return;
	}
	if (t == KP_TYPE_ARCHON || t == KP_TYPE_DARK_ARCHON || t == KP_TYPE_LURKER || t == KP_TYPE_NUCLEAR_MISSILE || t == KP_TYPE_OBSERVER) return;
	ai = u->ai[producer];
	if (ai == 0) return;
	if (kp_ai_ref_kind(ai) != KP_AI_BUILDING) {
		/* TODO: the training tags of a producer whose record is not a BuildingAI (the bytes +9.. of its record) */
		kp_game_fail(g, "ai: a unit trained by a record of type %lld (0x487990)", (long long)kp_ai_ref_kind(ai));
	}
	r = kp_ai_ref_index(ai);
	if (producer == n) {
		slot = 0;
		drop_own_building_ai(g, n); /* the released record's tags are still read */
	} else {
		slot = u->type[producer] == t ? 0 : u->queue_slot[producer];
	}
	tag = g->buildings.tags[r][slot];
	context = g->buildings.contexts[r][slot];
	if (tag == KP_AI_CONTEXT_GUARD) {
		int64_t k = kp_ai_ref_index(context);
		if (g->guards.unit[k] == 0) {
			kp_guard_attach_unit(g, k, n); /* AttachGuardToUnit 0x4b25b0 */
			return;
		}
		tag = g->buildings.tags[r][slot] = KP_AI_CONTEXT_REGION;
		context = g->buildings.contexts[r][slot] = KP_AI_CONTEXT(KP_AI_CONTEXT_REGION, unit_ai_region(g, n));
	}
	if (tag == KP_AI_CONTEXT_REGION) {
		kp_ai_military_add(g, u->owner[producer], kp_ai_ref_index(context), n, 0); /* AddMilitaryAi 0x4ae760 */
		return;
	}
	kp_guard_attach(g, n); /* AttachGuardRecord 0x4b2010 */
}

/* AssignMilitaryAiAtUnitRegion 0x4b0980: a Broodling joins the military of its region of its Queen's player, an idle
 * region starting to move. */
static void assign_broodling(KpGame *g, int64_t queen, int64_t n)
{
	int64_t p = g->u.owner[queen], r = unit_ai_region(g, n);
	if (g->air.state[p][r] == KP_RSTATE_IDLE) kp_ai_set_region_state(g, p, r, KP_RSTATE_MOVE);
	kp_ai_military_add(g, p, r, n, 1);
}

void kp_ai_assign_completed(KpGame *g, int64_t producer, int64_t n)
{
	KpUnits *u = &g->u;
	if (kp_controller(g, u->owner[producer]) != KP_CTRL_COMPUTER) return;
	kp_ai_register_medic(g, n);
	if (kp_band(KP_DAT(g, units, special_ability_flags, u->type[n]), DAT_WORKER) != 0) add_worker(g, producer, n);
	else if (u->type[n] == KP_TYPE_OVERLORD) kp_ai_add_building(g, producer, n);
	else if (u->type[n] == KP_TYPE_BROODLING) assign_broodling(g, producer, n);
	else assign_new_unit(g, producer, n);
}

/* MarkUnitTownAttacked 0x487b80: the town of unit n's record counts as under attack. */
void kp_ai_town_attacked(KpGame *g, int64_t n)
{
	if (g->u.ai[n] == 0) return;
	g->towns.under_attack[town_of_record(g, n)] = 1;
}

/* GetTownGuardPosition 0x487ac0: selector 0 the town's centre, 1 its mineral field, else its first full (or else
 * depleted) gas building; 0 when there is none. */
int kp_ai_town_guard_position(KpGame *g, int64_t k, int64_t selector, int64_t *x, int64_t *y)
{
	int64_t n;
	int32_t s;
	if (selector == 0) {
		*x = kp_s16v(g->towns.x[k]);
		*y = kp_s16v(g->towns.y[k]);
		return 1;
	}
	n = selector == 1 ? g->towns.mineral[k] : town_gas(g, k, GAS_FULL, 1);
	if (n == 0 && selector != 1) n = town_gas(g, k, GAS_DEPLETED, 1);
	if (n == 0) return 0;
	s = kp_unit_sprite(g, n);
	*x = kp_s16v(g->w->spr.x[s]);
	*y = kp_s16v(g->w->spr.y[s]);
	return 1;
}

/* 0x486ba0 (the Drone becomes a building) and 0x487900 (the building, cancelled, a Drone again): a computer's unit
 * leaves its town's worker (or building) record for the other kind in the same town. */
void kp_ai_drone_town_change(KpGame *g, int64_t n, int to_building)
{
	int64_t ai = g->u.ai[n], r, k;
	if (kp_controller(g, g->u.owner[n]) != KP_CTRL_COMPUTER || ai == 0) return;
	if (kp_ai_ref_kind(ai) != (to_building ? KP_AI_WORKER : KP_AI_BUILDING)) {
		kp_game_fail(g, "ai: a Drone's town change from a record of type %lld", (long long)kp_ai_ref_kind(ai));
	}
	r = kp_ai_ref_index(ai);
	g->u.ai[n] = 0;
	if (to_building) {
		k = record_town(g, g->workers.town[r], "worker");
		kp_pool_release(g->workers.next, g->workers.prev, &g->towns.workers[k], &g->workers.free, r); /* 0x483b70 */
		g->towns.worker_count[k] = kp_mod(g->towns.worker_count[k] - 1, 256);
	} else {
		k = record_town(g, g->buildings.town[r], "building");
		kp_pool_release(g->buildings.next, g->buildings.prev, &g->towns.buildings[k], &g->buildings.free, r); /* 0x483b10 */
	}
	kp_ai_town_assign(g, k, n); /* AssignUnitToTown 0x484cd0 */
}

/* ClearTownRepairScv 0x484f10: an SCV boarding a transport is no longer its town's repair SCV (a guard's or military
 * record has no town: the original reads unrelated memory there, which never names the unit). */
void kp_ai_clear_repair_scv(KpGame *g, int64_t n)
{
	int64_t kind = kp_ai_ref_kind(g->u.ai[n]), k;
	if (g->u.type[n] != KP_TYPE_SCV || g->u.ai[n] == 0 || (kind != KP_AI_WORKER && kind != KP_AI_BUILDING)) return;
	k = town_of_record(g, n);
	if (g->towns.repair[k] == n) g->towns.repair[k] = 0;
}
