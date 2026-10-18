/* Order node pool and unit order queues (kp_game.h). */
#include "kp_game_internal.h"

#define STATUS_NOT_INTERRUPTIBLE 0x1000
#define NO_HIGHLIGHT 0xffff
#define DEATH 0
#define STATUS_BURROWED 0x10
#define STATUS_ORDER_LOCK 0x1000
#define STATUS_LOCKS 0x3000
#define STATUS_ACTIVATION_CLEAR 0xf7ffeff7LL
#define SUBUNIT 0x10
#define STRING_QUEUE_FULL 0x368

void kp_orders_init(KpGame *g)
{
	KpOrders *o = &g->orders;
	int64_t i;
	for (i = 0; i < KP_ORDER_NODES; i++) {
		o->prev[i] = o->next[i] = KP_NONE;
		o->order[i] = o->type[i] = o->x[i] = o->y[i] = o->unit[i] = 0;
	}
	o->free.head = o->free.tail = KP_NONE;
	for (i = 0; i < KP_ORDER_NODES; i++) kp_list64_insert_after_head(o->prev, o->next, &o->free, i, KP_NONE);
	o->allocated = 0;
}

int64_t kp_order_node(KpGame *g, int64_t i)
{
	if (i < 0 || i >= KP_ORDER_NODES) kp_game_fail(g, "orders: node %lld out of range", (long long)i);
	return i;
}

/* AllocateOrderNode 0x413c80: the free head with its payload written, or KP_NONE. */
static int64_t allocate(KpGame *g, int64_t order, int64_t fow, int64_t x, int64_t y, int64_t target)
{
	KpOrders *o = &g->orders;
	int64_t i = o->free.head;
	if (i == KP_NONE) return KP_NONE;
	o->order[i] = kp_mod(order, 256);
	o->type[i] = kp_mod(fow, 65536);
	o->x[i] = kp_s16v(x);
	o->y[i] = kp_s16v(y);
	o->unit[i] = target;
	kp_list64_unlink(o->prev, o->next, &o->free, i, KP_NONE);
	o->allocated++;
	return i;
}

/* FreeOrderNode 0x413d50: after the free head; the payload stays. */
static void release(KpGame *g, int64_t i)
{
	KpOrders *o = &g->orders;
	o->allocated--;
	kp_list64_insert_after_head(o->prev, o->next, &o->free, i, KP_NONE);
}

static KpList queue(KpGame *g, int64_t n)
{
	KpList q;
	q.head = (int32_t)g->u.queue_head[n];
	q.tail = (int32_t)g->u.queue_tail[n];
	return q;
}

static void store(KpGame *g, int64_t n, KpList q)
{
	g->u.queue_head[n] = q.head;
	g->u.queue_tail[n] = q.tail;
}

static int highlighted(KpGame *g, int64_t order) { return KP_DAT(g, orders, highlight_icon, order) != NO_HIGHLIGHT; }

/* The decrement of the highlighted count with its 0xff -> 0 fix-up. */
static void unhighlight(KpGame *g, int64_t n, int64_t order)
{
	KpUnits *u = &g->u;
	if (highlighted(g, order)) u->queue_count[n] = kp_mod(u->queue_count[n] - 1, 256);
	if (u->queue_count[n] == 0xff) u->queue_count[n] = 0;
}

static void allocation_failed(KpGame *g, int64_t n) { kp_report_error(g, g->u.owner[n], STRING_QUEUE_FULL); }

static void push_tail(KpGame *g, int64_t n, int64_t i)
{
	KpOrders *o = &g->orders;
	KpList q = queue(g, n);
	if (q.tail != KP_NONE) kp_list64_insert_after(o->prev, o->next, &q, q.tail, i, KP_NONE);
	else kp_list64_insert_after_head(o->prev, o->next, &q, i, KP_NONE);
	store(g, n, q);
}

