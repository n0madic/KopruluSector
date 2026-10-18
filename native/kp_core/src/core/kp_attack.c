/* The attack order, weapon range and readiness, cooldowns and target ranking (kp_game.h), ported from the atlas (P07,
 * P11) and docs/research/native-attack-orders.md, native-combat-ai.md. */
#include "kp_game_internal.h"

#include <string.h>

#define SIEGE_TURRET 31
#define STATUS_COMPLETED 0x1
#define STATUS_AIR 0x4
#define STATUS_BURROWED 0x10
#define STATUS_IN_BUILDING 0x20
#define STATUS_HALTED 0x8000
#define STATUS_CAN_TURN 0x10000
#define STATUS_CAN_MOVE 0x20000
#define STATUS_UNREACHABLE 0x80
#define STATUS_TARGET_SWITCH 0x8000000
#define STATUS_FRENZY 0x20000000
#define DAT_WORKER 0x08
#define DAT_SUBUNIT 0x10
#define MOVING 0x2
#define ATTACKING 0x8
#define PATHING_NO_ATTACK 0x2
#define RANK_NONE 0x7fffffff
#define RANKS 6
#define RANK_SLOTS 16
#define QUERY_MARGIN 0x40
#define COOLDOWN_RANDOM 12
#define IN_PLACE_RANDOM 13
#define RANDOM_TARGET 19
#define COOLDOWN_MIN 5
#define COOLDOWN_MAX 250
#define MOTION_HEADING 0x20
#define MELEE_LEAD 3
#define FACE_TARGET 32
#define TANK_TURRET 6
#define SIEGE_PROBE 512
#define STATUS_CLOAK_BITS 0x300

static int64_t flags(KpGame *g, int64_t t) { return KP_DAT(g, units, special_ability_flags, t); }

/* IsSubunit 0x420000. */
static int is_subunit(KpGame *g, int64_t n) { return n != 0 && kp_band(flags(g, g->u.type[n]), DAT_SUBUNIT) != 0; }

/* The unit whose weapons fire: a subunit, else the unit itself. */
static int64_t weapon_unit(KpGame *g, int64_t n)
{
	int64_t sub = g->u.subunit[n];
	return is_subunit(g, sub) ? sub : n;
}

/* The weapon of weapon unit w against target: the air weapon for an airborne target, else the ground weapon (none for
 * an unburrowed Lurker). */
static int64_t weapon_against(KpGame *g, int64_t w, int64_t target)
{
	if (kp_band(g->u.status[target], STATUS_AIR) != 0) return KP_DAT(g, units, air_weapon, g->u.type[w]);
	return kp_ground_weapon(g, w);
}

/* GetWeaponMaxRange 0x420f00. */
int64_t kp_weapon_max_range(KpGame *g, int64_t n, int64_t weapon)
{
	KpUnits *u = &g->u;
	int64_t bonus = kp_band(u->status[n], STATUS_IN_BUILDING) != 0 ? 0x40 : 0, owner = u->owner[n];
	weapon = kp_mod(weapon, 256);
	switch (u->type[n]) {
	case 0: /* Marine: U-238 Shells */
		if (kp_upgrade_level(g, owner, 0x10) != 0) bonus += 0x20;
		break;
	case 38: /* Hydralisk: Grooved Spines */
		if (kp_upgrade_level(g, owner, 0x1e) != 0) bonus += 0x20;
		break;
	case 66: /* Dragoon: Singularity Charge */
		if (kp_upgrade_level(g, owner, 0x21) != 0) bonus += 0x40;
		break;
	case 78: /* Fenix (Dragoon) */
		if (g->expansion != 0) bonus += 0x40;
		break;
	case 3: case 4: /* Goliath and its turret: Charon Boosters */
		if (weapon == 8 && kp_upgrade_level(g, owner, 0x36) != 0) bonus += 0x60;
		break;
	case 17: case 18: /* Alan Schezar and his turret */
		if (weapon == 10 && g->expansion != 0) bonus += 0x60;
		break;
	}
	return kp_u32v(KP_DAT(g, weapons, maximum_range, weapon) + bonus);
}

/* CanUnitSeeTarget 0x422c80: target, or the order point without one. */
int kp_can_see_target(KpGame *g, int64_t n, int64_t target)
{
	KpUnits *u = &g->u;
	int64_t bit = (int64_t)1 << (u->owner[n] & 31);
	if (target != 0) {
		if (kp_band(u->status[target], 0x300) != 0 && kp_band(u->visibility[target], bit) == 0) return 0;
		return kp_band(g->w->spr.visibility[kp_unit_sprite(g, target)], kp_band(bit, 0xff)) != 0;
	} else {
		int64_t ty = kp_idiv64(g, kp_s16v(u->order_y[n]), 32), tx = kp_idiv64(g, kp_s16v(u->order_x[n]), 32);
		int64_t tile = g->terrain.tiles[ty * g->map_w + tx];
		return kp_band(kp_bnot(kp_band(tile, 0xff)), bit) != 0;
	}
}

/* IsWithinRangeWithMotion 0x421120: a moving unit reaches its halt distance further, unless its target moves the same
 * way. */
