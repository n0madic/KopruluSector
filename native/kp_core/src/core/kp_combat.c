/* Damage, kill credit and death statistics (kp_game.h), ported from the atlas (P05, P07, P15) and
 * docs/research/native-damage.md. */
#include "kp_game_internal.h"

#define SPRITE_SELECTED 0x08
#define STATUS_INVINCIBLE 0x04000000
#define DAT_HERO 0x40
#define IGNORE_ARMOR 4
#define NUCLEAR 5
#define IRRADIATE 0x22
#define MIN_DAMAGE 0x80
#define PLASMA_SHIELDS 0xf
#define CHITINOUS_PLATING 0x34
#define ULTRALISK 0x27
#define TORRASQUE 0x30
#define NUKE_MIN 500
#define STATUS_AIR 0x4
#define STATUS_BURROWED 0x10
#define STATUS_IN_BUILDING 0x20
#define STATUS_CAN_MOVE 0x20000
#define DAT_WORKER 0x8
#define DAT_BURROWABLE 0x100000
#define CONTROLLER_COMPUTER 1
#define LARVA 0x23
#define LURKER 0x67
#define ARBITER 0x47
#define DANIMOTH 0x56
#define ORDER_MOVE 0x06
#define HELP_RANGE 0x60
#define STATUS_COMPLETED 0x1
#define STATUS_HALLUCINATION 0x40000000
#define DAT_BUILDING 0x1
#define DAT_SUBUNIT 0x10
#define GROUP_MEN 0x08
#define GROUP_BUILDING 0x10
#define GROUP_FACTORY 0x20
#define CHEAT_DAMAGE 0x4 /* 0x5432fc bit 4: only human players deal damage */
#define CONTROLLER_HUMAN 2
#define SHIELD_HIT_IMAGE 424
#define STIM_COST 0xa00
#define STIM_TIME 37
#define STIM_SOUND_FIRST 278
#define STIM_SOUND_RANDOM 31
#define SHIELD_DIRECTION_BASE 124 /* hit directions 124..131 use shield LO point 0 */
#define INTERCEPTOR 0x49
#define INFESTED_TERRAN_A 0x23
#define INFESTED_TERRAN_B 0x24
#define KILLS_MAX 255
#define CARGO_SLOTS 8
#define EVENT_TIMER 20
/* The minimap's attack colour 0x6d237c, a palette index the interface sets at its start (0xae in every
 * mission's dump). */
#define ATTACK_COLOR 0xae

/* Per-player statistics: pstat arrays (0x5290c4 + 0x30 * k). */
#define PSTAT_MEN_LOST 3       /* 0x529154 */
#define PSTAT_MEN_KILLED 4     /* 0x529184 */
#define PSTAT_KILL_SCORE 6     /* 0x5291e4 */
#define PSTAT_BUILDINGS_LOST 10 /* 0x5292a4 */
#define PSTAT_RAZED 11         /* 0x5292d4 */
#define PSTAT_RAZING_SCORE 13  /* 0x529334 */
#define PSTAT_FACTORIES_LOST 16 /* 0x5293c4 */
#define PSTAT_FACTORIES_RAZED 17 /* 0x5293f4 */

/* The packed cargo slot k of every unit (+0xb0). */
int64_t *kp_cargo_slots(KpGame *g, int k)
{
	KpUnits *u = &g->u;
	switch (k) {
	case 0: return u->loaded0;
	case 1: return u->loaded1;
	case 2: return u->loaded2;
	case 3: return u->loaded3;
	case 4: return u->loaded4;
	case 5: return u->loaded5;
	case 6: return u->loaded6;
	case 7: return u->loaded7;
	}
	kp_game_fail(g, "transport: cargo slot %d", k);
}

/* GetCargoSlotUnit 0x4381d0: the unit of cargo slot k, 0 for an empty, stale or dying entry. */
int64_t kp_cargo_unit(KpGame *g, int64_t n, int k)
{
	return kp_unit_from_ref(g, kp_cargo_slots(g, k)[n]);
}

/* HasCargo 0x438260. */
int kp_has_cargo(KpGame *g, int64_t n)
{
	int k;
	for (k = 0; k < CARGO_SLOTS; k++) {
		if (kp_cargo_unit(g, n, k) != 0) return 1;
	}
	return 0;
}

