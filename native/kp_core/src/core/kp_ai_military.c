/* The computer's military records (kp_game.h KpMilitaryAis), ported from the atlas (P37, P38, P39;
 * docs/research/native-military-arrival.md, native-defense-recipes.md, native-campaign-recruitment.md,
 * native-donor-ranking.md): the pool (InitMilitaryPool 0x4a9a90), a record's allocation AddMilitaryAi 0x4ae760 and
 * release 0x4a9a30, the arrival MilitaryArrive 0x4a9b20 with RequestComputerOrder 0x4ada90, the region's slowest
 * member UpdateSlowestMember 0x4ab870, the donors (IsDonorEligible 0x4aaf70, ChooseSurplusDonor 0x4ac230,
 * ChooseFallbackDonor 0x4ac0d0), the defense recipes (BorrowRecipeUnits 0x4ac530, FulfilStrengthRecipe 0x4ac3c0) and
 * their periodic sweep RunRegionalMilitaryProduction 0x4af930.
 *
 * A region record's military list (KpAiRegions.military) holds its first record + 1 (0 none); the records link like
 * the other pools' (null -1, a released record keeps its payload). */
#include "kp_game_internal.h"
#include "kp_pathing.h"

#include <string.h>

#define NONE (-1)
#define STATUS_COMPLETED 0x1
#define STATUS_AIR 0x4
#define STATUS_BURROWED 0x10
#define STATUS_IN_BUILDING 0x20
#define STATUS_IN_TRANSPORT 0x40
#define DAT_BUILDING 0x1
#define DAT_SUBUNIT 0x10
#define RANDOM_ARRIVAL 0x38 /* the RNG source of the arrival's destination */
#define ARRIVAL_SPREAD 0x7f
#define ARRIVAL_PULL 64
#define FAR_AWAY 9999999 /* the donor searches' initial distance */
#define RECIPE_DONE 999 /* the recipe's progress sentinel */
#define RECIPE_SMALL_REGION 16 /* a state-4 requirement region below this many tiles takes no recipe */
#define CAMPAIGN_TRANSPORTS 2
#define TRANSPORTS 5
#define OBSERVERS 2
#define TRANSPORT_FIRST_PRIORITY 80
#define TRANSPORT_PRIORITY 50
#define OBSERVER_PRIORITY 80
#define DEFENSE_USE 4 /* the first use list (kp_game.h KP_AI_DEFENSE_LISTS) */
#define DEFENSE_AIR 1 /* ag's offset from gg, aa's from ga */
#define DEFENSE_UNWALKABLE 2 /* ga's offset from gg */

static int64_t record_head(KpGame *g, int64_t p, int64_t r) { return g->air.military[p][r] - 1; }

/* IsMilitaryCasterType 0x4abc90: a Science Vessel, Queen, Defiler or High Templar. */
int kp_ai_military_caster(int64_t t)
{
	return t == KP_TYPE_SCIENCE_VESSEL || t == KP_TYPE_QUEEN || t == KP_TYPE_DEFILER || t == KP_TYPE_HIGH_TEMPLAR;
}
static void set_record_head(KpGame *g, int64_t p, int64_t r, int64_t k) { g->air.military[p][r] = k + 1; }

/* InitMilitaryPool 0x4a9a90 (InitAiRegions 0x4b0640 mode 0). */
void kp_ai_military_init(KpGame *g)
{
	KpMilitaryAis *m = &g->military;
	int64_t k;
	memset(m, 0, sizeof *m);
	for (k = 0; k < KP_MILITARY_AIS; k++) {
		m->next[k] = k + 1 < KP_MILITARY_AIS ? k + 1 : NONE;
		m->prev[k] = k > 0 ? k - 1 : NONE;
		m->player[k] = m->region[k] = NONE;
	}
	m->free = 0;
}

static int64_t record_of(KpGame *g, int64_t n)
{
	int64_t ai = g->u.ai[n];
	if (ai == 0 || kp_ai_ref_kind(ai) != KP_AI_MILITARY) return NONE;
	return kp_ai_ref_index(ai);
}

/* IsMobileMilitaryAiUnit 0x4b1a30: a military record of this very unit, which is not a building. */
int kp_ai_mobile_military(KpGame *g, int64_t n)
{
	int64_t k = record_of(g, n);
	return k != NONE && g->military.unit[k] == n
		&& kp_band(KP_DAT(g, units, special_ability_flags, g->u.type[n]), DAT_BUILDING) == 0;
}

/* The military record of unit n: its player and region (NONE when it has none). */
int64_t kp_ai_military_record(KpGame *g, int64_t n, int64_t *p, int64_t *r)
{
	int64_t k = record_of(g, n);
	if (k == NONE) return NONE;
	*p = g->military.player[k];
	*r = g->military.region[k];
	return k;
}

/* IsInActiveAttackRegion 0x4b0580: unit n's military region moves or groups (or, unless staging is excluded, attacks
 * or waits to). */
int kp_ai_in_attack_region(KpGame *g, int64_t n, int exclude_staging)
{
	int64_t p, r, state;
	if (kp_ai_military_record(g, n, &p, &r) == NONE) return 0;
	state = g->air.state[p][r];
	if (state == KP_RSTATE_MOVE || state == KP_RSTATE_GROUP) return 1;
	return !exclude_staging && (state == KP_RSTATE_GATHER || state == KP_RSTATE_ATTACK);
}

/* TryEnterOwnBunker 0x4ab630: the unit enters the first of its player's Bunkers in region r that takes it (a Firebat
 * only one without a Firebat). */
static int enter_own_bunker(KpGame *g, int64_t n, int64_t r)
{
	KpUnits *u = &g->u;
	int64_t *found, bunker = 0;
	int count, i;
	found = kp_region_units(g, r, &count);
	for (i = 0; i < count && bunker == 0; i++) {
		int64_t b = found[i];
		if (u->type[b] != KP_TYPE_BUNKER || u->owner[b] != u->owner[n]) continue;
		if (u->type[n] == KP_TYPE_FIREBAT) {
			int64_t slots = KP_DAT(g, units, space_provided, u->type[b]), c;
			int k, firebat = 0;
			for (k = 0; k < slots && !firebat; k++) { /* 0x437da0 with 0x4aa4f0 */
				c = kp_cargo_unit(g, b, k);
				firebat = c != 0 && u->type[c] == KP_TYPE_FIREBAT;
			}
			if (firebat) continue;
		}
		if (kp_can_load(g, n, b)) bunker = b; /* HasRoomInTransport 0x437440 */
	}
	kp_query_release(g, found);
	if (bunker == 0) return 0;
	kp_issue_order_target(g, n, KP_ORD_ENTER_TRANSPORT, bunker);
	return 1;
}

/* HasUnreadyMember 0x4ab110: a member of the region's military is not completed or is merging or resetting. */
static int unready_member(KpGame *g, int64_t p, int64_t r)
{
	int64_t k;
	for (k = record_head(g, p, r); k != NONE; k = g->military.next[k]) {
		int64_t n = g->military.unit[k], o = g->u.order[n];
		if (kp_band(g->u.status[n], STATUS_COMPLETED) == 0 || o == KP_ORD_ARCHON_SUMMON || o == KP_ORD_RESET_COLLISION) return 1;
	}
	return 0;
}


/* MilitaryAiStep 0x4b1010: a Marine or Firebat takes to a Bunker of its region; else a reaction spell or an attack
 * target; else the unit goes to its region (a moving region's to its slowest member while one is not ready). */
void kp_ai_military_step(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t k = record_of(g, n), p, r, dest, x, y;
	if (k == NONE || g->military.unit[k] != n || kp_band(KP_DAT(g, units, special_ability_flags, u->type[n]), DAT_BUILDING) != 0) {
		return;
	}
	p = g->military.player[k];
	r = g->military.region[k];
	if ((u->type[n] == KP_TYPE_MARINE || u->type[n] == KP_TYPE_FIREBAT) && enter_own_bunker(g, n, r)) return;
	if (kp_ai_reaction_spell(g, n, 0)) return; /* TryAiReactionSpell 0x48d3a0 */
	if (kp_ai_update_attack_target(g, n, 0, 1, 0)) return; /* AiUpdateAttackTarget 0x41f6c0 */
	dest = r;
	if (g->air.state[p][r] == KP_RSTATE_MOVE) {
		int64_t leader;
		if (g->air.slowest[p][r] == 0) kp_ai_update_slowest(g, p, r);
		leader = g->air.slowest[p][r];
		if (leader != 0 && unready_member(g, p, r)) dest = kp_band(kp_unit_region(g, leader), 0xffff);
	}
	if (kp_band(kp_unit_region(g, n), 0xffff) == dest) {
		kp_stop_and_face(g, n);
		return;
	}
	kp_ai_region_center(g, dest, &x, &y); /* GetRegionCenter 0x481a30 */
	kp_ai_military_arrive(g, n, KP_ORD_ATTACK_MOVE, x, y);
}

/* RequestComputerOrder 0x4ada90: a burrowed unit unburrows first (a Lurker with a target stays) and queues the order;
 * another takes it when it has no target. A HarassMove remembers the region it was given in. */
void kp_ai_request_order(KpGame *g, int64_t n, int64_t order)
{
	KpUnits *u = &g->u;
	if (kp_band(u->status[n], STATUS_BURROWED) != 0) {
		if (u->type[n] == KP_TYPE_LURKER && u->order_unit[n] != 0) return;
		kp_request_unburrow(g, n);
		if (u->queue_head[n] == KP_NONE) kp_order_append(g, n, order, 0, 0, 0, KP_TYPE_NONE, 0); /* QueueOrder 0x429eb0 */
	} else if (u->order_unit[n] == 0) {
		kp_issue_order(g, n, order);
	}
	if (order == KP_ORD_HARASS_MOVE && kp_ai_mobile_military(g, n)) g->military.harass[record_of(g, n)] = kp_unit_region(g, n);
}

