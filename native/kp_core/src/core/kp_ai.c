/* The computer players' regional AI and their units' AI orders (kp_game.h), ported from the atlas (P28, P31, P37,
 * P38) and docs/research/native-combat-ai.md, native-region-states.md. */
#include "kp_game_internal.h"
#include "kp_pathing_internal.h"

#include <string.h>

#define CAMPAIGN 0x20
#define DEFAULT_MIN 100
#define AI_STATE_START 1
#define REGION_RECENT 0x04
#define REGION_NO_DECAY 0x40
#define STATUS_AIR 0x4
#define STATUS_IN_BUILDING 0x20
#define CONTROLLER_COMPUTER 1

/* GetRegionCenter 0x481a30. */
static void region_center(KpGame *g, int64_t r, int64_t *x, int64_t *y)
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
int kp_ai_campaign(KpGame *g, int64_t p) { return kp_band(g->pai.flags[p], CAMPAIGN) != 0; }

/* InitPlayerAiData 0x4830d0, InitAiRegions 0x4b0640 (mode 0) and SetupVictoryConditions 0x448530's SetCampaignAi
 * 0x482ee0 for every player of a campaign game. */
void kp_ai_regions_init(KpGame *g, int campaign)
{
	int p;
	memset(&g->air, 0, sizeof g->air);
	memset(g->ai_reach, 0, sizeof g->ai_reach);
	memset(g->ai_reach_time, 0, sizeof g->ai_reach_time);
	memset(g->ai_reach_source, 0, sizeof g->ai_reach_source);
	memset(&g->pai, 0, sizeof g->pai);
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
	if (kp_band(a->flags[p][r], REGION_NO_DECAY) != 0) return;
	a->need_air[p][r] = decay(a->need_air[p][r], noncampaign);
	a->need_ground[p][r] = decay(a->need_ground[p][r], noncampaign);
}