void kp_order_queue(KpGame *g, int64_t n, int64_t order, int64_t x, int64_t y, int64_t target, int64_t fow)
{
	int64_t i = allocate(g, order, fow, x, y, target);
	if (i == KP_NONE) {
		allocation_failed(g, n);
		return;
	}
	if (highlighted(g, order)) g->u.queue_count[n] = kp_mod(g->u.queue_count[n] + 1, 256);
	push_tail(g, n, i);
}

void kp_order_append(KpGame *g, int64_t n, int64_t order, int64_t x, int64_t y, int64_t target, int64_t fow,
	int clear_others)
{
	KpUnits *u = &g->u;
	KpOrders *o = &g->orders;
	KpList q;
	int64_t i;
	if (u->order[n] == DEATH) return;
	q = queue(g, n);
	while (q.tail != KP_NONE
		&& ((clear_others && KP_DAT(g, orders, interruptable, o->order[q.tail]) != 0) || o->order[q.tail] == order)) {
		int64_t tail = q.tail;
		unhighlight(g, n, o->order[tail]);
		kp_list64_unlink(o->prev, o->next, &q, tail, KP_NONE);
		release(g, tail);
	}
	store(g, n, q);
	if (order == KP_ORD_CLOAK) {
		if (q.head == KP_NONE) {
			kp_order_insert_after(g, n, u->order[n], u->order_x[n], u->order_y[n], u->order_unit[n], u->order_type[n], KP_NONE);
		} else {
			kp_order_insert_before(g, n, u->order[n], u->order_x[n], u->order_y[n], u->order_unit[n], u->order_type[n],
				q.head);
		}
		kp_order_insert_before(g, n, KP_ORD_CLOAK, x, y, target, KP_TYPE_NONE, u->queue_head[n]);
		u->status[n] = kp_band(u->status[n], kp_bnot(STATUS_ORDER_LOCK));
		kp_order_done(g, n);
		return;
	}
	i = allocate(g, order, fow, x, y, target);
	if (i == KP_NONE) {
		allocation_failed(g, n);
		return;
	}
	if (highlighted(g, order)) u->queue_count[n] = kp_mod(u->queue_count[n] + 1, 256);
	push_tail(g, n, i);
}

void kp_order_insert_after(KpGame *g, int64_t n, int64_t order, int64_t x, int64_t y, int64_t target, int64_t fow,
	int64_t anchor)
{
	KpOrders *o = &g->orders;
	KpList q;
	int64_t i = allocate(g, order, fow, x, y, target);
	if (i == KP_NONE) {
		allocation_failed(g, n);
		return;
	}
	if (highlighted(g, order)) g->u.queue_count[n] = kp_mod(g->u.queue_count[n] + 1, 256);
	q = queue(g, n);
	if (anchor == KP_NONE && q.tail == KP_NONE) {
		kp_list64_insert_after_head(o->prev, o->next, &q, i, KP_NONE);
	} else {
		kp_list64_insert_after(o->prev, o->next, &q, kp_order_node(g, anchor == KP_NONE ? q.tail : anchor), i, KP_NONE);
	}
	store(g, n, q);
}

void kp_order_insert_before(KpGame *g, int64_t n, int64_t order, int64_t x, int64_t y, int64_t target, int64_t fow,
	int64_t anchor)
{
	KpOrders *o = &g->orders;
	KpList q;
	int64_t i;
	if (g->u.ai[n] != 0 && g->u.queue_count[n] >= 9) return;
	i = allocate(g, order, fow, x, y, target);
	if (i == KP_NONE) {
		allocation_failed(g, n);
		return;
	}
	if (highlighted(g, order)) g->u.queue_count[n] = kp_mod(g->u.queue_count[n] + 1, 256);
	q = queue(g, n);
	kp_list64_insert_before(o->prev, o->next, &q, kp_order_node(g, anchor), i, KP_NONE);
	store(g, n, q);
}