int kp_within_range_moving(KpGame *g, int64_t n, int64_t range, int64_t target)
{
	KpUnits *u = &g->u;
	if (target == 0) target = u->order_unit[n];
	if (kp_band(u->movement_flags[n], MOVING) != 0
		&& (kp_band(u->movement_flags[target], MOVING) == 0
			|| kp_heading_difference(u->velocity_direction[n], u->velocity_direction[target]) > MOTION_HEADING)) {
		range += kp_sarv(kp_flingy_halt_distance(g, n), 8);
	}
	return kp_unit_box_within(g, n, range, target);
}

/* IsTargetInRange 0x421060: target, or the order target without one. */
int kp_target_in_range(KpGame *g, int64_t n, int64_t target)
{
	int64_t w, weapon, min;
	if (target == 0) {
		target = g->u.order_unit[n];
		if (target == 0) return 1;
	}
	if (!kp_can_see_target(g, n, target)) return 0;
	w = weapon_unit(g, n);
	weapon = weapon_against(g, w, target);
	if (weapon >= KP_WEAPON_NONE) return 0;
	min = KP_DAT(g, weapons, minimum_range, weapon);
	if (min != 0 && kp_unit_box_within(g, w, min, target)) return 0;
	return kp_within_range_moving(g, w, kp_weapon_max_range(g, n, weapon), target);
}

/* IsTargetOutOfRange 0x420ab0 (no minimum range). */
int kp_target_out_of_range(KpGame *g, int64_t n, int64_t target)
{
	int64_t w, weapon;
	if (!kp_can_see_target(g, n, target)) return 1;
	w = weapon_unit(g, n);
	weapon = weapon_against(g, w, target);
	if (weapon == KP_WEAPON_NONE) return 0;
	return !kp_within_range_moving(g, w, kp_weapon_max_range(g, n, weapon), target);
}

/* The angle between heading and direction d, 0 to 128. */
static int64_t heading_off(int64_t heading, int64_t d)
{
	int64_t e = heading - d;
	if (e < 0) e += 0x100;
	return e > 0x80 ? 0x100 - e : e;
}

/* IsFacingPoint 0x41fe60: within the weapon's attack angle of (x, y); a Lurker turns at once. */
static int facing_point(KpGame *g, int64_t n, int64_t weapon, int64_t x, int64_t y)
{
	KpUnits *u = &g->u;
	int32_t s = kp_unit_sprite(g, n);
	int64_t d = kp_direction_to(g, kp_s16v(g->w->spr.x[s]), kp_s16v(g->w->spr.y[s]), x, y);
	if (u->type[n] == KP_TYPE_LURKER) {
		u->heading[n] = d;
		return 1;
	}
	return heading_off(u->heading[n], d) <= KP_DAT(g, weapons, attack_angle, kp_mod(weapon, 256));
}

/* IsWeaponReady 0x41ff50. */
int kp_weapon_ready(KpGame *g, int64_t n, int64_t weapon)
{
	KpUnits *u = &g->u;
	int64_t dist, min;
	int32_t s;
	if (kp_band(u->movement_flags[n], ATTACKING) != 0) return 0;
	if (u->order_unit[n] != 0) dist = kp_unit_distance(g, n, u->order_unit[n]);
	else dist = kp_unit_distance_to_point(g, n, u->order_x[n], u->order_y[n]);
	min = KP_DAT(g, weapons, minimum_range, weapon);
	if (min != 0 && dist < min) return 0;
	if (dist > kp_weapon_max_range(g, n, weapon)) return 0;
	if (kp_band(u->pathing_flags[n], PATHING_NO_ATTACK) != 0) return 0;
	s = kp_unit_sprite(g, n);
	if ((u->target_waypoint_x[n] != kp_s16v(g->w->spr.x[s]) || u->target_waypoint_y[n] != kp_s16v(g->w->spr.y[s]))
		&& !facing_point(g, n, weapon, u->target_waypoint_x[n], u->target_waypoint_y[n])) {
		u->queue_timer[n] = 0;
		return 0;
	}
	return 1;
}

/* GetWeaponCooldown 0x41fb70: the cooldown before its jitter. */
int64_t kp_weapon_cooldown(KpGame *g, int64_t n, int64_t weapon)
{
	KpUnits *u = &g->u;
	int64_t v = KP_DAT(g, weapons, weapon_cooldown, kp_mod(weapon, 256)), spores = u->acid_spores[n], modifier;
	if (spores != 0) {
		int64_t step = kp_shrv(v, 3);
		if (step < 3) step = 3;
		v = kp_u32v(v + kp_mulv(step, spores));
	}
	/* GetAttackSpeedModifier 0x428ac0 */
	modifier = (u->stim_timer[n] != 0) + (kp_band(u->status[n], STATUS_FRENZY) != 0) - (u->ensnare_timer[n] != 0);
	if (modifier < 0) v = kp_u32v(v + kp_shrv(v, 2));
	else if (modifier > 0) v = kp_shrv(v, 1);
	if (v > COOLDOWN_MAX) v = COOLDOWN_MAX;
	if (v < COOLDOWN_MIN) v = COOLDOWN_MIN;
	return v;
}