/* MilitaryArrive 0x4a9b20: a completed mobile military unit goes into its region, a sieged tank unsieging first (or
 * turning on an enemy in reach); a unit of an attack (or a caster) moves by HarassMove, one in a transport through
 * ComputerAI; a destination its military can reach spreads around the region's centre by the arrival's draws. */
void kp_ai_military_arrive(KpGame *g, int64_t n, int64_t order, int64_t x, int64_t y)
{
	KpUnits *u = &g->u;
	int64_t k = record_of(g, n), p, r, o, tx = x, ty = y;
	int unsieging = 0;
	if (k == NONE || g->military.unit[k] != n
		|| kp_band(KP_DAT(g, units, special_ability_flags, u->type[n]), DAT_BUILDING) != 0
		|| kp_band(u->status[n], STATUS_COMPLETED) == 0) {
		return;
	}
	o = u->order[n];
	if (o == KP_ORD_ARCHON_SUMMON || o == KP_ORD_RESET_COLLISION) return;
	if (kp_band(u->status[n], STATUS_IN_BUILDING) != 0) kp_unload_unit(g, n);
	else if (o == KP_ORD_SIEGING || o == KP_ORD_MOVE) return;
	if (u->type[n] == KP_TYPE_SIEGE_TANK_SIEGED) {
		if (u->order[n] == KP_ORD_UNSIEGING) return;
		if (kp_enemy_in_reach(g, n)) {
			int64_t t;
			if (kp_ai_update_attack_target(g, n, 0, 1, 0)) return;
			t = kp_acquire_target(g, n);
			if (t == 0) return;
			kp_clear_order_target(g, n);
			kp_issue_attack(g, n, t, 0, 0);
			return;
		}
		kp_issue_order(g, n, KP_ORD_UNSIEGING);
		unsieging = 1;
	}
	if (!kp_ai_mobile_military(g, n)) return;
	p = g->military.player[k];
	r = g->military.region[k];
	if (!kp_ai_military_caster(u->type[n])
		&& (g->air.state[p][r] == KP_RSTATE_MOVE || g->air.state[p][r] == KP_RSTATE_GATHER || g->air.state[p][r] == KP_RSTATE_ATTACK)) {
		order = u->type[n] == KP_TYPE_MEDIC ? KP_ORD_HEAL_MOVE : KP_ORD_HARASS_MOVE;
	}
	if (kp_band(u->status[n], STATUS_IN_TRANSPORT) != 0 && kp_band(u->status[n], STATUS_IN_BUILDING) == 0) {
		kp_ai_request_order(g, n, KP_ORD_COMPUTER_AI);
		return;
	}
	if (kp_ai_military_reachable(g, u->owner[n], kp_unit_region(g, n), kp_region_at(g, x, y))) {
		int64_t sx, sy, a, len, dx, dy, ux, uy, d, b;
		kp_ai_region_center(g, kp_unit_region(g, n), &sx, &sy);
		a = kp_direction_to(g, sx, sy, x, y);
		a += kp_band(kp_rng_game_raw(g->rng, RANDOM_ARRIVAL), 8) != 0 ? 0x40 : 0xc0;
		if (a >= 0x100) a -= 0x100;
		len = kp_band(kp_rng_game_raw(g->rng, RANDOM_ARRIVAL), ARRIVAL_SPREAD);
		kp_direction_vector(a, len, &dx, &dy);
		ux = kp_s32v(dx + x);
		uy = kp_s32v(dy + y);
		b = kp_direction_to(g, sx, sy, ux, uy);
		d = kp_approx_distance(sx - ux, sy - uy);
		len = kp_s32v(d - ARRIVAL_PULL + kp_band(kp_rng_game_raw(g->rng, RANDOM_ARRIVAL), ARRIVAL_SPREAD));
		if (len < 0) len = 0;
		kp_direction_vector(b, len, &dx, &dy);
		dx = kp_s32v(dx + sx);
		dy = kp_s32v(dy + sy);
		if (dx >= 0 && dx < g->map_w_px && dy >= 0 && dy < g->map_h_px
			&& kp_region_group_of(g, kp_region_at(g, dx, dy)) == kp_region_group_of(g, kp_region_at(g, x, y))) {
			tx = dx;
			ty = dy;
		}
	}
	if (unsieging) {
		if (u->queue_head[n] == KP_NONE) {
			kp_order_append(g, n, order, tx, ty, 0, KP_TYPE_NONE, 0); /* QueueOrderAtPoint 0x429ee0 */
			kp_order_done(g, n); /* 0x42a5a0 */
		}
		return;
	}
	kp_issue_order_at(g, n, order, tx, ty);
}

/* UpdateSlowestMember 0x4ab870 (campaign AI): the slowest ground member, else the slowest flyer, from the one
 * remembered. */
void kp_ai_update_slowest(KpGame *g, int64_t p, int64_t r)
{
	KpUnits *u = &g->u;
	int64_t ground = 0, air = 0, ground_speed = 99999999, air_speed = 99999999, cached = g->air.slowest[p][r], k;
	if (!kp_ai_campaign(g, p)) return;
	if (cached != 0) {
		if (kp_band(u->status[cached], STATUS_AIR) != 0) {
			air = cached;
			air_speed = kp_s32v(u->top_speed[cached]);
		} else {
			ground = cached;
			ground_speed = kp_s32v(u->top_speed[cached]);
		}
	}
	for (k = record_head(g, p, r); k != NONE; k = g->military.next[k]) {
		int64_t n = g->military.unit[k], v = kp_s32v(u->top_speed[n]);
		if (kp_band(u->status[n], STATUS_AIR) != 0) {
			if (v < air_speed) {
				air_speed = v;
				air = n;
			}
		} else if (v < ground_speed) {
			ground_speed = v;
			ground = n;
		}
	}
	g->air.slowest[p][r] = ground != 0 ? ground : air;
}

/* AddMilitaryAi 0x4ae760: unit n joins region r of player p (an abandoned region without force gives it to the region
 * the unit stands in) and goes there. */
void kp_ai_military_add(KpGame *g, int64_t p, int64_t r, int64_t n, int force)
{
	KpMilitaryAis *m = &g->military;
	KpUnits *u = &g->u;
	int64_t k, head, cx, cy, state;
	if (g->air.state[p][r] == KP_RSTATE_ABANDONED && !force) {
		p = u->owner[n];
		r = kp_unit_region(g, n);
	}
	head = record_head(g, p, r);
	k = kp_pool_take(m->next, m->prev, &m->free, &head);
	if (k == NONE) return;
	set_record_head(g, p, r, k);
	m->type[k] = KP_AI_MILITARY;
	m->unit[k] = n;
	m->player[k] = p;
	m->region[k] = r;
	if (kp_band(u->status[n], STATUS_AIR) == 0) g->air.ground_count[p][r] = kp_mod(g->air.ground_count[p][r] + 1, 65536);
	u->ai[n] = KP_AI_REF(KP_AI_MILITARY, k);
	kp_ai_region_center(g, r, &cx, &cy);
	kp_ai_military_arrive(g, n, u->type[n] == KP_TYPE_MEDIC ? KP_ORD_HEAL_MOVE : KP_ORD_AI_ATTACK_MOVE, cx, cy);
	state = g->air.state[p][r];
	if (state == KP_RSTATE_GATHER || state == KP_RSTATE_ATTACK || state == KP_RSTATE_MOVE || state == KP_RSTATE_GROUP) kp_ai_update_slowest(g, p, r);
}

/* The release 0x4a9a30: record k leaves its region's list for the pool's free list. */
void kp_ai_military_release(KpGame *g, int64_t k)
{
	KpMilitaryAis *m = &g->military;
	int64_t p = m->player[k], r = m->region[k], head = record_head(g, p, r);
	if (kp_band(g->u.status[m->unit[k]], STATUS_AIR) == 0) {
		g->air.ground_count[p][r] = kp_mod(g->air.ground_count[p][r] - 1, 65536);
	}
	kp_pool_release(m->next, m->prev, &head, &m->free, k);
	set_record_head(g, p, r, head);
}

/* The first military record of region r of player p (NONE for none); the others follow through KpMilitaryAis.next. */
int64_t kp_ai_military_first(KpGame *g, int64_t p, int64_t r) { return record_head(g, p, r); }

/* ---- the donors ---- */

/* IsDonorEligible 0x4aaf70: a live, completed unit with AI of type t (5 also a sieged tank), not a subunit, busy with
 * an Archon summon or collision reset, threatened or (when excluded) in a building; a Marine not entering a Bunker,
 * the cheap types always, another unit with at least half its life. */
