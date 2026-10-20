/* The computer players' regional AI and their units' AI orders (kp_game.h), ported from the atlas (P28, P31, P37,
 * P38) and docs/research/native-combat-ai.md, native-region-states.md. */
#include "kp_game_internal.h"
#include "kp_pathing_internal.h"

#include <string.h>

#define CAMPAIGN 0x20
#define DEFAULT_MIN 100
#define AI_STATE_START 1
#define REGION_RECENT 0x04
#define STATUS_GROUNDED 0x2
#define DAT_RESOURCE_DEPOT 0x1000
#define DAT_BUILDING 0x1
#define STATUS_LOCKS 0x3000
#define STATUS_AIR 0x4
#define STATUS_IN_BUILDING 0x20

/* GetRegionCenter 0x481a30. */
void kp_ai_region_center(KpGame *g, int64_t r, int64_t *x, int64_t *y)
{
	*x = kp_sarv(g->pathing.rg.center_x[r], 8);
	*y = kp_sarv(g->pathing.rg.center_y[r], 8);
}

/* GetUnitRegion 0x481ba0. */
int64_t kp_unit_region(KpGame *g, int64_t n)
{
	int32_t s = kp_unit_sprite(g, n);
	return kp_region_at(g, kp_s16v(g->w->spr.x[s]), kp_s16v(g->w->spr.y[s]));
}

/* IsCampaignAi 0x4831e0. */
int kp_ai_campaign(KpGame *g, int64_t p)
{
	if (kp_u32v(p) >= KP_AI_PLAYERS) kp_game_fail(g, "ai: the PlayerAi record of player %lld", (long long)p);
	return kp_band(g->pai.flags[p], CAMPAIGN) != 0;
}

/* InitPlayerAiData 0x4830d0, InitAiRegions 0x4b0640 (mode 0) and SetupVictoryConditions 0x448530's SetCampaignAi
 * 0x482ee0 for every player of a campaign game. */
void kp_ai_regions_init(KpGame *g, int campaign)
{
	int p;
	/* The original's AI keeps 2500 region records per player (0x4aa310) and its reach cache 2500 marks (0x6b1aa0);
	 * FinishAiMapSetup 0x48dd80 copies `count` records out of them, so a larger map overruns them there. */
	if (g->regions.count > KP_AI_REGIONS) {
		kp_game_fail(g, "ai: a map of %u regions (the AI holds %d)", (unsigned)g->regions.count, KP_AI_REGIONS);
	}
	memset(&g->air, 0, sizeof g->air);
	/* InitAiRegions zeroes the marks 0x6b1aa0 and their times 0x6b68c0 but not their sources 0x6b68e0, which keep
	 * the game before's (KpCarry) */
	memset(g->ai_reach, 0, sizeof g->ai_reach);
	memset(g->ai_reach_time, 0, sizeof g->ai_reach_time);
	memset(&g->pai, 0, sizeof g->pai);
	kp_ai_military_init(g); /* InitMilitaryPool 0x4a9a90 */
	for (p = 0; p < KP_AI_PLAYERS; p++) {
		g->pai.ai_state[p] = AI_STATE_START;
		g->pai.default_min[p] = DEFAULT_MIN;
		if (campaign) g->pai.flags[p] = kp_bor(g->pai.flags[p], CAMPAIGN);
	}
}

/* DecayRequirement 0x4ac080: campaign AI lowers a need by 13, 6 or 2 with its size, other AI by 300. */
static int64_t decay(int64_t v, int noncampaign)
{
	if (v <= 2) return 0;
	if (noncampaign) return v > 300 ? v - 300 : 0;
	if (v > 1000) return v - 13;
	if (v > 200) return v - 6;
	return v - 2;
}

static void decay_needs(KpGame *g, int64_t p, int64_t r)
{
	KpAiRegions *a = &g->air;
	int noncampaign = !kp_ai_campaign(g, p);
	if (kp_band(a->flags[p][r], KP_RFLAG_NO_DECAY) != 0) return;
	a->need_air[p][r] = decay(a->need_air[p][r], noncampaign);
	a->need_ground[p][r] = decay(a->need_ground[p][r], noncampaign);
}

/* UpdateRegionStrength 0x4adbd0. */
void kp_ai_update_region_strength(KpGame *g, int64_t p, int64_t r)
{
	KpAiRegions *a = &g->air;
	int64_t v;
	int64_t k;
	a->local_ground[p][r] = a->local_air[p][r] = a->all_ground[p][r] = a->all_air[p][r] = 0;
	for (k = kp_ai_military_first(g, p, r); k != -1; k = g->military.next[k]) {
		int64_t n = g->military.unit[k], air, ground;
		if (kp_band(g->u.status[n], STATUS_IN_BUILDING) != 0) continue;
		air = kp_band(g->u.air_strength[n], 0xffff);
		ground = kp_band(g->u.ground_strength[n], 0xffff);
		a->all_ground[p][r] = kp_mod(a->all_ground[p][r] + ground, 65536);
		a->all_air[p][r] = kp_mod(a->all_air[p][r] + air, 65536);
		if (kp_unit_region(g, n) == r) {
			a->local_air[p][r] = kp_mod(a->local_air[p][r] + air, 65536);
			a->local_ground[p][r] = kp_mod(a->local_ground[p][r] + ground, 65536);
		}
	}
	if (kp_ai_campaign(g, p)) {
		int64_t *found, cx, cy;
		int count, i;
		kp_ai_region_center(g, r, &cx, &cy);
		found = kp_query_rect(g, kp_s16v(cx - 256), kp_s16v(cy - 256), kp_s16v(cx + 256), kp_s16v(cy + 256), &count);
		for (i = 0; i < count; i++) {
			/* AddOwnUnitStrengthCallback 0x4aa2c0 */
			int64_t n = found[i];
			if (kp_band(g->u.status[n], STATUS_IN_BUILDING) != 0 || g->u.owner[n] != p || kp_unit_region(g, n) != r) continue;
			a->all_air[p][r] = kp_mod(a->all_air[p][r] + g->u.air_strength[n], 65536);
			a->local_air[p][r] = kp_mod(a->local_air[p][r] + g->u.air_strength[n], 65536);
			a->all_ground[p][r] = kp_mod(a->all_ground[p][r] + g->u.ground_strength[n], 65536);
			a->local_ground[p][r] = kp_mod(a->local_ground[p][r] + g->u.ground_strength[n], 65536);
		}
		kp_query_release(g, found);
	}
	v = a->enemy_air[p][r];
	if (!kp_ai_campaign(g, p)) v += kp_shrv(v, 1);
	if (v > a->need_air[p][r]) a->need_air[p][r] = kp_mod(v, 65536);
	v = a->enemy_ground[p][r];
	if (!kp_ai_campaign(g, p)) v += kp_shrv(v, 1);
	if (v > a->need_ground[p][r]) a->need_ground[p][r] = kp_mod(v, 65536);
	if (g->pai.max_force[p] != 0) {
		if (a->need_air[p][r] > g->pai.max_force[p]) a->need_air[p][r] = g->pai.max_force[p];
		if (a->need_ground[p][r] > g->pai.max_force[p]) a->need_ground[p][r] = g->pai.max_force[p];
	}
}

/* StepRegion 0x4b0ce0. */
static int region_has_own_base(KpGame *g, int64_t p, int64_t r);
static int64_t choose_regional_target(KpGame *g, int64_t p, int64_t n, int64_t region, const int64_t *c, int count,
	int mode);

/* ChooseRegionAirTarget 0x4a9800 (mode 0) and ChooseRegionGroundTarget 0x4a9820 (mode 1): the best enemy among the
 * region's units. */
static int64_t region_target(KpGame *g, int64_t p, int64_t r, int mode)
{
	int count;
	int64_t *units = kp_region_units(g, r, &count), t = choose_regional_target(g, p, 0, r, units, count, mode);
	kp_query_release(g, units);
	return t;
}

/* UpdateRegionTargets 0x4adcf0: the region's air and ground threats; its military members turn on them. */
static void update_targets(KpGame *g, int64_t p, int64_t r)
{
	KpAiRegions *a = &g->air;
	KpUnits *u = &g->u;
	int64_t k;
	a->target_air[p][r] = region_target(g, p, r, 0);
	a->target_ground[p][r] = region_target(g, p, r, 1);
	if (a->target_air[p][r] == 0 && a->target_ground[p][r] == 0) return;
	for (k = kp_ai_military_first(g, p, r); k >= 0; k = g->military.next[k]) {
		int64_t n = g->military.unit[k], o = u->order[n], ta = a->target_air[p][r], tg = a->target_ground[p][r];
		if (!kp_ai_mobile_military(g, n) || kp_band(u->status[n], STATUS_LOCKS) != 0) continue;
		if (u->order_unit[n] == tg || u->order_unit[n] == ta) continue;
		if (o == KP_ORD_ATTACK_MOVE || o == KP_ORD_AI_ATTACK_MOVE || o == KP_ORD_HARASS_MOVE || kp_is_transport(g, n)) continue;
		if ((tg != 0 && kp_can_attack(g, n, tg, 1)) || (ta != 0 && kp_can_attack(g, n, ta, 1))) {
			kp_ai_request_order(g, n, KP_ORD_COMPUTER_AI); /* RequestComputerOrder 0x4ada90 */
		}
	}
}

int kp_ai_region_unthreatened(KpGame *g, int64_t p, int64_t r)
{
	const KpAiRegions *a = &g->air;
	return a->state[p][r] != KP_RSTATE_ABANDONED && a->target_air[p][r] == 0 && a->target_ground[p][r] == 0
		&& kp_band(a->flags[p][r], KP_RFLAG_UNDER_ATTACK) == 0;
}

int kp_ai_unit_region_unthreatened(KpGame *g, int64_t n)
{
	if (n == 0 || kp_unit_inactive(g, n)) return 0;
	return kp_ai_region_unthreatened(g, g->u.owner[n], kp_unit_region(g, n));
}

/* HasOwnBaseNear 0x4ab810: the region or one of its ground neighbours holds a building or worker of p. */
static int own_base_near(KpGame *g, int64_t p, int64_t r)
{
	const KpRegionGraph *rg = &g->pathing.rg;
	int64_t k;
	if (region_has_own_base(g, p, r)) return 1;
	for (k = 0; k < rg->ground[r]; k++) {
		if (region_has_own_base(g, p, rg->neighbors[rg->nb_at[r] + k])) return 1;
	}
	return 0;
}

/* DisperseWeakRegion 0x4aa9b0: a region weaker than it needs gives up: state 3 (0 for a campaign AI with a base near
 * or an attack region) and its military members go back to the regions they stand in. */
static int disperse(KpGame *g, int64_t p, int64_t r)
{
	KpAiRegions *a = &g->air;
	int64_t state = KP_RSTATE_ABANDONED, s = a->state[p][r];
	int64_t head = kp_ai_military_first(g, p, r);
	if (a->local_air[p][r] >= a->need_air[p][r] && a->local_ground[p][r] >= a->need_ground[p][r]) return 0;
	if (!kp_ai_campaign(g, p) && head >= 0
		&& (g->u.type[g->military.unit[head]] == KP_TYPE_FIREBAT || g->u.type[g->military.unit[head]] == KP_TYPE_ZERGLING)) {
		return 0;
	}
	if ((kp_ai_campaign(g, p) && own_base_near(g, p, r)) || s == KP_RSTATE_GATHER || s == KP_RSTATE_ATTACK || s == KP_RSTATE_MOVE || s == KP_RSTATE_GROUP) state = KP_RSTATE_IDLE;
	kp_ai_set_region_state(g, p, r, state);
	kp_ai_disperse_military(g, p, r); /* the members go to the regions they stand in (0x4a9a30, 0x4ae760) */
	return 1;
}

/* RegionStateIdleWatch 0x4ac740 (states 4 and 5): a threat other than a critter makes it state 6. */
static void state_watch(KpGame *g, int64_t p, int64_t r)
{
	KpAiRegions *a = &g->air;
	kp_ai_update_region_strength(g, p, r);
	update_targets(g, p, r);
	if ((a->target_air[p][r] != 0 && !kp_is_critter(g, a->target_air[p][r]))
		|| (a->target_ground[p][r] != 0 && !kp_is_critter(g, a->target_ground[p][r]))) {
		kp_ai_set_region_state(g, p, r, KP_RSTATE_DEFEND);
	}
	decay_needs(g, p, r);
}

/* RegionStateDefend 0x4ac690 (state 6): once its retry ends, a non-campaign AI needs 1000 there; without threats it
 * goes back to state 5 (0 for a campaign AI without a building or worker there), with threats and no base there the
 * region disperses. */
static void state_defend(KpGame *g, int64_t p, int64_t r)
{
	KpAiRegions *a = &g->air;
	kp_ai_update_region_strength(g, p, r);
	update_targets(g, p, r);
	if (a->retry[p][r] != 0) return;
	if (!kp_ai_campaign(g, p)) {
		if (g->pathing.rg.klass[r] == KP_REGION_UNWALKABLE) {
			if (a->need_ground[p][r] < 1000) a->need_ground[p][r] = 1000;
		} else if (a->need_air[p][r] < 1000) {
			a->need_air[p][r] = 1000;
		}
	}
	if (a->target_air[p][r] == 0 && a->target_ground[p][r] == 0) {
		kp_ai_set_region_state(g, p, r, kp_ai_campaign(g, p) && !region_has_own_base(g, p, r) ? KP_RSTATE_IDLE : KP_RSTATE_BASE);
		return;
	}
	if (!region_has_own_base(g, p, r)) disperse(g, p, r);
}


/* RegionStateAttackGather 0x4ac870 (states 8 and 9): a gathering that cannot go on goes for the nearest target it can
 * hit (that region attacks, state 9), else disperses; one whose roster is complete and whose units stand (or whose
 * time ran out) sets out for its destination (state 1). The region goes back to state 0. */