static int64_t *cooldown(KpGame *g, int selector)
{
	return selector == 1 ? g->u.ground_cooldown : selector == 2 ? g->u.air_cooldown : g->u.spell_cooldown;
}

/* TryStartWeaponCooldown 0x41f530: selector 1 (ground) or 2 (air). */
static int try_start_cooldown(KpGame *g, int64_t n, int selector, int64_t weapon, int64_t anim)
{
	KpUnits *u = &g->u;
	int64_t c;
	if (weapon == KP_WEAPON_NONE) return 0;
	c = cooldown(g, selector)[n];
	if (c != 0) {
		if (u->queue_timer[n] > c - 1) u->queue_timer[n] = c - 1;
		return 0;
	}
	if (!kp_weapon_ready(g, n, weapon)) return 0;
	u->movement_flags[n] = kp_bor(u->movement_flags[n], ATTACKING);
	c = kp_weapon_cooldown(g, n, weapon);
	c = kp_mod(c + kp_band(kp_rng_game_raw(g->rng, COOLDOWN_RANDOM), 3) - 1, 256);
	cooldown(g, selector)[n] = c;
	if (selector == 1) u->air_cooldown[n] = u->ground_cooldown[n];
	else if (selector == 2) u->ground_cooldown[n] = u->air_cooldown[n];
	kp_sprite_play(g->w, kp_unit_sprite(g, n), (int32_t)anim, 1);
	return 1;
}

/* StartAttackAnimation 0x41f630: base animation anim (the air attack is the next one). */
int kp_start_attack_animation(KpGame *g, int64_t n, int64_t anim)
{
	KpUnits *u = &g->u;
	int64_t target = u->order_unit[n];
	kp_clear_last_bullet_spawner(g);
	if (kp_band(u->status[n], STATUS_HALTED) != 0) return 0;
	if (target != 0 && kp_band(u->status[target], STATUS_AIR) != 0) {
		return try_start_cooldown(g, n, 2, KP_DAT(g, units, air_weapon, u->type[n]), kp_mod(anim + 1, 256));
	}
	return try_start_cooldown(g, n, 1, kp_ground_weapon(g, n), anim);
}

/* UnitHasWeapon 0x41d9c0. */
int kp_unit_has_weapon(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t t = u->type[n], sub;
	if (kp_ground_weapon(g, n) != KP_WEAPON_NONE) return 1;
	if (KP_DAT(g, units, air_weapon, t) != KP_WEAPON_NONE) return 1;
	if ((t == KP_TYPE_CARRIER || t == KP_TYPE_GANTRITHOR) && kp_mod(u->union_0xc8[n] + kp_shrv(u->union_0xc8[n], 8), 256) != 0) return 1;
	if ((t == KP_TYPE_REAVER || t == KP_TYPE_WARBRINGER) && kp_mod(u->union_0xc8[n], 256) != 0) return 1;
	sub = u->subunit[n];
	if (sub == 0) return 0;
	if (kp_ground_weapon(g, sub) != KP_WEAPON_NONE) return 1;
	return KP_DAT(g, units, air_weapon, u->type[sub]) != KP_WEAPON_NONE;
}

/* FirstCargoUnit 0x438140. */
int64_t kp_first_cargo(KpGame *g, int64_t n)
{
	int64_t space = KP_DAT(g, units, space_provided, g->u.type[n]);
	int k;
	for (k = 0; k < space && k < 8; k++) {
		int64_t c = kp_cargo_unit(g, n, k);
		if (c != 0) return c;
	}
	return 0;
}

/* GetTargetPriority 0x4319d0: 0 (best) to 5. */
int64_t kp_target_priority(KpGame *g, int64_t n, int64_t target)
{
	KpUnits *u = &g->u;
	int64_t c = target, t = u->type[target], r;
	if (t == KP_TYPE_BUNKER) {
		int64_t first = kp_first_cargo(g, target);
		if (first != 0) c = first;
	} else if (t == 0x23 || t == 0x24 || t == 0x3b || t == 0x61) {
		return 5;
	}
	if (kp_band(flags(g, u->type[c]), DAT_WORKER) != 0) r = 2;
	else if (kp_can_attack(g, c, n, 1)) r = 0;
	else if (kp_unit_has_weapon(g, c)) r = 2;
	else r = kp_band(u->status[c], STATUS_CAN_MOVE) != 0 ? 3 : 4;
	if (kp_band(u->status[c], STATUS_COMPLETED) == 0 || c != target) r++;
	if (r == 0 && kp_band(u->status[c], STATUS_HALTED) != 0) r = 1;
	return r;
}

/* ShouldRejectChase 0x48d2e0: a computer's ground unit does not chase a target marked unreachable. */
int kp_reject_chase(KpGame *g, int64_t n, int64_t target)
{
	KpUnits *u = &g->u;
	if (kp_controller(g, u->owner[n]) != KP_CTRL_COMPUTER || kp_band(u->status[n], STATUS_AIR) != 0) return 0;
	if (kp_target_in_range(g, n, target)) return 0;
	return kp_band(u->status[target], STATUS_UNREACHABLE) != 0;
}