static int donor_eligible(KpGame *g, int64_t n, int64_t t, int exclude_in_building)
{
	KpUnits *u = &g->u;
	int64_t type = u->type[n];
	if (kp_unit_inactive(g, n) || kp_band(KP_DAT(g, units, special_ability_flags, type), DAT_SUBUNIT) != 0
		|| u->ai[n] == 0) {
		return 0;
	}
	if (type != t && !(t == KP_TYPE_SIEGE_TANK && type == KP_TYPE_SIEGE_TANK_SIEGED)) return 0;
	if (kp_band(u->status[n], STATUS_COMPLETED) == 0 || u->order[n] == KP_ORD_ARCHON_SUMMON
		|| u->order[n] == KP_ORD_RESET_COLLISION || kp_ai_threatened_by(g, n, 0)) {
		return 0;
	}
	if (exclude_in_building && kp_band(u->status[n], STATUS_IN_BUILDING) != 0) return 0;
	if (type == KP_TYPE_MARINE) return u->order[n] != KP_ORD_ENTER_TRANSPORT;
	if (type == KP_TYPE_FIREBAT || type == KP_TYPE_ZERGLING || type == KP_TYPE_HYDRALISK || type == KP_TYPE_ZEALOT
		|| type == KP_TYPE_DRAGOON) {
		return 1;
	}
	return kp_s32v(kp_current_life(g, n)) >= kp_s32v(kp_max_life(g, n)) / 2;
}

static int attack_state(int64_t state) { return state == KP_RSTATE_GATHER || state == KP_RSTATE_ATTACK; } /* 0x4ab0f0 */

static int64_t sprite_distance(KpGame *g, int64_t n, int64_t x, int64_t y)
{
	int32_t s = kp_unit_sprite(g, n);
	return kp_approx_distance(x - kp_s16v(g->w->spr.x[s]), y - kp_s16v(g->w->spr.y[s]));
}

/* ChooseSurplusDonor 0x4ac230: the nearest military unit of type t (outside a building) of region record (p, r)'s
 * owner whose region keeps its needs without it and is neither this region, the destination of a support or attack,
 * nor one supporting or attacking. */
static int64_t surplus_donor(KpGame *g, int64_t p, int64_t r, int64_t t, int64_t x, int64_t y, int64_t dest)
{
	KpAiRegions *a = &g->air;
	KpUnits *u = &g->u;
	int64_t best = NONE, best_d = FAR_AWAY, n;
	if (t == KP_TYPE_SIEGE_TANK) return NONE;
	for (n = g->player_units[p]; n != 0; n = u->player_next[n]) {
		int64_t k, mr, air, ground, d;
		if (!donor_eligible(g, n, t, 1)) continue;
		k = record_of(g, n);
		if (k == NONE) continue;
		mr = g->military.region[k];
		if (mr == r || a->state[p][mr] == KP_RSTATE_HELP || attack_state(a->state[p][mr])) continue;
		if ((a->state[p][r] == KP_RSTATE_HELP || attack_state(a->state[p][r])) && mr == dest) continue;
		air = kp_band(u->air_strength[n], 0xffff);
		if (air != 0 && a->all_air[p][mr] - air < a->need_air[p][mr]) continue;
		ground = kp_band(u->ground_strength[n], 0xffff);
		if (ground != 0 && a->all_ground[p][mr] - ground <= a->need_ground[p][mr]) continue;
		/* a ground donor's route test 0x496ac0 has no effect: its result is not used */
		d = sprite_distance(g, n, x, y);
		if (kp_u32v(d) < kp_u32v(best_d)) {
			best_d = d;
			best = k;
		}
	}
	return best;
}

/* ChooseFallbackDonor 0x4ac0d0: the nearest unit of type t with a guard (a campaign AI's unless the receiver
 * supports; another AI's only for an attack) or a military record elsewhere below the priority threshold. The result
 * is a guard (kind KP_AI_GUARD) or a military record index. */
static int64_t fallback_donor(KpGame *g, int64_t p, int64_t r, int64_t threshold, int64_t t, int64_t x, int64_t y,
	int64_t dest, int *guard)
{
	KpAiRegions *a = &g->air;
	KpUnits *u = &g->u;
	int64_t best = NONE, best_d = FAR_AWAY, n;
	*guard = 0;
	for (n = g->player_units[p]; n != 0; n = u->player_next[n]) {
		int64_t d, kind, k, mr;
		if (!donor_eligible(g, n, t, 0)) continue;
		d = sprite_distance(g, n, x, y);
		if (kp_u32v(d) >= kp_u32v(best_d) || u->ai[n] == 0) continue;
		kind = kp_ai_unit_record_type(g, n);
		if (kind == KP_AI_GUARD) {
			if (kp_ai_campaign(g, p) ? a->state[p][r] == KP_RSTATE_HELP : !attack_state(a->state[p][r])) continue;
			best_d = d;
			best = kp_ai_ref_index(u->ai[n]);
			*guard = 1;
			continue;
		}
		if (kind != KP_AI_MILITARY) continue;
		k = record_of(g, n);
		mr = g->military.region[k];
		if (mr == r) continue;
		if ((a->state[p][mr] == KP_RSTATE_MOVE || a->state[p][mr] == KP_RSTATE_GROUP) && !kp_ai_campaign(g, p)) continue;
		if (kp_band(u->status[n], STATUS_IN_BUILDING) != 0
			&& (threshold == KP_AI_MANNED_CREW_PRIORITY || (!kp_ai_campaign(g, u->owner[n]) && !attack_state(a->state[p][r])))) {
			continue;
		}
		if ((a->state[p][r] == KP_RSTATE_HELP || attack_state(a->state[p][r])) && mr == dest) continue;
		if (a->priority[p][mr] >= threshold) continue;
		best_d = d;
		best = k;
		*guard = 0;
	}
	return best;
}

/* A defense list's word k counted on from the start of list l: the original reads past a full list into the next
 * ones and, past the last use list, into the build limits +0x3f0. */
static int64_t defense_word(KpGame *g, int64_t p, int64_t l, int64_t k)
{
	int64_t at = l * KP_AI_DEFENSE + k, limits;
	if (at < KP_AI_DEFENSE_LISTS * KP_AI_DEFENSE) return g->pai.defense[p][at / KP_AI_DEFENSE][at % KP_AI_DEFENSE];
	limits = 2 * (at - KP_AI_DEFENSE_LISTS * KP_AI_DEFENSE);
	if (limits + 1 >= KP_UNIT_TYPES) kp_game_fail(g, "ai: a defense list read past the build limits (0x4ac530)");
	return g->pai.limits[p][limits] | g->pai.limits[p][limits + 1] << 8;
}

/* IsInGroundDefenseUseList 0x4824b0: type t is in player p's ground-to-ground use list (read on past a full list). */
int kp_ai_defense_use_has(KpGame *g, int64_t p, int64_t t)
{
	int64_t k, word;
	for (k = 0; (word = defense_word(g, p, DEFENSE_USE, k)) != 0; k++) {
		if (word == t + 1) return 1;
	}
	return 0;
}

static int64_t type_strength(KpGame *g, int64_t t, int air) { return kp_band(g->strength[air ? 0 : 1][t], 0xffffffff); }

/* RecruitForRegion 0x4ae960: a unit of type t comes from the best donor into receiver region r (a guard's unit then
 * leaves its guard, a military one its region, unloading from a building); 0 without a donor. */
int kp_ai_recruit(KpGame *g, int64_t p, int64_t r, int64_t t, int64_t threshold)
{
	int64_t d, n, old, cx, cy, dest = g->air.dest[p][r];
	int guard = 0;
	kp_ai_region_center(g, r, &cx, &cy);
	d = surplus_donor(g, p, r, t, cx, cy, dest);
	if (d == NONE) d = fallback_donor(g, p, r, threshold, t, cx, cy, dest, &guard);
	if (d == NONE) return 0;
	if (guard) {
		n = g->guards.unit[d];
		g->u.ai[n] = 0;
		kp_ai_military_add(g, p, r, n, 1);
		g->guards.unit[d] = 0;
		old = kp_unit_region(g, n);
		kp_ai_update_region_strength(g, p, old);
	} else {
		int64_t op = g->military.player[d];
		old = g->military.region[d];
		n = g->military.unit[d];
		kp_ai_military_release(g, d);
		g->u.ai[n] = 0;
		kp_ai_military_add(g, p, r, n, 1);
		if (kp_band(g->u.status[n], STATUS_IN_BUILDING) != 0) kp_unload_unit(g, n);
		kp_ai_update_region_strength(g, op, old);
	}
	return 1;
}

/* BorrowRecipeUnits 0x4ac530: each unit type of the use list l is recruited into receiver region r; the credit is the
 * recruited types' base strength. */
static int64_t borrow(KpGame *g, int64_t p, int64_t r, int64_t l, int air)
{
	int64_t credit = 0, k, word;
	for (k = 0; (word = defense_word(g, p, l, k)) != 0; k++) {
		if (kp_ai_recruit(g, p, r, word - 1, g->air.priority[p][r])) credit = kp_u32v(credit + type_strength(g, word - 1, air));
	}
	return credit;
}

#define DAT_TWO_IN_EGG 0x400
#define RECRUIT_RADIUS 60
#define RECRUIT_RADIUS_WIDE 160
#define RECRUIT_PRIORITY 60
#define TIMED_REQUEST_SECONDS 30

/* GetMissingRosterUnits 0x4aa500: the types of player p's attack roster that its units do not yet stand for: a
 * completed unit of grouping region r's military, or one in the making (an egg as its product, twice for a pair;
 * unless incomplete ones are excluded). */