static void state_gather(KpGame *g, int64_t p, int64_t r)
{
	KpAiRegions *a = &g->air;
	int has_ground, no_ground, no_air;
	int64_t dest;
	kp_ai_update_region_strength(g, p, r);
	update_targets(g, p, r);
	if (kp_band(a->flags[p][r], KP_RFLAG_UNDER_ATTACK) != 0 && disperse(g, p, r)) return;
	kp_ai_weapon_summary(g, p, r, &has_ground, &no_ground, &no_air);
	if ((has_ground && g->pathing.rg.klass[r] == KP_REGION_UNWALKABLE) || kp_ai_should_retarget(g, p, r, r)) {
		int64_t t;
		kp_ai_set_region_state(g, p, r, KP_RSTATE_IDLE);
		t = kp_ai_global_target(g, p, r);
		if (t == 0) {
			kp_ai_disperse_military(g, p, r);
			return;
		}
		t = kp_band(kp_unit_region(g, t), 0xffff);
		kp_ai_set_region_state(g, p, t, KP_RSTATE_ATTACK);
		kp_ai_transfer_military(g, p, r, t, 0, 0, 1);
		return;
	}
	if (!kp_ai_grouping_timeout(g, p) && (kp_ai_roster_missing(g, p, r) || kp_ai_straggling_attacker(g, p))) return;
	dest = a->dest[p][r];
	if (dest != 0) {
		kp_ai_set_region_state(g, p, dest, KP_RSTATE_MOVE);
		kp_ai_transfer_military(g, p, r, dest, 0, 0, 1);
	}
	kp_ai_set_region_state(g, p, r, KP_RSTATE_IDLE);
}

#define MOVE_GROUP_GROUND 20 /* the ground members from which a moving region groups */

/* RegionStateMoving 0x4ac7c0 (states 1 and 2): a region without military goes idle; a large moving one groups; with
 * targets it can hit it stays; else its military goes for the nearest target it can hit (that region takes this
 * state) and this region goes idle. */
static void state_moving(KpGame *g, int64_t p, int64_t r)
{
	KpAiRegions *a = &g->air;
	int64_t t;
	update_targets(g, p, r);
	if (a->military[p][r] == 0) {
		kp_ai_set_region_state(g, p, r, KP_RSTATE_IDLE);
		return;
	}
	if (a->state[p][r] != KP_RSTATE_GROUP && a->ground_count[p][r] >= MOVE_GROUP_GROUND) {
		kp_ai_set_region_state(g, p, r, KP_RSTATE_GROUP);
		return;
	}
	if (a->slowest[p][r] == 0) kp_ai_update_slowest(g, p, r);
	if ((a->target_air[p][r] != 0 || a->target_ground[p][r] != 0) && !kp_ai_should_retarget(g, p, r, r)) return;
	t = kp_ai_global_target(g, p, r);
	if (t != 0) {
		t = kp_band(kp_unit_region(g, t), 0xffff);
		kp_ai_set_region_state(g, p, t, a->state[p][r]);
		kp_ai_transfer_military(g, p, r, t, 0, 0, 1);
	}
	kp_ai_set_region_state(g, p, r, KP_RSTATE_IDLE);
}

#define HELPING_PRIORITY 50
#define HELPING_WAITS 10
#define HELPED_RETRY 5

/* RegionStateRequestHelp 0x4aca00 (state 3): unless a neighbour already helps it (state 7 towards it), the largest
 * untargeted neighbour in state 5, 4 or 0 (not 0 for a campaign AI) on a safe route from the first town (the second
 * largest when one is found after the largest) comes to help. Ground neighbours, all for an inaccessible region. */
static void state_request_help(KpGame *g, int64_t p, int64_t r)
{
	KpAiRegions *a = &g->air;
	int64_t at, count, k, nb, tiles, best1 = 0, best2 = 0, size1 = 0, size2 = 0, base = -1, chosen;
	kp_ai_update_region_strength(g, p, r);
	update_targets(g, p, r);
	if (a->target_air[p][r] == 0 && a->target_ground[p][r] == 0) decay_needs(g, p, r);
	if (g->pathing.rg.klass[r] == KP_REGION_UNWALKABLE) count = kp_region_all_neighbors(g, r, &at); /* 0x481ad0 */
	else {
		at = g->pathing.rg.nb_at[r]; /* GetRegionGroundNeighbors 0x481ab0 */
		count = g->pathing.rg.ground[r];
	}
	for (k = 0; k < count; k++) {
		nb = g->pathing.rg.neighbors[at + k];
		if (a->state[p][nb] == KP_RSTATE_HELP && a->dest[p][nb] == r) return;
	}
	if (g->towns.heads[p] >= 0) { /* GetFirstTownCenter 0x4872a0 */
		int64_t first = g->towns.heads[p];
		base = kp_region_at(g, kp_s16v(g->towns.x[first]), kp_s16v(g->towns.y[first]));
	}
	for (k = 0; k < count; k++) {
		int64_t s;
		nb = g->pathing.rg.neighbors[at + k];
		s = a->state[p][nb];
		if (a->target_air[p][nb] != 0 || a->target_ground[p][nb] != 0) continue;
		if (s == KP_RSTATE_IDLE ? kp_ai_campaign(g, p) : s != KP_RSTATE_BASE && s != KP_RSTATE_WATCH) continue;
		if (base >= 0 && !kp_ai_route_interior_safe(g, p, base, nb)) continue; /* 0x496ac0 */
		tiles = kp_region_tiles(g, nb);
		if (tiles > size1) {
			size1 = tiles;
			best1 = nb;
		} else if (tiles > size2) {
			size2 = tiles;
			best2 = nb;
		}
	}
	chosen = best2 != 0 ? best2 : best1;
	if (chosen == 0) return;
	a->dest[p][chosen] = r;
	kp_ai_set_region_state(g, p, chosen, KP_RSTATE_HELP);
}

/* RegionStateHelping 0x4acbc0 (state 7): a helping region (unless it disperses) attacked itself defends (state 6);
 * else, once strong enough for its destination's needs (or after ten waits), its military joins the destination,
 * which defends with retry 5, and the region watches (state 5). */
static void state_helping(KpGame *g, int64_t p, int64_t r)
{
	KpAiRegions *a = &g->air;
	int64_t d;
	kp_ai_update_region_strength(g, p, r);
	update_targets(g, p, r);
	if (kp_band(a->flags[p][r], KP_RFLAG_UNDER_ATTACK) != 0 && disperse(g, p, r)) return;
	d = a->dest[p][r];
	if (a->priority[p][r] != HELPING_PRIORITY && a->target_air[p][r] == 0 && a->target_ground[p][r] == 0
		&& a->target_air[p][d] == 0 && a->target_ground[p][d] == 0) {
		a->priority[p][r] = HELPING_PRIORITY;
	}
	if ((a->target_air[p][r] != 0 && !kp_is_critter(g, a->target_air[p][r]))
		|| (a->target_ground[p][r] != 0 && !kp_is_critter(g, a->target_ground[p][r]))) {
		kp_ai_set_region_state(g, p, r, KP_RSTATE_DEFEND);
		return;
	}
	if (a->wait[p][r] < HELPING_WAITS) {
		a->wait[p][r]++;
		if (kp_band(a->local_air[p][r], 0xffff) < kp_band(a->need_air[p][d], 0xffff)
			|| kp_band(a->local_ground[p][r], 0xffff) < kp_band(a->need_ground[p][d], 0xffff)) {
			return;
		}
	}
	kp_ai_set_region_state(g, p, d, KP_RSTATE_DEFEND);
	kp_ai_transfer_military(g, p, r, d, 1, 1, 1); /* 0x4b15d0 */
	kp_ai_set_region_state(g, p, r, KP_RSTATE_BASE);
	a->retry[p][d] = HELPED_RETRY;
}

void kp_ai_step_region(KpGame *g, int64_t p, int64_t r)
{
	KpAiRegions *a = &g->air;
	a->flags[p][r] = kp_band(a->flags[p][r], kp_bnot(REGION_RECENT));
	if (a->retry[p][r] != 0) a->retry[p][r]--;
	switch (a->state[p][r]) {
	case KP_RSTATE_IDLE: /* 0x4ac9b0 */
		kp_ai_update_region_strength(g, p, r);
		decay_needs(g, p, r);
		return;
	case KP_RSTATE_WATCH: case KP_RSTATE_BASE: state_watch(g, p, r); return;
	case KP_RSTATE_DEFEND: state_defend(g, p, r); return;
	case KP_RSTATE_MOVE: case KP_RSTATE_GROUP: state_moving(g, p, r); return;
	case KP_RSTATE_ABANDONED: state_request_help(g, p, r); return;
	case KP_RSTATE_HELP: state_helping(g, p, r); return;
	case KP_RSTATE_GATHER: case KP_RSTATE_ATTACK: state_gather(g, p, r); return;
	default: return;
	}
}

/* RebuildEnemyStrengthMaps 0x4abf90: each computer player's view of its enemies' strength per region. */
void kp_ai_rebuild_enemy_strength(KpGame *g)
{
	KpUnits *u = &g->u;
	KpAiRegions *a = &g->air;
	int64_t count = (int64_t)g->regions.count, r, n;
	int p;
	if (g->ai.region_player != 0) return;
	for (p = 0; p < KP_AI_PLAYERS; p++) {
		for (r = 1; r < count; r++) a->enemy_air[p][r] = a->enemy_ground[p][r] = 0;
	}
	for (n = g->lists[KP_LIST_ACTIVE].head; n != 0; n = u->next[n]) {
		int64_t v = u->air_strength[n] > u->ground_strength[n] ? u->air_strength[n] : u->ground_strength[n];
		int64_t r2;
		if (u->sprite[n] == KP_NONE || u->order[n] == KP_ORD_DIE || v == 0) continue; /* IsUnitInactive 0x41fe40 */
		r2 = kp_unit_region(g, n);
		for (p = 0; p < KP_AI_PLAYERS; p++) {
			if (kp_controller(g, p) != KP_CTRL_COMPUTER || u->owner[n] == p || !kp_player_enemy_of(g, p, n)) continue;
			if (kp_band(u->status[n], STATUS_AIR) != 0) a->enemy_air[p][r2] = kp_mod(a->enemy_air[p][r2] + v, 65536);
			else a->enemy_ground[p][r2] = kp_mod(a->enemy_ground[p][r2] + v, 65536);
		}
	}
}

/* --- the military reachability cache (0x6b1aa0, docs/research/native-ai-routes.md) --- */

/* A region's tile count (the RGNS record's WORD +4). */
int64_t kp_region_tiles(KpGame *g, int64_t r)
{
	return kp_rgns_read(g, g->regions.regions_at + r * KP_REGION_SIZE + 4, 2);
}

/* GetRegionAllNeighbors 0x481ad0: the neighbour count less its count of region 0 (the native signed byte +0x21, byte 8 of the RGNS record). */
int64_t kp_region_all_neighbors(KpGame *g, int64_t r, int64_t *at)
{
	KpRegionGraph *rg = &g->pathing.rg;
	*at = rg->nb_at[r];
	return rg->nb_count[r] - kp_s8((uint32_t)kp_rgns_read(g, g->regions.regions_at + r * KP_REGION_SIZE + 8, 1));
}

/* BuildMilitaryReachCache 0x4abe90: from the source, the ground neighbours reachable through regions without targets
 * are marked 1 and flooded; a region with an air or ground target ends the flood there (2 once visited). */
static void build_reach(KpGame *g, int64_t p, int64_t source)
{
	KpRegionGraph *rg = &g->pathing.rg;
	uint8_t *reach = g->ai_reach[p];
	int64_t count = (int64_t)g->regions.count, r, changed;
	g->ai_reach_time[p] = kp_scalar(g, KP_ELAPSED);
	g->ai_reach_source[p] = source;
	memset(reach, 0, KP_AI_REGIONS);
	reach[source] = 1;
	do {
		changed = 0;
		for (r = 1; r < count; r++) {
			int64_t k, at;
			if (reach[r] != 1) continue;
			reach[r] = 2;
			at = rg->nb_at[r];
			for (k = 0; k < rg->ground[r]; k++) {
				int64_t nb = rg->neighbors[at + k];
				if (reach[nb] != 0) continue;
				reach[nb] = g->air.target_air[p][nb] == 0 && g->air.target_ground[p][nb] == 0 ? 1 : 2;
				changed = 1;
			}
		}
	} while (changed);
}

/* IsRegionReachableForMilitary 0x4ab330. */
int kp_ai_military_reachable(KpGame *g, int64_t p, int64_t source, int64_t target)
{
	int64_t at, k, count, t;
	if (kp_u32v(p) >= 8 || source == target) return 1;
	if (kp_region_group_of(g, target) != kp_region_group_of(g, source)) return 0;
	count = kp_region_all_neighbors(g, source, &at);
	for (k = 0; k < count; k++) {
		if (g->pathing.rg.neighbors[at + k] == target) return 1;
	}
	t = g->air.target_air[p][source];
	if (t != 0 && kp_air_weapon_with_subunit(g, t) != KP_WEAPON_NONE) return 1;
	if (kp_scalar(g, KP_ELAPSED) >= kp_u32v(g->ai_reach_time[p] + 10) || g->ai_reach_source[p] != source) {
		build_reach(g, p, source);
	}
	return g->ai_reach[p][target] == 2;
}

/* CanUnitReachPoint 0x4ab450. */
int kp_ai_can_reach_point(KpGame *g, int64_t n, int64_t x, int64_t y)
{
	return kp_ai_military_reachable(g, g->u.owner[n], kp_unit_region(g, n), kp_region_at(g, kp_s16v(x), kp_s16v(y)));
}

/* IsPointUnreachable 0x421370: a pathing unit cannot reach (x, y) (another ground group, or no route past the
 * computer's held regions). */
int kp_point_unreachable(KpGame *g, int64_t n, int64_t x, int64_t y)
{
	if (kp_band(g->u.pathing_flags[n], 1) == 0) return 0;
	if (!kp_placement_reachable(g, n, x, y)) return 1;
	/* CanUnitReachPoint 0x4ab450 */
	return !kp_ai_military_reachable(g, g->u.owner[n], kp_unit_region(g, n), kp_region_at(g, x, y));
}

/* IsUnitUnreachable 0x421320: the point unreachable is the target's position. */
int kp_unit_unreachable(KpGame *g, int64_t n, int64_t target)
{
	int32_t s = kp_unit_sprite(g, target);
	return kp_point_unreachable(g, n, kp_s16v(g->w->spr.x[s]), kp_s16v(g->w->spr.y[s]));
}

/* --- target choice --- */

#define STATUS_CAN_MOVE 0x20000
#define STATUS_HALLUCINATION 0x40000000
#define DAT_SPELLCASTER 0x200000
#define STATUS_INVINCIBLE 0x4000000
#define STATUS_CLOAKED 0x300
#define STATUS_UNREACHABLE 0x80
#define STATUS_INTERCEPT 0x80000
#define GUARD_RANGE 0xc0
#define REGION_REACH 0x120
#define DETECTOR_BONUS 1000

