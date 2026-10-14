/* Carriers and their Interceptors (kp_game.h), ported from the atlas (P09; docs/research/native-carrier.md): the
 * parent's orders Carrier 50, Fight 56 and HoldPosition 57 (0x41c290 with HangarParentEngage 0x41bfe0), Attack 53
 * (0x41c310), Stop 52 (0x41c410) and 55 (0x41c3c0), the launch 0x41ccd0 and the return 0x41ce40 of a fighter, the
 * Interceptor's orders Attack 64 (0x41c4e0 with 0x41cb20 and the orbit 0x41d100) and Return 69 (0x41c450), the
 * hangar repair of TrainFighter (secondary order 63, 0x41c630) and the unlink of a dying fighter (0x41cfe0).
 *
 * Union words: a parent's +0xc0 is its inside list, +0xc4 its outside list, the bytes +0xc8/+0xc9 their counts; a
 * fighter's +0xc0 is its parent, +0xc4/+0xc8 its next and previous siblings, the byte +0xcc whether it is outside. */
#include "kp_game_internal.h"

#define INTERCEPTOR 73
#define SCARAB 85
#define CARRIER 72
#define GANTRITHOR 82
#define REAVER 83
#define WARBRINGER 81
#define NO_TYPE 228
#define STATUS_COMPLETED 0x1
#define STATUS_AUTO_ATTACK 0x8
#define STATUS_CANNOT_ATTACK 0x8000
#define STATUS_INVINCIBLE 0x4000000
#define STATUS_TARGET_SWITCH 0x8000000
#define MOVING 0x2
#define CONTROLLER_COMPUTER 1
#define ORDER_MOVE 6
#define ORDER_NOTHING 0x17
#define ORDER_CARRIER 0x32
#define ORDER_CARRIER_FIGHT 0x38
#define ORDER_CARRIER_HOLD 0x39
#define ORDER_REAVER_FIGHT 0x3d
#define ORDER_REAVER_HOLD 0x3e
#define ORDER_CARRIER_IGNORE 0x37
#define ORDER_RETURN 0x45
#define ORDER_COMPUTER_AI 0x9c
#define LAUNCH_DELAY 7
#define LAUNCH_SOUND 616
#define ORBIT_RANDOM 11
#define ORBIT_TIMER 15
#define ORBIT_REACH 50
#define RETURN_LOWER 60
#define RETURN_DOCK 10
#define REPAIR 128
#define FIRE_ANIMATION 5
#define SCARAB_LEAD 25
#define SCARAB_DELAY 60
#define STATUS_CANNOT_ATTACK_FLAG 0x8000

static int64_t *inside(KpGame *g) { return g->u.union_0xc0; }
static int64_t *outside(KpGame *g) { return g->u.union_0xc4; }
static int64_t *parent(KpGame *g) { return g->u.union_0xc0; }
static int64_t *next(KpGame *g) { return g->u.union_0xc4; }
static int64_t *prev(KpGame *g) { return g->u.union_0xc8; }

static int64_t inside_count(KpGame *g, int64_t n) { return kp_band(g->u.union_0xc8[n], 0xff); }
static int64_t outside_count(KpGame *g, int64_t n) { return kp_band(kp_shrv(g->u.union_0xc8[n], 8), 0xff); }

/* The byte +0xc8 (inside) or +0xc9 (outside) of a parent, plus delta. */
static void add_count(KpGame *g, int64_t n, int out, int64_t delta)
{
	int64_t w = g->u.union_0xc8[n], shift = out ? 8 : 0, v = kp_band(kp_shrv(w, shift) + delta, 0xff);
	g->u.union_0xc8[n] = kp_bor(kp_band(w, kp_bnot((int64_t)0xff << shift)), v << shift);
}

static void set_outside_flag(KpGame *g, int64_t c, int v)
{
	g->u.union_0xcc[c] = kp_band(g->u.union_0xcc[c], 0xffffff00) + (v ? 1 : 0);
}

static int is_outside(KpGame *g, int64_t c) { return kp_band(g->u.union_0xcc[c], 0xff) != 0; }

