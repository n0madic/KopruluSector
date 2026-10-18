/* Spider Mines (kp_game.h), ported from the atlas (P11, P12): the Vulture's PlaceMine order 132, the mine's order 20
 * with its target search 0x431e90, and the intercept course of a pursuer (0x40c3f0); the Infested Terran's SapUnit
 * and SapLocation orders, which run into the target the same way and burst. */
#include "kp_game_internal.h"

#define SPIDER_MINES 3 /* the tech */
#define SPIDER_MINE 13
#define PLACE_RANGE 20
#define TRIGGER_RANGE 576
#define BURST_RANGE 30
#define ARM_DELAY 60
#define HOVER 0xc1 /* units.dat unknown_flags of a hovering unit */
#define BRAKING 0x4 /* movement flag */
#define STATUS_GROUNDED_BUILDING 0x2
#define STATUS_AIR 0x4
#define STATUS_CANNOT_ATTACK 0x8000
#define STATUS_INTERCEPT 0x80000
#define STATUS_NO_COLLIDE 0x200000
#define STATUS_INVINCIBLE 0x4000000
#define STATUS_HALLUCINATION 0x40000000
#define STATUS_CLOAK_BITS 0x300
#define SPRITE_BURROWED 0x40
#define SECONDARY_CLOAK 0x6d
#define SECONDARY_NOTHING 0x17
#define SIGNAL_DONE 0x1
#define SIGNAL_BURROW 0x4
#define UNDETERMINED 0x80000000LL
#define ANIM_SPECIAL1 13
#define ANIM_BURROW 25
#define ANIM_UNBURROW 26
#define FLYER 2 /* movement type */

/* SetMoveTargetAndFacing 0x40cc40. */
static void move_and_face(KpGame *g, int64_t n, int64_t x, int64_t y)
{
	kp_flingy_set_move_target(g, n, x, y);
	kp_flingy_set_target_waypoint(g, n, x, y);
}

/* 0x405c90: the second approximate distance, max + max/32 + 3·min/256 with its correction when max/4 < min. */
static int64_t approx_distance2(int64_t dx, int64_t dy)
{
	uint32_t a = (uint32_t)(dx < 0 ? -dx : dx), b = (uint32_t)(dy < 0 ? -dy : dy), r, q, m;
	if (a < b) {
		r = a;
		a = b;
		b = r;
	}
	m = (b * 3u) >> 1;
	r = a + (a >> 5) + (m >> 7);
	q = a >> 2;
	if (q < b) {
		q >>= 3;
		m >>= 2;
		r = r - q + m;
		q >>= 2;
		m >>= 3;
		r = r - q + m;
	}
	return (int64_t)r;
}

