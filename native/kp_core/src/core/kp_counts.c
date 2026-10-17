/* Unit counters, supply and score statistics (kp_game.h). */
#include "kp_game_internal.h"

#define NEUTRAL 11
#define STATUS_HALLUCINATION 0x40000000
#define SUBUNIT 0x10
#define DOUBLE_SUPPLY 0x400
#define SUPPLY_MAX 400

/* Per-player arrays by native address (0x5290c4 + 0x30 * k). */
#define PSTAT(addr) (((addr) - 0x5290c4) / 0x30)
#define MEN_LIVE PSTAT(0x5290c4)
#define MEN_COMPLETED PSTAT(0x5290f4)
#define MEN_PRODUCED PSTAT(0x529124)
#define UNIT_SCORE PSTAT(0x5291b4)
#define BUILDINGS_LIVE PSTAT(0x529214)
#define BUILDINGS_COMPLETED PSTAT(0x529244)
#define BUILDINGS_PRODUCED PSTAT(0x529274)
#define BUILDING_SCORE PSTAT(0x529304)
#define FACTORIES_LIVE PSTAT(0x529364)
#define FACTORIES_COMPLETED PSTAT(0x529394)

static const int SUPPLY_PROVIDED[3] = { PSTAT(0x529424), PSTAT(0x5294b4), PSTAT(0x529544) }; /* group 1, 2, 4 */
static const int SUPPLY_USED[3] = { PSTAT(0x529454), PSTAT(0x5294e4), PSTAT(0x529574) };
static const int SUPPLY_MAX_ARRAYS[3] = { PSTAT(0x529484), PSTAT(0x529514), PSTAT(0x5295a4) };

static int egg(int64_t t) { return t == 0x24 || t == 0x3b || t == 0x61; }
static int not_produced_men(int64_t t) { return t == 0x2c || t == 0x3e || t == 0x3f || t == 0x44 || t == 0x67; }
static int not_produced_buildings(int64_t t) { return t == 0x84 || t == 0x85 || t == 0x89 || t == 0x90 || t == 0x92; }

void kp_counts_init(KpGame *g)
{
	int i, p;
	for (i = 0; i < KP_UNIT_TYPES * KP_PLAYERS; i++) {
		g->count_all[i] = g->count_completed[i] = g->count_kills[i] = g->count_deaths[i] = 0;
	}
	for (i = 0; i < KP_PSTAT_ARRAYS; i++) {
		for (p = 0; p < KP_PLAYERS; p++) g->pstat[i][p] = 0;
	}
	for (i = 0; i < 3; i++) {
		for (p = 0; p < KP_PLAYERS; p++) g->pstat[SUPPLY_MAX_ARRAYS[i]][p] = SUPPLY_MAX;
	}
}

static void add(KpGame *g, int a, int64_t p, int64_t v)
{
	if (p < 0 || p >= KP_PLAYERS) kp_game_fail(g, "counts: player %lld", (long long)p);
	g->pstat[a][p] = kp_s32v(g->pstat[a][p] + v);
}

/* The queue slot field queue0..queue4 of unit n. */
static int64_t queued(KpGame *g, int64_t n)
{
	return *kp_queue_entry(g, n, g->u.queue_slot[n]);
}

static int64_t type_player(KpGame *g, int64_t t, int64_t p)
{
	int64_t i = t * KP_PLAYERS + p;
	if (i < 0 || i >= KP_UNIT_TYPES * KP_PLAYERS) kp_game_fail(g, "counts: type %lld player %lld", (long long)t, (long long)p);
	return i;
}

void kp_account_live(KpGame *g, int64_t n, int64_t delta)
{
	KpUnits *u = &g->u;
	int64_t t, p, i, group, supply;
	if (kp_band(u->status[n], STATUS_HALLUCINATION) != 0) return;
	t = u->type[n];
	if (kp_band(KP_DAT(g, units, special_ability_flags, t), SUBUNIT) != 0) return;
	p = u->owner[n];
	i = type_player(g, t, p);
	g->count_all[i] = kp_s32v(g->count_all[i] + delta);
	group = KP_DAT(g, units, staredit_group_flags, t);
	if (kp_mod(group, 2) == 1) {
		if (egg(t)) {
			int64_t q = queued(g, n);
			supply = KP_DAT(g, units, supply_required, q);
			if (kp_band(KP_DAT(g, units, special_ability_flags, q), DOUBLE_SUPPLY) != 0) supply = kp_mod(supply * 2, 256);
		} else {
			supply = KP_DAT(g, units, supply_required, t);
			if (kp_band(KP_DAT(g, units, special_ability_flags, t), DOUBLE_SUPPLY) != 0 && kp_band(u->status[n], 1) == 0) {
				supply = kp_mod(supply * 2, 256);
			}
		}
		add(g, SUPPLY_USED[0], p, supply * delta);
	} else if (kp_band(group, 2) != 0) {
		add(g, SUPPLY_USED[1], p, KP_DAT(g, units, supply_required, t) * delta);
	} else if (kp_band(group, 4) != 0) {
		add(g, SUPPLY_USED[2], p, KP_DAT(g, units, supply_required, t) * delta);
	}
	if (kp_band(group, 0x20) != 0) add(g, FACTORIES_LIVE, p, delta);
	if (kp_band(group, 8) != 0) add(g, MEN_LIVE, p, delta);
	else if (kp_band(group, 0x10) != 0) add(g, BUILDINGS_LIVE, p, delta);
	else if (egg(t)) add(g, MEN_LIVE, p, delta);
	if (g->count_all[i] < 0) g->count_all[i] = 0;
}

