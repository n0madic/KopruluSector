/* Order dispatch of the unit step (kp_game.h; game/units/order_dispatch.lua, the specification). */
#include "kp_game_internal.h"

#define DISPATCHER 0x439420
#define INLINE (-1)
#define PROCESS_PERIOD 8
#define DIE 0
#define STOP 1
#define GUARD 2
#define PLAYER_GUARD 3
#define NOTHING 0x17
#define HOLD_POSITION 0x6b
#define COMPUTER_AI 0x9c
#define GUARD_POST 0xa0
#define TURRET_GUARD 0x04
#define BURROWED 0x75
#define RESCUE_PASSIVE 0xa1
#define NEUTRAL 0xa2
#define LURKER 0x67
#define HYDRALISK 0x26
#define ORDER_UNIT_MORPH 0x2a
#define STATUS_NOT_INTERRUPTIBLE 0x1000
#define CONTROLLER_HUMAN 2
#define RESCUE_REACH 64
#define RESCUE_FLASH 45
#define RESCUE_SOUND 0x20
#define STATUS_IN_BUNKER 0x20
#define STATUS_GROUNDED_BUILDING 0x2
#define BUNKER_GUARD 0x05
#define HATCHERY 0x83
#define DAT_RESOURCE_DEPOT 0x1000
#define TRAP 0xa7
#define TOWER_GUARD 0x12
#define ATTACK_MOVE 0x0e
#define PATROL 0x98
#define FOLLOW 0x31
#define FOLLOW_PERIOD 7
#define FOLLOW_RANGE 0x60
#define CHASE_RANGE 0x1c0
#define NYDUS_CANAL 0x86
#define SHIELD_BATTERY 0xac
#define NEUTRAL_OWNER 11
#define CONTROLLER_RESCUE 3
#define CONTROLLER_NEUTRAL 4
#define STATUS_AIR 0x4
#define MEDIC 0x22
#define STATUS_INVINCIBLE 0x4000000
#define USER_HIDE 4
#define STATUS_NO_COLLIDE 0x200000
#define DETECTED_BY_NONE 0x80000000LL
#define CRITTER 0xa6
#define KAKARU 0x5e
#define CRITTER_RANDOM 0x30
#define CRITTER_PAUSE_RANDOM 0x2f
#define CRITTER_STEP 0x20
#define KAKARU_STEP_MIN 0xa0
#define KAKARU_STEP_MAX 0x280
#define CRITTER_PAUSE_MAX 0x4b
#define CRITTER_PAUSE_MIN 15
#define CRITTER_BLOCKED_PAUSE 0x4b
#define MOVE 0x06
#define MOVE_UNIT 0x33
#define GUARD_RANDOM 0x1d
#define GUARD_DELAY_MAX 15
#define SEEK_PERIOD 15
#define STATUS_HOLDING 0x8000000
#define STATUS_CAN_MOVE 0x20000
#define STATUS_HALTED 0x8000
#define DAT_SUBUNIT 0x10
#define DAT_HERO 0x40
#define CONTROLLER_COMPUTER 1
#define NO_TYPE 0xe4
#define GUARD_KIND_POST 1
#define SECONDARY_HALLUCINATION_END 0x95
#define SECONDARY_CLOAK 0x6d
#define SECONDARY_DECLOAK 0x6e
#define SECONDARY_NOTHING 0x17
#define NO_OP 0x4be950

typedef struct {
	int order;
	int64_t address;
} Entry;

/* The dispatcher's switches: the native function of every order (INLINE: handled in the
 * dispatcher). */
