/* The Bullet pool and the projectiles (kp_game.h), ported from the atlas (P02, P07) and
 * docs/research/native-projectiles.md. A bullet's flingy part lives in the unit field arrays at record KP_UNITS + b:
 * the flingy fields, its owner (+0x4c), state (+0x4d, `order`), signal (+0x4f), order point and target (+0x58..+0x5c);
 * the rest in KpBullets. */
#include "kp_game_internal.h"

#include <string.h>

#define NO_WEAPON 0x82
#define NO_TYPE 0xe4
#define HALO_ROCKETS 0x67
#define HALO_ROCKETS_MAX 80
#define SCARAB 0x55
#define STATUS_AIR 0x4
#define STATUS_HALTED 0x8000
#define STATUS_HALLUCINATION 0x40000000
#define DAT_BUILDING 0x1
#define DAT_SUBUNIT 0x10
#define FLAG_ATTACKING 0x8
#define HIT_MISS 0x1
#define HIT_HALLUCINATION 0x2
#define SIGNAL_LAUNCH 0x1
#define SPIN_RANDOM 0
#define MISS_RANDOM 1
#define MISS_OFFSET 30
#define COVER_MISS 0x77
#define TILE_COVER 0x100000
#define DARK_SWARM 0xca
#define NEUTRAL 11
#define ANIM_DEATH 1
#define ANIM_FLY 2
#define ANIM_SPECIAL_STATE 14
#define LURKER 0x67
#define CHARON_BOOSTERS 0x36
#define DAT_HERO 0x40

/* Bullet states (+0x4d) */
enum { STATE_INIT, STATE_MOVE, STATE_FOLLOW, STATE_BOUNCE, STATE_PERSIST, STATE_DYING, STATE_NEAR };

/* Weapon behaviours (weapons.dat) */
enum {
	FLY_TO_POINT, FLY_TO_TARGET, APPEAR_ON_TARGET_UNIT, PERSIST_ON_SITE, APPEAR_ON_TARGET_SITE, APPEAR_ON_ATTACKER,
	SELF_DESTRUCT, BOUNCE, ATTACK_NEARBY, MAX_RANGE
};

/* Weapon effects (weapons.dat explosion type) */
enum {
	EFFECT_NONE, EFFECT_NORMAL, EFFECT_SPLASH_RADIAL, EFFECT_SPLASH_ENEMY, EFFECT_LOCKDOWN, EFFECT_NUCLEAR, EFFECT_PARASITE,
	EFFECT_BROODLINGS, EFFECT_EMP, EFFECT_IRRADIATE, EFFECT_ENSNARE, EFFECT_PLAGUE, EFFECT_STASIS, EFFECT_DARK_SWARM,
	EFFECT_CONSUME, EFFECT_YAMATO, EFFECT_RESTORATION, EFFECT_DISRUPTION_WEB, EFFECT_CORROSIVE_ACID, EFFECT_OPTICAL_FLARE = 21,
	EFFECT_MAELSTROM, EFFECT_AIR_SPLASH = 24
};

/* weapons.dat target flags and the units.dat properties they test (0x41dbf0) */
#define TARGET_AIR 0x1
#define TARGET_GROUND 0x2
#define TARGET_MECHANICAL 0x4
#define TARGET_ORGANIC 0x8
#define TARGET_NOT_BUILDING 0x10
#define TARGET_NOT_ROBOTIC 0x20
#define TARGET_TERRAIN 0x40
#define TARGET_NOT_ROBOTIC_SPELL 0x80
#define TARGET_OWN 0x100
#define DAT_ROBOTIC 0x4000
#define DAT_ORGANIC 0x10000
#define DAT_MECHANICAL 0x40000000
#define STATUS_INVINCIBLE 0x04000000
#define STATUS_BURROWED 0x10
#define PSI_STORM 0x54
#define SUBTERRANEAN_SPINES 0x6d
#define INTERCEPTOR 0x49
#define AIR_SPLASH_RANDOM 58

int64_t kp_bullet_flingy(KpGame *g, int64_t b)
{
	if (b < 1 || b > KP_BULLETS) kp_game_fail(g, "bullet: record %lld out of range", (long long)b);
	return KP_UNITS + b;
}

/* InitBullets 0x40a0e0: every record cleared, the free list 1, 100, 99, ..., 2. */
void kp_bullets_init(KpGame *g)
{
	KpUnits *u = &g->u;
	KpBullets *bl = &g->bullets;
	int64_t b;
	memset(bl, 0, sizeof *bl);
	for (b = 1; b <= KP_BULLETS; b++) {
		int64_t f = KP_UNITS + b;
#define CLEAR(name) u->name[f] = 0;
		KP_UNIT_FIELDS(CLEAR)
#undef CLEAR
		u->sprite[f] = KP_NONE;
		u->queue_head[f] = u->queue_tail[f] = KP_NONE;
		kp_list64_insert_after_head(bl->prev, bl->next, &bl->free, b, 0);
	}
}