/* The candidate buckets of AcquireBestTarget (0x665b70, counts 0x665cf0, ranges 0x665d1c/0x665d20). */
typedef struct {
	int64_t unit[RANKS][RANK_SLOTS];
	int count[RANKS];
	int64_t max_range, min_range;
} Buckets;

/* The callback 0x432280 for candidate c. */
static void bucket_candidate(KpGame *g, int64_t n, int64_t c, Buckets *b)
{
	KpUnits *u = &g->u;
	int64_t w, rank;
	if (c == n || kp_band(g->w->spr.visibility[kp_unit_sprite(g, c)], kp_band((int64_t)1 << u->owner[n], 0xff)) == 0) return;
	if (!kp_can_attack(g, n, c, 1) || !kp_is_enemy(g, n, c)) return;
	if (b->min_range != 0 && kp_unit_box_within(g, n, b->min_range, c)) return;
	if (!kp_unit_box_within(g, n, b->max_range, c)) return;
	w = is_subunit(g, u->subunit[n]) ? u->subunit[n] : n;
	if (kp_band(u->status[w], STATUS_CAN_TURN) == 0) {
		int32_t s = kp_unit_sprite(g, c);
		if (!facing_point(g, w, KP_DAT(g, units, ground_weapon, u->type[w]), kp_s16v(g->w->spr.x[s]),
				kp_s16v(g->w->spr.y[s]))) {
			return;
		}
	}
	if (kp_reject_chase(g, n, c)) return;
	rank = kp_target_priority(g, n, c);
	if (b->count[rank] < RANK_SLOTS) b->unit[rank][b->count[rank]++] = c;
}

/* GetCurrentLife 0x41faa0: whole hit points and shields. */
static int64_t current_life(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t life = kp_sarv(u->hp[n] + 0xff, 8);
	if (KP_DAT(g, units, shield_enabled, u->type[n]) != 0) life += kp_sarv(u->shields[n], 8);
	return kp_u32v(life);
}

/* ChooseFromBucket 0x433910: a computer's Scourge takes the healthiest, every other unit the nearest. */
static int64_t choose_from_bucket(KpGame *g, int64_t n, const Buckets *b, int rank)
{
	const int64_t *c = b->unit[rank];
	int count = b->count[rank], k, best = 0;
	int64_t value;
	int32_t s = kp_unit_sprite(g, n);
	if (count == 1) return c[0];
	if (g->u.ai[n] != 0 && g->u.type[n] == KP_TYPE_SCOURGE) {
		value = current_life(g, c[0]);
		for (k = 1; k < count; k++) {
			int64_t life = current_life(g, c[k]);
			if (life > value) {
				value = life;
				best = k;
			}
		}
		return c[best];
	}
	value = KP_NIL;
	for (k = 0; k < count; k++) {
		int32_t cs = kp_unit_sprite(g, c[k]);
		int64_t d = kp_approx_distance(kp_s16v(g->w->spr.x[cs]) - kp_s16v(g->w->spr.x[s]),
			kp_s16v(g->w->spr.y[cs]) - kp_s16v(g->w->spr.y[s]));
		if (k == 0 || d < value) {
			value = d;
			best = k;
		}
	}
	return c[best];
}

/* The smallest minimum range of the ground and air weapons, 0 without either (0x433c50). */
static int64_t acquisition_min_range(KpGame *g, int64_t n)
{
	int64_t gw = kp_ground_weapon_with_subunit(g, n), aw = kp_air_weapon_with_subunit(g, n);
	if (gw == KP_WEAPON_NONE) return aw == KP_WEAPON_NONE ? 0 : KP_DAT(g, weapons, minimum_range, aw);
	if (aw == KP_WEAPON_NONE) return KP_DAT(g, weapons, minimum_range, gw);
	{
		int64_t a = KP_DAT(g, weapons, minimum_range, gw), c = KP_DAT(g, weapons, minimum_range, aw);
		return c < a ? c : a;
	}
}

/* The candidates within max_range (and not within a nonzero min_range) around unit n, by rank (the walk of
 * FindUnitInRadiusBox 0x442b20 with the callback 0x432280, which never stops it). */
static void bucket_candidates(KpGame *g, int64_t n, Buckets *b)
{
	int64_t x, y, r, *found;
	int32_t s = kp_unit_sprite(g, n);
	int count, k;
	x = kp_s16v(g->w->spr.x[s]);
	y = kp_s16v(g->w->spr.y[s]);
	r = b->max_range + QUERY_MARGIN;
	found = kp_query_rect(g, kp_s16v(x - r), kp_s16v(y - r), kp_s16v(x + r), kp_s16v(y + r), &count);
	for (k = 0; k < count; k++) bucket_candidate(g, n, found[k], b);
	kp_query_release(g, found);
}

static int first_rank(const Buckets *b)
{
	int rank;
	for (rank = 0; rank < RANKS; rank++) {
		if (b->count[rank] != 0) return rank;
	}
	return -1;
}

