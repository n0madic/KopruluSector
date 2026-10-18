/* Shield Batteries (kp_game.h), ported from the atlas (P10, P11): the recharge test 0x42f310, the orders
 * RechargeShieldsUnit 66 (0x42f640) and RechargeShieldsBattery 67 (0x42f550), the battery's secondary order 68
 * (0x42f770) and one recharge step (0x4300e0). */
#include "kp_game_internal.h"

#define STATUS_COMPLETED 0x1
#define STATUS_GROUNDED_BUILDING 0x2
#define STATUS_CAN_MOVE 0x20000
#define BATTERY_RANGE 128
#define RECHARGE_IMAGE 368
#define RECHARGE_GAIN 0x500
#define RECHARGE_COST 0x280

static int64_t max_shields(KpGame *g, int64_t n) { return KP_DAT(g, units, shield_amount, g->u.type[n]) * 256; }

/* CanRechargeShieldsFrom 0x42f310: unit n may draw shields from battery b now. */
int kp_can_recharge_from(KpGame *g, int64_t n, int64_t b)
{
	KpUnits *u = &g->u;
	int64_t status = u->status[n], head;
	if (u->owner[n] != u->owner[b] || KP_DAT(g, units, shield_enabled, u->type[n]) == 0
		|| kp_band(status, STATUS_COMPLETED) == 0 || kp_band(status, STATUS_CAN_MOVE) == 0
		|| kp_band(status, STATUS_GROUNDED_BUILDING) != 0 || kp_unit_race(g, n) != KP_RACE_PROTOSS
		|| kp_s32v(u->shields[n]) >= max_shields(g, n) || kp_band(u->status[b], STATUS_COMPLETED) == 0
		|| kp_mod(u->energy[b], 65536) == 0 || kp_unit_disabled(g, b) || u->type[n] == KP_TYPE_INTERCEPTOR || u->type[n] == KP_TYPE_SCARAB) {
		return 0;
	}
	if (u->ai[n] == 0) return 1;
	if (u->order[n] == KP_ORD_RECHARGE_SHIELDS_UNIT || u->order[n] == KP_ORD_TRANSPORT_REQUEST) return 0;
	head = u->queue_head[n];
	return head == KP_NONE || g->orders.order[head] != KP_ORD_RECHARGE_SHIELDS_UNIT;
}

/* RechargeShieldsFromBattery 0x4300e0: 5 shields for 2.5 energy, less when fewer are missing or energy is short. */
static void recharge_step(KpGame *g, int64_t n, int64_t b)
{
	KpUnits *u = &g->u;
	int64_t gain = RECHARGE_GAIN, cost = RECHARGE_COST, missing = max_shields(g, n) - kp_s32v(u->shields[n]);
	if (missing < RECHARGE_GAIN) {
		gain = missing;
		cost = missing / 2;
	}
	if (cost > kp_mod(u->energy[b], 65536)) {
		cost = kp_mod(u->energy[b], 65536);
		gain = cost * 2;
	}
	u->shields[n] = kp_s32v(u->shields[n] + gain);
	if (u->shields[n] > max_shields(g, n)) u->shields[n] = max_shields(g, n);
	kp_spend_energy(g, b, cost);
}

static void battery_working(KpGame *g, int64_t b)
{
	if (g->u.secondary_order[b] != KP_ORD_SHIELD_BATTERY) kp_set_secondary_order(g, b, KP_ORD_SHIELD_BATTERY);
}

