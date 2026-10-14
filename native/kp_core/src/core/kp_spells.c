/* Spell casting (kp_game.h), ported from the atlas (P11): the target legality of technologies (0x434130 with 0x435380
 * and 0x435320), the abandon test of spell orders and of IScript `castspell` (0x434e70), the generic spell order
 * OrderCastSpell 0x434460 with its approach (0x435020, 0x435530), and the weapon a spell fires (0x41f4a0). */
#include "kp_game_internal.h"

#define TECHS 44
#define NO_WEAPON 130
#define NO_TYPE 228
#define SPIDER_MINES 3
#define INFESTATION 12
#define CONSUME 16
#define HALLUCINATION 20
#define MIND_CONTROL 27
#define FEEDBACK 29
#define COMMAND_CENTER 0x6a
#define INTERCEPTOR 0x49
#define LARVA 0x23
#define DAT_BUILDING 0x1
#define DAT_SPELLCASTER 0x200000
#define DAT_INVINCIBLE 0x20000000
#define STATUS_COMPLETED 0x1
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
/* the orders the AI's abandon test knows (0x434e70) */
#define ORDER_LOCKDOWN 115
#define ORDER_PARASITE 120
#define ORDER_EMP 122
#define ORDER_IRRADIATE 143
#define ORDER_PLAGUE 144
#define ORDER_ENSNARE 146
#define ORDER_STASIS 147

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
	if (kp_band(u->status[n], STATUS_COMPLETED) == 0 || u->type[n] != COMMAND_CENTER) return 0;
	life = kp_sarv(kp_s32v(u->hp[n]) + 0xff, 8);
	max = kp_sarv(kp_s32v(KP_DAT(g, units, hit_points, COMMAND_CENTER)), 8);
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
	case INFESTATION:
		return kp_infestable(g, target) ? 0 : ERR_NOT_INFESTABLE;
	case CONSUME:
		return building || u->owner[target] != owner || kp_unit_race(g, target) != 0 || t == LARVA ? ERR_CONSUME : 0;
	case HALLUCINATION:
		if (t == INTERCEPTOR) return ERR_INVALID;
		return building ? ERR_BUILDING : 0;
	case MIND_CONTROL:
		if (u->owner[target] == owner) return ERR_SAME_OWNER;
		if (building) return ERR_BUILDING;
		switch (t) {
		case 13: case 35: case 36: case 59: case 73: case 85: case 97: return ERR_INVALID;
		}
		return 0;
	case FEEDBACK:
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
	case INFESTATION: return type == COMMAND_CENTER ? 0 : ERR_NOT_INFESTABLE;
	case CONSUME: return ERR_CONSUME;
	}
	return ERR_INVALID;
}

/* PixelTileHasFlags 0x413220. */
static int tile_has(KpGame *g, int64_t x, int64_t y, int64_t required, int64_t forbidden)
{
	int64_t f = g->terrain.tiles[kp_idiv64(g, kp_s16v(y), 32) * g->map_w + kp_idiv64(g, kp_s16v(x), 32)];
	return kp_band(f, required) != 0 && kp_band(f, forbidden) == 0;
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
	if (tech == SPIDER_MINES) {
		if (tile_has(g, x, y, MINE_TILES, 0)) return 1;
		if (error) *error = ERR_MINE_TERRAIN;
		return 0;
	}
	if (target == 0 && type == NO_TYPE) {
		if (error) *error = ERR_NO_TARGET;
		return 0;
	}
	if (target == 0 || kp_band(u->status[target], STATUS_INVINCIBLE) != 0) {
		/* IsTypeAttackable 0x420a90 */
		if (type >= NO_TYPE || kp_band(flags(g, type), DAT_INVINCIBLE) != 0) {
			if (error) *error = ERR_INVALID;
			return 0;
		}
	}
	e = type == NO_TYPE ? on_unit(g, tech, target, u->owner[caster]) : on_fog_type(tech, type);
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
		case ORDER_LOCKDOWN: case ORDER_STASIS:
			if (target == 0 || kp_unit_disabled(g, target)) return 1;
			break;
		case ORDER_PARASITE:
			if (target == 0 || u->parasites[target] != 0) return 1;
			break;
		case ORDER_EMP:
			if (target != 0 && !(real_spellcaster(g, target) && kp_shrv(kp_mod(u->energy[target], 65536), 8) >= 100)) {
				if (KP_DAT(g, units, shield_enabled, u->type[target]) == 0) return 1;
				if (kp_s32v(kp_band(u->shields[target], 0xffffff00)) < 0x6400) return 1;
			}
			break;
		case ORDER_IRRADIATE:
			if (target == 0 || u->irradiate_timer[target] != 0) return 1;
			break;
		case ORDER_PLAGUE:
			if (target == 0 || u->plague_timer[target] != 0) return 1;
			break;
		case ORDER_ENSNARE:
			if (target != 0 && u->ensnare_timer[target] != 0) return 1;
			break;
		}
	}
	if (KP_DAT(g, orders, use_weapon_targeting, order) != 0) {
		return !kp_weapon_can_target(g, n, KP_DAT(g, orders, targeting, order), target);
	}
	tech = KP_DAT(g, orders, technology_energy, order);
	if (tech >= TECHS) return 0;
	return !kp_validate_spell_target(g, n, tech, target, NO_TYPE, kp_s16v(u->order_x[n]), kp_s16v(u->order_y[n]), NULL);
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

/* OrderCastSpell 0x434460 (orders 113, 115, 119-122, 142-147, 180, 181, 185, 186). */
void kp_order_cast_spell(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t order = u->order[n], weapon = KP_DAT(g, orders, targeting, order), tech, cost, c, target;
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
	/* GetOrderEnergyCost 0x4265e0 */
	tech = KP_DAT(g, orders, technology_energy, order);
	cost = tech < TECHS ? kp_mod(KP_DAT(g, techdata, energy_required, tech) * 256, 65536) : 0;
	if (kp_band(kp_scalar(g, KP_CHEATS), CHEAT_ENERGY) == 0 && kp_mod(u->energy[n], 65536) < cost) {
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
	/* SpendEnergy 0x435300 */
	if (kp_band(kp_scalar(g, KP_CHEATS), CHEAT_ENERGY) == 0) u->energy[n] = kp_mod(u->energy[n] - cost, 65536);
	c = kp_weapon_cooldown(g, n, weapon);
	u->spell_cooldown[n] = kp_mod(c + kp_band(kp_rng_game_raw(g->rng, SPELL_COOLDOWN_RANDOM), 3) - 1, 256);
	u->movement_flags[n] = kp_bor(u->movement_flags[n], ATTACKING);
	u->order_signal[n] = kp_band(u->order_signal[n], kp_bnot(SIGNAL_CAST));
	kp_sprite_play(g->w, kp_unit_sprite(g, n), (int32_t)KP_DAT(g, orders, iscript_animation, order), 1);
	u->order_state[n] = 2;
	if (u->ai[n] != 0) kp_game_fail(g, "spells: the computer's cast marks its PlayerAi (0x482ef0)");
}
