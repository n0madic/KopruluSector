/* Spell casting (kp_game.h), ported from the atlas (P11): the target legality of technologies (0x434130 with 0x435380
 * and 0x435320), the abandon test of spell orders and of IScript `castspell` (0x434e70), the generic spell order
 * OrderCastSpell 0x434460 with its approach (0x435020, 0x435530), and the weapon a spell fires (0x41f4a0). */
#include "kp_game_internal.h"

#define INFESTING_TIME 60
#define INFESTED_DELAY 3
#define NO_IMAGE 999
#define SCANNER_SOUND 388
#define SIGNAL_SWEEP_DONE 0x4
#define DAT_BUILDING 0x1
#define DAT_SPELLCASTER 0x200000
#define DAT_INVINCIBLE 0x20000000
#define STATUS_COMPLETED 0x1
#define STATUS_GROUNDED_BUILDING 0x2
#define STATUS_INVINCIBLE 0x4000000
#define STATUS_HALLUCINATION 0x40000000
#define CHEAT_ENERGY 0x800
#define MINE_TILES 0x410000
#define SIGNAL_CAST 0x2
#define ATTACKING 0x8
#define BRAKING 0x4
#define MOVING 0x2
#define SPELL_COOLDOWN_RANDOM 49
/* the error strings of stat_txt.tbl */
#define ERR_INVALID 876
#define ERR_BUILDING 877
#define ERR_NOT_INFESTABLE 870
#define ERR_NO_TARGET 878
#define ERR_STASIS 900
#define ERR_CONSUME 897
#define ERR_MINE_TERRAIN 902
#define ERR_SAME_OWNER 1327
#define ERR_NOT_CASTER 1330

static int64_t flags(KpGame *g, int64_t t) { return KP_DAT(g, units, special_ability_flags, t); }

/* IsRealSpellcaster 0x4118d0. */
static int real_spellcaster(KpGame *g, int64_t n)
{
	return kp_band(flags(g, g->u.type[n]), DAT_SPELLCASTER) != 0 && kp_band(g->u.status[n], STATUS_HALLUCINATION) == 0;
}

/* IsInfestableCommandCenter 0x41fed0. */
int kp_infestable(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t max, life;
	if (kp_band(u->status[n], STATUS_COMPLETED) == 0 || u->type[n] != KP_TYPE_COMMAND_CENTER) return 0;
	life = kp_sarv(kp_s32v(u->hp[n]) + 0xff, 8);
	max = kp_sarv(kp_s32v(KP_DAT(g, units, hit_points, KP_TYPE_COMMAND_CENTER)), 8);
	if (max == 0) max = life > 1 ? life : 1;
	return kp_s32v(life * 100) / max < 50;
}

/* ValidateSpellOnUnit 0x435380: 0 or the error string of tech at target for caster owner `owner`. */
static int64_t on_unit(KpGame *g, int64_t tech, int64_t target, int64_t owner)
{
	KpUnits *u = &g->u;
	int64_t t = u->type[target];
	int building = kp_band(flags(g, t), DAT_BUILDING) != 0;
	if (u->stasis_timer[target] != 0) return ERR_STASIS;
	switch (tech) {
	case 6: case 7: case 24: case 30:
		return building ? ERR_BUILDING : 0;
	case KP_TECH_INFESTATION:
		return kp_infestable(g, target) ? 0 : ERR_NOT_INFESTABLE;
	case KP_TECH_CONSUME:
		return building || u->owner[target] != owner || kp_unit_race(g, target) != 0 || t == KP_TYPE_LARVA ? ERR_CONSUME : 0;
	case KP_TECH_HALLUCINATION:
		if (t == KP_TYPE_INTERCEPTOR) return ERR_INVALID;
		return building ? ERR_BUILDING : 0;
	case KP_TECH_MIND_CONTROL:
		if (u->owner[target] == owner) return ERR_SAME_OWNER;
		if (building) return ERR_BUILDING;
		switch (t) {
		case 13: case 35: case 36: case 59: case 73: case 85: case 97: return ERR_INVALID;
		}
		return 0;
	case KP_TECH_FEEDBACK:
		if (building) return ERR_BUILDING;
		return real_spellcaster(g, target) ? 0 : ERR_NOT_CASTER;
	}
	return ERR_INVALID;
}