/* ClearLastBulletSpawner 0x40a9e0. */
void kp_clear_last_bullet_spawner(KpGame *g) { g->bullets.last_spawner = 0; }

static int64_t weapon_of(KpGame *g, int64_t b) { return g->bullets.weapon[b]; }

static int64_t behavior(KpGame *g, int64_t weapon) { return KP_DAT(g, weapons, weapon_behavior, weapon); }

/* SetOrderTargetUnit 0x417a50 (with the target's position) and SetOrderTargetPoint 0x417a10, for a bullet's state. */
static void set_state_target(KpGame *g, int64_t f, int64_t state, int64_t target)
{
	KpUnits *u = &g->u;
	u->order[f] = state;
	u->order_unit[f] = target;
	if (target != 0) {
		int32_t s = kp_unit_sprite(g, target);
		u->order_x[f] = kp_s16v(g->w->spr.x[s]);
		u->order_y[f] = kp_s16v(g->w->spr.y[s]);
	} else {
		u->order_x[f] = u->order_y[f] = 0;
	}
	u->order_state[f] = 0;
	u->order_type[f] = NO_TYPE;
}

static void set_state_point(KpGame *g, int64_t f, int64_t state, int64_t x, int64_t y)
{
	KpUnits *u = &g->u;
	u->order[f] = state;
	u->order_x[f] = kp_s16v(x);
	u->order_y[f] = kp_s16v(y);
	u->order_unit[f] = 0;
	u->order_state[f] = 0;
	u->order_type[f] = NO_TYPE;
}

/* 0x40a3d0: the state toward the bullet's target unit or point. */
static void set_state(KpGame *g, int64_t f, int64_t state)
{
	KpUnits *u = &g->u;
	if (u->order_unit[f] != 0) set_state_target(g, f, state, u->order_unit[f]);
	else set_state_point(g, f, state, u->order_x[f], u->order_y[f]);
}

static void play(KpGame *g, int64_t f, int64_t anim) { kp_sprite_play(g->w, kp_unit_sprite(g, f), (int32_t)anim, 1); }

/* The Death state with its animation (0x409990, 0x40a8c0). */
static void start_dying(KpGame *g, int64_t f)
{
	set_state(g, f, STATE_DYING);
	play(g, f, ANIM_DEATH);
}

static void unlink(KpGame *g, int64_t b)
{
	KpBullets *bl = &g->bullets;
	kp_list64_unlink(bl->prev, bl->next, &bl->active, b, 0);
	kp_list64_insert_after_head(bl->prev, bl->next, &bl->free, b, 0);
	bl->allocated--;
}

/* 0x409950: the dying bullet returns to the pool once its sprite is gone. */
static void state_dying(KpGame *g, int64_t b)
{
	if (g->u.sprite[kp_bullet_flingy(g, b)] == KP_NONE) unlink(g, b);
}

/* 0x409990: toward the saved point; the Death state on arrival, on leaving the map or when its time runs out. */
static void state_move(KpGame *g, int64_t b)
{
	KpUnits *u = &g->u;
	int64_t f = kp_bullet_flingy(g, b), x, y, time;
	kp_prepare_flingy_movement(g, f);
	kp_commit_flingy(g, f);
	x = kp_s16v(u->x[f]);
	y = kp_s16v(u->y[f]);
	if (x < 0 || x >= kp_mod(g->map_w_px, 65536) || y < 0 || y >= kp_mod(g->map_h_px, 65536)) g->bullets.time[b] = 0;
	kp_flingy_clamp_to_map(g, f);
	time = g->bullets.time[b];
	g->bullets.time[b] = kp_mod(time - 1, 256);
	if (time != 0 && (u->move_x[f] != u->x[f] || u->move_y[f] != u->y[f])) return;
	start_dying(g, f);
}

/* 0x409aa0: after a live target, or as a miss toward its point. */
static void state_follow(KpGame *g, int64_t b)
{
	KpUnits *u = &g->u;
	int64_t f = kp_bullet_flingy(g, b), target = u->order_unit[f];
	if (target != 0) {
		if (kp_band(g->bullets.hit_flags[b], HIT_MISS) == 0) {
			int32_t s = kp_unit_sprite(g, target);
			/* SetMoveTargetAndFacing 0x40cc40 */
			kp_flingy_set_move_target(g, f, kp_s16v(g->w->spr.x[s]), kp_s16v(g->w->spr.y[s]));
			kp_flingy_set_target_waypoint(g, f, kp_s16v(g->w->spr.x[s]), kp_s16v(g->w->spr.y[s]));
		} else {
			set_state_target(g, f, STATE_MOVE, target);
		}
	} else {
		set_state_point(g, f, STATE_MOVE, u->order_x[f], u->order_y[f]);
	}
	state_move(g, b);
}

static void dispatch(KpGame *g, int64_t b);