static int64_t missing_roster(KpGame *g, int64_t p, int64_t r, int64_t *out, int exclude_incomplete)
{
	KpUnits *u = &g->u;
	int64_t count = 0, n;
	while (count < KP_AI_ROSTER && g->pai.roster[p][count] != 0) {
		out[count] = g->pai.roster[p][count] - 1;
		count++;
	}
	for (n = g->player_units[p]; n != 0; n = u->player_next[n]) {
		int64_t t = u->type[n], times = 1, k, o = u->order[n];
		int done;
		if (kp_unit_inactive(g, n) || kp_band(KP_DAT(g, units, special_ability_flags, t), DAT_SUBUNIT) != 0) continue;
		if (t == KP_TYPE_SIEGE_TANK_SIEGED) t = KP_TYPE_SIEGE_TANK;
		done = kp_band(u->status[n], STATUS_COMPLETED) != 0 && o != KP_ORD_ARCHON_SUMMON && o != KP_ORD_RESET_COLLISION;
		if (!done) {
			if (exclude_incomplete) continue;
			if (t == KP_TYPE_EGG || t == KP_TYPE_COCOON || t == KP_TYPE_LURKER_EGG) {
				t = kp_band(*kp_queue_entry(g, n, u->queue_slot[n]), 0xffff);
				if (kp_band(KP_DAT(g, units, special_ability_flags, t), DAT_TWO_IN_EGG) != 0) times = 2;
			}
		}
		if (kp_band(KP_DAT(g, units, ai_internal, t), KP_AIINT_IGNORE) != 0) continue;
		if (done) {
			int64_t mp, mr;
			if (kp_ai_military_record(g, n, &mp, &mr) == NONE || mp != p || mr != r) continue;
		}
		for (; times > 0; times--) {
			for (k = 0; k < count; k++) {
				if (out[k] != t) continue;
				out[k] = KP_TYPE_NONE;
				if (k == count - 1) count--;
				break;
			}
		}
	}
	return count;
}

/* RecruitAttackRoster 0x4ab040: grouping region r recruits each missing roster type, else requests it. */
static void recruit_roster(KpGame *g, int64_t p, int64_t r)
{
	int64_t missing[KP_AI_ROSTER], count, k, radius = RECRUIT_RADIUS, timed = g->pai.timed_request[p];
	int campaign = kp_ai_campaign(g, p);
	count = missing_roster(g, p, r, missing, !campaign);
	if (!campaign && (timed == 0 || kp_u32v(kp_scalar(g, KP_ELAPSED) - timed) >= TIMED_REQUEST_SECONDS)) {
		radius = RECRUIT_RADIUS_WIDE; /* CanIssueTimedRequest 0x483570 */
	}
	for (k = 0; k < count; k++) {
		if (missing[k] >= KP_TYPE_NONE || kp_ai_recruit(g, p, r, missing[k], radius)) continue;
		kp_ai_request_unit(g, p, KP_AI_CONTEXT(KP_AI_CONTEXT_REGION, r), missing[k], RECRUIT_PRIORITY, 1, 1); /* 0x482990 */
	}
}

/* FulfilStrengthRecipe 0x4ac3c0: receiver r of player p borrows by its use recipe (ga or aa where either region is
 * inaccessible, else gg or ag by the mode); without borrowed credit it requests its build recipe's units, a campaign AI
 * until the requirement is covered. 999 when nothing can be made (a small state-4 requirement, an empty recipe). */
static int64_t fulfil(KpGame *g, int64_t p, int64_t r, int64_t req, int air, int64_t required)
{
	KpAiRegions *a = &g->air;
	int64_t list, credit, k, word, priority;
	int unwalkable = g->pathing.rg.klass[r] == KP_REGION_UNWALKABLE || g->pathing.rg.klass[req] == KP_REGION_UNWALKABLE;
	if (a->state[p][req] == KP_RSTATE_WATCH) {
		if (kp_region_tiles(g, req) < RECIPE_SMALL_REGION) return RECIPE_DONE;
	} else {
		credit = borrow(g, p, r, DEFENSE_USE + (unwalkable ? DEFENSE_UNWALKABLE : 0) + (air ? DEFENSE_AIR : 0), air);
		if (credit != 0) return credit;
	}
	list = (unwalkable ? DEFENSE_UNWALKABLE : 0) + (air ? DEFENSE_AIR : 0);
	if (defense_word(g, p, list, 0) == 0) return RECIPE_DONE;
	priority = a->priority[p][r] + g->pai.spend_counter[p]; /* 0x4827c0 */
	credit = 0;
	for (k = 0; (word = defense_word(g, p, list, k)) != 0; k++) {
		kp_ai_request_unit(g, p, KP_AI_CONTEXT(KP_AI_CONTEXT_REGION, r), word - 1, priority, 0, 0);
		credit = kp_u32v(credit + type_strength(g, word - 1, air));
		if (kp_ai_campaign(g, p) && credit >= required) break;
	}
	return credit;
}

/* A recipe of zero-strength types makes no progress: the original's loop below would never end
 * (native-defense-recipes.md), so the game fails with its address instead of hanging. */
static void recipe_stalled(KpGame *g, int64_t p, int64_t r)
{
	kp_game_fail(g, "ai: player %lld region %lld: a defense recipe without strength never meets the need (0x4af930)",
		(long long)p, (long long)r);
}

/* RunRegionalMilitaryProduction 0x4af930: every region beyond state 3 fills its (or its destination's) needs by the
 * recipes; an attack's first roster is recruited; then the transports (Terran with a Control Tower, Protoss with two
 * Observers) for the last base region. */
void kp_ai_military_production(KpGame *g, int64_t p)
{
	KpAiRegions *a = &g->air;
	int64_t r, base = NONE, race = g->players.race[p], transport, have, want, priority, k;
	for (r = 1; r < (int64_t)g->regions.count; r++) {
		int64_t state = a->state[p][r], req, sum;
		if (state <= KP_RSTATE_ABANDONED) continue;
		if (state == KP_RSTATE_GATHER) {
			recruit_roster(g, p, r);
			continue;
		}
		if (state == KP_RSTATE_BASE || state == KP_RSTATE_WATCH) base = r;
		req = state >= KP_RSTATE_HELP ? a->dest[p][r] : r;
		sum = a->all_air[p][r];
		while (a->need_air[p][req] != 0 && a->need_air[p][req] > sum) {
			int64_t credit = fulfil(g, p, r, req, 1, a->need_air[p][req] - sum);
			if (credit == 0 && a->need_air[p][req] > sum) recipe_stalled(g, p, r);
			sum = kp_u32v(sum + credit);
		}
		sum = a->all_ground[p][r];
		while (a->need_ground[p][req] != 0 && a->need_ground[p][req] > sum) {
			int64_t credit = fulfil(g, p, r, req, a->enemy_air[p][req] != 0, a->need_ground[p][req] - sum);
			if (credit == 0 && a->need_ground[p][req] > sum) recipe_stalled(g, p, r);
			sum = kp_u32v(sum + credit);
		}
		if (kp_band(g->pai.flags[p], KP_AIF_REQUESTED) != 0) { /* ConsumeAiRequestFlag 0x482900 */
			if (g->pai.spend_counter[p] != 0) g->pai.spend_counter[p]--;
			g->pai.flags[p] = kp_band(g->pai.flags[p], kp_bnot(KP_AIF_REQUESTED));
		}
	}
	if (kp_band(g->pai.flags[p], KP_AIF_TRANSPORTS_OFF) != 0) return;
	if (race == KP_RACE_TERRAN) {
		if (g->count_completed[KP_TYPE_CONTROL_TOWER * KP_PLAYERS + p] == 0) return;
		transport = KP_TYPE_DROPSHIP;
	} else if (race == KP_RACE_PROTOSS) {
		have = kp_u32v(g->count_all[KP_TYPE_OBSERVER * KP_PLAYERS + p]);
		for (k = have; k < OBSERVERS; k++) kp_ai_request_military_unit(g, p, KP_TYPE_OBSERVER, OBSERVER_PRIORITY);
		transport = KP_TYPE_SHUTTLE;
	} else {
		return;
	}
	if (base == NONE) return;
	have = kp_u32v(g->count_all[transport * KP_PLAYERS + p]);
	want = kp_ai_campaign(g, p) ? CAMPAIGN_TRANSPORTS : TRANSPORTS;
	priority = have == 0 ? TRANSPORT_FIRST_PRIORITY : TRANSPORT_PRIORITY;
	for (k = have; k < want; k++) kp_ai_request_unit(g, p, KP_AI_CONTEXT(KP_AI_CONTEXT_REGION, base), transport, priority, 1, 1);
}

/* FindSpareMilitaryUnit 0x4af750: the nearest live unit of type t of player p with a military record whose region
 * (not attacking, supporting or in state 1) keeps more ground strength than it needs without it. */
int64_t kp_ai_spare_military(KpGame *g, int64_t p, int64_t t, int64_t x, int64_t y)
{
	KpAiRegions *a = &g->air;
	KpUnits *u = &g->u;
	int64_t best = 0, best_d = FAR_AWAY, n;
	for (n = g->player_units[p]; n != 0; n = u->player_next[n]) {
		int64_t ai = u->ai[n], k, mp, mr, state, d;
		if (kp_unit_inactive(g, n) || u->type[n] != t || ai == 0 || kp_ai_ref_kind(ai) != KP_AI_MILITARY) continue;
		k = kp_ai_ref_index(ai);
		mp = g->military.player[k];
		mr = g->military.region[k];
		state = a->state[mp][mr];
		if (state == KP_RSTATE_MOVE || attack_state(state) || state == KP_RSTATE_HELP) continue;
		if (kp_s32v(a->all_ground[mp][mr] - kp_band(u->ground_strength[n], 0xffff)) <= a->need_ground[mp][mr]) continue;
		d = sprite_distance(g, n, x, y);
		if (kp_u32v(d) < kp_u32v(best_d)) {
			best_d = d;
			best = n;
		}
	}
	return best;
}

