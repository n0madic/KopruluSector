/* Order dispatch of the unit step (kp_game.h). */
#include "kp_game_internal.h"

#define DISPATCHER 0x439420
#define INLINE (-1)
#define PROCESS_PERIOD 8
#define RESCUE_REACH 64
#define RESCUE_FLASH 45
#define RESCUE_SOUND 0x20
#define STATUS_IN_BUNKER 0x20
#define STATUS_IN_TRANSPORT 0x40
#define STATUS_GROUNDED_BUILDING 0x2
#define DAT_RESOURCE_DEPOT 0x1000
#define DOODAD_SIGNAL 0x1 /* the sigorder of a trap's or a door's animation */
#define OPEN_DOOR_ELEVATION 1
#define STATUS_DISABLED 0x400
#define STATUS_NOBRK 0x2000
#define CREEP_SPREAD_PERIOD 15
#define HATCHERY_FIRST 131
#define HATCHERY_LAST 133
#define BURROWED_AI_PERIOD 7
#define DAT_SINGLE_ENTITY 0x800
#define DAT_BUILDING 0x1
#define STATUS_FREE_CLOAK 0x800
#define HELPLESS_RADIUS 0x280
#define HARASS_REACH 96
#define FOLLOW_PERIOD 7
#define FOLLOW_RANGE 0x60
#define CHASE_RANGE 0x1c0
#define STATUS_AIR 0x4
#define STATUS_INVINCIBLE 0x4000000
#define USER_HIDE 4
#define STATUS_NO_COLLIDE 0x200000
#define DETECTED_BY_NONE 0x80000000LL
#define STATUS_HALLUCINATION 0x40000000
#define STATUS_LIFETIME_EXPIRED 0x80000000LL
#define CRITTER_RANDOM 0x30
#define CRITTER_PAUSE_RANDOM 0x2f
#define CRITTER_STEP 0x20
#define KAKARU_STEP_MIN 0xa0
#define KAKARU_STEP_MAX 0x280
#define CRITTER_PAUSE_MAX 0x4b
#define CRITTER_PAUSE_MIN 15
#define CRITTER_BLOCKED_PAUSE 0x4b
#define GUARD_RANDOM 0x1d
#define GUARD_DELAY_MAX 15
#define SEEK_PERIOD 15
#define STATUS_AUTO_ATTACK 0x8 /* set by an attack on the remembered target (0x41dce0) */
#define STATUS_CAN_MOVE 0x20000
#define STATUS_HALTED 0x8000
#define DAT_SUBUNIT 0x10
#define DAT_HERO 0x40
#define NO_OP 0x4be950

typedef struct {
	int order;
	int64_t address;
} Entry;

/* The dispatcher's switches: the native function of every order (INLINE: handled in the
 * dispatcher). */