/* 0x40a8c0: waits for the image's launch signal, then takes the weapon behaviour's state. */
static void state_init(KpGame *g, int64_t b)
{
	KpUnits *u = &g->u;
	int64_t f = kp_bullet_flingy(g, b), be;
	if (kp_band(u->order_signal[f], SIGNAL_LAUNCH) == 0) return;
	u->order_signal[f] = kp_band(u->order_signal[f], 0xfe);
	be = behavior(g, weapon_of(g, b));
	switch (be) {
	case APPEAR_ON_TARGET_UNIT: case APPEAR_ON_TARGET_SITE: case APPEAR_ON_ATTACKER: case SELF_DESTRUCT:
		start_dying(g, f);
		break;
	case FLY_TO_TARGET:
		set_state(g, f, STATE_FOLLOW);
		play(g, f, ANIM_FLY);
		break;
	case BOUNCE:
		set_state(g, f, STATE_BOUNCE);
		play(g, f, ANIM_FLY);
		break;
	case PERSIST_ON_SITE:
		set_state(g, f, STATE_PERSIST);
		play(g, f, ANIM_SPECIAL_STATE);
		break;
	case ATTACK_NEARBY:
		set_state(g, f, STATE_NEAR);
		play(g, f, ANIM_FLY);
		break;
	default:
		set_state(g, f, STATE_MOVE);
		play(g, f, ANIM_FLY);
		break;
	}
	dispatch(g, b);
}

/* DispatchBulletState 0x40a6d0 and the switch of 0x40a5f0 (whose inline state 5 is 0x409950). */
static void dispatch(KpGame *g, int64_t b)
{
	int64_t f = kp_bullet_flingy(g, b);
	switch (g->u.order[f]) {
	case STATE_INIT: state_init(g, b); break;
	case STATE_MOVE: state_move(g, b); break;
	case STATE_FOLLOW: state_follow(g, b); break;
	case STATE_BOUNCE: kp_game_fail(g, "bullet: bouncing (0x409830)");
	case STATE_PERSIST: kp_game_fail(g, "bullet: damage over time (0x409a30)");
	case STATE_DYING: state_dying(g, b); break;
	case STATE_NEAR: kp_game_fail(g, "bullet: moving near its target (0x409b00)");
	default: break;
	}
}

/* UpdateThingyVisibility 0x417e50: the sprite takes the visibility of its center tile. */
static void update_visibility(KpGame *g, int64_t f)
{
	int32_t s = (int32_t)g->u.sprite[f];
	int64_t x, y, mask;
	if (s == KP_NONE || kp_band(g->w->spr.flags[s], 0x20) != 0) return;
	x = kp_idiv64(g, kp_s16v(g->w->spr.x[s]), 32);
	y = kp_idiv64(g, kp_s16v(g->w->spr.y[s]), 32);
	mask = kp_band(kp_bnot(g->terrain.tiles[y * g->map_w + x]), 0xff);
	if (g->w->spr.visibility[s] != mask) kp_sprite_set_visibility(g->w, s, (int32_t)mask);
}

/* UpdateBullets 0x40a5f0. */
void kp_bullets_step(KpGame *g)
{
	KpBullets *bl = &g->bullets;
	KpImages *w = g->w;
	int64_t b = bl->active.head;
	while (b != 0) {
		int64_t next = bl->next[b], f = kp_bullet_flingy(g, b);
		w->flingy = (int32_t)f;
		w->has_flingy = 1;
		w->bullet = (int32_t)b;
		w->has_bullet = 1;
		update_visibility(g, f);
		kp_step_unit_sprite(g, f);
		dispatch(g, b);
		b = next;
	}
	w->has_bullet = 0;
	w->has_flingy = 0;
}

/* ClearBulletReferencesToUnit 0x40a5a0. */
void kp_clear_bullet_references(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	KpBullets *bl = &g->bullets;
	int64_t b;
	for (b = bl->active.head; b != 0; b = bl->next[b]) {
		int64_t f = KP_UNITS + b;
		if (u->order_unit[f] == n) {
			int32_t s = kp_unit_sprite(g, n);
			u->order_x[f] = kp_s16v(g->w->spr.x[s]);
			u->order_y[f] = kp_s16v(g->w->spr.y[s]);
			u->order_unit[f] = 0;
		}
		if (bl->source[b] == n) bl->source[b] = 0;
	}
}

/* GetMissChance 0x41fa00 of the target. */
int64_t kp_cover_miss_chance(KpGame *g, int64_t target)
{
	KpUnits *u = &g->u;
	int32_t s;
	int64_t tx, ty;
	if (kp_band(u->status[target], STATUS_AIR) != 0) return 0;
	if (kp_band(KP_DAT(g, units, special_ability_flags, u->type[target]), DAT_BUILDING) == 0
		&& g->count_completed[DARK_SWARM * KP_PLAYERS + NEUTRAL] != 0) {
		kp_game_fail(g, "bullet: Dark Swarm cover (0x409cc0, 0x432070)");
	}
	s = kp_unit_sprite(g, target);
	tx = kp_idiv64(g, kp_s16v(g->w->spr.x[s]), 32);
	ty = kp_idiv64(g, kp_s16v(g->w->spr.y[s]), 32);
	return kp_band(g->terrain.tiles[ty * g->map_w + tx], TILE_COVER) != 0 ? COVER_MISS : 0;
}