/* ---- the attack's preparation (PrepareAttack 0x4af300; docs/research/native-campaign-attack.md) ---- */

/* The classes of the preparation's region map. */
enum { CLASS_NONE, CLASS_ENEMY, CLASS_FRONT, CLASS_TARGET, CLASS_BEYOND, CLASS_OWN, CLASS_OPEN };
#define DAT_FLYER 0x4
#define STATUS_HIDDEN 0x300 /* cloaked or burrowed: seen only when detected */
#define STAGING_LAYERS 5
#define STAGING_SHALLOW 4 /* the depths below this one need the shallow search */
#define STAGING_MIN_TILES 60
#define STAGING_FAR 99999999
#define RANDOM_STAGING 0x28 /* the RNG source of a campaign attack's staging choice */
static const int64_t STAGING_DEPTHS[] = { 4, 3, 5, 6, 2 };

static int64_t neighbors(KpGame *g, int64_t r, int all, int64_t *at)
{
	if (all) return kp_region_all_neighbors(g, r, at); /* GetRegionAllNeighbors 0x481ad0 */
	*at = g->pathing.rg.nb_at[r]; /* GetRegionGroundNeighbors 0x481ab0 */
	return g->pathing.rg.ground[r];
}

/* 0x4ab760: the regions of player p's buildings (or, without any, of all its units) are its own. */
static void classify_own(KpGame *g, int64_t p, uint8_t *cls)
{
	KpUnits *u = &g->u;
	int64_t n;
	int found = 0;
	for (n = g->player_units[p]; n != 0; n = u->player_next[n]) {
		int64_t flags = KP_DAT(g, units, special_ability_flags, u->type[n]);
		if (kp_unit_inactive(g, n) || kp_band(flags, DAT_SUBUNIT) != 0 || kp_band(flags, DAT_BUILDING) == 0) continue;
		cls[kp_band(kp_unit_region(g, n), 0xffff)] = CLASS_OWN;
		found = 1;
	}
	if (found) return;
	for (n = g->player_units[p]; n != 0; n = u->player_next[n]) {
		if (kp_unit_inactive(g, n) || kp_band(KP_DAT(g, units, special_ability_flags, u->type[n]), DAT_SUBUNIT) != 0) continue;
		cls[kp_band(kp_unit_region(g, n), 0xffff)] = CLASS_OWN;
	}
}

/* 0x4ab6d0: the regions of the enemies player p sees with a weapon or a building. */
static void classify_enemies(KpGame *g, int64_t p, uint8_t *cls)
{
	KpUnits *u = &g->u;
	int64_t n;
	for (n = g->lists[KP_LIST_ACTIVE].head; n != 0; n = u->next[n]) {
		if (kp_unit_inactive(g, n)) continue;
		if (!kp_unit_has_weapon(g, n) && kp_band(KP_DAT(g, units, special_ability_flags, u->type[n]), DAT_BUILDING) == 0) continue;
		if (kp_band(u->status[n], STATUS_HIDDEN) != 0 && kp_band(u->visibility[n], (int64_t)1 << p) == 0) continue;
		if (!kp_player_enemy_of(g, p, n)) continue; /* 0x429110 */
		cls[kp_band(kp_unit_region(g, n), 0xffff)] = CLASS_ENEMY;
	}
}

/* 0x4aab90: around the own regions (by the classes before the pass) an enemy region becomes the front, an empty one
 * open ground; 1 when a class changed. */
static int expand_front(KpGame *g, uint8_t *cls, int64_t count)
{
	uint8_t before[KP_AI_REGIONS];
	int64_t r, k, n, at;
	int changed = 0;
	memcpy(before, cls, sizeof before);
	for (r = 0; r < count; r++) {
		if (before[r] < CLASS_OWN) continue;
		n = kp_region_all_neighbors(g, r, &at);
		for (k = 0; k < n; k++) {
			int64_t q = g->pathing.rg.neighbors[at + k];
			if (cls[q] == CLASS_ENEMY) cls[q] = CLASS_FRONT;
			else if (cls[q] == CLASS_NONE) cls[q] = CLASS_OPEN;
			else continue;
			changed = 1;
		}
	}
	return changed;
}

/* 0x4aab20: the front (by the classes before the pass) becomes the target, its unclassified neighbours beyond it. */
static void promote_front(KpGame *g, uint8_t *cls, int64_t count)
{
	uint8_t before[KP_AI_REGIONS];
	int64_t r, k, n, at;
	memcpy(before, cls, sizeof before);
	for (r = 0; r < count; r++) {
		if (before[r] != CLASS_FRONT) continue;
		cls[r] = CLASS_TARGET;
		n = kp_region_all_neighbors(g, r, &at);
		for (k = 0; k < n; k++) {
			int64_t q = g->pathing.rg.neighbors[at + k];
			if (cls[q] == CLASS_NONE) cls[q] = CLASS_BEYOND;
		}
	}
}

/* FindAttackStagingRegion 0x4b1810: from target region t, layers of open regions player p idles, defends or holds;
 * by the depths STAGING_DEPTHS the region of at least STAGING_MIN_TILES tiles nearest (x, y) (+ 1), else for a
 * campaign the largest one met; 0 for none. */
static int64_t staging_region(KpGame *g, int64_t p, int64_t t, const uint8_t *cls, int64_t x, int64_t y, int shallow,
	int all)
{
	KpAiRegions *a = &g->air;
	uint8_t layer[KP_AI_REGIONS];
	int64_t count = (int64_t)g->regions.count, d, r, best = STAGING_FAR, fallback = 0, most = 0;
	size_t i;
	if (cls[t] != CLASS_TARGET) return 0;
	memset(layer, 0, sizeof layer);
	layer[t] = 1;
	for (d = 1; d <= STAGING_LAYERS; d++) {
		for (r = 1; r < count; r++) {
			int64_t k, n, at;
			if (layer[r] != d) continue;
			n = neighbors(g, r, all, &at);
			for (k = 0; k < n; k++) {
				int64_t q = g->pathing.rg.neighbors[at + k], state = a->state[p][q];
				if ((state == KP_RSTATE_IDLE || state == KP_RSTATE_BASE || state == KP_RSTATE_WATCH) && layer[q] == 0 && cls[q] == CLASS_OPEN) layer[q] = (uint8_t)(d + 1);
			}
		}
	}
	for (i = 0; i < sizeof STAGING_DEPTHS / sizeof STAGING_DEPTHS[0]; i++) {
		int64_t depth = STAGING_DEPTHS[i], found = 0;
		if (!shallow && depth < STAGING_SHALLOW) continue;
		for (r = 1; r < count; r++) {
			int64_t tiles, cx, cy, dist;
			if (kp_band(a->flags[p][r], KP_RFLAG_UNDER_ATTACK) != 0 || layer[r] != depth) continue;
			tiles = kp_region_tiles(g, r);
			if (tiles > most) {
				most = tiles;
				fallback = r + 1;
			}
			if (tiles < STAGING_MIN_TILES) continue;
			kp_ai_region_center(g, r, &cx, &cy); /* GetRegionCenter 0x481a30 */
			dist = kp_approx_distance(cx - x, cy - y);
			if (dist < best) {
				best = dist;
				found = r + 1;
			}
		}
		if (found != 0) return found;
	}
	return kp_ai_campaign(g, p) ? fallback : 0;
}

/* SetAttackGrouping 0x483a60: the grouping region (+ 1), and its time unless an unforced grouping already has one. */
static void set_grouping(KpGame *g, int64_t p, int64_t r, int force)
{
	KpPlayerAi *pai = &g->pai;
	if (force || pai->grouping_time[p] == 0) pai->grouping_time[p] = kp_u32v(kp_scalar(g, KP_ELAPSED));
	pai->attack_target[p] = kp_band(r + 1, 0xffff);
}

/* ChooseAttackStaging 0x4b1710: one of the targets with a staging region (a campaign's at random) starts grouping
 * there (state 8) towards it. */
static int choose_staging(KpGame *g, int64_t p, const uint8_t *cls, int64_t x, int64_t y, int shallow, int all, int force)
{
	int64_t targets[KP_AI_REGIONS], stagings[KP_AI_REGIONS], count = (int64_t)g->regions.count, found = 0, t, pick;
	for (t = 1; t < count; t++) {
		int64_t s = staging_region(g, p, t, cls, x, y, shallow, all);
		if (s == 0) continue;
		targets[found] = t;
		stagings[found++] = s - 1;
	}
	if (found == 0) return 0;
	if (found == 1) {
		pick = 0;
	} else if (kp_ai_campaign(g, p)) {
		pick = kp_u32v(kp_rng_game_raw(g->rng, RANDOM_STAGING)) % found;
	} else {
		/* TODO: a non-campaign attack's ranking ChooseWeakestNearbyRegion 0x4aa350 (melee, outside V1) */
		kp_game_fail(g, "ai: a non-campaign attack's target ranking (0x4aa350)");
	}
	set_grouping(g, p, stagings[pick], force);
	kp_ai_set_region_state(g, p, stagings[pick], KP_RSTATE_GATHER); /* SetRegionState 0x4ab900 */
	g->air.dest[p][stagings[pick]] = targets[pick];
	return 1;
}