static const Entry FIRST[] = { { 0x00, 0x41e630 }, { 0x30, 0x42f020 }, { 0x81, INLINE }, { 0xae, 0x42f1b0 } };
static const Entry EVERY_STEP[] = {
	{ 0x04, 0x41f300 }, { 0x16, 0x41f170 }, { 0x1a, 0x43b900 }, { 0x1e, 0x41a580 }, { 0x1f, 0x42ee00 },
	{ 0x21, 0x41a870 }, { 0x22, 0x41ad30 }, { 0x29, 0x43bd50 }, { 0x2a, 0x43bac0 }, { 0x2c, 0x41a770 },
	{ 0x2d, 0x43b6e0 }, { 0x41, 0x41c950 }, { 0x42, 0x42f640 }, { 0x47, 0x435bf0 }, { 0x48, 0x435f00 },
	{ 0x4b, 0x43ac40 }, { 0x4c, 0x43ace0 }, { 0x58, 0x423c70 }, { 0x59, 0x423cc0 }, { 0x5b, 0x42f7e0 },
	{ 0x62, 0x436a80 }, { 0x63, 0x436be0 }, { 0x69, 0x42f830 }, { 0x6a, 0x42f9e0 }, { 0x7c, 0x4356c0 },
	{ 0x82, 0x42f510 }, { 0x96, 0x41b1f0 }, { 0x97, 0x42e0d0 }, { 0x9b, 0x4214d0 }, { 0xa5, 0x42a4d0 },
	{ 0xa6, 0x427ed0 }, { 0xb0, 0x436250 }, { 0xb1, 0x436310 }, { 0xb2, 0x436430 }, { 0xb3, 0x436520 },
	{ 0xb7, 0x42fa70 },
};
static const Entry PERIODIC[] = {
	{ 0x01, 0x428a10 }, { 0x02, 0x41e700 }, { 0x03, 0x41ee10 }, { 0x06, 0x428660 }, { 0x07, 0x41c910 },
	{ 0x09, 0x427c90 }, { 0x0a, 0x41e5c0 }, { 0x0b, 0x41e400 }, { 0x0c, 0x4be950 }, { 0x0d, 0x4be950 },
	{ 0x0e, 0x41e4a0 }, { 0x0f, 0x43c5a0 }, { 0x11, 0x427210 }, { 0x12, 0x41f090 }, { 0x13, 0x41f100 },
	{ 0x14, 0x436cf0 }, { 0x15, 0x4288f0 }, { 0x17, INLINE }, { 0x19, 0x43ba90 }, { 0x1b, 0x43cf10 },
	{ 0x1c, 0x427c90 }, { 0x1d, 0x43cfe0 }, { 0x20, 0x42f230 }, { 0x23, 0x427c90 }, { 0x24, 0x435730 },
	{ 0x2b, 0x43be80 }, { 0x2e, 0x43bc50 }, { 0x2f, 0x43ccc0 }, { 0x31, 0x428310 }, { 0x32, 0x41c290 },
	{ 0x33, 0x428660 }, { 0x34, 0x41c410 }, { 0x35, 0x41c310 }, { 0x36, 0x427c90 }, { 0x37, 0x41c3c0 },
	{ 0x38, 0x41c290 }, { 0x39, 0x41c290 }, { 0x3a, 0x41c790 }, { 0x3b, 0x41c860 }, { 0x3c, 0x427c90 },
	{ 0x3d, 0x41c790 }, { 0x3e, 0x41c790 }, { 0x40, 0x41c4e0 }, { 0x43, 0x42f550 }, { 0x45, 0x41c450 },
	{ 0x46, 0x43c970 }, { 0x4a, 0x436030 }, { 0x4d, 0x43cd90 }, { 0x4f, 0x423480 }, { 0x50, 0x427c90 },
	{ 0x51, 0x423480 }, { 0x52, 0x4237a0 }, { 0x53, 0x4235c0 }, { 0x54, 0x423ce0 }, { 0x55, 0x4239a0 },
	{ 0x56, 0x423c30 }, { 0x57, 0x423810 }, { 0x5a, 0x423ce0 }, { 0x5c, 0x4376d0 }, { 0x5d, 0x437b50 },
	{ 0x5e, 0x4377a0 }, { 0x5f, 0x437850 }, { 0x60, 0x437a00 }, { 0x61, 0x427290 }, { 0x64, 0x428a30 },
	{ 0x65, 0x43cd50 }, { 0x67, 0x43d250 }, { 0x6b, 0x41e9b0 }, { 0x6c, INLINE }, { 0x6f, 0x437ba0 },
	{ 0x70, 0x437890 }, { 0x71, 0x434460 }, { 0x72, 0x427c90 }, { 0x73, 0x434460 }, { 0x74, 0x43c6f0 },
	{ 0x75, 0x43c810 }, { 0x76, 0x43c880 }, { 0x77, 0x434460 }, { 0x78, 0x434460 }, { 0x79, 0x434460 },
	{ 0x7a, 0x434460 }, { 0x7d, 0x436540 }, { 0x7e, 0x4366f0 }, { 0x7f, 0x41eba0 }, { 0x80, 0x41ea40 },
	{ 0x84, 0x436820 }, { 0x85, 0x431200 }, { 0x86, 0x41ef70 }, { 0x87, 0x41eeb0 }, { 0x88, INLINE },
	{ 0x89, 0x42fc40 }, { 0x8b, 0x436990 }, { 0x8c, 0x436a60 }, { 0x8d, 0x438560 }, { 0x8e, 0x434460 },
	{ 0x8f, 0x434460 }, { 0x90, 0x434460 }, { 0x91, 0x434460 }, { 0x92, 0x434460 }, { 0x93, 0x434460 },
	{ 0x94, 0x430bd0 }, { 0x98, 0x41ebe0 }, { 0x99, 0x421440 }, { 0x9a, 0x4213c0 }, { 0x9c, 0x48cd70 },
	{ 0x9d, 0x41e510 }, { 0x9e, 0x41e890 }, { 0x9f, 0x48cee0 }, { 0xa0, 0x41e7f0 }, { 0xa1, 0x48d120 },
	{ 0xa2, 0x48cfb0 }, { 0xa3, 0x41e370 }, { 0xa4, 0x42f5b0 }, { 0xa7, 0x428490 }, { 0xa8, 0x4281a0 },
	{ 0xa9, 0x428150 }, { 0xaa, 0x4285a0 }, { 0xab, 0x4288a0 }, { 0xac, 0x4281f0 }, { 0xad, 0x428010 },
	{ 0xaf, 0x4361b0 }, { 0xb4, 0x434460 }, { 0xb5, 0x434460 }, { 0xb6, 0x430ce0 }, { 0xb8, 0x430d90 },
	{ 0xb9, 0x434460 }, { 0xba, 0x434460 }, { 0xbb, 0x4286f0 },
};
/* DispatchHiddenOrder 0x439e20: IDLE becomes BunkerGuard/Nothing, NONE does nothing. */
#define HIDDEN_TO_IDLE (-2)
#define HIDDEN_NONE (-3)
static const Entry HIDDEN_FIRST[] = {
	{ 0x00, 0x41e630 }, { 0x03, HIDDEN_TO_IDLE }, { 0x04, HIDDEN_TO_IDLE }, { 0x11, 0x427210 },
	{ 0x16, HIDDEN_TO_IDLE }, { 0x17, HIDDEN_NONE }, { 0x18, HIDDEN_NONE }, { 0x1d, 0x43cfe0 },
	{ 0x53, 0x4235c0 }, { 0x5c, HIDDEN_TO_IDLE }, { 0x61, 0x427290 }, { 0x7d, 0x436540 }, { 0x96, 0x41b1f0 },
	{ 0x97, 0x42e0d0 }, { 0xa2, HIDDEN_NONE }, { 0xaf, HIDDEN_NONE }, { 0xb0, HIDDEN_NONE },
};
static const Entry HIDDEN_PERIODIC[] = { { 0x05, 0x41e760 }, { 0x60, 0x437a00 }, { 0x9c, 0x48d2d0 }, { 0xa1, 0x48d120 } };
static const Entry SECONDARY[] = {
	{ 0x25, 0x439310 }, { 0x26, 0x41b060 }, { 0x3f, 0x41c630 }, { 0x44, 0x42f770 }, { 0x4e, 0x43d0d0 },
	{ 0x66, 0x43d1a0 }, { 0x83, 0x434710 },
};