/* GetMissThreshold 0x41fd10: the cover, at least 0x77 against a higher ground target. */
static int64_t miss_threshold(KpGame *g, int64_t n, int64_t target)
{
	KpUnits *u = &g->u;
	int64_t chance = kp_cover_miss_chance(g, target), hs, ht;
	int32_t s, t;
	if (kp_band(u->status[n], STATUS_AIR) != 0 || kp_band(u->status[target], STATUS_AIR) != 0) return chance;
	s = kp_unit_sprite(g, n);
	t = kp_unit_sprite(g, target);
	hs = kp_terrain_height(g, kp_s16v(g->w->spr.x[s]), kp_s16v(g->w->spr.y[s]));
	ht = kp_terrain_height(g, kp_s16v(g->w->spr.x[t]), kp_s16v(g->w->spr.y[t]));
	if (hs >= ht) return chance;
	return chance > COVER_MISS ? chance : COVER_MISS;
}

/* The miss test of 0x409d70 (a target and a source): whether the low byte of a draw falls within the threshold. */
static int misses(KpGame *g, int64_t b, int64_t target)
{
	int64_t threshold = miss_threshold(g, g->bullets.source[b], target);
	return kp_band(kp_rng_game_raw(g->rng, MISS_RANDOM), 0xff) <= threshold;
}

/* InitializeBullet 0x409d70. */
static int init_bullet(KpGame *g, int64_t b, int64_t weapon, int64_t x, int64_t y, int64_t owner, int64_t heading,
	int64_t spawner)
{
	KpUnits *u = &g->u;
	KpBullets *bl = &g->bullets;
	KpImages *w = g->w;
	int64_t f = kp_bullet_flingy(g, b), spin, target, ox, oy;
	int32_t s, ts;
	if (!kp_thingy_init(g, f, KP_DAT(g, weapons, graphics, weapon), x, y, owner, heading)) return 0;
	bl->weapon[b] = weapon;
	bl->time[b] = KP_DAT(g, weapons, remove_after, weapon);
	u->movement_flags[f] = kp_bor(u->movement_flags[f], FLAG_ATTACKING);
	bl->hit_flags[b] = bl->bounces[b] = 0;
	/* SetOrderNoTarget 0x4179b0 */
	u->order[f] = STATE_INIT;
	u->order_x[f] = u->order_y[f] = 0;
	u->order_state[f] = 0;
	u->order_unit[f] = 0;
	u->order_type[f] = NO_TYPE;
	state_init(g, b);
	spin = KP_DAT(g, weapons, launch_spin, weapon);
	if (spin != 0) {
		int64_t sign, dir;
		if (spawner == bl->last_spawner) sign = bl->spin_sign == 0;
		else sign = kp_band(kp_rng_game_raw(g->rng, SPIN_RANDOM), 1);
		bl->spin_sign = sign;
		dir = kp_mod(u->velocity_direction[f] + (sign ? spin : -spin), 256);
		bl->last_spawner = spawner;
		u->velocity_direction[f] = u->heading[f] = dir;
	}
	bl->source[b] = spawner;
	if (kp_band(KP_DAT(g, units, special_ability_flags, u->type[spawner]), DAT_SUBUNIT) != 0) bl->source[b] = u->subunit[spawner];
	if (u->type[spawner] == SCARAB) bl->source[b] = u->union_0xc0[spawner];
	if (kp_band(u->status[spawner], STATUS_HALLUCINATION) != 0) bl->hit_flags[b] = kp_bor(bl->hit_flags[b], HIT_HALLUCINATION);
	bl->previous_target[b] = 0;
	s = kp_unit_sprite(g, f);
	target = u->order_unit[spawner];
	if (target != 0) {
		ts = kp_unit_sprite(g, target);
		w->spr.elevation[s] = (int32_t)kp_mod(w->spr.elevation[ts] + 1, 256);
		u->order_unit[f] = target;
		u->order_x[f] = kp_s16v(w->spr.x[ts]);
		u->order_y[f] = kp_s16v(w->spr.y[ts]);
	} else {
		w->spr.elevation[s] = (int32_t)kp_mod(w->spr.elevation[kp_unit_sprite(g, spawner)] + 1, 256);
		u->order_unit[f] = 0;
		u->order_x[f] = u->order_x[spawner];
		u->order_y[f] = u->order_y[spawner];
	}
	switch (behavior(g, weapon)) {
	case FLY_TO_POINT: case FLY_TO_TARGET: case BOUNCE:
		if (behavior(g, weapon) == BOUNCE) bl->bounces[b] = 3;
		if (target != 0 && bl->source[b] != 0 && misses(g, b, target)) {
			ox = kp_sarv(kp_mulv(KP_DIR_X[kp_mod(heading, 256)], MISS_OFFSET), 8);
			oy = kp_sarv(kp_mulv(KP_DIR_Y[kp_mod(heading, 256)], MISS_OFFSET), 8);
			u->order_x[f] = kp_s16v(u->order_x[f] - ox);
			u->order_y[f] = kp_s16v(u->order_y[f] - oy);
			bl->hit_flags[b] = kp_bor(bl->hit_flags[b], HIT_MISS);
		}
		kp_flingy_set_move_target(g, f, u->order_x[f], u->order_y[f]);
		return 1;
	case APPEAR_ON_TARGET_UNIT: case APPEAR_ON_TARGET_SITE:
		if (target != 0 && bl->source[b] != 0 && misses(g, b, target)) {
			ox = kp_sarv(kp_mulv(KP_DIR_X[kp_mod(heading, 256)], MISS_OFFSET), 8);
			oy = kp_sarv(kp_mulv(KP_DIR_Y[kp_mod(heading, 256)], MISS_OFFSET), 8);
			kp_flingy_set_position(g, f, kp_s16v(w->spr.x[s] - ox), kp_s16v(w->spr.y[s] - oy));
			bl->hit_flags[b] = kp_bor(bl->hit_flags[b], HIT_MISS);
		}
		return 1;
	case PERSIST_ON_SITE:
		kp_flingy_set_position(g, f, u->order_x[f], u->order_y[f]);
		return 1;
	case APPEAR_ON_ATTACKER:
		return 1;
	case SELF_DESTRUCT:
		kp_game_fail(g, "bullet: the attacker's self-destruction (0x425700)");
	case ATTACK_NEARBY:
		kp_game_fail(g, "bullet: the nearby-area ring (0x409ebb)");
	case MAX_RANGE:
		kp_game_fail(g, "bullet: flying to the maximum range (0x409fd2)");
	default:
		return 0;
	}
}