/* ValidateSpellOnFogType 0x435320. */
static int64_t on_fog_type(int64_t tech, int64_t type)
{
	switch (tech) {
	case 6: case 7: return ERR_BUILDING;
	case KP_TECH_INFESTATION: return type == KP_TYPE_COMMAND_CENTER ? 0 : ERR_NOT_INFESTABLE;
	case KP_TECH_CONSUME: return ERR_CONSUME;
	}
	return ERR_INVALID;
}

/* ValidateSpellTarget 0x434130: 1 when tech may target `target` (or the fog type `type`, or the point). */
int kp_validate_spell_target(KpGame *g, int64_t caster, int64_t tech, int64_t target, int64_t type, int64_t x, int64_t y,
	int64_t *error)
{
	KpUnits *u = &g->u;
	int64_t e;
	switch (tech) {
	case 2: case 4: case 14: case 15: case 17: case 19: case 21: case 22: case 25: case 31:
		return 1;
	}
	if (tech == KP_TECH_SPIDER_MINES) {
		if (kp_tile_has_flags(g, x, y, MINE_TILES, 0)) return 1;
		if (error) *error = ERR_MINE_TERRAIN;
		return 0;
	}
	if (target == 0 && type == KP_TYPE_NONE) {
		if (error) *error = ERR_NO_TARGET;
		return 0;
	}
	if (target == 0 || kp_band(u->status[target], STATUS_INVINCIBLE) != 0) {
		/* IsTypeAttackable 0x420a90 */
		if (type >= KP_TYPE_NONE || kp_band(flags(g, type), DAT_INVINCIBLE) != 0) {
			if (error) *error = ERR_INVALID;
			return 0;
		}
	}
	e = type == KP_TYPE_NONE ? on_unit(g, tech, target, u->owner[caster]) : on_fog_type(tech, type);
	if (e != 0 && error) *error = e;
	return e == 0;
}

/* SpellTargetInvalid 0x434e70: 1 when the caster must abandon its spell order. */
int kp_spell_target_invalid(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t order = u->order[n], target = u->order_unit[n], tech;
	if (u->ai[n] != 0) {
		switch (order) {
		case KP_ORD_CAST_LOCKDOWN: case KP_ORD_CAST_STASIS_FIELD:
			if (target == 0 || kp_unit_disabled(g, target)) return 1;
			break;
		case KP_ORD_CAST_PARASITE:
			if (target == 0 || u->parasites[target] != 0) return 1;
			break;
		case KP_ORD_CAST_EMP_SHOCKWAVE:
			if (target != 0 && !(real_spellcaster(g, target) && kp_shrv(kp_mod(u->energy[target], 65536), 8) >= 100)) {
				if (KP_DAT(g, units, shield_enabled, u->type[target]) == 0) return 1;
				if (kp_s32v(kp_band(u->shields[target], 0xffffff00)) < 0x6400) return 1;
			}
			break;
		case KP_ORD_CAST_IRRADIATE:
			if (target == 0 || u->irradiate_timer[target] != 0) return 1;
			break;
		case KP_ORD_CAST_PLAGUE:
			if (target == 0 || u->plague_timer[target] != 0) return 1;
			break;
		case KP_ORD_CAST_ENSNARE:
			if (target != 0 && u->ensnare_timer[target] != 0) return 1;
			break;
		}
	}
	if (KP_DAT(g, orders, use_weapon_targeting, order) != 0) {
		return !kp_weapon_can_target(g, n, KP_DAT(g, orders, targeting, order), target);
	}
	tech = KP_DAT(g, orders, technology_energy, order);
	if (tech >= KP_TECHS) return 0;
	return !kp_validate_spell_target(g, n, tech, target, KP_TYPE_NONE, kp_s16v(u->order_x[n]), kp_s16v(u->order_y[n]), NULL);
}

/* FireWeaponFromUnit 0x41f4a0: castspell's weapon, twice for a damage factor of 2. */
void kp_fire_weapon(KpGame *g, int64_t n, int64_t weapon)
{
	weapon = kp_mod(weapon, 256);
	kp_spawn_weapon_bullet(g, n, weapon);
	if (KP_DAT(g, weapons, damage_factor, weapon) == 2) kp_spawn_weapon_bullet(g, n, weapon);
}