/* UpdateRegionStrength 0x4adbd0. */
static void update_strength(KpGame *g, int64_t p, int64_t r)
{
	KpAiRegions *a = &g->air;
	int64_t v;
	a->local_ground[p][r] = a->local_air[p][r] = a->all_ground[p][r] = a->all_air[p][r] = 0;
	if (a->military[p][r] != 0) kp_game_fail(g, "ai: the military of a region (0x4adbd0)");
	if (kp_ai_campaign(g, p)) {
		int64_t *found, cx, cy;
		int count, k;
		region_center(g, r, &cx, &cy);
		found = kp_query_rect(g, kp_s16v(cx - 256), kp_s16v(cy - 256), kp_s16v(cx + 256), kp_s16v(cy + 256), &count);
		for (k = 0; k < count; k++) {
			/* AddOwnUnitStrengthCallback 0x4aa2c0 */
			int64_t n = found[k];
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
void kp_ai_step_region(KpGame *g, int64_t p, int64_t r)
{
	KpAiRegions *a = &g->air;
	a->flags[p][r] = kp_band(a->flags[p][r], kp_bnot(REGION_RECENT));
	if (a->retry[p][r] != 0) a->retry[p][r]--;
	switch (a->state[p][r]) {
	case 0: /* 0x4ac9b0 */
		update_strength(g, p, r);
		decay_needs(g, p, r);
		return;
	case 1: case 2: kp_game_fail(g, "ai: region state %lld (0x4ac7c0)", (long long)a->state[p][r]);
	case 3: kp_game_fail(g, "ai: region state 3 (0x4aca00)");
	case 4: case 5: kp_game_fail(g, "ai: region state %lld (0x4ac740)", (long long)a->state[p][r]);
	case 6: kp_game_fail(g, "ai: region state 6 (0x4ac690)");
	case 7: kp_game_fail(g, "ai: region state 7 (0x4acbc0)");
	case 8: case 9: kp_game_fail(g, "ai: region state %lld (0x4ac870)", (long long)a->state[p][r]);
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
		if (u->sprite[n] == KP_NONE || u->order[n] == 0 || v == 0) continue; /* IsUnitInactive 0x41fe40 */
		r2 = kp_unit_region(g, n);
		for (p = 0; p < KP_AI_PLAYERS; p++) {
			if (kp_controller(g, p) != CONTROLLER_COMPUTER || u->owner[n] == p || !kp_player_enemy_of(g, p, n)) continue;
			if (kp_band(u->status[n], STATUS_AIR) != 0) a->enemy_air[p][r2] = kp_mod(a->enemy_air[p][r2] + v, 65536);
			else a->enemy_ground[p][r2] = kp_mod(a->enemy_ground[p][r2] + v, 65536);
		}
	}
}

/* --- the military reachability cache (0x6b1aa0, docs/research/native-ai-routes.md) --- */

/* GetRegionAllNeighbors 0x481ad0: the neighbour count less its count of region 0 (the native signed byte +0x21, byte 8 of the RGNS record). */
static int64_t all_neighbors(KpGame *g, int64_t r, int64_t *at)
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
static int region_reachable(KpGame *g, int64_t p, int64_t source, int64_t target)
{
	int64_t at, k, count, t;
	if (kp_u32v(p) >= 8 || source == target) return 1;
	if (kp_region_group_of(g, target) != kp_region_group_of(g, source)) return 0;
	count = all_neighbors(g, source, &at);
	for (k = 0; k < count; k++) {
		if (g->pathing.rg.neighbors[at + k] == target) return 1;
	}
	t = g->air.target_air[p][source];
	if (t != 0 && kp_air_weapon_with_subunit(g, t) != 0x82) return 1;
	if (kp_scalar(g, KP_ELAPSED) >= kp_u32v(g->ai_reach_time[p] + 10) || g->ai_reach_source[p] != source) {
		build_reach(g, p, source);
	}
	return g->ai_reach[p][target] == 2;
}

/* IsUnitUnreachable 0x421320: a pathing unit cannot reach the target's position (another ground group, or no route
 * past the computer's held regions). */
int kp_unit_unreachable(KpGame *g, int64_t n, int64_t target)
{
	int32_t s = kp_unit_sprite(g, target);
	int64_t x = kp_s16v(g->w->spr.x[s]), y = kp_s16v(g->w->spr.y[s]);
	if (kp_band(g->u.pathing_flags[n], 1) == 0) return 0;
	if (!kp_placement_reachable(g, n, x, y)) return 1;
	/* CanUnitReachPoint 0x4ab450 */
	return !region_reachable(g, g->u.owner[n], kp_unit_region(g, n), kp_region_at(g, x, y));
}

/* --- target choice --- */

#define STATUS_CAN_MOVE 0x20000
#define STATUS_HALLUCINATION 0x40000000
#define STATUS_INVINCIBLE 0x4000000
#define STATUS_CLOAKED 0x300
#define STATUS_HALTED 0x8000
#define STATUS_TARGET_SWITCH 0x8000000
#define STATUS_UNREACHABLE 0x80
#define STATUS_INTERCEPT 0x80000
#define DAT_DETECTOR 0x8000
#define GUARD_AI 1
#define MILITARY_AI 4
#define INTERCEPTOR 0x49
#define BUNKER 0x7d
#define WRAITH 8
#define OBSERVER 0x54
#define SIEGE_TANK_SIEGED 0x1e
#define PATROL 0x98
#define ATTACK_UNIT 0x0a
#define ORDER_96 0x60
#define TANK_MODE 0x63
#define COMPUTER_AI 0x9c
#define COMPUTER_RETURN 0xa3
#define GUARD_RANGE 0xc0
#define REGION_REACH 0x120
#define DETECTOR_BONUS 1000

static int64_t head_order(KpGame *g, int64_t n) { return g->orders.order[kp_order_node(g, g->u.queue_head[n])]; }

/* The unit's guard record (its AI is a guard record of KpGuards), or -1. */
static int64_t guard_of(KpGame *g, int64_t n)
{
	int64_t ai = g->u.ai[n];
	if (ai == 0) return -1;
	if (ai < 1 || ai > KP_GUARDS) kp_game_fail(g, "ai: the AI record of unit %lld", (long long)n);
	return ai - 1;
}

/* The AI type byte (+8) of the unit's record: 1 guard, 4 military. */
static int64_t ai_type(KpGame *g, int64_t n)
{
	int64_t k = guard_of(g, n);
	if (k < 0) return 0;
	return g->guards.kind[k];
}

/* IsCritterType 0x429070. */
static int critter(KpGame *g, int64_t n)
{
	int64_t t = g->u.type[n];
	return t == 89 || t == 90 || t == 93 || t == 94 || t == 95 || t == 96;
}

/* IsWithinApproxDistance 0x417aa0: the unit's exact position within dist of the point. */
int kp_within_distance(KpGame *g, int64_t n, int64_t dist, int64_t x, int64_t y)
{
	int64_t d = kp_approx_distance(kp_shlv(x, 8) - g->u.halt_x[n], kp_shlv(y, 8) - g->u.halt_y[n]);
	return kp_u32v(dist) >= kp_shrv(d, 8);
}

/* IsActiveDetector 0x431a90. */
static int active_detector(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	if (kp_band(KP_DAT(g, units, special_ability_flags, u->type[n]), DAT_DETECTOR) == 0) return 0;
	if (kp_band(u->status[n], 1) == 0 || kp_band(u->status[n], 0x400) != 0) return 0;
	return u->lockdown_timer[n] == 0 && u->stasis_timer[n] == 0 && u->maelstrom_timer[n] == 0 && u->blind[n] == 0;
}

/* AdjustThreatByUnitType 0x4a9520. */
static int64_t adjust_threat(KpGame *g, int64_t n, int mode, int64_t v)
{
	KpUnits *u = &g->u;
	switch (u->type[n]) {
	case 1: return u->order[n] >= 123 && u->order[n] <= 129 ? 1000 : v;
	case 2: return kp_u32v(v + kp_band(u->union_0xc0[n], 0xff) * 100);
	case 11: case 42: case 57: case 69: kp_game_fail(g, "ai: the cargo of a transport target (0x438350)");
	case 30: return kp_u32v(v + kp_shrv(v, 2));
	case 34: return kp_u32v(v + g->strength[mode][0]);
	case 84: return kp_u32v(v + 50);
	default: return v;
	}
}

/* ScoreTargetThreat 0x4a9460. */
static int64_t score_target(KpGame *g, int64_t n, int mode)
{
	KpUnits *u = &g->u;
	int64_t t = u->type[n], v, life;
	if (t == 0x23 || t == 0x24 || t == 0x3b || t == 0x61) return 0;
	if (kp_band(u->status[n], STATUS_HALLUCINATION) != 0 && kp_current_life(g, n) < kp_max_life(g, n)) return 0;
	if (critter(g, n)) return 1;
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
		if (!kp_player_enemy_of(g, p, x) && !critter(g, x)) continue;
		if (kp_band(u->status[x], STATUS_INVINCIBLE) != 0) continue;
		if (kp_band(u->status[x], STATUS_CLOAKED) != 0 && kp_band(u->visibility[x], (int64_t)1 << p) == 0) continue;
		if (kp_band(u->status[x], STATUS_HALLUCINATION) != 0 && kp_current_life(g, x) < kp_max_life(g, x)) continue;
		if (n != 0 && !kp_can_attack(g, n, x, 1)) continue;
		if (critter(g, x) && kp_ai_campaign(g, p)) continue;
		if (n != 0 && kp_reject_chase(g, n, x)) continue;
		if (gk >= 0 && g->guards.kind[gk] == GUARD_AI) {
			int64_t hx = kp_s16v(g->guards.x1[gk]), hy = kp_s16v(g->guards.y1[gk]);
			if (kp_region_at(g, hx, hy) != kp_unit_region(g, x)
				&& (!kp_within_distance(g, x, GUARD_RANGE, hx, hy) || !kp_within_distance(g, n, GUARD_RANGE, hx, hy))) {
				continue;
			}
		} else if (kp_unit_region(g, x) != home && !kp_unit_box_within(g, n, REGION_REACH, x)) {
			continue;
		}
		score = score_target(g, x, mode);
		if (n != 0 && u->type[n] == WRAITH && active_detector(g, x) && kp_band(u->status[x], STATUS_CAN_MOVE) != 0) {
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
	if (ai_type(g, n) == MILITARY_AI) kp_game_fail(g, "ai: a military unit's region targets (0x4ae840, 0x4ae870)");
	r = kp_unit_region(g, n);
	found = kp_query_rect(g, kp_s16v(rg->left[r] - REGION_REACH), kp_s16v(rg->top[r] - REGION_REACH),
		kp_s16v(rg->right[r] + REGION_REACH), kp_s16v(rg->bottom[r] + REGION_REACH), &count);
	result = choose_regional_target(g, g->u.owner[n], n, r, found, count, mode);
	kp_query_release(g, found);
	return result;
}

/* HasCargo 0x438260. */
static int has_cargo(KpGame *g, int64_t n)
{
	int k;
	for (k = 0; k < 8; k++) {
		if (kp_cargo_unit(g, n, k) != 0) return 1;
	}
	return 0;
}

/* IsThreatenedBy 0x41fd90: other (or the unit's target) can attack the unit now. */
static int threatened_by(KpGame *g, int64_t n, int64_t o)
{
	KpUnits *u = &g->u;
	if (o == 0) o = u->order_unit[n];
	if (o == 0 || !kp_is_enemy(g, n, o)) return 0;
	if (kp_band(u->status[o], STATUS_HALLUCINATION) != 0 && kp_current_life(g, o) != kp_max_life(g, o)) return 0;
	if (kp_can_attack(g, o, n, 1)) return !kp_target_out_of_range(g, o, n);
	return u->type[o] == BUNKER && has_cargo(g, o);
}

/* An Interceptor stands for its Carrier when the Carrier is in range or the unit can move. */
static int64_t carrier_of(KpGame *g, int64_t n, int64_t c)
{
	KpUnits *u = &g->u;
	if (c == 0 || n == 0) return 0;
	if (u->type[c] == INTERCEPTOR && u->union_0xc0[c] != 0
		&& (kp_target_in_range(g, n, u->union_0xc0[c]) || kp_band(u->status[n], STATUS_CAN_MOVE) != 0)) {
		return u->union_0xc0[c];
	}
	return c;
}

static int dangerous(KpGame *g, int64_t c)
{
	return (g->u.type[c] == BUNKER && has_cargo(g, c)) || kp_unit_has_weapon(g, c);
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
	if (!threatened_by(g, n, a) && threatened_by(g, n, b)) return b;
	if (kp_target_in_range(g, n, a)) return a;
	if (kp_target_in_range(g, n, b)) return b;
	return kp_unit_distance(g, n, a) <= kp_unit_distance(g, n, b) ? a : b;
}

/* AiMayRetarget 0x4ae040. */
static int may_retarget(KpGame *g, int64_t n)
{
	int64_t type = ai_type(g, n);
	if (type == GUARD_AI || type != MILITARY_AI) return 1;
	kp_game_fail(g, "ai: a military unit's retargeting (0x4ae040)");
}

/* GuardReturnHome 0x4b2110: a guard away from its post and not fighting goes home (check_only only tells). */
int kp_ai_guard_return_home(KpGame *g, int64_t n, int check_only)
{
	KpUnits *u = &g->u;
	int64_t k = guard_of(g, n), hx, hy;
	if (k < 0 || g->guards.kind[k] != GUARD_AI) return 0;
	if (kp_unit_race(g, n) == 1 && kp_band(KP_DAT(g, units, special_ability_flags, u->type[n]), 0x40000000) != 0
		&& u->hp[n] != KP_DAT(g, units, hit_points, u->type[n])) {
		return 0;
	}
	if (u->auto_target[n] != 0) return 0;
	hx = kp_s16v(g->guards.x1[k]);
	hy = kp_s16v(g->guards.y1[k]);
	if (kp_region_at(g, hx, hy) == kp_unit_region(g, n)) return 0;
	if (kp_within_distance(g, n, GUARD_RANGE, hx, hy)) return 0;
	if (u->order_unit[n] != 0 && kp_target_in_range(g, n, 0)) return 0;
	if (u->queue_head[n] != KP_NONE && head_order(g, n) == PATROL) return 0;
	if (!check_only) kp_issue_order_at(g, n, COMPUTER_RETURN, hx, hy);
	return 1;
}

/* The AI target test of 0x41f6c0. */
static int valid_target(KpGame *g, int64_t n, int64_t c, int allow_critters, int strict)
{
	if (c == 0) return 0;
	if (strict && kp_unit_unreachable(g, n, c)) return 0;
	if (!kp_can_attack(g, n, c, 1)) return 0;
	return allow_critters || !critter(g, c);
}

/* AiUpdateAttackTarget 0x41f6c0: 1 when a target is (still) chosen. */
int kp_ai_update_attack_target(KpGame *g, int64_t n, int issue, int allow_critters, int strict)
{
	KpUnits *u = &g->u;
	int64_t c1, cur, a, best;
	int air = kp_band(u->status[n], STATUS_AIR) != 0;
	if (u->ai[n] == 0 || !kp_unit_has_weapon(g, n)) return 0;
	if (u->queue_head[n] != KP_NONE && head_order(g, n) == PATROL) return 0;
	if (u->order[n] == ORDER_96) {
		strict = 1;
		if (u->auto_target[n] == 0 && u->queue_head[n] != KP_NONE && head_order(g, n) == ATTACK_UNIT) return 0;
	}
	if (u->order_unit[n] != 0 && kp_band(u->status[n], STATUS_CAN_MOVE) != 0 && u->move_unit[n] != 0
		&& kp_is_enemy(g, n, u->move_unit[n]) && kp_move_arrival(g, n) == 2) {
		int32_t s = kp_unit_sprite(g, n);
		u->move_x[n] = kp_s16v(g->w->spr.x[s]);
		u->move_y[n] = kp_s16v(g->w->spr.y[s]);
		u->status[n] = kp_band(u->status[n], kp_bnot(STATUS_INTERCEPT));
		u->status[u->order_unit[n]] = kp_bor(u->status[u->order_unit[n]], STATUS_UNREACHABLE);
		if (u->order_unit[n] == u->auto_target[n]) u->auto_target[n] = 0;
		/* ClearOrderTarget 0x41df90 */
		u->order_unit[n] = 0;
		if (u->subunit[n] != 0 && kp_band(KP_DAT(g, units, special_ability_flags, u->type[u->subunit[n]]), 0x10) != 0) {
			u->order_unit[u->subunit[n]] = 0;
		}
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
		if ((u->type[a] != BUNKER && (at == 0 || u->owner[at] != u->owner[n]))
			|| (strict && kp_unit_unreachable(g, n, a))) {
			u->auto_target[n] = 0;
		}
		a = u->auto_target[n];
		if (a != 0 && (!kp_can_attack(g, n, a, 1) || (!allow_critters && critter(g, a)))) u->auto_target[n] = 0;
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
		if ((u->type[n] == 72 || u->type[n] == 82 || u->type[n] == 83 || u->type[n] == 81) && !kp_target_in_range(g, n, best)) {
			u->status[n] = kp_bor(u->status[n], 0x8);
		}
	}
	return 1;
}

/* TryAiReactionSpell 0x48d3a0: the casters' spells come with the spells (plan phase 4.6); other units cast nothing. */
int kp_ai_reaction_spell(KpGame *g, int64_t n, int reactive)
{
	if (!reactive && g->pai.countdown1[g->u.owner[n]] != 0) return 0; /* IsSpellGateClear 0x4833c0 */
	switch (g->u.type[n]) {
	case 1: case 9: case 12: case 34: case 45: case 46: case 60: case 63: case 67: case 71:
		kp_game_fail(g, "ai: a caster's reaction spell (0x48d3a0)");
	default:
		return 0;
	}
}

#define MEDIC 0x22
#define ORDER_GUARD 2

/* Order_ComputerReturn 0x41e370 (order 163): back to the guard's home, which becomes where it stops. */
void kp_order_computer_return(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t k;
	int32_t s;
	if (kp_ai_update_attack_target(g, n, 0, 1, 0)) return;
	if (u->type[n] == MEDIC) {
		kp_order_medic_idle(g, n);
		if (u->order[n] != COMPUTER_RETURN) return;
	}
	if (u->order_state[n] == 0) {
		if (!kp_request_move_to_point(g, n, kp_s16v(u->order_x[n]), kp_s16v(u->order_y[n]))) return;
		u->order_state[n] = 1;
	}
	k = guard_of(g, n);
	if (k < 0) return;
	if (g->guards.kind[k] != GUARD_AI) {
		kp_issue_order(g, n, COMPUTER_AI);
		return;
	}
	if (kp_move_arrival(g, n) == 0) return;
	/* SetGuardHome 0x4b2270 */
	s = kp_unit_sprite(g, n);
	g->guards.x1[k] = kp_s16v(g->w->spr.x[s]);
	g->guards.y1[k] = kp_s16v(g->w->spr.y[s]);
	kp_issue_order(g, n, ORDER_GUARD);
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
	if (u->type[n] == OBSERVER) return;
	k = guard_of(g, n);
	if (k >= 0 && g->guards.kind[k] == GUARD_AI) {
		int32_t s = kp_unit_sprite(g, n);
		int64_t hx = kp_s16v(g->guards.x1[k]), hy = kp_s16v(g->guards.y1[k]);
		if (kp_s16v(g->w->spr.x[s]) != hx || kp_s16v(g->w->spr.y[s]) != hy) {
			if (u->type[n] == SIEGE_TANK_SIEGED) kp_order_append(g, n, TANK_MODE, 0, 0, 0, 0xe4, 0);
			kp_order_append(g, n, COMPUTER_RETURN, hx, hy, 0, 0xe4, 0);
			kp_order_done(g, n);
			return;
		}
	}
	kp_issue_order(g, n, COMPUTER_AI);
}

#define LARVA 0x23
#define DRONE 0x29
#define SIEGE_TANK 0x05
#define STATUS_BURROWED 0x10
#define DAT_CLOAKABLE 0x200
#define DAT_BURROWABLE 0x100000
#define DAT_MECHANICAL 0x40000000
#define DAT_WORKER 0x08
#define SECONDARY_CLOAK 0x6d
#define SECONDARY_DECLOAK 0x6e
#define BURROWING 0x74
#define BURROW_TECH 11
#define WORKER_AI 2
#define BUILDING_AI 3

/* AiTryBurrow 0x48de70: an idle computer unit at its move target burrows when it can. */
static void try_burrow(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int32_t s = kp_unit_sprite(g, n);
	if (kp_band(u->status[n], STATUS_BURROWED) != 0 || u->type[n] == DRONE) return;
	if (u->order[n] != COMPUTER_AI && u->order[n] != 2) return;
	if (u->move_x[n] != kp_s16v(g->w->spr.x[s]) || u->move_y[n] != kp_s16v(g->w->spr.y[s])) return;
	/* the status 0x80000 test that follows always passes */
	if (kp_can_use_tech(g, BURROW_TECH, u->owner[n], n) == 1) kp_issue_order(g, n, BURROWING);
}

/* AiUnitTypeStep 0x48e180: 1 when the unit was given something to do. */
static int unit_type_step(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	if (u->secondary_order[n] == SECONDARY_CLOAK && kp_band(KP_DAT(g, units, special_ability_flags, u->type[n]), DAT_CLOAKABLE) != 0
		&& !threatened_by(g, n, 0) && u->auto_target[n] == 0) {
		kp_game_fail(g, "ai: the decloak test of a cloaked unit (0x432bd0)");
	}
	switch (u->type[n]) {
	case 2: case 5: case 11: case 42: case 69: case 71: case 72: case 83: case 124: case 144: case 146: case 162: case 172:
		kp_game_fail(g, "ai: the step of a computer unit of type %lld (0x48e180)", (long long)u->type[n]);
	default:
		return 0;
	}
}

/* Order_ComputerAI 0x48cd70 (order 156). */
void kp_order_computer_ai(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t t, type;
	if (u->order_timer[n] != 0) return;
	t = u->type[n];
	u->order_timer[n] = 15;
	if (t == LARVA) kp_game_fail(g, "ai: a computer's Larva (0x43cd90)");
	if (t == MEDIC) kp_game_fail(g, "ai: a computer's Medic (0x4ae8e0, 0x4afd40)");
	if (u->ai[n] == 0) {
		kp_issue_order(g, n, KP_DAT(g, units, comp_ai_idle, t));
		return;
	}
	if (unit_type_step(g, n)) return;
	type = ai_type(g, n);
	if (type == WORKER_AI) kp_game_fail(g, "ai: a worker's step (0x487580)");
	else if (type == BUILDING_AI) kp_game_fail(g, "ai: a building's step (0x487110)");
	else if (type == MILITARY_AI) kp_game_fail(g, "ai: a military unit's step (0x4b1010)");
	else kp_issue_order(g, n, KP_DAT(g, units, comp_ai_idle, t));
	if (kp_unit_race(g, n) == 1 && kp_band(KP_DAT(g, units, special_ability_flags, t), DAT_MECHANICAL) != 0
		&& ai_type(g, n) != MILITARY_AI /* IsInActiveAttackRegion 0x4b0580 */
		&& kp_sarv(u->hp[n] + 0xff, 8) != kp_max_hit_points(g, n) && kp_band(u->status[n], STATUS_CAN_MOVE) != 0
		&& u->plague_timer[n] == 0 && u->irradiate_timer[n] == 0
		&& kp_band(KP_DAT(g, units, special_ability_flags, t), DAT_WORKER) == 0) {
		kp_game_fail(g, "ai: a damaged machine's repair trip (0x432bb0)");
	}
	if (t == SIEGE_TANK) kp_game_fail(g, "ai: a computer's Siege Tank (0x48dee0)");
	if (kp_band(KP_DAT(g, units, special_ability_flags, t), DAT_BURROWABLE) != 0) try_burrow(g, n);
}

#define STIM_PACKS 0
#define STIM_LIFE 100

/* AiConsiderStim 0x4b1100: a computer's Marine or Firebat with a strong target uses Stim Packs. */
void kp_ai_consider_stim(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t target = u->order_unit[n], speed, half;
	if (u->type[n] != 0 && u->type[n] != 0x20) return;
	if (target == 0) return;
	/* 0x428af0: the speed state of stim, the 0x10000000 status and ensnare */
	speed = (u->stim_timer[n] != 0) + (kp_band(u->status[n], 0x10000000) != 0) - (u->ensnare_timer[n] != 0);
	if (speed > 0) return;
	half = kp_max_hit_points(g, n);
	half = (half - (half < 0 ? -1 : 0)) >> 1; /* signed halving toward zero */
	if (kp_sarv(u->hp[n] + 0xff, 8) < half) return;
	if (!kp_can_attack(g, target, n, 1) || !kp_target_in_range(g, n, 0)) return;
	if (kp_can_use_tech(g, STIM_PACKS, u->owner[n], n) != 1) return;
	if (kp_s32v(kp_current_life(g, target)) < STIM_LIFE) return;
	kp_apply_stim(g, n);
}

#define ORDER_TRANSPORT_REQUEST 0x60
#define SCV 7
#define ORDER_HARVEST_GAS 0x22
#define REAVER 0x53
#define REAVER_DROP_DISTANCE 0x3c0
#define CLASS_UNWALKABLE 0x1ffd

/* TryAiTransportForMove 0x437540: 1 when an AI ground unit's move goes to a transport request instead. */
int kp_ai_transport_for_move(KpGame *g, int64_t n, int64_t x, int64_t y)
{
	KpUnits *u = &g->u;
	int32_t s = kp_unit_sprite(g, n);
	int64_t sx = kp_s16v(g->w->spr.x[s]), sy = kp_s16v(g->w->spr.y[s]);
	if (u->order[n] == ORDER_TRANSPORT_REQUEST) return 1;
	if (kp_band(u->status[n], STATUS_AIR) != 0 || kp_band(KP_DAT(g, units, special_ability_flags, u->type[n]), 0x10) != 0) {
		return 0;
	}
	if ((sx == x && sy == y) || kp_band(u->status[n], STATUS_CAN_MOVE) == 0) return 0;
	if (u->type[n] == SCV && u->order[n] == ORDER_HARVEST_GAS && u->order_unit[n] != 0
		&& kp_band(u->status[u->order_unit[n]], STATUS_AIR) != 0) {
		return 0;
	}
	if (g->pathing.rg.klass[kp_region_at(g, x, y)] == CLASS_UNWALKABLE) return 0; /* 0x481bb0 */
	if (kp_placement_reachable(g, n, x, y)) {
		if (ai_type(g, n) == MILITARY_AI) kp_game_fail(g, "ai: a military unit's move in an attack region (0x4b0580)");
		if (region_reachable(g, u->owner[n], kp_unit_region(g, n), kp_region_at(g, x, y))) {
			if (u->type[n] != REAVER || kp_approx_distance(x - sx, y - sy) < REAVER_DROP_DISTANCE) return 0;
		}
	}
	kp_game_fail(g, "ai: a transport request for a move (0x437f80)");
}

/* --- the reaction to attacks --- */

#define TERRAN_FLAG_IN_BUNKER STATUS_IN_BUILDING
#define LURKER 0x67
#define CARRIER 0x48
#define UNLOAD 0x6f
#define FLEEING_ORDER 0x9f
#define ORDER_MOVE 0x06
#define ZERGLING 0x25
#define UNDER_ATTACK 0x20
#define CAMPAIGN_HELP_RANGE_BUILDING 0x120
#define HELP_RANGE_BUILDING 0x240
#define CAMPAIGN_HELP_RANGE 0x60
#define HELP_RANGE 0x120
#define DEFENSE_SECONDS 0x3c

/* GetUnitsInRegion 0x4222d0: the units of the region's box standing in the region (stacked like kp_query_rect). */
static int64_t *region_units(KpGame *g, int64_t r, int *count)
{
	KpRegionGraph *rg = &g->pathing.rg;
	int64_t *found = kp_query_rect(g, rg->left[r], rg->top[r], rg->right[r], rg->bottom[r], count);
	int k, kept = 0;
	for (k = 0; k < *count; k++) {
		if (kp_unit_region(g, found[k]) == r) found[kept++] = found[k];
	}
	g->spatial.frame_top -= *count - kept;
	*count = kept;
	return found;
}

/* RegionHasOwnBuildingOrWorker 0x4861a0. */
static int region_has_own_base(KpGame *g, int64_t p, int64_t r)
{
	int count, k, found = 0;
	int64_t *units = region_units(g, r, &count);
	for (k = 0; k < count && !found; k++) {
		int64_t n = units[k];
		if (g->u.owner[n] == p && kp_band(KP_DAT(g, units, special_ability_flags, g->u.type[n]), 9) != 0) found = 1;
	}
	kp_query_release(g, units);
	return found;
}

/* SetRegionState 0x4ab900. */
void kp_ai_set_region_state(KpGame *g, int64_t p, int64_t r, int64_t state)
{
	KpAiRegions *a = &g->air;
	int campaign = kp_ai_campaign(g, p);
	if (r == 0) return;
	if (a->state[p][r] == 0) {
		int inaccessible = g->pathing.rg.klass[r] == CLASS_UNWALKABLE;
		int64_t m = campaign ? g->pai.default_min[p] : 0;
		a->need_ground[p][r] = inaccessible ? 0 : m;
		a->need_air[p][r] = inaccessible ? m : 0;
		a->local_ground[p][r] = a->local_air[p][r] = a->all_ground[p][r] = a->all_air[p][r] = 0;
		a->target_air[p][r] = a->target_ground[p][r] = a->slowest[p][r] = a->detector[p][r] = 0;
		a->enemy_air[p][r] = a->enemy_ground[p][r] = 0;
		if (state != 7) a->dest[p][r] = 0;
	}
	a->flags[p][r] = kp_band(a->flags[p][r], kp_bnot(UNDER_ATTACK));
	a->state[p][r] = kp_mod(state, 256);
	switch (state) {
	case 0:
		a->priority[p][r] = kp_band(a->flags[p][r], REGION_NO_DECAY) != 0 && !campaign ? 40 : 10;
		break;
	case 1: case 2: a->priority[p][r] = campaign ? 70 : 160; break;
	case 3:
		a->flags[p][r] = kp_band(a->flags[p][r], 0xfe);
		a->priority[p][r] = 10;
		break;
	case 4: a->priority[p][r] = 40; break;
	case 5: a->priority[p][r] = 50; break;
	case 6: a->priority[p][r] = region_has_own_base(g, p, r) ? 110 : 100; break;
	case 7:
		a->wait[p][r] = 0;
		if (region_has_own_base(g, p, a->dest[p][r])) a->priority[p][r] = 110;
		else a->priority[p][r] = region_has_own_base(g, p, r) ? 110 : 100;
		break;
	case 8: case 9: a->priority[p][r] = campaign ? 50 : 160; break;
	default: a->priority[p][r] = 10; break;
	}
}

/* SumEnemyStrengthInRegion 0x4a9330: the strength of player p's enemies in region r, flying or not. */
static int64_t sum_enemy_strength(KpGame *g, int64_t p, int64_t r, int flying)
{
	KpUnits *u = &g->u;
	int count, k;
	int64_t *units = region_units(g, r, &count), sum = 0;
	for (k = 0; k < count; k++) {
		int64_t n = units[k], t = u->type[n], air = 0, ground;
		if ((kp_band(u->status[n], STATUS_AIR) != 0) != (flying != 0) || !kp_player_enemy_of(g, p, n)) continue;
		if (!(t == 0x23 || t == 0x24 || t == 0x3b || t == 0x61)
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
static void call_nearby_for_help(KpGame *g, int64_t n, int64_t attacker, int military_only)
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
		if (u->type[h] == 0x47) continue;
		if (!kp_can_attack(g, h, attacker, 1)
			&& (!kp_can_attack(g, attacker, h, 1) || kp_band(u->status[h], STATUS_BURROWED) != 0)) {
			continue;
		}
		if (military_only) {
			if (ai_type(g, h) != MILITARY_AI) continue;
			kp_game_fail(g, "ai: the military help of a grouped unit (0x4aa660)");
		}
		gk = guard_of(g, h);
		if (gk < 0 || g->guards.kind[gk] != GUARD_AI) {
			kp_ai_react_to_attack(g, h, attacker, 0, 1);
		} else {
			int32_t as = kp_unit_sprite(g, attacker);
			int64_t d = kp_approx_distance(kp_s16v(g->guards.x1[gk]) - kp_s16v(g->w->spr.x[as]),
				kp_s16v(g->guards.y1[gk]) - kp_s16v(g->w->spr.y[as]));
			int64_t w = kp_band(KP_DAT(g, units, special_ability_flags, u->type[u->subunit[h]]), 0x10) != 0 && u->subunit[h] != 0
				? u->subunit[h] : h, weapon;
			weapon = kp_band(u->status[h], STATUS_AIR) != 0 ? KP_DAT(g, units, air_weapon, u->type[w]) : kp_ground_weapon(g, w);
			if (weapon != 0x82 && kp_u32v(d) <= kp_u32v(kp_weapon_max_range(g, w, weapon) + GUARD_RANGE)) {
				kp_ai_react_to_attack(g, h, attacker, 0, 1);
			}
		}
	}
	kp_query_release(g, found);
}

/* AiReactToAttack 0x4aebc0: a computer's unit n attacked by a (direct: real damage; propagated: called for a
 * neighbour's help). */
void kp_ai_react_to_attack(KpGame *g, int64_t n, int64_t a, int direct, int propagated)
{
	KpUnits *u = &g->u;
	int64_t p = u->owner[n], t, vr, ar, order, type, in_group = 0, skip_regions = 0;
	int locked;
	if (kp_u32v(p) >= 8 || kp_controller(g, p) != CONTROLLER_COMPUTER) return;
	if (kp_band(KP_DAT(g, units, special_ability_flags, u->type[n]), 1) != 0 && u->ai[n] != 0) {
		kp_game_fail(g, "ai: a town building under attack (0x4835a0, 0x487b80)");
	}
	/* ResolveAttackerForHelp 0x4b1380 */
	t = 0;
	if (a != 0 && n != 0) {
		t = a;
		if (u->type[a] == INTERCEPTOR && u->union_0xc0[a] != 0
			&& (kp_target_in_range(g, n, u->union_0xc0[a]) || kp_band(u->status[n], STATUS_CAN_MOVE) != 0)) {
			t = u->union_0xc0[a];
		}
	}
	if (t == 0) kp_game_fail(g, "ai: a reaction without an attacker (0x4aebc0)");
	if (kp_band(u->status[t], TERRAN_FLAG_IN_BUNKER) != 0) {
		t = u->connected[t];
		if (t == 0) return;
	}
	if (kp_band(u->status[t], 0x800) != 0 && kp_band(u->status[t], STATUS_BURROWED) == 0) {
		kp_game_fail(g, "ai: the nearest enemy Arbiter of a cloaked attacker (0x431ed0)");
	}
	if (direct) {
		/* AiTryCloak 0x4b0210 */
		if (kp_band(u->status[n], 0x100) == 0) {
			int64_t tech = kp_cloak_tech(g, n);
			if (tech != 0x2c && kp_can_use_tech(g, tech, p, n) == 1) kp_game_fail(g, "ai: cloaking when attacked (0x434270)");
		}
		if (u->type[n] == LURKER && kp_band(u->status[n], STATUS_BURROWED) == 0 && u->order[n] != BURROWING) {
			kp_issue_order(g, n, BURROWING);
		}
	}
	if (u->ai[n] != 0) {
		if (ai_type(g, n) == MILITARY_AI) kp_game_fail(g, "ai: a military unit under attack (0x4b1a30)");
		if (direct && kp_is_transport(g, n) && kp_band(u->status[n], STATUS_CAN_MOVE) != 0) {
			kp_game_fail(g, "ai: a transport under attack (0x4afd40)");
		}
	}
	vr = kp_unit_region(g, n);
	if (!skip_regions) {
		KpAiRegions *rec = &g->air;
		int react;
		if (!propagated) call_nearby_for_help(g, n, t, (int)in_group);
		if (!in_group) {
			if (u->ai[n] == 0) react = 0;
			else react = !(ai_type(g, n) == GUARD_AI && kp_ai_campaign(g, p));
			ar = kp_unit_region(g, t);
			if (react) {
				if (rec->state[p][vr] == 5 || rec->state[p][vr] == 4) kp_ai_set_region_state(g, p, vr, 6);
				if (rec->state[p][vr] != 0 && rec->state[p][ar] == 0 && kp_scalar(g, KP_ELAPSED) > DEFENSE_SECONDS) {
					kp_ai_set_region_state(g, p, ar, 5);
				}
			}
			if (!propagated) {
				rec->enemy_air[p][ar] = kp_mod(sum_enemy_strength(g, p, ar, 1), 65536);
				rec->enemy_ground[p][ar] = kp_mod(sum_enemy_strength(g, p, ar, 0), 65536);
				if (rec->need_air[p][ar] > rec->need_air[p][vr]) rec->need_air[p][vr] = rec->need_air[p][ar];
				if (rec->need_ground[p][ar] > rec->need_ground[p][vr]) rec->need_ground[p][vr] = rec->need_ground[p][ar];
			}
		}
	}
	g->air.flags[p][vr] = kp_bor(g->air.flags[p][vr], UNDER_ATTACK);
	if (kp_band(u->status[n], 1) == 0) return;
	order = u->order[n];
	if (order == 0x6a || order == 0x96 || order == 0x21) return;
	if (!direct && kp_ai_spell_cast_order(order)) return;
	type = u->type[n];
	if (type == 0 && order == 0x5c) return;
	locked = kp_band(u->status[n], 0x3000) != 0; /* IsOrderLocked 0x429aa0 */
	if (!locked && kp_ai_reaction_spell(g, n, direct)) return;
	if (order == 0x42 || order == ORDER_MOVE) return;
	if (direct) {
		if (type == CARRIER && kp_mod(kp_band(u->union_0xc8[n], 0xff) + kp_band(kp_shrv(u->union_0xc8[n], 8), 0xff), 256) <= 2) {
			kp_game_fail(g, "ai: a Carrier without Interceptors retreats (0x4afd40)");
		}
		if (!kp_ai_campaign(g, p) && kp_band(u->status[t], STATUS_CAN_MOVE) == 0) {
			kp_game_fail(g, "ai: a damaged unit retreats (0x4afd40)");
		}
		if (type == LURKER && kp_band(u->status[n], STATUS_BURROWED) == 0 && order != BURROWING && order != 0x76) {
			kp_game_fail(g, "ai: a Lurker flees (0x4aadf0)");
		}
		if (!locked && kp_band(u->status[n], STATUS_CAN_MOVE) != 0) {
			int32_t ts = kp_unit_sprite(g, t);
			if (!kp_placement_reachable(g, n, kp_s16v(g->w->spr.x[ts]), kp_s16v(g->w->spr.y[ts]))) {
				kp_game_fail(g, "ai: a unit flees an unreachable attacker (0x4aadf0)");
			}
		}
	}
	if (u->order_unit[n] == t) return;
	if (kp_band(KP_DAT(g, units, special_ability_flags, type), DAT_WORKER) != 0
		&& kp_band(KP_DAT(g, units, special_ability_flags, u->type[t]), DAT_WORKER) == 0 && u->type[t] != ZERGLING) {
		if (!direct) return;
		kp_game_fail(g, "ai: a worker flees (0x4aadf0)");
	}
	if (direct && u->ai[n] != 0 && kp_is_undetected_by(g, t, n)) kp_game_fail(g, "ai: a detection request (0x4ae3e0)");
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
	if (order == FLEEING_ORDER) kp_game_fail(g, "ai: a fleeing unit's new flight (0x421180)");
	if (!direct || type == MEDIC) return;
	if (u->order_unit[n] != 0 && kp_can_attack(g, u->order_unit[n], u->order_unit[n], 1)) return;
	kp_game_fail(g, "ai: an unarmed unit flees (0x4aadf0)");
}

/* IsSpellCastOrder 0x4adb40. */
int kp_ai_spell_cast_order(int64_t order)
{
	switch (order) {
	case 113: case 115: case 119: case 121: case 122: case 142: case 143: case 144: case 146: case 147: return 1;
	default: return 0;
	}
}

/* The AI record type byte (+8) of unit n: 1 guard, 2 worker, 3 building, 4 military; 0 without one. */
int64_t kp_ai_unit_record_type(KpGame *g, int64_t n) { return ai_type(g, n); }

/* The home of a unit with a guard record (+0x12/+0x14); 0 for any other record. */
int kp_ai_guard_home(KpGame *g, int64_t n, int64_t *x, int64_t *y)
{
	int64_t k = guard_of(g, n);
	if (k < 0 || g->guards.kind[k] != GUARD_AI) return 0;
	*x = kp_s16v(g->guards.x1[k]);
	*y = kp_s16v(g->guards.y1[k]);
	return 1;
}

/* RemoveUnitFromAiRegions 0x4b0770: the computer players forget the unit (targets, slowest, detector) and its
 * strength in their needs; a military record leaves its region. */
void kp_ai_remove_from_regions(KpGame *g, int64_t n, int64_t killed)
{
	KpUnits *u = &g->u;
	KpAiRegions *a = &g->air;
	int64_t count = (int64_t)g->regions.count, r;
	int p;
	(void)killed;
	for (p = 0; p < KP_AI_PLAYERS; p++) {
		if (kp_controller(g, p) != CONTROLLER_COMPUTER) continue;
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
	if (ai_type(g, n) == MILITARY_AI) kp_game_fail(g, "ai: a military record leaves its region (0x4b0884)");
}

/* DetachGuardOnDeath 0x4b26f0: the guard record loses its unit and counts a death (up to 100). */
void kp_ai_detach_guard(KpGame *g, int64_t n)
{
	int64_t k = guard_of(g, n);
	if (k < 0 || g->guards.kind[k] != GUARD_AI) return;
	g->guards.unit[k] = 0;
	if (g->guards.deaths[k] < 100) g->guards.deaths[k]++;
}