/* IsPlayerFinished 0x4bd590. */
int kp_player_finished(KpGame *g, int64_t p)
{
	int64_t c;
	if (p < 0 || p >= 8) return 0;
	c = kp_controller(g, p);
	if (c == 10 || c == 11) return 1;
	return (c == 1 || c == 2) && (g->results[p] == 1 || g->results[p] == 2);
}

/* RecordUnitDeathStatistics 0x414080. */
void kp_record_death(KpGame *g, int64_t n, int64_t player)
{
	KpUnits *u = &g->u;
	int64_t t = u->type[n], owner = u->owner[n], group;
	if (kp_band(u->status[n], STATUS_HALLUCINATION) != 0) return;
	if (kp_band(KP_DAT(g, units, special_ability_flags, t), DAT_SUBUNIT) != 0) return;
	group = KP_DAT(g, units, staredit_group_flags, t);
	if (kp_band(group, GROUP_MEN) != 0) g->pstat[PSTAT_MEN_LOST][owner]++;
	else if (kp_band(group, GROUP_BUILDING) != 0) g->pstat[PSTAT_BUILDINGS_LOST][owner]++;
	if (kp_band(group, GROUP_FACTORY) != 0) g->pstat[PSTAT_FACTORIES_LOST][owner]++;
	g->count_deaths[t * KP_PLAYERS + owner]++;
	if (player == owner || kp_u32v(player) >= 8 || kp_player_finished(g, player)) return;
	if (kp_band(group, GROUP_MEN) != 0 || t == INFESTED_TERRAN_A || t == INFESTED_TERRAN_B) {
		g->pstat[PSTAT_MEN_KILLED][player]++;
		g->pstat[PSTAT_KILL_SCORE][player] += KP_DAT(g, units, destroy_score, t);
	} else if (kp_band(group, GROUP_BUILDING) != 0) {
		g->pstat[PSTAT_RAZED][player]++;
		g->pstat[PSTAT_RAZING_SCORE][player] += KP_DAT(g, units, destroy_score, t);
	}
	if (kp_band(group, GROUP_FACTORY) != 0) g->pstat[PSTAT_FACTORIES_RAZED][player]++;
	g->count_kills[t * KP_PLAYERS + player]++;
}

/* IncrementKillCount 0x41fc40. */
void kp_increment_kills(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	for (;;) {
		if (u->kills[n] < KILLS_MAX) u->kills[n]++;
		if (u->type[n] != INTERCEPTOR || u->union_0xc0[n] == 0) return;
		n = kp_game_unit(g, u->union_0xc0[n]);
	}
}

/* NotifyUnitAttacked 0x446f00: the local player's unit under attack. */
void kp_notify_attacked(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	if (g->w->replay || u->owner[n] != g->local_player) return;
	u->last_event_timer[n] = EVENT_TIMER;
	u->last_event_color[n] = ATTACK_COLOR;
	/* TODO: the under-attack voice and minimap ping (cooldown 0x66727c, off-screen test 0x422c20, 0x4460d0,
	 * 0x4a7200) belong to the interface of phase 9; none of them changes the simulation. */
}

/* DamageHitPoints 0x41e150: raw damage with its attacker (or 0), the attacking player and whether the victim's
 * owner is notified. */
void kp_damage_hit_points(KpGame *g, int64_t n, int64_t damage, int64_t attacker, int64_t player, int notify)
{
	KpUnits *u = &g->u;
	if (u->hp[n] == 0) return;
	/* A selected sprite's health bars are redrawn (0x416bb0): presentation only. */
	if (attacker != 0) kp_on_unit_damaged(g, n, attacker, notify);
	if (kp_band(kp_scalar(g, KP_CHEATS), CHEAT_DAMAGE) != 0 && (player < 0 || player >= KP_PLAYERS
		|| kp_controller(g, player) != CONTROLLER_HUMAN)) {
		damage = 0;
	}
	if (damage < u->hp[n]) {
		u->hp[n] -= damage;
		kp_refresh_unit_strength(g, n);
		if (kp_band(u->status[n], STATUS_COMPLETED) != 0) {
			KpImages *w = g->w;
			int32_t s = kp_unit_sprite(g, n);
			if (KP_DAT(g, images, damage_overlay, w->img.id[kp_check_image(w, w->spr.main[s])]) != 0) {
				kp_update_damage_overlays(g, n);
			}
		}
		if (attacker != 0 && u->owner[n] != u->owner[attacker] && notify) kp_notify_attacked(g, n);
		return;
	}
	if (kp_is_transport(g, n) && kp_band(KP_DAT(g, units, special_ability_flags, u->type[n]), DAT_BUILDING) == 0) {
		int k;
		for (k = 0; k < CARGO_SLOTS; k++) {
			int64_t c = kp_cargo_unit(g, n, k);
			if (c == 0 || u->sprite[c] == -1 || u->order[c] == 0) continue;
			kp_unit_kill(g, c);
			kp_record_death(g, c, player);
		}
	}
	u->hp[n] = 0;
	kp_unit_kill(g, n);
	kp_record_death(g, n, player);
	if (attacker != 0 && kp_is_enemy(g, attacker, n)) kp_increment_kills(g, attacker);
}

