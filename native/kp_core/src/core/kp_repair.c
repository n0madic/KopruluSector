/* An SCV's Repair (kp_game.h), ported from the atlas (P06): the order Repair 34 (0x41ad30), the price of a repair's
 * instalment and its period (ComputeRepairPrice 0x41b910), the stock check at the start (0x41ba50) and the payment of
 * each instalment (PayRepairInstalment 0x41bab0). The repairer's union WORD +0xcc counts down to the next payment. */
#include "kp_game_internal.h"

#define STATUS_COMPLETED 0x1
#define STATUS_LOADED 0x40
#define STATUS_CAN_MOVE 0x20000
#define DAT_MECHANICAL 0x40000000
#define RACE_TERRAN 1
#define ORDER_CONSTRUCTING 33
#define ORDER_LIFTING_OFF 74
#define ANIM_GND_ATTK_TO_IDLE 8
#define ANIM_WALKING_TO_IDLE 12
#define ANIM_ALMOST_BUILT 15
#define REPAIR_RANGE 5
#define U_TICK 0xcc
#define NO_PERIOD 0xffff

/* ComputeRepairPrice 0x41b910: c is the smaller of the unit's two prices (the larger when one is 0); the instalments
 * come every 3H / (G c) steps, a single unit of the cheaper resource and the price ratio of the other, or for a
 * period below one step a share of both prices every step. */
static void repair_price(KpGame *g, int64_t t, int64_t *minerals, int64_t *gas, int64_t *period)
{
	int64_t type = g->u.type[t], m = kp_band(KP_DAT(g, units, mineral_cost, type), 0xffff);
	int64_t v = kp_band(KP_DAT(g, units, vespene_cost, type), 0xffff), c = v, hp3, gain;
	if (m == 0 || v == 0) {
		if (v < m) c = m;
	} else if (m < v) {
		c = m;
	}
	if (c == 0) {
		*minerals = *gas = 0;
		*period = NO_PERIOD;
		return;
	}
	hp3 = kp_s32v(KP_DAT(g, units, hit_points, type) * 3);
	gain = kp_build_hp_gain(g, t);
	*period = kp_band(kp_idiv64(g, hp3, kp_s32v(gain * c)), 0xffff);
	if (*period == 0) {
		*period = 1;
		*minerals = m == 0 ? 0 : kp_idiv64(g, kp_s32v(gain * m), hp3);
		*gas = v == 0 ? 0 : kp_idiv64(g, kp_s32v(gain * v), hp3);
		if (m != 0 && *minerals < 1) *minerals = 1;
		if (v != 0 && *gas < 1) *gas = 1;
	} else if (c == m) {
		*minerals = 1;
		*gas = kp_udiv64(g, v, m);
	} else {
		*minerals = kp_udiv64(g, m, v);
		*gas = 1;
	}
}

/* PayRepairInstalment 0x41bab0: the next instalment from the repairer's owner, the countdown to the one after. */
static int pay_instalment(KpGame *g, int64_t n, int64_t t)
{
	int64_t p = g->u.owner[n], m, v, period;
	repair_price(g, t, &m, &v, &period);
	if (!kp_check_stock_of(g, p, m, v, 1)) return 0;
	kp_union_set(g, n, U_TICK, 2, period);
	g->players.minerals[p] = kp_u32v(g->players.minerals[p] - m);
	g->players.gas[p] = kp_u32v(g->players.gas[p] - v);
	return 1;
}

static int full_hp(KpGame *g, int64_t t)
{
	return kp_s32v(KP_DAT(g, units, hit_points, g->u.type[t])) <= kp_s32v(g->u.hp[t]);
}

static void end_repair(KpGame *g, int64_t n)
{
	kp_sprite_play(g->w, kp_unit_sprite(g, n), ANIM_WALKING_TO_IDLE, 1);
	kp_order_done(g, n);
}

/* The abort path: the target forgets its repairer unless it is loaded. */
static void abort_repair(KpGame *g, int64_t n, int64_t t)
{
	end_repair(g, n);
	if (t != 0 && kp_band(g->u.status[t], STATUS_LOADED) == 0) g->u.connected[t] = 0;
}