static const Entry EVERY_STEP[] = {
	{ KP_ORD_TURRET_GUARD, 0x41f300 }, { KP_ORD_TURRET_ATTACK, 0x41f170 }, { KP_ORD_DRONE_BUILD, 0x43b900 },
	{ KP_ORD_PLACE_BUILDING, 0x41a580 }, { KP_ORD_PLACE_PROTOSS_BUILDING, 0x42ee00 },
	{ KP_ORD_CONSTRUCTING, 0x41a870 }, { KP_ORD_REPAIR, 0x41ad30 }, { KP_ORD_ZERG_BIRTH, 0x43bd50 },
	{ KP_ORD_ZERG_UNIT_MORPH, 0x43bac0 }, { KP_ORD_INCOMPLETE_BUILDING, 0x41a770 },
	{ KP_ORD_INCOMPLETE_MORPHING, 0x43b6e0 }, { KP_ORD_SCARAB_ATTACK, 0x41c950 },
	{ KP_ORD_RECHARGE_SHIELDS_UNIT, 0x42f640 }, { KP_ORD_BUILDING_LAND, 0x435bf0 },
	{ KP_ORD_BUILDING_LIFTOFF, 0x435f00 }, { KP_ORD_RESEARCH, 0x43ac40 }, { KP_ORD_UPGRADE, 0x43ace0 },
	{ KP_ORD_HARVEST3, 0x423c70 }, { KP_ORD_HARVEST4, 0x423cc0 },
	{ KP_ORD_RECHARGE_SHIELDS_UNIT_REMOVE_OVERLAY, 0x42f7e0 }, { KP_ORD_SIEGING, 0x436a80 },
	{ KP_ORD_UNSIEGING, 0x436be0 }, { KP_ORD_ARCHON_WARP, 0x42f830 }, { KP_ORD_ARCHON_SUMMON, 0x42f9e0 },
	{ KP_ORD_NUKE_TRAIN, 0x4356c0 }, { KP_ORD_INITIALIZE_ARBITER, 0x42f510 }, { KP_ORD_RESET_COLLISION, 0x41b1f0 },
	{ KP_ORD_RESET_HARVEST_COLLISION, 0x42e0d0 }, { KP_ORD_CTFCOP2, 0x4214d0 }, { KP_ORD_SELF_DESTRUCTING, 0x42a4d0 },
	{ KP_ORD_CRITTER, 0x427ed0 }, { KP_ORD_MEDIC_HEAL, 0x436250 }, { KP_ORD_HEAL_MOVE, 0x436310 },
	{ KP_ORD_MEDIC_HOLD_POSITION, 0x436430 }, { KP_ORD_MEDIC_HEAL_TO_IDLE, 0x436520 },
	{ KP_ORD_DARK_ARCHON_MELD, 0x42fa70 },
};
static const Entry PERIODIC[] = {
	{ KP_ORD_STOP, 0x428a10 }, { KP_ORD_GUARD, 0x41e700 }, { KP_ORD_PLAYER_GUARD, 0x41ee10 },
	{ KP_ORD_MOVE, 0x428660 }, { KP_ORD_REAVER_STOP, 0x41c910 }, { KP_ORD_MOVE_TO_ATTACK, 0x427c90 },
	{ KP_ORD_ATTACK_UNIT, 0x41e5c0 }, { KP_ORD_ATTACK_FIXED_RANGE, 0x41e400 }, { KP_ORD_ATTACK_TILE, 0x4be950 },
	{ KP_ORD_HOVER, 0x4be950 }, { KP_ORD_ATTACK_MOVE, 0x41e4a0 }, { KP_ORD_INFESTED_COMMAND_CENTER, 0x43c5a0 },
	{ KP_ORD_UNUSED_POWERUP, 0x427210 }, { KP_ORD_TOWER_GUARD, 0x41f090 }, { KP_ORD_TOWER_ATTACK, 0x41f100 },
	{ KP_ORD_SPIDER_MINE, 0x436cf0 }, { KP_ORD_STAY_IN_RANGE, 0x4288f0 }, { KP_ORD_NOTHING, INLINE },
	{ KP_ORD_DRONE_START_BUILD, 0x43ba90 }, { KP_ORD_CAST_INFESTATION, 0x43cf10 },
	{ KP_ORD_MOVE_TO_INFEST, 0x427c90 }, { KP_ORD_INFESTING_COMMAND_CENTER, 0x43cfe0 },
	{ KP_ORD_CREATE_PROTOSS_BUILDING, 0x42f230 }, { KP_ORD_MOVE_TO_REPAIR, 0x427c90 },
	{ KP_ORD_PLACE_ADDON, 0x435730 }, { KP_ORD_ZERG_BUILDING_MORPH, 0x43be80 }, { KP_ORD_BUILD_NYDUS_EXIT, 0x43bc50 },
	{ KP_ORD_ENTER_NYDUS_CANAL, 0x43ccc0 }, { KP_ORD_FOLLOW, 0x428310 }, { KP_ORD_CARRIER, 0x41c290 },
	{ KP_ORD_REAVER_CARRIER_MOVE, 0x428660 }, { KP_ORD_CARRIER_STOP, 0x41c410 }, { KP_ORD_CARRIER_ATTACK, 0x41c310 },
	{ KP_ORD_CARRIER_MOVE_TO_ATTACK, 0x427c90 }, { KP_ORD_CARRIER_IGNORE2, 0x41c3c0 },
	{ KP_ORD_CARRIER_FIGHT, 0x41c290 }, { KP_ORD_CARRIER_HOLD_POSITION, 0x41c290 }, { KP_ORD_REAVER, 0x41c790 },
	{ KP_ORD_REAVER_ATTACK, 0x41c860 }, { KP_ORD_REAVER_MOVE_TO_ATTACK, 0x427c90 }, { KP_ORD_REAVER_FIGHT, 0x41c790 },
	{ KP_ORD_REAVER_HOLD_POSITION, 0x41c790 }, { KP_ORD_INTERCEPTOR_ATTACK, 0x41c4e0 },
	{ KP_ORD_RECHARGE_SHIELDS_BATTERY, 0x42f550 }, { KP_ORD_INTERCEPTOR_RETURN, 0x41c450 },
	{ KP_ORD_DRONE_LAND, 0x43c970 }, { KP_ORD_LIFTING_OFF, 0x436030 }, { KP_ORD_LARVA, 0x43cd90 },
	{ KP_ORD_HARVEST1, 0x423480 }, { KP_ORD_HARVEST2, 0x427c90 }, { KP_ORD_MOVE_TO_GAS, 0x423480 },
	{ KP_ORD_WAIT_FOR_GAS, 0x4237a0 }, { KP_ORD_HARVEST_GAS, 0x4235c0 }, { KP_ORD_RETURN_GAS, 0x423ce0 },
	{ KP_ORD_MOVE_TO_MINERALS, 0x4239a0 }, { KP_ORD_WAIT_FOR_MINERALS, 0x423c30 },
	{ KP_ORD_MINING_MINERALS, 0x423810 }, { KP_ORD_RETURN_MINERALS, 0x423ce0 }, { KP_ORD_ENTER_TRANSPORT, 0x4376d0 },
	{ KP_ORD_PICKUP_IDLE, 0x437b50 }, { KP_ORD_PICKUP_TRANSPORT, 0x4377a0 }, { KP_ORD_PICKUP_BUNKER, 0x437850 },
	{ KP_ORD_TRANSPORT_REQUEST, 0x437a00 }, { KP_ORD_POWERUP_IDLE, 0x427290 }, { KP_ORD_WATCH_TARGET, 0x428a30 },
	{ KP_ORD_INIT_CREEP_GROWTH, 0x43cd50 }, { KP_ORD_STOPPING_CREEP_GROWTH, 0x43d250 },
	{ KP_ORD_HOLD_POSITION, 0x41e9b0 }, { KP_ORD_QUEEN_HOLD_POSITION, INLINE }, { KP_ORD_UNLOAD, 0x437ba0 },
	{ KP_ORD_MOVE_UNLOAD, 0x437890 }, { KP_ORD_FIRE_YAMATO_GUN, 0x434460 },
	{ KP_ORD_MOVE_TO_FIRE_YAMATO_GUN, 0x427c90 }, { KP_ORD_CAST_LOCKDOWN, 0x434460 }, { KP_ORD_BURROWING, 0x43c6f0 },
	{ KP_ORD_BURROWED, 0x43c810 }, { KP_ORD_UNBURROWING, 0x43c880 }, { KP_ORD_CAST_DARK_SWARM, 0x434460 },
	{ KP_ORD_CAST_PARASITE, 0x434460 }, { KP_ORD_CAST_SPAWN_BROODLINGS, 0x434460 },
	{ KP_ORD_CAST_EMP_SHOCKWAVE, 0x434460 }, { KP_ORD_NUKE_LAUNCH, 0x436540 }, { KP_ORD_NUKE_PAINT, 0x4366f0 },
	{ KP_ORD_NUKE_UNIT, 0x41eba0 }, { KP_ORD_CAST_NUCLEAR_STRIKE, 0x41ea40 }, { KP_ORD_PLACE_MINE, 0x436820 },
	{ KP_ORD_RIGHT_CLICK_ACTION, 0x431200 }, { KP_ORD_SUICIDE_UNIT, 0x41ef70 }, { KP_ORD_SUICIDE_LOCATION, 0x41eeb0 },
	{ KP_ORD_SUICIDE_HOLD_POSITION, INLINE }, { KP_ORD_CAST_RECALL, 0x42fc40 },
	{ KP_ORD_CAST_SCANNER_SWEEP, 0x436990 }, { KP_ORD_SCANNER, 0x436a60 }, { KP_ORD_CAST_DEFENSIVE_MATRIX, 0x438560 },
	{ KP_ORD_CAST_PSIONIC_STORM, 0x434460 }, { KP_ORD_CAST_IRRADIATE, 0x434460 }, { KP_ORD_CAST_PLAGUE, 0x434460 },
	{ KP_ORD_CAST_CONSUME, 0x434460 }, { KP_ORD_CAST_ENSNARE, 0x434460 }, { KP_ORD_CAST_STASIS_FIELD, 0x434460 },
	{ KP_ORD_CAST_HALLUCINATION, 0x430bd0 }, { KP_ORD_PATROL, 0x41ebe0 }, { KP_ORD_CTFCOP_INIT, 0x421440 },
	{ KP_ORD_CTFCOP_STARTED, 0x4213c0 }, { KP_ORD_COMPUTER_AI, 0x48cd70 }, { KP_ORD_AI_ATTACK_MOVE, 0x41e510 },
	{ KP_ORD_HARASS_MOVE, 0x41e890 }, { KP_ORD_AI_PATROL, 0x48cee0 }, { KP_ORD_AI_GUARD, 0x41e7f0 },
	{ KP_ORD_RESCUE_PASSIVE, 0x48d120 }, { KP_ORD_NEUTRAL, 0x48cfb0 }, { KP_ORD_COMPUTER_RETURN, 0x41e370 },
	{ KP_ORD_INITIALIZE_PSI_PROVIDER, 0x42f5b0 }, { KP_ORD_HIDDEN_GUN, 0x428490 }, { KP_ORD_OPEN_DOOR, 0x4281a0 },
	{ KP_ORD_CLOSE_DOOR, 0x428150 }, { KP_ORD_HIDE_TRAP, 0x4285a0 }, { KP_ORD_REVEAL_TRAP, 0x4288a0 },
	{ KP_ORD_ENABLE_DOODAD, 0x4281f0 }, { KP_ORD_DISABLE_DOODAD, 0x428010 }, { KP_ORD_MEDIC_IDLE, 0x4361b0 },
	{ KP_ORD_CAST_RESTORATION, 0x434460 }, { KP_ORD_CAST_DISRUPTION_WEB, 0x434460 },
	{ KP_ORD_CAST_MIND_CONTROL, 0x430ce0 }, { KP_ORD_CAST_FEEDBACK, 0x430d90 },
	{ KP_ORD_CAST_OPTICAL_FLARE, 0x434460 }, { KP_ORD_CAST_MAELSTROM, 0x434460 }, { KP_ORD_JUNKYARD_DOG, 0x4286f0 },
};
/* DispatchHiddenOrder 0x439e20: IDLE becomes BunkerGuard/Nothing, NONE does nothing. */
#define HIDDEN_TO_IDLE (-2)
#define HIDDEN_NONE (-3)
static const Entry HIDDEN_FIRST[] = {
	{ KP_ORD_DIE, 0x41e630 }, { KP_ORD_PLAYER_GUARD, HIDDEN_TO_IDLE }, { KP_ORD_TURRET_GUARD, HIDDEN_TO_IDLE },
	{ KP_ORD_UNUSED_POWERUP, 0x427210 }, { KP_ORD_TURRET_ATTACK, HIDDEN_TO_IDLE }, { KP_ORD_NOTHING, HIDDEN_NONE },
	{ KP_ORD_NOTHING_WAIT, HIDDEN_NONE }, { KP_ORD_INFESTING_COMMAND_CENTER, 0x43cfe0 },
	{ KP_ORD_HARVEST_GAS, 0x4235c0 }, { KP_ORD_ENTER_TRANSPORT, HIDDEN_TO_IDLE }, { KP_ORD_POWERUP_IDLE, 0x427290 },
	{ KP_ORD_NUKE_LAUNCH, 0x436540 }, { KP_ORD_RESET_COLLISION, 0x41b1f0 },
	{ KP_ORD_RESET_HARVEST_COLLISION, 0x42e0d0 }, { KP_ORD_NEUTRAL, HIDDEN_NONE }, { KP_ORD_MEDIC_IDLE, HIDDEN_NONE },
	{ KP_ORD_MEDIC_HEAL, HIDDEN_NONE },
};
static const Entry HIDDEN_PERIODIC[] = {
	{ KP_ORD_BUNKER_GUARD, 0x41e760 }, { KP_ORD_TRANSPORT_REQUEST, 0x437a00 }, { KP_ORD_COMPUTER_AI, 0x48d2d0 },
	{ KP_ORD_RESCUE_PASSIVE, 0x48d120 },
};
static const Entry SECONDARY[] = {
	{ KP_ORD_BUILD_ADDON, 0x439310 }, { KP_ORD_TRAIN, 0x41b060 }, { KP_ORD_TRAIN_FIGHTER, 0x41c630 },
	{ KP_ORD_SHIELD_BATTERY, 0x42f770 }, { KP_ORD_SPAWNING_LARVA, 0x43d0d0 }, { KP_ORD_SPREAD_CREEP, 0x43d1a0 },
	{ KP_ORD_CLOAK_NEARBY_UNITS, 0x434710 },
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
	if (kp_controller(g, u->owner[n]) == KP_CTRL_COMPUTER) {
		u->order[n] = KP_ORD_COMPUTER_AI;
		if (u->ai[n] != 0) {
			if (kp_ai_unit_record_type(g, n) == KP_AI_GUARD) u->order[n] = KP_ORD_AI_GUARD; /* the record's type +8 */
			return;
		}
		if (kp_band(flags(g, u->type[n]), DAT_HERO) == 0) return;
	}
	u->order[n] = KP_ORD_PLAYER_GUARD;
}

static void flingy_set_target_waypoint(KpGame *g, int64_t n, int64_t x, int64_t y)
{
	kp_flingy_set_target_waypoint(g, n, x, y);
}

/* TryAttackAutoTarget 0x41dce0: a unit without AI attacks its remembered hostile attacker. */
int kp_try_attack_auto_target(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t target = u->auto_target[n];
	if (target == 0 || u->ai[n] != 0) return 0;
	if (kp_is_enemy(g, n, target) && kp_can_attack(g, n, target, 1)) {
		kp_issue_attack(g, n, target, 1, 0);
		u->status[n] = kp_bor(u->status[n], STATUS_AUTO_ATTACK);
		return 1;
	}
	u->auto_target[n] = 0;
	return 0;
}

/* Order_PlayerGuard 0x41ee10. */
static void order_player_guard(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	if (kp_try_attack_auto_target(g, n)) return;
	if (u->order_timer[n] != 0) return;
	u->order_timer[n] = SEEK_PERIOD;
	if (kp_band(flags(g, u->type[n]), DAT_SUBUNIT) != 0) {
		int64_t base = u->subunit[n];
		flingy_set_target_waypoint(g, n, u->target_waypoint_x[base], u->target_waypoint_y[base]);
	}
	if (kp_seek_range(g, n) != 0) {
		int64_t target = kp_acquire_target(g, n);
		if (target != 0) kp_issue_attack(g, n, target, 1, 0);
	}
}

/* OrderStop 0x428a10. */
static void order_stop(KpGame *g, int64_t n)
{
	kp_stop_and_face(g, n);
	kp_reset_to_idle_animation(g, n);
	kp_order_done(g, n);
}

#define STATUS_BUNKER_ATTACKER 0x8000000