/* The union words 0..2 of a fighter or parent as unit references. */
static void link(KpGame *g, int64_t n, int word, int64_t v) { kp_set_union_unit(g, n, word, v); }

static int reaver(int64_t t) { return t == REAVER || t == WARBRINGER; }

/* LaunchHangarChild 0x41ccd0: a completed fighter leaves the hangar at the parent; 0 when none may. */
static int64_t launch(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t c = inside(g)[n], best, first_hp, x, y;
	int32_t s;
	while (c != 0 && kp_band(u->status[c], STATUS_COMPLETED) == 0) c = next(g)[c];
	if (c == 0) return 0;
	best = c;
	first_hp = kp_s32v(u->hp[c]);
	for (c = next(g)[best]; c != 0; c = next(g)[c]) {
		if (kp_band(u->status[c], STATUS_COMPLETED) != 0 && kp_s32v(u->hp[c]) > first_hp) best = c;
	}
	if (u->type[best] == INTERCEPTOR && kp_s32v(u->hp[best]) < KP_DAT(g, units, hit_points, INTERCEPTOR) / 2) return 0;
	s = kp_unit_sprite(g, n);
	x = kp_s16v(g->w->spr.x[s]);
	y = kp_s16v(g->w->spr.y[s]);
	if (u->type[best] == SCARAB) {
		/* GetDirectionVector 0x40c270: 25 pixels ahead of the Reaver, which the Scarab faces */
		int64_t dir = kp_mod(u->heading[n], 256);
		x = kp_s16v(x + kp_sarv(kp_s32v(KP_DIR_X[dir] * SCARAB_LEAD), 8));
		y = kp_s16v(y + kp_sarv(kp_s32v(KP_DIR_Y[dir] * SCARAB_LEAD), 8));
		kp_flingy_set_heading(g, best, u->heading[n]);
	}
	if (inside(g)[n] == best) link(g, n, 0, next(g)[best]);
	if (next(g)[best] != 0) link(g, next(g)[best], 2, prev(g)[best]);
	if (prev(g)[best] != 0) link(g, prev(g)[best], 1, next(g)[best]);
	add_count(g, n, 0, -1);
	link(g, best, 1, outside(g)[n]);
	link(g, best, 2, 0);
	if (outside(g)[n] != 0) link(g, outside(g)[n], 2, best);
	add_count(g, n, 1, 1);
	link(g, n, 1, best);
	kp_unit_relocate(g, best, x, y);
	kp_unit_show(g, best);
	set_outside_flag(g, best, 1);
	return best;
}

/* ReturnToHangar 0x41ce40: fighter c leaves the outside list of parent n for the inside list and hides. */
static void dock(KpGame *g, int64_t n, int64_t c)
{
	if (prev(g)[c] != 0) link(g, prev(g)[c], 1, next(g)[c]);
	else link(g, n, 1, next(g)[c]);
	if (next(g)[c] != 0) link(g, next(g)[c], 2, prev(g)[c]);
	add_count(g, n, 1, -1);
	link(g, c, 1, inside(g)[n]);
	link(g, c, 2, 0);
	if (inside(g)[n] != 0) link(g, inside(g)[n], 2, c);
	add_count(g, n, 0, 1);
	link(g, n, 0, c);
	set_outside_flag(g, c, 0);
	kp_unit_hide(g, c);
}

/* UnlinkHangarChild 0x41cfe0: a dying fighter leaves its parent's list. */
void kp_hangar_unlink(KpGame *g, int64_t c)
{
	int64_t p = parent(g)[c];
	if (p == 0) {
		link(g, c, 1, 0);
		link(g, c, 2, 0);
		return;
	}
	if (prev(g)[c] != 0) link(g, prev(g)[c], 1, next(g)[c]);
	else link(g, p, is_outside(g, c) ? 1 : 0, next(g)[c]);
	add_count(g, p, is_outside(g, c), -1);
	if (next(g)[c] != 0) link(g, next(g)[c], 2, prev(g)[c]);
}

