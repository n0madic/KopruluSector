/* Unit strength for the AI's force estimates (kp_game.h). */
#include "kp_game_internal.h"

#include <math.h>

#define SPELLCASTER 0x200000
#define HALLUCINATION 0x40000000
#define STRENGTH_SCALE 758
#define STRENGTH_DIVISOR 100

static int no_strength(int64_t t) { return t == 0x23 || t == 0x24 || t == 0x3b || t == 0x61; }

/* ComputeTypeStrength 0x4a9000. The square root is double arithmetic, which
 * IEEE 754 rounds the same everywhere (sqrt, * and / are correctly rounded). */
static int64_t type_strength(KpGame *g, int64_t t, int64_t w)
{
	int64_t shields = KP_DAT(g, units, shield_enabled, t) != 0 ? KP_DAT(g, units, shield_amount, t) : 0;
	int64_t life = kp_sarv(KP_DAT(g, units, hit_points, t), 8) + shields;
	int64_t damage, cooldown, factor, rate, burst = 0, total;
	double v;
	if (life == 0) return 0;
	damage = KP_DAT(g, weapons, damage_amount, w);
	cooldown = KP_DAT(g, weapons, weapon_cooldown, w);
	factor = KP_DAT(g, weapons, damage_factor, w);
	rate = kp_mulv(kp_mulv(kp_udiv64(g, KP_DAT(g, weapons, maximum_range, w), cooldown), factor), damage);
	if (w != KP_WEAPON_NONE) burst = kp_idiv64(g, kp_s32v(kp_shlv(kp_mulv(damage, factor), 11)), cooldown);
	total = kp_addv(kp_shrv(kp_mulv(burst, life), 8), rate);
	v = floor(sqrt((double)total) * STRENGTH_SCALE / STRENGTH_DIVISOR);
	return kp_mod((int64_t)v, 4294967296LL);
}

/* ApplyStrengthTypeModifier 0x4a8f60. */
static int64_t type_modifier(KpGame *g, int64_t t, int64_t v)
{
	if (t == 7 || t == 0x29 || t == 0x40) return kp_shrv(v, 2);
	if (t == 0xd || t == 0x49 || t == 0x55) return 0;
	if (t == 0x20 || t == 0x2b || t == 0x41) return kp_mod(v * 2, 4294967296LL);
	if (t == 0x2f || t == 0x32) return kp_shrv(v, 4);
	if (t == 0x53) return kp_udiv64(g, v, 10);
	return v;
}

static int64_t weapon_type_of(KpGame *g, int64_t t)
{
	int64_t sub;
	if (t == 0x48 || t == 0x52) return 0x49;
	if (t == 0x53 || t == 0x51) return 0x55;
	sub = KP_DAT(g, units, subunit1, t);
	return sub == KP_TYPE_NONE ? t : sub;
}

void kp_strength_init(KpGame *g)
{
	int64_t t;
	for (t = 0; t < KP_UNIT_TYPES; t++) {
		int64_t a = 0, gr = 0;
		if (!no_strength(t)) {
			int64_t wt = weapon_type_of(g, t), w;
			w = KP_DAT(g, units, air_weapon, wt);
			a = w == KP_WEAPON_NONE ? 1 : type_modifier(g, t, type_strength(g, t, w));
			w = KP_DAT(g, units, ground_weapon, wt);
			gr = w == KP_WEAPON_NONE ? 1 : type_modifier(g, t, type_strength(g, t, w));
		}
		g->strength[0][t] = (a == 1 && gr > 1) ? 0 : a;
		g->strength[1][t] = (gr == 1 && a > 1) ? 0 : gr;
	}
}

static int64_t type_table(KpGame *g, int kind, int64_t t)
{
	if (t < 0 || t >= KP_UNIT_TYPES) kp_game_fail(g, "strength: type %lld", (long long)t);
	return g->strength[kind][t];
}

int64_t kp_unit_type_strength(KpGame *g, int64_t n, int kind)
{
	KpUnits *u = &g->u;
	int64_t t = u->type[n], v = type_table(g, kind, t);
	if (t == KP_TYPE_BUNKER) v = kp_mulv(kp_cargo_space(g, n), type_table(g, kind, KP_TYPE_MARINE)); /* GetUsedCargoSpace 0x438350 */
	if (kp_band(KP_DAT(g, units, special_ability_flags, t), SPELLCASTER) != 0 && kp_band(u->status[n], HALLUCINATION) == 0) {
		return kp_shrv(kp_shrv(u->energy[n], 8), 1) + v;
	}
	return v;
}

int64_t kp_current_life(KpGame *g, int64_t n)
{
	int64_t life = kp_sarv(g->u.hp[n] + 0xff, 8);
	if (KP_DAT(g, units, shield_enabled, g->u.type[n]) != 0) life += kp_sarv(g->u.shields[n], 8);
	return life;
}

int64_t kp_max_hit_points(KpGame *g, int64_t n)
{
	int64_t v = kp_sarv(KP_DAT(g, units, hit_points, g->u.type[n]), 8);
	if (v == 0) {
		int64_t cur = kp_sarv(g->u.hp[n] + 0xff, 8);
		v = cur != 0 ? cur : 1;
	}
	return v;
}

int64_t kp_max_life(KpGame *g, int64_t n)
{
	int64_t t = g->u.type[n];
	return kp_max_hit_points(g, n) + (KP_DAT(g, units, shield_enabled, t) != 0 ? KP_DAT(g, units, shield_amount, t) : 0);
}

int64_t kp_unit_current_strength(KpGame *g, int64_t n, int kind)
{
	KpUnits *u = &g->u;
	int64_t t = u->type[n], s, life, shields, max;
	if (no_strength(t)) return 0;
	if (kp_band(u->status[n], HALLUCINATION) != 0 && kp_current_life(g, n) < kp_max_life(g, n)) return 0;
	s = kp_unit_type_strength(g, n, kind);
	life = kp_current_life(g, n);
	shields = KP_DAT(g, units, shield_enabled, t) != 0 ? KP_DAT(g, units, shield_amount, t) : 0;
	max = kp_max_hit_points(g, n) + shields;
	if (max == 0) max = 1;
	return kp_udiv64(g, kp_mulv(life, s), max);
}

void kp_refresh_unit_strength(KpGame *g, int64_t n)
{
	g->u.air_strength[n] = kp_mod(kp_unit_current_strength(g, n, 0), 65536);
	g->u.ground_strength[n] = kp_mod(kp_unit_current_strength(g, n, 1), 65536);
}
