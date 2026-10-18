/* The computer's reaction spells (kp_game.h), ported from the atlas (P13, P28; docs/research/native-combat-ai.md,
 * native-reaction-spells.md): TryAiReactionSpell 0x48d3a0 with the readiness of a spell 0x48e580 and the reservation of
 * its target 0x48e5c0, the target selectors 0x432bf0-0x433240 and their candidate tests. */
#include "kp_game_internal.h"

#define NO_WEAPON 130
#define NO_TYPE 228
#define NEUTRAL_PLAYERS 8
#define DAT_BUILDING 0x1
#define DAT_WORKER 0x8
#define DAT_SUBUNIT 0x10
#define DAT_HERO 0x40
#define DAT_ORGANIC 0x10000
#define DAT_SPELLCASTER 0x200000
#define STATUS_GROUNDED_BUILDING 0x2
#define STATUS_AIR 0x4
#define STATUS_CLOAKED 0x100
#define STATUS_CLOAK_BITS 0x300
#define STATUS_DISABLED 0x400
#define STATUS_CANNOT_ATTACK 0x8000
#define STATUS_CAN_MOVE 0x20000
#define STATUS_INVINCIBLE 0x4000000
#define STATUS_HALLUCINATION 0x40000000
#define MOVING 0x2 /* movement flag */
#define QUEEN_RANDOM 42
#define REACTIVE_RADIUS 288
#define SCORE_RADIUS 96
#define EMP_SCORE_RADIUS 160
#define NUKE_SCORE_RADIUS 192
#define NUKE_SCORE_THRESHOLD 800
#define SUNKEN_COLONY 146
#define LURKER 103 /* never a building: its test in 0x432150 never matches */
#define SCORE_THRESHOLD 250
#define EMP_THRESHOLD 200
#define REGION_ABANDONED 3
#define REGION_TARGETED 0x20
#define ORDER_MOVE 6
#define RECALL_SHIELDS 0xa00
/* the weapons of the spells */
#define W_LOCKDOWN 32
#define W_EMP 33
#define W_IRRADIATE 34
#define W_PARASITE 56
#define W_BROODLINGS 57
#define W_ENSNARE 58
#define W_PLAGUE 60
#define W_STASIS 83
#define W_STORM 84
#define W_RESTORATION 102
#define W_MAELSTROM 108
/* the spell orders and their techs */
#define O_YAMATO 0x71
#define O_LOCKDOWN 0x73
#define O_DARK_SWARM 0x77
#define O_PARASITE 0x78
#define O_BROODLINGS 0x79
#define O_EMP 0x7a
#define O_NUKE 0x7e
#define O_RECALL 0x89
#define O_MATRIX 0x8d
#define O_STORM 0x8e
#define O_IRRADIATE 0x8f
#define O_PLAGUE 0x90
#define O_ENSNARE 0x92
#define O_STASIS 0x93
#define O_HALLUCINATION 0x94
#define O_RESTORATION 0xb4
#define O_WEB 0xb5
#define O_MIND_CONTROL 0xb6
#define O_FEEDBACK 0xb8
#define O_FLARE 0xb9
#define O_MAELSTROM 0xba
#define T_MAELSTROM 31
/* unit types */
#define DROPSHIP 11
#define SHUTTLE 69
#define CARRIER 72
#define REAVER 83
#define GANTRITHOR 82
#define WARBRINGER 81
#define SCOUT 70
#define ARBITER 71
#define ARCHON 68
#define MISSILE_TURRET 124
#define BUNKER 125
#define SPORE_COLONY 144
#define PHOTON_CANNON 162
#define EGG 36

static int64_t flags(KpGame *g, int64_t n) { return KP_DAT(g, units, special_ability_flags, g->u.type[n]); }

static int campaign(KpGame *g, int64_t n) { return kp_ai_campaign(g, g->u.owner[n]); } /* IsCampaignAi 0x4831e0 */

static int moving(KpGame *g, int64_t n) { return kp_band(g->u.movement_flags[n], MOVING) != 0; }

static int64_t rounded_hp(KpGame *g, int64_t n) { return kp_sarv(kp_s32v(g->u.hp[n]) + 0xff, 8); }

/* The candidate's order target is a unit of a player that is not the caster's enemy. */
static int attacks_friend(KpGame *g, int64_t n, int64_t c)
{
	int64_t t = g->u.order_unit[c];
	return t != 0 && g->u.owner[t] < NEUTRAL_PLAYERS && !kp_is_enemy(g, n, t);
}

/* A transport carrying units: a Shuttle or a Dropship with cargo. */
static int loaded_transport(KpGame *g, int64_t c)
{
	return (g->u.type[c] == SHUTTLE || g->u.type[c] == DROPSHIP) && kp_has_cargo(g, c);
}

/* IsVisibleEnemyTarget 0x433df0: c is a visible (and detected) enemy of caster n that is not a missile or a unit
 * entering a transport. */