/* SpellTargetInRange 0x435530. */
static int in_range(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t weapon = KP_DAT(g, orders, targeting, u->order[n]);
	if (!kp_can_see_target(g, n, u->order_unit[n])) return 0;
	if (u->order_unit[n] != 0) return kp_within_range_moving(g, n, kp_weapon_max_range(g, n, weapon), 0);
	return kp_within_distance(g, n, kp_weapon_max_range(g, n, weapon) + kp_sarv(kp_flingy_halt_distance(g, n), 8),
		kp_s16v(u->order_x[n]), kp_s16v(u->order_y[n]));
}

/* SpellApproachTarget 0x435020: moves into casting range; the in-range result. */
static int approach(KpGame *g, int64_t n, int first)
{
	KpUnits *u = &g->u;
	int64_t target = u->order_unit[n];
	int r;
	if (target != 0) {
		int32_t s = kp_unit_sprite(g, target);
		u->order_x[n] = kp_s16v(g->w->spr.x[s]);
		u->order_y[n] = kp_s16v(g->w->spr.y[s]);
	}
	r = in_range(g, n);
	if (!r) {
		if (target == 0) kp_move_to_point_throttled(g, n, kp_s16v(u->order_x[n]), kp_s16v(u->order_y[n]));
		else if (first) kp_set_move_target_unit(g, n, target);
		else kp_move_toward_target(g, n, target);
	} else if (kp_band(u->movement_flags[n], BRAKING) == 0) {
		kp_stop_movement(g, n);
	}
	kp_flingy_set_target_waypoint(g, n, kp_s16v(u->order_x[n]), kp_s16v(u->order_y[n]));
	return r;
}

/* The energy of tech (techdata.dat, ×256 as a WORD). */
int64_t kp_tech_energy(KpGame *g, int64_t tech) { return kp_mod(KP_DAT(g, techdata, energy_required, tech) * 256, 65536); }

/* GetOrderEnergyCost 0x4265e0: the energy of the order's tech, 0 without one. */
int64_t kp_order_energy(KpGame *g, int64_t order)
{
	int64_t tech = KP_DAT(g, orders, technology_energy, order);
	return tech < KP_TECHS ? kp_tech_energy(g, tech) : 0;
}

/* NeedsMoreEnergy 0x434b50. */
int kp_needs_energy(KpGame *g, int64_t n, int64_t cost)
{
	return kp_band(kp_cheats(g), CHEAT_ENERGY) == 0 && kp_mod(g->u.energy[n], 65536) < cost;
}

/* SpendEnergy 0x435300. */
void kp_spend_energy(KpGame *g, int64_t n, int64_t cost)
{
	if (kp_band(kp_cheats(g), CHEAT_ENERGY) == 0) g->u.energy[n] = kp_mod(g->u.energy[n] - cost, 65536);
}