#define COUNT(t) ((int)(sizeof t / sizeof t[0]))

/* The address of `order` in a switch table, 0 for none. */
static int64_t lookup(const Entry *t, int count, int64_t order)
{
	int i;
	for (i = 0; i < count; i++) {
		if (t[i].order == order) return t[i].address;
	}
	return 0;
}

static void unported(KpGame *g, const char *kind, int64_t order, int64_t address)
{
	if (address == INLINE) address = DISPATCHER;
	kp_game_fail(g, "orders: %s order 0x%02x (0x%x)", kind, (unsigned)order, (unsigned)address);
}


static int64_t flags(KpGame *g, int64_t t) { return KP_DAT(g, units, special_ability_flags, t); }

/* Order_Guard 0x41e700. */
static void order_guard(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	u->order_timer[n] = kp_rng_game_range(g->rng, GUARD_RANDOM, 0, GUARD_DELAY_MAX) % 256;
	if (kp_controller(g, u->owner[n]) == CONTROLLER_COMPUTER) {
		int64_t ai;
		u->order[n] = COMPUTER_AI;
		ai = u->ai[n];
		if (ai != 0) {
			if (ai >= 1 && ai <= KP_GUARDS && g->guards.kind[ai - 1] == GUARD_KIND_POST) u->order[n] = GUARD_POST;
			return;
		}
		if (kp_band(flags(g, u->type[n]), DAT_HERO) == 0) return;
	}
	u->order[n] = PLAYER_GUARD;
}

static void flingy_set_target_waypoint(KpGame *g, int64_t n, int64_t x, int64_t y)
{
	kp_flingy_set_target_waypoint(g, n, x, y);
}

/* Order_PlayerGuard 0x41ee10. */
static void order_player_guard(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t target = u->auto_target[n];
	if (target != 0 && u->ai[n] == 0) {
		if (kp_is_enemy(g, n, target) && kp_can_attack(g, n, target, 1)) {
			kp_game_fail(g, "orders: attack of the remembered target (0x4202c0)");
		}
		u->auto_target[n] = 0;
	}
	if (u->order_timer[n] != 0) return;
	u->order_timer[n] = SEEK_PERIOD;
	if (kp_band(flags(g, u->type[n]), DAT_SUBUNIT) != 0) {
		int64_t base = u->subunit[n];
		flingy_set_target_waypoint(g, n, u->target_waypoint_x[base], u->target_waypoint_y[base]);
	}
	if (kp_seek_range(g, n) != 0 && kp_acquire_target(g, n) != 0) {
		kp_game_fail(g, "orders: attack of an acquired target (0x4202c0)");
	}
}