static int visible_enemy(KpGame *g, int64_t n, int64_t c)
{
	KpUnits *u = &g->u;
	int64_t bit = (int64_t)1 << u->owner[n], t = u->type[c], o = u->order[c];
	if (kp_band(g->w->spr.visibility[kp_unit_sprite(g, c)], bit) == 0) return 0;
	if (kp_band(u->status[c], STATUS_CLOAK_BITS) != 0 && kp_band(u->visibility[c], bit) == 0) return 0;
	if (!kp_is_enemy(g, n, c)) return 0;
	return t != 73 && t != 13 && t != 85 && o != 0x51 && o != 0x52 && o != 0x53;
}

/* The area scores of 0x442b20 around c: the enemies' life (0x432090, or 0x4321e0 for Maelstrom's organic targets)
 * must exceed twice the allies' (0x432100), and reach 250 unless the selector was reactive (0x665d30). */
static int area_favorable(KpGame *g, int64_t n, int64_t c, int64_t weapon)
{
	KpUnits *u = &g->u;
	int32_t s = kp_unit_sprite(g, c);
	int64_t x = g->w->spr.x[s], y = g->w->spr.y[s], *found, enemy = 0, allied = 0;
	int count, k;
	found = kp_query_rect(g, kp_s16v(x - SCORE_RADIUS), kp_s16v(y - SCORE_RADIUS), kp_s16v(x + SCORE_RADIUS),
		kp_s16v(y + SCORE_RADIUS), &count);
	for (k = 0; k < count; k++) {
		int64_t m = found[k];
		if (m == n || kp_band(u->status[m], STATUS_INVINCIBLE) != 0 || !kp_weapon_can_target(g, n, weapon, m)
			|| !kp_is_enemy(g, n, m)) {
			continue;
		}
		if (weapon == W_MAELSTROM) {
			if (kp_band(flags(g, m), DAT_ORGANIC) == 0 || u->maelstrom_timer[m] != 0) continue;
			enemy += kp_current_life(g, m);
		} else {
			enemy += weapon == W_PLAGUE ? rounded_hp(g, m) : kp_current_life(g, m);
		}
	}
	kp_query_release(g, found);
	enemy = kp_u32v(enemy);
	if (g->ai.spell_reactive == 0 && enemy < SCORE_THRESHOLD) return 0;
	found = kp_query_rect(g, kp_s16v(x - SCORE_RADIUS), kp_s16v(y - SCORE_RADIUS), kp_s16v(x + SCORE_RADIUS),
		kp_s16v(y + SCORE_RADIUS), &count);
	for (k = 0; k < count; k++) {
		int64_t m = found[k];
		if (kp_band(u->status[m], STATUS_INVINCIBLE) != 0 || !kp_weapon_can_target(g, n, weapon, m) || kp_is_enemy(g, n, m)) {
			continue;
		}
		allied += kp_current_life(g, m);
	}
	kp_query_release(g, found);
	return kp_u32v(allied * 2) < enemy;
}

/* The EMP scores within 160 pixels of c (0x433cd0, 0x433410): the enemies' shields or casters' energy reach 200. */
static int emp_favorable(KpGame *g, int64_t n, int64_t c, int energy)
{
	KpUnits *u = &g->u;
	int32_t s = kp_unit_sprite(g, c);
	int64_t x = g->w->spr.x[s], y = g->w->spr.y[s], *found, sum = 0;
	int count, k;
	found = kp_query_rect(g, kp_s16v(x - EMP_SCORE_RADIUS), kp_s16v(y - EMP_SCORE_RADIUS), kp_s16v(x + EMP_SCORE_RADIUS),
		kp_s16v(y + EMP_SCORE_RADIUS), &count);
	for (k = 0; k < count; k++) {
		int64_t m = found[k];
		if (kp_band(u->status[m], STATUS_INVINCIBLE) != 0 || !kp_is_enemy(g, n, m)) continue;
		if (energy) {
			if (kp_band(flags(g, m), DAT_SPELLCASTER) != 0 && kp_band(u->status[m], STATUS_HALLUCINATION) == 0) {
				sum += kp_shrv(kp_mod(u->energy[m], 65536), 8);
			}
		} else if (KP_DAT(g, units, shield_enabled, u->type[m]) != 0) {
			sum += kp_sarv(kp_s32v(u->shields[m]), 8);
		}
	}
	kp_query_release(g, found);
	return kp_u32v(sum) >= EMP_THRESHOLD;
}

/* 0x432650: Spawn Broodlings. */
static int brood_target(KpGame *g, int64_t c, int64_t n)
{
	int64_t f = flags(g, c), t = g->u.type[c], o = g->u.order[c];
	if (!visible_enemy(g, n, c) || !kp_weapon_can_target(g, n, W_BROODLINGS, c) || kp_band(f, DAT_HERO) != 0) return 0;
	if (g->ai.spell_reactive != 0 || kp_current_life(g, c) >= 100 || kp_band(f, DAT_WORKER) != 0) return 1;
	if (t == 30 || t == 5 || t == 34) return 1;
	return o >= 123 && o <= 129;
}