/* Order_BunkerGuard 0x41e760 (order 5 of a unit inside a Bunker): the units that can fire from it attack in place. */
static void order_bunker_guard(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t t = u->type[n], target;
	if (t != KP_TYPE_MARINE && t != KP_TYPE_JIM_RAYNOR_MARINE && t != KP_TYPE_GHOST && t != KP_TYPE_SARAH_KERRIGAN
		&& t != KP_TYPE_ALEXEI_STUKOV && t != KP_TYPE_SAMIR_DURAN && t != KP_TYPE_INFESTED_DURAN && t != KP_TYPE_FIREBAT
		&& t != KP_TYPE_GUI_MONTAG) return;
	u->status[n] = kp_bor(u->status[n], STATUS_BUNKER_ATTACKER);
	if (u->subunit[n] != 0) u->status[u->subunit[n]] = kp_bor(u->status[u->subunit[n]], STATUS_BUNKER_ATTACKER);
	if (kp_attack_in_place(g, n)) {
		kp_flingy_set_target_waypoint(g, n, u->order_x[n], u->order_y[n]);
		return;
	}
	if (u->order_timer[n] != 0) return;
	u->order_timer[n] = SEEK_PERIOD;
	target = kp_acquire_random_target(g, n); /* 0x432060 */
	u->order_unit[n] = target;
	if (target != 0) u->queue_timer[n] = 0;
}

#define STATUS_GROUND_COLLIDES 0x100000
#define STATUS_IGNORE_TILE_COLLISION 0x800000

/* OrderResetCollision 0x41b1f0 (order 150): the unit's elevation and collision again, then the next order. */
static void order_reset_collision(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	g->w->spr.elevation[kp_unit_sprite(g, n)] = (int32_t)KP_DAT(g, units, elevation_level, u->type[n]);
	u->status[n] = kp_bor(kp_band(u->status[n], kp_bnot(STATUS_NO_COLLIDE | STATUS_IGNORE_TILE_COLLISION)),
		STATUS_GROUND_COLLIDES);
	kp_update_overlap_flags(g, n); /* 0x42e600 */
	kp_reset_movement(g, n);
	if (u->queue_head[n] == KP_NONE) kp_order_queue(g, n, KP_DAT(g, units, return_to_idle, u->type[n]), 0, 0, 0, KP_TYPE_NONE);
	kp_order_unlock_done(g, n);
}

/* Order_HoldPosition 0x41e9b0. */
static void order_hold_position(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t target, sub;
	if (u->order_state[n] == 0) {
		kp_stop_and_face(g, n);
		kp_set_target_switch(g, n);
		u->order_state[n] = 1;
	}
	if (kp_attack_in_place(g, n)) {
		sub = u->subunit[n];
		if (sub == 0 || kp_band(KP_DAT(g, units, special_ability_flags, u->type[sub]), DAT_SUBUNIT) == 0
			|| u->type[n] == KP_TYPE_GOLIATH || u->type[n] == KP_TYPE_ALAN_SCHEZAR) {
			kp_flingy_set_target_waypoint(g, n, u->order_x[n], u->order_y[n]);
		}
		return;
	}
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
int kp_move_toward_target(KpGame *g, int64_t n, int64_t target)
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
		if (tt == KP_TYPE_NYDUS_CANAL && kp_can_enter_nydus(g, n, target)) {
			kp_issue_order_target(g, n, KP_ORD_ENTER_NYDUS_CANAL, target);
			return;
		}
		if (tt == KP_TYPE_SHIELD_BATTERY && kp_can_recharge_from(g, n, target)) {
			kp_issue_order_target(g, n, KP_ORD_RECHARGE_SHIELDS_UNIT, target);
			return;
		}
		u->order_state[n] = 1;
	}
	if (u->order_timer[n] != 0) return;
	u->order_timer[n] = FOLLOW_PERIOD;
	owner = u->owner[target];
	if (owner == KP_PLAYER_NEUTRAL) owner = g->w->spr.owner[kp_unit_sprite(g, target)];
	if (kp_alliance(g, u->owner[n], owner) == 0 && kp_can_attack(g, n, target, 1)) {
		kp_issue_order(g, n, KP_ORD_GUARD);
		return;
	}
	if (kp_band(u->status[n], STATUS_AIR) == 0 && !kp_same_ground_group(g, n, target)) {
		kp_stop_and_face(g, n);
		return;
	}
	ctl = kp_controller(g, u->owner[target]);
	if ((ctl == KP_CTRL_RESCUE || ctl == KP_CTRL_RESCUE_ACTIVE) && !unit_box_within(g, n, 1, target)) {
		kp_request_move_to_unit(g, n, target);
		return;
	}
	if (!unit_box_within(g, n, FOLLOW_RANGE, target)) kp_move_toward_target(g, n, target);
	else if (kp_band(u->status[target], STATUS_GROUNDED_BUILDING) != 0 || u->queue_head[n] != -1) kp_order_done(g, n);
	else kp_stop_and_face(g, n);
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

/* OrderAttackObscured 0x427c90 (orders 9, 28, 35, 54, 60, 80 and 114: an order aimed at a remembered unit of the fog):
 * the unit moves to the point (one that cannot move waits at once); once the owner sees the point, the order is
 * replaced by its counterpart (orders.dat obscured) aimed at the unit of the remembered type that stands there, and
 * ends. An Attack puts the unit's AttackUnit order first, and its turret's too. */
static void order_attack_obscured(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t x = kp_s16v(u->order_x[n]), y = kp_s16v(u->order_y[n]);
	if (u->order_state[n] == 0) {
		if (kp_band(u->status[n], STATUS_CAN_MOVE) == 0 || kp_request_move_to_point(g, n, x, y)) u->order_state[n] = 1; /* 0x43a070 */
	} else if (u->order_state[n] == 1 && kp_point_visible_to(g, x, y, u->owner[n])) {
		int64_t found = kp_fog_unit(g, x, y, u->order_type[n]); /* 0x418210 */
		if (found != 0) {
			int64_t order = KP_DAT(g, orders, obscured_order, u->order[n]), sub = u->subunit[n];
			if (order == KP_ORD_ATTACK_DEFAULT) {
				order = KP_DAT(g, units, attack_unit, u->type[n]);
				if (kp_is_subunit(g, sub)) {
					kp_order_prepend_target(g, sub, KP_DAT(g, units, attack_unit, u->type[sub]), found); /* 0x429bc0 */
					kp_order_done(g, sub); /* 0x42a5a0 */
				}
			}
			kp_order_prepend_target(g, n, order, found);
		}
		kp_order_done(g, n);
	}
}

/* The dispatcher's inline QueenHoldPosition and SuicideHoldPosition (0x439420, orders 108 and 136; a Queen, a Scourge
 * and an Infested Terran hold their position by them, GetHoldPositionOrder 0x41fae0): the unit stops and faces its
 * point once, then waits for the queue like Nothing. */
static void order_hold_once(KpGame *g, int64_t n)
{
	if (g->u.order_state[n] == 0) {
		kp_stop_and_face(g, n); /* 0x43a4e0 */
		g->u.order_state[n] = 1;
	}
	order_nothing(g, n);
}

/* Order_TurretGuard 0x41f300. */
static void order_turret_guard(KpGame *g, int64_t n)
{
	int64_t base = g->u.subunit[n];
	flingy_set_target_waypoint(g, n, g->u.target_waypoint_x[base], g->u.target_waypoint_y[base]);
	order_player_guard(g, n);
}

static void rescued_order(KpGame *g, int64_t n, int64_t owner)
{
	KpUnits *u = &g->u;
	kp_change_owner(g, n, owner, 1);
	kp_issue_order(g, n, kp_band(u->status[n], STATUS_IN_BUNKER) != 0 ? KP_ORD_BUNKER_GUARD : KP_DAT(g, units, human_ai_idle, u->type[n]));
}

static int hatchery_complete(int64_t t) { return t == KP_TYPE_LAIR || t == KP_TYPE_HIVE; }

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
		if (kp_controller(g, u->owner[found[k]]) == KP_CTRL_HUMAN) {
			rescuer = found[k];
			break;
		}
	}
	if (rescuer == 0) return;
	owner = u->owner[rescuer];
	t = u->type[n];
	status = u->status[n];
	if ((kp_band(status, 1) != 0 || hatchery_complete(t) || (t == KP_TYPE_HATCHERY && kp_is_morphing_zerg_building(g, n)))
		&& kp_band(status, STATUS_GROUNDED_BUILDING) != 0 && kp_band(flags(g, t), DAT_RESOURCE_DEPOT) != 0) {
		int64_t m = g->player_units[u->owner[n]];
		while (m != 0) {
			int64_t next = u->player_next[m];
			if (!kp_unit_inactive(g, m)) rescued_order(g, m, owner);
			m = next;
		}
	} else {
		rescued_order(g, n, owner);
	}
	if (u->owner[n] == g->local_player && !g->w->replay) { /* PlayRescueSound 0x446b30: 0x446940 with [1, 0] */
		g->w->spr.sel_timer[s] = RESCUE_FLASH;
		kp_play_unit_sound(g, RESCUE_SOUND + g->players.race[g->local_player], n);
	}
	g->interface_refresh++; /* RefreshStatusUi 0x4a7ed0, for the interface */
}

void kp_set_cloak_bits(KpGame *g, int64_t n, int64_t v)
{
	g->u.status[n] = kp_bor(kp_band(g->u.status[n], kp_bnot(0x300)), v * 256);
}

static int floor_trap(int64_t t) { return t == KP_TYPE_FLOOR_MISSILE_TRAP || t == KP_TYPE_FLOOR_GUN_TRAP; }