/* OrderStop 0x428a10. */
static void order_stop(KpGame *g, int64_t n)
{
	kp_stop_movement(g, n);
	kp_reset_to_idle_animation(g, n);
	kp_order_done(g, n);
}

/* Order_HoldPosition 0x41e9b0. */
static void order_hold_position(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t target;
	if (u->order_state[n] == 0) {
		int64_t sub;
		kp_stop_movement(g, n);
		u->status[n] = kp_bor(u->status[n], STATUS_HOLDING);
		sub = u->subunit[n];
		if (sub != 0) u->status[sub] = kp_bor(u->status[sub], STATUS_HOLDING);
		u->order_state[n] = 1;
	}
	if (u->order_unit[n] != 0) kp_game_fail(g, "orders: attack in place (0x420bd0)");
	if (u->order_timer[n] != 0) return;
	u->order_timer[n] = SEEK_PERIOD;
	target = kp_acquire_random_target(g, n);
	u->order_unit[n] = target;
	if (target != 0) u->queue_timer[n] = 0;
}

int kp_request_move_to_point(KpGame *g, int64_t n, int64_t x, int64_t y)
{
	int ok = kp_set_move_target_point(g, n, x, y);
	flingy_set_target_waypoint(g, n, x, y);
	return ok;
}

int kp_request_move_to_unit(KpGame *g, int64_t n, int64_t target)
{
	int ok = kp_set_move_target_unit(g, n, target);
	int32_t s = kp_unit_sprite(g, target);
	flingy_set_target_waypoint(g, n, kp_s16v(g->w->spr.x[s]), kp_s16v(g->w->spr.y[s]));
	return ok;
}

static int unit_box_within(KpGame *g, int64_t n, int64_t range, int64_t target)
{
	return kp_unit_box_within(g, n, range, target);
}

static int64_t move_arrival(KpGame *g, int64_t n) { return kp_move_arrival(g, n); }

/* MoveTowardTarget 0x438ba0: false when the order ended. */
static int move_toward_target(KpGame *g, int64_t n, int64_t target)
{
	KpUnits *u = &g->u;
	if (u->recent_order_timer[n] != 0) return 1;
	if (kp_band(g->w->spr.visibility[kp_unit_sprite(g, target)], (int64_t)1 << u->owner[n]) != 0
		|| unit_box_within(g, n, CHASE_RANGE, target)) {
		return kp_set_move_target_unit(g, n, target);
	}
	if (move_arrival(g, n) == 1) {
		kp_order_done(g, n);
		return 0;
	}
	return 1;
}


/* OrderFollow 0x428310. */
static void order_follow(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t target = u->order_unit[n], owner, ctl;
	if (target == 0) {
		kp_order_done(g, n);
		return;
	}
	if (u->ai[n] != 0) {
		int64_t max = kp_sarv(KP_DAT(g, units, hit_points, u->type[n]), 8);
		int64_t hp = kp_sarv(u->hp[n] + 0xff, 8);
		if (max == 0) max = hp != 0 ? hp : 1;
		if (hp == max) {
			kp_order_done(g, n);
			return;
		}
	}
	if (u->order_state[n] == 0) {
		int64_t tt;
		if (!kp_request_move_to_unit(g, n, target)) return;
		tt = u->type[target];
		if (tt == NYDUS_CANAL || tt == SHIELD_BATTERY) {
			kp_game_fail(g, "orders: following into a Nydus Canal or to a Shield Battery (0x43c360, 0x42f310)");
		}
		u->order_state[n] = 1;
	}
	if (u->order_timer[n] != 0) return;
	u->order_timer[n] = FOLLOW_PERIOD;
	owner = u->owner[target];
	if (owner == NEUTRAL_OWNER) owner = g->w->spr.owner[kp_unit_sprite(g, target)];
	if (kp_alliance(g, u->owner[n], owner) == 0 && kp_can_attack(g, n, target, 1)) {
		kp_issue_order(g, n, GUARD);
		return;
	}
	if (kp_band(u->status[n], STATUS_AIR) == 0 && !kp_same_ground_group(g, n, target)) {
		kp_stop_movement(g, n);
		return;
	}
	ctl = kp_controller(g, u->owner[target]);
	if ((ctl == CONTROLLER_RESCUE || ctl == CONTROLLER_NEUTRAL) && !unit_box_within(g, n, 1, target)) {
		kp_request_move_to_unit(g, n, target);
		return;
	}
	if (!unit_box_within(g, n, FOLLOW_RANGE, target)) move_toward_target(g, n, target);
	else if (kp_band(u->status[target], STATUS_GROUNDED_BUILDING) != 0 || u->queue_head[n] != -1) kp_order_done(g, n);
	else kp_stop_movement(g, n);
}