/* OrderCastSpell 0x434460 (orders 113, 115, 119-122, 142-147, 180, 181, 185, 186). */
void kp_order_cast_spell(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t order = u->order[n], weapon = KP_DAT(g, orders, targeting, order), cost, c, target;
	int r;
	if (u->order_state[n] == 2) {
		if (kp_band(u->order_signal[n], SIGNAL_CAST) != 0) {
			u->order_signal[n] = kp_band(u->order_signal[n], kp_bnot(SIGNAL_CAST));
			kp_order_done(g, n);
			return;
		}
		target = u->order_unit[n];
		if (target != 0) {
			int32_t s = kp_unit_sprite(g, target);
			u->order_x[n] = kp_s16v(g->w->spr.x[s]);
			u->order_y[n] = kp_s16v(g->w->spr.y[s]);
			kp_flingy_set_target_waypoint(g, n, u->order_x[n], u->order_y[n]);
		}
		return;
	}
	if (kp_spell_target_invalid(g, n)) {
		/* the error message (0x446760) is the interface's */
		kp_stop_movement(g, n);
		kp_order_done(g, n);
		return;
	}
	cost = kp_order_energy(g, order);
	if (kp_needs_energy(g, n, cost)) {
		/* the "not enough energy" message is the interface's */
		kp_order_done(g, n);
		return;
	}
	r = approach(g, n, u->order_state[n] == 0);
	if (u->order_state[n] == 0) u->order_state[n] = 1;
	if (!r) {
		if (kp_move_arrival(g, n) == 2) kp_order_done(g, n);
		return;
	}
	if (u->spell_cooldown[n] != 0 || !kp_weapon_ready(g, n, weapon) || kp_band(u->movement_flags[n], MOVING) != 0
		|| kp_move_arrival(g, n) == 0) {
		return;
	}
	target = u->order_unit[n];
	if (u->ai[n] != 0 && target != 0) {
		int64_t blocked = u->irradiate_timer[target];
		switch (weapon) {
		case 32: case 108: blocked |= kp_unit_disabled(g, target); break;
		case 56: blocked |= u->parasites[target]; break;
		case 58: blocked |= u->ensnare_timer[target]; break;
		case 60: blocked |= u->plague_timer[target]; break;
		case 107: blocked |= u->blind[target]; break;
		}
		if ((weapon == 32 || weapon == 34 || weapon == 56 || weapon == 58 || weapon == 60 || weapon == 107
				|| weapon == 108) && blocked != 0) {
			kp_order_done(g, n);
			return;
		}
	}
	kp_spend_energy(g, n, cost);
	c = kp_weapon_cooldown(g, n, weapon);
	u->spell_cooldown[n] = kp_mod(c + kp_band(kp_rng_game_raw(g->rng, SPELL_COOLDOWN_RANDOM), 3) - 1, 256);
	u->movement_flags[n] = kp_bor(u->movement_flags[n], ATTACKING);
	u->order_signal[n] = kp_band(u->order_signal[n], kp_bnot(SIGNAL_CAST));
	kp_sprite_play(g->w, kp_unit_sprite(g, n), (int32_t)KP_DAT(g, orders, iscript_animation, order), 1);
	u->order_state[n] = 2;
	if (u->ai[n] != 0) g->pai.countdown1[u->owner[n]] = 1; /* SetSpellGate 0x482ef0 */
}

#define FACING 5
#define MATRIX_SOUND 349
#define MATRIX_CAST_IMAGE 539
#define FEEDBACK_SOUND 1061
#define MIND_CONTROL_SOUND 1062
#define FEEDBACK_OVERLAY 979
#define MIND_CONTROL_OVERLAY 973
#define FEEDBACK_KILL_SPRITE 500
#define DIRECT_ELEVATION_STEP 1

/* SpellApproachAndFace 0x4350b0: 1 when the caster stands in range and faces its target. */
static int approach_and_face(KpGame *g, int64_t n, int64_t range)
{
	KpUnits *u = &g->u;
	int64_t target = u->order_unit[n];
	int32_t s;
	if (target == 0) {
		kp_stop_movement(g, n);
		kp_order_done(g, n);
		return 0;
	}
	s = kp_unit_sprite(g, target);
	u->order_x[n] = kp_s16v(g->w->spr.x[s]);
	u->order_y[n] = kp_s16v(g->w->spr.y[s]);
	kp_flingy_set_target_waypoint(g, n, u->order_x[n], u->order_y[n]);
	if (kp_can_see_target(g, n, target) && kp_within_range_moving(g, n, range, 0)) {
		int64_t d;
		kp_stop_movement(g, n);
		if (u->target_waypoint_x[n] == u->x[n] && u->target_waypoint_y[n] == u->y[n]) return 1;
		s = kp_unit_sprite(g, n);
		d = kp_direction_to(g, kp_s16v(g->w->spr.x[s]), kp_s16v(g->w->spr.y[s]), kp_s16v(u->target_waypoint_x[n]),
			kp_s16v(u->target_waypoint_y[n]));
		return kp_heading_difference(d, u->heading[n]) <= FACING;
	}
	if (kp_move_arrival(g, n) == 2) {
		kp_stop_movement(g, n);
		kp_order_done(g, n);
		return 0;
	}
	if (u->order_unit[n] != 0) kp_move_toward_target(g, n, u->order_unit[n]);
	else kp_move_to_point_throttled(g, n, kp_s16v(u->order_x[n]), kp_s16v(u->order_y[n]));
	return 0;
}