/* OrderTrap 0x428490. */
static void order_trap(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t state = u->order_state[n], target;
	int32_t s;
	if (state == 0) {
		u->status[n] = kp_bor(u->status[n], STATUS_NO_COLLIDE);
		if (floor_trap(u->type[n])) g->w->spr.elevation[kp_unit_sprite(g, n)] = 0;
		kp_set_cloak_bits(g, n, 3);
		u->visibility[n] = DETECTED_BY_NONE;
		u->secondary_timer[n] = 0;
		u->order_state[n] = 1;
	} else if (state == 1 && !kp_unit_disabled(g, n) && u->order_timer[n] == 0) {
		u->order_timer[n] = SEEK_PERIOD;
		target = kp_acquire_random_target(g, n);
		if (target == 0) return;
		u->status[n] = kp_band(u->status[n], kp_bnot(STATUS_NO_COLLIDE));
		if (floor_trap(u->type[n])) {
			g->w->spr.elevation[kp_unit_sprite(g, n)] = (int32_t)KP_DAT(g, units, elevation_level, u->type[n]);
		}
		kp_set_cloak_bits(g, n, 0);
		u->secondary_timer[n] = 0;
		kp_issue_order(g, n, KP_ORD_REVEAL_TRAP);
		s = kp_unit_sprite(g, target);
		kp_order_append(g, n, KP_DAT(g, units, attack_unit, u->type[n]), kp_s16v(g->w->spr.x[s]), kp_s16v(g->w->spr.y[s]),
			target, KP_TYPE_NONE, 0);
		kp_order_append(g, n, KP_ORD_HIDE_TRAP, 0, 0, 0, KP_TYPE_NONE, 0);
	}
}

/* The end of a doodad's order: the stock idle when nothing is queued, then UnlockAndEndOrder 0x429d00. */
static void doodad_idle(KpGame *g, int64_t n)
{
	if (g->u.queue_head[n] == KP_NONE) kp_issue_order(g, n, KP_DAT(g, units, return_to_idle, g->u.type[n]));
	kp_order_unlock_done(g, n);
}

/* The end of RevealTrap and HideTrap: on the animation's signal the trap goes idle when nothing is queued. */
static void trap_animation_done(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	if (kp_band(u->order_signal[n], DOODAD_SIGNAL) == 0) return;
	u->order_signal[n] = kp_band(u->order_signal[n], kp_bnot(DOODAD_SIGNAL));
	doodad_idle(g, n);
}

/* OrderRevealTrap 0x4288a0 (order 171). */
static void order_reveal_trap(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	if (u->order_state[n] == 0) {
		kp_sprite_play(g->w, kp_unit_sprite(g, n), KP_ANIM_SPECIAL1, 1);
		u->order_signal[n] = kp_band(u->order_signal[n], kp_bnot(DOODAD_SIGNAL));
		u->order_state[n] = 1;
	} else if (u->order_state[n] == 1) {
		trap_animation_done(g, n);
	}
}

/* OrderHideTrap 0x4285a0 (order 170): a trap with a new target attacks again, else it hides. */
static void order_hide_trap(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t target;
	if (u->order_state[n] == 0) {
		if (!kp_unit_disabled(g, n) && (target = kp_acquire_random_target(g, n)) != 0) {
			kp_issue_order_target(g, n, KP_DAT(g, units, attack_unit, u->type[n]), target);
			kp_order_append(g, n, KP_ORD_HIDE_TRAP, 0, 0, 0, KP_TYPE_NONE, 0);
			kp_order_unlock_done(g, n);
			return;
		}
		kp_sprite_play(g->w, kp_unit_sprite(g, n), KP_ANIM_SPECIAL2, 1);
		u->order_signal[n] = kp_band(u->order_signal[n], kp_bnot(DOODAD_SIGNAL));
		u->order_state[n] = 1;
	} else if (u->order_state[n] == 1) {
		trap_animation_done(g, n);
	}
}

/* The start of EnableDoodad 0x4281f0 and DisableDoodad 0x428010 (atlas P09, docs/research/native-special-orders.md):
 * 0 when the order waits (a disabled doodad goes idle, one in a no-break script section tries again); else the
 * animation plays and a floor trap or a door takes elevation (KP_NIL for the stock one). */
static int doodad_switch(KpGame *g, int64_t n, int32_t anim, int64_t elevation)
{
	KpUnits *u = &g->u;
	int64_t t = u->type[n];
	int32_t s;
	if (kp_unit_disabled(g, n)) {
		doodad_idle(g, n);
		return 0;
	}
	if (kp_band(u->status[n], STATUS_NOBRK) != 0) return 0;
	s = kp_unit_sprite(g, n);
	kp_sprite_play(g->w, s, anim, 1);
	if (floor_trap(t) || kp_door_type(t)) {
		g->w->spr.elevation[s] = (int32_t)(elevation == KP_NIL ? KP_DAT(g, units, elevation_level, t) : elevation);
	}
	return 1;
}

/* A door's part of EnableDoodad and DisableDoodad: its sound (PlaySoundAtUnit 0x446940) and the order that waits for
 * the animation's signal before the queue. */
static void door_switch(KpGame *g, int64_t n, int64_t sound, int64_t order)
{
	kp_play_unit_sound(g, sound, n);
	kp_order_prepend(g, n, order);
	kp_order_unlock_done(g, n);
}

/* OrderDisableDoodad 0x428010 (order 173): a door opens after the Disable animation, a trap hides and turns off. */
static void order_disable_doodad(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t t = u->type[n];
	if (!doodad_switch(g, n, KP_ANIM_DISABLE, OPEN_DOOR_ELEVATION)) return;
	if (kp_door_type(t)) {
		door_switch(g, n, KP_DAT(g, units, what_sound_start, t), KP_ORD_OPEN_DOOR);
		return;
	}
	kp_set_cloak_bits(g, n, 3);
	u->status[n] = kp_bor(u->status[n], STATUS_DISABLED);
	u->visibility[n] = DETECTED_BY_NONE;
	u->secondary_timer[n] = 0;
	doodad_idle(g, n);
}

/* OrderEnableDoodad 0x4281f0 (order 172): a door closes after the Enable animation, a trap goes back to its order. */
static void order_enable_doodad(KpGame *g, int64_t n)
{
	int64_t t = g->u.type[n];
	if (!doodad_switch(g, n, KP_ANIM_ENABLE, KP_NIL)) return;
	if (kp_door_type(t)) {
		door_switch(g, n, KP_DAT(g, units, what_sound_end, t), KP_ORD_CLOSE_DOOR);
		return;
	}
	doodad_idle(g, n);
}

/* OrderOpenDoor 0x4281a0 (order 168): on the Disable animation's signal the door stops colliding and turns off. */
static void order_open_door(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	if (kp_band(u->order_signal[n], DOODAD_SIGNAL) == 0) return;
	u->status[n] = kp_bor(u->status[n], STATUS_NO_COLLIDE);
	u->order_signal[n] = kp_band(u->order_signal[n], kp_bnot(DOODAD_SIGNAL));
	g->w->spr.elevation[kp_unit_sprite(g, n)] = OPEN_DOOR_ELEVATION;
	kp_issue_order(g, n, KP_DAT(g, units, return_to_idle, u->type[n]));
	u->status[n] = kp_bor(u->status[n], STATUS_DISABLED);
	kp_order_unlock_done(g, n);
	g->ai.terrain_changed = 1; /* 0x48dd40 */
}

/* OrderCloseDoor 0x428150 (order 169): on the Enable animation's signal the door collides again. */
static void order_close_door(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	if (kp_band(u->order_signal[n], DOODAD_SIGNAL) == 0) return;
	u->order_signal[n] = kp_band(u->order_signal[n], kp_bnot(DOODAD_SIGNAL));
	u->status[n] = kp_band(u->status[n], kp_bnot(STATUS_NO_COLLIDE));
	g->w->spr.elevation[kp_unit_sprite(g, n)] = (int32_t)KP_DAT(g, units, elevation_level, u->type[n]);
	kp_issue_order(g, n, KP_DAT(g, units, return_to_idle, u->type[n]));
	kp_order_unlock_done(g, n);
	kp_update_overlap_flags(g, n);
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
	if (target == 0) return;
	kp_issue_order_target(g, n, KP_ORD_TOWER_ATTACK, target);
	kp_set_target_switch(g, n);
}

/* Order_TowerAttack 0x41f100. */
static void order_tower_attack(KpGame *g, int64_t n)
{
	int32_t s;
	if (kp_unit_disabled(g, n) || !kp_attack_in_place(g, n)) {
		kp_order_done(g, n);
		return;
	}
	s = kp_unit_sprite(g, n);
	kp_set_move_target_point(g, n, kp_s16v(g->w->spr.x[s]), kp_s16v(g->w->spr.y[s]));
	kp_flingy_set_target_waypoint(g, n, g->u.order_x[n], g->u.order_y[n]);
}

static int attackable_auto_target(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t a = u->auto_target[n];
	return a != 0 && kp_is_enemy(g, n, a) && kp_band(g->w->spr.flags[kp_unit_sprite(g, a)], 0x20) == 0
		&& kp_band(u->status[a], STATUS_INVINCIBLE) == 0 && !kp_is_undetected_by(g, a, n);
}

/* Order at target, inserted before the queue's head (0x429da0). */
static void insert_at_unit(KpGame *g, int64_t n, int64_t order, int64_t target)
{
	int32_t s = kp_unit_sprite(g, target);
	kp_order_insert_before(g, n, order, kp_s16v(g->w->spr.x[s]), kp_s16v(g->w->spr.y[s]), target, KP_TYPE_NONE, g->u.queue_head[n]);
}

/* The attack order of the unit's type against target, inserted before the queue's head. */
static void insert_attack(KpGame *g, int64_t n, int64_t target)
{
	insert_at_unit(g, n, KP_DAT(g, units, attack_unit, g->u.type[n]), target);
}

