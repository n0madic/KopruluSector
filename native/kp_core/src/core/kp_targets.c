/* Target acquisition and unit relations (kp_game.h; game/units/targets.lua, the specification). */
#include "kp_game_internal.h"

#define NO_WEAPON 0x82
#define NEUTRAL_OWNER 11
#define STATUS_INVINCIBLE 0x4000000
#define STATUS_AIR 0x4
#define STATUS_IN_BUILDING 0x20
#define STATUS_CLOAKED 0x300
#define STATUS_BURROWED 0x10
#define SPRITE_HIDDEN 0x20
#define DAT_SUBUNIT 0x10
#define CARRIER 0x48
#define GANTRITHOR 0x52
#define INTERCEPTOR 0x47
#define LURKER 0x67
#define QUERY_MARGIN 0x40
#define CLOAKED_SEEK_ORDER 0x6b

static int subunit_type(KpGame *g, int64_t n)
{
	return n != 0 && kp_band(KP_DAT(g, units, special_ability_flags, g->u.type[n]), DAT_SUBUNIT) != 0;
}

static int64_t alliance(KpGame *g, int64_t a, int64_t b) { return KP_GET(g, "alliance", a, NULL, b); }

int kp_is_enemy(KpGame *g, int64_t n, int64_t target)
{
	int64_t owner = g->u.owner[target];
	if (owner == NEUTRAL_OWNER) owner = g->w->spr.owner[kp_unit_sprite(g, target)];
	return alliance(g, g->u.owner[n], owner) == 0;
}

int kp_is_undetected_by(KpGame *g, int64_t n, int64_t viewer)
{
	KpUnits *u = &g->u;
	return kp_band(u->status[n], STATUS_CLOAKED) != 0 && kp_band(u->visibility[n], (int64_t)1 << u->owner[viewer]) == 0;
}

int64_t kp_ground_weapon(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	if (u->type[n] == LURKER && kp_band(u->status[n], STATUS_BURROWED) == 0) return NO_WEAPON;
	return KP_DAT(g, units, ground_weapon, u->type[n]);
}

int64_t kp_ground_weapon_with_subunit(KpGame *g, int64_t n)
{
	int64_t w = kp_ground_weapon(g, n);
	if (w == NO_WEAPON && g->u.type[n] != LURKER) {
		int64_t sub = g->u.subunit[n];
		if (sub == 0) return NO_WEAPON;
		return KP_DAT(g, units, ground_weapon, g->u.type[sub]);
	}
	return w;
}

int64_t kp_air_weapon_with_subunit(KpGame *g, int64_t n)
{
	int64_t w = KP_DAT(g, units, air_weapon, g->u.type[n]);
	if (w == NO_WEAPON) {
		int64_t sub = g->u.subunit[n];
		if (sub == 0) return NO_WEAPON;
		return KP_DAT(g, units, air_weapon, g->u.type[sub]);
	}
	return w;
}

/* GetSeekRange 0x41f320: the bonus upgrade (or -1: the expansion) and bonus of a type. */
static int seek_bonus(int64_t t, int64_t *upgrade, int64_t *bonus)
{
	switch (t) {
	case 0: *upgrade = 0x10; *bonus = 1; return 1;
	case 3: case 4: *upgrade = 0x36; *bonus = 3; return 1;
	case 17: case 18: *upgrade = -1; *bonus = 3; return 1;
	case 38: *upgrade = 0x1e; *bonus = 1; return 1;
	case 66: *upgrade = 0x21; *bonus = 2; return 1;
	case 78: *upgrade = -1; *bonus = 2; return 1;
	}
	return 0;
}

static int cloaked_no_seek(int64_t t) { return t == 1 || t == 16 || t == 100 || t == 99 || t == 104; }