/* CastSpellApproach 0x434b80: 1 with the energy cost when tech may go off at the order target now. */
static int cast_gate(KpGame *g, int64_t n, int64_t tech, int64_t range, int64_t *cost)
{
	KpUnits *u = &g->u;
	int64_t target = u->order_unit[n];
	int32_t s;
	/* the error messages (0x446760, 0x46ae50) are the interface's */
	if (target == 0) {
		kp_order_done(g, n);
		return 0;
	}
	s = kp_unit_sprite(g, target);
	if (!kp_validate_spell_target(g, n, tech, target, KP_TYPE_NONE, kp_s16v(g->w->spr.x[s]), kp_s16v(g->w->spr.y[s]), NULL)) {
		kp_order_done(g, n);
		return 0;
	}
	*cost = kp_tech_energy(g, tech);
	if (kp_needs_energy(g, n, *cost)) {
		kp_order_done(g, n);
		return 0;
	}
	return approach_and_face(g, n, range);
}

static int64_t cast_range(KpGame *g, int64_t n) { return kp_unit_sight(g, n, 1) * 32; }

/* Order_DefensiveMatrix 0x438560 (order 141). */
void kp_order_defensive_matrix(KpGame *g, int64_t n)
{
	int64_t cost;
	if (!cast_gate(g, n, KP_TECH_DEFENSIVE_MATRIX, cast_range(g, n), &cost)) return;
	kp_spend_energy(g, n, cost);
	kp_apply_matrix(g, g->u.order_unit[n]);
	kp_play_unit_sound(g, MATRIX_SOUND, n);
	kp_sprite_image_above_main(g->w, kp_unit_sprite(g, n), MATRIX_CAST_IMAGE, 0, 0, 0);
	kp_order_done(g, n);
}

/* The lone sprite of a unit's end at its position, one level above it (0x430b90). */
static void end_sprite(KpGame *g, int64_t id, int64_t n)
{
	KpImages *w = g->w;
	int32_t s = kp_unit_sprite(g, n), t = kp_lone_create(w, (int32_t)id, w->spr.x[s], w->spr.y[s], 0);
	if (t == KP_NONE) return;
	w->spr.elevation[w->thg.sprite[t]] = w->spr.elevation[s] + DIRECT_ELEVATION_STEP;
	kp_thingy_visibility(w, t);
}

static int alive(KpGame *g, int64_t n) { return g->u.sprite[n] != KP_NONE && g->u.order[n] != 0; }

/* Order_Feedback 0x430d90 (order 184). */
void kp_order_feedback(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t target = u->order_unit[n], cost;
	if (target == 0 || kp_band(flags(g, u->type[target]), DAT_SPELLCASTER) == 0
		|| kp_band(u->status[target], STATUS_HALLUCINATION) != 0) {
		kp_order_done(g, n);
		return;
	}
	if (!cast_gate(g, n, KP_TECH_FEEDBACK, cast_range(g, n), &cost)) return;
	if (kp_band(u->status[target], STATUS_HALLUCINATION) != 0) {
		end_sprite(g, FEEDBACK_KILL_SPRITE + kp_overlay_size(g, target), target);
		kp_spend_energy(g, n, cost);
		kp_unit_kill(g, target);
		kp_order_done(g, n);
		return;
	}
	if (kp_mod(u->energy[target], 65536) == 0) {
		kp_order_done(g, n);
		return;
	}
	kp_weapon_damage(g, target, kp_mod(u->energy[target], 65536), KP_WEAPON_FEEDBACK, 1, 1, n, u->owner[n]);
	u->energy[target] = 0;
	kp_spend_energy(g, n, cost);
	kp_play_unit_sound(g, FEEDBACK_SOUND, target);
	if (alive(g, target)) kp_add_status_overlay(g, target, FEEDBACK_OVERLAY + kp_overlay_size(g, target));
	else end_sprite(g, FEEDBACK_KILL_SPRITE + kp_overlay_size(g, target), target);
	kp_order_done(g, n);
}