/* PrependOrNewOrder 0x429b20: the order before the queue's head, or alone in an empty queue. */
void kp_order_prepend(KpGame *g, int64_t n, int64_t order)
{
	int64_t head = g->u.queue_head[n];
	if (head != KP_NONE) kp_order_insert_before(g, n, order, 0, 0, 0, KP_TYPE_NONE, head);
	else kp_order_queue(g, n, order, 0, 0, 0, KP_TYPE_NONE);
}

/* PrependOrNewOrderAt 0x429c40: the order with a point and a target before the queue's head, or alone. */
void kp_order_prepend_at(KpGame *g, int64_t n, int64_t order, int64_t x, int64_t y, int64_t target)
{
	int64_t head = g->u.queue_head[n];
	if (head != KP_NONE) kp_order_insert_before(g, n, order, x, y, target, KP_TYPE_NONE, head);
	else kp_order_queue(g, n, order, x, y, target, KP_TYPE_NONE);
}

/* SetOrderTargetUnit 0x417980. */
void kp_set_order_unit(KpGame *g, int64_t n, int64_t t)
{
	KpUnits *u = &g->u;
	u->order_unit[n] = t;
	if (t != 0) {
		int32_t s = kp_unit_sprite(g, t);
		u->order_x[n] = kp_s16v(g->w->spr.x[s]);
		u->order_y[n] = kp_s16v(g->w->spr.y[s]);
	} else {
		u->order_x[n] = 0;
		u->order_y[n] = 0;
	}
}

/* PrependOrNewOrderOnUnit 0x429bc0: the same at the target's sprite position. */
void kp_order_prepend_target(KpGame *g, int64_t n, int64_t order, int64_t target)
{
	int32_t s = kp_unit_sprite(g, target);
	kp_order_prepend_at(g, n, order, g->w->spr.x[s], g->w->spr.y[s], target);
}

void kp_order_remove(KpGame *g, int64_t n, int64_t i)
{
	KpOrders *o = &g->orders;
	KpList q;
	kp_order_node(g, i);
	unhighlight(g, n, o->order[i]);
	q = queue(g, n);
	kp_list64_unlink(o->prev, o->next, &q, i, KP_NONE);
	store(g, n, q);
	release(g, i);
}

void kp_order_clear_queue(KpGame *g, int64_t n)
{
	KpOrders *o = &g->orders;
	KpList q = queue(g, n);
	while (q.head != KP_NONE) {
		int64_t head = kp_order_node(g, q.head);
		unhighlight(g, n, o->order[head]);
		kp_list64_unlink(o->prev, o->next, &q, head, KP_NONE);
		release(g, head);
	}
	store(g, n, q);
}

void kp_set_order(KpGame *g, int64_t n, int64_t order)
{
	KpUnits *u = &g->u;
	u->order[n] = order;
	u->order_x[n] = u->order_y[n] = u->order_state[n] = u->order_unit[n] = 0;
	u->order_type[n] = KP_TYPE_NONE;
}

void kp_set_order_at(KpGame *g, int64_t n, int64_t order, int64_t x, int64_t y)
{
	KpUnits *u = &g->u;
	u->order[n] = order;
	u->order_x[n] = x;
	u->order_y[n] = y;
	u->order_unit[n] = u->order_state[n] = 0;
	u->order_type[n] = KP_TYPE_NONE;
}

void kp_set_order_fow(KpGame *g, int64_t n, int64_t order, int64_t fow, int64_t x, int64_t y)
{
	KpUnits *u = &g->u;
	u->order[n] = order;
	u->order_type[n] = fow;
	u->order_x[n] = x;
	u->order_unit[n] = 0;
	u->order_y[n] = y;
	u->order_state[n] = 0;
}

void kp_set_order_target(KpGame *g, int64_t n, int64_t order, int64_t target)
{
	KpUnits *u = &g->u;
	u->order[n] = order;
	u->order_unit[n] = target;
	if (target != 0) {
		int32_t s = kp_unit_sprite(g, target);
		u->order_x[n] = kp_s16v(g->w->spr.x[s]);
		u->order_y[n] = kp_s16v(g->w->spr.y[s]);
	} else {
		u->order_x[n] = u->order_y[n] = 0;
	}
	u->order_state[n] = 0;
	u->order_type[n] = KP_TYPE_NONE;
}