/* MoveToInterceptTarget 0x40c3f0: pursuer n heads for where target will be. */
void kp_pursue(KpGame *g, int64_t n, int64_t target)
{
	KpUnits *u = &g->u;
	int32_t ts = kp_unit_sprite(g, target), s = kp_unit_sprite(g, n);
	int64_t speed = kp_sarv(kp_s32v(u->top_speed[n] * 2), 8), t, v, x, y, dir;
	if ((u->move_x[target] == u->x[target] && u->move_y[target] == u->y[target]) || speed <= 0) {
		move_and_face(g, n, kp_s16v(g->w->spr.x[ts]), kp_s16v(g->w->spr.y[ts]));
		return;
	}
	if (kp_band(u->movement_flags[target], BRAKING) != 0) {
		kp_flingy_set_move_target(g, n, kp_s16v(u->move_x[target]), kp_s16v(u->move_y[target]));
		kp_flingy_set_target_waypoint(g, n, kp_s16v(u->move_x[target]), kp_s16v(u->move_y[target]));
		return;
	}
	t = approx_distance2(kp_s16v(g->w->spr.x[s]) - kp_s16v(g->w->spr.x[ts]), kp_s16v(g->w->spr.y[s]) - kp_s16v(g->w->spr.y[ts]));
	t = kp_s32v(t / speed);
	if (u->movement_type[n] == FLYER) {
		v = u->top_speed[target];
	} else {
		v = kp_s32v(kp_mod(u->acceleration[target], 65536) * t);
		v = kp_s32v(v / 2 + u->speed2[target]);
		if (v > kp_s32v(u->top_speed[target])) v = kp_s32v(u->top_speed[target]);
	}
	v = kp_s32v(v * t);
	dir = kp_mod(u->velocity_direction[target], 256);
	x = kp_sarv(kp_s32v((int64_t)KP_DIR_X[dir] * v), 16) + kp_s16v(g->w->spr.x[ts]);
	y = kp_sarv(kp_s32v((int64_t)KP_DIR_Y[dir] * v), 16) + kp_s16v(g->w->spr.y[ts]);
	if (x < 0) x = 0;
	else if (x >= kp_mod(g->map_w_px, 65536)) x = kp_mod(g->map_w_px, 65536) - 1;
	if (y < 0) y = 0;
	else if (y >= kp_mod(g->map_h_px, 65536)) y = kp_mod(g->map_h_px, 65536) - 1;
	move_and_face(g, n, x, y);
}

/* OrderPlaceMine 0x436820 (order 132). */
void kp_order_place_mine(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t x, y, m;
	int32_t s;
	if (u->order_state[n] == 0) {
		int64_t t = u->order_unit[n];
		if (t != 0) {
			if (!kp_request_move_to_unit(g, n, t)) return;
			s = kp_unit_sprite(g, t);
			u->order_x[n] = kp_s16v(g->w->spr.x[s]);
			u->order_y[n] = kp_s16v(g->w->spr.y[s]);
		} else if (!kp_request_move_to_point(g, n, kp_s16v(u->order_x[n]), kp_s16v(u->order_y[n]))) {
			return;
		}
		u->order_state[n] = 1;
		return;
	}
	s = kp_unit_sprite(g, n);
	/* at its move target the order ends only with status 0x80000; else the distance decides */
	if (!kp_can_use_tech(g, SPIDER_MINES, u->owner[n], n)
		|| (u->move_x[n] == kp_s16v(g->w->spr.x[s]) && u->move_y[n] == kp_s16v(g->w->spr.y[s])
			&& kp_band(u->status[n], STATUS_INTERCEPT) != 0)) {
		kp_order_done(g, n);
		return;
	}
	if (!kp_within_distance(g, n, PLACE_RANGE, kp_s16v(u->order_x[n]), kp_s16v(u->order_y[n]))) {
		if (u->ai[n] != 0 && kp_move_arrival(g, n) != 0) kp_order_done(g, n);
		return;
	}
	kp_stop_and_face(g, n);
	x = kp_s16v(u->order_x[n]);
	y = kp_s16v(u->order_y[n]);
	kp_clamp_to_map(g, SPIDER_MINE, &x, &y);
	m = kp_unit_create(g, SPIDER_MINE, x, y, u->owner[n]);
	if (m != 0) {
		kp_unit_complete_in_place(g, m);
		kp_flingy_set_heading(g, m, u->heading[n]);
		u->union_0xc0[n] = kp_band(u->union_0xc0[n], 0xffffff00) + kp_band(u->union_0xc0[n] - 1, 0xff);
	}
	/* a failure shows the unit limit message (0x425020): the interface's */
	kp_order_done(g, n);
}

/* The mine's target test 0x432390. */
static int mine_prey(KpGame *g, int64_t c, int64_t mine)
{
	KpUnits *u = &g->u;
	return kp_is_enemy(g, mine, c) && kp_band(u->status[c], STATUS_INVINCIBLE) == 0
		&& kp_band(u->status[c], STATUS_GROUNDED_BUILDING | STATUS_AIR) == 0
		&& KP_DAT(g, units, unknown_flags, u->type[c]) != HOVER;
}