/* The damage factors of weapon type and unit size (0x503240, in 1/256). */
static const int64_t DAMAGE_FACTOR[5][4] = {
	{ 0, 0, 0, 0 }, { 0, 128, 192, 256 }, { 0, 256, 128, 64 }, { 0, 256, 256, 256 }, { 0, 256, 256, 256 },
};

/* ScaleDamageByTypeAndSize 0x43eb20. */
static int64_t scale_damage(int64_t damage, int64_t type, int64_t size)
{
	if (type < 0 || type > 4 || size < 0 || size > 3) return 0;
	return kp_shrv(kp_mulv(DAMAGE_FACTOR[type][size], damage), 8);
}

/* GetArmorUpgradeBonus 0x43ab10: the armor upgrade, with the expansion's Chitinous Plating of the Ultralisk and the
 * Torrasque (its caller takes the low byte). */
static int64_t armor_bonus(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t t = u->type[n], extra = 0;
	if (g->expansion != 0 && (t == ULTRALISK || t == TORRASQUE)
		&& (kp_band(KP_DAT(g, units, special_ability_flags, t), DAT_HERO) != 0
			|| kp_upgrade_level(g, u->owner[n], CHITINOUS_PLATING) != 0)) {
		extra = 2;
	}
	return kp_upgrade_level(g, u->owner[n], KP_DAT(g, units, armor_upgrade, t)) + extra;
}

/* AddShieldHitOverlay 0x422fe0: image 424 at the shield LO point of the hit direction. */
static void shield_hit(KpGame *g, int64_t n, int64_t direction)
{
	KpImages *w = g->w;
	int32_t s = kp_unit_sprite(g, n), frame, x, y;
	frame = (int32_t)kp_band(kp_shrv(kp_u32v(kp_band(direction, 0xff) - SHIELD_DIRECTION_BASE), 3), 31);
	/* CreateShieldHitOverlay 0x415370 */
	kp_images_shield_lo_offset(w, kp_check_image(w, w->spr.main[s]), frame, &x, &y);
	kp_sprite_image_above_main(w, s, SHIELD_HIT_IMAGE, x, y, frame);
}

/* WeaponDamage 0x41dfc0: raw damage of weapon at unit n, from direction, divided by divisor. */
void kp_weapon_damage(KpGame *g, int64_t n, int64_t damage, int64_t weapon, int64_t divisor, int64_t direction,
	int64_t attacker, int64_t player)
{
	KpUnits *u = &g->u;
	int64_t status = u->status[n], d, shield_damage = 0, type;
	if (u->hp[n] == 0 || kp_band(status, STATUS_INVINCIBLE) != 0) return;
	weapon = kp_mod(weapon, 256);
	damage = kp_u32v(damage);
	if (kp_band(kp_scalar(g, KP_CHEATS), CHEAT_DAMAGE) != 0 && (player < 0 || player >= KP_PLAYERS
		|| kp_controller(g, player) != CONTROLLER_HUMAN)) {
		damage = 0;
	}
	if (kp_band(status, STATUS_HALLUCINATION) != 0) damage = kp_u32v(damage * 2);
	d = kp_s32v(kp_udiv64(g, damage, kp_u32v(divisor)) + u->acid_spores[n] * 256);
	if (d < MIN_DAMAGE) d = MIN_DAMAGE;
	if (u->matrix_damage[n] != 0) {
		int64_t a = d > u->matrix_damage[n] ? u->matrix_damage[n] : d;
		d -= a;
		kp_reduce_matrix(g, n, a);
	}
	type = KP_DAT(g, weapons, weapon_type, weapon);
	if (KP_DAT(g, units, shield_enabled, u->type[n]) != 0 && u->shields[n] >= 0x100) {
		if (type != IGNORE_ARMOR) {
			int64_t plasma = kp_upgrade_level(g, u->owner[n], PLASMA_SHIELDS) * 256;
			if (plasma >= d) d = MIN_DAMAGE;
			else d -= plasma;
		}
		shield_damage = d > u->shields[n] ? u->shields[n] : d;
		d -= shield_damage;
	}
	if (type != IGNORE_ARMOR) {
		int64_t armor = (kp_band(armor_bonus(g, n), 0xff) + KP_DAT(g, units, armor, u->type[n])) * 256;
		d -= d > armor ? armor : d;
	}
	d = scale_damage(d, type, KP_DAT(g, units, unit_size, u->type[n]));
	if (shield_damage == 0 && kp_s32v(d) < MIN_DAMAGE) d = MIN_DAMAGE;
	kp_damage_hit_points(g, n, d, attacker, player, weapon != IRRADIATE);
	if (shield_damage != 0) {
		u->shields[n] -= shield_damage;
		if (type != 0 && u->shields[n] != 0) shield_hit(g, n, direction);
	}
	kp_refresh_unit_strength(g, n);
}