static int64_t head_order(KpGame *g, int64_t n) { return g->orders.order[kp_order_node(g, g->u.queue_head[n])]; }

/* The unit's guard record (its AI is a guard record of KpGuards), or -1. */
static int64_t guard_of(KpGame *g, int64_t n)
{
	int64_t ai = g->u.ai[n], k = kp_ai_ref_index(ai);
	if (ai == 0 || kp_ai_ref_kind(ai) != KP_AI_GUARD) return -1;
	if (k < 0 || k >= KP_GUARDS) kp_game_fail(g, "ai: the AI record of unit %lld", (long long)n);
	return k;
}

/* The AI type byte (+8) of the unit's record: 1 guard, 4 military. */
static int64_t ai_type(KpGame *g, int64_t n)
{
	int64_t k = guard_of(g, n);
	if (k >= 0) return g->guards.kind[k];
	return kp_ai_ref_kind(g->u.ai[n]);
}


/* The Carriers and Reavers with their heroes. */
static int hangar_parent(int64_t t) { return t == KP_TYPE_CARRIER || t == KP_TYPE_GANTRITHOR || t == KP_TYPE_REAVER || t == KP_TYPE_WARBRINGER; }

/* IsCritterType 0x429070. */
int kp_is_critter(KpGame *g, int64_t n)
{
	int64_t t = g->u.type[n];
	return t == KP_TYPE_RHYNADON || t == KP_TYPE_BENGALAAS || t == KP_TYPE_SCANTID || t == KP_TYPE_KAKARU
		|| t == KP_TYPE_RAGNASAUR || t == KP_TYPE_URSADON;
}

/* IsWithinApproxDistance 0x417aa0: the unit's exact position within dist of the point. */
int kp_within_distance(KpGame *g, int64_t n, int64_t dist, int64_t x, int64_t y)
{
	int64_t d = kp_approx_distance(kp_shlv(x, 8) - g->u.halt_x[n], kp_shlv(y, 8) - g->u.halt_y[n]);
	return kp_u32v(dist) >= kp_shrv(d, 8);
}

/* AdjustThreatByUnitType 0x4a9520. */
static int64_t adjust_threat(KpGame *g, int64_t n, int mode, int64_t v)
{
	KpUnits *u = &g->u;
	switch (u->type[n]) {
	case KP_TYPE_GHOST: return u->order[n] >= KP_ORD_NUKE_WAIT && u->order[n] <= KP_ORD_NUKE_TRACK ? 1000 : v;
	case KP_TYPE_VULTURE: return kp_u32v(v + kp_band(u->union_0xc0[n], 0xff) * 100);
	case KP_TYPE_DROPSHIP: case KP_TYPE_OVERLORD: case KP_TYPE_YGGDRASILL:
	case KP_TYPE_SHUTTLE: return kp_u32v(v + kp_cargo_space(g, n) * 100); /* GetUsedCargoSpace 0x438350 */
	case KP_TYPE_SIEGE_TANK_SIEGED: return kp_u32v(v + kp_shrv(v, 2));
	case KP_TYPE_MEDIC: return kp_u32v(v + g->strength[mode][0]);
	case KP_TYPE_OBSERVER: return kp_u32v(v + 50);
	default: return v;
	}
}

/* ScoreTargetThreat 0x4a9460. */
static int64_t score_target(KpGame *g, int64_t n, int mode)
{
	KpUnits *u = &g->u;
	int64_t t = u->type[n], v, life;
	if (t == KP_TYPE_LARVA || t == KP_TYPE_EGG || t == KP_TYPE_COCOON || t == KP_TYPE_LURKER_EGG) return 0;
	if (kp_band(u->status[n], STATUS_HALLUCINATION) != 0 && kp_current_life(g, n) < kp_max_life(g, n)) return 0;
	if (kp_is_critter(g, n)) return 1;
	v = adjust_threat(g, n, mode, kp_unit_type_strength(g, n, mode));
	life = kp_u32v(kp_current_life(g, n));
	if (life == 0) life = 1;
	v = kp_udiv64(g, kp_shlv(v, 7), life);
	if (v != 0) return v;
	return kp_band(u->status[n], STATUS_CAN_MOVE) != 0 ? 2 : 1;
}

/* ChooseRegionalTarget 0x4a90d0: the best-scoring enemy among the candidates. */
static int64_t choose_regional_target(KpGame *g, int64_t p, int64_t n, int64_t region, const int64_t *c, int count,
	int mode)
{
	KpUnits *u = &g->u;
	int64_t home = n != 0 ? kp_unit_region(g, n) : region, gk = n != 0 ? guard_of(g, n) : -1, best = -1, result = 0;
	int k;
	for (k = 0; k < count; k++) {
		int64_t x = c[k], score;
		if (!kp_player_enemy_of(g, p, x) && !kp_is_critter(g, x)) continue;
		if (kp_band(u->status[x], STATUS_INVINCIBLE) != 0) continue;
		if (kp_band(u->status[x], STATUS_CLOAKED) != 0 && kp_band(u->visibility[x], (int64_t)1 << p) == 0) continue;
		if (kp_band(u->status[x], STATUS_HALLUCINATION) != 0 && kp_current_life(g, x) < kp_max_life(g, x)) continue;
		if (n != 0 && !kp_can_attack(g, n, x, 1)) continue;
		if (kp_is_critter(g, x) && kp_ai_campaign(g, p)) continue;
		if (n != 0 && kp_reject_chase(g, n, x)) continue;
		if (gk >= 0 && g->guards.kind[gk] == KP_AI_GUARD) {
			int64_t hx = kp_s16v(g->guards.x1[gk]), hy = kp_s16v(g->guards.y1[gk]);
			if (kp_region_at(g, hx, hy) != kp_unit_region(g, x)
				&& (!kp_within_distance(g, x, GUARD_RANGE, hx, hy) || !kp_within_distance(g, n, GUARD_RANGE, hx, hy))) {
				continue;
			}
		} else if (kp_unit_region(g, x) != home && !kp_unit_box_within(g, n, REGION_REACH, x)) {
			continue;
		}
		score = score_target(g, x, mode);
		if (n != 0 && u->type[n] == KP_TYPE_WRAITH && kp_is_detector(g, x) && kp_band(u->status[x], STATUS_CAN_MOVE) != 0) {
			score += DETECTOR_BONUS;
		}
		if (kp_s32v(score) > best) {
			best = kp_s32v(score);
			result = x;
		}
	}
	return result;
}

/* ChooseAirTargetNearUnit 0x4a9760 (mode 0) and ChooseGroundTargetNearUnit 0x4a9860 (mode 1): the targets in the
 * unit's region's box grown by 288. */
static int64_t choose_target_near(KpGame *g, int64_t n, int mode)
{
	KpRegionGraph *rg = &g->pathing.rg;
	int64_t r, *found, result;
	int count;
	if (ai_type(g, n) == KP_AI_MILITARY) {
		/* GetRegionAirTarget 0x4ae840 / GetRegionGroundTarget 0x4ae870: its own record's region's target; none for a
		 * building */
		int64_t p, k = kp_ai_military_record(g, n, &p, &r);
		if (k < 0 || g->military.unit[k] != n || kp_band(KP_DAT(g, units, special_ability_flags, g->u.type[n]), DAT_BUILDING) != 0) {
			return 0;
		}
		return mode == 0 ? g->air.target_air[p][r] : g->air.target_ground[p][r];
	}
	r = kp_unit_region(g, n);
	found = kp_query_rect(g, kp_s16v(rg->left[r] - REGION_REACH), kp_s16v(rg->top[r] - REGION_REACH),
		kp_s16v(rg->right[r] + REGION_REACH), kp_s16v(rg->bottom[r] + REGION_REACH), &count);
	result = choose_regional_target(g, g->u.owner[n], n, r, found, count, mode);
	kp_query_release(g, found);
	return result;
}


/* IsThreatenedBy 0x41fd90: other (or the unit's target) can attack the unit now. */
int kp_ai_threatened_by(KpGame *g, int64_t n, int64_t o)
{
	KpUnits *u = &g->u;
	if (o == 0) o = u->order_unit[n];
	if (o == 0 || !kp_is_enemy(g, n, o)) return 0;
	if (kp_band(u->status[o], STATUS_HALLUCINATION) != 0 && kp_current_life(g, o) != kp_max_life(g, o)) return 0;
	if (kp_can_attack(g, o, n, 1)) return !kp_target_out_of_range(g, o, n);
	return u->type[o] == KP_TYPE_BUNKER && kp_has_cargo(g, o);
}

/* An Interceptor stands for its Carrier when the Carrier is in range or the unit can move. */
static int64_t carrier_of(KpGame *g, int64_t n, int64_t c)
{
	KpUnits *u = &g->u;
	if (c == 0 || n == 0) return 0;
	if (u->type[c] == KP_TYPE_INTERCEPTOR && u->union_0xc0[c] != 0
		&& (kp_target_in_range(g, n, u->union_0xc0[c]) || kp_band(u->status[n], STATUS_CAN_MOVE) != 0)) {
		return u->union_0xc0[c];
	}
	return c;
}

static int dangerous(KpGame *g, int64_t c)
{
	return (g->u.type[c] == KP_TYPE_BUNKER && kp_has_cargo(g, c)) || kp_unit_has_weapon(g, c);
}

/* ChooseBetterAttackTarget 0x4ae080. */
static int64_t choose_better(KpGame *g, int64_t n, int64_t a, int64_t b)
{
	a = carrier_of(g, n, a);
	b = carrier_of(g, n, b);
	if (a != 0 && (kp_reject_chase(g, n, a) || !kp_can_attack(g, n, a, 1))) a = 0;
	if (b != 0 && (kp_reject_chase(g, n, b) || !kp_can_attack(g, n, b, 1))) b = 0;
	if (a == 0) return b;
	if (b == 0) return a;
	if (!dangerous(g, b) && kp_unit_has_weapon(g, a)) return a;
	if (!dangerous(g, a) && kp_unit_has_weapon(g, b)) return b;
	if (!kp_ai_threatened_by(g, n, a) && kp_ai_threatened_by(g, n, b)) return b;
	if (kp_target_in_range(g, n, a)) return a;
	if (kp_target_in_range(g, n, b)) return b;
	return kp_unit_distance(g, n, a) <= kp_unit_distance(g, n, b) ? a : b;
}

/* AiMayRetarget 0x4ae040: a military unit in its record's region does not retarget while the region supports. */
static int may_retarget(KpGame *g, int64_t n)
{
	int64_t p, r;
	if (ai_type(g, n) != KP_AI_MILITARY) return 1;
	kp_ai_military_record(g, n, &p, &r);
	if (kp_band(kp_unit_region(g, n), 0xffff) != r) return 1;
	return g->air.state[p][r] != KP_RSTATE_HELP;
}

/* GuardReturnHome 0x4b2110: a guard away from its post and not fighting goes home (check_only only tells). */
int kp_ai_guard_return_home(KpGame *g, int64_t n, int check_only)
{
	KpUnits *u = &g->u;
	int64_t k = guard_of(g, n), hx, hy;
	if (k < 0 || g->guards.kind[k] != KP_AI_GUARD) return 0;
	if (kp_unit_race(g, n) == KP_RACE_TERRAN
		&& kp_band(KP_DAT(g, units, special_ability_flags, u->type[n]), 0x40000000) != 0
		&& u->hp[n] != KP_DAT(g, units, hit_points, u->type[n])) {
		return 0;
	}
	if (u->auto_target[n] != 0) return 0;
	hx = kp_s16v(g->guards.x1[k]);
	hy = kp_s16v(g->guards.y1[k]);
	if (kp_region_at(g, hx, hy) == kp_unit_region(g, n)) return 0;
	if (kp_within_distance(g, n, GUARD_RANGE, hx, hy)) return 0;
	if (u->order_unit[n] != 0 && kp_target_in_range(g, n, 0)) return 0;
	if (u->queue_head[n] != KP_NONE && head_order(g, n) == KP_ORD_PATROL) return 0;
	if (!check_only) kp_issue_order_at(g, n, KP_ORD_COMPUTER_RETURN, hx, hy);
	return 1;
}

/* The AI target test of 0x41f6c0. */
static int valid_target(KpGame *g, int64_t n, int64_t c, int allow_critters, int strict)
{
	if (c == 0) return 0;
	if (strict && kp_unit_unreachable(g, n, c)) return 0;
	if (!kp_can_attack(g, n, c, 1)) return 0;
	return allow_critters || !kp_is_critter(g, c);
}