/* 0x4327e0: Restoration of an own unit under an effect. */
static int restoration_target(KpGame *g, int64_t c, int64_t n)
{
	KpUnits *u = &g->u;
	if (u->owner[c] != u->owner[n] || !kp_weapon_can_target(g, n, W_RESTORATION, c) || kp_current_life(g, c) <= 60) return 0;
	return u->ensnare_timer[c] != 0 || u->plague_timer[c] != 0 || u->irradiate_timer[c] != 0 || u->lockdown_timer[c] != 0
		|| u->maelstrom_timer[c] != 0 || u->acid_spores[c] != 0;
}

/* 0x433840: a friend of the free Medic carrying a Parasite or held by a Maelstrom (SelectRestorationCleanseTarget
 * 0x433000's map-wide search). */
static int cleanse_target(KpGame *g, int64_t c, int64_t n)
{
	KpUnits *u = &g->u;
	if (u->owner[c] != u->owner[n] || (u->parasites[c] == 0 && u->maelstrom_timer[c] == 0)) return 0;
	return kp_weapon_can_target(g, n, W_RESTORATION, c);
}

/* 0x4328d0: Defensive Matrix on a wounded friend that fights. */
static int matrix_target(KpGame *g, int64_t c, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t max;
	int32_t s;
	if (u->matrix_damage[c] != 0 || kp_is_enemy(g, n, c) || u->order_unit[c] == 0
		|| kp_band(u->status[c], STATUS_CLOAKED) != 0) {
		return 0;
	}
	/* GetMaxHpWhole 0x409c90 */
	max = kp_sarv(kp_s32v(KP_DAT(g, units, hit_points, u->type[c])), 8);
	if (max == 0) max = rounded_hp(g, c) != 0 ? rounded_hp(g, c) : 1;
	if (max < 100 || kp_s32v(kp_band(u->hp[c] + 0xff, 0xffffff00)) > 0x4b00) return 0;
	s = kp_unit_sprite(g, c);
	return kp_validate_spell_target(g, n, 6, c, NO_TYPE, kp_s16v(g->w->spr.x[s]), kp_s16v(g->w->spr.y[s]), NULL);
}

/* 0x4326e0: Disruption Web over an enemy that attacks a friend's air unit, or over a defense. */
static int web_target(KpGame *g, int64_t c, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t t = u->type[c];
	if (!visible_enemy(g, n, c)) return 0;
	if (t == BUNKER && kp_has_cargo(g, c)) {
		int64_t f = kp_first_cargo(g, c), a;
		if (f != 0) {
			a = u->order_unit[f];
			if (a == 0 || u->owner[a] >= NEUTRAL_PLAYERS || kp_band(u->status[a], STATUS_AIR) == 0 || kp_is_enemy(g, n, a)) {
				return 0;
			}
		}
	} else {
		int64_t a = u->order_unit[c];
		if (a == 0 || u->owner[a] >= NEUTRAL_PLAYERS || kp_band(u->status[a], STATUS_AIR) == 0 || kp_is_enemy(g, n, a)) {
			return 0;
		}
	}
	if (kp_band(u->status[c], STATUS_CANNOT_ATTACK) != 0) return 0;
	if (kp_band(u->status[c], STATUS_AIR) == 0 && kp_unit_box_within(g, n, 0x100, c)) return 1;
	return t == MISSILE_TURRET || t == BUNKER || t == SPORE_COLONY || t == PHOTON_CANNON;
}

/* 0x4329e0: EMP on shields. */
static int emp_shield_target(KpGame *g, int64_t c, int64_t n)
{
	if (!visible_enemy(g, n, c) || KP_DAT(g, units, shield_enabled, g->u.type[c]) == 0
		|| !kp_weapon_can_target(g, n, W_EMP, c)) {
		return 0;
	}
	return emp_favorable(g, n, c, 0);
}

/* 0x432950: EMP on energy. */
static int emp_energy_target(KpGame *g, int64_t c, int64_t n)
{
	if (!visible_enemy(g, n, c) || kp_band(flags(g, c), DAT_SPELLCASTER) == 0
		|| kp_band(g->u.status[c], STATUS_HALLUCINATION) != 0 || !kp_weapon_can_target(g, n, W_EMP, c)) {
		return 0;
	}
	return emp_favorable(g, n, c, 1);
}

/* 0x432ac0: Ensnare. */
static int ensnare_target(KpGame *g, int64_t c, int64_t n)
{
	KpUnits *u = &g->u;
	if (!visible_enemy(g, n, c) || !kp_weapon_can_target(g, n, W_ENSNARE, c) || u->ensnare_timer[c] != 0
		|| kp_band(u->status[c], STATUS_CAN_MOVE) == 0 || !attacks_friend(g, n, c)) {
		return 0;
	}
	return area_favorable(g, n, c, W_ENSNARE);
}

/* 0x433260: Hallucination of an own capital ship or Archon. */
static int hallucination_target(KpGame *g, int64_t c, int64_t n)
{
	int64_t t = g->u.type[c];
	int32_t s;
	if (g->u.owner[c] != g->u.owner[n]) return 0;
	s = kp_unit_sprite(g, c);
	if (!kp_validate_spell_target(g, n, 20, c, NO_TYPE, kp_s16v(g->w->spr.x[s]), kp_s16v(g->w->spr.y[s]), NULL)) return 0;
	return t == CARRIER || t == SCOUT || t == ARBITER || t == ARCHON;
}