/* SpiderMineAcquireTarget 0x431e90. */
static int64_t mine_target(KpGame *g, int64_t n)
{
	if (kp_band(g->u.status[n], STATUS_CANNOT_ATTACK) != 0) return 0;
	return kp_nearest_unit_around(g, n, kp_seek_range(g, n) * 32, mine_prey, n);
}

/* StartCloakingWithUiRefresh 0x41d300. */
static void cloak_mine(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	kp_cloak_list_add(g, n);
	u->secondary_timer[n] = 0;
	kp_sprite_set_cloak_mode(g->w, kp_unit_sprite(g, n), 3, 0, 0);
	u->status[n] = kp_bor(u->status[n], STATUS_CLOAK_BITS);
	kp_set_secondary_order(g, n, SECONDARY_CLOAK);
	u->visibility[n] = UNDETERMINED;
}

static int signal(KpGame *g, int64_t n, int64_t bit)
{
	KpUnits *u = &g->u;
	if (kp_band(u->order_signal[n], bit) == 0) return 0;
	u->order_signal[n] = kp_band(u->order_signal[n], kp_bnot(bit));
	return 1;
}

/* OrderVultureMine 0x436cf0 (order 20): arm, burrow, wait, rise, chase and burst. */
void kp_order_spider_mine(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	KpImages *w = g->w;
	int32_t s = kp_unit_sprite(g, n);
	int64_t t;
	switch (u->order_state[n]) {
	case 0:
		u->ground_cooldown[n] = ARM_DELAY;
		u->order_state[n] = 1;
		/* fall through */
	case 1:
		if (u->ground_cooldown[n] != 0) return;
		kp_sprite_play(w, s, ANIM_BURROW, 1);
		u->status[n] = kp_bor(u->status[n], STATUS_NO_COLLIDE);
		u->order_state[n] = 2;
		/* fall through */
	case 2:
		if (!signal(g, n, SIGNAL_BURROW)) return;
		if (kp_band(u->status[n], STATUS_HALLUCINATION) == 0) {
			kp_set_burrowed(g, n);
			cloak_mine(g, n);
		}
		u->order_state[n] = 3;
		/* fall through */
	case 3:
		t = mine_target(g, n);
		if (t == 0) return;
		u->order_unit[n] = t;
		kp_sprite_play(w, s, ANIM_UNBURROW, 1);
		w->spr.flags[s] = (int32_t)kp_band(w->spr.flags[s], kp_bnot(SPRITE_BURROWED));
		kp_set_secondary_order(g, n, SECONDARY_NOTHING);
		u->order_state[n] = 4;
		/* fall through */
	case 4:
		if (!signal(g, n, SIGNAL_BURROW)) return;
		w->spr.elevation[s] = (int32_t)KP_DAT(g, units, elevation_level, u->type[n]);
		u->status[n] = kp_band(u->status[n], kp_bnot(STATUS_NO_COLLIDE));
		if (u->order_unit[n] == 0) {
			u->order_state[n] = 1;
			return;
		}
		kp_pursue(g, n, u->order_unit[n]);
		u->order_state[n] = 5;
		return;
	case 5:
		kp_clear_burrowed(g, n);
		t = u->order_unit[n];
		if (t == 0 || !kp_unit_box_within(g, n, TRIGGER_RANGE, t)) {
			kp_stop_movement(g, n);
			u->order_state[n] = 1;
			return;
		}
		kp_pursue(g, n, t);
		if (!kp_unit_box_within(g, n, BURST_RANGE, t)) {
			if (kp_move_arrival(g, n) == 2) {
				kp_stop_movement(g, n);
				u->order_state[n] = 1;
			}
			return;
		}
		u->order_unit[n] = 0;
		s = kp_unit_sprite(g, n);
		u->order_x[n] = kp_s16v(w->spr.x[s]);
		u->order_y[n] = kp_s16v(w->spr.y[s]);
		kp_sprite_play(w, s, ANIM_SPECIAL1, 1);
		u->order_state[n] = 6;
		/* fall through */
	case 6:
		if (kp_band(u->order_signal[n], SIGNAL_DONE) != 0) kp_unit_kill(g, n);
		return;
	default:
		return;
	}
}