/* SetSecondaryOrderForce 0x42a180. */
void kp_set_secondary_order_force(KpGame *g, int64_t n, int64_t order)
{
	KpUnits *u = &g->u;
	u->secondary_order[n] = order;
	u->secondary_x[n] = u->secondary_y[n] = u->build_unit[n] = u->secondary_state[n] = 0;
}

void kp_set_secondary_order(KpGame *g, int64_t n, int64_t order)
{
	if (g->u.secondary_order[n] != order) kp_set_secondary_order_force(g, n, order);
}

void kp_order_activate(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	KpOrders *o = &g->orders;
	int64_t head = u->queue_head[n], order, fow, x, y, target, status, t, sub, st;
	KpList q;
	if (u->ai[n] != 0 && head != KP_NONE && kp_band(u->status[n], STATUS_BURROWED) != 0) {
		int64_t ord = o->order[kp_order_node(g, head)];
		t = u->type[n];
		if (ord != DEATH && ord != KP_ORD_UNBURROWING && ord != KP_ORD_BURROWED && ord != KP_ORD_COMPUTER_AI && t != KP_TYPE_SPIDER_MINE
			&& (t != KP_TYPE_LURKER || (ord != KP_ORD_GUARD && ord != KP_ORD_ATTACK_FIXED_RANGE))) {
			int32_t s = kp_unit_sprite(g, n);
			kp_order_insert_before(g, n, KP_ORD_UNBURROWING, kp_s16v(g->w->spr.x[s]), kp_s16v(g->w->spr.y[s]), 0, KP_TYPE_NONE, head);
			u->status[n] = kp_band(u->status[n], kp_bnot(STATUS_ORDER_LOCK));
			kp_order_done(g, n);
			return;
		}
	}
	if (head == KP_NONE) return;
	kp_order_node(g, head);
	if (kp_band(u->status[n], STATUS_LOCKS) != 0 && o->order[head] != DEATH) return;
	unhighlight(g, n, o->order[head]);
	q = queue(g, n);
	kp_list64_unlink(o->prev, o->next, &q, head, KP_NONE);
	store(g, n, q);
	release(g, head);
	order = o->order[head];
	fow = o->type[head];
	x = o->x[head];
	y = o->y[head];
	target = o->unit[head];
	u->queue_timer[n] = 0;
	u->user_action[n] = kp_band(u->user_action[n], 0xfe);
	u->recent_order_timer[n] = 0;
	status = kp_band(u->status[n], STATUS_ACTIVATION_CLEAR);
	if (KP_DAT(g, orders, interruptable, order) == 0) status = kp_bor(status, STATUS_ORDER_LOCK);
	u->status[n] = status;
	if (target == 0) {
		if (fow == KP_TYPE_NONE) kp_set_order_at(g, n, order, x, y);
		else kp_set_order_fow(g, n, order, fow, x, y);
	} else {
		kp_set_order_target(g, n, order, target);
	}
	if (u->ai[n] == 0) u->auto_target[n] = 0;
	kp_reset_to_idle_animation(g, n);
	t = u->type[n];
	sub = u->subunit[n];
	if (kp_band(KP_DAT(g, units, special_ability_flags, t), SUBUNIT) != 0 || sub == 0
		|| kp_band(KP_DAT(g, units, special_ability_flags, u->type[sub]), SUBUNIT) == 0) {
		return;
	}
	st = u->type[sub];
	if (order == KP_DAT(g, units, return_to_idle, t)) order = KP_DAT(g, units, return_to_idle, st);
	else if (order == KP_DAT(g, units, attack_unit, t)) order = KP_DAT(g, units, attack_unit, st);
	else if (order == KP_DAT(g, units, attack_move, t)) order = KP_DAT(g, units, attack_move, st);
	else if (KP_DAT(g, orders, changes_subunit_order, order) == 0) return;
	if (target != 0) kp_issue_order_packed(g, sub, order, x, y, target);
	else kp_issue_order_fow(g, sub, order, fow, x, y);
}