/* 0x4332f0: Irradiate (with IsIrradiatable 0x438b50). */
static int irradiate_target(KpGame *g, int64_t c, int64_t n)
{
	int64_t f = flags(g, c), t = g->u.type[c];
	if (!visible_enemy(g, n, c) || g->u.irradiate_timer[c] != 0 || !kp_weapon_can_target(g, n, W_IRRADIATE, c)) return 0;
	if (kp_band(f, DAT_ORGANIC) == 0 || kp_band(f, DAT_BUILDING) != 0 || t == 35 || t == 36 || t == 97) return 0;
	if (g->ai.spell_reactive != 0 || !campaign(g, n)) return 1;
	return kp_band(f, DAT_WORKER) != 0 || t == 42 || t == 34;
}

/* 0x433360: Lockdown. */
static int lockdown_target(KpGame *g, int64_t c, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t t = u->type[c];
	if (!visible_enemy(g, n, c) || !kp_weapon_can_target(g, n, W_LOCKDOWN, c) || kp_band(u->status[c], STATUS_DISABLED) != 0
		|| u->lockdown_timer[c] != 0 || u->stasis_timer[c] != 0 || u->maelstrom_timer[c] != 0) {
		return 0;
	}
	if ((t == SHUTTLE || t == DROPSHIP) && moving(g, c) && kp_has_cargo(g, c)) return 1;
	if (t == CARRIER || t == REAVER) return 1;
	return attacks_friend(g, n, c);
}

/* 0x433460: Mind Control of a valuable enemy. */
static int mind_control_target(KpGame *g, int64_t c, int64_t n)
{
	int64_t t = g->u.type[c];
	if (!visible_enemy(g, n, c) || kp_band(flags(g, c), DAT_HERO) != 0) return 0;
	if (loaded_transport(g, c)) return 1;
	switch (t) {
	case 5: case 9: case 12: case 30: case 34: case 39: case 42: case 45: case 46: case 62: case 63: case 67: case 71:
	case 72: case 83: case 103:
		return 1;
	}
	/* at least the mineral and gas cost of a Dark Templar (61) */
	return KP_DAT(g, units, mineral_cost, t) >= KP_DAT(g, units, mineral_cost, 61)
		&& KP_DAT(g, units, vespene_cost, t) >= KP_DAT(g, units, vespene_cost, 61);
}

/* 0x433550: Optical Flare. */
static int flare_target(KpGame *g, int64_t c, int64_t n)
{
	if (!visible_enemy(g, n, c) || kp_band(flags(g, c), DAT_BUILDING) != 0 || g->u.blind[c] != 0) return 0;
	return kp_is_detector(g, c) || kp_current_life(g, c) > 80;
}

/* 0x4336b0: Maelstrom. */
static int maelstrom_target(KpGame *g, int64_t c, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t t = u->type[c];
	if (!visible_enemy(g, n, c) || !kp_weapon_can_target(g, n, W_MAELSTROM, c) || kp_band(flags(g, c), DAT_ORGANIC) == 0
		|| u->maelstrom_timer[c] != 0) {
		return 0;
	}
	if (t == 42 && moving(g, c) && kp_has_cargo(g, c)) return 1;
	if (kp_band(flags(g, c), DAT_BUILDING) != 0 || !attacks_friend(g, n, c)) return 0;
	if (t == 35 || t == 36 || t == 59 || t == 97) return 0;
	return area_favorable(g, n, c, W_MAELSTROM);
}

/* 0x433870: Parasite. */
static int parasite_target(KpGame *g, int64_t c, int64_t n)
{
	KpUnits *u = &g->u;
	if (!visible_enemy(g, n, c)) return 0;
	if (campaign(g, n) && u->parasites[c] != 0) return 0;
	if (kp_band(u->parasites[c], (int64_t)1 << u->owner[n]) != 0) return 0;
	if (!campaign(g, n) && !kp_is_detector(g, c) && !kp_is_transport(g, c)
		&& !(kp_band(flags(g, c), DAT_SPELLCASTER) != 0 && kp_band(u->status[c], STATUS_HALLUCINATION) == 0)
		&& kp_band(flags(g, c), DAT_WORKER) == 0 && kp_current_life(g, c) < 300) {
		return 0;
	}
	return kp_weapon_can_target(g, n, W_PARASITE, c);
}

/* 0x433a50: Plague. */
static int plague_target(KpGame *g, int64_t c, int64_t n)
{
	if (!visible_enemy(g, n, c) || !kp_weapon_can_target(g, n, W_PLAGUE, c) || g->u.plague_timer[c] != 0
		|| kp_band(flags(g, c), DAT_HERO) != 0) {
		return 0;
	}
	return area_favorable(g, n, c, W_PLAGUE);
}

/* 0x433b10: Psionic Storm. */
static int storm_target(KpGame *g, int64_t c, int64_t n)
{
	if (!visible_enemy(g, n, c) || !kp_weapon_can_target(g, n, W_STORM, c)) return 0;
	return area_favorable(g, n, c, W_STORM);
}