/* Order_RechargeShieldsUnit 0x42f640 (order 66): the unit walks to the battery and draws shields every step. */
void kp_order_recharge_shields(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t b = u->order_unit[n];
	if (b == 0 || kp_mod(u->energy[b], 65536) == 0 || kp_unit_disabled(g, b)) {
		kp_order_done(g, n);
		return;
	}
	switch (u->order_state[n]) {
	case 0:
		if (kp_request_move_to_unit(g, n, b)) u->order_state[n] = 1;
		return;
	case 1: {
		int64_t a = kp_move_arrival(g, n);
		if (a == 2) {
			kp_order_done(g, n);
			return;
		}
		if (a == 0 && !kp_unit_box_within(g, n, BATTERY_RANGE, b)) return;
		kp_stop_and_face(g, n);
		u->order_state[n] = 2;
	}
		/* fall through */
	case 2: {
		int64_t head = u->queue_head[n];
		battery_working(g, b);
		kp_sprite_image_above_main(g->w, kp_unit_sprite(g, n), (int32_t)(RECHARGE_IMAGE + kp_overlay_size(g, n)), 0, 0, 0);
		/* PrependOrNewOrderDirect 0x42a210 */
		if (head != KP_NONE) kp_order_insert_before(g, n, KP_ORD_RECHARGE_SHIELDS_UNIT_REMOVE_OVERLAY, 0, 0, 0, KP_TYPE_NONE, head);
		else kp_order_queue(g, n, KP_ORD_RECHARGE_SHIELDS_UNIT_REMOVE_OVERLAY, 0, 0, 0, KP_TYPE_NONE);
		u->order_state[n] = 3;
	}
		/* fall through */
	case 3:
		if (u->order_unit[b] == 0) u->order_unit[b] = n;
		recharge_step(g, n, b);
		if (kp_s32v(u->shields[n]) < max_shields(g, n) && kp_mod(u->energy[b], 65536) != 0) return;
		if (u->order_unit[b] == n) u->order_unit[b] = 0;
		kp_order_done(g, n);
		return;
	default:
		return;
	}
}

/* Order_RechargeShieldsBattery 0x42f550 (order 67): the battery calls its target, or every unit around the point. */
void kp_order_battery_recharge(KpGame *g, int64_t b)
{
	KpUnits *u = &g->u;
	int64_t t = u->order_unit[b], x, y, *found;
	int count, k;
	battery_working(g, b);
	if (t != 0 && kp_can_recharge_from(g, t, b)) {
		kp_issue_order_target(g, t, KP_ORD_FOLLOW, b);
		kp_order_done(g, b);
		return;
	}
	x = kp_s16v(u->order_x[b]);
	y = kp_s16v(u->order_y[b]);
	found = kp_query_rect(g, kp_s16v(x - BATTERY_RANGE), kp_s16v(y - BATTERY_RANGE), kp_s16v(x + BATTERY_RANGE),
		kp_s16v(y + BATTERY_RANGE), &count);
	for (k = 0; k < count; k++) {
		if (kp_can_recharge_from(g, found[k], b)) kp_issue_order_target(g, found[k], KP_ORD_FOLLOW, b);
	}
	kp_query_release(g, found);
	kp_order_done(g, b);
}

/* SecondaryOrder_ShieldBattery 0x42f770 (secondary order 68): the battery glows while a unit draws from it. */
void kp_order_battery_glow(KpGame *g, int64_t b)
{
	KpUnits *u = &g->u;
	int64_t t = u->order_unit[b];
	switch (u->secondary_state[b]) {
	case 0:
		if (t == 0) return;
		kp_sprite_play(g->w, kp_unit_sprite(g, b), KP_ANIM_IS_WORKING, 1);
		u->secondary_state[b] = 1;
		return;
	case 1:
		if (t != 0 && u->order[t] == KP_ORD_RECHARGE_SHIELDS_UNIT) return;
		u->order_unit[b] = 0;
		u->secondary_state[b] = 2;
		return;
	case 2:
		if (t != 0) {
			u->secondary_state[b] = 1;
			return;
		}
		kp_sprite_play(g->w, kp_unit_sprite(g, b), KP_ANIM_WORKING_TO_IDLE, 1);
		u->secondary_state[b] = 0;
		return;
	}
}


/* Order_RemoveRechargeOverlay 0x42f7e0 (order 91): the recharge image goes and the unit idles. */
void kp_order_recharge_done(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	KpImages *w = g->w;
	int32_t i = kp_sprite_find_image(w, kp_unit_sprite(g, n), RECHARGE_IMAGE, RECHARGE_IMAGE + 2);
	if (i != KP_NONE) kp_image_release(w, i);
	if (u->queue_head[n] == KP_NONE) kp_order_queue(g, n, KP_DAT(g, units, return_to_idle, u->type[n]), 0, 0, 0, KP_TYPE_NONE);
	kp_order_unlock_done(g, n);
}