int kp_ai_prepare_attack(KpGame *g, int64_t p, int64_t x, int64_t y, int force, int retry)
{
	KpPlayerAi *pai = &g->pai;
	uint8_t cls[KP_AI_REGIONS];
	int64_t count = (int64_t)g->regions.count, grouping = pai->attack_target[p], k;
	int air;
	if (pai->countdown2[p] != 0) return 1; /* 0x4833e0 */
	if (grouping != 0) return g->air.state[p][grouping - 1] != KP_RSTATE_GATHER; /* 0x483800 */
	memset(cls, 0, sizeof cls);
	classify_own(g, p, cls);
	classify_enemies(g, p, cls);
	while (expand_front(g, cls, count)) promote_front(g, cls, count);
	air = pai->roster[p][0] != 0;
	for (k = 0; k < KP_AI_ROSTER && pai->roster[p][k] != 0; k++) {
		if (pai->roster[p][k] > KP_UNIT_TYPES) {
			/* TODO: a lost roster entry (KP_AI_ROSTER_LOST, MarkRosterUnitLost 0x4839a0) reads past the units' flags
			 * here */
			kp_game_fail(g, "ai: the flags of roster entry %lld (0x4af300)", (long long)pai->roster[p][k]);
		}
		if (kp_band(KP_DAT(g, units, special_ability_flags, pai->roster[p][k] - 1), DAT_FLYER) == 0) {
			air = 0;
			break;
		}
	}
	pai->flags[p] = kp_bor(pai->flags[p], KP_AIF_ENGAGED); /* 0x4832d0 */
	if (choose_staging(g, p, cls, x, y, 0, air, force) || choose_staging(g, p, cls, x, y, 1, air, force)) return 0;
	if (!retry || air) return 1;
	if (choose_staging(g, p, cls, x, y, 0, 1, force) || choose_staging(g, p, cls, x, y, 1, 1, force)) return 0;
	pai->countdown2[p] = 1; /* 0x483500 */
	return 1;
}

#define GROUPING_SECONDS 180
#define GROUPING_DEFAULT_LEFT 20

/* StartAttackGrouping 0x483ad0: the grouping time backdated so that it ends after `left` seconds (an unset left: 20,
 * unless a grouping already runs). */
void kp_ai_start_grouping(KpGame *g, int64_t p, int64_t left)
{
	if (left == 0) {
		if (g->pai.grouping_time[p] != 0) return;
		left = GROUPING_DEFAULT_LEFT;
	}
	g->pai.grouping_time[p] = kp_u32v(kp_scalar(g, KP_ELAPSED) - GROUPING_SECONDS + left);
}

/* HasActiveAttackRegion 0x4ab5d0: a region of player p moves, groups, attacks or waits to. */
int kp_ai_attack_active(KpGame *g, int64_t p)
{
	int64_t r, count = (int64_t)g->regions.count;
	for (r = 1; r < count; r++) {
		int64_t state = g->air.state[p][r];
		if (state == KP_RSTATE_MOVE || state == KP_RSTATE_GROUP || state == KP_RSTATE_GATHER || state == KP_RSTATE_ATTACK) return 1;
	}
	return 0;
}

/* ---- a group's moves (HarassMove 158; docs/research/native-military-ai.md) ---- */

#define LEADER_WAIT_NEAR 256
#define LEADER_WAIT_FAR 384
#define FOLLOWER_NEAR 128
#define FOLLOWER_HOME 384
#define FOLLOWER_MIN_TILES 60

static int64_t unit_distance(KpGame *g, int64_t a, int64_t b)
{
	int32_t s = kp_unit_sprite(g, b);
	return sprite_distance(g, a, kp_s16v(g->w->spr.x[s]), kp_s16v(g->w->spr.y[s]));
}

/* GetGroupSlowestUnit 0x4afaf0: the slowest member of a mobile military unit's region, 0 for none. */
int64_t kp_ai_group_slowest(KpGame *g, int64_t n)
{
	int64_t k = record_of(g, n);
	if (!kp_ai_mobile_military(g, n)) return 0;
	return g->air.slowest[g->military.player[k]][g->military.region[k]];
}

/* FollowerIsReady 0x4b09c0: 0 when a mobile military unit should chase its region's leader (another one, outside a
 * transport, in a large region it reaches, more than 128 away while the unit is within 384 of its region's centre). */
int kp_ai_follower_ready(KpGame *g, int64_t n)
{
	int64_t k = record_of(g, n), p, r, leader, cx, cy;
	if (!kp_ai_mobile_military(g, n)) return 1;
	p = g->military.player[k];
	r = g->military.region[k];
	leader = g->air.slowest[p][r];
	if (leader == 0 || kp_band(g->u.status[leader], STATUS_IN_TRANSPORT) != 0 || leader == n || g->air.state[p][r] == KP_RSTATE_GROUP) {
		return 1;
	}
	if (kp_region_tiles(g, kp_band(kp_unit_region(g, leader), 0xffff)) < FOLLOWER_MIN_TILES) return 1;
	/* 0x4ab420 */
	if (!kp_ai_military_reachable(g, g->u.owner[n], kp_unit_region(g, n), kp_band(kp_unit_region(g, leader), 0xffff))) return 1;
	if (kp_u32v(unit_distance(g, n, leader)) <= FOLLOWER_NEAR) return 1;
	kp_ai_region_center(g, r, &cx, &cy);
	return kp_u32v(sprite_distance(g, n, cx, cy)) <= FOLLOWER_HOME;
}

/* LeaderShouldWait 0x4aff20: a mobile military unit of a region that does not group waits for a member 256..384
 * away that it reaches and that is not ready. */
int kp_ai_leader_should_wait(KpGame *g, int64_t n)
{
	int64_t k = record_of(g, n), p, r, m;
	if (!kp_ai_mobile_military(g, n)) return 0;
	p = g->military.player[k];
	r = g->military.region[k];
	if (g->air.state[p][r] == KP_RSTATE_GROUP) return 0;
	for (m = record_head(g, p, r); m != NONE; m = g->military.next[m]) {
		int64_t c = g->military.unit[m], d;
		if (c == n) continue;
		d = kp_u32v(unit_distance(g, n, c));
		if (d > LEADER_WAIT_FAR) continue;
		if (!kp_ai_military_reachable(g, g->u.owner[n], kp_band(kp_unit_region(g, n), 0xffff), kp_band(kp_unit_region(g, c), 0xffff))) continue;
		if (!kp_ai_follower_ready(g, c) && d > LEADER_WAIT_NEAR) return 1;
	}
	return 0;
}

/* GroupHasPendingPickup 0x4afec0: another member of a mobile military unit's region (not grouping) waits for a
 * transport outside one. */
int kp_ai_group_pending_pickup(KpGame *g, int64_t n)
{
	int64_t k = record_of(g, n), p, r, m;
	if (!kp_ai_mobile_military(g, n)) return 0;
	p = g->military.player[k];
	r = g->military.region[k];
	if (g->air.state[p][r] == KP_RSTATE_GROUP) return 0;
	for (m = record_head(g, p, r); m != NONE; m = g->military.next[m]) {
		int64_t c = g->military.unit[m];
		if (c != n && g->u.order[c] == KP_ORD_TRANSPORT_REQUEST && kp_band(g->u.status[c], STATUS_IN_TRANSPORT) == 0) return 1;
	}
	return 0;
}

/* RejoinAssignedRegion 0x4b0e10: a mobile military unit that stopped outside its region moves back there (HarassMove,
 * a Medic's HealMove). Its status 0x80000 test never stops it. */
void kp_ai_rejoin_region(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t k = record_of(g, n), r, cx, cy;
	int32_t s;
	if (!kp_ai_mobile_military(g, n)) return;
	s = kp_unit_sprite(g, n);
	if (u->move_x[n] != kp_s16v(g->w->spr.x[s]) || u->move_y[n] != kp_s16v(g->w->spr.y[s])) return;
	r = g->military.region[k];
	if (kp_band(kp_unit_region(g, n), 0xffff) == r) return;
	kp_ai_region_center(g, r, &cx, &cy);
	kp_issue_order_at(g, n, u->type[n] == KP_TYPE_MEDIC ? KP_ORD_HEAL_MOVE : KP_ORD_HARASS_MOVE, cx, cy); /* 0x42a340 */
}

/* ---- the gathering of an attack (region states 8 and 9, RegionStateAttackGather 0x4ac870) ---- */

#define STATUS_GROUNDED_BUILDING 0x2
#define GROUPING_SECONDS_SHORT 120
#define TARGET_FAR 9999999

/* 0x4aa8a0: player p has a completed transport (a Shuttle, a Dropship, or an Overlord with Ventral Sacs). */
int kp_ai_has_transports(KpGame *g, int64_t p)
{
	if (g->count_completed[KP_TYPE_SHUTTLE * KP_PLAYERS + p] != 0 || g->count_completed[KP_TYPE_DROPSHIP * KP_PLAYERS + p] != 0) return 1;
	return kp_upgrade_level(g, p, KP_UPG_VENTRAL_SACS) != 0 && g->count_completed[KP_TYPE_OVERLORD * KP_PLAYERS + p] != 0;
}

/* GetGroupWeaponSummary 0x4aaef0: whether region r's military has a ground member, and none with a ground or an air
 * weapon. */
void kp_ai_weapon_summary(KpGame *g, int64_t p, int64_t r, int *has_ground, int *no_ground, int *no_air)
{
	int64_t k;
	*has_ground = 0;
	*no_ground = *no_air = 1;
	for (k = record_head(g, p, r); k != NONE; k = g->military.next[k]) {
		int64_t n = g->military.unit[k];
		if (kp_ground_weapon(g, n) != KP_WEAPON_NONE) *no_ground = 0; /* 0x41fc20 */
		if (kp_air_weapon_with_subunit(g, n) != KP_WEAPON_NONE) *no_air = 0; /* 0x41fb40 */
		if (kp_band(g->u.status[n], STATUS_AIR) == 0) *has_ground = 1;
	}
}