void kp_account_completed(KpGame *g, int64_t n, int64_t delta, int score)
{
	KpUnits *u = &g->u;
	int64_t t, p, i, group;
	if (kp_band(u->status[n], STATUS_HALLUCINATION) != 0) return;
	t = u->type[n];
	if (kp_band(KP_DAT(g, units, special_ability_flags, t), SUBUNIT) != 0) return;
	p = u->owner[n];
	i = type_player(g, t, p);
	g->count_completed[i] = kp_s32v(g->count_completed[i] + delta);
	group = KP_DAT(g, units, staredit_group_flags, t);
	if (kp_mod(group, 2) == 1) add(g, SUPPLY_PROVIDED[0], p, KP_DAT(g, units, supply_provided, t) * delta);
	else if (kp_band(group, 2) != 0) add(g, SUPPLY_PROVIDED[1], p, KP_DAT(g, units, supply_provided, t) * delta);
	else if (kp_band(group, 4) != 0) add(g, SUPPLY_PROVIDED[2], p, KP_DAT(g, units, supply_provided, t) * delta);
	if (kp_band(group, 0x20) != 0) add(g, FACTORIES_COMPLETED, p, delta);
	if (kp_band(group, 8) != 0) add(g, MEN_COMPLETED, p, delta);
	else if (kp_band(group, 0x10) != 0) add(g, BUILDINGS_COMPLETED, p, delta);
	if (score && p != NEUTRAL) {
		if (kp_band(group, 8) != 0) {
			if (!not_produced_men(t)) add(g, MEN_PRODUCED, p, delta);
			add(g, UNIT_SCORE, p, KP_DAT(g, units, build_score, t) * delta);
		} else if (kp_band(group, 0x10) != 0) {
			if (!not_produced_buildings(t)) add(g, BUILDINGS_PRODUCED, p, delta);
			add(g, BUILDING_SCORE, p, KP_DAT(g, units, build_score, t) * delta);
		}
	}
	if (g->count_completed[i] < 0) g->count_completed[i] = 0;
}

/* The supply arrays of type t's race by its group byte: bit 1 (Zerg), then 2 (Terran), then 4 (Protoss); -1 for none. */
static int supply_race(KpGame *g, int64_t t)
{
	int64_t group = KP_DAT(g, units, staredit_group_flags, t);
	if (kp_band(group, 1) != 0) return 0;
	if (kp_band(group, 2) != 0) return 1;
	if (kp_band(group, 4) != 0) return 2;
	return -1;
}

static int64_t pstat_u32(KpGame *g, int a, int64_t p)
{
	if (p < 0 || p >= KP_PLAYERS) kp_game_fail(g, "counts: player %lld", (long long)p);
	return kp_u32v(g->pstat[a][p]);
}

/* GetSupplyMax 0x4142e0: the supply cap of type t's race. */
int64_t kp_supply_max(KpGame *g, int64_t t, int64_t p)
{
	int k = supply_race(g, t);
	return k < 0 ? 0 : pstat_u32(g, SUPPLY_MAX_ARRAYS[k], p);
}

/* GetSupplyUsedByRace 0x414320: race 0 Zerg, 1 Terran, 2 Protoss. */
int64_t kp_supply_used(KpGame *g, int64_t race, int64_t p)
{
	return race >= 0 && race <= 2 ? pstat_u32(g, SUPPLY_USED[race], p) : 0;
}

/* GetFreeSupply 0x414180: min(provided, cap) - used (an unsigned minimum, a wrapping difference), cap - used under the
 * supply cheat. */
int64_t kp_supply_free(KpGame *g, int64_t t, int64_t p, int cheat)
{
	int k = supply_race(g, t);
	int64_t provided, cap, used;
	if (k < 0) return 0;
	provided = pstat_u32(g, SUPPLY_PROVIDED[k], p);
	cap = pstat_u32(g, SUPPLY_MAX_ARRAYS[k], p);
	used = pstat_u32(g, SUPPLY_USED[k], p);
	if (cheat) return kp_s32v(cap - used);
	return kp_s32v((provided < cap ? provided : cap) - used);
}

/* A Zerg building cancelled back into its Drone takes back the score the Drone's production gave (0x43b380): the
 * unit score loses the type's build score and the men produced one. */
void kp_unscore_produced(KpGame *g, int64_t p, int64_t t)
{
	add(g, UNIT_SCORE, p, -kp_band(KP_DAT(g, units, build_score, t), 0xffff));
	add(g, MEN_PRODUCED, p, -1);
}