/* AiUpdateAttackTarget 0x41f6c0: 1 when a target is (still) chosen. */
int kp_ai_update_attack_target(KpGame *g, int64_t n, int issue, int allow_critters, int strict)
{
	KpUnits *u = &g->u;
	int64_t c1, cur, a, best;
	int air = kp_band(u->status[n], STATUS_AIR) != 0;
	if (u->ai[n] == 0 || !kp_unit_has_weapon(g, n)) return 0;
	if (u->queue_head[n] != KP_NONE && head_order(g, n) == KP_ORD_PATROL) return 0;
	if (u->order[n] == KP_ORD_TRANSPORT_REQUEST) {
		strict = 1;
		if (u->auto_target[n] == 0 && u->queue_head[n] != KP_NONE && head_order(g, n) == KP_ORD_ATTACK_UNIT) return 0;
	}
	if (u->order_unit[n] != 0 && kp_band(u->status[n], STATUS_CAN_MOVE) != 0 && u->move_unit[n] != 0
		&& kp_is_enemy(g, n, u->move_unit[n]) && kp_move_arrival(g, n) == 2) {
		int32_t s = kp_unit_sprite(g, n);
		u->move_x[n] = kp_s16v(g->w->spr.x[s]);
		u->move_y[n] = kp_s16v(g->w->spr.y[s]);
		u->status[n] = kp_band(u->status[n], kp_bnot(STATUS_INTERCEPT));
		u->status[u->order_unit[n]] = kp_bor(u->status[u->order_unit[n]], STATUS_UNREACHABLE);
		if (u->order_unit[n] == u->auto_target[n]) u->auto_target[n] = 0;
		kp_clear_order_target(g, n);
	}
	c1 = choose_target_near(g, n, air ? 0 : 1);
	if (!valid_target(g, n, c1, allow_critters, strict)) {
		c1 = choose_target_near(g, n, air ? 1 : 0);
		if (!valid_target(g, n, c1, allow_critters, strict)) c1 = 0;
	}
	cur = KP_DAT(g, orders, use_weapon_targeting, u->order[n]) != 0 ? u->order_unit[n] : 0;
	if (!valid_target(g, n, cur, allow_critters, strict)) cur = 0;
	if (strict && u->auto_target[n] != 0 && kp_unit_unreachable(g, n, u->auto_target[n])) u->auto_target[n] = 0;
	a = u->auto_target[n];
	if (a != 0 && !kp_unit_disabled(g, a)) {
		int64_t at = u->order_unit[a];
		if ((u->type[a] != KP_TYPE_BUNKER && (at == 0 || u->owner[at] != u->owner[n]))
			|| (strict && kp_unit_unreachable(g, n, a))) {
			u->auto_target[n] = 0;
		}
		a = u->auto_target[n];
		if (a != 0 && (!kp_can_attack(g, n, a, 1) || (!allow_critters && kp_is_critter(g, a)))) u->auto_target[n] = 0;
	}
	best = choose_better(g, n, choose_better(g, n, cur, c1), u->auto_target[n]);
	if (best == 0) {
		int64_t c;
		if (!may_retarget(g, n)) return 0;
		c = kp_ai_guard_return_home(g, n, 1) ? 0 : kp_acquire_target(g, n);
		best = choose_better(g, n, 0, c);
		if (!valid_target(g, n, best, allow_critters, strict)) return 0;
	}
	if (best != u->order_unit[n]) {
		if (kp_band(u->status[n], STATUS_IN_BUILDING) != 0) u->order_unit[n] = best;
		else kp_issue_attack(g, n, best, 1, issue);
		if (hangar_parent(u->type[n]) && !kp_target_in_range(g, n, best)) {
			u->status[n] = kp_bor(u->status[n], 0x8);
		}
	}
	return 1;
}


/* SetGuardHome 0x4b2270: the home of a guard record. */
void kp_ai_set_guard_home(KpGame *g, int64_t n, int64_t x, int64_t y)
{
	int64_t k = guard_of(g, n);
	if (k < 0 || g->guards.kind[k] != KP_AI_GUARD) return;
	g->guards.x1[k] = kp_s16v(x);
	g->guards.y1[k] = kp_s16v(y);
}


/* TryEvadeAttacker 0x4aadf0: 1 when the unit moved away from attacker (or retreated). */
static int try_evade(KpGame *g, int64_t n, int64_t attacker)
{
	KpUnits *u = &g->u;
	int64_t t = u->type[n], status = u->status[n], x, y;
	int32_t s;
	if (kp_band(status, STATUS_CAN_MOVE) == 0 || u->order[n] == KP_ORD_UNBURROWING || t == KP_TYPE_LARVA
		|| (t == KP_TYPE_LURKER && u->irradiate_timer[n] != 0)) {
		return 0;
	}
	if (((kp_band(KP_DAT(g, units, special_ability_flags, t), DAT_SPELLCASTER) != 0
			&& kp_band(status, STATUS_HALLUCINATION) == 0) || t == KP_TYPE_OVERLORD) && kp_ai_return_to_base(g, n)) {
		return 1;
	}
	if (t == KP_TYPE_CARRIER && kp_mod(kp_band(u->union_0xc8[n], 0xff) + kp_band(kp_shrv(u->union_0xc8[n], 8), 0xff), 256) <= 4
		&& kp_ai_return_to_base(g, n)) {
		return 1;
	}
	kp_flee_position(g, attacker, n, &x, &y);
	s = kp_unit_sprite(g, n);
	if (x == kp_s16v(g->w->spr.x[s]) && y == kp_s16v(g->w->spr.y[s])) return 0;
	kp_issue_order_packed(g, n, KP_ORD_MOVE, x, y, 0);
	return 1;
}

/* Order_ComputerReturn 0x41e370 (order 163): a guard goes back to its home, which becomes where it stops; a unit of
 * another AI (a detector sent to an undetected enemy, 0x4ae3e0) goes back to the computer's AI once it moves. */
void kp_order_computer_return(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int32_t s;
	if (kp_ai_update_attack_target(g, n, 0, 1, 0)) return;
	if (u->type[n] == KP_TYPE_MEDIC) {
		kp_order_medic_idle(g, n);
		if (u->order[n] != KP_ORD_COMPUTER_RETURN) return;
	}
	if (u->order_state[n] == 0) {
		if (!kp_request_move_to_point(g, n, kp_s16v(u->order_x[n]), kp_s16v(u->order_y[n]))) return;
		u->order_state[n] = 1;
	}
	/* without an AI record nothing more; any AI type but a guard's goes back to the computer's AI */
	if (u->ai[n] == 0) return;
	if (ai_type(g, n) != KP_AI_GUARD) {
		kp_issue_order(g, n, KP_ORD_COMPUTER_AI);
		return;
	}
	if (kp_move_arrival(g, n) == 0) return;
	s = kp_unit_sprite(g, n);
	kp_ai_set_guard_home(g, n, kp_s16v(g->w->spr.x[s]), kp_s16v(g->w->spr.y[s]));
	kp_issue_order(g, n, KP_ORD_GUARD);
}

/* ReactToDisabledEnemyCallback 0x4aaaa0 over FindUnitInRadiusBox 0x442b20: the computer's units around `enemy` that
 * can attack it turn on it; the second pass (0x6b1a9c) only takes flyers. */
void kp_ai_attack_helpless(KpGame *g, int64_t enemy, int64_t radius, int second_pass)
{
	KpUnits *u = &g->u;
	int32_t s = kp_unit_sprite(g, enemy);
	int64_t x = kp_s16v(g->w->spr.x[s]), y = kp_s16v(g->w->spr.y[s]), *found;
	int count, k;
	found = kp_query_rect(g, kp_s16v(x - radius), kp_s16v(y - radius), kp_s16v(x + radius), kp_s16v(y + radius), &count);
	for (k = 0; k < count; k++) {
		int64_t c = found[k];
		if (u->ai[c] == 0) continue;
		if (kp_band(u->status[c], STATUS_AIR) == 0 && (second_pass || kp_band(u->status[enemy], 0x8000) != 0)) continue;
		if (!kp_is_enemy(g, c, enemy) || u->auto_target[c] != 0 || !kp_can_attack(g, c, enemy, 1)) continue;
		if (kp_band(u->status[c], 0x20000) != 0 || kp_target_in_range(g, c, enemy)) kp_ai_react_to_attack(g, c, enemy, 0, 1);
	}
	kp_query_release(g, found);
}

/* Order_AiGuard 0x41e7f0 (order 160). */
void kp_order_ai_guard(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t k;
	if (u->order_timer[n] != 0) return;
	u->order_timer[n] = 15;
	if (kp_ai_reaction_spell(g, n, 0)) return;
	if (kp_ai_update_attack_target(g, n, 0, 1, 0)) return;
	if (u->type[n] == KP_TYPE_OBSERVER) return;
	k = guard_of(g, n);
	if (k >= 0 && g->guards.kind[k] == KP_AI_GUARD) {
		int32_t s = kp_unit_sprite(g, n);
		int64_t hx = kp_s16v(g->guards.x1[k]), hy = kp_s16v(g->guards.y1[k]);
		if (kp_s16v(g->w->spr.x[s]) != hx || kp_s16v(g->w->spr.y[s]) != hy) {
			if (u->type[n] == KP_TYPE_SIEGE_TANK_SIEGED) kp_order_append(g, n, KP_ORD_UNSIEGING, 0, 0, 0, 0xe4, 0);
			kp_order_append(g, n, KP_ORD_COMPUTER_RETURN, hx, hy, 0, 0xe4, 0);
			kp_order_done(g, n);
			return;
		}
	}
	kp_issue_order(g, n, KP_ORD_COMPUTER_AI);
}

#define STATUS_BURROWED 0x10
#define DAT_CLOAKABLE 0x200
#define DAT_BURROWABLE 0x100000
#define DAT_MECHANICAL 0x40000000
#define DAT_WORKER 0x08

/* AiTryBurrow 0x48de70: an idle computer unit at its move target burrows when it can. */
static void try_burrow(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int32_t s = kp_unit_sprite(g, n);
	if (kp_band(u->status[n], STATUS_BURROWED) != 0 || u->type[n] == KP_TYPE_DRONE) return;
	if (u->order[n] != KP_ORD_COMPUTER_AI && u->order[n] != KP_ORD_GUARD) return;
	if (u->move_x[n] != kp_s16v(g->w->spr.x[s]) || u->move_y[n] != kp_s16v(g->w->spr.y[s])) return;
	/* the status 0x80000 test that follows always passes */
	if (kp_can_use_tech(g, KP_TECH_BURROWING, u->owner[n], n) == 1) kp_issue_order(g, n, KP_ORD_BURROWING);
}

#define ARBITER_REACH 640
#define DETECTOR_FAR 9999999
#define STATUS_COMPLETED 0x1
#define DECLOAK_REACH 256
#define STATUS_FREE_CLOAK 0x800
#define BATTERY_ENERGY 50
#define BATTERY_SHIELDS 0x2800
#define MINE_SPACING 96
#define MINE_SCATTER 0x1ff

static int town_builder(KpGame *g, int64_t c, int64_t n);

/* ChooseSpiderMinePoint 0x4aac20: a random point around a random base region where the owner has no mine yet. */
static int mine_point(KpGame *g, int64_t n, int64_t *px, int64_t *py)
{
	KpUnits *u = &g->u;
	int64_t p = u->owner[n], count = (int64_t)g->regions.count, r, regions[KP_AI_REGIONS], x, y, sx, sy, *found;
	int k = 0, hits, i, mine = 0;
	for (r = 1; r < count; r++) {
		int64_t state = g->air.state[p][r];
		if ((state == KP_RSTATE_BASE || state == KP_RSTATE_WATCH) && kp_band(g->air.flags[p][r], KP_RFLAG_GUARDED | KP_RFLAG_NO_DECAY) != 0) regions[k++] = r;
	}
	if (k == 0) return 0;
	r = regions[kp_rng_game_raw(g->rng, 0x2c) % (uint32_t)k];
	kp_ai_region_center(g, r, &x, &y);
	x += kp_band(kp_rng_game_raw(g->rng, 0x2d), MINE_SCATTER) - 256;
	y += kp_band(kp_rng_game_raw(g->rng, 0x2e), MINE_SCATTER) - 256;
	if (x < 0) x = 0;
	else if (x >= kp_mod(g->map_w_px, 65536)) x = kp_mod(g->map_w_px, 65536) - 1;
	if (y < 0) y = 0;
	else if (y >= kp_mod(g->map_h_px, 65536)) y = kp_mod(g->map_h_px, 65536) - 1;
	/* UnitCanReachPoint 0x481d00 and IsPixelInaccessible 0x481be0 */
	kp_sprite_pos(g, n, &sx, &sy);
	if (kp_region_group_of(g, kp_region_at(g, sx, sy)) != kp_region_group_of(g, kp_region_at(g, x, y))) return 0;
	if (g->pathing.rg.klass[kp_rg_check(g, kp_region_at(g, x, y))] == KP_REGION_UNWALKABLE) return 0;
	found = kp_query_rect(g, kp_s16v(x - MINE_SPACING), kp_s16v(y - MINE_SPACING), kp_s16v(x + MINE_SPACING),
		kp_s16v(y + MINE_SPACING), &hits);
	for (i = 0; i < hits && !mine; i++) mine = u->type[found[i]] == KP_TYPE_SPIDER_MINE && u->owner[found[i]] == p;
	kp_query_release(g, found);
	if (mine) return 0;
	*px = x;
	*py = y;
	return 1;
}

/* The callback 0x432620 of FindNearestArmedEnemy256 0x432bd0: an armed enemy of unit n. */
static int armed_enemy(KpGame *g, int64_t c, int64_t n) { return c != n && kp_is_enemy(g, n, c) && kp_unit_has_weapon(g, c); }

static int nuke_order(int64_t order) { return order >= KP_ORD_NUKE_WAIT && order <= KP_ORD_NUKE_TRACK; }

