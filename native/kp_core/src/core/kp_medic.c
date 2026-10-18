/* The Medic's Heal (kp_game.h), ported from the atlas (P11, P12): the heal target search, one frame of healing and the
 * Medic orders 175-179. */
#include "kp_game_internal.h"

#define HEAL_SEARCH_RANGE 160
#define HEAL_RANGE 30
#define HEAL_FAR 99999999
#define STATUS_COMPLETED 0x1
#define SPRITE_HIDDEN 0x20
#define HEAL_PER_FRAME 200 /* raw hit points (1/256) */
#define SEEK_PERIOD 15
#define STATUS_AIR 0x4
#define STATUS_HALLUCINATION 0x40000000
#define DAT_BUILDING 0x1
#define DAT_ORGANIC 0x10000
#define SIGNAL_HEALED 0x1

enum { HEAL_NONE, HEAL_DONE, HEAL_NO_ENERGY, HEAL_OUT_OF_RANGE };

static int64_t flags(KpGame *g, int64_t t) { return KP_DAT(g, units, special_ability_flags, t); }

/* CanMedicHealTarget 0x437060. */
static int can_heal(KpGame *g, int64_t medic, int64_t t)
{
	KpUnits *u = &g->u;
	if (t == 0 || t == medic || kp_band(u->status[medic], STATUS_HALLUCINATION) != 0) return 0;
	if (kp_band(flags(g, u->type[t]), DAT_ORGANIC) == 0 || kp_band(flags(g, u->type[t]), DAT_BUILDING) != 0) return 0;
	if (kp_band(u->status[t], STATUS_AIR) != 0 || kp_is_enemy(g, medic, t)) return 0;
	if (u->owner[medic] >= 8 || u->owner[t] >= 8) return 0;
	if (kp_s32v(u->hp[t]) >= kp_s32v(KP_DAT(g, units, hit_points, u->type[t]))) return 0;
	return !kp_unit_disabled(g, t);
}

/* The heal search callback 0x4332c0. */
static int heal_candidate(KpGame *g, int64_t c, int64_t medic)
{
	KpUnits *u = &g->u;
	return u->being_healed[c] == 0 && kp_band(u->status[c], STATUS_HALLUCINATION) == 0 && can_heal(g, medic, c);
}

/* FindHealTarget 0x431ce0: a computer's Medic takes the nearest organic unit of its player that it can heal and
 * that no Medic of the player (from the first one that could be healed on) is healing or about to. */
int64_t kp_find_heal_target(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t *candidates = g->ai_scratch.heal_candidates;
	int64_t count = 0, c, first_medic = 0, best = 0, best_d = HEAL_FAR, k;
	if (!kp_can_use_tech(g, KP_TECH_HEALING, u->owner[n], n)) return 0;
	if (u->ai[n] == 0) return kp_nearest_unit_around(g, n, HEAL_SEARCH_RANGE, heal_candidate, n);
	for (c = g->player_units[u->owner[n]]; c != 0; c = u->player_next[c]) {
		if (kp_unit_inactive(g, c) || kp_band(g->w->spr.flags[kp_unit_sprite(g, c)], SPRITE_HIDDEN) != 0) continue;
		if (kp_band(u->status[c], STATUS_COMPLETED) == 0 || u->being_healed[c] != 0 || !can_heal(g, n, c)) continue;
		if (kp_band(u->status[c], STATUS_HALLUCINATION) != 0) continue;
		if (first_medic == 0 && u->type[c] == KP_TYPE_MEDIC) first_medic = c;
		if (kp_band(flags(g, u->type[c]), DAT_ORGANIC) != 0) candidates[count++] = c;
	}
	for (c = first_medic; c != 0 && count != 0; c = u->player_next[c]) {
		int64_t target;
		if (u->order[c] == KP_ORD_MEDIC_HEAL) {
			target = u->order_unit[c];
		} else if (u->queue_head[c] != KP_NONE && g->orders.order[kp_order_node(g, u->queue_head[c])] == KP_ORD_MEDIC_HEAL) {
			target = g->orders.unit[kp_order_node(g, u->queue_head[c])]; /* GetQueueHeadOrder 0x429d10 */
		} else {
			continue;
		}
		for (k = 0; k < count; k++) {
			if (candidates[k] == target) {
				candidates[k] = 0;
				break;
			}
		}
	}
	for (k = 0; k < count; k++) {
		int64_t d;
		int32_t a, b;
		if (candidates[k] == 0) continue;
		a = kp_unit_sprite(g, n);
		b = kp_unit_sprite(g, candidates[k]);
		d = kp_u32v(kp_approx_distance(kp_s16v(g->w->spr.x[b]) - kp_s16v(g->w->spr.x[a]),
			kp_s16v(g->w->spr.y[b]) - kp_s16v(g->w->spr.y[a])));
		if (d < best_d) {
			best_d = d;
			best = candidates[k];
		}
	}
	return best;
}