/* The current order stays queued at its target (0x429c40) while the unit attacks target first (0x429da0, 0x429d00). */
static void attack_first(KpGame *g, int64_t n, int64_t order, int64_t target)
{
	KpUnits *u = &g->u;
	kp_stop_movement(g, n);
	kp_order_prepend_at(g, n, order, u->order_x[n], u->order_y[n], u->order_unit[n]);
	insert_attack(g, n, target);
}

/* AttackMoveReaction 0x420600: 1 when the caller goes on. */
static int attack_move_reaction(KpGame *g, int64_t n, int64_t order)
{
	KpUnits *u = &g->u;
	if (u->order_state[n] == 0) {
		if (!kp_request_move_to_point(g, n, u->order_x[n], u->order_y[n])) return 0;
		u->order_state[n] = 1;
	}
	if (!attackable_auto_target(g, n)) return 1;
	if (u->ai[n] != 0) return !kp_ai_update_attack_target(g, n, 0, 0, 0);
	attack_first(g, n, order, u->auto_target[n]);
	u->auto_target[n] = 0;
	kp_order_unlock_done(g, n);
	kp_set_target_switch(g, n);
	return 0;
}

/* AcquireAndQueueAttack 0x4206e0. */
static void acquire_and_queue_attack(KpGame *g, int64_t n, int64_t order)
{
	KpUnits *u = &g->u;
	int64_t c;
	if (kp_seek_range(g, n) == 0) return;
	if (u->ai[n] != 0) {
		kp_ai_update_attack_target(g, n, 0, 0, 0);
		return;
	}
	c = kp_acquire_target(g, n);
	if (c == 0) return;
	attack_first(g, n, order, c);
	kp_order_unlock_done(g, n);
	kp_set_target_switch(g, n);
}

/* Order_AttackMove 0x41e4a0. */
static void order_attack_move(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int32_t s;
	if (!attack_move_reaction(g, n, KP_ORD_ATTACK_MOVE) || u->order_timer[n] != 0) return;
	u->order_timer[n] = SEEK_PERIOD;
	s = kp_unit_sprite(g, n);
	if (u->move_x[n] == kp_s16v(g->w->spr.x[s]) && u->move_y[n] == kp_s16v(g->w->spr.y[s])) {
		kp_order_done(g, n);
		return;
	}
	acquire_and_queue_attack(g, n, KP_ORD_ATTACK_MOVE);
}

/* Order_AiAttackMove 0x41e510 (order 157): an AI unit's attack move, done in the region of its point; it stops when
 * the move there fails and the point is unreachable. */
static void order_ai_attack_move(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t x, y;
	int32_t s;
	if (!attack_move_reaction(g, n, KP_ORD_AI_ATTACK_MOVE) || u->order_timer[n] != 0) return;
	u->order_timer[n] = SEEK_PERIOD;
	x = kp_s16v(u->order_x[n]);
	y = kp_s16v(u->order_y[n]);
	if (kp_band(kp_region_at(g, x, y), 0xffff) == kp_band(kp_unit_region(g, n), 0xffff)) {
		kp_stop_and_face(g, n);
		kp_order_done(g, n);
		return;
	}
	s = kp_unit_sprite(g, n);
	if (move_arrival(g, n) != 0 && (x != kp_s16v(g->w->spr.x[s]) || y != kp_s16v(g->w->spr.y[s]))
		&& !kp_request_move_to_point(g, n, x, y)) {
		if (kp_point_unreachable(g, n, x, y)) kp_stop_and_face(g, n);
		return;
	}
	acquire_and_queue_attack(g, n, KP_ORD_AI_ATTACK_MOVE);
}

/* Order_HarassMove 0x41e890 (order 158): an AI group's move; its leader waits for stragglers, a follower chases the
 * leader, all wait for a pending pickup; the order ends in the target's region within 96 of the point. */
static void order_harass_move(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t leader, x, y, sx, sy;
	int32_t s;
	if (!attack_move_reaction(g, n, KP_ORD_HARASS_MOVE) || u->order_timer[n] != 0) return;
	u->order_timer[n] = SEEK_PERIOD;
	leader = kp_ai_group_slowest(g, n);
	if (leader == n) {
		if (kp_ai_leader_should_wait(g, n)) {
			kp_stop_and_face(g, n);
			return;
		}
	} else if (leader != 0 && !kp_ai_follower_ready(g, n)) {
		kp_set_move_target_unit(g, n, leader); /* SetMoveTargetUnit 0x43a180 */
		return;
	} else if (kp_ai_group_pending_pickup(g, n)) {
		kp_stop_and_face(g, n);
		return;
	}
	kp_ai_rejoin_region(g, n);
	if (move_arrival(g, n) == 0) {
		acquire_and_queue_attack(g, n, KP_ORD_HARASS_MOVE);
		return;
	}
	x = kp_s16v(u->order_x[n]);
	y = kp_s16v(u->order_y[n]);
	s = kp_unit_sprite(g, n);
	sx = kp_s16v(g->w->spr.x[s]);
	sy = kp_s16v(g->w->spr.y[s]);
	if (kp_band(kp_region_at(g, x, y), 0xffff) == kp_band(kp_unit_region(g, n), 0xffff)
		&& ((x == sx && y == sy) || kp_s32v(kp_approx_distance(x - sx, y - sy)) > HARASS_REACH)) {
		kp_order_done(g, n);
		acquire_and_queue_attack(g, n, KP_ORD_HARASS_MOVE);
		return;
	}
	if (kp_request_move_to_point(g, n, x, y)) acquire_and_queue_attack(g, n, KP_ORD_HARASS_MOVE);
}

/* Patrol's attack (or a Medic's heal): the Patrol order stays queued before it (0x429e90, 0x429da0). */
static void patrol_attack(KpGame *g, int64_t n, int64_t order, int64_t target, int clear_auto_target)
{
	KpUnits *u = &g->u;
	kp_order_insert_before(g, n, KP_ORD_PATROL, u->order_x[n], u->order_y[n], u->order_unit[n], KP_TYPE_NONE, u->queue_head[n]);
	insert_at_unit(g, n, order, target);
	if (clear_auto_target) u->auto_target[n] = 0;
	kp_order_unlock_done(g, n);
	kp_set_target_switch(g, n);
}

/* Order_Patrol 0x41ebe0. */
static void order_patrol(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int32_t s = kp_unit_sprite(g, n);
	int64_t x, y, c;
	if (u->order_state[n] == 0) {
		int64_t head = u->queue_head[n];
		if (head == -1 || g->orders.order[head] != KP_ORD_PATROL) {
			kp_issue_order_packed(g, n, KP_ORD_PATROL, u->order_x[n], u->order_y[n], u->order_unit[n]);
			kp_order_queue(g, n, KP_ORD_PATROL, kp_s16v(g->w->spr.x[s]), kp_s16v(g->w->spr.y[s]), 0, KP_TYPE_NONE);
			return;
		}
		if (!kp_request_move_to_point(g, n, u->order_x[n], u->order_y[n])) return;
		u->order_state[n] = 1;
	}
	if (attackable_auto_target(g, n)) {
		patrol_attack(g, n, KP_DAT(g, units, attack_unit, u->type[n]), u->auto_target[n], 1);
		return;
	}
	if (u->order_timer[n] != 0) return;
	u->order_timer[n] = SEEK_PERIOD;
	if (u->type[n] == KP_TYPE_MEDIC) {
		c = kp_find_heal_target(g, n);
		if (c != 0 && u->order_unit[n] == 0) {
			patrol_attack(g, n, KP_ORD_MEDIC_HEAL, c, 1);
			return;
		}
	}
	x = kp_s16v(g->w->spr.x[s]);
	y = kp_s16v(g->w->spr.y[s]);
	if (u->move_x[n] == x && u->move_y[n] == y) {
		if (move_arrival(g, n) == 2) {
			x = u->order_x[n];
			y = u->order_y[n];
		}
		kp_order_queue(g, n, KP_ORD_PATROL, x, y, 0, KP_TYPE_NONE);
		kp_order_unlock_done(g, n);
		return;
	}
	if (kp_seek_range(g, n) != 0 && (c = kp_acquire_target(g, n)) != 0) {
		patrol_attack(g, n, KP_DAT(g, units, attack_unit, u->type[n]), c, 0);
	}
}

/* OrderWatchTarget 0x428a30 (order 100): a sieged tank keeps its target while it can reach it. */
static void order_watch_target(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t t = u->order_unit[n];
	if (t != 0 && kp_can_attack(g, n, t, 1) && (kp_target_in_range(g, n, 0) || kp_right_click_action(g, n) != 3)) return;
	/* ReturnToIdleOrAiAt 0x42a530 */
	if (u->queue_head[n] == KP_NONE) {
		u->user_action[n] = kp_bor(u->user_action[n], 1);
		if (u->ai[n] != 0) kp_order_append(g, n, KP_ORD_COMPUTER_AI, 0, 0, 0, KP_TYPE_NONE, 1);
		else kp_order_append(g, n, KP_DAT(g, units, return_to_idle, u->type[n]), kp_s16v(u->order_x[n]), kp_s16v(u->order_y[n]), 0, KP_TYPE_NONE, 1);
	}
	kp_order_activate(g, n);
}

/* The indexed subunit (a turret) of n attacks target too. */
static void attack_with_subunit(KpGame *g, int64_t n, int64_t target)
{
	int64_t sub = g->u.subunit[n];
	if (kp_is_subunit(g, sub)) kp_issue_order_target(g, sub, KP_DAT(g, units, attack_unit, g->u.type[sub]), target);
}