/* The weapons a halted spawner may still fire (0x408f00's allowlist). */
static int halted_weapon(int64_t weapon)
{
	static const int64_t allowed[] = { 6, 32, 33, 34, 50, 51, 54, 56, 57, 58, 59, 60, 61, 68, 69, 82, 83, 84, 102, 105, 106,
		107, 108 };
	size_t k;
	for (k = 0; k < sizeof allowed / sizeof allowed[0]; k++) {
		if (allowed[k] == weapon) return 1;
	}
	return 0;
}

/* CreateBullet 0x408f00: the bullet, or 0. */
static int64_t create_bullet(KpGame *g, int64_t weapon, int64_t x, int64_t y, int64_t owner, int64_t heading,
	int64_t spawner)
{
	KpBullets *bl = &g->bullets;
	int64_t b = bl->free.head;
	if (b == 0) return 0;
	if (weapon == HALO_ROCKETS && bl->allocated >= HALO_ROCKETS_MAX) return 0;
	if (kp_band(g->u.status[spawner], STATUS_HALTED) != 0 && !halted_weapon(weapon)) return 0;
	if (!init_bullet(g, b, weapon, x, y, owner, heading, spawner)) return 0;
	kp_list64_unlink(bl->prev, bl->next, &bl->free, b, 0);
	kp_list64_insert_after_head(bl->prev, bl->next, &bl->active, b, 0);
	bl->allocated++;
	return b;
}

/* ClipSegmentToRect 0x48c6d0 (Cohen-Sutherland, 16-bit points). */
static int outcode(int64_t x, int64_t y, const int64_t *r)
{
	int c = 0;
	if (x < r[0]) c |= 1;
	else if (x > r[2]) c |= 2;
	if (y < r[1]) c |= 8;
	else if (y > r[3]) c |= 4;
	return c;
}

int kp_clip_segment(KpGame *g, int64_t *a, int64_t *bp, const int64_t *r)
{
	int ca = outcode(a[0], a[1], r), cb = outcode(bp[0], bp[1], r);
	for (;;) {
		int c;
		int64_t x, y, *p;
		if (ca == 0 && cb == 0) return 1;
		if ((ca & cb) != 0) return 0;
		c = ca != 0 ? ca : cb;
		if (c & 8) {
			y = r[1];
			x = a[0] + kp_idiv64(g, kp_s32v((r[1] - a[1]) * (bp[0] - a[0])), bp[1] - a[1]);
		} else if (c & 4) {
			y = r[3];
			x = a[0] + kp_idiv64(g, kp_s32v((r[3] - a[1]) * (bp[0] - a[0])), bp[1] - a[1]);
		} else if (c & 2) {
			x = r[2];
			y = a[1] + kp_idiv64(g, kp_s32v((r[2] - a[0]) * (bp[1] - a[1])), bp[0] - a[0]);
		} else {
			x = r[0];
			y = a[1] + kp_idiv64(g, kp_s32v((r[0] - a[0]) * (bp[1] - a[1])), bp[0] - a[0]);
		}
		p = c == ca ? a : bp;
		p[0] = kp_s16v(x);
		p[1] = kp_s16v(y);
		if (p == a) ca = outcode(a[0], a[1], r);
		else cb = outcode(bp[0], bp[1], r);
	}
}