/* MedicTryHeal 0x4371a0. */
static int try_heal(KpGame *g, int64_t n, int64_t t)
{
	KpUnits *u = &g->u;
	int64_t amount, cost;
	if (!kp_can_use_tech(g, KP_TECH_HEALING, u->owner[n], n)) return HEAL_NONE;
	if (t == 0 || !can_heal(g, n, t)) return HEAL_NONE;
	if (u->being_healed[t] != 0) {
		t = kp_find_heal_target(g, n);
		if (t == 0) return HEAL_NONE;
		kp_set_order_unit(g, n, t);
		kp_set_move_target_unit(g, n, t);
	}
	if (!kp_unit_box_within(g, n, HEAL_RANGE, t)) return HEAL_OUT_OF_RANGE;
	amount = kp_u32v(KP_DAT(g, units, hit_points, u->type[t]) - u->hp[t]);
	if (amount > HEAL_PER_FRAME) amount = HEAL_PER_FRAME;
	cost = kp_shrv(amount, 1);
	if (kp_needs_energy(g, n, cost)) return HEAL_NO_ENERGY;
	kp_spend_energy(g, n, cost);
	kp_set_unit_hp(g, t, u->hp[t] + amount);
	u->being_healed[t] = 1;
	return HEAL_DONE;
}

/* The healing branch of 0x436250 and 0x436430: the heal animation, MedicHealToIdle queued first, facing the target. */
static void healed(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t t = u->order_unit[n];
	int32_t s;
	if (kp_band(u->order_signal[n], SIGNAL_HEALED) == 0) {
		int64_t head = u->queue_head[n];
		kp_sprite_play(g->w, kp_unit_sprite(g, n), KP_ANIM_SPECIAL1, 1);
		if (head == KP_NONE || g->orders.order[head] != KP_ORD_MEDIC_HEAL_TO_IDLE) kp_order_prepend(g, n, KP_ORD_MEDIC_HEAL_TO_IDLE);
	}
	s = kp_unit_sprite(g, t);
	kp_flingy_set_target_waypoint(g, n, kp_s16v(g->w->spr.x[s]), kp_s16v(g->w->spr.y[s]));
}

/* OrderMedicIdle 0x4361b0 (order 175). */
void kp_order_medic_idle(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t t, hx, hy;
	if (u->ai[n] != 0 && kp_ai_reaction_spell(g, n, 0)) return;
	t = kp_find_heal_target(g, n);
	if (t != 0) {
		kp_issue_order(g, n, KP_ORD_MEDIC_HEAL);
		kp_set_order_unit(g, n, t);
		kp_set_move_target_unit(g, n, t);
		return;
	}
	if (u->ai[n] == 0) return;
	if (!kp_ai_guard_home(g, n, &hx, &hy)) {
		kp_issue_order(g, n, KP_ORD_COMPUTER_AI);
		return;
	}
	{
		int32_t s = kp_unit_sprite(g, n);
		if (kp_s16v(g->w->spr.x[s]) != hx || kp_s16v(g->w->spr.y[s]) != hy) kp_issue_order_at(g, n, KP_ORD_COMPUTER_RETURN, hx, hy);
	}
}