/* ShouldRetargetRegion 0x4ab490: 1 when region src's ground military has no transport and cannot reach region dst,
 * or when dst (another region) holds an enemy src's military can hit with what it has. */
int kp_ai_should_retarget(KpGame *g, int64_t p, int64_t src, int64_t dst)
{
	int64_t *found, head, cx, cy;
	int has_ground, no_ground, no_air, count, i, result = 1;
	kp_ai_weapon_summary(g, p, dst, &has_ground, &no_ground, &no_air);
	if (has_ground && !kp_ai_has_transports(g, p)) {
		head = record_head(g, p, src);
		if (head == NONE) return 1;
		kp_ai_region_center(g, dst, &cx, &cy);
		if (!kp_placement_reachable(g, g->military.unit[head], cx, cy)) return 1; /* 0x48e980 */
	}
	if ((!no_air && !no_ground) || src == dst) return 0;
	found = kp_region_units(g, dst, &count);
	for (i = 0; i < count; i++) {
		int64_t n = found[i];
		int air;
		if (!kp_player_enemy_of(g, p, n)) continue;
		air = kp_band(g->u.status[n], STATUS_AIR) != 0;
		if ((!air && no_air) || (air && no_ground)) {
			result = 0;
			break;
		}
	}
	kp_query_release(g, found);
	return result;
}

/* Whether region r's ground military reaches unit n (an accessible region, with transports or by ground). */
static int ground_reaches(KpGame *g, int64_t p, int64_t r, int64_t n)
{
	int32_t s;
	if (g->pathing.rg.klass[kp_band(kp_unit_region(g, n), 0xffff)] == KP_REGION_UNWALKABLE) return 0;
	if (kp_ai_has_transports(g, p)) return 1;
	s = kp_unit_sprite(g, n);
	return kp_placement_reachable(g, g->military.unit[record_head(g, p, r)], kp_s16v(g->w->spr.x[s]), kp_s16v(g->w->spr.y[s]));
}

/* ChooseGlobalAttackTarget 0x4aba70: the enemy nearest region r's centre that its military can reach and hit (a
 * building before any other unit); else the last enemy standing in a region of player p's AI that is not idle, or the
 * first other one. */
int64_t kp_ai_global_target(KpGame *g, int64_t p, int64_t r)
{
	KpUnits *u = &g->u;
	int64_t n, fallback = 0, building = 0, other = 0, best_building = TARGET_FAR, best_other = TARGET_FAR, cx, cy;
	int has_ground, no_ground, no_air;
	kp_ai_weapon_summary(g, p, r, &has_ground, &no_ground, &no_air);
	if (record_head(g, p, r) == NONE) return 0;
	kp_ai_region_center(g, r, &cx, &cy);
	for (n = g->lists[KP_LIST_ACTIVE].head; n != 0; n = u->next[n]) {
		int64_t t = u->type[n], d;
		if (kp_unit_inactive(g, n) || !kp_player_enemy_of(g, p, n)) continue;
		if (g->air.state[p][kp_band(kp_unit_region(g, n), 0xffff)] != KP_RSTATE_IDLE) {
			fallback = n;
			continue;
		}
		if (t >= KP_TYPE_FIRST_BEACON && t <= KP_TYPE_LAST_BEACON) continue;
		if (fallback == 0) fallback = n;
		if (!has_ground) {
			if (t == KP_TYPE_MISSILE_TURRET || t == KP_TYPE_PHOTON_CANNON || t == KP_TYPE_SPORE_COLONY || t == KP_TYPE_BUNKER) continue;
		} else if (!ground_reaches(g, p, r, n)) {
			continue;
		}
		if (kp_band(u->status[n], STATUS_AIR) != 0 ? no_air : no_ground) continue;
		d = kp_u32v(sprite_distance(g, n, cx, cy));
		if (kp_band(u->status[n], STATUS_GROUNDED_BUILDING) != 0) {
			if (d < best_building) {
				best_building = d;
				building = n;
			}
		} else if (d < best_other) {
			best_other = d;
			other = n;
		}
	}
	if (building != 0) return building;
	if (other != 0) return other;
	if (!has_ground || fallback == 0) return fallback;
	return ground_reaches(g, p, r, fallback) ? fallback : 0;
}

#define GUARD_RECRUIT_RADIUS 256

/* RecruitGuardInRegionCallback 0x4ada40 (native-radius-recruitment.md): a guard of player p standing in region r
 * (outside a building) leaves its guard for r's military. */
static void recruit_guard(KpGame *g, int64_t p, int64_t r, int64_t n)
{
	KpUnits *u = &g->u;
	if (u->ai[n] == 0 || kp_ai_unit_record_type(g, n) != KP_AI_GUARD || kp_band(u->status[n], STATUS_IN_BUILDING) != 0
		|| u->owner[n] != p || kp_unit_region(g, n) != r) {
		return;
	}
	kp_remove_unit_ai(g, n, 0); /* 0x48dbc0 */
	kp_ai_military_add(g, p, r, n, 1); /* 0x4ae760 */
}

#define RANDOM_ENEMY 0x36 /* the RNG source of AttackNearestEnemyOfRandomPlayer's first player */
#define SUICIDE_TIMER 24
#define STATUS_MOVABLE 0x20000
#define SPRITE_HIDDEN 0x20

/* AttackNearestEnemyOfRandomPlayer 0x4abd00: a unit that can move goes for the nearest unit it can attack (any unit
 * without a weapon) of its hostile target, else of a random hostile player's list (the next ones in turn when one
 * gives none), and leaves its AI. */
static void attack_random_enemy(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t starts[KP_AI_PLAYERS], count = 0, first, k, c, best, d, dx, dy, nearest;
	int32_t s = kp_unit_sprite(g, n), cs;
	if (kp_band(u->status[n], STATUS_MOVABLE) == 0 && kp_band(u->status[n], STATUS_BURROWED) == 0) return;
	if (u->order_unit[n] != 0 && kp_is_enemy(g, n, u->order_unit[n])) { /* 0x4290e0 */
		starts[count++] = u->order_unit[n];
	} else {
		for (k = 0; k < KP_AI_PLAYERS; k++) {
			c = g->player_units[k];
			if (c != 0 && kp_is_enemy(g, n, c)) starts[count++] = c;
		}
		if (count == 0) return;
	}
	first = kp_u32v(kp_rng_game_raw(g->rng, RANDOM_ENEMY)) % count;
	for (k = 0; k < count; k++, first++) {
		if (first >= count) first = 0;
		best = 0;
		nearest = 0xffffffff;
		for (c = starts[first]; c != 0; c = u->player_next[c]) {
			if (kp_unit_inactive(g, c)) continue; /* 0x41fe40 */
			if (kp_unit_has_weapon(g, n) && !kp_can_attack(g, n, c, 1)) continue; /* 0x41d9c0, 0x41da50 */
			cs = kp_unit_sprite(g, c);
			dx = kp_s16v(g->w->spr.x[cs]) - kp_s16v(g->w->spr.x[s]);
			dy = kp_s16v(g->w->spr.y[cs]) - kp_s16v(g->w->spr.y[s]);
			d = kp_u32v(dx * dx + dy * dy);
			if (d >= nearest) continue;
			nearest = d;
			best = c;
		}
		if (best == 0) continue;
		if (kp_unit_has_weapon(g, n)) kp_issue_attack(g, n, best, 0, 0); /* IssueAttackOrder 0x4202c0 */
		else {
			cs = kp_unit_sprite(g, best);
			kp_issue_order_at(g, n, KP_ORD_MOVE, kp_s16v(g->w->spr.x[cs]), kp_s16v(g->w->spr.y[cs]));
		}
		kp_remove_unit_ai(g, n, 0); /* 0x48dbc0 */
		return;
	}
}

/* SendSuicide 0x4b11b0. With a mode, every unit of player p that can fight (not a subunit, missile or Larva, shown and
 * not threatened by its target) goes for a random enemy outside the AI. Without one (and no suicide pending), the
 * computer's units (outside transports; in buildings only a campaign AI's; guards first joining their region's
 * military) that are not moving, grouping or attacking join one attack group: the first prepares the attack at its
 * position (a refused one is skipped), which starts the timer +0x22c. */