/* AiUnitTypeStep 0x48e180: 1 when the unit was given something to do. */
static int unit_type_step(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	if (u->secondary_order[n] == KP_ORD_CLOAK && kp_band(KP_DAT(g, units, special_ability_flags, u->type[n]), DAT_CLOAKABLE) != 0
		&& !kp_ai_threatened_by(g, n, 0) && u->auto_target[n] == 0
		&& kp_nearest_unit_around(g, n, DECLOAK_REACH, armed_enemy, n) == 0 && !nuke_order(u->order[n])) {
		kp_set_secondary_order(g, n, KP_ORD_DECLOAK);
	}
	switch (u->type[n]) {
	case KP_TYPE_VULTURE: {
		int64_t x, y;
		if (kp_can_use_tech(g, KP_TECH_SPIDER_MINES, u->owner[n], n) != 1 || !mine_point(g, n, &x, &y)) return 0;
		kp_issue_order_at(g, n, KP_ORD_PLACE_MINE, x, y);
		return 1;
	}
	case KP_TYPE_SIEGE_TANK:
		if (ai_type(g, n) != KP_AI_GUARD || kp_ai_guard_return_home(g, n, 0)
			|| kp_can_use_tech(g, KP_TECH_TANK_SIEGE_MODE, u->owner[n], n) != 1 || u->queue_head[n] != KP_NONE) {
			return 0;
		}
		kp_issue_order(g, n, KP_ORD_SIEGING);
		return 1;
	case KP_TYPE_DROPSHIP: {
		int64_t max = kp_sarv(kp_s32v(KP_DAT(g, units, hit_points, u->type[n])), 8), builder;
		if (max == 0) max = kp_sarv(u->hp[n] + 0xff, 8) != 0 ? kp_sarv(u->hp[n] + 0xff, 8) : 1; /* 0x409c90 */
		if (kp_sarv(u->hp[n] + 0xff, 8) != max) {
			builder = kp_nearest_unit_map(g, n, town_builder, n); /* 0x432bb0 */
			if (builder != 0) {
				if (kp_has_cargo(g, n)) kp_issue_order(g, n, KP_ORD_UNLOAD);
				else kp_issue_order_target(g, n, KP_ORD_FOLLOW, builder);
				return 1;
			}
		}
	}
		/* fall through */
	case KP_TYPE_SHUTTLE:
		if (kp_ai_return_to_base(g, n)) return 1;
		/* fall through */
	case KP_TYPE_OVERLORD:
		if (!kp_has_cargo(g, n)) return 0;
		kp_issue_order(g, n, KP_ORD_UNLOAD);
		return 1;
	case KP_TYPE_ARBITER: { /* Arbiter: to the first own fighting unit within 640 */
		int32_t s = kp_unit_sprite(g, n);
		int64_t x = g->w->spr.x[s], y = g->w->spr.y[s], *found, target = 0;
		int count, k;
		found = kp_query_rect(g, kp_s16v(x - ARBITER_REACH), kp_s16v(y - ARBITER_REACH), kp_s16v(x + ARBITER_REACH),
			kp_s16v(y + ARBITER_REACH), &count);
		for (k = 0; k < count && target == 0; k++) {
			int64_t c = found[k];
			if (u->owner[c] == u->owner[n] && u->auto_target[c] != 0
				&& kp_band(KP_DAT(g, units, special_ability_flags, u->type[c]), DAT_BUILDING) == 0) {
				target = c;
			}
		}
		kp_query_release(g, found);
		if (target == 0) return 0;
		s = kp_unit_sprite(g, target);
		kp_issue_order_at(g, n, KP_ORD_ATTACK_MOVE, kp_s16v(g->w->spr.x[s]), kp_s16v(g->w->spr.y[s]));
		return 1;
	}
	case KP_TYPE_CARRIER: case KP_TYPE_REAVER:
		kp_queue_fighter(g, n);
		return 0;
	case KP_TYPE_MISSILE_TURRET: case KP_TYPE_SPORE_COLONY: case KP_TYPE_SUNKEN_COLONY: case KP_TYPE_PHOTON_CANNON:
		kp_issue_order(g, n, KP_ORD_TOWER_GUARD);
		return 1;
	case KP_TYPE_SHIELD_BATTERY: { /* Shield Battery: recharge the first unit low on shields */
		int64_t c;
		if (kp_shrv(kp_mod(u->energy[n], 65536), 8) < BATTERY_ENERGY) return 0;
		for (c = g->player_units[u->owner[n]]; c != 0; c = u->player_next[c]) {
			if (!kp_can_recharge_from(g, c, n) || u->auto_target[c] != 0 || kp_ai_in_attack_region(g, c, 0)) continue;
			if (kp_s32v(kp_band(u->shields[c], 0xffffff00)) <= BATTERY_SHIELDS) {
				int32_t s = kp_unit_sprite(g, c);
				kp_issue_order_at(g, n, KP_ORD_RECHARGE_SHIELDS_BATTERY, kp_s16v(g->w->spr.x[s]), kp_s16v(g->w->spr.y[s]));
				return 0;
			}
		}
		return 0;
	}
	default:
		return 0;
	}
}

#define STATUS_IN_TRANSPORT 0x40

/* IsTownBuilderScvCallback 0x432e70: a same-owner worker that is its AI town's builder. */
static int town_builder(KpGame *g, int64_t c, int64_t n)
{
	KpUnits *u = &g->u;
	if (u->owner[c] != u->owner[n] || kp_band(KP_DAT(g, units, special_ability_flags, u->type[c]), DAT_WORKER) == 0
		|| kp_band(u->status[c], STATUS_IN_TRANSPORT) != 0 || u->ai[c] == 0) {
		return 0;
	}
	return kp_ai_town_repair_scv(g, c) == c; /* GetUnitTownRepairScv 0x487530 */
}

/* AiTrySiege 0x48dee0: an idle tank of a computer without the campaign flag sieges. */
static void try_siege(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int32_t s = kp_unit_sprite(g, n);
	if (kp_ai_campaign(g, u->owner[n]) || (u->order[n] != KP_ORD_COMPUTER_AI && u->order[n] != KP_ORD_GUARD)) return;
	if (u->move_x[n] != kp_s16v(g->w->spr.x[s]) || u->move_y[n] != kp_s16v(g->w->spr.y[s])) return;
	/* the status 0x80000 test that follows always passes */
	if (kp_can_use_tech(g, KP_TECH_TANK_SIEGE_MODE, u->owner[n], n) == 1) kp_issue_order(g, n, KP_ORD_SIEGING);
}

/* The order timer after a building's step: long when its consumer took the request, short otherwise. */
#define BUILDING_PAUSE_DONE 45
#define BUILDING_PAUSE_RETRY 3
#define MILITARY_PAUSE 3

/* Order_ComputerAI 0x48cd70 (order 156). */
void kp_order_computer_ai(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t t, type;
	if (u->order_timer[n] != 0) return;
	t = u->type[n];
	u->order_timer[n] = 15;
	if (t == KP_TYPE_LARVA) {
		kp_order_larva(g, n); /* 0x43cd90 */
		if (kp_unit_inactive(g, n)) return; /* 0x41fe40 */
	}
	if (t == KP_TYPE_MEDIC) { /* a Medic of an idle region's military goes to a base (IsInStateZeroRegion 0x4ae8e0), else idles */
		int64_t p, r;
		if (kp_ai_military_record(g, n, &p, &r) >= 0 && g->air.state[p][r] == KP_RSTATE_IDLE) {
			kp_ai_return_to_base(g, n);
			return;
		}
		kp_issue_order(g, n, KP_ORD_MEDIC_IDLE);
		return;
	}
	if (u->ai[n] == 0) {
		kp_issue_order(g, n, KP_DAT(g, units, comp_ai_idle, t));
		return;
	}
	if (unit_type_step(g, n)) return;
	type = ai_type(g, n);
	if (type == KP_AI_WORKER) {
		kp_ai_run_worker(g, n); /* 0x487580 */
	} else if (type == KP_AI_BUILDING) {
		u->order_timer[n] = kp_ai_run_building(g, n) ? BUILDING_PAUSE_DONE : BUILDING_PAUSE_RETRY; /* 0x487110 */
	} else if (type == KP_AI_MILITARY) {
		kp_ai_military_step(g, n); /* MilitaryAiStep 0x4b1010 */
		u->order_timer[n] = MILITARY_PAUSE;
	} else {
		kp_issue_order(g, n, KP_DAT(g, units, comp_ai_idle, t));
	}
	if (kp_unit_race(g, n) == KP_RACE_TERRAN && kp_band(KP_DAT(g, units, special_ability_flags, t), DAT_MECHANICAL) != 0
		&& !kp_ai_in_attack_region(g, n, 0)
		&& kp_sarv(u->hp[n] + 0xff, 8) != kp_max_hit_points(g, n) && kp_band(u->status[n], STATUS_CAN_MOVE) != 0
		&& u->plague_timer[n] == 0 && u->irradiate_timer[n] == 0
		&& kp_band(KP_DAT(g, units, special_ability_flags, t), DAT_WORKER) == 0) {
		/* FindNearestTownBuilderWorker 0x432bb0: the damaged machine follows its town's builder to be repaired */
		int64_t builder = kp_nearest_unit_map(g, n, town_builder, n);
		if (builder != 0) kp_issue_order_target(g, n, KP_ORD_FOLLOW, builder);
		return;
	}
	if (t == KP_TYPE_SIEGE_TANK) try_siege(g, n);
	if (kp_band(KP_DAT(g, units, special_ability_flags, t), DAT_BURROWABLE) != 0) try_burrow(g, n);
}

#define STIM_LIFE 100

/* AiConsiderStim 0x4b1100: a computer's Marine or Firebat with a strong target uses Stim Packs. */
void kp_ai_consider_stim(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t target = u->order_unit[n], speed, half;
	if (u->type[n] != KP_TYPE_MARINE && u->type[n] != KP_TYPE_FIREBAT) return;
	if (target == 0) return;
	/* 0x428af0: the speed state of stim, the 0x10000000 status and ensnare */
	speed = (u->stim_timer[n] != 0) + (kp_band(u->status[n], 0x10000000) != 0) - (u->ensnare_timer[n] != 0);
	if (speed > 0) return;
	half = kp_max_hit_points(g, n);
	half = (half - (half < 0 ? -1 : 0)) >> 1; /* signed halving toward zero */
	if (kp_sarv(u->hp[n] + 0xff, 8) < half) return;
	if (!kp_can_attack(g, target, n, 1) || !kp_target_in_range(g, n, 0)) return;
	if (kp_can_use_tech(g, KP_TECH_STIM_PACKS, u->owner[n], n) != 1) return;
	if (kp_s32v(kp_current_life(g, target)) < STIM_LIFE) return;
	kp_apply_stim(g, n);
}

#define REAVER_DROP_DISTANCE 0x3c0

#define FAR_TRANSPORT 99999999

/* FindTransportForUnit 0x437f80: player's transport nearest passenger n (by n's fixed-point halt position against the
 * transport's pixel position) that takes it (already coming for it: that one); 1 when a transport fit to fly (at least
 * three quarters of its life, or full shields) exists. */
int kp_ai_find_transport(KpGame *g, int64_t n, int64_t *out)
{
	KpUnits *u = &g->u;
	int64_t c, best = FAR_TRANSPORT;
	int healthy = 0;
	*out = 0;
	for (c = g->player_units[u->owner[n]]; c != 0; c = u->player_next[c]) {
		int64_t t = u->type[c], max, life, d;
		if (kp_band(u->status[c], STATUS_HALLUCINATION) != 0) continue;
		if (t == KP_TYPE_OVERLORD && kp_upgrade_level(g, u->owner[c], KP_UPG_VENTRAL_SACS) == 0) continue;
		if (KP_DAT(g, units, space_provided, t) == 0) continue;
		if (kp_band(u->status[c], 1) == 0 || kp_band(u->status[c], STATUS_CAN_MOVE) == 0) continue;
		if (u->order_unit[c] == n) {
			*out = c;
			return 1;
		}
		life = kp_sarv(u->hp[c] + 0xff, 8);
		max = kp_sarv(KP_DAT(g, units, hit_points, t), 8);
		if (max == 0) max = life != 0 ? life : 1;
		if (life < kp_s32v(3 * max) / 4
			&& (KP_DAT(g, units, shield_enabled, t) == 0 || kp_sarv(u->shields[c], 8) != KP_DAT(g, units, shield_amount, t))) {
			continue;
		}
		healthy = 1;
		if (!kp_can_load(g, n, c) || u->order[c] == KP_ORD_PICKUP_TRANSPORT || u->order[c] == KP_ORD_UNLOAD) continue;
		if (u->order[c] == KP_ORD_MOVE_UNLOAD
			&& kp_band(kp_region_at(g, kp_s16v(u->order_x[c]), kp_s16v(u->order_y[c])), 0xffff)
				!= kp_band(kp_region_at(g, kp_s16v(u->order_x[n]), kp_s16v(u->order_y[n])), 0xffff)) {
			continue;
		}
		d = kp_u32v(kp_approx_distance(kp_s32v(u->halt_x[n]) - kp_s16v(g->w->spr.x[kp_unit_sprite(g, c)]),
			kp_s32v(u->halt_y[n]) - kp_s16v(g->w->spr.y[kp_unit_sprite(g, c)])));
		if (d < best) {
			best = d;
			*out = c;
		}
	}
	return healthy;
}

/* TryAiTransportForMove 0x437540: 1 when an AI ground unit's move goes to a transport request instead. */
int kp_ai_transport_for_move(KpGame *g, int64_t n, int64_t x, int64_t y)
{
	KpUnits *u = &g->u;
	int32_t s = kp_unit_sprite(g, n);
	int64_t sx = kp_s16v(g->w->spr.x[s]), sy = kp_s16v(g->w->spr.y[s]);
	if (u->order[n] == KP_ORD_TRANSPORT_REQUEST) return 1;
	if (kp_band(u->status[n], STATUS_AIR) != 0 || kp_band(KP_DAT(g, units, special_ability_flags, u->type[n]), 0x10) != 0) {
		return 0;
	}
	if ((sx == x && sy == y) || kp_band(u->status[n], STATUS_CAN_MOVE) == 0) return 0;
	if (u->type[n] == KP_TYPE_SCV && u->order[n] == KP_ORD_REPAIR && u->order_unit[n] != 0
		&& kp_band(u->status[u->order_unit[n]], STATUS_AIR) != 0) {
		return 0;
	}
	if (g->pathing.rg.klass[kp_region_at(g, x, y)] == KP_REGION_UNWALKABLE) return 0; /* 0x481bb0 */
	if (kp_same_region_group(g, n, x, y)) { /* UnitCanReachPoint 0x481d00 */
		if (kp_ai_in_attack_region(g, n, 1)) return 0;
		if (kp_ai_military_reachable(g, u->owner[n], kp_unit_region(g, n), kp_region_at(g, x, y))) {
			if (u->type[n] != KP_TYPE_REAVER || kp_approx_distance(x - sx, y - sy) < REAVER_DROP_DISTANCE) return 0;
		}
	}
	{ /* the current order waits behind a TransportRequest to the point */
		int64_t transport, sub;
		if (!kp_ai_find_transport(g, n, &transport)) return 0;
		if (u->queue_head[n] != KP_NONE) { /* InsertOrderBeforePacked 0x429e90 */
			kp_order_insert_before(g, n, u->order[n], u->order_x[n], u->order_y[n], u->order_unit[n], KP_TYPE_NONE, u->queue_head[n]);
		} else { /* QueueOrderPacked 0x42a0c0 */
			kp_order_append(g, n, u->order[n], u->order_x[n], u->order_y[n], u->order_unit[n], KP_TYPE_NONE, 0);
		}
		kp_order_insert_before(g, n, KP_ORD_TRANSPORT_REQUEST, x, y, 0, KP_TYPE_NONE, u->queue_head[n]); /* 0x429d70 */
		sub = u->subunit[n];
		if (kp_is_subunit(g, sub)) kp_issue_order(g, sub, KP_ORD_NOTHING); /* IsSubunit 0x420000 */
		kp_order_unlock_done(g, n); /* 0x429d00 */
		kp_set_target_switch(g, n); /* 0x41cf90 */
		return 1;
	}
}

/* --- the reaction to attacks --- */