/* AcquireBestTarget 0x431c40. */
int64_t kp_acquire_target(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	Buckets b;
	int64_t tiles = kp_seek_range(g, n);
	int rank;
	if (kp_band(u->status[n], STATUS_IN_BUILDING) != 0) {
		tiles += 2;
	} else if (u->ai[n] != 0) {
		int64_t sight = KP_DAT(g, units, sight_range, u->type[n]);
		if (sight > tiles) tiles = sight;
	}
	memset(b.count, 0, sizeof b.count);
	b.max_range = tiles * 32;
	b.min_range = acquisition_min_range(g, n);
	bucket_candidates(g, n, &b);
	rank = first_rank(&b);
	return rank < 0 ? 0 : choose_from_bucket(g, n, &b, rank);
}

/* AcquireRandomTarget 0x431b70. */
int64_t kp_acquire_random_target(KpGame *g, int64_t n)
{
	Buckets b;
	int rank;
	memset(b.count, 0, sizeof b.count);
	b.max_range = kp_seek_range(g, n) * 32;
	if (kp_band(g->u.status[n], STATUS_IN_BUILDING) != 0) b.max_range += 0x40;
	b.min_range = 0;
	bucket_candidates(g, n, &b);
	rank = first_rank(&b);
	if (rank < 0) return 0;
	if (b.count[rank] == 1) return b.unit[rank][0];
	return b.unit[rank][kp_rng_game_raw(g->rng, RANDOM_TARGET) % (uint32_t)b.count[rank]];
}

/* UpdateAttackTarget 0x41de10. */
void kp_update_attack_target(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t t, rank, a, c, sub;
	if (kp_band(u->status[n], STATUS_TARGET_SWITCH) == 0) return;
	t = u->order_unit[n];
	if (t == 0) {
		rank = RANK_NONE;
	} else {
		if (u->ai[n] != 0 && !kp_is_enemy(g, n, t)) return;
		if (!kp_can_attack(g, n, t, 1) || kp_reject_chase(g, n, t)) {
			t = 0;
			rank = RANK_NONE;
		} else {
			rank = kp_target_priority(g, n, t);
			if (rank == 0) {
				if (kp_target_in_range(g, n, t)) return;
				rank = 1;
			}
		}
	}
	a = u->auto_target[n];
	if (a != 0) {
		if (kp_can_attack(g, n, a, 1) && kp_is_enemy(g, n, a) && !kp_reject_chase(g, n, a)) {
			int64_t r = kp_target_priority(g, n, a);
			if (r == 0 && !kp_target_in_range(g, n, a)) r = 1;
			if (r < rank) {
				t = a;
				rank = r;
			}
		} else {
			u->auto_target[n] = 0;
		}
	}
	if (t == 0 || rank != 0) {
		if (u->ai[n] != 0 && kp_ai_update_attack_target(g, n, 0, 1, 0)) return;
		c = kp_acquire_target(g, n);
		if (c != 0 && kp_target_priority(g, n, c) < rank) t = c;
	}
	if (t != 0 && !kp_can_attack(g, n, t, 1)) return;
	u->order_unit[n] = t;
	sub = u->subunit[n];
	if (sub != 0) {
		int64_t order = u->order[sub];
		if (order == KP_DAT(g, units, attack_unit, u->type[sub]) || order == KP_ORD_HOLD_POSITION) u->order_unit[sub] = t;
	}
}

/* PrependOrNewOrderAtPoint 0x429b70. */
static void prepend_order_at(KpGame *g, int64_t n, int64_t order, int64_t x, int64_t y)
{
	kp_order_prepend_at(g, n, order, x, y, 0);
}

/* RequestMoveToPointThrottled 0x438b80: 0 only when an AI transport request took the move over. */
int kp_move_to_point_throttled(KpGame *g, int64_t n, int64_t x, int64_t y)
{
	if (g->u.recent_order_timer[n] != 0) return 1;
	return kp_set_move_target_point(g, n, x, y);
}

/* ApproachMovingTarget 0x41d8f0: toward where a moving target will be. */
static void approach_moving_target(KpGame *g, int64_t n, int64_t target)
{
	KpUnits *u = &g->u;
	int32_t s;
	int64_t lead, ox, oy, tx, ty, px, py, dir;
	if (!kp_can_see_target(g, n, target) || kp_band(u->movement_flags[target], MOVING) == 0) {
		kp_move_toward_target(g, n, target);
		return;
	}
	s = kp_unit_sprite(g, target);
	tx = kp_s16v(g->w->spr.x[s]);
	ty = kp_s16v(g->w->spr.y[s]);
	lead = kp_mulv(kp_flingy_halt_distance(g, n), MELEE_LEAD);
	/* GetDirectionVector 0x40c270 */
	dir = u->velocity_direction[target];
	kp_direction_vector(dir, lead, &ox, &oy);
	px = kp_u32v(tx + kp_sarv(ox, 8));
	py = kp_u32v(ty + kp_sarv(oy, 8));
	if (px < kp_mod(g->map_w_px, 65536) && py < kp_mod(g->map_h_px, 65536)) kp_move_to_point_throttled(g, n, px, py);
	else kp_move_to_point_throttled(g, n, tx, ty);
}