void kp_order_done(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t order;
	if (u->queue_head[n] != KP_NONE) {
		u->user_action[n] = kp_bor(u->user_action[n], 1);
		kp_order_activate(g, n);
		return;
	}
	order = u->ai[n] == 0 ? KP_DAT(g, units, return_to_idle, u->type[n]) : KP_ORD_COMPUTER_AI;
	u->user_action[n] = kp_bor(u->user_action[n], 1);
	kp_order_append(g, n, order, 0, 0, 0, KP_TYPE_NONE, 1);
	kp_order_activate(g, n);
}

/* SetOrderNoTarget 0x4179b0: the order without a point, target or type, from its start. */
void kp_set_order_no_target(KpGame *g, int64_t n, int64_t order)
{
	KpUnits *u = &g->u;
	u->order[n] = order;
	u->order_x[n] = u->order_y[n] = 0;
	u->order_state[n] = 0;
	u->order_unit[n] = 0;
	u->order_type[n] = KP_TYPE_NONE;
}

/* ClearOrderTarget 0x41df90: the unit and its indexed subunit lose their target. */
void kp_clear_order_target(KpGame *g, int64_t n)
{
	int64_t sub = g->u.subunit[n];
	g->u.order_unit[n] = 0;
	if (kp_is_subunit(g, sub)) g->u.order_unit[sub] = 0;
}

/* UnlockAndEndOrder 0x429d00: the order ends and the unit may be interrupted again. */
void kp_order_unlock_done(KpGame *g, int64_t n)
{
	g->u.status[n] = kp_band(g->u.status[n], kp_bnot(STATUS_NOT_INTERRUPTIBLE));
	kp_order_done(g, n);
}

/* KillUnitFlagged 0x425700: the Die order with user action bit 4. */
void kp_kill_flagged(KpGame *g, int64_t n)
{
	g->u.user_action[n] = kp_bor(g->u.user_action[n], 4);
	kp_unit_kill(g, n);
}

void kp_unit_kill(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	kp_drop_powerup(g, n);
	kp_order_clear_queue(g, n);
	u->user_action[n] = kp_bor(u->user_action[n], 1);
	kp_order_append(g, n, DEATH, u->order_x[n], u->order_y[n], 0, KP_TYPE_NONE, 1);
	kp_order_activate(g, n);
	kp_remove_unit_ai(g, n, 1);
}

static void issue(KpGame *g, int64_t n, int64_t order, int64_t x, int64_t y, int64_t target, int64_t fow)
{
	g->u.user_action[n] = kp_bor(g->u.user_action[n], 1);
	kp_order_append(g, n, order, x, y, target, fow, 1);
	kp_order_activate(g, n);
}

void kp_issue_order(KpGame *g, int64_t n, int64_t order) { issue(g, n, order, 0, 0, 0, KP_TYPE_NONE); }

void kp_issue_order_at(KpGame *g, int64_t n, int64_t order, int64_t x, int64_t y) { issue(g, n, order, x, y, 0, KP_TYPE_NONE); }

void kp_issue_order_fow(KpGame *g, int64_t n, int64_t order, int64_t fow, int64_t x, int64_t y)
{
	issue(g, n, order, x, y, 0, fow);
}

void kp_issue_order_target(KpGame *g, int64_t n, int64_t order, int64_t target)
{
	int64_t x = 0, y = 0;
	if (target != 0) {
		int32_t s = kp_unit_sprite(g, target);
		x = g->w->spr.x[s];
		y = g->w->spr.y[s];
	}
	issue(g, n, order, x, y, target, KP_TYPE_NONE);
}

void kp_issue_order_packed(KpGame *g, int64_t n, int64_t order, int64_t x, int64_t y, int64_t target)
{
	issue(g, n, order, x, y, target, KP_TYPE_NONE);
}