#define TERRAN_FLAG_IN_BUNKER STATUS_IN_BUILDING
#define UNDER_ATTACK 0x20
#define CAMPAIGN_HELP_RANGE_BUILDING 0x120
#define HELP_RANGE_BUILDING 0x240
#define CAMPAIGN_HELP_RANGE 0x60
#define HELP_RANGE 0x120
#define DEFENSE_SECONDS 0x3c

/* GetUnitsInRegion 0x4222d0: the units of the region's box standing in the region (stacked like kp_query_rect). */
int64_t *kp_region_units(KpGame *g, int64_t r, int *count)
{
	KpRegionGraph *rg = &g->pathing.rg;
	int64_t *found = kp_query_rect(g, rg->left[r], rg->top[r], rg->right[r], rg->bottom[r], count);
	int k, kept = 0;
	for (k = 0; k < *count; k++) {
		if (kp_unit_region(g, found[k]) == r) found[kept++] = found[k];
	}
	kp_query_keep(g, found, kept);
	*count = kept;
	return found;
}

/* RegionHasOwnBuildingOrWorker 0x4861a0. */
static int region_has_own_base(KpGame *g, int64_t p, int64_t r)
{
	int count, k, found = 0;
	int64_t *units = kp_region_units(g, r, &count);
	for (k = 0; k < count && !found; k++) {
		int64_t n = units[k];
		if (g->u.owner[n] == p && kp_band(KP_DAT(g, units, special_ability_flags, g->u.type[n]), 9) != 0) found = 1;
	}
	kp_query_release(g, units);
	return found;
}

#define FAR_AWAY 9999999

static int base_region(KpGame *g, int64_t p, int64_t r)
{
	int64_t state = g->air.state[p][r];
	return (state == KP_RSTATE_WATCH || state == KP_RSTATE_BASE) && region_has_own_base(g, p, r);
}

/* AiTryCloak 0x4b0210: an uncloaked unit cloaks when it may. */
void kp_ai_try_cloak(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t tech;
	if (kp_band(u->status[n], 0x100) != 0) return;
	tech = kp_cloak_tech(g, n);
	if (tech != KP_TECH_NONE && kp_can_use_tech(g, tech, u->owner[n], n) == 1) kp_try_start_cloak(g, n);
}

/* ReturnToNearestBase 0x4afd40: an AI unit away from its bases moves to the nearest one (a military unit joins its
 * region). */
int kp_ai_return_to_base(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t p = u->owner[n], count = (int64_t)g->regions.count, r, bx = -1, by = -1, best = FAR_AWAY, order;
	int32_t s;
	if (u->ai[n] == 0 || base_region(g, p, kp_unit_region(g, n))) return 0;
	s = kp_unit_sprite(g, n);
	for (r = 1; r < count; r++) {
		int64_t cx, cy, d;
		if (!base_region(g, p, r)) continue;
		kp_ai_region_center(g, r, &cx, &cy);
		d = kp_approx_distance(cx - kp_s16v(g->w->spr.x[s]), cy - kp_s16v(g->w->spr.y[s]));
		if (kp_u32v(d) < kp_u32v(best)) {
			best = d;
			bx = cx;
			by = cy;
		}
	}
	if (bx == -1) return 0;
	if (ai_type(g, n) == KP_AI_MILITARY) { /* RemoveUnitAi 0x48dbc0, AddMilitaryAi 0x4ae760 */
		kp_remove_unit_ai(g, n, 0);
		kp_ai_military_add(g, p, kp_band(kp_region_at(g, bx, by), 0xffff), n, 1);
	}
	order = u->type[n] == KP_TYPE_MEDIC ? KP_ORD_HEAL_MOVE : KP_ORD_MOVE;
	if (u->order[n] == KP_ORD_UNLOAD || u->order[n] == KP_ORD_MOVE_UNLOAD) order = KP_ORD_MOVE_UNLOAD;
	kp_issue_order_at(g, n, order, bx, by);
	return 1;
}

#define BASE_NEED_FACTOR 6 /* a campaign AI's main building region needs default_min times this */
#define BASE_NEED_GROUND 1500
#define BASE_NEED_AIR 1000
#define BASE_RETRY 45

/* AssignBuildingRegionState 0x4b00a0: the region of player p's town unit n becomes a base region (state 5, flag 0x01,
 * 0x40 for a non-campaign AI; the non-campaign players allied to p take their idle record of it to state 4); a main
 * building (completed or morphing, grounded, a resource depot, or the race's main type) makes it state 6 with its
 * needs (default_min * 6 each for a campaign AI, else 1500 ground and 1000 air) and retry 45. */
void kp_ai_building_region_state(KpGame *g, int64_t p, int64_t n)
{
	KpAiRegions *a = &g->air;
	KpUnits *u = &g->u;
	int64_t r = kp_unit_region(g, n), t = u->type[n], q;
	int main;
	if (a->state[p][r] == KP_RSTATE_IDLE || a->state[p][r] == KP_RSTATE_WATCH) {
		kp_ai_set_region_state(g, p, r, KP_RSTATE_BASE);
		a->flags[p][r] = kp_bor(a->flags[p][r], KP_RFLAG_GUARDED);
		if (!kp_ai_campaign(g, p)) a->flags[p][r] = kp_bor(a->flags[p][r], KP_RFLAG_NO_DECAY);
		for (q = 0; q < KP_AI_PLAYERS; q++) {
			if (!kp_ai_campaign(g, q) && g->players.alliance[q][p] != 0 && a->state[q][r] == KP_RSTATE_IDLE) {
				kp_ai_set_region_state(g, q, r, KP_RSTATE_WATCH);
			}
		}
	}
	main = (kp_band(u->status[n], STATUS_COMPLETED) != 0 || t == KP_TYPE_HIVE || t == KP_TYPE_LAIR
			|| (t == KP_TYPE_HATCHERY && kp_is_morphing_zerg_building(g, n)))
		&& kp_band(u->status[n], STATUS_GROUNDED) != 0
		&& kp_band(KP_DAT(g, units, special_ability_flags, t), DAT_RESOURCE_DEPOT) != 0;
	if (!main && t != kp_ai_race_main_building(g, p)) return;
	if (kp_ai_campaign(g, p)) {
		a->need_ground[p][r] = kp_mod(g->pai.default_min[p] * BASE_NEED_FACTOR, 65536);
		a->need_air[p][r] = kp_mod(g->pai.default_min[p] * BASE_NEED_FACTOR, 65536);
		kp_ai_set_region_state(g, p, r, KP_RSTATE_DEFEND);
	} else {
		kp_ai_set_region_state(g, p, r, KP_RSTATE_DEFEND);
		a->flags[p][r] = kp_bor(a->flags[p][r], KP_RFLAG_NO_DECAY);
		a->need_ground[p][r] = BASE_NEED_GROUND;
		a->need_air[p][r] = BASE_NEED_AIR;
	}
	a->retry[p][r] = BASE_RETRY;
}

/* SetRegionState 0x4ab900. */
void kp_ai_set_region_state(KpGame *g, int64_t p, int64_t r, int64_t state)
{
	KpAiRegions *a = &g->air;
	int campaign = kp_ai_campaign(g, p);
	if (r == 0) return;
	if (a->state[p][r] == KP_RSTATE_IDLE) {
		int inaccessible = g->pathing.rg.klass[r] == KP_REGION_UNWALKABLE;
		int64_t m = campaign ? g->pai.default_min[p] : 0;
		a->need_ground[p][r] = inaccessible ? 0 : m;
		a->need_air[p][r] = inaccessible ? m : 0;
		a->local_ground[p][r] = a->local_air[p][r] = a->all_ground[p][r] = a->all_air[p][r] = 0;
		a->target_air[p][r] = a->target_ground[p][r] = a->slowest[p][r] = a->detector[p][r] = 0;
		a->enemy_air[p][r] = a->enemy_ground[p][r] = 0;
		if (state != KP_RSTATE_HELP) a->dest[p][r] = 0;
	}
	a->flags[p][r] = kp_band(a->flags[p][r], kp_bnot(UNDER_ATTACK));
	a->state[p][r] = kp_mod(state, 256);
	switch (state) {
	case KP_RSTATE_IDLE:
		a->priority[p][r] = kp_band(a->flags[p][r], KP_RFLAG_NO_DECAY) != 0 && !campaign ? 40 : 10;
		break;
	case KP_RSTATE_MOVE: case KP_RSTATE_GROUP: a->priority[p][r] = campaign ? 70 : 160; break;
	case KP_RSTATE_ABANDONED:
		a->flags[p][r] = kp_band(a->flags[p][r], 0xfe);
		a->priority[p][r] = 10;
		break;
	case KP_RSTATE_WATCH: a->priority[p][r] = 40; break;
	case KP_RSTATE_BASE: a->priority[p][r] = 50; break;
	case KP_RSTATE_DEFEND: a->priority[p][r] = region_has_own_base(g, p, r) ? 110 : 100; break;
	case KP_RSTATE_HELP:
		a->wait[p][r] = 0;
		if (region_has_own_base(g, p, a->dest[p][r])) a->priority[p][r] = 110;
		else a->priority[p][r] = region_has_own_base(g, p, r) ? 110 : 100;
		break;
	case KP_RSTATE_GATHER: case KP_RSTATE_ATTACK: a->priority[p][r] = campaign ? 50 : 160; break;
	default: a->priority[p][r] = 10; break;
	}
}

/* SumEnemyStrengthInRegion 0x4a9330: the strength of player p's enemies in region r, flying or not. */
static int64_t sum_enemy_strength(KpGame *g, int64_t p, int64_t r, int flying)
{
	KpUnits *u = &g->u;
	int count, k;
	int64_t *units = kp_region_units(g, r, &count), sum = 0;
	for (k = 0; k < count; k++) {
		int64_t n = units[k], t = u->type[n], air = 0, ground;
		if ((kp_band(u->status[n], STATUS_AIR) != 0) != (flying != 0) || !kp_player_enemy_of(g, p, n)) continue;
		if (!(t == KP_TYPE_LARVA || t == KP_TYPE_EGG || t == KP_TYPE_COCOON || t == KP_TYPE_LURKER_EGG)
			&& !(kp_band(u->status[n], STATUS_HALLUCINATION) != 0 && kp_current_life(g, n) < kp_max_life(g, n))) {
			int64_t max = kp_max_hit_points(g, n) + (KP_DAT(g, units, shield_enabled, t) != 0 ? KP_DAT(g, units, shield_amount, t) : 0);
			if (kp_u32v(max) == 0) max = 1;
			air = kp_udiv64(g, kp_mulv(kp_current_life(g, n), kp_unit_type_strength(g, n, 0)), kp_u32v(max));
		}
		ground = kp_unit_current_strength(g, n, 1);
		sum = kp_u32v(sum + (air > ground ? air : ground));
	}
	kp_query_release(g, units);
	return sum;
}

/* CallNearbyUnitsForHelp 0x4aa660. */
static void call_nearby_for_help(KpGame *g, int64_t n, int64_t attacker, int attacking)
{
	KpUnits *u = &g->u;
	int campaign = kp_ai_campaign(g, u->owner[n]);
	int64_t r, x, y, *found;
	int32_t s = kp_unit_sprite(g, n);
	int count, k;
	if (kp_band(KP_DAT(g, units, special_ability_flags, u->type[n]), 1) != 0) {
		r = campaign ? CAMPAIGN_HELP_RANGE_BUILDING : HELP_RANGE_BUILDING;
	} else {
		r = campaign ? CAMPAIGN_HELP_RANGE : HELP_RANGE;
	}
	x = g->w->spr.x[s];
	y = g->w->spr.y[s];
	found = kp_query_rect(g, kp_s16v(x - r), kp_s16v(y - r), kp_s16v(x + r), kp_s16v(y + r), &count);
	for (k = 0; k < count; k++) {
		int64_t h = found[k], gk;
		if (h == n || u->owner[h] != u->owner[n]) continue;
		if (kp_band(KP_DAT(g, units, special_ability_flags, u->type[h]), DAT_WORKER) != 0
			&& kp_band(KP_DAT(g, units, special_ability_flags, u->type[attacker]), DAT_WORKER) == 0) {
			continue; /* the Zergling branch behind AreDefenseBuildListsEmpty 0x4832f0 is unreachable */
		}
		if (u->type[h] == KP_TYPE_ARBITER) continue;
		if (!kp_can_attack(g, h, attacker, 1)
			&& (!kp_can_attack(g, attacker, h, 1) || kp_band(u->status[h], STATUS_BURROWED) != 0)) {
			continue;
		}
		if (attacking) { /* an attacking group's helpers are its attackers, which then also help as usual */
			if (!kp_ai_in_attack_region(g, h, 0)) continue;
			kp_ai_react_to_attack(g, h, attacker, 0, 1);
		}
		gk = guard_of(g, h);
		if (gk < 0 || g->guards.kind[gk] != KP_AI_GUARD) {
			kp_ai_react_to_attack(g, h, attacker, 0, 1);
		} else {
			int32_t as = kp_unit_sprite(g, attacker);
			int64_t d = kp_approx_distance(kp_s16v(g->guards.x1[gk]) - kp_s16v(g->w->spr.x[as]),
				kp_s16v(g->guards.y1[gk]) - kp_s16v(g->w->spr.y[as]));
			int64_t w = kp_band(KP_DAT(g, units, special_ability_flags, u->type[u->subunit[h]]), 0x10) != 0 && u->subunit[h] != 0
				? u->subunit[h] : h, weapon;
			weapon = kp_band(u->status[h], STATUS_AIR) != 0 ? KP_DAT(g, units, air_weapon, u->type[w]) : kp_ground_weapon(g, w);
			if (weapon != KP_WEAPON_NONE && kp_u32v(d) <= kp_u32v(kp_weapon_max_range(g, w, weapon) + GUARD_RANGE)) {
				kp_ai_react_to_attack(g, h, attacker, 0, 1);
			}
		}
	}
	kp_query_release(g, found);
}

/* The callback 0x432250 of FindNearestEnemyOfType for the Arbiter: an enemy of unit n of that type. */
static int enemy_arbiter(KpGame *g, int64_t c, int64_t n) { return g->u.type[c] == KP_TYPE_ARBITER && kp_is_enemy(g, n, c); }

/* AiReactToAttack 0x4aebc0: a computer's unit n attacked by a (direct: real damage; propagated: called for a
 * neighbour's help). */