#define SAP_WEAPON 54 /* Suicide Infested Terran */
#define SAP_CONTACT 4
#define SAP_INTERCEPT_RANGE 0x100
#define SAP_ORDER_MOVE 6
#define STATUS_NOT_INTERRUPTIBLE 0x1000
#define STATUS_EXPIRED 0x80000000LL
#define MOVING 0x2 /* movement flag */

/* The burst shared by SapUnit and SapLocation: the Infested Terran plays its special animation and dies on its signal. */
static void sap_burst(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	u->order_unit[n] = n;
	kp_sprite_play(g->w, kp_unit_sprite(g, n), ANIM_SPECIAL1, 1);
	u->status[n] = kp_bor(u->status[n], STATUS_NOT_INTERRUPTIBLE);
	u->order_state[n] = 2;
}

static void sap_detonate(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	if (kp_band(u->order_signal[n], SIGNAL_DONE) == 0) return;
	u->status[n] = kp_bor(u->status[n], STATUS_EXPIRED);
	kp_unit_kill(g, n);
}

/* SapUnit 0x41ef70: the Infested Terran runs into its target and explodes. */
void kp_order_sap_unit(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t t = u->order_unit[n], arrival;
	int32_t ts;
	if (t == 0) {
		kp_order_done(g, n);
		return;
	}
	if (!kp_can_attack(g, n, t, 1)) {
		ts = kp_unit_sprite(g, t);
		kp_issue_order_at(g, n, SAP_ORDER_MOVE, kp_s16v(g->w->spr.x[ts]), kp_s16v(g->w->spr.y[ts]));
		return;
	}
	switch (u->order_state[n]) {
	case 0:
		if (!kp_set_move_target_unit(g, n, t)) return;
		u->order_state[n] = 1;
		/* fall through */
	case 1:
		arrival = kp_move_arrival(g, n);
		if (!kp_unit_box_within(g, n, SAP_CONTACT, t) && arrival != 2) {
			if (kp_unit_box_within(g, n, SAP_INTERCEPT_RANGE, t) && kp_band(u->movement_flags[t], MOVING) != 0) {
				kp_pursue(g, n, t);
			} else {
				kp_move_toward_target(g, n, t);
			}
			return;
		}
		kp_stop_movement(g, n);
		if (!kp_unit_box_within(g, n, KP_DAT(g, weapons, outer_splash_range, SAP_WEAPON), t)) {
			kp_order_done(g, n);
			return;
		}
		sap_burst(g, n);
		/* fall through */
	case 2:
		sap_detonate(g, n);
		return;
	default:
		return;
	}
}

/* SapLocation 0x41eeb0: the Infested Terran runs to the order point and explodes there. */
void kp_order_sap_location(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t x = kp_s16v(u->order_x[n]), y = kp_s16v(u->order_y[n]);
	switch (u->order_state[n]) {
	case 0:
		if (!kp_request_move_to_point(g, n, x, y)) return;
		u->order_state[n] = 1;
		/* fall through */
	case 1:
		switch (kp_move_arrival(g, n)) {
		case 0: return;
		case 2:
			if (!kp_within_distance(g, n, KP_DAT(g, weapons, outer_splash_range, SAP_WEAPON), x, y)) {
				kp_order_done(g, n);
				return;
			}
			break;
		default: break;
		}
		sap_burst(g, n);
		/* fall through */
	case 2:
		sap_detonate(g, n);
		return;
	default:
		return;
	}
}
