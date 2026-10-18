/* Target acquisition and unit relations (kp_game.h). */
#include "kp_game_internal.h"

#define STATUS_INVINCIBLE 0x4000000
#define STATUS_AIR 0x4
#define STATUS_CLOAKED 0x300
#define STATUS_BURROWED 0x10
#define SPRITE_HIDDEN 0x20
#define DAT_SUBUNIT 0x10

static int subunit_type(KpGame *g, int64_t n)
{
	return n != 0 && kp_band(KP_DAT(g, units, special_ability_flags, g->u.type[n]), DAT_SUBUNIT) != 0;
}


int kp_is_enemy(KpGame *g, int64_t n, int64_t target)
{
	int64_t owner = g->u.owner[target];
	if (owner == KP_PLAYER_NEUTRAL) owner = g->w->spr.owner[kp_unit_sprite(g, target)];
	return kp_alliance(g, g->u.owner[n], owner) == 0;
}

int kp_is_undetected_by(KpGame *g, int64_t n, int64_t viewer)
{
	KpUnits *u = &g->u;
	return kp_band(u->status[n], STATUS_CLOAKED) != 0 && kp_band(u->visibility[n], (int64_t)1 << u->owner[viewer]) == 0;
}

int64_t kp_ground_weapon(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	if (u->type[n] == KP_TYPE_LURKER && kp_band(u->status[n], STATUS_BURROWED) == 0) return KP_WEAPON_NONE;
	return KP_DAT(g, units, ground_weapon, u->type[n]);
}

int64_t kp_ground_weapon_with_subunit(KpGame *g, int64_t n)
{
	int64_t w = kp_ground_weapon(g, n);
	if (w == KP_WEAPON_NONE && g->u.type[n] != KP_TYPE_LURKER) {
		int64_t sub = g->u.subunit[n];
		if (sub == 0) return KP_WEAPON_NONE;
		return KP_DAT(g, units, ground_weapon, g->u.type[sub]);
	}
	return w;
}

int64_t kp_air_weapon_with_subunit(KpGame *g, int64_t n)
{
	int64_t w = KP_DAT(g, units, air_weapon, g->u.type[n]);
	if (w == KP_WEAPON_NONE) {
		int64_t sub = g->u.subunit[n];
		if (sub == 0) return KP_WEAPON_NONE;
		return KP_DAT(g, units, air_weapon, g->u.type[sub]);
	}
	return w;
}

/* GetSeekRange 0x41f320: the bonus upgrade (or -1: the expansion) and bonus of a type. */
static int seek_bonus(int64_t t, int64_t *upgrade, int64_t *bonus)
{
	switch (t) {
	case KP_TYPE_MARINE: *upgrade = KP_UPG_U_238_SHELLS; *bonus = 1; return 1;
	case KP_TYPE_GOLIATH: case KP_TYPE_GOLIATH_TURRET: *upgrade = KP_UPG_CHARON_BOOSTERS; *bonus = 3; return 1;
	case KP_TYPE_ALAN_SCHEZAR: case KP_TYPE_ALAN_SCHEZAR_TURRET: *upgrade = -1; *bonus = 3; return 1;
	case KP_TYPE_HYDRALISK: *upgrade = KP_UPG_GROOVED_SPINES; *bonus = 1; return 1;
	case KP_TYPE_DRAGOON: *upgrade = KP_UPG_SINGULARITY_CHARGE; *bonus = 2; return 1;
	case KP_TYPE_FENIX_DRAGOON: *upgrade = -1; *bonus = 2; return 1;
	}
	return 0;
}

int64_t kp_seek_range(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t t = u->type[n], range, upgrade, bonus;
	if (kp_ghost_type(t) && kp_band(u->status[n], STATUS_CLOAKED) != 0 && u->order[n] != KP_ORD_HOLD_POSITION) return 0;
	range = KP_DAT(g, units, target_acquisition_range, t);
	if (seek_bonus(t, &upgrade, &bonus)) {
		int applies = upgrade < 0 ? g->expansion != 0 : kp_upgrade_level(g, u->owner[n], upgrade) != 0;
		if (applies) range += bonus;
	}
	return kp_mod(range, 256);
}

int kp_can_attack(KpGame *g, int64_t n, int64_t target, int check_detection)
{
	KpUnits *u = &g->u;
	int64_t ts, attacker, t;
	if (kp_unit_disabled(g, n) || target == 0) return 0;
	ts = u->status[target];
	if (kp_band(ts, STATUS_INVINCIBLE) != 0) return 0;
	if (kp_band(g->w->spr.flags[kp_unit_sprite(g, target)], SPRITE_HIDDEN) != 0) return 0;
	if (check_detection && kp_is_undetected_by(g, target, n)) return 0;
	attacker = subunit_type(g, u->subunit[n]) ? u->subunit[n] : n;
	t = u->type[n];
	if (t == KP_TYPE_CARRIER || t == KP_TYPE_GANTRITHOR) return 1;
	if (t == KP_TYPE_REAVER || t == KP_TYPE_WARBRINGER) {
		if (kp_band(ts, STATUS_AIR) != 0) return 0;
		return kp_same_ground_group(g, n, target);
	}
	if (t == KP_TYPE_QUEEN || t == KP_TYPE_MATRIARCH) return kp_infestable(g, target);
	if (t == KP_TYPE_ARBITER && u->ai[n] != 0) return 0;
	if (kp_band(ts, STATUS_AIR) == 0) return kp_ground_weapon(g, attacker) != KP_WEAPON_NONE;
	return KP_DAT(g, units, air_weapon, u->type[attacker]) != KP_WEAPON_NONE;
}