int64_t kp_seek_range(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t t = u->type[n], range, upgrade, bonus;
	if (cloaked_no_seek(t) && kp_band(u->status[n], STATUS_CLOAKED) != 0 && u->order[n] != CLOAKED_SEEK_ORDER) return 0;
	range = KP_DAT(g, units, target_acquisition_range, t);
	if (seek_bonus(t, &upgrade, &bonus)) {
		int applies = upgrade < 0 ? g->expansion != 0 : KP_LUA2(g, "upgrade_level", u->owner[n], upgrade) != 0;
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
	if (t == CARRIER || t == GANTRITHOR) return 1;
	if (t == 0x53 || t == 0x51) {
		if (kp_band(ts, STATUS_AIR) != 0) return 0;
		return KP_LUA2(g, "same_ground_group", n, target) == 1;
	}
	if (t == 0x2d || t == 0x31) kp_game_fail(g, "targets: Queen target check (0x41fed0)");
	if (t == INTERCEPTOR && u->ai[n] != 0) return 0;
	if (kp_band(ts, STATUS_AIR) == 0) return kp_ground_weapon(g, attacker) != NO_WEAPON;
	return KP_DAT(g, units, air_weapon, u->type[attacker]) != NO_WEAPON;
}

static int64_t minimum_range(KpGame *g, int64_t n)
{
	int64_t gw = kp_ground_weapon_with_subunit(g, n), aw = kp_air_weapon_with_subunit(g, n);
	if (gw == NO_WEAPON) {
		if (aw != NO_WEAPON) return KP_DAT(g, weapons, minimum_range, aw);
		return 0;
	}
	if (aw != NO_WEAPON) {
		int64_t a = KP_DAT(g, weapons, minimum_range, gw), b = KP_DAT(g, weapons, minimum_range, aw);
		return a < b ? a : b;
	}
	return KP_DAT(g, weapons, minimum_range, gw);
}

static void consider(KpGame *g, int64_t n, int64_t c, int64_t range, int64_t min_range)
{
	if (!kp_can_attack(g, n, c, 1)) return;
	if (min_range != 0 && KP_LUA3(g, "unit_box_within", n, min_range, c) == 1) return;
	if (KP_LUA3(g, "unit_box_within", n, range, c) != 1) return;
	kp_game_fail(g, "targets: scoring of an attack candidate (0x41fe60, 0x48d2e0, 0x4319d0)");
}

/* FindUnitInRadiusBox 0x442b20 with 0x432280. */
static int64_t scan(KpGame *g, int64_t n, int64_t range, int64_t min_range)
{
	KpUnits *u = &g->u;
	int32_t s = kp_unit_sprite(g, n);
	int64_t x = kp_s16v(g->w->spr.x[s]), y = kp_s16v(g->w->spr.y[s]), r = range + QUERY_MARGIN;
	int64_t seen_by = (int64_t)1 << u->owner[n], found[KP_UNITS];
	int count = kp_units_in_rect(g, kp_s16v(x - r), kp_s16v(y - r), kp_s16v(x + r), kp_s16v(y + r), found, KP_UNITS), k;
	for (k = 0; k < count; k++) {
		int64_t c = found[k];
		int32_t cs = kp_unit_sprite(g, c);
		if (c != n && kp_band(g->w->spr.visibility[cs], seen_by) != 0) {
			int64_t owner = u->owner[c];
			if (owner == NEUTRAL_OWNER) owner = g->w->spr.owner[cs];
			if (alliance(g, u->owner[n], owner) == 0) consider(g, n, c, range, min_range);
		}
	}
	return 0;
}

int64_t kp_acquire_target(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t range = kp_seek_range(g, n);
	if (kp_band(u->status[n], STATUS_IN_BUILDING) != 0) {
		range += 2;
	} else if (u->ai[n] != 0) {
		int64_t sight = KP_DAT(g, units, sight_range, u->type[n]);
		if (range < sight) range = sight;
	}
	return scan(g, n, range * 32, minimum_range(g, n));
}

int64_t kp_acquire_random_target(KpGame *g, int64_t n)
{
	int64_t range = kp_seek_range(g, n) * 32;
	if (kp_band(g->u.status[n], STATUS_IN_BUILDING) != 0) range += 0x40;
	return scan(g, n, range, 0);
}