/* OrderMove 0x428660. */
static void order_move(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t state = u->order_state[n];
	if (state == 0) {
		int64_t target = u->order_unit[n];
		if (target != 0) {
			if (kp_request_move_to_unit(g, n, target)) u->order_state[n] = 1;
		} else if (kp_request_move_to_point(g, n, u->order_x[n], u->order_y[n])) {
			u->order_state[n] = 1;
		}
	} else if (state == 1) {
		int32_t s = kp_unit_sprite(g, n);
		if (u->move_x[n] == kp_s16v(g->w->spr.x[s]) && u->move_y[n] == kp_s16v(g->w->spr.y[s])) {
			kp_order_done(g, n);
			if (u->ai[n] != 0) u->auto_target[n] = 0;
		}
	}
}

static void order_nothing(KpGame *g, int64_t n)
{
	if (g->u.queue_head[n] != -1) kp_order_activate(g, n);
}

/* Order_TurretGuard 0x41f300. */
static void order_turret_guard(KpGame *g, int64_t n)
{
	int64_t base = g->u.subunit[n];
	flingy_set_target_waypoint(g, n, g->u.target_waypoint_x[base], g->u.target_waypoint_y[base]);
	order_player_guard(g, n);
}

/* IsUnitInactive 0x41fe40. */
static int inactive(KpGame *g, int64_t n) { return g->u.sprite[n] == -1 || g->u.order[n] == DIE; }

static void rescued_order(KpGame *g, int64_t n, int64_t owner)
{
	KpUnits *u = &g->u;
	kp_change_owner(g, n, owner, 1);
	kp_issue_order(g, n, kp_band(u->status[n], STATUS_IN_BUNKER) != 0 ? BUNKER_GUARD : KP_DAT(g, units, human_ai_idle, u->type[n]));
}

static int hatchery_complete(int64_t t) { return t == 0x84 || t == 0x85; }

/* Order_RescuePassive 0x48d120. */
static void order_rescue_passive(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int32_t s;
	int64_t x, y, r = RESCUE_REACH, rescuer = 0, owner, t, status, found[KP_UNITS];
	int count, k;
	if (u->order_timer[n] != 0) return;
	u->order_timer[n] = 1;
	s = kp_unit_sprite(g, n);
	x = g->w->spr.x[s];
	y = g->w->spr.y[s];
	count = kp_units_in_rect(g, kp_s16v(x - r), kp_s16v(y - r), kp_s16v(x + r), kp_s16v(y + r), found, KP_UNITS);
	for (k = 0; k < count; k++) {
		if (kp_controller(g, u->owner[found[k]]) == CONTROLLER_HUMAN) {
			rescuer = found[k];
			break;
		}
	}
	if (rescuer == 0) return;
	owner = u->owner[rescuer];
	t = u->type[n];
	status = u->status[n];
	if ((kp_band(status, 1) != 0 || hatchery_complete(t) || (t == HATCHERY && kp_is_morphing_zerg_building(g, n)))
		&& kp_band(status, STATUS_GROUNDED_BUILDING) != 0 && kp_band(flags(g, t), DAT_RESOURCE_DEPOT) != 0) {
		int64_t m = g->player_units[u->owner[n]];
		while (m != 0) {
			int64_t next = u->player_next[m];
			if (!inactive(g, m)) rescued_order(g, m, owner);
			m = next;
		}
	} else {
		rescued_order(g, n, owner);
	}
	if (u->owner[n] == g->local_player) {
		g->w->spr.sel_timer[s] = RESCUE_FLASH;
		if (g->w->has_sound) {
			int64_t args[3];
			args[0] = RESCUE_SOUND + g->players.race[g->local_player];
			args[1] = g->w->spr.x[s];
			args[2] = g->w->spr.y[s];
			g->w->hook(g->w, g->w->ctx, KP_HOOK_SOUND, -1, args, 3, NULL);
		}
	}
}

void kp_set_cloak_bits(KpGame *g, int64_t n, int64_t v)
{
	g->u.status[n] = kp_bor(kp_band(g->u.status[n], kp_bnot(0x300)), v * 256);
}