/* RetargetOutsideChildren 0x41d060. */
static void retarget_outside(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t c;
	int32_t s = kp_unit_sprite(g, n);
	for (c = outside(g)[n]; c != 0; c = next(g)[c]) {
		g->w->spr.elevation[kp_unit_sprite(g, c)] = g->w->spr.elevation[s] - 1;
		kp_issue_order_target(g, c, KP_DAT(g, units, attack_unit, u->type[c]), u->order_unit[n]);
	}
}

/* The Fight variant of a parent's order (0x41d0d0) and its idle variant (0x41d1c0). */
static int64_t fight_order(KpGame *g, int64_t n)
{
	int64_t o = g->u.order[n];
	if (reaver(g->u.type[n])) return o == ORDER_REAVER_HOLD ? ORDER_REAVER_HOLD : ORDER_REAVER_FIGHT;
	return o == ORDER_CARRIER_HOLD ? ORDER_CARRIER_HOLD : ORDER_CARRIER_FIGHT;
}

static int64_t idle_order(KpGame *g, int64_t n)
{
	int64_t o = g->u.order[n];
	if (o == ORDER_CARRIER_IGNORE) return ORDER_CARRIER_IGNORE;
	if (reaver(g->u.type[n])) return o == ORDER_REAVER_HOLD ? 0x3e : 0x3a;
	return o == ORDER_CARRIER_HOLD ? ORDER_CARRIER_HOLD : ORDER_CARRIER;
}

static void clear_target(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	if (u->order_unit[n] == u->auto_target[n]) u->auto_target[n] = 0;
	u->order_unit[n] = 0;
}

/* WithinRouteLimit 0x41d0b0: the walk to the order point is shorter than limit (0: no limit). */
static int within_limit(KpGame *g, int64_t n, int64_t limit)
{
	return limit == 0 || limit > kp_route_distance(g, n, kp_s16v(g->u.order_x[n]), kp_s16v(g->u.order_y[n]));
}

/* HangarParentEngage 0x41bfe0: 1 when the parent should launch now. */
static int engage(KpGame *g, int64_t n, int64_t range, int64_t limit)
{
	KpUnits *u = &g->u;
	int hold = u->order[n] == ORDER_CARRIER_HOLD || u->order[n] == ORDER_REAVER_HOLD;
	int64_t t = u->order_unit[n], before, c;
	if (hold && kp_band(u->movement_flags[n], MOVING) != 0) kp_stop_movement(g, n);
	if (t != 0) {
		if (kp_band(u->status[t], STATUS_INVINCIBLE) != 0 || !kp_can_attack(g, n, t, 1)) {
			clear_target(g, n);
		} else {
			int32_t s = kp_unit_sprite(g, t);
			u->order_x[n] = kp_s16v(g->w->spr.x[s]);
			u->order_y[n] = kp_s16v(g->w->spr.y[s]);
			if (kp_can_see_target(g, n, t) && kp_within_range_moving(g, n, range, 0) && within_limit(g, n, limit)) {
				kp_stop_movement(g, n);
			} else if ((!hold && kp_band(u->status[n], STATUS_TARGET_SWITCH) == 0)
				|| (kp_controller(g, u->owner[n]) == CONTROLLER_COMPUTER && kp_band(u->status[n], STATUS_AUTO_ATTACK) != 0)) {
				if (!kp_request_move_to_point(g, n, kp_s16v(u->order_x[n]), kp_s16v(u->order_y[n]))) return 0;
			} else {
				clear_target(g, n);
				return 0;
			}
		}
	}
	if (inside_count(g, n) + outside_count(g, n) == 0) {
		kp_stop_movement(g, n);
		kp_order_done(g, n);
		return 0;
	}
	if (u->order_timer[n] != 0) return 0;
	before = u->order_unit[n];
	if (before == 0 && u->queue_head[n] != KP_NONE) {
		kp_order_activate(g, n);
		return 0;
	}
	if (before == 0 || kp_band(u->status[n], STATUS_TARGET_SWITCH) != 0) {
		kp_update_attack_target(g, n);
		if ((u->type[n] == CARRIER || u->type[n] == GANTRITHOR) && u->order_unit[n] != before) retarget_outside(g, n);
	}
	if (u->order_unit[n] != 0) {
		if (!hold) u->order[n] = fight_order(g, n);
		return kp_can_see_target(g, n, u->order_unit[n]) && kp_within_range_moving(g, n, range, 0) && within_limit(g, n, limit);
	}
	c = kp_acquire_random_target(g, n);
	if (c == 0) {
		if (!hold) u->order[n] = idle_order(g, n);
		return 0;
	}
	if (!hold) u->order[n] = fight_order(g, n);
	{
		int32_t s = kp_unit_sprite(g, c);
		u->order_x[n] = kp_s16v(g->w->spr.x[s]);
		u->order_y[n] = kp_s16v(g->w->spr.y[s]);
	}
	kp_set_target_switch(g, n);
	if (!within_limit(g, n, limit)) return 0;
	u->order_unit[n] = c;
	return 1;
}