static void face_target(KpGame *g, int64_t n, int64_t t)
{
	int32_t s = kp_unit_sprite(g, t);
	kp_flingy_set_target_waypoint(g, n, kp_s16v(g->w->spr.x[s]), kp_s16v(g->w->spr.y[s])); /* 0x40cbd0 */
	kp_set_order_unit(g, n, t);
}

static int in_reach(KpGame *g, int64_t n, int64_t t) { return kp_unit_distance(g, n, t) <= REPAIR_RANGE; } /* 0x4229a0 */

/* OrderRepair 0x41ad30 (order 34): the SCV walks to the target, turns to it and repairs it an instalment at a time
 * from the second state on. */
void kp_order_repair(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t t = u->order_unit[n], m, v, period, tick;
	if (t == 0 || full_hp(g, t) || u->stasis_timer[t] != 0
		/* a computer's SCV repairs a unit that can move only when the unit targets it (0x41ad72) */
		|| (u->ai[n] != 0 && kp_band(u->status[t], STATUS_CAN_MOVE) != 0 && u->order_unit[t] != n)
		|| kp_band(u->status[t], STATUS_LOADED) != 0
		|| kp_band(KP_DAT(g, units, special_ability_flags, u->type[t]), DAT_MECHANICAL) == 0
		|| kp_band(u->status[t], STATUS_COMPLETED) == 0) {
		abort_repair(g, n, t);
		return;
	}
	if (kp_unit_race(g, t) != RACE_TERRAN) {
		end_repair(g, n);
		return;
	}
	switch (u->order_state[n]) {
	case 0:
		repair_price(g, t, &m, &v, &period);
		kp_check_stock_of(g, u->owner[n], m, v, 1); /* only the feedback (0x41ba50) */
		u->order_state[n] = 1;
		kp_union_set(g, n, U_TICK, 2, 0);
		/* fall through */
	case 1:
		if (kp_band(u->status[t], STATUS_COMPLETED) == 0) {
			/* unreachable after the checks above */
			kp_issue_order_target(g, n, ORDER_CONSTRUCTING, t);
			kp_placement_recheck(g); /* 0x47d070, 0x47db60, 0x47d300 */
			return;
		}
		if (!kp_request_move_to_unit(g, n, t)) return; /* 0x43a0a0 */
		kp_set_order_unit(g, n, t);
		u->order_state[n] = 6;
		return;
	case 6:
		if (kp_move_arrival(g, n) == 0) {
			kp_move_toward_target(g, n, t);
			return;
		}
		if (!in_reach(g, n, t)) {
			u->order_state[n] = 1;
			return;
		}
		face_target(g, n, t);
		u->order_state[n] = 7;
		break;
	case 7:
		if (!in_reach(g, n, t)) {
			u->order_state[n] = 1;
			return;
		}
		face_target(g, n, t);
		if (!kp_facing_waypoint(g, n)) break;
		kp_sprite_play(g->w, kp_unit_sprite(g, n), ANIM_ALMOST_BUILT, 1);
		u->order_state[n] = 8;
		break;
	case 8:
		face_target(g, n, t);
		if (!in_reach(g, n, t) || u->order[t] == ORDER_LIFTING_OFF) {
			kp_sprite_play(g->w, kp_unit_sprite(g, n), ANIM_GND_ATTK_TO_IDLE, 1);
			u->order_state[n] = 1;
		}
		break;
	}
	tick = kp_union_get(g, n, U_TICK, 2);
	kp_union_set(g, n, U_TICK, 2, tick - 1);
	if (tick == 0 && !pay_instalment(g, n, t)) {
		end_repair(g, n);
		return;
	}
	kp_set_unit_hp(g, t, kp_s32v(kp_build_hp_gain(g, t) + u->hp[t])); /* 0x41bb50 */
	if (full_hp(g, t)) abort_repair(g, n, t);
}