void kp_ai_react_to_attack(KpGame *g, int64_t n, int64_t a, int direct, int propagated)
{
	KpUnits *u = &g->u;
	int64_t p = u->owner[n], t, vr, ar, order, type;
	int locked, attacking = 0, quiet = 0;
	if (kp_u32v(p) >= 8 || kp_controller(g, p) != KP_CTRL_COMPUTER) return;
	if (kp_band(KP_DAT(g, units, special_ability_flags, u->type[n]), DAT_BUILDING) != 0 && u->ai[n] != 0) {
		g->pai.timed_request[p] = kp_u32v(kp_scalar(g, KP_ELAPSED)); /* StampRequestTime 0x4835a0 */
		kp_ai_town_attacked(g, n); /* MarkUnitTownAttacked 0x487b80 */
		/* an unfinished building below a quarter of its life is cancelled (CancelConstruction 0x41a340) */
		if (direct && a != 0 && kp_band(KP_DAT(g, units, special_ability_flags, u->type[a]), DAT_WORKER) == 0
			&& !kp_unit_inactive(g, n) && kp_band(u->status[n], 1) == 0
			&& kp_current_life(g, n) < kp_s32v(kp_max_life(g, n)) / 4) {
			kp_cancel_construction(g, n);
			return;
		}
	}
	/* ResolveAttackerForHelp 0x4b1380 */
	t = 0;
	if (a != 0 && n != 0) {
		t = a;
		if (u->type[a] == KP_TYPE_INTERCEPTOR && u->union_0xc0[a] != 0
			&& (kp_target_in_range(g, n, u->union_0xc0[a]) || kp_band(u->status[n], STATUS_CAN_MOVE) != 0)) {
			t = u->union_0xc0[a];
		}
	}
	if (t == 0) kp_game_fail(g, "ai: a reaction without an attacker (0x4aebc0)");
	if (kp_band(u->status[t], TERRAN_FLAG_IN_BUNKER) != 0) {
		t = u->connected[t];
		if (t == 0) return;
	}
	if (kp_band(u->status[t], STATUS_FREE_CLOAK) != 0 && kp_band(u->status[t], STATUS_BURROWED) == 0) {
		t = kp_nearest_unit_map(g, n, enemy_arbiter, n); /* FindNearestEnemyOfType 0x431ed0 */
		if (t == 0) return;
	}
	if (direct) {
		kp_ai_try_cloak(g, n);
		if (u->type[n] == KP_TYPE_LURKER && kp_band(u->status[n], STATUS_BURROWED) == 0 && u->order[n] != KP_ORD_BURROWING) {
			kp_issue_order(g, n, KP_ORD_BURROWING);
		}
	}
	if (u->ai[n] != 0) {
		if (ai_type(g, n) == KP_AI_MILITARY) {
			int64_t mp, mr;
			kp_ai_military_record(g, n, &mp, &mr);
			/* a moving group attacked starts to group (0x4b1a30) */
			if (g->air.state[mp][mr] == KP_RSTATE_MOVE && kp_ai_mobile_military(g, n)) kp_ai_set_region_state(g, mp, mr, KP_RSTATE_GROUP);
			attacking = kp_ai_in_attack_region(g, n, 0);
		}
		if (direct && kp_is_transport(g, n) && kp_band(u->status[n], STATUS_CAN_MOVE) != 0) {
			if (kp_has_cargo(g, n)) { /* HasCargo 0x438260: it unloads */
				if (u->order[n] != KP_ORD_UNLOAD) kp_issue_order(g, n, KP_ORD_UNLOAD);
				return;
			}
			if (!kp_ai_guard_return_home(g, n, 0) && kp_ai_return_to_base(g, n)) return;
			quiet = kp_ai_campaign(g, p);
		}
	}
	vr = kp_unit_region(g, n);
	if (!quiet) {
		KpAiRegions *rec = &g->air;
		int react;
		if (!propagated) call_nearby_for_help(g, n, t, attacking);
		if (attacking) goto marked;
		if (u->ai[n] == 0) react = 0;
		else react = !(ai_type(g, n) == KP_AI_GUARD && kp_ai_campaign(g, p));
		ar = kp_unit_region(g, t);
		if (react) {
			if (rec->state[p][vr] == KP_RSTATE_BASE || rec->state[p][vr] == KP_RSTATE_WATCH) kp_ai_set_region_state(g, p, vr, KP_RSTATE_DEFEND);
			if (rec->state[p][vr] != KP_RSTATE_IDLE && rec->state[p][ar] == KP_RSTATE_IDLE && kp_scalar(g, KP_ELAPSED) > DEFENSE_SECONDS) {
				kp_ai_set_region_state(g, p, ar, KP_RSTATE_BASE);
			}
		}
		if (!propagated) {
			rec->enemy_air[p][ar] = kp_mod(sum_enemy_strength(g, p, ar, 1), 65536);
			rec->enemy_ground[p][ar] = kp_mod(sum_enemy_strength(g, p, ar, 0), 65536);
			if (rec->need_air[p][ar] > rec->need_air[p][vr]) rec->need_air[p][vr] = rec->need_air[p][ar];
			if (rec->need_ground[p][ar] > rec->need_ground[p][vr]) rec->need_ground[p][vr] = rec->need_ground[p][ar];
		}
	}
marked:
	g->air.flags[p][vr] = kp_bor(g->air.flags[p][vr], UNDER_ATTACK);
	if (kp_band(u->status[n], 1) == 0) return;
	order = u->order[n];
	if (order == KP_ORD_ARCHON_SUMMON || order == KP_ORD_RESET_COLLISION || order == KP_ORD_CONSTRUCTING) return;
	if (!direct && kp_ai_spell_cast_order(order)) return;
	type = u->type[n];
	if (type == KP_TYPE_MARINE && order == KP_ORD_ENTER_TRANSPORT) return;
	locked = kp_band(u->status[n], 0x3000) != 0; /* IsOrderLocked 0x429aa0 */
	if (!locked && kp_ai_reaction_spell(g, n, direct)) return;
	if (order == KP_ORD_RECHARGE_SHIELDS_UNIT || order == KP_ORD_MOVE) return;
	if (direct) {
		/* the retreats of ReturnToNearestBase 0x4afd40: a Carrier short of Interceptors; a non-campaign AI's damaged
		 * flyer facing an immobile attacker */
		if (type == KP_TYPE_CARRIER && kp_mod(kp_band(u->union_0xc8[n], 0xff) + kp_band(kp_shrv(u->union_0xc8[n], 8), 0xff), 256) <= 2
			&& kp_ai_return_to_base(g, n)) {
			return;
		}
		if (!kp_ai_campaign(g, p) && kp_band(u->status[t], STATUS_CAN_MOVE) == 0) {
			int64_t life = kp_current_life(g, n), max = kp_s32v(kp_max_life(g, n));
			if (life <= max * 3 / 4 && (type == KP_TYPE_ARBITER || type == KP_TYPE_WRAITH || type == KP_TYPE_MUTALISK) && kp_ai_return_to_base(g, n)) {
				return;
			}
			if (life <= max * 2 / 3 && (type == KP_TYPE_BATTLECRUISER || type == KP_TYPE_CARRIER || type == KP_TYPE_SCOUT) && kp_ai_return_to_base(g, n)) {
				return;
			}
		}
		if (type == KP_TYPE_LURKER && kp_band(u->status[n], STATUS_BURROWED) == 0 && order != KP_ORD_BURROWING
			&& order != KP_ORD_UNBURROWING && try_evade(g, n, t)) {
			return;
		}
		if (!locked && kp_band(u->status[n], STATUS_CAN_MOVE) != 0) {
			int32_t ts = kp_unit_sprite(g, t);
			if (!kp_placement_reachable(g, n, kp_s16v(g->w->spr.x[ts]), kp_s16v(g->w->spr.y[ts])) && try_evade(g, n, t)) return;
		}
	}
	if (u->order_unit[n] == t) return;
	if (kp_band(KP_DAT(g, units, special_ability_flags, type), DAT_WORKER) != 0
		&& kp_band(KP_DAT(g, units, special_ability_flags, u->type[t]), DAT_WORKER) == 0 && u->type[t] != KP_TYPE_ZERGLING) {
		if (direct) try_evade(g, n, t);
		return;
	}
	if (direct && u->ai[n] != 0 && kp_is_undetected_by(g, t, n)) kp_ai_request_detection(g, n, t);
	if (locked) return;
	if (kp_can_attack(g, n, t, 1)) {
		u->auto_target[n] = t;
		if (u->subunit[n] != 0 && kp_band(KP_DAT(g, units, special_ability_flags, u->type[u->subunit[n]]), 0x10) != 0) {
			u->auto_target[u->subunit[n]] = t;
		}
		kp_ai_update_attack_target(g, n, 0, 1, 0);
		return;
	}
	if (u->ai[n] == 0) return;
	if (order == KP_ORD_AI_PATROL) {
		int64_t x, y;
		int32_t s = kp_unit_sprite(g, n);
		kp_flee_position(g, t, n, &x, &y);
		if (x != kp_s16v(g->w->spr.x[s]) || y != kp_s16v(g->w->spr.y[s])) {
			kp_issue_order_packed(g, n, KP_ORD_MOVE, x, y, 0);
			return;
		}
	}
	if (!direct || type == KP_TYPE_MEDIC) return;
	if (u->order_unit[n] != 0 && kp_can_attack(g, u->order_unit[n], u->order_unit[n], 1)) return;
	try_evade(g, n, t);
}

/* IsSpellCastOrder 0x4adb40. */
int kp_ai_spell_cast_order(int64_t order)
{
	switch (order) {
	case KP_ORD_FIRE_YAMATO_GUN: case KP_ORD_CAST_LOCKDOWN: case KP_ORD_CAST_DARK_SWARM:
	case KP_ORD_CAST_SPAWN_BROODLINGS: case KP_ORD_CAST_EMP_SHOCKWAVE: case KP_ORD_CAST_PSIONIC_STORM:
	case KP_ORD_CAST_IRRADIATE: case KP_ORD_CAST_PLAGUE: case KP_ORD_CAST_ENSNARE:
	case KP_ORD_CAST_STASIS_FIELD: return 1;
	default: return 0;
	}
}

/* The AI record type byte (+8) of unit n: 1 guard, 2 worker, 3 building, 4 military; 0 without one. */
int64_t kp_ai_unit_record_type(KpGame *g, int64_t n) { return ai_type(g, n); }

/* The home of a unit with a guard record (+0x12/+0x14); 0 for any other record. */
int kp_ai_guard_home(KpGame *g, int64_t n, int64_t *x, int64_t *y)
{
	int64_t k = guard_of(g, n);
	if (k < 0 || g->guards.kind[k] != KP_AI_GUARD) return 0;
	*x = kp_s16v(g->guards.x1[k]);
	*y = kp_s16v(g->guards.y1[k]);
	return 1;
}


/* MarkRosterUnitLost 0x4839a0: the first roster entry of the type (a sieged tank's as a tank) is lost. */
static void roster_lost(KpGame *g, int64_t p, int64_t t)
{
	int k;
	if (t == KP_TYPE_SIEGE_TANK_SIEGED) t = KP_TYPE_SIEGE_TANK;
	for (k = 0; k < KP_AI_ROSTER_UNITS; k++) {
		if (g->pai.roster[p][k] == t + 1) {
			g->pai.roster[p][k] = KP_AI_ROSTER_LOST;
			return;
		}
	}
}

/* RemoveUnitFromAiRegions 0x4b0770's military record: an attacking unit is lost from the roster; a killed unit of a
 * holding region away from it, where the player has no base, abandons the region; the record goes back to the pool. */
static void military_leave(KpGame *g, int64_t n, int64_t killed)
{
	int64_t p, r, k = kp_ai_military_record(g, n, &p, &r), state;
	if (k < 0) return;
	state = g->air.state[p][r];
	if (state == KP_RSTATE_GATHER) {
		roster_lost(g, g->u.owner[n], g->u.type[n]);
	} else if (killed && state != KP_RSTATE_IDLE && !kp_ai_in_attack_region(g, n, 0)
		&& kp_band(kp_unit_region(g, n), 0xffff) != r && !region_has_own_base(g, p, r)) {
		kp_ai_set_region_state(g, p, r, KP_RSTATE_ABANDONED);
	}
	kp_ai_military_release(g, k); /* ReleaseMilitaryNode 0x4a9a30 */
}

/* RemoveUnitFromAiRegions 0x4b0770: the computer players forget the unit (targets, slowest, detector) and its
 * strength in their needs; a military record leaves its region. */
void kp_ai_remove_from_regions(KpGame *g, int64_t n, int64_t killed)
{
	KpUnits *u = &g->u;
	KpAiRegions *a = &g->air;
	int64_t count = (int64_t)g->regions.count, r;
	int p;
	for (p = 0; p < KP_AI_PLAYERS; p++) {
		if (kp_controller(g, p) != KP_CTRL_COMPUTER) continue;
		for (r = 1; r < count; r++) {
			if (a->target_air[p][r] == n) a->target_air[p][r] = 0;
			if (a->target_ground[p][r] == n) a->target_ground[p][r] = 0;
			if (a->slowest[p][r] == n) a->slowest[p][r] = 0;
			if (a->detector[p][r] == n) a->detector[p][r] = 0;
		}
		if (kp_player_enemy_of(g, p, n)) {
			int64_t ur = kp_unit_region(g, n), v;
			v = g->strength[0][u->type[n]];
			a->need_air[p][ur] = v >= a->need_air[p][ur] ? 0 : a->need_air[p][ur] - v;
			v = g->strength[1][u->type[n]];
			a->need_ground[p][ur] = v >= a->need_ground[p][ur] ? 0 : a->need_ground[p][ur] - v;
		}
	}
	military_leave(g, n, killed);
}

/* DetachGuardOnDeath 0x4b26f0: the guard record loses its unit and counts a death (up to 100). */
void kp_ai_detach_guard(KpGame *g, int64_t n)
{
	int64_t k = guard_of(g, n);
	if (k < 0 || g->guards.kind[k] != KP_AI_GUARD) return;
	g->guards.unit[k] = 0;
	if (g->guards.deaths[k] < 100) g->guards.deaths[k]++;
}

/* --- AIPatrol (order 159, docs/research/atlas/p38.md) --- */

#define PATROL_PERIOD 15
#define PATROL_REACH 352
#define PATROL_RANDOM 0x37
#define PATROL_FLAG 0x100 /* PlayerAi +0x218 (0x482f20) */
#define PATROL_LIST 16
#define REGION_PATROLLED 0x04
#define PATROL_FAR 99999999 /* 0x5f5e0ff */