/* 0x433bb0: Feedback on a caster whose energy outweighs its life. */
static int feedback_target(KpGame *g, int64_t c, int64_t n)
{
	KpUnits *u = &g->u;
	if (!visible_enemy(g, n, c) || kp_band(flags(g, c), DAT_SPELLCASTER) == 0
		|| kp_band(u->status[c], STATUS_HALLUCINATION | STATUS_GROUNDED_BUILDING) != 0 || kp_band(flags(g, c), DAT_HERO) != 0) {
		return 0;
	}
	return kp_shrv(kp_mod(u->energy[c], 65536), 8) >= kp_current_life(g, c);
}

/* 0x433d10: Stasis Field. */
static int stasis_target(KpGame *g, int64_t c, int64_t n)
{
	if (!visible_enemy(g, n, c) || !kp_weapon_can_target(g, n, W_STASIS, c) || kp_band(flags(g, c), DAT_BUILDING) != 0
		|| !attacks_friend(g, n, c)) {
		return 0;
	}
	return area_favorable(g, n, c, W_STASIS);
}

/* 0x433e60: Dark Swarm over a ranged enemy that shoots a friend's melee unit in the open. */
static int swarm_target(KpGame *g, int64_t c, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t victim, w, shooter;
	if (!visible_enemy(g, n, c) || !attacks_friend(g, n, c)) return 0;
	victim = u->order_unit[c];
	if (u->subunit[victim] != 0 || kp_band(u->status[victim], STATUS_AIR) != 0) return 0;
	w = kp_ground_weapon(g, victim);
	if (w == NO_WEAPON || KP_DAT(g, weapons, weapon_behavior, w) != 5) return 0;
	shooter = u->subunit[c];
	if (shooter == 0 || kp_band(flags(g, shooter), DAT_SUBUNIT) == 0) shooter = c; /* IsSubunit 0x420000 */
	w = kp_ground_weapon(g, shooter);
	if (w == NO_WEAPON) return 0;
	switch (KP_DAT(g, weapons, weapon_behavior, w)) {
	case 0: case 1: case 2: case 4: case 7:
		return kp_miss_threshold(g, shooter, victim) == 0;
	}
	return 0;
}

/* 0x433f40: the Yamato Gun. */
static int yamato_target(KpGame *g, int64_t c, int64_t n)
{
	int64_t t = g->u.type[c], life;
	if (!visible_enemy(g, n, c)) return 0;
	if (kp_band(g->u.status[c], STATUS_GROUNDED_BUILDING) != 0 && kp_unit_has_weapon(g, c)) return 1;
	if (t == BUNKER) {
		if (kp_has_cargo(g, c)) return 1;
	} else if (t == EGG) {
		return 0;
	}
	life = kp_u32v(kp_current_life(g, c) - 200);
	return life <= 250 && kp_band(flags(g, c), DAT_HERO) == 0;
}

/* 0x433c10: Recall of an own Carrier or Reaver that fights with its shields down. */
static int recall_target(KpGame *g, int64_t c, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t t = u->type[c];
	if (u->owner[c] != u->owner[n] || u->order_unit[c] == 0) return 0;
	if (t != CARRIER && t != GANTRITHOR && t != REAVER && t != WARBRINGER) return 0;
	return kp_s32v(kp_band(u->shields[c], 0xffffff00)) <= RECALL_SHIELDS;
}

static int64_t around(KpGame *g, int64_t n, int64_t radius, KpUnitFilter f) { return kp_nearest_unit_around(g, n, radius, f, n); }

static int64_t map_wide(KpGame *g, int64_t n, KpUnitFilter f) { return kp_nearest_unit_map(g, n, f, n); }

/* The reactive window, else a wider one that the campaign flag doubles. */
static int64_t window(KpGame *g, int64_t n, int reactive, int64_t wide, int doubles)
{
	if (reactive) return REACTIVE_RADIUS;
	return doubles && campaign(g, n) ? wide * 2 : wide;
}

typedef int64_t (*Selector)(KpGame *g, int64_t n, int reactive);