/* Order_MindControl 0x430ce0 (order 182). */
void kp_order_mind_control(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t target = u->order_unit[n], cost;
	if (target == 0 || u->owner[n] == u->owner[target]) {
		kp_order_done(g, n);
		return;
	}
	if (!cast_gate(g, n, KP_TECH_MIND_CONTROL, cast_range(g, n), &cost)) return;
	if (kp_band(u->status[target], STATUS_HALLUCINATION) != 0) {
		kp_unit_kill(g, target);
	} else {
		kp_add_status_overlay(g, target, MIND_CONTROL_OVERLAY + kp_overlay_size(g, target));
		/* ApplyMindControl 0x430f60 through TriggerGiveUnit 0x4b81a0 */
		kp_give_unit_checked(g, target, u->owner[n]);
		if (u->type[target] == KP_TYPE_DARK_ARCHON) u->energy[target] = 0;
		kp_order_done(g, target);
	}
	u->shields[n] = 0;
	kp_spend_energy(g, n, cost);
	kp_play_unit_sound(g, MIND_CONTROL_SOUND, target);
	kp_order_done(g, n);
}

#define HALLUCINATION_COPIES 2
#define HALLUCINATION_SOUND 618
#define HALLUCINATION_OVERLAY 545

/* OrderCastHallucination 0x430bd0 (order 148): two copies of the target for the caster's owner. */
void kp_order_hallucination(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t cost, h, target;
	int k;
	if (!cast_gate(g, n, KP_TECH_HALLUCINATION, cast_range(g, n), &cost)) return;
	target = u->order_unit[n];
	for (k = HALLUCINATION_COPIES; k-- > 0;) {
		h = kp_create_hallucination(g, target, u->owner[n]);
		if (h == 0) break;
		if (!kp_place_hallucination(g, h)) {
			kp_unit_destroy(g, h);
			break;
		}
	}
	kp_spend_energy(g, n, cost);
	kp_play_unit_sound(g, HALLUCINATION_SOUND, target);
	kp_add_status_overlay(g, target, HALLUCINATION_OVERLAY);
	kp_order_done(g, n);
}

#define RECALL_RADIUS 64
#define RECALL_DELAY 22
#define RECALL_SPRITE 379
#define RECALL_SOUND 550
#define RECALLED_SOUND 552
#define RECALL_RANDOM 17
#define RECALLED_RANDOM 18
#define SPRITE_HIDDEN 0x20
#define STATUS_BURROWED 0x10

/* The Recall field's lone sprite at (x, y), one level above unit n's sprite (0x42fd16, 0x4305aa). */
static void recall_field(KpGame *g, int64_t x, int64_t y, int64_t n)
{
	KpImages *w = g->w;
	int32_t t = kp_lone_create(w, RECALL_SPRITE, (int32_t)x, (int32_t)y, 0);
	if (t == KP_NONE) return;
	w->spr.elevation[w->thg.sprite[t]] = w->spr.elevation[kp_unit_sprite(g, n)] + 1;
	kp_thingy_visibility(w, t);
}

/* The candidates of the Recall callback 0x4302c0. */
static int recallable(KpGame *g, int64_t c, int64_t caster)
{
	KpUnits *u = &g->u;
	int64_t t = u->type[c];
	return kp_band(g->w->spr.flags[kp_unit_sprite(g, c)], SPRITE_HIDDEN) == 0 && u->owner[c] == u->owner[caster]
		&& kp_band(flags(g, t), DAT_BUILDING) == 0
		&& kp_band(u->status[c], STATUS_INVINCIBLE | STATUS_BURROWED | STATUS_HALLUCINATION) == 0 && t != 35 && t != 36
		&& t != 97 && c != caster;
}

/* RecallUnitToCaster 0x4304d0: 1 when unit c moved next to the caster. */
static int recall_unit(KpGame *g, int64_t c, int64_t caster)
{
	KpUnits *u = &g->u;
	KpImages *w = g->w;
	int32_t s = kp_unit_sprite(g, c), cs = kp_unit_sprite(g, caster);
	int64_t ox = kp_s16v(w->spr.x[s]), oy = kp_s16v(w->spr.y[s]), cx = kp_s16v(w->spr.x[cs]), cy = kp_s16v(w->spr.y[cs]);
	int64_t x, y;
	kp_unit_relocate(g, c, cx, cy);
	if (!kp_find_unit_placement(g, c, cx, cy, NULL, 0, &x, &y)) {
		kp_unit_relocate(g, c, ox, oy);
		return 0;
	}
	kp_unit_detach(g, c, 0);
	u->order_unit[c] = 0;
	kp_unit_relocate(g, c, x, y);
	kp_unit_reattach(g, c);
	if (u->type[c] != KP_TYPE_COCOON) kp_issue_order(g, c, KP_DAT(g, units, return_to_idle, u->type[c]));
	recall_field(g, x, y, c);
	kp_ghost_release_nuke(g, c);
	return 1;
}