/* OrderTrap 0x428490. */
static void order_trap(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t state = u->order_state[n];
	if (state == 0) {
		u->status[n] = kp_bor(u->status[n], STATUS_NO_COLLIDE);
		if (u->type[n] == 0xcb || u->type[n] == 0xd1) g->w->spr.elevation[kp_unit_sprite(g, n)] = 0;
		kp_set_cloak_bits(g, n, 3);
		u->visibility[n] = DETECTED_BY_NONE;
		u->secondary_timer[n] = 0;
		u->order_state[n] = 1;
	} else if (state == 1 && !kp_unit_disabled(g, n) && u->order_timer[n] == 0) {
		u->order_timer[n] = SEEK_PERIOD;
		/* TODO: the trap's reveal and attack (RevealTrap 171, AttackUnit, HideTrap 170) arrive
		 * with combat in phase 4. */
		if (kp_acquire_random_target(g, n) != 0) kp_game_fail(g, "orders: a trap attacks its prey (0x428490)");
	}
}

/* Order_TowerGuard 0x41f090. */
static void order_tower_guard(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t target;
	if (kp_unit_disabled(g, n) || u->order_timer[n] != 0) return;
	u->order_timer[n] = SEEK_PERIOD;
	target = kp_acquire_random_target(g, n);
	u->order_unit[n] = target;
	/* TODO: TowerAttack 0x13 and the attack status arrive with combat in phase 4. */
	if (target != 0) kp_game_fail(g, "orders: a tower attacks (0x41f090)");
}

static int attackable_auto_target(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t a = u->auto_target[n];
	return a != 0 && kp_is_enemy(g, n, a) && kp_band(g->w->spr.flags[kp_unit_sprite(g, a)], 0x20) == 0
		&& kp_band(u->status[a], STATUS_INVINCIBLE) == 0 && !kp_is_undetected_by(g, a, n);
}

static void unlock_and_end(KpGame *g, int64_t n)
{
	g->u.status[n] = kp_band(g->u.status[n], kp_bnot(STATUS_NOT_INTERRUPTIBLE));
	kp_order_done(g, n);
}

/* AttackMovePrepare 0x420600. */
static int attack_move_prepare(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	if (u->order_state[n] == 0) {
		if (!kp_request_move_to_point(g, n, u->order_x[n], u->order_y[n])) return 0;
		u->order_state[n] = 1;
	}
	if (attackable_auto_target(g, n)) {
		/* TODO: the attack on the remembered target (0x429c40, 0x429da0) arrives with combat in
		 * phase 4, the computer's choice (0x41f6c0) with the AI in phase 7. */
		kp_game_fail(g, "orders: attack-move on the remembered target (0x420600)");
	}
	return 1;
}

/* Order_AttackMove 0x41e4a0. */
static void order_attack_move(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int32_t s;
	if (!attack_move_prepare(g, n) || u->order_timer[n] != 0) return;
	u->order_timer[n] = SEEK_PERIOD;
	s = kp_unit_sprite(g, n);
	if (u->move_x[n] == kp_s16v(g->w->spr.x[s]) && u->move_y[n] == kp_s16v(g->w->spr.y[s])) {
		kp_order_done(g, n);
		return;
	}
	if (kp_seek_range(g, n) == 0) return;
	if (u->ai[n] != 0) kp_game_fail(g, "orders: computer attack-move target (0x41f6c0)");
	if (kp_acquire_target(g, n) != 0) kp_game_fail(g, "orders: attack-move on an acquired target (0x4206e0)");
}

/* Order_Patrol 0x41ebe0. */
static void order_patrol(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int32_t s = kp_unit_sprite(g, n);
	int64_t x, y;
	if (u->order_state[n] == 0) {
		int64_t head = u->queue_head[n];
		if (head == -1 || g->orders.order[head] != PATROL) {
			kp_issue_order_packed(g, n, PATROL, u->order_x[n], u->order_y[n], u->order_unit[n]);
			kp_order_queue(g, n, PATROL, kp_s16v(g->w->spr.x[s]), kp_s16v(g->w->spr.y[s]), 0, NO_TYPE);
			return;
		}
		if (!kp_request_move_to_point(g, n, u->order_x[n], u->order_y[n])) return;
		u->order_state[n] = 1;
	}
	if (attackable_auto_target(g, n)) kp_game_fail(g, "orders: patrol attack on the remembered target (0x41ebe0)");
	if (u->order_timer[n] != 0) return;
	u->order_timer[n] = SEEK_PERIOD;
	if (u->type[n] == MEDIC) kp_game_fail(g, "orders: patrolling Medic's heal search (0x431ce0)");
	x = kp_s16v(g->w->spr.x[s]);
	y = kp_s16v(g->w->spr.y[s]);
	if (u->move_x[n] == x && u->move_y[n] == y) {
		if (move_arrival(g, n) == 2) {
			x = u->order_x[n];
			y = u->order_y[n];
		}
		kp_order_queue(g, n, PATROL, x, y, 0, NO_TYPE);
		unlock_and_end(g, n);
		return;
	}
	if (kp_seek_range(g, n) != 0 && kp_acquire_target(g, n) != 0) {
		kp_game_fail(g, "orders: patrol attack on an acquired target (0x41ebe0)");
	}
}