/* A region state a patrol claims or keeps (tables 0x4ae028, 0x4b0bc0). */
static int patrol_state(int64_t state)
{
	return state == KP_RSTATE_MOVE || state == KP_RSTATE_GROUP || state == KP_RSTATE_HELP || state == KP_RSTATE_GATHER || state == KP_RSTATE_ATTACK;
}

static int defended(int64_t state) { return state == KP_RSTATE_WATCH || state == KP_RSTATE_BASE; }

/* ContinueRegionPatrol 0x4b0af0: the region n patrols keeps it near its center, or lets it go. */
static int continue_patrol(KpGame *g, int64_t n)
{
	KpAiRegions *a = &g->air;
	int64_t p = g->u.owner[n], count = (int64_t)g->regions.count, r, x, y;
	int32_t s;
	for (r = 1; r < count; r++) {
		if (a->detector[p][r] != n) continue;
		if (patrol_state(a->state[p][r])) {
			kp_ai_region_center(g, r, &x, &y);
			if (kp_within_distance(g, n, PATROL_REACH, x, y)) kp_stop_and_face(g, n);
			else kp_request_move_to_point(g, n, x, y);
			return 1;
		}
		a->detector[p][r] = 0;
		s = kp_unit_sprite(g, n);
		kp_request_move_to_point(g, n, kp_s16v(g->w->spr.x[s]), kp_s16v(g->w->spr.y[s]));
		return 0;
	}
	return 0;
}

/* ClaimPatrolRegion 0x4adf80: the first unpatrolled region with military and a patrol state. */
static int claim_patrol(KpGame *g, int64_t n)
{
	KpAiRegions *a = &g->air;
	int64_t p = g->u.owner[n], count = (int64_t)g->regions.count, r, x, y;
	if (kp_band(g->pai.flags[p], PATROL_FLAG) == 0) return 0;
	for (r = 1; r < count; r++) {
		if (a->military[p][r] == 0 || !patrol_state(a->state[p][r]) || a->detector[p][r] != 0) continue;
		a->detector[p][r] = n;
		kp_ai_region_center(g, r, &x, &y);
		kp_request_move_to_point(g, n, x, y);
		return 1;
	}
	return 0;
}

/* PickPatrolPoint 0x4afb20: the center of a random defended neighbour (guarded and not yet patrolled first), else of
 * the nearest defended region, else of n's own region; the region is marked patrolled. */
void kp_ai_patrol_point(KpGame *g, int64_t n, int64_t *x, int64_t *y)
{
	KpAiRegions *a = &g->air;
	int64_t p = g->u.owner[n], r = kp_unit_region(g, n), lists[4][PATROL_LIST], at, k, nb, chosen = r;
	int counts[4] = { 0, 0, 0, 0 }, i;
	int64_t total = kp_region_all_neighbors(g, r, &at);
	for (k = 0; k < total; k++) {
		int64_t f;
		nb = g->pathing.rg.neighbors[at + k];
		if (!defended(a->state[p][nb])) continue;
		f = a->flags[p][nb];
		if (kp_band(f, KP_RFLAG_GUARDED) != 0) i = kp_band(f, REGION_PATROLLED) != 0 ? 1 : 0;
		else i = kp_band(f, REGION_PATROLLED) != 0 ? 3 : 2;
		if (counts[i] != PATROL_LIST) lists[i][counts[i]++] = nb;
	}
	for (i = 0; i < 4; i++) {
		if (counts[i] == 0) continue;
		chosen = lists[i][kp_rng_game_raw(g->rng, PATROL_RANDOM) % (uint32_t)counts[i]];
		break;
	}
	if (!defended(a->state[p][chosen])) {
		int64_t count = (int64_t)g->regions.count, best = PATROL_FAR, cx, cy, d;
		int32_t s = kp_unit_sprite(g, n);
		for (k = 1; k < count; k++) {
			if (!defended(a->state[p][k])) continue;
			kp_ai_region_center(g, k, &cx, &cy);
			d = kp_approx_distance(cx - kp_s16v(g->w->spr.x[s]), cy - kp_s16v(g->w->spr.y[s]));
			if (d < best) {
				best = d;
				chosen = k;
			}
		}
	}
	kp_ai_region_center(g, chosen, x, y);
	a->flags[p][chosen] = kp_bor(a->flags[p][chosen], REGION_PATROLLED);
}

/* Order_AIPatrol 0x48cee0 (order 159): a computer's patrolling unit (an Observer) watches its regions. */
void kp_order_ai_patrol(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t x, y;
	if (u->order_timer[n] != 0) return;
	u->order_timer[n] = PATROL_PERIOD;
	switch (u->order_state[n]) {
	case 0:
		if (u->order_unit[n] != 0) {
			int32_t s = kp_unit_sprite(g, u->order_unit[n]);
			u->order_x[n] = kp_s16v(g->w->spr.x[s]);
			u->order_y[n] = kp_s16v(g->w->spr.y[s]);
			kp_request_move_to_point(g, n, u->order_x[n], u->order_y[n]);
			return;
		}
		u->order_state[n] = continue_patrol(g, n) || claim_patrol(g, n) ? 1 : 2;
		return;
	case 1:
		if (!continue_patrol(g, n)) u->order_state[n] = 0;
		return;
	case 2:
		if (claim_patrol(g, n)) {
			u->order_state[n] = 1;
			return;
		}
		if (kp_move_arrival(g, n) == 0) return;
		kp_ai_patrol_point(g, n, &x, &y);
		kp_request_move_to_point(g, n, x, y);
		return;
	default:
		return;
	}
}

/* RequestDetectionOrCounter 0x4ae3e0: computer unit n facing undetected enemy e calls its region's patrol, a Comsat's
 * Scanner Sweep, else the nearest idle detector and the nearest Queen that can ensnare e. */
void kp_ai_request_detection(KpGame *g, int64_t n, int64_t e)
{
	KpUnits *u = &g->u;
	int64_t p = u->owner[n], k, m, detector = 0, queen = 0, best = DETECTOR_FAR, best_queen = DETECTOR_FAR, d;
	int32_t s, ms;
	if (u->ai[n] == 0) return;
	g->pai.flags[p] = kp_bor(g->pai.flags[p], PATROL_FLAG); /* 0x4832d0 */
	k = guard_of(g, n);
	if (ai_type(g, n) == KP_AI_MILITARY) { /* the detector its military region has */
		int64_t mp, mr;
		kp_ai_military_record(g, n, &mp, &mr);
		if (g->air.detector[mp][mr] != 0) {
			kp_issue_order_target(g, g->air.detector[mp][mr], KP_ORD_AI_PATROL, n);
			return;
		}
	} else if (k >= 0 && g->guards.kind[k] == KP_AI_GUARD) {
		int64_t claimed = g->air.detector[p][kp_region_at(g, kp_s16v(g->guards.x2[k]), kp_s16v(g->guards.y2[k]))];
		if (claimed != 0) {
			kp_issue_order_target(g, claimed, KP_ORD_AI_PATROL, n);
			return;
		}
	}
	s = kp_unit_sprite(g, n);
	for (m = g->player_units[p]; m != 0; m = u->player_next[m]) {
		int64_t t = u->type[m], o = u->order[m];
		if (u->sprite[m] == KP_NONE || o == KP_ORD_DIE) continue; /* IsUnitInactive 0x41fe40 */
		ms = kp_unit_sprite(g, m);
		d = kp_approx_distance(kp_s16v(g->w->spr.x[s]) - kp_s16v(g->w->spr.x[ms]), kp_s16v(g->w->spr.y[s]) - kp_s16v(g->w->spr.y[ms]));
		if (t == KP_TYPE_SCIENCE_VESSEL || t == KP_TYPE_OBSERVER || (t == KP_TYPE_OVERLORD && !kp_has_cargo(g, m))) {
			if (u->blind[m] != 0) continue;
			if (o != KP_ORD_COMPUTER_AI && o != KP_ORD_COMPUTER_RETURN && o != KP_ORD_AI_GUARD && o != KP_ORD_CAST_EMP_SHOCKWAVE && o != KP_ORD_AI_PATROL) continue;
			if (d < best) {
				best = d;
				detector = m;
			}
		} else if (t == KP_TYPE_QUEEN) {
			if (!kp_can_attack(g, n, e, 1) || u->type[e] == KP_TYPE_LURKER || kp_needs_energy(g, m, kp_order_energy(g, KP_ORD_CAST_ENSNARE))
				|| kp_can_use_tech(g, KP_TECH_ENSNARE, u->owner[m], m) != 1) {
				continue;
			}
			if (d < best_queen) {
				best_queen = d;
				queen = m;
			}
		} else if (t == KP_TYPE_COMSAT) {
			if (kp_band(u->status[m], STATUS_COMPLETED) == 0) continue;
			if (!(u->type[n] == KP_TYPE_BUNKER && kp_has_cargo(g, n))
				&& (!kp_unit_has_weapon(g, n) || kp_band(KP_DAT(g, units, special_ability_flags, u->type[n]), DAT_WORKER) != 0)) {
				continue;
			}
			if (kp_needs_energy(g, m, kp_tech_energy(g, KP_TECH_SCANNER_SWEEP)) || o == KP_ORD_CAST_SCANNER_SWEEP) continue;
			kp_issue_order_target(g, m, KP_ORD_CAST_SCANNER_SWEEP, e);
			return;
		}
	}
	if (detector != 0) kp_issue_order_at(g, detector, KP_ORD_COMPUTER_RETURN, kp_s16v(g->w->spr.x[s]), kp_s16v(g->w->spr.y[s]));
	if (queen != 0) {
		int32_t es = kp_unit_sprite(g, e);
		kp_issue_order_at(g, queen, KP_ORD_CAST_ENSNARE, kp_s16v(g->w->spr.x[es]), kp_s16v(g->w->spr.y[es]));
	}
}

#define CAPTAIN_SMALL_REGION 10 /* the tiles of a region the second ring of capt_expand passes through */

/* CaptainExpand 0x4ad310 (capt_expand): the idle regions next to those player p holds (state above 3) are defended
 * (state 4, guarded; with the campaign's no-decay flag), then once more through regions of at most 10 tiles. */
void kp_ai_captain_expand(KpGame *g, int64_t p)
{
	KpAiRegions *a = &g->air;
	int64_t count = (int64_t)g->regions.count, r;
	int campaign = kp_ai_campaign(g, p), small;
	uint8_t marked[KP_AI_REGIONS];
	for (small = 0; small <= 1; small++) {
		memset(marked, 0, sizeof marked);
		for (r = 1; r < count; r++) {
			int64_t at, k, n;
			if (small && kp_region_tiles(g, r) > CAPTAIN_SMALL_REGION) continue;
			a->flags[p][r] = kp_band(a->flags[p][r], kp_bnot(KP_RFLAG_GUARDED));
			if (campaign) a->flags[p][r] = kp_band(a->flags[p][r], kp_bnot(KP_RFLAG_NO_DECAY));
			if (a->state[p][r] <= KP_RSTATE_ABANDONED) continue;
			n = kp_region_all_neighbors(g, r, &at); /* 0x481ad0 */
			for (k = 0; k < n; k++) marked[g->pathing.rg.neighbors[at + k]] = 1;
		}
		for (r = 1; r < count; r++) {
			if (!marked[r] || a->state[p][r] != KP_RSTATE_IDLE) continue;
			kp_ai_set_region_state(g, p, r, KP_RSTATE_WATCH);
			a->flags[p][r] = kp_bor(a->flags[p][r], KP_RFLAG_GUARDED);
			if (campaign) a->flags[p][r] = kp_bor(a->flags[p][r], KP_RFLAG_NO_DECAY);
		}
	}
}

#define DEFENSE_NEED_GROUND 1000

/* MarkDefenseRegion 0x4b0030: an idle region of player p is defended (state 4, guarded; no decay when strong or for a
 * campaign AI; a strong accessible one needs 1000 ground). */
void kp_ai_mark_defense_region(KpGame *g, int64_t p, int64_t r, int strong)
{
	KpAiRegions *a = &g->air;
	if (a->state[p][r] != KP_RSTATE_IDLE) return;
	kp_ai_set_region_state(g, p, r, KP_RSTATE_WATCH);
	a->flags[p][r] = kp_bor(a->flags[p][r], KP_RFLAG_GUARDED);
	if (!strong && !kp_ai_campaign(g, p)) return;
	a->flags[p][r] = kp_bor(a->flags[p][r], KP_RFLAG_NO_DECAY);
	if (strong && g->pathing.rg.klass[r] != KP_REGION_UNWALKABLE) a->need_ground[p][r] = DEFENSE_NEED_GROUND; /* 0x481bb0 */
}

#define NO_REGION 2500

/* ChooseUnloadDestination 0x4af810: a computer's transport that could not unload goes on to a ground neighbour of its
 * region (the last one the player holds, attacks or waits to attack; else the last idle one; else the last other),
 * else stays in its region; an unwalkable destination sends it to a base instead. */
void kp_ai_choose_unload(KpGame *g, int64_t n)
{
	const KpRegionGraph *rg = &g->pathing.rg;
	int64_t r = kp_band(kp_unit_region(g, n), 0xffff), held = NO_REGION, idle = NO_REGION, other = NO_REGION, dest, k, x, y;
	int64_t p = g->u.owner[n];
	for (k = 0; k < rg->ground[r]; k++) { /* GetRegionGroundNeighbors 0x481ab0 */
		int64_t q = rg->neighbors[rg->nb_at[r] + k], state = g->air.state[p][q];
		if (state == KP_RSTATE_IDLE) idle = q;
		else if (state == KP_RSTATE_WATCH || state == KP_RSTATE_BASE || state == KP_RSTATE_GATHER || state == KP_RSTATE_ATTACK) held = q;
		else other = q;
	}
	dest = held != NO_REGION ? held : idle != NO_REGION ? idle : other != NO_REGION ? other : r;
	if (rg->klass[dest] == KP_REGION_UNWALKABLE) { /* 0x481bb0 */
		kp_ai_return_to_base(g, n);
		return;
	}
	kp_ai_region_center(g, dest, &x, &y); /* GetRegionCenter 0x481a30 */
	kp_issue_order_at(g, n, KP_ORD_MOVE_UNLOAD, x, y);
}