/* AttackUnitMain 0x420770: 1 when the caller may fire. */
static int attack_unit_main(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t target = u->order_unit[n], gw;
	int32_t s;
	if (!kp_can_attack(g, n, target, 1)) {
		if (target != 0 && kp_band(u->status[n], STATUS_TARGET_SWITCH) == 0) {
			if (u->ai[n] == 0) {
				s = kp_unit_sprite(g, target);
				prepend_order_at(g, n, KP_ORD_MOVE, kp_s16v(g->w->spr.x[s]), kp_s16v(g->w->spr.y[s]));
			}
			kp_order_done(g, n);
			return 0;
		}
		kp_stop_movement(g, n);
		kp_order_done(g, n);
		return 0;
	}
	s = kp_unit_sprite(g, target);
	u->order_x[n] = kp_s16v(g->w->spr.x[s]);
	u->order_y[n] = kp_s16v(g->w->spr.y[s]);
	kp_flingy_set_target_waypoint(g, n, u->order_x[n], u->order_y[n]);
	if (u->order_state[n] == 0) {
		if (!kp_target_in_range(g, n, 0) && !kp_set_move_target_unit(g, n, u->order_unit[n])) return 0;
		u->order_state[n] = 1;
		return 1;
	}
	target = u->order_unit[n];
	if (!kp_target_out_of_range(g, n, target)) {
		if (u->type[n] == KP_TYPE_SCOURGE) approach_moving_target(g, n, target);
		else kp_stop_movement(g, n);
		return 1;
	}
	if (u->ai[n] != 0) {
		if (kp_unit_unreachable(g, n, target) || kp_move_arrival(g, n) == 2) {
			if (kp_ai_update_attack_target(g, n, 0, 1, 0)) return 0;
			kp_stop_and_face(g, n);
			kp_order_done(g, n);
			return 0;
		}
	} else if ((kp_band(u->status[n], STATUS_TARGET_SWITCH) != 0 && kp_band(u->status[n], 0xc) == 0
				&& kp_band(u->status[target], STATUS_AIR) != 0)
		|| kp_move_arrival(g, n) == 2) {
		kp_stop_and_face(g, n);
		kp_order_done(g, n);
		return 0;
	}
	gw = KP_DAT(g, units, ground_weapon, u->type[n]);
	if (u->type[n] != KP_TYPE_SCOURGE && (gw == KP_WEAPON_NONE || KP_DAT(g, weapons, graphics, gw) != 0)) {
		kp_move_toward_target(g, n, target);
	} else {
		approach_moving_target(g, n, target);
	}
	return 0;
}

/* Order_AttackUnit 0x41e5c0. */
void kp_order_attack_unit(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	if (kp_controller(g, u->owner[n]) == KP_CTRL_COMPUTER) {
		int64_t target = u->order_unit[n];
		if ((target == 0 || kp_band(flags(g, u->type[target]), 1) == 0) && kp_ai_guard_return_home(g, n, 0)) return;
	}
	kp_update_attack_target(g, n);
	if (!attack_unit_main(g, n)) return;
	if (u->ai[n] != 0) kp_ai_consider_stim(g, n);
	kp_start_attack_animation(g, n, KP_ANIM_GND_ATTK_RPT);
}

/* AttackFixedRange 0x41e400 (order 0x0b): the Lurker's attack, never moving; out of range a computer's unit attack-moves
 * to the target and a human's ends the order. */
void kp_order_attack_fixed_range(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t target;
	if (u->ai[n] != 0) {
		u->order_unit[n] = kp_acquire_target(g, n);
	} else {
		kp_update_attack_target(g, n);
	}
	target = u->order_unit[n];
	if (target != 0) {
		int32_t s = kp_unit_sprite(g, target);
		u->order_x[n] = kp_s16v(g->w->spr.x[s]);
		u->order_y[n] = kp_s16v(g->w->spr.y[s]);
	}
	if (!kp_can_attack(g, n, target, 1)) {
		kp_order_done(g, n);
		return;
	}
	if (!kp_target_in_range(g, n, target)) {
		if (u->ai[n] == 0) {
			kp_order_done(g, n);
			return;
		}
		kp_issue_order_at(g, n, KP_ORD_ATTACK_MOVE, kp_s16v(u->order_x[n]), kp_s16v(u->order_y[n]));
		return;
	}
	kp_flingy_set_target_waypoint(g, n, kp_s16v(u->order_x[n]), kp_s16v(u->order_y[n]));
	kp_start_attack_animation(g, n, KP_ANIM_GND_ATTK_RPT);
}