/* Order_Neutral 0x48cfb0. */
static void order_neutral(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t attacker = u->auto_target[n];
	if (attacker == 0) return;
	if (kp_band(g->w->spr.flags[kp_unit_sprite(g, attacker)], 0x20) == 0 && !kp_is_undetected_by(g, attacker, n)
		&& u->owner[n] != u->owner[attacker]) {
		kp_game_fail(g, "orders: a neutral unit turns hostile (0x48cfb0)");
	}
	u->auto_target[n] = 0;
}

/* Order_Burrowed 0x43c810. */
static void order_burrowed(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t t = u->type[n], head;
	if (t == LURKER) {
		u->status[n] = kp_band(u->status[n], kp_bnot(STATUS_NOT_INTERRUPTIBLE));
		kp_order_done(g, n);
		return;
	}
	head = u->queue_head[n];
	if (t == HYDRALISK && head != -1 && g->orders.order[head] == ORDER_UNIT_MORPH) {
		kp_game_fail(g, "orders: burrowed Hydralisk morph (0x43c810)");
	}
	if (u->ai[n] != 0 && u->order_timer[n] == 0) kp_game_fail(g, "orders: burrowed unit AI reaction (0x48d3a0, 0x41f6c0)");
}

/* OrderCritter 0x427ed0. */
static void order_critter(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t state = u->order_state[n], t = u->type[n];
	if (state == 0) {
		int64_t dir = kp_rng_game_range(g->rng, CRITTER_RANDOM, 0, 0xff), dist = CRITTER_STEP, x, y;
		int32_t s;
		if (t == KAKARU) dist = kp_rng_game_range(g->rng, CRITTER_RANDOM, KAKARU_STEP_MIN, KAKARU_STEP_MAX);
		s = kp_unit_sprite(g, n);
		x = kp_s16v(kp_sarv(KP_DIR_X[dir] * dist, 8) + g->w->spr.x[s]);
		y = kp_s16v(kp_sarv(KP_DIR_Y[dir] * dist, 8) + g->w->spr.y[s]);
		kp_clamp_to_map(g, t, &x, &y);
		u->order_x[n] = x;
		u->order_y[n] = y;
		kp_set_move_target_point(g, n, x, y);
		u->order_state[n] = 1;
	} else if (state == 1) {
		int64_t arrival = move_arrival(g, n);
		if (arrival == 0) return;
		if (t == KAKARU) {
			u->order_state[n] = 0;
		} else if (arrival == 2) {
			u->order_timer[n] = CRITTER_BLOCKED_PAUSE;
			u->order_state[n] = 2;
		} else {
			int64_t r = kp_rng_game_range(g->rng, CRITTER_PAUSE_RANDOM, 0, CRITTER_PAUSE_MAX);
			u->order_state[n] = 2;
			u->order_timer[n] = r > CRITTER_PAUSE_MIN ? kp_mod(r, 256) : 0;
		}
	} else if (state == 2 && u->order_timer[n] == 0) {
		u->order_state[n] = 0;
	}
}

/* Order_Die 0x41e630.
 * TODO: the visible Death animation and the hallucination smoke arrive with combat in phase 4. */
static void order_die(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t sub;
	if (kp_band(u->user_action[n], USER_HIDE) != 0) kp_unit_hide(g, n);
	sub = u->subunit[n];
	if (sub != 0) {
		int64_t ss = u->sprite[sub];
		kp_sprite_take_main(g->w, kp_unit_sprite(g, n), ss == KP_NONE ? KP_NONE : kp_check_sprite(g->w, (int32_t)ss));
		kp_unit_destroy(g, sub);
		u->subunit[n] = 0;
	}
	u->order_state[n] = 1;
	if (kp_band(g->w->spr.flags[kp_unit_sprite(g, n)], 0x20) == 0) {
		kp_game_fail(g, "orders: the Death animation of a visible unit (0x41e630)");
	}
	kp_unit_hide(g, n);
	kp_unit_destroy(g, n);
}

typedef void (*Handler)(KpGame *g, int64_t n);

static Handler every_step_handler(int64_t order)
{
	if (order == TURRET_GUARD) return order_turret_guard;
	if (order == CRITTER) return order_critter;
	return NULL;
}

static Handler periodic_handler(int64_t order)
{
	switch (order) {
	case GUARD: return order_guard;
	case PLAYER_GUARD: return order_player_guard;
	case NOTHING: return order_nothing;
	case STOP: return order_stop;
	case HOLD_POSITION: return order_hold_position;
	case MOVE: return order_move;
	case MOVE_UNIT: return order_move;
	case RESCUE_PASSIVE: return order_rescue_passive;
	case NEUTRAL: return order_neutral;
	case BURROWED: return order_burrowed;
	case TRAP: return order_trap;
	case TOWER_GUARD: return order_tower_guard;
	case ATTACK_MOVE: return order_attack_move;
	case PATROL: return order_patrol;
	case FOLLOW: return order_follow;
	}
	return NULL;
}