/* GetTargetImpactPoint 0x420970: where the line from the target's center to the attacker leaves its inner box (the
 * target's box shrunk by a quarter on each side). */
void kp_impact_point(KpGame *g, int64_t n, int64_t *x, int64_t *y)
{
	KpUnits *u = &g->u;
	int64_t target = u->order_unit[n], t = u->type[target], r[4], a[2], c[2], dx, dy;
	int32_t ts = kp_unit_sprite(g, target), s = kp_unit_sprite(g, n);
	int64_t tx = kp_s16v(g->w->spr.x[ts]), ty = kp_s16v(g->w->spr.y[ts]);
#define EXT(k) KP_DAT(g, units, unit_extents, t * 4 + (k))
	int64_t left = kp_s16v(tx - EXT(0)), top = kp_s16v(ty - EXT(1));
	int64_t right = kp_s16v(tx + EXT(2)), bottom = kp_s16v(ty + EXT(3));
#undef EXT
	dx = kp_sarv(right - left, 2);
	dy = kp_sarv(bottom - top, 2);
	r[0] = kp_s16v(left + dx);
	r[1] = kp_s16v(top + dy);
	r[2] = kp_s16v(right - dx);
	r[3] = kp_s16v(bottom - dy);
	c[0] = kp_s16v(kp_idiv64(g, r[2] - r[0], 2) + r[0]);
	c[1] = kp_s16v(kp_idiv64(g, r[3] - r[1], 2) + r[1]);
	a[0] = kp_s16v(g->w->spr.x[s]);
	a[1] = kp_s16v(g->w->spr.y[s]);
	if (kp_clip_segment(g, c, a, r)) {
		*x = a[0];
		*y = a[1];
	} else {
		*x = c[0];
		*y = c[1];
	}
}

/* SpawnWeaponBullet 0x420030. */
void kp_spawn_weapon_bullet(KpGame *g, int64_t n, int64_t weapon)
{
	KpUnits *u = &g->u;
	int64_t x, y, be = behavior(g, weapon);
	if (be == APPEAR_ON_TARGET_UNIT) {
		if (u->order_unit[n] == 0) return;
		kp_impact_point(g, n, &x, &y);
	} else if (be == APPEAR_ON_TARGET_SITE) {
		x = kp_s16v(u->order_x[n]);
		y = kp_s16v(u->order_y[n]);
	} else {
		int32_t s = kp_unit_sprite(g, n);
		int64_t h = u->heading[n], d = KP_DAT(g, weapons, forward_offset, weapon);
		x = kp_sarv(kp_mulv(KP_DIR_X[h], d), 8) + kp_s16v(g->w->spr.x[s]);
		y = kp_sarv(kp_mulv(KP_DIR_Y[h], d), 8) + kp_s16v(g->w->spr.y[s]) - KP_DAT(g, weapons, upward_offset, weapon);
	}
	if (KP_DAT(g, weapons, graphics, weapon) == 0) return;
	create_bullet(g, weapon, x, y, u->owner[n], u->heading[n], n);
}

/* FireWeaponBySelector 0x41f5d0: selector 1 the ground weapon, any other the air weapon; a weapon of damage factor 2
 * fires twice. */
void kp_fire_weapon_by_selector(KpGame *g, int64_t n, int64_t selector)
{
	KpUnits *u = &g->u;
	int64_t weapon;
	if (u->order_unit[n] == 0) return;
	if (kp_mod(selector, 256) == 1) weapon = kp_ground_weapon(g, n);
	else weapon = KP_DAT(g, units, air_weapon, u->type[n]);
	kp_spawn_weapon_bullet(g, n, weapon);
	if (KP_DAT(g, weapons, damage_factor, weapon) == 2) kp_spawn_weapon_bullet(g, n, weapon);
}

/* CanWeaponTargetUnit 0x41dbf0: the weapons.dat target flags against unit target (n the attacker). */
int kp_weapon_can_target(KpGame *g, int64_t n, int64_t weapon, int64_t target)
{
	KpUnits *u = &g->u;
	int64_t flags, props;
	weapon = kp_mod(weapon, 256);
	if (weapon >= NO_WEAPON) return 0;
	flags = KP_DAT(g, weapons, target_flags, weapon);
	if (target == 0) return kp_band(flags, TARGET_TERRAIN) != 0;
	if (kp_band(u->status[target], STATUS_INVINCIBLE) != 0) return 0;
	if (kp_band(flags, kp_band(u->status[target], STATUS_AIR) != 0 ? TARGET_AIR : TARGET_GROUND) == 0) return 0;
	props = KP_DAT(g, units, special_ability_flags, u->type[target]);
	if (kp_band(flags, TARGET_MECHANICAL) != 0 && kp_band(props, DAT_MECHANICAL) == 0) return 0;
	if (kp_band(flags, TARGET_NOT_ROBOTIC) != 0 && kp_band(props, DAT_ROBOTIC) != 0) return 0;
	if (kp_band(flags, TARGET_ORGANIC) != 0 && kp_band(props, DAT_ORGANIC) == 0) return 0;
	if (kp_band(flags, TARGET_NOT_BUILDING) != 0 && kp_band(props, DAT_BUILDING) != 0) return 0;
	if (kp_band(flags, TARGET_NOT_ROBOTIC_SPELL) != 0 && kp_band(props, DAT_MECHANICAL | DAT_ORGANIC) == 0) return 0;
	if (kp_band(flags, TARGET_OWN) != 0 && u->owner[n] != u->owner[target]) return 0;
	return 1;
}