static int64_t select_broodlings(KpGame *g, int64_t n, int r) /* 0x432bf0 */
{
	g->ai.spell_reactive = r;
	return around(g, n, window(g, n, r, 2048, 0), brood_target);
}
static int64_t select_restoration(KpGame *g, int64_t n, int r) { (void)r; return around(g, n, 1024, restoration_target); }
static int64_t select_matrix(KpGame *g, int64_t n, int r) { return around(g, n, window(g, n, r, 2048, 0), matrix_target); }
static int64_t select_web(KpGame *g, int64_t n, int r) { return around(g, n, window(g, n, r, 2048, 1), web_target); }
static int64_t select_emp(KpGame *g, int64_t n, int r) /* 0x432d00 */
{
	int64_t radius = window(g, n, r, 2048, 0), c = around(g, n, radius, emp_shield_target);
	if (c == 0 && !r) c = around(g, n, radius, emp_energy_target);
	return c;
}
static int64_t select_ensnare(KpGame *g, int64_t n, int r) { return around(g, n, window(g, n, r, 1024, 1), ensnare_target); }
static int64_t select_hallucination(KpGame *g, int64_t n, int r) { return r ? 0 : around(g, n, 1024, hallucination_target); }
static int64_t select_irradiate(KpGame *g, int64_t n, int r) /* 0x432dc0 */
{
	g->ai.spell_reactive = r;
	return around(g, n, window(g, n, r, 2048, 0), irradiate_target);
}
static int64_t select_lockdown(KpGame *g, int64_t n, int r) { return around(g, n, window(g, n, r, 1024, 1), lockdown_target); }
static int64_t select_mind_control(KpGame *g, int64_t n, int r) { (void)r; return around(g, n, 1024, mind_control_target); }
static int64_t select_flare(KpGame *g, int64_t n, int r) { return around(g, n, window(g, n, r, 1024, 0), flare_target); }
static int64_t select_maelstrom(KpGame *g, int64_t n, int r) /* 0x432fa0 */
{
	g->ai.spell_reactive = r;
	return around(g, n, window(g, n, r, 1024, 1), maelstrom_target);
}
static int64_t select_parasite(KpGame *g, int64_t n, int r) { return around(g, n, window(g, n, r, 2048, 0), parasite_target); }
static int64_t select_plague(KpGame *g, int64_t n, int r) { return around(g, n, window(g, n, r, 4096, 0), plague_target); }
static int64_t select_storm(KpGame *g, int64_t n, int r) /* 0x4330a0 */
{
	g->ai.spell_reactive = r;
	return around(g, n, window(g, n, r, 1024, 1), storm_target);
}
static int64_t select_feedback(KpGame *g, int64_t n, int r) { return around(g, n, window(g, n, r, 1024, 1), feedback_target); }
static int64_t select_stasis(KpGame *g, int64_t n, int r) { return around(g, n, window(g, n, r, 1024, 1), stasis_target); }
static int64_t select_swarm(KpGame *g, int64_t n, int r) /* 0x4331d0: over an air enemy, at its victim */
{
	int64_t c = around(g, n, window(g, n, r, 1024, 1), swarm_target);
	if (c != 0 && kp_band(g->u.status[c], STATUS_AIR) != 0) c = g->u.order_unit[c];
	return c;
}
static int64_t select_yamato(KpGame *g, int64_t n, int r) /* 0x433240 */
{
	return r ? around(g, n, 512, yamato_target) : map_wide(g, n, yamato_target);
}
static int64_t select_recall(KpGame *g, int64_t n, int r) /* 0x433140: from a region the AI holds */
{
	int64_t p = g->u.owner[n], reg, *a;
	int32_t s = kp_unit_sprite(g, n);
	(void)r;
	reg = kp_region_at(g, kp_s16v(g->w->spr.x[s]), kp_s16v(g->w->spr.y[s]));
	/* IsRegionUnthreatened 0x4aea70 */
	a = &g->air.state[p][reg];
	if (*a == REGION_ABANDONED || g->air.target_air[p][reg] != 0 || g->air.target_ground[p][reg] != 0
		|| kp_band(g->air.flags[p][reg], REGION_TARGETED) != 0) {
		return 0;
	}
	return map_wide(g, n, recall_target);
}

/* IsSpellReady 0x48e580: the energy of order's spell and the use of tech. */
static int ready(KpGame *g, int64_t n, int64_t order, int64_t tech)
{
	if (kp_needs_energy(g, n, kp_order_energy(g, order))) return 0;
	return kp_can_use_tech(g, tech, g->u.owner[n], n) == 1;
}

/* ReserveSpellTarget 0x48e5c0: the order on target, marked for the other casters. */
static int reserve(KpGame *g, int64_t n, int64_t order, int64_t target, int64_t mask)
{
	KpUnits *u = &g->u;
	if (target == 0 || kp_band(u->ai_action[target], mask) != 0) return 0;
	if (u->order[n] != order) {
		kp_issue_order_target(g, n, order, target);
		u->ai_action[target] = kp_bor(u->ai_action[target], mask);
	}
	return 1;
}

enum { CAST, STOP, NEXT };

/* One attempt of the dispatcher: NEXT when the spell is not ready or no reservation took, STOP when the caster already
 * casts it at the selected target. */
static int attempt(KpGame *g, int64_t n, int64_t order, int64_t tech, Selector select, int reactive, int64_t mask)
{
	int64_t target;
	if (!ready(g, n, order, tech)) return NEXT;
	target = select(g, n, reactive);
	if (g->u.order[n] == order && g->u.order_unit[n] == target) return STOP;
	return reserve(g, n, order, target, mask) ? CAST : NEXT;
}

/* The whole energy as GetMaxEnergyWhole 0x434a80 gives it. */
static int full_energy(KpGame *g, int64_t n)
{
	return kp_shrv(kp_mod(g->u.energy[n], 65536), 8) == kp_sarv(kp_max_energy(g, n), 8);
}