/* Order_CarrierEngage 0x41c290 (orders 50, 56, 57): launches a fighter at the target. */
void kp_order_carrier(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t c;
	if (!engage(g, n, kp_seek_range(g, n) * 32, 0)) {
		if (u->ai[n] != 0 && u->order_timer[n] == 0) kp_issue_order(g, n, ORDER_COMPUTER_AI);
		return;
	}
	c = launch(g, n);
	if (c == 0) return;
	u->shields[c] = KP_DAT(g, units, shield_amount, u->type[c]) * 256;
	g->w->spr.elevation[kp_unit_sprite(g, c)] = g->w->spr.elevation[kp_unit_sprite(g, n)] - 1;
	kp_issue_order_target(g, c, KP_DAT(g, units, attack_unit, INTERCEPTOR), u->order_unit[n]);
	u->order_timer[n] = LAUNCH_DELAY;
}

/* Order_CarrierAttack 0x41c310 (order 53) and OrderReaverAttack 0x41c860 (order 59): the outside fighters take the
 * target, the Fight order goes first. */
void kp_order_carrier_attack(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t t = u->order_unit[n], kept, order, head;
	int32_t s;
	if (t == 0) {
		kp_order_done(g, n);
		return;
	}
	if (!kp_can_attack(g, n, t, 1)) {
		kp_issue_order_at(g, n, ORDER_MOVE, kp_s16v(u->order_x[n]), kp_s16v(u->order_y[n]));
		return;
	}
	kept = kp_band(u->status[n], STATUS_AUTO_ATTACK | STATUS_TARGET_SWITCH);
	if (!reaver(u->type[n])) retarget_outside(g, n); /* OrderReaverAttack 0x41c860 keeps its Scarabs */
	if (reaver(u->type[n])) order = u->order[n] == ORDER_REAVER_HOLD ? ORDER_REAVER_HOLD : ORDER_REAVER_FIGHT;
	else order = u->order[n] == ORDER_CARRIER_HOLD ? ORDER_CARRIER_HOLD : ORDER_CARRIER_FIGHT;
	/* PrependOrNewOrderOnUnit 0x429bc0 */
	s = kp_unit_sprite(g, t);
	head = u->queue_head[n];
	if (head != KP_NONE) kp_order_insert_before(g, n, order, g->w->spr.x[s], g->w->spr.y[s], t, NO_TYPE, head);
	else kp_order_queue(g, n, order, g->w->spr.x[s], g->w->spr.y[s], t, NO_TYPE);
	kp_order_activate(g, n);
	u->status[n] = kp_bor(kp_band(u->status[n], kp_bnot(STATUS_AUTO_ATTACK | STATUS_TARGET_SWITCH)), kept);
}