/* BulletInTargetRange 0x40a9f0: unit c's box within radius of the bullet's sprite. */
static int bullet_in_range(KpGame *g, int64_t b, int64_t radius, int64_t c)
{
	int32_t bs = kp_unit_sprite(g, kp_bullet_flingy(g, b)), cs = kp_unit_sprite(g, c);
	int64_t t = g->u.type[c], bx = kp_s16v(g->w->spr.x[bs]), by = kp_s16v(g->w->spr.y[bs]);
	int64_t cx = g->w->spr.x[cs], cy = g->w->spr.y[cs], l, tp, r, bt, dx, dy;
#define EXT(k) KP_DAT(g, units, unit_extents, t * 4 + (k))
	l = kp_s16v(cx - EXT(0));
	tp = kp_s16v(cy - EXT(1));
	r = kp_s16v(cx + EXT(2) + 1);
	bt = kp_s16v(cy + EXT(3) + 1);
#undef EXT
	dx = bx - r;
	if (dx < 0) {
		dx = l - bx;
		if (dx < 0) dx = 0;
	}
	dy = by - bt;
	if (dy < 0) {
		dy = tp - by;
		if (dy < 0) dy = 0;
	}
	return kp_u32v(radius) >= kp_approx_distance(dx, dy);
}

/* SplashDamageCallback 0x4095a0: the ground pass (0x409590, ground_pass) damages the inner, medium and outer
 * radii by 1, 2 and 4; the air splash's second pass (0x4096c0) spares the inner radius, burrowed units, the target
 * and Interceptors. */
static void splash_candidate(KpGame *g, int64_t b, int64_t c, int ground_pass)
{
	KpUnits *u = &g->u;
	int64_t f = kp_bullet_flingy(g, b), weapon = g->bullets.weapon[b], effect;
	if (c == g->bullets.source[b] && weapon != PSI_STORM) return;
	effect = KP_DAT(g, weapons, explosion_type, weapon);
	if ((effect == EFFECT_SPLASH_ENEMY || effect == EFFECT_AIR_SPLASH) && u->owner[c] == u->owner[f]
		&& c != u->order_unit[f]) {
		return;
	}
	if (!kp_weapon_can_target(g, c, weapon, c)) return;
	if (!bullet_in_range(g, b, KP_DAT(g, weapons, outer_splash_range, weapon), c)) return;
	if (weapon == PSI_STORM) {
		if (u->storm_timer[c] != 0) return;
		u->storm_timer[c] = 1;
	}
	if (ground_pass) {
		if (bullet_in_range(g, b, KP_DAT(g, weapons, inner_splash_range, weapon), c)) {
			kp_bullet_damage(g, c, b, 1);
			return;
		}
	}
	if (kp_band(u->status[c], STATUS_BURROWED) != 0) return;
	if (!ground_pass && (c == u->order_unit[f] || u->type[c] == INTERCEPTOR)) return;
	if (bullet_in_range(g, b, KP_DAT(g, weapons, medium_splash_range, weapon), c)) kp_bullet_damage(g, c, b, 2);
	else kp_bullet_damage(g, c, b, 4);
}

/* The units overlapping the bullet's sprite position plus and minus the outer splash radius (16-bit), in the order of
 * QueryUnitsOverlappingRect 0x4224c0, in a query frame (kp_query_release). */
static int64_t *splash_area(KpGame *g, int64_t b, int *count)
{
	int32_t s = kp_unit_sprite(g, kp_bullet_flingy(g, b));
	int64_t r = KP_DAT(g, weapons, outer_splash_range, g->bullets.weapon[b]);
	int64_t x = g->w->spr.x[s], y = g->w->spr.y[s];
	return kp_query_overlap(g, kp_s16v(x - r), kp_s16v(y - r), kp_s16v(x + r), kp_s16v(y + r), count);
}