/* OrderStayInRange 0x4288f0 (order 21): a vehicle's body keeps its turret's target in range. */
void kp_order_stay_in_range(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t target = u->order_unit[n], t;
	int32_t s;
	if (target == 0) {
		kp_stop_and_face(g, n);
		kp_order_done(g, n);
		return;
	}
	s = kp_unit_sprite(g, target);
	u->order_x[n] = kp_s16v(g->w->spr.x[s]);
	u->order_y[n] = kp_s16v(g->w->spr.y[s]);
	if (!kp_can_attack(g, n, target, 1)) {
		if (kp_band(u->status[n], STATUS_TARGET_SWITCH) == 0 && u->ai[n] == 0) {
			prepend_order_at(g, n, KP_ORD_MOVE, u->order_x[n], u->order_y[n]);
		}
		kp_order_done(g, n);
		return;
	}
	if (u->order_state[n] == 0) {
		if (!kp_target_in_range(g, n, 0) && !kp_set_move_target_unit(g, n, u->order_unit[n])) return;
		u->order_state[n] = 1;
	} else if (kp_target_in_range(g, n, 0)) {
		kp_stop_and_face(g, n);
	} else if (!kp_move_toward_target(g, n, u->order_unit[n])) {
		return;
	}
	t = u->type[n];
	if ((t == KP_TYPE_GOLIATH || t == KP_TYPE_ALAN_SCHEZAR) && kp_band(u->movement_flags[n], MOVING) == 0) {
		int64_t d, delta;
		s = kp_unit_sprite(g, n);
		d = kp_direction_to(g, kp_s16v(g->w->spr.x[s]), kp_s16v(g->w->spr.y[s]), u->order_x[n], u->order_y[n]);
		/* LimitTurn 0x40ced0 with the limit 128: the signed byte difference */
		delta = kp_s8((uint32_t)(d - u->heading[n]));
		if (delta < 0) delta = -delta;
		if (delta >= FACE_TARGET) kp_flingy_set_target_waypoint(g, n, u->order_x[n], u->order_y[n]);
	}
}

/* Order_TurretAttack 0x41f170 (order 22): a turret aims and fires while its body keeps it in range. */
void kp_order_turret_attack(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t target, parent;
	kp_update_attack_target(g, n);
	target = u->order_unit[n];
	if (target != 0) {
		int32_t s = kp_unit_sprite(g, target);
		u->order_x[n] = kp_s16v(g->w->spr.x[s]);
		u->order_y[n] = kp_s16v(g->w->spr.y[s]);
	}
	if (!kp_can_attack(g, n, target, 1) || !kp_can_see_target(g, n, u->order_unit[n])) {
		kp_issue_order(g, n, KP_ORD_TURRET_GUARD);
		return;
	}
	parent = u->subunit[n];
	if (u->type[n] == TANK_TURRET && u->ai[parent] != 0) {
		/* the computer sieges when the target is beyond the tank's range but within the siege cannon's */
		int64_t t = u->order_unit[n], normal = KP_DAT(g, units, ground_weapon, TANK_TURRET);
		int64_t siege = KP_DAT(g, units, ground_weapon, SIEGE_TURRET);
		if (kp_can_use_tech(g, KP_TECH_TANK_SIEGE_MODE, u->owner[n], parent) == 1 && kp_can_see_target(g, n, t)
			&& kp_within_range_moving(g, parent, kp_weapon_max_range(g, n, siege), t)
			&& !kp_within_range_moving(g, parent, kp_weapon_max_range(g, n, normal), t) && !kp_is_critter(g, t)) {
			kp_issue_order(g, n, KP_ORD_NOTHING);
			kp_order_queue(g, n, KP_DAT(g, units, attack_unit, SIEGE_TURRET), 0, 0, 0, KP_TYPE_NONE);
			kp_issue_order(g, parent, KP_ORD_SIEGING);
			{
				int32_t s = kp_unit_sprite(g, t);
				kp_order_queue(g, parent, KP_DAT(g, units, attack_unit, KP_TYPE_SIEGE_TANK_SIEGED), g->w->spr.x[s], g->w->spr.y[s], t,
					KP_TYPE_NONE);
			}
			return;
		}
	}
	if (!kp_target_in_range(g, n, 0)
		&& (u->order[parent] != KP_DAT(g, units, attack_unit, u->type[parent]) || kp_right_click_action(g, parent) == 3)) {
		kp_issue_order(g, n, KP_ORD_TURRET_GUARD);
		return;
	}
	kp_flingy_set_target_waypoint(g, n, u->order_x[n], u->order_y[n]);
	if (kp_band(u->movement_flags[parent], MOVING) != 0) return;
	kp_start_attack_animation(g, n, KP_ANIM_GND_ATTK_RPT);
}