/* OrderCastScannerSweep 0x436990 (order 139): a Scanner Sweep appears at the order point. */
void kp_order_scanner_sweep(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t cost = kp_tech_energy(g, KP_TECH_SCANNER_SWEEP), x, y, sweep;
	if (kp_needs_energy(g, n, cost)) {
		/* the "not enough energy" message (0x46ae50) is the interface's */
		kp_order_done(g, n);
		return;
	}
	x = kp_s16v(u->order_x[n]);
	y = kp_s16v(u->order_y[n]);
	kp_clamp_to_map(g, KP_TYPE_SPELL_SCANNER_SWEEP, &x, &y);
	sweep = kp_unit_create(g, KP_TYPE_SPELL_SCANNER_SWEEP, x, y, u->owner[n]);
	if (sweep != 0) {
		kp_unit_complete_in_place(g, sweep);
		kp_play_unit_sound(g, SCANNER_SOUND, n);
		kp_spend_energy(g, n, cost);
	}
	/* a failure shows the unit limit message (0x425020): the interface's */
	kp_order_done(g, n);
}

/* The Scanner Sweep's order 140 (0x436a60): it dies on its animation's signal. */
void kp_order_scanner(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	if (kp_band(u->order_signal[n], SIGNAL_SWEEP_DONE) == 0) return;
	u->order_signal[n] = kp_band(u->order_signal[n], kp_bnot(SIGNAL_SWEEP_DONE));
	kp_unit_kill(g, n);
}

/* OrderCastRecall 0x42fc40 (order 137): the field opens at the order point and, 22 frames later, brings the caster's
 * units within 64 pixels of it. */
void kp_order_recall(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t cost = kp_tech_energy(g, KP_TECH_RECALL), x, y;
	if (u->order_state[n] == 1) {
		int64_t *found;
		int count, k, recalled = 0;
		if (u->order_timer[n] != 0) return;
		x = kp_s16v(u->order_x[n]);
		y = kp_s16v(u->order_y[n]);
		found = kp_query_rect(g, kp_s16v(x - RECALL_RADIUS), kp_s16v(y - RECALL_RADIUS), kp_s16v(x + RECALL_RADIUS),
			kp_s16v(y + RECALL_RADIUS), &count);
		for (k = 0; k < count; k++) {
			if (recallable(g, found[k], n) && recall_unit(g, found[k], n)) recalled = 1;
		}
		kp_query_release(g, found);
		if (recalled) kp_play_unit_sound(g, RECALLED_SOUND + kp_band(kp_rng_game_raw(g->rng, RECALLED_RANDOM), 1), n);
		kp_order_done(g, n);
		return;
	}
	if (kp_needs_energy(g, n, cost)) {
		/* the "not enough energy" message (0x46ae50) is the interface's */
		kp_order_done(g, n);
		return;
	}
	kp_spend_energy(g, n, cost);
	if (u->order_unit[n] != 0) {
		int32_t s = kp_unit_sprite(g, u->order_unit[n]);
		u->order_x[n] = kp_s16v(g->w->spr.x[s]);
		u->order_y[n] = kp_s16v(g->w->spr.y[s]);
	}
	x = kp_s16v(u->order_x[n]);
	y = kp_s16v(u->order_y[n]);
	recall_field(g, x, y, n);
	kp_play_sound_at(g, RECALL_SOUND + kp_band(kp_rng_game_raw(g->rng, RECALL_RANDOM), 1), x, y);
	u->order_timer[n] = RECALL_DELAY;
	u->order_state[n] = 1;
}

/* OrderCastInfestation 0x43cf10 (order 27): the Queen walks to a damaged Command Center. */
void kp_order_cast_infestation(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t t = u->order_unit[n], arrival;
	if (u->order_state[n] == 0) {
		if (t == 0 || !kp_infestable(g, t)) {
			/* the "not infestable" message (stat_txt 870) is the interface's */
			kp_order_done(g, n);
			return;
		}
		if (kp_request_move_to_unit(g, n, t)) u->order_state[n] = 1;
		return;
	}
	if (u->order_state[n] != 1) return;
	arrival = kp_move_arrival(g, n);
	if (arrival == 0) {
		if (t != 0) kp_move_toward_target(g, n, t);
		return;
	}
	if (t == 0 || !kp_infestable(g, t) || arrival == 2) {
		kp_order_done(g, n);
		return;
	}
	kp_issue_order_target(g, n, KP_ORD_INFESTING_COMMAND_CENTER, t);
}