/* Order_Neutral 0x48cfb0: when attacked, the owner becomes a computer player hostile to the attacker. */
static void order_neutral(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t attacker = u->auto_target[n];
	if (attacker == 0) return;
	if (kp_band(g->w->spr.flags[kp_unit_sprite(g, attacker)], 0x20) == 0 && !kp_is_undetected_by(g, attacker, n)
		&& u->owner[n] != u->owner[attacker]) {
		int64_t owner = u->owner[n], m;
		g->players.controller[owner] = KP_CTRL_COMPUTER;
		g->players.alliance[owner][u->owner[attacker]] = 0;
		g->players.alliance[u->owner[attacker]][owner] = 0;
		if (kp_can_attack(g, n, attacker, 1)) {
			kp_clear_order_target(g, n);
			kp_issue_order_target(g, n, KP_DAT(g, units, attack_unit, u->type[n]), u->auto_target[n]);
			attack_with_subunit(g, n, u->auto_target[n]);
		} else {
			kp_issue_order(g, n, KP_DAT(g, units, return_to_idle, u->type[n]));
		}
		kp_assign_unit_ai(g, n);
		/* the owner's other units wake up */
		for (m = g->player_units[owner]; m != 0; m = u->player_next[m]) {
			if (kp_unit_inactive(g, m) || kp_band(KP_DAT(g, units, special_ability_flags, u->type[m]), DAT_SUBUNIT) != 0 || m == n) {
				continue;
			}
			if (kp_band(u->status[m], STATUS_IN_TRANSPORT) == 0) kp_issue_order(g, m, KP_DAT(g, units, return_to_idle, u->type[m]));
			else if (kp_band(u->status[m], STATUS_IN_BUNKER) != 0) kp_issue_order(g, m, KP_ORD_BUNKER_GUARD);
			attack_with_subunit(g, m, u->auto_target[n]);
			kp_assign_unit_ai(g, m);
		}
	}
	u->auto_target[n] = 0;
}

/* Order_Burrowed 0x43c810. */
static void order_burrowed(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t t = u->type[n], head;
	if (t == KP_TYPE_LURKER) {
		kp_order_unlock_done(g, n);
		return;
	}
	head = u->queue_head[n];
	if (t == KP_TYPE_HYDRALISK && head != -1 && g->orders.order[head] == KP_ORD_ZERG_UNIT_MORPH) {
		kp_game_fail(g, "orders: burrowed Hydralisk morph (0x43c810)");
	}
	if (u->ai[n] != 0 && u->order_timer[n] == 0) {
		u->order_timer[n] = BURROWED_AI_PERIOD;
		if (!kp_ai_reaction_spell(g, n, 0)) kp_ai_update_attack_target(g, n, 0, 1, 0);
	}
}