/* The air splash (effect 24): one primary target, then the second pass. */
static void air_splash(KpGame *g, int64_t b)
{
	KpUnits *u = &g->u;
	int64_t f = kp_bullet_flingy(g, b), weapon = g->bullets.weapon[b], *found;
	int count, k, chosen = 0;
	found = splash_area(g, b, &count);
	for (k = 0; k < count; k++) {
		/* CollectAirSplashCandidate 0x4096d0; the candidates are kept in place, in order */
		int64_t c = found[k];
		if (c == g->bullets.source[b]) continue;
		if (u->owner[c] == u->owner[f] && c != u->order_unit[f]) continue;
		if (!kp_weapon_can_target(g, c, weapon, c)) continue;
		if (!bullet_in_range(g, b, KP_DAT(g, weapons, outer_splash_range, weapon), c)) continue;
		if (bullet_in_range(g, b, KP_DAT(g, weapons, inner_splash_range, weapon), c) && c == u->order_unit[f]) {
			found[0] = c;
			chosen = 1;
			break;
		}
		found[chosen++] = c;
	}
	if (chosen != 0) {
		int64_t idx = chosen == 1 ? 0 : (int64_t)(kp_rng_game_raw(g->rng, AIR_SPLASH_RANDOM) % (uint32_t)chosen);
		int64_t c = found[idx];
		kp_query_release(g, found);
		kp_bullet_damage(g, c, b, 1);
	} else {
		kp_query_release(g, found);
	}
	found = splash_area(g, b, &count);
	for (k = 0; k < count; k++) splash_candidate(g, b, found[k], 0);
	kp_query_release(g, found);
}

/* The target of an effect is alive: it has a sprite and is not dying (IsUnitInactive 0x41fe40). */
static int alive(KpGame *g, int64_t n) { return n != 0 && g->u.sprite[n] != KP_NONE && g->u.order[n] != 0; }

/* ApplyBulletHitEffect 0x4090e0 (domissiledmg of bullet b). */
void kp_bullet_hit(KpGame *g, int64_t b)
{
	KpUnits *u = &g->u;
	KpBullets *bl = &g->bullets;
	int64_t f = kp_bullet_flingy(g, b), weapon = bl->weapon[b], target = u->order_unit[f];
	int64_t effect = KP_DAT(g, weapons, explosion_type, weapon);
	switch (effect) {
	case EFFECT_NONE:
		return;
	case EFFECT_NORMAL:
		if (target == 0) return;
		if (kp_band(bl->hit_flags[b], HIT_MISS) == 0) {
			int64_t divisor = 1;
			if (behavior(g, weapon) == BOUNCE) {
				int64_t k = kp_mod(2 - bl->bounces[b], 256);
				while (k != 0) {
					divisor = kp_mod(divisor * 3, 256);
					k = kp_mod(k - 1, 256);
				}
			}
			kp_bullet_damage(g, target, b, divisor);
		} else if (u->ai[target] != 0 && bl->source[b] != 0) {
			kp_on_unit_damaged(g, target, bl->source[b], 1);
		}
		return;
	case EFFECT_YAMATO:
		if (target != 0) kp_bullet_damage(g, target, b, 1);
		return;
	case EFFECT_SPLASH_RADIAL: case EFFECT_SPLASH_ENEMY: case EFFECT_NUCLEAR: {
		int64_t *found;
		int count, k;
		if (weapon == SUBTERRANEAN_SPINES) kp_game_fail(g, "bullet: the Lurker's line splash (0x409770)");
		found = splash_area(g, b, &count);
		for (k = 0; k < count; k++) splash_candidate(g, b, found[k], 1);
		kp_query_release(g, found);
		return;
	}
	case EFFECT_AIR_SPLASH:
		air_splash(g, b);
		return;
	case EFFECT_PARASITE:
		if (alive(g, target)) kp_apply_parasite(g, target, u->owner[f], bl->source[b]);
		return;
	case EFFECT_BROODLINGS:
		if (bl->source[b] != 0 && alive(g, target)) {
			kp_ai_react_to_attack(g, target, bl->source[b], 1, 0);
			kp_spawn_broodlings(g, bl->source[b], target);
		}
		return;
	case EFFECT_ENSNARE: {
		int32_t s = kp_unit_sprite(g, f);
		kp_ensnare_area(g, kp_s16v(g->w->spr.x[s]), kp_s16v(g->w->spr.y[s]), bl->source[b]);
		return;
	}
	default:
		kp_game_fail(g, "bullet: weapon effect %lld (0x4090e0)", (long long)effect);
	}
}

/* The flingy record of the IScript's current bullet (0x543308). */
int64_t kp_current_bullet(KpGame *g)
{
	if (!g->w->has_bullet) kp_game_fail(g, "bullet: no current bullet");
	return g->w->bullet;
}

/* The sprol test of 0x4107d6: a bullet of a Goliath (0x40fd10) with Charon Boosters, or of a hero Goliath in the
 * expansion, spawns the other missile trail. */
int kp_bullet_goliath_trail(KpGame *g, int64_t b)
{
	KpUnits *u = &g->u;
	int64_t src;
	if (b < 1 || b > KP_BULLETS) kp_game_fail(g, "bullet: record %lld out of range", (long long)b);
	src = g->bullets.source[b];
	if (src == 0 || (u->type[src] != 3 && u->type[src] != 17)) return 0;
	if (kp_upgrade_level(g, u->owner[src], CHARON_BOOSTERS) != 0) return 1;
	return kp_band(KP_DAT(g, units, special_ability_flags, u->type[src]), DAT_HERO) != 0 && g->expansion != 0;
}