/* OrderMedicHeal 0x436250 (order 176). */
void kp_order_medic_heal(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int r = try_heal(g, n, u->order_unit[n]);
	if (r == HEAL_NONE) {
		kp_stop_movement(g, n);
		kp_order_done(g, n);
		return;
	}
	if (u->order_state[n] == 0) {
		int64_t t = u->order_unit[n];
		if (t == 0 || !kp_move_toward_target(g, n, t) || kp_move_arrival(g, n) == 2) {
			kp_order_done(g, n);
			return;
		}
		if (kp_unit_box_within(g, n, HEAL_RANGE, t)) {
			kp_stop_movement(g, n);
			u->order_state[n] = 1;
		}
		return;
	}
	if (r == HEAL_DONE) healed(g, n);
	else u->order_state[n] = 0;
}

/* OrderMedicHealToIdle 0x436520 (order 179). */
void kp_order_medic_heal_to_idle(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	if (kp_band(u->movement_flags[n], 0x2) == 0) kp_sprite_play(g->w, kp_unit_sprite(g, n), KP_ANIM_WALKING_TO_IDLE, 1);
	kp_order_unlock_done(g, n);
}

/* OrderHealMove 0x436310 (order 177): moves to the order point, healing on the way. */
void kp_order_heal_move(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t target = u->order_unit[n], t, x, y;
	int32_t s;
	if (target != 0) {
		if (can_heal(g, n, target)) {
			kp_issue_order(g, n, KP_ORD_MEDIC_HEAL);
			kp_set_order_unit(g, n, target);
			kp_set_move_target_unit(g, n, target);
			return;
		}
		s = kp_unit_sprite(g, target);
		u->order_x[n] = kp_s16v(g->w->spr.x[s]);
		u->order_y[n] = kp_s16v(g->w->spr.y[s]);
	}
	if (!kp_move_to_point_throttled(g, n, u->order_x[n], u->order_y[n])) return;
	s = kp_unit_sprite(g, n);
	if (u->move_x[n] == kp_s16v(g->w->spr.x[s]) && u->move_y[n] == kp_s16v(g->w->spr.y[s])) {
		kp_order_done(g, n);
		return;
	}
	if (u->order_timer[n] != 0) return;
	u->order_timer[n] = SEEK_PERIOD;
	t = kp_find_heal_target(g, n);
	if (t == 0) return;
	x = u->order_x[n];
	y = u->order_y[n];
	kp_issue_order_packed(g, n, KP_ORD_MEDIC_HEAL, x, y, target);
	kp_set_move_target_unit(g, n, t);
	kp_set_order_unit(g, n, t);
	/* AppendOrderOnUnit 0x429550, AppendOrderAtPoint 0x429520 */
	if (target != 0) {
		s = kp_unit_sprite(g, target);
		kp_order_queue(g, n, KP_ORD_HEAL_MOVE, kp_s16v(g->w->spr.x[s]), kp_s16v(g->w->spr.y[s]), target, KP_TYPE_NONE);
	} else {
		kp_order_queue(g, n, KP_ORD_HEAL_MOVE, x, y, 0, KP_TYPE_NONE);
	}
}

/* OrderMedicHoldPosition 0x436430 (order 178): heals within reach without moving. */
void kp_order_medic_hold(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t t;
	if (u->order_state[n] == 0) {
		kp_stop_and_face(g, n);
		kp_set_target_switch(g, n);
		u->order_state[n] = 1;
		kp_sprite_play(g->w, kp_unit_sprite(g, n), KP_ANIM_WALKING_TO_IDLE, 1);
	}
	switch (try_heal(g, n, u->order_unit[n])) {
	case HEAL_DONE:
		healed(g, n);
		return;
	case HEAL_OUT_OF_RANGE:
		u->order_unit[n] = 0;
		/* fall through */
	case HEAL_NONE:
	case HEAL_NO_ENERGY:
		kp_sprite_play(g->w, kp_unit_sprite(g, n), KP_ANIM_WALKING_TO_IDLE, 1);
		break;
	default:
		break;
	}
	if (u->order_timer[n] != 0) return;
	u->order_timer[n] = SEEK_PERIOD;
	t = kp_find_heal_target(g, n);
	u->order_unit[n] = t;
	if (t != 0) {
		int32_t s = kp_unit_sprite(g, t);
		u->order_x[n] = kp_s16v(g->w->spr.x[s]);
		u->order_y[n] = kp_s16v(g->w->spr.y[s]);
	}
}