/* TryAttackInPlace 0x420bd0: 1 while the unit keeps its target and fires at it without moving (Hold Position, towers). */
int kp_attack_in_place(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t target, weapon, c, d, min;
	int selector;
	int32_t s;
	if (u->order_unit[n] == 0) return 0;
	kp_update_attack_target(g, n);
	if (!kp_can_see_target(g, n, u->order_unit[n]) || !kp_can_attack(g, n, u->order_unit[n], 1)) return 0;
	target = u->order_unit[n];
	s = kp_unit_sprite(g, target);
	u->order_x[n] = kp_s16v(g->w->spr.x[s]);
	u->order_y[n] = kp_s16v(g->w->spr.y[s]);
	if (is_subunit(g, u->subunit[n])) return 1;
	if (kp_band(u->status[target], STATUS_AIR) != 0) {
		weapon = KP_DAT(g, units, air_weapon, u->type[n]);
		selector = 2;
	} else {
		weapon = kp_ground_weapon(g, n);
		selector = 1;
	}
	c = cooldown(g, selector)[n];
	if (c != 0) {
		if (u->queue_timer[n] > c - 1) u->queue_timer[n] = c - 1;
		return 1;
	}
	if (kp_band(u->movement_flags[n], ATTACKING) != 0) return 1;
	d = kp_unit_distance(g, n, target);
	min = KP_DAT(g, weapons, minimum_range, weapon);
	if ((min != 0 && d < min) || d > kp_weapon_max_range(g, n, weapon)) return 0;
	s = kp_unit_sprite(g, n);
	d = kp_direction_to(g, kp_s16v(g->w->spr.x[s]), kp_s16v(g->w->spr.y[s]), u->order_x[n], u->order_y[n]);
	if (heading_off(u->heading[n], d) > KP_DAT(g, weapons, attack_angle, weapon)) {
		if (kp_band(u->status[n], STATUS_CAN_TURN) == 0) return 0;
		u->queue_timer[n] = 0;
		return 1;
	}
	if (kp_band(u->status[n], STATUS_HALTED) != 0) return 0;
	if (kp_band(u->status[n], STATUS_IN_BUILDING) != 0) kp_bunker_muzzle_flash(g, n);
	kp_clear_last_bullet_spawner(g);
	u->movement_flags[n] = kp_bor(u->movement_flags[n], ATTACKING);
	c = kp_mod(kp_weapon_cooldown(g, n, weapon) + kp_band(kp_rng_game_raw(g->rng, IN_PLACE_RANDOM), 3) - 1, 256);
	u->ground_cooldown[n] = c;
	u->air_cooldown[n] = c;
	kp_sprite_play(g->w, kp_unit_sprite(g, n), KP_ANIM_GND_ATTK_RPT - 1 + selector, 1);
	return 1;
}

/* SetTargetSwitchFlag 0x41cf90 on unit n and its subunit. */
void kp_set_target_switch(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	u->status[n] = kp_bor(u->status[n], STATUS_TARGET_SWITCH);
	if (u->subunit[n] != 0) u->status[u->subunit[n]] = kp_bor(u->status[u->subunit[n]], STATUS_TARGET_SWITCH);
}

/* HasAttackableEnemyNearby 0x4317a0: an enemy within 512 that sieged tank n can see, attack and reach (the square query
 * with the callback 0x432a60). */
int kp_enemy_in_reach(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int32_t s = kp_unit_sprite(g, n);
	int64_t x = kp_s16v(g->w->spr.x[s]), y = kp_s16v(g->w->spr.y[s]), *found, c;
	int count, k, any = 0;
	found = kp_query_rect(g, kp_s16v(x - SIEGE_PROBE), kp_s16v(y - SIEGE_PROBE), kp_s16v(x + SIEGE_PROBE), kp_s16v(y + SIEGE_PROBE),
		&count);
	for (k = 0; k < count && !any; k++) {
		c = found[k];
		if (c == n || !kp_is_enemy(g, n, c)) continue;
		if (kp_band(u->status[c], STATUS_CLOAK_BITS) != 0 && kp_band(u->visibility[c], (int64_t)1 << u->owner[n]) == 0) continue;
		any = kp_can_attack(g, n, c, 1) && kp_target_in_range(g, n, c);
	}
	kp_query_release(g, found);
	return any;
}

/* IssueAttackOrder 0x4202c0: unit n (and its turret) attacks target; ready sets the target switch status. */
void kp_issue_attack(KpGame *g, int64_t n, int64_t target, int ready, int ai_path)
{
	KpUnits *u = &g->u;
	int64_t sub;
	int32_t s;
	if (u->type[target] == KP_TYPE_INTERCEPTOR && u->union_0xc0[target] != 0) target = u->union_0xc0[target];
	kp_issue_order_target(g, n, KP_DAT(g, units, attack_unit, u->type[n]), target);
	if (ready) kp_set_target_switch(g, n);
	sub = u->subunit[n];
	if (sub != 0 && kp_band(KP_DAT(g, units, special_ability_flags, u->type[sub]), DAT_SUBUNIT) != 0) {
		kp_issue_order_target(g, sub, KP_DAT(g, units, attack_unit, u->type[sub]), target);
		if (ready) kp_set_target_switch(g, sub);
	}
	if (u->ai[n] == 0 || u->type[n] != KP_TYPE_SIEGE_TANK_SIEGED || u->order[n] == KP_ORD_UNSIEGING || kp_target_in_range(g, n, target)) return;
	if (kp_enemy_in_reach(g, n)) {
		if (ai_path) kp_ai_request_detection(g, n, target);
		else kp_ai_update_attack_target(g, n, 1, 1, 0);
		return;
	}
	/* out of reach of everything: the tank unsieges and its tank mode attacks */
	kp_issue_order(g, n, KP_ORD_UNSIEGING);
	s = kp_unit_sprite(g, target);
	kp_order_append(g, n, KP_DAT(g, units, attack_unit, KP_TYPE_SIEGE_TANK), kp_s16v(g->w->spr.x[s]), kp_s16v(g->w->spr.y[s]), target,
		KP_TYPE_NONE, 0);
}