void kp_ai_send_suicide(KpGame *g, int64_t p, int mode)
{
	KpUnits *u = &g->u;
	int64_t n, group = NONE, state;
	if (mode) {
		for (n = g->player_units[p]; n != 0; n = u->player_next[n]) {
			if (kp_unit_inactive(g, n) || kp_band(KP_DAT(g, units, special_ability_flags, u->type[n]), DAT_SUBUNIT) != 0
				|| u->type[n] == KP_TYPE_NUCLEAR_MISSILE || u->type[n] == KP_TYPE_LARVA
				|| kp_band(g->w->spr.flags[kp_unit_sprite(g, n)], SPRITE_HIDDEN) != 0 || kp_ai_threatened_by(g, n, 0)) {
				continue;
			}
			attack_random_enemy(g, n);
		}
		return;
	}
	if (g->pai.countdown0[p] != 0) return; /* 0x483380 */
	for (n = g->player_units[p]; n != 0; n = u->player_next[n]) {
		int64_t kind, mp, mr;
		if (kp_unit_inactive(g, n) || u->ai[n] == 0 || kp_is_transport(g, n)) continue; /* 0x4381f0 */
		if (kp_band(u->status[n], STATUS_IN_BUILDING) != 0 && !kp_ai_campaign(g, p)) continue;
		kind = kp_ai_unit_record_type(g, n);
		if (kind == KP_AI_GUARD) { /* the guard keeps its record without its unit */
			int64_t k = kp_ai_ref_index(u->ai[n]);
			u->ai[n] = 0;
			kp_ai_military_add(g, p, kp_band(kp_unit_region(g, n), 0xffff), n, 1);
			g->guards.unit[k] = 0;
		} else if (kind != KP_AI_MILITARY) {
			continue;
		}
		if (kp_ai_military_record(g, n, &mp, &mr) != NONE) {
			state = g->air.state[mp][mr];
			if (state == KP_RSTATE_MOVE || state == KP_RSTATE_GROUP || state == KP_RSTATE_GATHER || state == KP_RSTATE_ATTACK) continue;
		}
		if (group == NONE) {
			int32_t s = kp_unit_sprite(g, n);
			kp_ai_attack_clear(g, p, 0); /* 0x483770 */
			if (kp_ai_prepare_attack(g, p, kp_s16v(g->w->spr.x[s]), kp_s16v(g->w->spr.y[s]), 0, 1)) continue; /* 0x4af300 */
			g->pai.countdown0[p] = SUICIDE_TIMER; /* 0x4834b0 */
			group = g->pai.attack_target[p] - 1; /* 0x483800 */
		}
		kp_remove_unit_ai(g, n, 0); /* 0x48dbc0 */
		kp_ai_military_add(g, p, group, n, 1); /* 0x4ae760 */
	}
}

#define EVALUATION_SECONDS 360
#define EVALUATION_LATE_SECONDS 2700
#define EVALUATION_STRONG 6000
#define ALLIED_VICTORY 2 /* an alliance byte: allied for the victory */
#define DAT_WORKER_FLAG 0x8

/* EvaluateGlobalAttackOpportunity 0x4acdf0 (outside the campaigns: any campaign AI stops it): a census of the active
 * units' ground strength per player: its own, its allied-for-victory team's fighting units, all its enemies' (either
 * side not allied), and the strongest team that an enemy of each player fields. A computer stronger than that rival
 * stops requesting ordinary units (KP_AIF_SUPPRESS_UNITS set; clear when it is not stronger); a strong team (or after
 * 45 minutes) twice its enemies' strength sends all its units (SendSuicide mode 0, a grouping without time). */
void kp_ai_evaluate_attacks(KpGame *g)
{
	KpUnits *u = &g->u;
	uint32_t own[KP_AI_PLAYERS] = { 0 }, team[KP_AI_PLAYERS] = { 0 }, hostile[KP_AI_PLAYERS] = { 0 };
	uint32_t rival[KP_AI_PLAYERS] = { 0 };
	int64_t n, o, q, r, s;
	if (kp_u32v(kp_scalar(g, KP_ELAPSED)) < EVALUATION_SECONDS) return;
	for (q = 0; q < KP_AI_PLAYERS; q++) {
		if (kp_ai_campaign(g, q)) return;
	}
	for (n = g->lists[KP_LIST_ACTIVE].head; n != 0; n = u->next[n]) {
		if (kp_unit_inactive(g, n) || u->owner[n] >= KP_AI_PLAYERS) continue;
		o = u->owner[n];
		s = kp_band(u->ground_strength[n], 0xffff);
		own[o] += (uint32_t)s;
		if (kp_band(KP_DAT(g, units, special_ability_flags, u->type[n]), DAT_BUILDING | DAT_WORKER_FLAG) == 0) {
			for (q = 0; q < KP_AI_PLAYERS; q++) {
				if (q == o || g->players.alliance[o][q] == ALLIED_VICTORY) team[q] += (uint32_t)s;
			}
		}
		for (q = 0; q < KP_AI_PLAYERS; q++) {
			if (g->players.alliance[q][o] == 0 || g->players.alliance[o][q] == 0) hostile[q] += (uint32_t)s;
		}
	}
	for (r = 0; r < KP_AI_PLAYERS; r++) {
		for (q = 0; q < KP_AI_PLAYERS; q++) {
			if (g->players.alliance[r][q] == 0 && team[r] > rival[q]) rival[q] = team[r];
		}
	}
	for (q = 0; q < KP_AI_PLAYERS; q++) {
		if (kp_controller(g, q) != KP_CTRL_COMPUTER) continue;
		g->pai.flags[q] = rival[q] < own[q] ? kp_bor(g->pai.flags[q], KP_AIF_SUPPRESS_UNITS) /* 0x482cb0 */
		                                    : kp_band(g->pai.flags[q], kp_bnot(KP_AIF_SUPPRESS_UNITS));
		if ((team[q] >= EVALUATION_STRONG || kp_u32v(kp_scalar(g, KP_ELAPSED)) >= EVALUATION_LATE_SECONDS) && hostile[q] != 0
			&& team[q] > (uint32_t)(hostile[q] * 2)) {
			kp_ai_send_suicide(g, q, 0); /* 0x4b11b0 */
			kp_ai_start_grouping(g, q, 0); /* 0x483ad0 */
		}
	}
}

/* TransferRegionMilitary 0x4b15d0: region src's military (but casters unless included, and a non-campaign unit in a
 * building when restricted) moves to region dst, which also takes src's detector when it has none; with recruit_guards
 * the guards within GUARD_RECRUIT_RADIUS of dst's centre join it too. */
void kp_ai_transfer_military(KpGame *g, int64_t p, int64_t src, int64_t dst, int recruit_guards, int casters,
	int restrict_loaded)
{
	KpAiRegions *a = &g->air;
	int64_t k = record_head(g, p, src);
	if (a->detector[p][dst] == 0) {
		a->detector[p][dst] = a->detector[p][src];
		a->detector[p][src] = 0;
	}
	while (k != NONE) {
		int64_t next = g->military.next[k], n = g->military.unit[k];
		int keep = (!casters && kp_ai_military_caster(g->u.type[n]))
			|| (restrict_loaded && kp_band(g->u.status[n], STATUS_IN_BUILDING) != 0 && !kp_ai_campaign(g, g->u.owner[n]));
		if (!keep) {
			kp_ai_military_release(g, k);
			g->u.ai[n] = 0;
			kp_ai_military_add(g, p, dst, n, 1);
		}
		if (record_head(g, p, src) == NONE) break;
		k = next;
	}
	if (recruit_guards) { /* FindUnitInRadiusBox 0x442b20 around the destination's centre */
		int64_t x, y, *found;
		int count, i;
		kp_ai_region_center(g, dst, &x, &y); /* GetRegionCenter 0x481a30 */
		found = kp_query_rect(g, kp_s16v(x - GUARD_RECRUIT_RADIUS), kp_s16v(y - GUARD_RECRUIT_RADIUS), kp_s16v(x + GUARD_RECRUIT_RADIUS),
			kp_s16v(y + GUARD_RECRUIT_RADIUS), &count);
		for (i = 0; i < count; i++) recruit_guard(g, p, dst, found[i]);
		kp_query_release(g, found);
	}
}

/* DisperseRegionMilitary 0x4b1500: region r's military returns to the regions its members stand in. */
void kp_ai_disperse_military(KpGame *g, int64_t p, int64_t r)
{
	int64_t k = record_head(g, p, r);
	while (k != NONE) {
		int64_t next = g->military.next[k], n = g->military.unit[k], q = g->u.owner[n];
		kp_ai_military_release(g, k);
		g->u.ai[n] = 0;
		kp_ai_military_add(g, q, kp_band(kp_unit_region(g, n), 0xffff), n, 1);
		if (record_head(g, p, r) == NONE) break;
		k = next;
	}
}

/* CheckAttackGroupingTimeout 0x483940: the grouping (180 seconds for a campaign AI, else 120) has run out; its time
 * clears. */
int kp_ai_grouping_timeout(KpGame *g, int64_t p)
{
	int64_t time = g->pai.grouping_time[p];
	if (time == 0) return 0;
	if (kp_u32v(kp_scalar(g, KP_ELAPSED) - time) < (kp_ai_campaign(g, p) ? GROUPING_SECONDS : GROUPING_SECONDS_SHORT)) return 0;
	g->pai.grouping_time[p] = 0;
	return 1;
}

/* HasStragglingAttacker 0x4ab150: a unit of player p in an attacking region's military, without an enemy order target
 * or an auto target, stands outside its region. */
int kp_ai_straggling_attacker(KpGame *g, int64_t p)
{
	KpUnits *u = &g->u;
	int64_t n;
	for (n = g->player_units[p]; n != 0; n = u->player_next[n]) {
		int64_t mp, mr, state;
		if (kp_unit_inactive(g, n) || kp_ai_military_record(g, n, &mp, &mr) == NONE) continue;
		state = g->air.state[mp][mr];
		if (state != KP_RSTATE_GATHER && state != KP_RSTATE_ATTACK) continue;
		if (u->order_unit[n] != 0 && kp_is_enemy(g, n, u->order_unit[n])) continue; /* 0x4290e0 */
		if (u->auto_target[n] != 0) continue;
		if (kp_band(kp_unit_region(g, n), 0xffff) != mr) return 1;
	}
	return 0;
}

/* Whether region r's grouping still misses a completed roster unit (GetMissingRosterUnits 0x4aa500). */
int kp_ai_roster_missing(KpGame *g, int64_t p, int64_t r)
{
	int64_t missing[KP_AI_ROSTER], count = missing_roster(g, p, r, missing, 1), k;
	for (k = 0; k < count; k++) {
		if (missing[k] < KP_TYPE_NONE) return 1;
	}
	return 0;
}