/* HallucinatedWeaponHit 0x41e2f0: the reactions and the shield image, no damage. */
static void hallucinated_hit(KpGame *g, int64_t n, int64_t weapon, int64_t direction, int64_t attacker)
{
	KpUnits *u = &g->u;
	if (u->hp[n] == 0 || kp_band(u->status[n], STATUS_INVINCIBLE) != 0) return;
	weapon = kp_mod(weapon, 256);
	if (attacker != 0) {
		kp_on_unit_damaged(g, n, attacker, weapon != IRRADIATE);
		if (weapon != IRRADIATE && u->owner[n] != u->owner[attacker]) kp_notify_attacked(g, n);
	}
	if (KP_DAT(g, units, shield_enabled, u->type[n]) != 0 && u->shields[n] >= 0x100
		&& KP_DAT(g, weapons, weapon_type, weapon) != 0) {
		shield_hit(g, n, direction);
	}
}

/* GetBulletDamageAmount 0x409010: the weapon's damage with its owner's upgrade, or the Nuke's share of the target's
 * life. */
static int64_t bullet_damage_amount(KpGame *g, int64_t b, int64_t target)
{
	KpUnits *u = &g->u;
	int64_t f = kp_bullet_flingy(g, b), weapon = g->bullets.weapon[b], t, life, v;
	if (KP_DAT(g, weapons, explosion_type, weapon) != NUCLEAR) {
		int64_t level = kp_upgrade_level(g, u->owner[f], KP_DAT(g, weapons, damage_upgrade, weapon));
		return kp_shlv(kp_mulv(kp_band(level, 0xff), KP_DAT(g, weapons, damage_bonus, weapon))
			+ KP_DAT(g, weapons, damage_amount, weapon), 8);
	}
	t = u->type[target];
	life = kp_sarv(KP_DAT(g, units, hit_points, t), 8);
	if (life == 0) {
		life = kp_sarv(u->hp[target] + 0xff, 8);
		if (life == 0) life = 1;
	}
	if (KP_DAT(g, units, shield_enabled, t) != 0) life += KP_DAT(g, units, shield_amount, t);
	v = kp_s32v(life * 2);
	v = v / 3; /* the signed truncating 0x55555556 multiply */
	if (v < NUKE_MIN) v = NUKE_MIN;
	return kp_shlv(v, 8);
}

/* BulletDamage 0x41e290. */
void kp_bullet_damage(KpGame *g, int64_t n, int64_t b, int64_t divisor)
{
	KpBullets *bl = &g->bullets;
	int64_t f = kp_bullet_flingy(g, b);
	if (kp_band(bl->hit_flags[b], 2) != 0) {
		hallucinated_hit(g, n, bl->weapon[b], g->u.heading[f], bl->source[b]);
		return;
	}
	kp_weapon_damage(g, n, bullet_damage_amount(g, b, n), bl->weapon[b], divisor, g->u.heading[f], bl->source[b],
		g->u.owner[f]);
}