static int queen(KpGame *g, int64_t n, int r)
{
	KpUnits *u = &g->u;
	int a, parasite_first;
	if (campaign(g, n)) parasite_first = kp_band(kp_rng_game_raw(g->rng, QUEEN_RANDOM), 0xff) == 1;
	else parasite_first = full_energy(g, n);
	if (parasite_first) {
		if (ready(g, n, O_PARASITE, 18)) {
			int64_t target = select_parasite(g, n, r);
			if (u->order[n] == O_PARASITE && u->order_unit[n] == target) return 0;
			return reserve(g, n, O_PARASITE, target, 4);
		}
		if (campaign(g, n)) return 0;
	}
	a = attempt(g, n, O_BROODLINGS, 13, select_broodlings, r, 1);
	if (a == STOP) return 0;
	if (a == CAST) {
		int32_t s = kp_unit_sprite(g, n);
		kp_order_append(g, n, ORDER_MOVE, kp_s16v(g->w->spr.x[s]), kp_s16v(g->w->spr.y[s]), 0, NO_TYPE, 0);
		return 1;
	}
	if (!campaign(g, n) && !full_energy(g, n)) return 0;
	return attempt(g, n, O_ENSNARE, 17, select_ensnare, r, 1) == CAST;
}

/* NukeScoreCallback 0x432150: enough of n's enemies' buildings and workers (their whole life), or an enemy detector or
 * Sunken Colony, within 192 pixels of c. */
static int nuke_score(KpGame *g, int64_t c, int64_t n)
{
	KpUnits *u = &g->u;
	int32_t s;
	int64_t x, y, *found, sum = 0;
	int count, k;
	s = kp_unit_sprite(g, c);
	x = g->w->spr.x[s];
	y = g->w->spr.y[s];
	found = kp_query_rect(g, kp_s16v(x - NUKE_SCORE_RADIUS), kp_s16v(y - NUKE_SCORE_RADIUS), kp_s16v(x + NUKE_SCORE_RADIUS),
		kp_s16v(y + NUKE_SCORE_RADIUS), &count);
	for (k = 0; k < count; k++) {
		int64_t m = found[k], f = flags(g, u->type[m]);
		if (kp_band(u->status[m], STATUS_INVINCIBLE) != 0 || !kp_is_enemy(g, n, m)) continue;
		if (kp_band(f, DAT_WORKER) != 0 || kp_band(u->status[m], STATUS_CAN_MOVE) == 0) sum += kp_current_life(g, m);
		if (kp_band(f, DAT_BUILDING) != 0 && (kp_is_detector(g, m) || u->type[m] == SUNKEN_COLONY || u->type[m] == LURKER)) {
			sum = NUKE_SCORE_THRESHOLD;
		}
	}
	kp_query_release(g, found);
	return kp_u32v(sum) >= NUKE_SCORE_THRESHOLD;
}

/* NukeTargetCallback 0x4335b0: a detected enemy scoring enough. */
static int nuke_target(KpGame *g, int64_t c, int64_t n)
{
	KpUnits *u = &g->u;
	if (kp_band(u->status[c], STATUS_CLOAK_BITS) != 0 && kp_band(u->visibility[c], (int64_t)1 << u->owner[n]) == 0) return 0;
	if (!kp_is_enemy(g, n, c)) return 0;
	return nuke_score(g, c, n);
}

typedef struct {
	int64_t ghost, rect[4], last; /* the rectangle 0x665d38 and the last enemy scored in it 0x665d34 */
} NukeRect;

/* NukeRectTargetCallback 0x433620: an enemy (detected or not) inside the rectangle (inclusive) scoring enough. */
static int nuke_rect_target(KpGame *g, int64_t c, int64_t arg)
{
	NukeRect *q = (NukeRect *)(intptr_t)arg;
	int32_t s = kp_unit_sprite(g, c);
	int64_t x = kp_s16v(g->w->spr.x[s]), y = kp_s16v(g->w->spr.y[s]);
	if (x < q->rect[0] || y < q->rect[1] || x > q->rect[2] || y > q->rect[3] || !kp_is_enemy(g, q->ghost, c)) return 0;
	q->last = c;
	return nuke_score(g, c, q->ghost);
}

/* SelectNukeTargetInRect 0x432f60 (nuke_location): the nearest enemy in the rectangle {left, top, right, bottom}
 * scoring enough, else the last one scored there, else 0. */
int64_t kp_ai_nuke_target_in_rect(KpGame *g, int64_t ghost, const int64_t rect[4])
{
	NukeRect q;
	int64_t found;
	int k;
	q.ghost = ghost;
	for (k = 0; k < 4; k++) q.rect[k] = kp_s16v(rect[k]);
	q.last = 0;
	found = kp_nearest_unit_map(g, ghost, nuke_rect_target, (int64_t)(intptr_t)&q);
	return found != 0 ? found : q.last;
}