int kp_order_handler_run(KpGame *g, int64_t order, int64_t n)
{
	Handler h = every_step_handler(order);
	if (!h) h = periodic_handler(order);
	if (!h) return 0;
	h(g, n);
	return 1;
}

int kp_order_has_handler(int64_t order) { return every_step_handler(order) || periodic_handler(order); }

/* AlertIfDisabledNearby 0x4af270. */
static void disabled_alert(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	if (u->order_timer[n] != 0) return;
	u->order_timer[n] = 15;
	if (u->lockdown_timer[n] != 0 || u->maelstrom_timer[n] != 0 || kp_band(u->status[n], STATUS_HALTED) != 0) {
		kp_game_fail(g, "orders: help request of a disabled unit (0x442b20 with 0x4aaaa0)");
	}
}

/* AlertIfAcidSporesOrInterceptor 0x4af2c0. */
static void acid_or_interceptor_alert(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	if (u->acid_spores[n] >= 4) kp_game_fail(g, "orders: acid spore alert (0x442b20 with 0x4aaaa0)");
	if (kp_scalar(g, KP_INTERCEPTOR_ALERTED) != 1 && u->type[n] == 0x49 && u->union_0xc0[n] != 0) {
		kp_game_fail(g, "orders: interceptor alert (0x4af220)");
	}
}

void kp_dispatch_main_order(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t order = u->order[n], status, timer, address;
	Handler h;
	if (order == DIE) {
		order_die(g, n);
		return;
	}
	address = lookup(FIRST, COUNT(FIRST), order);
	if (address) unported(g, "main", order, address);
	status = u->status[n];
	if (kp_unit_disabled(g, n)) {
		disabled_alert(g, n);
		return;
	}
	if (kp_band(status, STATUS_CAN_MOVE) == 0 && kp_band(status, STATUS_HALTED) != 0) disabled_alert(g, n);
	h = every_step_handler(order);
	if (h) {
		h(g, n);
	} else {
		address = lookup(EVERY_STEP, COUNT(EVERY_STEP), order);
		if (address) unported(g, "main", order, address);
	}
	timer = u->queue_timer[n];
	u->queue_timer[n] = kp_mod(timer - 1, 256);
	if (timer != 0) return;
	u->queue_timer[n] = PROCESS_PERIOD;
	acid_or_interceptor_alert(g, n);
	u->ai_action[n] = 0;
	h = periodic_handler(order);
	if (h) {
		h(g, n);
	} else {
		address = lookup(PERIODIC, COUNT(PERIODIC), order);
		if (address && address != NO_OP) unported(g, "main", order, address);
	}
}

void kp_dispatch_hidden_order(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t order = u->order[n], first = lookup(HIDDEN_FIRST, COUNT(HIDDEN_FIRST), order), timer, address;
	if (order == DIE) {
		order_die(g, n);
		return;
	} else if (first == HIDDEN_TO_IDLE) {
		kp_issue_order(g, n, kp_band(u->status[n], STATUS_IN_BUNKER) != 0 ? BUNKER_GUARD : NOTHING);
		return;
	} else if (first == HIDDEN_NONE) {
		return;
	} else if (first) {
		unported(g, "hidden", order, first);
	}
	timer = u->queue_timer[n];
	u->queue_timer[n] = kp_mod(timer - 1, 256);
	if (timer != 0) return;
	u->queue_timer[n] = PROCESS_PERIOD;
	if (order == RESCUE_PASSIVE) {
		order_rescue_passive(g, n);
	} else {
		address = lookup(HIDDEN_PERIODIC, COUNT(HIDDEN_PERIODIC), order);
		if (address) unported(g, "hidden", order, address);
	}
}

void kp_dispatch_secondary_order(KpGame *g, int64_t n)
{
	int64_t order = g->u.secondary_order[n], address;
	if (order == SECONDARY_HALLUCINATION_END) kp_game_fail(g, "orders: secondary order 0x95 (0x439310)");
	if (kp_unit_disabled(g, n)) return;
	kp_secondary_cloak_order(g, n, order);
	address = lookup(SECONDARY, COUNT(SECONDARY), order);
	if (address) unported(g, "secondary", order, address);
}

void kp_secondary_cloak_order(KpGame *g, int64_t n, int64_t order)
{
	if (order == SECONDARY_CLOAK) kp_cloak_list_add(g, n);
	else if (order == SECONDARY_DECLOAK) kp_set_secondary_order(g, n, SECONDARY_NOTHING);
}