/* OrderReaver 0x41c790 (orders 58, 61, 62): facing its target, the Reaver sends one Scarab at a time. */
void kp_order_reaver(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t s = kp_band(kp_seek_range(g, n), 0xff), c, d;
	int32_t sp;
	if (!engage(g, n, s * 32, s * 64)) {
		if (u->ai[n] != 0 && u->order_timer[n] == 0) kp_issue_order(g, n, ORDER_COMPUTER_AI);
		return;
	}
	kp_flingy_set_target_waypoint(g, n, kp_s16v(u->order_x[n]), kp_s16v(u->order_y[n]));
	sp = kp_unit_sprite(g, n);
	d = kp_direction_to(g, kp_s16v(g->w->spr.x[sp]), kp_s16v(g->w->spr.y[sp]), kp_s16v(u->target_waypoint_x[n]),
		kp_s16v(u->target_waypoint_y[n]));
	if (kp_u32v(kp_mod(u->heading[n], 256) - d) > 1) {
		u->queue_timer[n] = 0;
		return;
	}
	if (kp_band(u->status[n], STATUS_CANNOT_ATTACK_FLAG) != 0 || outside(g)[n] != 0) return;
	c = launch(g, n);
	if (c == 0) return;
	kp_issue_order_target(g, c, KP_DAT(g, units, attack_unit, SCARAB), u->order_unit[n]);
	link(g, c, 0, n);
	u->order_timer[n] = SCARAB_DELAY;
}

/* Order_CarrierStop 0x41c410 (order 52): the fighters return; the parent keeps its target. */
void kp_order_carrier_stop(KpGame *g, int64_t n)
{
	int64_t c;
	kp_stop_and_face(g, n);
	for (c = outside(g)[n]; c != 0; c = next(g)[c]) kp_issue_order(g, c, ORDER_RETURN);
	kp_issue_order_target(g, n, ORDER_CARRIER, g->u.order_unit[n]);
}

/* Order_CarrierIgnore2 0x41c3c0 (order 55): the parent moves to the point first. */
void kp_order_carrier_ignore(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	if (u->order_state[n] == 0) {
		if (!kp_request_move_to_point(g, n, kp_s16v(u->order_x[n]), kp_s16v(u->order_y[n]))) return;
		u->order_state[n] = 1;
	}
	if (reaver(u->type[n])) {
		kp_order_reaver(g, n);
		if (u->order_unit[n] != 0) u->order[n] = ORDER_REAVER_FIGHT;
		return;
	}
	kp_order_carrier(g, n);
}

/* SetInterceptorOrbit 0x41d100: a random point within 127·scale of (x, y), mirrored back onto the map. */
static void orbit(KpGame *g, int64_t n, int64_t scale, int64_t x, int64_t y)
{
	int64_t dx = (int32_t)kp_rng_game_range(g->rng, ORBIT_RANDOM, (uint32_t)-127, 128) * scale;
	int64_t dy = (int32_t)kp_rng_game_range(g->rng, ORBIT_RANDOM, (uint32_t)-127, 128) * scale;
#define EXT(i) kp_s16v(KP_DAT(g, units, unit_extents, INTERCEPTOR * 4 + (i)))
	if (x + dx < EXT(0) || x + dx >= kp_mod(g->map_w_px, 65536) - EXT(2)) dx = -dx;
	if (y + dy < EXT(1) || y + dy >= kp_mod(g->map_h_px, 65536) - EXT(3)) dy = -dy;
#undef EXT
	x = kp_s16v(x + dx);
	y = kp_s16v(y + dy);
	kp_clamp_to_map(g, INTERCEPTOR, &x, &y);
	kp_request_move_to_point(g, n, kp_s16v(x), kp_s16v(y));
}

/* InterceptorKeepTarget 0x41cb20: 0 (and the return) when the target is gone or too far from the parent. */
static int keep_target(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t t = u->order_unit[n], p, range;
	int32_t s;
	if ((t == 0 || kp_band(u->status[t], STATUS_INVINCIBLE) != 0) && !kp_try_attack_auto_target(g, n)) {
		kp_issue_order(g, n, ORDER_RETURN);
		return 0;
	}
	t = u->order_unit[n];
	s = kp_unit_sprite(g, t);
	u->order_x[n] = kp_s16v(g->w->spr.x[s]);
	u->order_y[n] = kp_s16v(g->w->spr.y[s]);
	p = parent(g)[n];
	range = (kp_seek_range(g, p != 0 ? p : n) + 2) * 32;
	if (p != 0 && !kp_within_range_moving(g, p, range, t)) {
		kp_issue_order(g, n, ORDER_RETURN);
		return 0;
	}
	return 1;
}