static int ghost(KpGame *g, int64_t n, int r)
{
	KpUnits *u = &g->u;
	int64_t p = u->owner[n], m;
	int a = attempt(g, n, O_LOCKDOWN, 1, select_lockdown, r, 1);
	if (a != NEXT) return a == CAST;
	/* IsNukeReady 0x4832a0 */
	if (kp_scalar(g, KP_ELAPSED) < kp_u32v(g->pai.last_nuke[p] + g->pai.nuke_rate[p] * 60)) return 0;
	m = kp_find_ready_silo(g, n, 1, n); /* 0x436f60 */
	if (m == 0 || !reserve(g, n, O_NUKE, kp_nearest_unit_map(g, n, nuke_target, n), 1)) return 0; /* 0x432f40 */
	u->connected[u->union_0xd0[m]] = n;
	g->pai.last_nuke[p] = kp_scalar(g, KP_ELAPSED); /* SetNukeTime 0x483270 */
	return 1;
}

static int medic(KpGame *g, int64_t n, int r)
{
	if (ready(g, n, O_RESTORATION, 24)) {
		int64_t target = select_restoration(g, n, r);
		if (g->u.order[n] == O_RESTORATION && g->u.order_unit[n] == target) return 0;
		if (reserve(g, n, O_RESTORATION, target, 1)) return 1;
		/* the player's free Medic (IsFreeMedic 0x482450) also cleanses (SelectRestorationCleanseTarget 0x433000) */
		if (g->pai.medic[g->u.owner[n]] == n && g->u.order[n] != O_RESTORATION
			&& reserve(g, n, O_RESTORATION, r ? 0 : map_wide(g, n, cleanse_target), 1)) {
			return 1;
		}
	}
	if (!full_energy(g, n)) return 0;
	return attempt(g, n, O_FLARE, 30, select_flare, r, 1) == CAST;
}

static int dark_archon(KpGame *g, int64_t n, int r)
{
	int64_t need, max;
	int a = attempt(g, n, O_FEEDBACK, 29, select_feedback, r, 1);
	if (a != NEXT) return a == CAST;
	a = attempt(g, n, O_MIND_CONTROL, 27, select_mind_control, r, 1);
	if (a != NEXT) return a == CAST;
	/* GetOrderEnergyCost 0x4265e0 of Maelstrom and Mind Control, at most the maximum energy */
	need = kp_tech_energy(g, KP_DAT(g, orders, technology_energy, O_MAELSTROM))
		+ kp_tech_energy(g, KP_DAT(g, orders, technology_energy, O_MIND_CONTROL));
	max = kp_max_energy(g, n);
	if (need > max) need = max;
	if (kp_needs_energy(g, n, need)) return 0;
	if (kp_can_use_tech(g, T_MAELSTROM, g->u.owner[n], n) != 1) return 0;
	{
		int64_t target = select_maelstrom(g, n, r);
		if (g->u.order[n] == O_MAELSTROM && g->u.order_unit[n] == target) return 0;
		return reserve(g, n, O_MAELSTROM, target, 1);
	}
}

/* Two spells: the second when the first is not ready or reserves nothing. */
static int pair(KpGame *g, int64_t n, int r, int64_t o1, int64_t t1, Selector s1, int64_t o2, int64_t t2, Selector s2)
{
	int a = attempt(g, n, o1, t1, s1, r, 1);
	if (a != NEXT) return a == CAST;
	return attempt(g, n, o2, t2, s2, r, 1) == CAST;
}

/* TryAiReactionSpell 0x48d3a0: 1 when the caster took a spell order. */
int kp_ai_reaction_spell(KpGame *g, int64_t n, int reactive)
{
	int a;
	/* the original reads the PlayerAi record of the owner (0x67d068), of which there are 8 */
	if (kp_u32v(g->u.owner[n]) >= KP_AI_PLAYERS) {
		kp_game_fail(g, "ai: a reaction spell of player %lld (0x48d3a0)", (long long)g->u.owner[n]);
	}
	if (!reactive && g->pai.countdown1[g->u.owner[n]] != 0) return 0; /* IsSpellGateClear 0x4833c0 */
	switch (g->u.type[n]) {
	case 1:
		return ghost(g, n, reactive);
	case 9:
		a = attempt(g, n, O_EMP, 2, select_emp, reactive, 1);
		if (a != NEXT) return a == CAST;
		return pair(g, n, reactive, O_MATRIX, 6, select_matrix, O_IRRADIATE, 7, select_irradiate);
	case 12:
		return attempt(g, n, O_YAMATO, 8, select_yamato, reactive, 1) == CAST;
	case 34:
		return medic(g, n, reactive);
	case 45:
		return queen(g, n, reactive);
	case 46:
		return pair(g, n, reactive, O_PLAGUE, 15, select_plague, O_DARK_SWARM, 14, select_swarm);
	case 60:
		return attempt(g, n, O_WEB, 25, select_web, reactive, 1) == CAST;
	case 63:
		return dark_archon(g, n, reactive);
	case 67:
		a = attempt(g, n, O_STORM, 19, select_storm, reactive, 1);
		if (a != NEXT) return a == CAST;
		if (!full_energy(g, n)) return 0;
		return attempt(g, n, O_HALLUCINATION, 20, select_hallucination, reactive, 1) == CAST;
	case 71:
		return pair(g, n, reactive, O_STASIS, 22, select_stasis, O_RECALL, 21, select_recall);
	}
	return 0;
}