/* OrderCritter 0x427ed0. */
static void order_critter(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t state = u->order_state[n], t = u->type[n];
	if (state == 0) {
		int64_t dir = kp_rng_game_range(g->rng, CRITTER_RANDOM, 0, 0xff), dist = CRITTER_STEP, x, y;
		int32_t s;
		if (t == KP_TYPE_KAKARU) dist = kp_rng_game_range(g->rng, CRITTER_RANDOM, KAKARU_STEP_MIN, KAKARU_STEP_MAX);
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
		if (t == KP_TYPE_KAKARU) {
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

#define JUNKYARD_STEP_MIN 0x400
#define JUNKYARD_STEP_MAX 0x800
#define JUNKYARD_COOLDOWN 0xff
#define JUNKYARD_PERIOD 15
#define STATUS_CLOAK_BITS 0x300
#define STATUS_INVINCIBLE_BIT 0x4000000
#define STATUS_AIR_BIT 0x4
#define SPRITE_HIDDEN_FLAG 0x20

/* The JunkYardDog's attack on t, then the roaming again (0x42a380, 0x4295a0). */
static void junkyard_attack(KpGame *g, int64_t n, int64_t t)
{
	kp_issue_order_target(g, n, KP_DAT(g, units, attack_unit, g->u.type[n]), t);
	kp_order_insert_after(g, n, KP_ORD_JUNKYARD_DOG, 0, 0, 0, KP_TYPE_NONE, KP_NONE);
}

/* OrderJunkYardDog 0x4286f0 (order 187): roam 1024 to 2048 pixels in a random direction (the critters' RNG source)
 * and attack a visible, hostile, vulnerable attacker at once, else every 15 frames what AcquireBestTarget 0x431c40
 * finds while the roam's cooldown runs (a ground unit roams again when it ends) or the unit has not arrived. */
static void order_junkyard_dog(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int32_t s = kp_unit_sprite(g, n);
	int64_t a, t;
	if (u->order_state[n] == 0) {
		int64_t dir = kp_rng_game_range(g->rng, CRITTER_RANDOM, 0, 0xff);
		int64_t dist = kp_rng_game_range(g->rng, CRITTER_RANDOM, JUNKYARD_STEP_MIN, JUNKYARD_STEP_MAX), x, y;
		x = kp_s16v(kp_sarv(KP_DIR_X[dir] * dist, 8) + g->w->spr.x[s]);
		y = kp_s16v(kp_sarv(KP_DIR_Y[dir] * dist, 8) + g->w->spr.y[s]);
		kp_clamp_to_map(g, u->type[n], &x, &y);
		u->order_x[n] = x;
		u->order_y[n] = y;
		kp_set_move_target_point(g, n, x, y);
		u->order_state[n] = 1;
		u->spell_cooldown[n] = JUNKYARD_COOLDOWN;
	}
	a = u->auto_target[n];
	if (a != 0 && kp_player_enemy_of(g, u->owner[n], a)
		&& kp_band(g->w->spr.flags[kp_unit_sprite(g, a)], SPRITE_HIDDEN_FLAG) == 0
		&& kp_band(u->status[a], STATUS_INVINCIBLE_BIT) == 0
		&& !(kp_band(u->status[a], STATUS_CLOAK_BITS) != 0 && kp_band(u->visibility[a], (int64_t)1 << u->owner[n]) == 0)) {
		junkyard_attack(g, n, a); /* IsUnitUndetectedBy 0x40a180 above */
		u->auto_target[n] = 0;
		kp_set_target_switch(g, n); /* 0x41cf90 */
		return;
	}
	if (u->order_timer[n] != 0) return;
	u->order_timer[n] = JUNKYARD_PERIOD;
	/* an arrived unit roams again (its 0x80000 status test never matters) */
	if ((u->move_x[n] == kp_s16v(g->w->spr.x[s]) && u->move_y[n] == kp_s16v(g->w->spr.y[s]))
		|| (u->spell_cooldown[n] == 0 && kp_band(u->status[n], STATUS_AIR_BIT) == 0)) {
		u->order_state[n] = 0;
		return;
	}
	t = kp_acquire_target(g, n);
	if (t == 0) return;
	junkyard_attack(g, n, t);
	kp_set_target_switch(g, n); /* inline in the original */
}

/* Order_Die 0x41e630. */
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
		if (kp_band(u->status[n], STATUS_HALLUCINATION) != 0 && kp_band(u->status[n], STATUS_LIFETIME_EXPIRED) == 0) {
			kp_hallucination_death(g, n);
			return;
		}
		kp_sprite_play(g->w, kp_unit_sprite(g, n), KP_ANIM_DEATH, 1);
		kp_remove_body(g, n);
		return;
	}
	kp_unit_hide(g, n);
	kp_unit_destroy(g, n);
}

/* OrderCloakNearbyUnits 0x434710 (secondary order 131): the Arbiter cloaks its owner's units within its weapon's
 * range. */
static void cloak_nearby(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int32_t s = kp_unit_sprite(g, n);
	int64_t r = kp_weapon_max_range(g, n, KP_DAT(g, units, air_weapon, u->type[n])), x = kp_s16v(g->w->spr.x[s]);
	int64_t y = kp_s16v(g->w->spr.y[s]), l = kp_s16v(x - r), t = kp_s16v(y - r), rr = kp_s16v(x + r), b = kp_s16v(y + r), *found;
	int count, k, cloaked = 0;
	if (l < 0) l = 0;
	else if (rr >= kp_mod(g->map_w_px, 65536)) rr = kp_mod(g->map_w_px, 65536) - 1;
	if (t < 0) t = 0;
	else if (b >= kp_mod(g->map_h_px, 65536)) b = kp_mod(g->map_h_px, 65536) - 1;
	found = kp_query_rect(g, l, t, rr, b, &count);
	for (k = 0; k < count; k++) {
		int64_t c = found[k], ct = u->type[c], f = KP_DAT(g, units, special_ability_flags, ct);
		if (ct == KP_TYPE_ARBITER || ct == KP_TYPE_DANIMOTH || kp_band(f, DAT_BUILDING | DAT_SINGLE_ENTITY) != 0
			|| kp_band(u->status[c], STATUS_HALLUCINATION) != 0 || ct == KP_TYPE_NUCLEAR_MISSILE || u->order[c] == KP_ORD_WARP_IN
			|| u->owner[c] != u->owner[n] || !kp_unit_box_within(g, n, r, c)) {
			continue;
		}
		kp_cloak_list_add(g, c);
		if (kp_band(u->status[c], STATUS_FREE_CLOAK) == 0) {
			u->status[c] = kp_bor(u->status[c], STATUS_FREE_CLOAK);
			cloaked = 1;
		}
	}
	kp_query_release(g, found);
	if (cloaked) g->interface_refresh++; /* RefreshStatusUi 0x4a7ed0, for the interface */
}

/* Order_InitializeArbiter 0x42f510 (order 130). */
static void order_initialize_arbiter(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	if (u->queue_head[n] == KP_NONE) kp_issue_order(g, n, KP_DAT(g, units, return_to_idle, u->type[n]));
	if (kp_band(u->status[n], STATUS_HALLUCINATION) == 0) kp_set_secondary_order(g, n, KP_ORD_CLOAK_NEARBY_UNITS);
	kp_order_unlock_done(g, n);
}

typedef void (*Handler)(KpGame *g, int64_t n);

static Handler every_step_handler(int64_t order)
{
	if (order == KP_ORD_TURRET_GUARD) return order_turret_guard;
	if (order == KP_ORD_TURRET_ATTACK) return kp_order_turret_attack;
	if (order == KP_ORD_CRITTER) return order_critter;
	if (order == KP_ORD_SIEGING) return kp_order_sieging;
	if (order == KP_ORD_INITIALIZE_ARBITER) return order_initialize_arbiter;
	if (order == KP_ORD_UNSIEGING) return kp_order_unsieging;
	if (order == KP_ORD_MEDIC_HEAL) return kp_order_medic_heal;
	if (order == KP_ORD_RECHARGE_SHIELDS_UNIT) return kp_order_recharge_shields;
	if (order == KP_ORD_RECHARGE_SHIELDS_UNIT_REMOVE_OVERLAY) return kp_order_recharge_done;
	if (order == KP_ORD_ARCHON_WARP || order == KP_ORD_DARK_ARCHON_MELD) return kp_order_archon_merge;
	if (order == KP_ORD_ARCHON_SUMMON) return kp_order_completing_summon;
	if (order == KP_ORD_RESET_COLLISION) return order_reset_collision;
	if (order == KP_ORD_SCARAB_ATTACK) return kp_order_scarab_attack;
	if (order == KP_ORD_HEAL_MOVE) return kp_order_heal_move;
	if (order == KP_ORD_MEDIC_HOLD_POSITION) return kp_order_medic_hold;
	if (order == KP_ORD_MEDIC_HEAL_TO_IDLE) return kp_order_medic_heal_to_idle;
	if (order == KP_ORD_HARVEST3) return kp_order_harvest3;
	if (order == KP_ORD_ZERG_BIRTH) return kp_order_zerg_birth;
	if (order == KP_ORD_PLACE_BUILDING) return kp_order_place_building;
	if (order == KP_ORD_CONSTRUCTING) return kp_order_constructing_building;
	if (order == KP_ORD_INCOMPLETE_BUILDING) return kp_order_build_terran;
	if (order == KP_ORD_ZERG_UNIT_MORPH) return kp_order_zerg_unit_morph;
	if (order == KP_ORD_HARVEST4) return kp_order_harvest4;
	if (order == KP_ORD_RESET_HARVEST_COLLISION) return kp_order_reset_harvest_collision;
	if (order == KP_ORD_REPAIR) return kp_order_repair;
	if (order == KP_ORD_NUKE_TRAIN) return kp_order_nuke_train;
	if (order == KP_ORD_BUILDING_LAND) return kp_order_building_land;
	if (order == KP_ORD_BUILDING_LIFTOFF) return kp_order_building_liftoff;
	if (order == KP_ORD_DRONE_BUILD) return kp_order_drone_build;
	if (order == KP_ORD_PLACE_PROTOSS_BUILDING) return kp_order_place_protoss_building;
	if (order == KP_ORD_INCOMPLETE_MORPHING) return kp_order_zerg_build_self;
	if (order == KP_ORD_RESEARCH) return kp_order_research;
	if (order == KP_ORD_UPGRADE) return kp_order_upgrade;
	if (order == KP_ORD_SELF_DESTRUCTING) return kp_unit_kill; /* KillUnit 0x42a4d0 */
	return NULL;
}

static void order_init_creep_growth(KpGame *g, int64_t n);
static void order_stopping_creep_growth(KpGame *g, int64_t n);

static Handler periodic_handler(int64_t order)
{
	switch (order) {
	case KP_ORD_GUARD: return order_guard;
	case KP_ORD_PLAYER_GUARD: return order_player_guard;
	case KP_ORD_NOTHING: return order_nothing;
	case KP_ORD_STOP: return order_stop;
	case KP_ORD_HOLD_POSITION: return order_hold_position;
	case KP_ORD_QUEEN_HOLD_POSITION: case KP_ORD_SUICIDE_HOLD_POSITION: return order_hold_once;
	case KP_ORD_MOVE_TO_ATTACK: case KP_ORD_MOVE_TO_INFEST: case KP_ORD_MOVE_TO_REPAIR: case KP_ORD_CARRIER_MOVE_TO_ATTACK:
	case KP_ORD_REAVER_MOVE_TO_ATTACK: case KP_ORD_HARVEST2: case KP_ORD_MOVE_TO_FIRE_YAMATO_GUN:
		return order_attack_obscured;
	case KP_ORD_RIGHT_CLICK_ACTION: return kp_order_right_click_action;
	case KP_ORD_MOVE: return order_move;
	case KP_ORD_REAVER_CARRIER_MOVE: return order_move;
	case KP_ORD_RESCUE_PASSIVE: return order_rescue_passive;
	case KP_ORD_NEUTRAL: return order_neutral;
	case KP_ORD_BURROWED: return order_burrowed;
	case KP_ORD_HIDDEN_GUN: return order_trap;
	case KP_ORD_HIDE_TRAP: return order_hide_trap;
	case KP_ORD_REVEAL_TRAP: return order_reveal_trap;
	case KP_ORD_OPEN_DOOR: return order_open_door;
	case KP_ORD_CLOSE_DOOR: return order_close_door;
	case KP_ORD_ENABLE_DOODAD: return order_enable_doodad;
	case KP_ORD_DISABLE_DOODAD: return order_disable_doodad;
	case KP_ORD_TOWER_GUARD: return order_tower_guard;
	case KP_ORD_TOWER_ATTACK: return order_tower_attack;
	case KP_ORD_ATTACK_MOVE: return order_attack_move;
	case KP_ORD_AI_ATTACK_MOVE: return order_ai_attack_move;
	case KP_ORD_HARASS_MOVE: return order_harass_move;
	case KP_ORD_TRANSPORT_REQUEST: return kp_order_transport_request;
	case KP_ORD_PATROL: return order_patrol;
	case KP_ORD_FOLLOW: return order_follow;
	case KP_ORD_ATTACK_UNIT: return kp_order_attack_unit;
	case KP_ORD_STAY_IN_RANGE: return kp_order_stay_in_range;
	case KP_ORD_ATTACK_FIXED_RANGE: return kp_order_attack_fixed_range;
	case KP_ORD_AI_GUARD: return kp_order_ai_guard;
	case KP_ORD_AI_PATROL: return kp_order_ai_patrol;
	case KP_ORD_COMPUTER_AI: return kp_order_computer_ai;
	case KP_ORD_COMPUTER_RETURN: return kp_order_computer_return;
	case KP_ORD_MEDIC_IDLE: return kp_order_medic_idle;
	case KP_ORD_HARVEST1: case KP_ORD_MOVE_TO_GAS: return kp_order_move_to_gas;
	case KP_ORD_UNUSED_POWERUP: return kp_order_powerup_return;
	case KP_ORD_POWERUP_IDLE: return kp_order_powerup_idle;
	case KP_ORD_LARVA: return kp_order_larva;
	case KP_ORD_WAIT_FOR_GAS: return kp_order_wait_for_gas;
	case KP_ORD_HARVEST_GAS: return kp_order_harvest_gas;
	case KP_ORD_RETURN_GAS: case KP_ORD_RETURN_MINERALS: return kp_order_return_cargo;
	case KP_ORD_MOVE_TO_MINERALS: return kp_order_move_to_minerals;
	case KP_ORD_WAIT_FOR_MINERALS: return kp_order_wait_for_minerals;
	case KP_ORD_MINING_MINERALS: return kp_order_mining_minerals;
	case KP_ORD_FIRE_YAMATO_GUN: case KP_ORD_CAST_LOCKDOWN: case KP_ORD_CAST_DARK_SWARM: case KP_ORD_CAST_PARASITE:
	case KP_ORD_CAST_SPAWN_BROODLINGS: case KP_ORD_CAST_EMP_SHOCKWAVE: case KP_ORD_CAST_PSIONIC_STORM:
	case KP_ORD_CAST_IRRADIATE: case KP_ORD_CAST_PLAGUE: case KP_ORD_CAST_CONSUME:
	case KP_ORD_CAST_ENSNARE: case KP_ORD_CAST_STASIS_FIELD: case KP_ORD_CAST_RESTORATION:
	case KP_ORD_CAST_DISRUPTION_WEB: case KP_ORD_CAST_OPTICAL_FLARE: case KP_ORD_CAST_MAELSTROM:
		return kp_order_cast_spell; /* OrderCastSpell 0x434460 */
	case KP_ORD_CAST_DEFENSIVE_MATRIX: return kp_order_defensive_matrix;
	case KP_ORD_CAST_MIND_CONTROL: return kp_order_mind_control;
	case KP_ORD_CAST_HALLUCINATION: return kp_order_hallucination;
	case KP_ORD_CAST_RECALL: return kp_order_recall;
	case KP_ORD_RECHARGE_SHIELDS_BATTERY: return kp_order_battery_recharge;
	case KP_ORD_INITIALIZE_PSI_PROVIDER: return kp_order_init_psi_provider;
	case KP_ORD_CARRIER: case KP_ORD_CARRIER_FIGHT: case KP_ORD_CARRIER_HOLD_POSITION: return kp_order_carrier;
	case KP_ORD_CARRIER_STOP: return kp_order_carrier_stop;
	case KP_ORD_REAVER_STOP: return kp_order_reaver_stop;
	case KP_ORD_CARRIER_ATTACK: return kp_order_carrier_attack;
	case KP_ORD_CARRIER_IGNORE2: return kp_order_carrier_ignore;
	case KP_ORD_INTERCEPTOR_ATTACK: return kp_order_interceptor_attack;
	case KP_ORD_REAVER: case KP_ORD_REAVER_FIGHT: case KP_ORD_REAVER_HOLD_POSITION: return kp_order_reaver;
	case KP_ORD_REAVER_ATTACK: return kp_order_carrier_attack;
	case KP_ORD_INTERCEPTOR_RETURN: return kp_order_interceptor_return;
	case KP_ORD_ENTER_TRANSPORT: return kp_order_enter_transport;
	case KP_ORD_PICKUP_IDLE: return kp_order_pickup_idle;
	case KP_ORD_PICKUP_TRANSPORT: return kp_order_pickup;
	case KP_ORD_UNLOAD: return kp_order_unload;
	case KP_ORD_MOVE_UNLOAD: return kp_order_move_unload;
	case KP_ORD_CAST_FEEDBACK: return kp_order_feedback;
	case KP_ORD_PLACE_MINE: return kp_order_place_mine;
	case KP_ORD_SPIDER_MINE: return kp_order_spider_mine;
	case KP_ORD_SUICIDE_UNIT: return kp_order_sap_unit;
	case KP_ORD_INIT_CREEP_GROWTH: return order_init_creep_growth;
	case KP_ORD_STOPPING_CREEP_GROWTH: return order_stopping_creep_growth;
	case KP_ORD_DRONE_START_BUILD: return kp_order_drone_start_build;
	case KP_ORD_NUKE_LAUNCH: return kp_order_nuke_launch;
	case KP_ORD_NUKE_PAINT: return kp_order_nuke_paint;
	case KP_ORD_NUKE_UNIT: return kp_order_nuke_unit;
	case KP_ORD_CAST_NUCLEAR_STRIKE: return kp_order_nuke_ground;
	case KP_ORD_PLACE_ADDON: return kp_order_place_addon;
	case KP_ORD_LIFTING_OFF: return kp_order_lifting_off;
	case KP_ORD_CREATE_PROTOSS_BUILDING: return kp_order_create_protoss_building;
	case KP_ORD_DRONE_LAND: return kp_order_drone_land;
	case KP_ORD_ZERG_BUILDING_MORPH: return kp_order_zerg_building_morph;
	case KP_ORD_BUILD_NYDUS_EXIT: return kp_order_build_nydus_exit;
	case KP_ORD_CAST_INFESTATION: return kp_order_cast_infestation;
	case KP_ORD_INFESTING_COMMAND_CENTER: return kp_order_infesting;
	case KP_ORD_INFESTED_COMMAND_CENTER: return kp_order_infested_command_center;
	case KP_ORD_ENTER_NYDUS_CANAL: return kp_order_enter_nydus;
	case KP_ORD_CAST_SCANNER_SWEEP: return kp_order_scanner_sweep;
	case KP_ORD_SCANNER: return kp_order_scanner;
	case KP_ORD_SUICIDE_LOCATION: return kp_order_sap_location;
	case KP_ORD_WATCH_TARGET: return order_watch_target;
	case KP_ORD_BURROWING: return kp_order_burrowing;
	case KP_ORD_UNBURROWING: return kp_order_unburrowing;
	case KP_ORD_JUNKYARD_DOG: return order_junkyard_dog;
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
		kp_ai_attack_helpless(g, n, HELPLESS_RADIUS, 0);
	}
}

/* 0x4af220: the computer's units within 640 of n, then its flyers within 1280, turn on it. */
static void alert_around(KpGame *g, int64_t n)
{
	kp_ai_attack_helpless(g, n, HELPLESS_RADIUS, 0);
	kp_ai_attack_helpless(g, n, HELPLESS_RADIUS * 2, 1);
}

/* AlertIfAcidSporesOrInterceptor 0x4af2c0. */
static void acid_or_interceptor_alert(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	if (u->acid_spores[n] >= 4) {
		alert_around(g, n);
		return;
	}
	if (kp_scalar(g, KP_INTERCEPTOR_ALERTED) != 1 && u->type[n] == KP_TYPE_INTERCEPTOR && u->union_0xc0[n] != 0) {
		alert_around(g, u->union_0xc0[n]);
		kp_scalar_set(g, KP_INTERCEPTOR_ALERTED, 1);
	}
}

void kp_dispatch_main_order(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t order = u->order[n], status, timer, address;
	Handler h;
	if (order == KP_ORD_DIE) {
		order_die(g, n);
		return;
	}
	/* the first stage's orders run before the disabled check (0x439420) */
	if (order == KP_ORD_INCOMPLETE_WARPING) {
		kp_order_incomplete_warping(g, n);
		return;
	}
	if (order == KP_ORD_WARP_IN) {
		kp_order_warp_in(g, n);
		return;
	}
	if (order == KP_ORD_NUKE_TRACK) {
		kp_order_nuke_track(g, n);
		return;
	}
	status = u->status[n];
	if (kp_unit_disabled(g, n)) {
		disabled_alert(g, n);
		return;
	}
	if (kp_band(status, STATUS_CAN_MOVE) == 0 && kp_band(status, STATUS_HALTED) != 0) disabled_alert(g, n);
	/* Each stage switches on the current order: a handler may have replaced it (0x439420). */
	order = u->order[n];
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
	order = u->order[n];
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
	if (order == KP_ORD_DIE) {
		order_die(g, n);
		return;
	} else if (first == HIDDEN_TO_IDLE) {
		kp_issue_order(g, n, kp_band(u->status[n], STATUS_IN_BUNKER) != 0 ? KP_ORD_BUNKER_GUARD : KP_ORD_NOTHING);
		return;
	} else if (first == HIDDEN_NONE) {
		return;
	} else if (first) {
		/* the orders that run every step while hidden */
		if (!kp_order_handler_run(g, order, n)) unported(g, "hidden", order, first);
		return;
	}
	timer = u->queue_timer[n];
	u->queue_timer[n] = kp_mod(timer - 1, 256);
	if (timer != 0) return;
	u->queue_timer[n] = PROCESS_PERIOD;
	if (order == KP_ORD_RESCUE_PASSIVE) {
		order_rescue_passive(g, n);
	} else if (order == KP_ORD_BUNKER_GUARD) {
		order_bunker_guard(g, n);
	} else if (order == KP_ORD_TRANSPORT_REQUEST) {
		kp_order_transport_request(g, n); /* OrderTransportRequest 0x437a00 */
	} else if (order == KP_ORD_COMPUTER_AI) {
		/* Order_BunkerGuardIfInside 0x48d2d0: a hidden computer unit inside a Bunker guards from it */
		if (kp_band(u->status[n], STATUS_IN_BUNKER) != 0) order_bunker_guard(g, n);
	} else {
		address = lookup(HIDDEN_PERIODIC, COUNT(HIDDEN_PERIODIC), order);
		if (address) unported(g, "hidden", order, address);
	}
}

static int hatchery(int64_t t) { return t >= HATCHERY_FIRST && t <= HATCHERY_LAST; }

/* SecondarySpreadCreep 0x43d1a0 (secondary order 102): every 15 frames one creep tile spreads in the large area of a
 * Hive (the constant 133 of every caller); with nothing left to spread the order ends. */
static void secondary_spread_creep(KpGame *g, int64_t n)
{
	KpUnits *u = &g->u;
	int64_t timer = kp_band(u->union_0xcc[n], 0xff);
	int32_t s;
	if (hatchery(u->type[n])) kp_secondary_spawning_larva(g, n);
	if (timer != 0) {
		u->union_0xcc[n] = kp_band(u->union_0xcc[n], kp_bnot(0xff)) + timer - 1;
		return;
	}
	u->union_0xcc[n] = kp_band(u->union_0xcc[n], kp_bnot(0xff)) + CREEP_SPREAD_PERIOD;
	s = kp_unit_sprite(g, n);
	if (kp_band(kp_creep_spread_once(g, KP_TYPE_HIVE, kp_s16v(g->w->spr.x[s]), kp_s16v(g->w->spr.y[s])), 0xff) == 0) {
		kp_set_secondary_order(g, n, hatchery(u->type[n]) ? KP_ORD_SPAWNING_LARVA : KP_ORD_NOTHING);
	}
}

/* OrderInitCreepGrowth 0x43cd50 (order 101) and OrderStoppingCreepGrowth 0x43d250 (order 103): the building idles
 * and starts to spread creep, or stops. */
static void creep_growth(KpGame *g, int64_t n, int64_t secondary)
{
	KpUnits *u = &g->u;
	kp_issue_order(g, n, u->ai[n] != 0 ? KP_ORD_COMPUTER_AI : KP_DAT(g, units, return_to_idle, u->type[n]));
	kp_set_secondary_order(g, n, secondary);
}

static void order_init_creep_growth(KpGame *g, int64_t n) { creep_growth(g, n, KP_ORD_SPREAD_CREEP); }
static void order_stopping_creep_growth(KpGame *g, int64_t n) { creep_growth(g, n, KP_ORD_NOTHING); }

void kp_dispatch_secondary_order(KpGame *g, int64_t n)
{
	int64_t order = g->u.secondary_order[n], address;
	if (order == KP_ORD_HALLUCINATION2) {
		kp_order_hallucination_end(g, n);
		return;
	}
	if (kp_unit_disabled(g, n)) return;
	kp_secondary_cloak_order(g, n, order);
	if (order == KP_ORD_CLOAK_NEARBY_UNITS) {
		cloak_nearby(g, n);
		return;
	}
	if (order == KP_ORD_SHIELD_BATTERY) {
		kp_order_battery_glow(g, n);
		return;
	}
	if (order == KP_ORD_SPREAD_CREEP) {
		secondary_spread_creep(g, n);
		return;
	}
	if (order == KP_ORD_TRAIN) {
		kp_secondary_train(g, n);
		return;
	}
	if (order == KP_ORD_BUILD_ADDON) {
		kp_secondary_build_addon(g, n);
		return;
	}
	if (order == KP_ORD_SPAWNING_LARVA) {
		kp_secondary_spawning_larva(g, n);
		return;
	}
	if (order == KP_ORD_TRAIN_FIGHTER) {
		kp_order_train_fighter(g, n);
		return;
	}
	address = lookup(SECONDARY, COUNT(SECONDARY), order);
	if (address) unported(g, "secondary", order, address);
}

void kp_secondary_cloak_order(KpGame *g, int64_t n, int64_t order)
{
	if (order == KP_ORD_CLOAK) kp_cloak_list_add(g, n);
	else if (order == KP_ORD_DECLOAK) kp_set_secondary_order(g, n, KP_ORD_NOTHING);
}