/* Order_InterceptorAttack 0x41c4e0 (order 64): launch, orbit the target and fire; back at a quarter of the shields. */
void kp_order_interceptor_attack(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	if (parent(g)[n] != 0 && kp_s32v(u->shields[n]) < kp_sarv(KP_DAT(g, units, shield_amount, INTERCEPTOR) * 256, 2)) {
		kp_issue_order(g, n, ORDER_RETURN);
		return;
	}
	if (!keep_target(g, n)) return;
	switch (u->order_state[n]) {
	case 0:
		if (parent(g)[n] == 0) {
			kp_unit_kill(g, n);
			return;
		}
		kp_play_unit_sound(g, LAUNCH_SOUND, n);
		orbit(g, n, 3, kp_s16v(u->order_x[n]), kp_s16v(u->order_y[n]));
		u->order_timer[n] = ORBIT_TIMER;
		u->order_state[n] = 1;
		/* fall through */
	case 1:
		if (u->order_timer[n] != 0) return;
		g->w->spr.elevation[kp_unit_sprite(g, n)] += 2;
		u->order_state[n] = 3;
		/* fall through */
	case 3:
		if (kp_target_in_range(g, n, 0)) kp_start_attack_animation(g, n, FIRE_ANIMATION);
		if (kp_within_range_moving(g, n, ORBIT_REACH, 0)) {
			orbit(g, n, 1, kp_s16v(u->order_x[n]), kp_s16v(u->order_y[n]));
			u->order_state[n] = 4;
			return;
		}
		kp_request_move_to_point(g, n, kp_s16v(u->order_x[n]), kp_s16v(u->order_y[n]));
		return;
	case 4:
		if (!kp_within_distance(g, n, ORBIT_REACH, kp_s16v(u->move_x[n]), kp_s16v(u->move_y[n]))) return;
		orbit(g, n, 2, kp_s16v(u->order_x[n]), kp_s16v(u->order_y[n]));
		u->order_state[n] = 5;
		return;
	case 5:
		if (!kp_within_distance(g, n, ORBIT_REACH, kp_s16v(u->move_x[n]), kp_s16v(u->move_y[n]))) return;
		if (kp_request_move_to_point(g, n, kp_s16v(u->order_x[n]), kp_s16v(u->order_y[n]))) u->order_state[n] = 3;
		return;
	default:
		return;
	}
}

/* Order_InterceptorReturn 0x41c450 (order 69): back down into the parent's hangar. */
void kp_order_interceptor_return(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t p = parent(g)[n], px, py;
	int32_t ps;
	if (p == 0) {
		kp_unit_kill(g, n);
		return;
	}
	ps = kp_unit_sprite(g, p);
	px = kp_s16v(g->w->spr.x[ps]);
	py = kp_s16v(g->w->spr.y[ps]);
	if (u->order_state[n] == 0 && kp_within_distance(g, n, RETURN_LOWER, px, py)) {
		u->order_state[n] = 1;
		if (u->type[n] == INTERCEPTOR) g->w->spr.elevation[kp_unit_sprite(g, n)] = g->w->spr.elevation[ps] - 2;
	}
	if (!kp_within_distance(g, n, RETURN_DOCK, px, py)) {
		kp_request_move_to_point(g, n, px, py);
		return;
	}
	dock(g, p, n);
	kp_issue_order(g, n, ORDER_NOTHING);
	/* the interface refresh 0x4a7ed0 is the presentation's */
}

/* TrainFighter 0x41c630 (secondary order 63) of a Carrier: with nothing queued, the first damaged Interceptor inside
 * gets half a hit point per step. */