/* OrderInfestingCommandCenter 0x43cfe0 (order 29): the Queen hides inside the Command Center for 60 frames and comes
 * out next to it. */
void kp_order_infesting(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t t = u->order_unit[n], x, y;
	int32_t s;
	if (u->order_state[n] == 0) {
		if (t != 0 && kp_infestable(g, t) && u->order[t] != KP_ORD_INFESTED_COMMAND_CENTER) {
			kp_unit_hide(g, n);
			kp_issue_order_target(g, t, KP_ORD_INFESTED_COMMAND_CENTER, n);
			u->order_state[n] = 3;
			u->build_time[n] = INFESTING_TIME;
			return;
		}
		kp_stop_and_face(g, n);
	} else if (u->order_state[n] == 3) {
		if (u->build_time[n] != 0) {
			u->build_time[n] = u->build_time[n] - 1;
			return;
		}
		s = kp_unit_sprite(g, n);
		if (!kp_find_unit_placement(g, n, kp_s16v(g->w->spr.x[s]), kp_s16v(g->w->spr.y[s]), NULL, 0, &x, &y)) return;
		kp_unit_relocate(g, n, x, y);
		kp_stop_and_face(g, n);
		kp_unit_show(g, n);
	} else {
		return;
	}
	if (u->queue_head[n] == KP_NONE) kp_order_append(g, n, KP_DAT(g, units, return_to_idle, u->type[n]), 0, 0, 0, KP_TYPE_NONE, 0);
	kp_order_unlock_done(g, n);
}

/* The Command Center's order 15 (0x43c5a0): four frames after the Queen enters, it becomes the infested type of the
 * Queen's owner at full hit points. */
void kp_order_infested_command_center(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t queen = u->order_unit[n], owner, type, timer;
	int completed;
	int32_t s;
	if (queen == 0 || !kp_infestable(g, n)) {
		kp_order_unlock_done(g, n);
		return;
	}
	if (u->order_state[n] == 0) {
		u->order_state[n] = 2;
		u->build_time[n] = INFESTED_DELAY;
		return;
	}
	timer = u->build_time[n];
	u->build_time[n] = kp_mod(timer - 1, 65536);
	if (timer != 0) return;
	if (u->union_0xc0[n] != 0) kp_detach_addon(g, n);
	if (kp_rally_producer(u->type[n])) u->rally_unit[n] = n;
	owner = u->owner[queen];
	type = KP_DAT(g, units, infestation, u->type[n]);
	kp_record_death(g, n, owner);
	completed = kp_band(u->status[n], STATUS_COMPLETED) != 0;
	kp_account_live(g, n, -1);
	if (completed) kp_account_completed(g, n, -1, 0);
	u->type[n] = type;
	kp_account_live(g, n, 1);
	if (completed) kp_account_completed(g, n, 1, 0);
	kp_change_owner(g, n, owner, 1);
	kp_set_sprite_owner(g, n, owner);
	kp_set_secondary_order_force(g, n, KP_ORD_NOTHING);
	kp_cancel_all_queued(g, n); /* 0x41b5c0 */
	kp_order_clear_queue(g, n);
	s = kp_unit_sprite(g, n);
	if (kp_band(u->status[n], STATUS_GROUNDED_BUILDING) != 0) kp_sprite_play(g->w, s, KP_ANIM_WORKING_TO_IDLE, 1);
	if (KP_DAT(g, units, construction_animation, type) < NO_IMAGE) {
		kp_sprite_image_above_main(g->w, s, (int32_t)KP_DAT(g, units, construction_animation, type), 0, 0, 0);
	}
	kp_set_unit_hp(g, n, KP_DAT(g, units, hit_points, type));
	kp_set_button_set(g, n, type);
	/* the interface refresh 0x4a7ed0 is the presentation's */
	kp_order_unlock_done(g, n);
}