/* MeleeDamage 0x41fc70 (attackmelee): the ground weapon at the order target, its upgrade bonus a 16-bit product. */
void kp_melee_damage(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t target = u->order_unit[n], weapon, level, d;
	if (target == 0) return;
	weapon = kp_ground_weapon(g, n);
	if (kp_band(u->status[n], STATUS_HALLUCINATION) != 0) {
		hallucinated_hit(g, target, weapon, u->heading[n], n);
		return;
	}
	level = kp_band(kp_upgrade_level(g, u->owner[n], KP_DAT(g, weapons, damage_upgrade, weapon)), 0xff);
	d = kp_shlv(kp_band(level * KP_DAT(g, weapons, damage_bonus, weapon), 0xffff) + KP_DAT(g, weapons, damage_amount, weapon), 8);
	kp_weapon_damage(g, target, d, weapon, 1, u->heading[n], n, u->owner[n]);
}

/* IsSubunit 0x420000. */
int kp_is_subunit(KpGame *g, int64_t n)
{
	return n != 0 && kp_band(KP_DAT(g, units, special_ability_flags, g->u.type[n]), DAT_SUBUNIT) != 0;
}

/* The attacker as auto-target of unit n, copied to its subunit with the last attacker. */
static void adopt_attacker(KpGame *g, int64_t n, int64_t attacker)
{
	KpUnits *u = &g->u;
	int64_t sub = u->subunit[n];
	u->auto_target[n] = attacker;
	if (kp_is_subunit(g, sub)) {
		u->last_attacker[sub] = u->last_attacker[n];
		u->auto_target[sub] = attacker;
	}
}

/* ShouldSwitchTarget 0x4212b0: candidate over the current auto-target. */
static int should_switch_target(KpGame *g, int64_t n, int64_t current, int64_t candidate)
{
	if (candidate == current || !kp_is_enemy(g, n, candidate)) return 0;
	if (current == 0 || !kp_can_attack(g, n, current, 1) || !kp_is_enemy(g, n, current)) return 1;
	return kp_target_in_range(g, n, candidate) && !kp_target_in_range(g, n, current);
}

/* GetFleePosition 0x421180: the first point 192, 128, 64, 32 or 16 pixels away from the attacker that the unit can
 * reach (a pathing unit), else where it stands; a computer's guard takes it as its home. */
void kp_flee_position(KpGame *g, int64_t attacker, int64_t n, int64_t *x, int64_t *y)
{
	static const int64_t DISTANCES[] = { 192, 128, 64, 32, 16 };
	KpUnits *u = &g->u;
	int32_t as = kp_unit_sprite(g, attacker), s = kp_unit_sprite(g, n);
	int64_t sx = kp_s16v(g->w->spr.x[s]), sy = kp_s16v(g->w->spr.y[s]);
	int64_t d = kp_direction_to(g, kp_s16v(g->w->spr.x[as]), kp_s16v(g->w->spr.y[as]), sx, sy);
	int64_t w = kp_mod(g->map_w_px, 65536), h = kp_mod(g->map_h_px, 65536), px, py;
	int k;
	for (k = 0; k < (int)(sizeof DISTANCES / sizeof DISTANCES[0]); k++) {
		px = sx + kp_sarv(kp_mulv(KP_DIR_X[d], DISTANCES[k]), 8);
		py = sy + kp_sarv(kp_mulv(KP_DIR_Y[d], DISTANCES[k]), 8);
		if (px < 0) px = 0;
		else if (px >= w) px = w - 1;
		if (py < 0) py = 0;
		else if (py >= h) py = h - 1;
		if (kp_band(u->pathing_flags[n], 1) == 0
			|| (kp_placement_reachable(g, n, px, py) && kp_ai_can_reach_point(g, n, px, py))) {
			*x = kp_s16v(px);
			*y = kp_s16v(py);
			if (u->ai[n] != 0) kp_ai_set_guard_home(g, n, *x, *y);
			return;
		}
	}
	*x = sx;
	*y = sy;
}