void kp_order_train_fighter(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t t = u->type[n], c, entry;
	if (t != CARRIER && t != GANTRITHOR && t != REAVER && t != WARBRINGER) return;
	switch (u->secondary_state[n]) {
	case 0:
	case 1: {
		int64_t queue[5];
		queue[0] = u->queue0[n];
		queue[1] = u->queue1[n];
		queue[2] = u->queue2[n];
		queue[3] = u->queue3[n];
		queue[4] = u->queue4[n];
		entry = kp_band(queue[kp_mod(u->queue_slot[n], 5)], 0xffff);
		if (entry == NO_TYPE) {
			u->secondary_state[n] = 3;
			u->build_unit[n] = 0;
			return;
		}
		/* TODO: training a paid fighter (0x41bc30, 0x41b290, 0x41a280) comes with the economy's queues (plan phase 6) */
		kp_game_fail(g, "carrier: training a fighter (0x41bc30)");
		return;
	}
	case 2:
		kp_game_fail(g, "carrier: training a fighter (0x41b290, 0x41a280)");
		return;
	case 3:
		if (reaver(t)) {
			u->secondary_state[n] = 4;
			return;
		}
		for (c = inside(g)[n]; c != 0; c = next(g)[c]) {
			if (kp_s32v(u->hp[c]) < KP_DAT(g, units, hit_points, INTERCEPTOR)) {
				kp_set_unit_hp(g, c, u->hp[c] + REPAIR); /* AddUnitHp 0x41b900 */
				return;
			}
		}
		return;
	default:
		return;
	}
}

#define SCARAB_LIFE 90
#define SCARAB_ARM 7
#define SCARAB_CHASE 256
#define STATUS_NO_COLLIDE 0x200000
#define STATUS_EXPIRED 0x80000000LL
#define SIGNAL_DONE 0x1
#define ANIM_SPECIAL1 13

/* OrderScarabAttack 0x41c950 (order 65): the Scarab chases its target and bursts within half its inner splash. */
void kp_order_scarab_attack(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t t = u->order_unit[n];
	int hit;
	switch (u->order_state[n]) {
	case 0:
		if (u->remove_timer[n] == 0 || u->remove_timer[n] > SCARAB_LIFE) u->remove_timer[n] = SCARAB_LIFE;
		u->status[n] = kp_bor(u->status[n], STATUS_NO_COLLIDE);
		u->order_timer[n] = SCARAB_ARM;
		if (t != 0) {
			kp_set_move_target_unit(g, n, t);
			u->order_state[n] = 2;
		} else if (kp_request_move_to_point(g, n, kp_s16v(u->order_x[n]), kp_s16v(u->order_y[n]))) {
			u->order_state[n] = 2;
		}
		return;
	case 2:
		if (u->order_timer[n] == 0) {
			u->status[n] = kp_band(u->status[n], kp_bnot(STATUS_NO_COLLIDE));
			u->order_state[n] = 3;
		}
		/* fall through */
	case 3:
		t = u->order_unit[n];
		if (t != 0) {
			if (kp_unit_box_within(g, n, SCARAB_CHASE, t) && kp_band(u->movement_flags[t], MOVING) != 0) {
				kp_pursue(g, n, t);
			} else {
				kp_move_toward_target(g, n, t);
			}
			hit = kp_unit_box_within(g, n,
				kp_shrv(KP_DAT(g, weapons, inner_splash_range, KP_DAT(g, units, ground_weapon, u->type[n])), 1), u->order_unit[n]);
		} else {
			hit = kp_move_arrival(g, n) != 0;
		}
		if (hit) {
			int32_t s = kp_unit_sprite(g, n);
			u->order_x[n] = kp_s16v(g->w->spr.x[s]);
			u->order_y[n] = kp_s16v(g->w->spr.y[s]);
			kp_sprite_play(g->w, s, ANIM_SPECIAL1, 1);
			u->order_state[n] = 6;
		}
		return;
	case 6:
		if (kp_band(u->order_signal[n], SIGNAL_DONE) == 0) return;
		u->status[n] = kp_bor(u->status[n], STATUS_EXPIRED);
		kp_unit_kill(g, n);
		return;
	default:
		return;
	}
}
