/* The Medic's Heal (kp_game.h), ported from the atlas (P11, P12): the heal target search, one frame of healing and the
 * Medic orders 175-179. */
#include "kp_game_internal.h"

#define HEALING 34 /* the tech */
#define HEAL_SEARCH_RANGE 160
#define HEAL_RANGE 30
#define HEAL_PER_FRAME 200 /* raw hit points (1/256) */
#define SEEK_PERIOD 15
#define STATUS_AIR 0x4
#define STATUS_HALLUCINATION 0x40000000
#define DAT_BUILDING 0x1
#define DAT_ORGANIC 0x10000
#define CHEAT_ENERGY 0x800 /* 0x5432fc: spells cost nothing */
#define SIGNAL_HEALED 0x1
#define ANIM_WALKING_TO_IDLE 12
#define ANIM_SPECIAL1 13
#define MEDIC_HEAL 0xb0
#define HEAL_MOVE 0xb1
#define MEDIC_HEAL_TO_IDLE 0xb3
#define COMPUTER_AI 0x9c
#define COMPUTER_RETURN 0xa3
#define NO_TYPE 0xe4
#define STATUS_NOT_INTERRUPTIBLE 0x1000

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

/* FindHealTarget 0x431ce0. */
int64_t kp_find_heal_target(KpGame *g, int64_t n)
{
	if (!kp_can_use_tech(g, HEALING, g->u.owner[n], n)) return 0;
	if (g->u.ai[n] != 0) kp_game_fail(g, "medic: a computer's Medic looks for a heal target (0x431ce0)");
	return kp_nearest_unit_around(g, n, HEAL_SEARCH_RANGE, heal_candidate, n);
}

/* SetOrderTargetUnit 0x417980. */
static void set_order_target(KpGame *g, int64_t n, int64_t t)
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

/* MedicTryHeal 0x4371a0. */
static int try_heal(KpGame *g, int64_t n, int64_t t)
{
	KpUnits *u = &g->u;
	int64_t amount, cost;
	if (!kp_can_use_tech(g, HEALING, u->owner[n], n)) return HEAL_NONE;
	if (t == 0 || !can_heal(g, n, t)) return HEAL_NONE;
	if (u->being_healed[t] != 0) {
		t = kp_find_heal_target(g, n);
		if (t == 0) return HEAL_NONE;
		set_order_target(g, n, t);
		kp_set_move_target_unit(g, n, t);
	}
	if (!kp_unit_box_within(g, n, HEAL_RANGE, t)) return HEAL_OUT_OF_RANGE;
	amount = kp_u32v(KP_DAT(g, units, hit_points, u->type[t]) - u->hp[t]);
	if (amount > HEAL_PER_FRAME) amount = HEAL_PER_FRAME;
	cost = kp_shrv(amount, 1);
	/* NeedsMoreEnergy 0x434b50, SpendEnergy 0x435300 */
	if (kp_band(kp_scalar(g, KP_CHEATS), CHEAT_ENERGY) == 0) {
		if (kp_mod(u->energy[n], 65536) < cost) return HEAL_NO_ENERGY;
		u->energy[n] = kp_mod(u->energy[n] - cost, 65536);
	}
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
		kp_sprite_play(g->w, kp_unit_sprite(g, n), ANIM_SPECIAL1, 1);
		/* PrependOrNewOrder 0x429b20 */
		if (head == KP_NONE) kp_order_queue(g, n, MEDIC_HEAL_TO_IDLE, 0, 0, 0, NO_TYPE);
		else if (g->orders.order[head] != MEDIC_HEAL_TO_IDLE) {
			kp_order_insert_before(g, n, MEDIC_HEAL_TO_IDLE, 0, 0, 0, NO_TYPE, head);
		}
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
		kp_issue_order(g, n, MEDIC_HEAL);
		set_order_target(g, n, t);
		kp_set_move_target_unit(g, n, t);
		return;
	}
	if (u->ai[n] == 0) return;
	if (!kp_ai_guard_home(g, n, &hx, &hy)) {
		kp_issue_order(g, n, COMPUTER_AI);
		return;
	}
	{
		int32_t s = kp_unit_sprite(g, n);
		if (kp_s16v(g->w->spr.x[s]) != hx || kp_s16v(g->w->spr.y[s]) != hy) kp_issue_order_at(g, n, COMPUTER_RETURN, hx, hy);
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
	if (kp_band(u->movement_flags[n], 0x2) == 0) kp_sprite_play(g->w, kp_unit_sprite(g, n), ANIM_WALKING_TO_IDLE, 1);
	/* UnlockAndEndOrder 0x429d00 */
	u->status[n] = kp_band(u->status[n], kp_bnot(STATUS_NOT_INTERRUPTIBLE));
	kp_order_done(g, n);
}

/* OrderHealMove 0x436310 (order 177): moves to the order point, healing on the way. */
void kp_order_heal_move(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t target = u->order_unit[n], t, x, y;
	int32_t s;
	if (target != 0) {
		if (can_heal(g, n, target)) {
			kp_issue_order(g, n, MEDIC_HEAL);
			set_order_target(g, n, target);
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
	kp_issue_order_packed(g, n, MEDIC_HEAL, x, y, target);
	kp_set_move_target_unit(g, n, t);
	set_order_target(g, n, t);
	/* AppendOrderOnUnit 0x429550, AppendOrderAtPoint 0x429520 */
	if (target != 0) {
		s = kp_unit_sprite(g, target);
		kp_order_queue(g, n, HEAL_MOVE, kp_s16v(g->w->spr.x[s]), kp_s16v(g->w->spr.y[s]), target, NO_TYPE);
	} else {
		kp_order_queue(g, n, HEAL_MOVE, x, y, 0, NO_TYPE);
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
		kp_sprite_play(g->w, kp_unit_sprite(g, n), ANIM_WALKING_TO_IDLE, 1);
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
		kp_sprite_play(g->w, kp_unit_sprite(g, n), ANIM_WALKING_TO_IDLE, 1);
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