/* OnHitChangeTarget 0x420190: the victim turns on its attacker, unburrows or flees. */
static void on_hit_change_target(KpGame *g, int64_t n, int64_t attacker)
{
	KpUnits *u = &g->u;
	int64_t t = u->type[n], f;
	if (t == LARVA) return;
	if (kp_band(u->status[n], STATUS_BURROWED) != 0 && u->irradiate_timer[n] == 0 && t != LURKER) {
		if (kp_band(KP_DAT(g, units, special_ability_flags, t), DAT_BURROWABLE) != 0) kp_request_unburrow(g, n);
		return;
	}
	f = KP_DAT(g, units, special_ability_flags, t);
	if (kp_band(f, DAT_BUILDING) != 0 || (kp_can_attack(g, n, attacker, 1) && kp_band(f, DAT_WORKER) == 0)) {
		adopt_attacker(g, n, attacker);
		return;
	}
	if (t == LURKER && kp_band(u->status[n], STATUS_BURROWED) != 0 && (u->ai[n] == 0 || u->irradiate_timer[n] != 0)) return;
	if (kp_is_subunit(g, n)) n = u->subunit[n];
	if (KP_DAT(g, orders, flee_unreturnable_damage, u->order[n]) == 0 || kp_band(u->status[n], STATUS_CAN_MOVE) == 0) return;
	/* TODO: a computer's guard flees and takes a new home (0x4b2720, plan phase 7) */
	if (u->ai[n] != 0) kp_game_fail(g, "combat: the guard at home (0x4b2720)");
	if (kp_cover_miss_chance(g, n) == 0xff) return;
	{
		int64_t x, y;
		kp_flee_position(g, attacker, n, &x, &y);
		kp_issue_order_packed(g, n, ORDER_MOVE, x, y, 0);
	}
}

/* AllyOnHitRetarget 0x420120: a nearby armed ally takes the attacker as auto-target. */
static void ally_on_hit_retarget(KpGame *g, int64_t n, int64_t attacker)
{
	KpUnits *u = &g->u;
	int64_t t = u->type[n];
	if (t == ARBITER || t == DANIMOTH || kp_band(KP_DAT(g, units, special_ability_flags, t), DAT_WORKER) != 0) return;
	if (kp_band(u->status[n], STATUS_BURROWED) != 0 || !kp_unit_has_weapon(g, n)) return;
	if (should_switch_target(g, n, u->auto_target[n], attacker)) adopt_attacker(g, n, attacker);
}

/* OnUnitDamaged 0x4203d0. */
void kp_on_unit_damaged(KpGame *g, int64_t n, int64_t attacker, int notify)
{
	KpUnits *u = &g->u;
	int64_t *found;
	int count, k;
	int32_t s;
	if (attacker == 0 || u->owner[attacker] == u->owner[n]) return;
	u->last_attacker[n] = u->owner[attacker];
	if (u->ai[n] != 0 && u->auto_target[n] == 0) {
		int64_t sub = u->subunit[n];
		u->auto_target[n] = attacker;
		if (kp_is_subunit(g, sub)) {
			u->last_attacker[sub] = u->last_attacker[n];
			u->auto_target[u->subunit[n]] = attacker;
		}
	}
	kp_ai_react_to_attack(g, n, attacker, 1, 0);
	if (notify && !kp_is_undetected_by(g, attacker, n) && u->owner[n] < 8) {
		int32_t as = kp_unit_sprite(g, attacker);
		kp_reveal_sight(g, kp_s16v(g->w->spr.x[as]), kp_s16v(g->w->spr.y[as]), kp_band(u->status[attacker], STATUS_AIR) != 0,
			(int64_t)1 << u->owner[n], 1);
	}
	if (kp_band(u->status[attacker], STATUS_IN_BUILDING) != 0) attacker = u->connected[attacker];
	if (should_switch_target(g, n, u->auto_target[n], attacker)) on_hit_change_target(g, n, attacker);
	if (u->ai[n] != 0) return;
	s = kp_unit_sprite(g, n);
	{
		int64_t x = g->w->spr.x[s], y = g->w->spr.y[s];
		found = kp_query_rect(g, kp_s16v(x - HELP_RANGE), kp_s16v(y - HELP_RANGE), kp_s16v(x + HELP_RANGE),
			kp_s16v(y + HELP_RANGE), &count);
	}
	for (k = 0; k < count; k++) {
		int64_t c = found[k];
		if (c != n && u->owner[c] == u->owner[n]) ally_on_hit_retarget(g, c, attacker);
	}
	kp_query_release(g, found);
}

/* ApplyStimPack 0x438af0: 10 hit points for 37 frames of stim. */
void kp_apply_stim(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	if (kp_s32v(u->hp[n]) <= STIM_COST) return;
	kp_play_unit_sound(g, kp_rng_game_range(g->rng, STIM_SOUND_RANDOM, STIM_SOUND_FIRST, STIM_SOUND_FIRST + 1), n);
	kp_damage_hit_points(g, n, STIM_COST, 0, -1, 1);
	if (u->stim_timer[n] < STIM_TIME) {
		u->stim_timer[n] = STIM_TIME;
		kp_recalculate_speed(g, n);
	}
}
