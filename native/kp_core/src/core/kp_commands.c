/* Game commands of the turn (kp_game.h). */
#include "kp_game_internal.h"

#define EXECUTOR 0x475d30
#define SYNC 0x37
#define KEEP_ALIVE 0x05
#define CHEAT_COMMAND 0x12
#define SAVE_COMMAND 0x06
#define LOAD_COMMAND 0x07
#define RESTART_COMMAND 0x08
#define SAVE_NAME 28
#define SPEED_COMMAND 0x0f
#define SPEEDS 7
#define EXIT_RESTART 5             /* RequestGameExit 0x444e00's code */
#define DAT_BUILDING 0x1
#define DAT_ADDON 0x2
#define DAT_MECHANICAL 0x40000000
#define STATUS_COMPLETED 0x1
#define CARRY_GAS 0x1
#define CARRY_MINERALS 0x2
#define SELECT 0x09
#define SELECT_ADD 0x0a
#define SELECT_REMOVE 0x0b
#define RIGHT_CLICK 0x14
#define TARGETED_ORDER 0x15
#define STOP_COMMAND 0x1a
#define RETURN_CARGO_COMMAND 0x1e
#define BUILD_COMMAND 0x0c
#define CONTROL_GROUP_COMMAND 0x13
#define TRAIN_COMMAND 0x1f
#define TRAIN_FIGHTER_COMMAND 0x27
#define UNIT_MORPH_COMMAND 0x23
#define CANCEL_MORPH_COMMAND 0x19
#define CANCEL_CONSTRUCTION_COMMAND 0x18
#define CANCEL_TRAIN_COMMAND 0x20
#define RESEARCH_COMMAND 0x30
#define BUILDING_MORPH_COMMAND 0x35
#define LIFT_OFF_COMMAND 0x2f
#define CANCEL_NUKE_COMMAND 0x2e
#define CANCEL_ADDON_COMMAND 0x34
#define CANCEL_RESEARCH_COMMAND 0x31
#define UPGRADE_COMMAND 0x32
#define CANCEL_UPGRADE_COMMAND 0x33
#define CANCEL_LAST 0xfe
#define CANCEL_NONE 0xff
#define HOLD_POSITION_COMMAND 0x2b
#define BURROW_COMMAND 0x2c
#define UNBURROW_COMMAND 0x2d
#define STIM_COMMAND 0x36
#define CLOAK_COMMAND 0x21
#define DECLOAK_COMMAND 0x22
#define UNLOAD_ALL_COMMAND 0x28
#define MERGE_ARCHON_COMMAND 0x2a
#define MERGE_DARK_ARCHON_COMMAND 0x5a
#define UNLOAD_COMMAND 0x29
#define STATUS_IN_TRANSPORT 0x40
#define UNSIEGE_COMMAND 0x25
#define SIEGE_COMMAND 0x26
#define DAT_BURROWABLE 0x100000
#define DAT_POWERUP 0x800
#define STATUS_BURROWED 0x10
#define STATUS_GROUNDED_BUILDING 0x2
#define STATUS_HALLUCINATION 0x40000000
#define USER_RIGHT_CLICKED 2
#define QUEUE_MAX 8
#define QUEUE_POOL_LIMIT 1800
#define STRING_QUEUE_FULL 0x367
#define STRING_QUEUE_POOL_FULL 0x369
#define KIND_UNIT 1
#define KIND_POINT 2

static const struct {
	int id;
	int64_t address;
} HANDLERS[] = {
	{ 0x06, 0x472f50 }, { 0x07, 0x4729e0 }, { 0x08, 0x472e20 }, { 0x09, 0x472fc0 },
	{ 0x0a, 0x473070 }, { 0x0b, 0x473170 }, { 0x0c, 0x472d30 }, { 0x0d, 0x473280 }, { 0x0e, 0x4731d0 },
	{ 0x0f, 0x473310 }, { 0x10, 0x472c70 }, { 0x11, 0x472be0 }, { 0x12, 0x472530 }, { 0x13, 0x472790 },
	{ 0x14, 0x472f10 }, { 0x15, 0x472270 }, { 0x18, 0x472880 }, { 0x19, 0x4728b0 }, { 0x1a, 0x473440 },
	{ 0x1b, 0x473670 }, { 0x1c, 0x473710 }, { 0x1d, 0x472b40 }, { 0x1e, 0x472e40 }, { 0x1f, 0x4722c0 },
	{ 0x20, 0x473540 }, { 0x21, 0x472560 }, { 0x22, 0x4726b0 }, { 0x23, 0x472a50 }, { 0x25, 0x473930 },
	{ 0x26, 0x473330 }, { 0x27, 0x472330 }, { 0x28, 0x4739a0 }, { 0x29, 0x472750 }, { 0x2a, 0x4737b0 },
	{ 0x2b, 0x4727c0 }, { 0x2c, 0x472410 }, { 0x2d, 0x472600 }, { 0x2e, 0x4724f0 }, { 0x2f, 0x4728e0 },
	{ 0x30, 0x472dc0 }, { 0x31, 0x4735e0 }, { 0x32, 0x473a60 }, { 0x33, 0x473630 }, { 0x34, 0x4734f0 },
	{ 0x35, 0x473ac0 }, { 0x36, 0x4733a0 }, { 0x37, 0x47a510 }, { 0x38, 0x473b60 }, { 0x39, 0x473b60 },
	{ 0x3a, 0x473bc0 }, { 0x3b, 0x473bc0 }, { 0x55, 0x476d10 }, { 0x56, 0x4732f0 }, { 0x57, 0x472da0 },
	{ 0x58, 0x472a00 }, { 0x5a, 0x473870 }, { 0x5c, 0x46a860 },
};

/* Orders the targeted-order command ignores (its jump table 0x49ced4). */
static int ignored_targeted(int64_t o)
{
	switch (o) {
	case KP_ORD_DIE: case KP_ORD_INFESTED_COMMAND_CENTER: case KP_ORD_SPIDER_MINE: case KP_ORD_DRONE_START_BUILD:
	case KP_ORD_INFESTING_COMMAND_CENTER: case KP_ORD_PLACE_BUILDING: case KP_ORD_PLACE_PROTOSS_BUILDING:
	case KP_ORD_CREATE_PROTOSS_BUILDING: case KP_ORD_CONSTRUCTING: case KP_ORD_PLACE_ADDON:
	case KP_ORD_BUILD_NYDUS_EXIT: case KP_ORD_BUILDING_LAND: case KP_ORD_BUILDING_LIFTOFF:
	case KP_ORD_DRONE_LIFT_OFF: case KP_ORD_HARVEST2: case KP_ORD_MOVE_TO_GAS: case KP_ORD_WAIT_FOR_GAS:
	case KP_ORD_HARVEST_GAS: case KP_ORD_MOVE_TO_MINERALS: case KP_ORD_WAIT_FOR_MINERALS: case KP_ORD_MINING_MINERALS:
	case KP_ORD_HARVEST3: case KP_ORD_HARVEST4: case KP_ORD_CTFCOP2:
		return 1;
	}
	return 0;
}

static int building_targeted(int64_t o)
{
	return o == KP_ORD_RALLY_POINT_UNIT || o == KP_ORD_RALLY_POINT_TILE || o == KP_ORD_RECHARGE_SHIELDS_BATTERY
		|| o == KP_ORD_PICKUP_BUNKER;
}

/* Current orders that a queued command replaces instead of queueing behind (0x42a8b0). */
static int idle_order(int64_t o)
{
	return o == KP_ORD_GUARD || o == KP_ORD_PLAYER_GUARD || o == KP_ORD_NOTHING || o == KP_ORD_PICKUP_IDLE
		|| o == KP_ORD_PATROL || o == KP_ORD_MEDIC_IDLE;
}

/* GetHoldPositionOrder 0x41fae0. */
static int64_t hold_order(int64_t t)
{
	switch (t) {
	case KP_TYPE_CARRIER: case KP_TYPE_GANTRITHOR: return KP_ORD_CARRIER_HOLD_POSITION;
	case KP_TYPE_REAVER: case KP_TYPE_WARBRINGER: return KP_ORD_REAVER_HOLD_POSITION;
	case KP_TYPE_QUEEN: case KP_TYPE_MATRIARCH: return KP_ORD_QUEEN_HOLD_POSITION;
	case KP_TYPE_SCOURGE: case KP_TYPE_INFESTED_TERRAN: return KP_ORD_SUICIDE_HOLD_POSITION;
	case KP_TYPE_MEDIC: return KP_ORD_MEDIC_HOLD_POSITION;
	}
	return KP_ORD_HOLD_POSITION;
}

/* The WORD at byte offset `at` of a command (a short command fails). */
static int64_t u16(KpGame *g, const uint8_t *b, size_t size, size_t at)
{
	if (at + 1 >= size) kp_game_fail(g, "commands: command of %u bytes read at %u", (unsigned)size, (unsigned)at);
	return b[at] | (int64_t)b[at + 1] << 8;
}

/* The DWORD at byte offset `at`. */
static int64_t u32(KpGame *g, const uint8_t *b, size_t size, size_t at)
{
	return u16(g, b, size, at) | u16(g, b, size, at + 2) << 16;
}

/* A flag byte: a missing byte counts as set. */
static int flag(const uint8_t *b, size_t size, size_t at) { return at >= size || b[at] != 0; }

static int64_t byte_at(KpGame *g, const uint8_t *b, size_t size, size_t at)
{
	if (at >= size) kp_game_fail(g, "commands: command of %u bytes read at %u", (unsigned)size, (unsigned)at);
	return b[at];
}

/* IssueCommand 0x42a8b0. */
static void issue_command(KpGame *g, int64_t n, int queued, int64_t order, int kind, int64_t x, int64_t y,
	int64_t target, int64_t fow)
{
	KpUnits *u = &g->u;
	if (queued && KP_DAT(g, orders, queueable, order) != 0 && !idle_order(u->order[n])) {
		if (u->queue_count[n] >= QUEUE_MAX) {
			kp_report_error(g, u->owner[n], STRING_QUEUE_FULL);
		} else if (g->orders.allocated >= QUEUE_POOL_LIMIT) {
			kp_report_error(g, u->owner[n], STRING_QUEUE_POOL_FULL);
		} else if (kind == KIND_UNIT) {
			int64_t tx = 0, ty = 0;
			if (target != 0) {
				int32_t s = kp_unit_sprite(g, target);
				tx = g->w->spr.x[s];
				ty = g->w->spr.y[s];
			}
			kp_order_queue(g, n, order, tx, ty, target, KP_TYPE_NONE);
		} else {
			kp_order_queue(g, n, order, x, y, 0, fow);
		}
	} else if (kind == KIND_UNIT) {
		kp_issue_order_target(g, n, order, target);
	} else {
		kp_issue_order_fow(g, n, order, fow, x, y);
	}
}

static void issue_command_on_unit(KpGame *g, int64_t n, int queued, int64_t order, int64_t target)
{
	issue_command(g, n, queued, order, KIND_UNIT, 0, 0, target, KP_TYPE_NONE);
}

static void issue_command_at_point(KpGame *g, int64_t n, int queued, int64_t order, int64_t fow, int64_t x, int64_t y)
{
	issue_command(g, n, queued, order, KIND_POINT, x, y, 0, fow);
}

static int queen(int64_t t) { return t == KP_TYPE_QUEEN || t == KP_TYPE_MATRIARCH; }

/* IsBeaconType 0x421620, or a powerup. */
static int beacon_or_powerup(KpGame *g, int64_t t)
{
	return (t >= KP_TYPE_FIRST_BEACON && t <= KP_TYPE_LAST_BEACON) || kp_band(KP_DAT(g, units, special_ability_flags, t), DAT_POWERUP) != 0;
}

/* ValidateOrderTarget 0x429870 for Repair 34: a completed mechanical Terran unit below its full hit points. */
int kp_repairable(KpGame *g, int64_t target)
{
	KpUnits *u = &g->u;
	int64_t t = u->type[target];
	return kp_unit_race(g, target) == KP_RACE_TERRAN && kp_band(KP_DAT(g, units, special_ability_flags, t), DAT_MECHANICAL) != 0
		&& kp_band(u->status[target], STATUS_COMPLETED) != 0 && kp_s32v(u->hp[target]) < KP_DAT(g, units, hit_points, t);
}

/* The right-click handlers by GetUnitRightClickAction 0x422d90 (table 0x508e18). */
static int64_t right_click_order(KpGame *g, int64_t action, int64_t n, int64_t target, int64_t fow, int64_t *keep)
{
	KpUnits *u = &g->u;
	*keep = target;
	switch (action) {
	case 0:
		*keep = 0;
		return KP_ORD_NOTHING; /* 0x47ef40 */
	case 1: /* RightClickOrderNormal 0x47ef50 */
		if (kp_band(u->status[n], 2) != 0) return KP_ORD_NOTHING;
		if (target == 0) return fow != KP_TYPE_NONE ? KP_ORD_RIGHT_CLICK_ACTION : KP_ORD_MOVE;
		if (beacon_or_powerup(g, u->type[target])) return KP_ORD_MOVE;
		if (kp_is_enemy(g, n, target)) return KP_ORD_ATTACK_DEFAULT;
		if (kp_is_transport(g, target) && kp_can_load(g, n, target)) return KP_ORD_ENTER_TRANSPORT;
		return kp_band(u->status[target], STATUS_BURROWED) != 0 ? KP_ORD_MOVE : KP_ORD_FOLLOW;
	case 2: /* RightClickOrderMoveOnly 0x47f070 */
		if (kp_band(u->status[n], 2) != 0) {
			if (!kp_rally_producer(u->type[n])) return KP_ORD_NOTHING;
			return target != 0 ? KP_ORD_RALLY_POINT_UNIT : KP_ORD_RALLY_POINT_TILE;
		}
		if (target == 0) return queen(u->type[n]) && fow != KP_TYPE_NONE ? KP_ORD_RIGHT_CLICK_ACTION : KP_ORD_MOVE;
		/* ValidateOrderTarget 0x429870 for PickupTransport 94: a unit of the same owner that may board */
		if (kp_is_transport(g, n) && u->owner[target] == u->owner[n] && kp_can_load(g, target, n)) return KP_ORD_PICKUP_TRANSPORT;
		if (kp_is_transport(g, target) && kp_can_load(g, n, target)) return KP_ORD_ENTER_TRANSPORT;
		if (kp_band(u->status[target], STATUS_BURROWED) != 0) return KP_ORD_MOVE;
		if (queen(u->type[n]) && kp_validate_spell_target(g, n, KP_TECH_INFESTATION, target, KP_TYPE_NONE, 0, 0, NULL)) return KP_ORD_CAST_INFESTATION;
		return u->type[n] == KP_TYPE_MEDIC ? KP_ORD_HEAL_MOVE : KP_ORD_FOLLOW;
	case 3: /* RightClickOrderAttackOnly 0x47f010 */
		if (target != 0 && !beacon_or_powerup(g, u->type[target]) && kp_is_enemy(g, n, target)) return KP_ORD_ATTACK_DEFAULT;
		return KP_DAT(g, units, return_to_idle, u->type[n]);
	case 4: { /* RightClickOrderHarvest 0x47f2c0 */
		int64_t order = target != 0 ? kp_harvest_right_click_order(g, n, target) : KP_ORD_NONE;
		return order != KP_ORD_NONE ? order : right_click_order(g, 1, n, target, fow, keep);
	}
	case 5: { /* RightClickOrderHarvestRepair 0x47f180 */
		int64_t order;
		if (target == 0) return right_click_order(g, 1, n, target, fow, keep);
		order = kp_harvest_right_click_order(g, n, target);
		if (order != KP_ORD_NONE) return order;
		if (kp_band(u->status[target], STATUS_GROUNDED_BUILDING | STATUS_COMPLETED) == STATUS_GROUNDED_BUILDING) {
			int64_t c = u->connected[target];
			if (u->owner[target] == u->owner[n] && kp_unit_race(g, target) == KP_RACE_TERRAN
				&& kp_band(KP_DAT(g, units, special_ability_flags, u->type[target]), DAT_ADDON) == 0
				&& (c == 0 || u->order_unit[c] != target)) {
				return KP_ORD_CONSTRUCTING;
			}
		} else if (kp_alliance(g, u->owner[n], u->owner[target]) != 0) {
			int loadable = kp_is_transport(g, target) && kp_can_load(g, n, target);
			if (loadable && kp_band(KP_DAT(g, units, special_ability_flags, u->type[target]), DAT_BUILDING) == 0) {
				return KP_ORD_ENTER_TRANSPORT;
			}
			if (kp_repairable(g, target)) return KP_ORD_REPAIR;
			if (loadable) return KP_ORD_ENTER_TRANSPORT;
		}
		return right_click_order(g, 1, n, target, fow, keep);
	}
	}
	kp_game_fail(g, "commands: right-click action %lld", (long long)action);
}

/* ExecuteRightClickCommand 0x47f680. */
static void right_click(KpGame *g, int64_t x, int64_t y, int64_t target, int64_t fow, int queued)
{
	KpUnits *u = &g->u;
	KpFormation f;
	int64_t units[KP_SLOTS], player = kp_scalar(g, KP_COMMAND_PLAYER);
	int count, k;
	kp_group_formation(g, x, y, 1, &f);
	if (target != 0) fow = KP_TYPE_NONE;
	else if (fow != KP_TYPE_NONE) kp_game_fail(g, "commands: right click on a fog-of-war thingy (0x418210)");
	count = kp_selected_units(g, player, units);
	for (k = 0; k < count; k++) {
		int64_t n = units[k], action = kp_right_click_action(g, n), order, t;
		if (kp_band(u->status[n], STATUS_HALLUCINATION) != 0 && (action == 4 || action == 5)) action = 1;
		kp_scalar_set(g, KP_SECOND_ORDER, KP_ORD_NOTHING);
		order = right_click_order(g, action, n, target, fow, &t);
		if (order == KP_ORD_RALLY_POINT_UNIT) {
			if (kp_rally_producer(u->type[n])) kp_set_rally_unit(g, n, target);
		} else if (order != KP_ORD_NOTHING && n != target) {
			if (order == KP_ORD_RALLY_POINT_TILE) {
				if (kp_rally_producer(u->type[n])) kp_set_rally_point(g, n, x, y);
				continue;
			}
			if (order == KP_ORD_ATTACK_DEFAULT) {
				/* The turret attacks first, before the permission check (0x47f7b3). */
				int64_t sub = u->subunit[n];
				if (sub != 0) issue_command_on_unit(g, sub, queued, KP_DAT(g, units, attack_unit, u->type[sub]), t);
				order = KP_DAT(g, units, attack_unit, u->type[n]);
			}
			if (kp_can_issue_order(g, order, player, n) == 1) {
				if (t != 0) {
					issue_command_on_unit(g, n, queued, order, t);
					if (kp_scalar(g, KP_SECOND_ORDER) != KP_ORD_NOTHING) issue_command_on_unit(g, n, queued, kp_scalar(g, KP_SECOND_ORDER), t);
				} else {
					int64_t px = x, py = y;
					if (fow == KP_TYPE_NONE) kp_formation_point(g, n, &f, &px, &py);
					issue_command_at_point(g, n, queued, order, fow, px, py);
					if (kp_scalar(g, KP_SECOND_ORDER) != KP_ORD_NOTHING) {
						issue_command_at_point(g, n, queued, kp_scalar(g, KP_SECOND_ORDER), fow, px, py);
					}
				}
				u->user_action[n] = kp_bor(u->user_action[n], USER_RIGHT_CLICKED);
			}
		}
	}
}

/* Stop 0x473440, HoldPosition 0x4727c0. */
static void order_selection(KpGame *g, int queued, int hold)
{
	int64_t units[KP_SLOTS], player = kp_scalar(g, KP_COMMAND_PLAYER);
	int count = kp_selected_units(g, player, units), k;
	for (k = 0; k < count; k++) {
		int64_t n = units[k], order = hold ? hold_order(g->u.type[n]) : KP_ORD_STOP;
		if (kp_can_issue_order(g, order, player, n) == 1) issue_command_on_unit(g, n, queued, order, 0);
	}
}

/* ExecSiege 0x473330, ExecUnsiege 0x473930 (their error feedback is the interface's). */
static void siege_selection(KpGame *g, int siege, int queued)
{
	int64_t units[KP_SLOTS], player = kp_scalar(g, KP_COMMAND_PLAYER);
	int count = kp_selected_units(g, player, units), k;
	for (k = 0; k < count; k++) {
		int64_t order = kp_siege_command_order(g, units[k], siege);
		if (order != 0 && kp_can_use_tech(g, KP_TECH_TANK_SIEGE_MODE, player, units[k]) == 1) {
			issue_command_on_unit(g, units[k], queued, order, 0);
		}
	}
}

/* ExecUnloadAll 0x4739a0 (its error feedback is the interface's). */
static void unload_selection(KpGame *g, int queued)
{
	int64_t units[KP_SLOTS], player = kp_scalar(g, KP_COMMAND_PLAYER);
	int count = kp_selected_units(g, player, units), k;
	for (k = 0; k < count; k++) {
		if (kp_can_issue_order(g, KP_ORD_UNLOAD, player, units[k]) == 1 && kp_has_cargo(g, units[k])) {
			issue_command_on_unit(g, units[k], queued, KP_ORD_UNLOAD, 0);
		}
	}
}

/* ExecUnloadUnit 0x472750: one passenger of the player, selected or not its transport. */
static void unload_passenger(KpGame *g, int64_t p)
{
	KpUnits *u = &g->u;
	if (p != 0 && u->connected[p] != 0 && kp_band(u->status[p], STATUS_IN_TRANSPORT) != 0
		&& u->owner[p] == kp_scalar(g, KP_COMMAND_PLAYER)) {
		kp_unload_unit(g, p);
	}
}

/* The issuer's only selected unit (0x49cb10 without a second, 0x49cb20), else 0. */
static int64_t single_selected(KpGame *g, int64_t player)
{
	int64_t units[KP_SLOTS];
	return kp_selected_units(g, player, units) == 1 ? units[0] : 0;
}

/* ExecTrain 0x4722c0 (command 0x1f): the only selected unit queues a unit type it may train and starts Train (a
 * refusal's message is the interface's). */
static void train(KpGame *g, int64_t t)
{
	int64_t player = kp_scalar(g, KP_COMMAND_PLAYER), n = single_selected(g, player);
	if (n == 0 || kp_can_train_unit(g, t, player, n) != 1 || t > KP_TYPE_LAST_UNIT) return;
	if (kp_enqueue_unit(g, n, t)) kp_set_secondary_order(g, n, KP_ORD_TRAIN);
	g->interface_refresh++; /* RefreshStatusUi 0x4a7ed0, for the interface */
}

/* ExecTrainFighter 0x472330 (command 0x27): each selected Carrier and Reaver queues a fighter. */
static void train_fighters(KpGame *g)
{
	int64_t units[KP_SLOTS], player = kp_scalar(g, KP_COMMAND_PLAYER);
	int count = kp_selected_units(g, player, units), k;
	g->interface_refresh++; /* RefreshStatusUi 0x4a7ed0, for the interface */
	for (k = 0; k < count; k++) kp_queue_fighter(g, units[k]);
}

/* ExecUnitMorph 0x472a50 (command 0x23): each selected Larva or Mutalisk that may train type t, or Hydralisk with the
 * Lurker Aspect for a Lurker, not morphing already, queues t and starts to morph (a refusal's message is the
 * interface's). */
static void unit_morph(KpGame *g, int64_t t)
{
	KpUnits *u = &g->u;
	int64_t units[KP_SLOTS], player = kp_scalar(g, KP_COMMAND_PLAYER), ok;
	int count = kp_selected_units(g, player, units), k;
	g->interface_refresh++; /* RefreshStatusUi 0x4a7ed0, for the interface */
	for (k = 0; k < count; k++) {
		int64_t n = units[k], type = u->type[n];
		if (type == KP_TYPE_HYDRALISK && t == KP_TYPE_LURKER) ok = kp_can_use_tech(g, KP_TECH_LURKER_ASPECT, player, n);
		else if (type == KP_TYPE_LARVA || type == KP_TYPE_MUTALISK) ok = kp_can_train_unit(g, t, player, n);
		else continue;
		if (ok == 1 && u->order[n] != KP_ORD_ZERG_UNIT_MORPH && kp_enqueue_unit(g, n, t)) kp_issue_order(g, n, KP_ORD_ZERG_UNIT_MORPH);
	}
}

/* ExecCancelMorph 0x4728b0 (command 0x19, every selected unit of the issuer) and ExecCancelConstruction 0x472880
 * (command 0x18, the only one): CancelConstruction 0x41a340. */
static void cancel_morph(KpGame *g, int all)
{
	int64_t units[KP_SLOTS], player = kp_scalar(g, KP_COMMAND_PLAYER);
	int count = kp_selected_units(g, player, units), k;
	if (!all && count != 1) return;
	for (k = 0; k < count; k++) {
		if (g->u.owner[units[k]] == player) kp_cancel_construction(g, units[k]);
	}
}

/* ExecCancelTrain 0x473540 (command 0x20): the issuer's only selected completed producer (the Zerg morphs aside)
 * cancels the slot (relative to the head), its last queued unit (254) or nothing (255). */
static void cancel_train(KpGame *g, int64_t slot)
{
	KpUnits *u = &g->u;
	int64_t player = kp_scalar(g, KP_COMMAND_PLAYER), n = single_selected(g, player), t;
	if (n == 0) return;
	t = u->type[n];
	if (t == KP_TYPE_LARVA || t == KP_TYPE_MUTALISK || t == KP_TYPE_HYDRALISK || t == KP_TYPE_HATCHERY || t == KP_TYPE_LAIR || t == KP_TYPE_SPIRE || t == KP_TYPE_CREEP_COLONY
		|| kp_band(u->status[n], STATUS_COMPLETED) == 0 || u->owner[n] != player) {
		return;
	}
	if (slot == CANCEL_LAST) kp_cancel_last_queued(g, n);
	else if (slot != CANCEL_NONE) kp_cancel_queued(g, n, kp_s16v(slot));
	g->interface_refresh++; /* RefreshStatusUi 0x4a7ed0, for the interface */
}

/* ExecReturnCargo 0x472e40 (command 0x1e): the selected workers that carry minerals or gas take them back. */
static void return_cargo_selection(KpGame *g, int queued)
{
	KpUnits *u = &g->u;
	int64_t units[KP_SLOTS], player = kp_scalar(g, KP_COMMAND_PLAYER), order;
	int count = kp_selected_units(g, player, units), k;
	for (k = 0; k < count; k++) {
		int64_t n = units[k];
		if (kp_band(u->resource_type[n], CARRY_MINERALS) != 0) order = KP_ORD_RETURN_MINERALS;
		else if (kp_band(u->resource_type[n], CARRY_GAS) != 0) order = KP_ORD_RETURN_GAS;
		else continue;
		if (kp_can_issue_order(g, order, player, n) == 1) issue_command_on_unit(g, n, queued, order, 0);
	}
}

static void stim_selection(KpGame *g)
{
	int64_t units[KP_SLOTS], player = kp_scalar(g, KP_COMMAND_PLAYER);
	int count = kp_selected_units(g, player, units), k;
	for (k = 0; k < count; k++) {
		if (kp_can_use_tech(g, KP_TECH_STIM_PACKS, player, units[k]) == 1) kp_apply_stim(g, units[k]);
	}
}

/* CMD_Cloak 0x472560 and CMD_Decloak 0x4726b0: the selected units that may use their cloaking tech (a refusal's
 * message is the interface's). */
static void cloak_selection(KpGame *g, int cloak)
{
	int64_t units[KP_SLOTS], player = kp_scalar(g, KP_COMMAND_PLAYER);
	int count = kp_selected_units(g, player, units), k;
	for (k = 0; k < count; k++) {
		int64_t n = units[k];
		if (kp_can_use_tech(g, kp_cloak_tech(g, n), player, n) != 1) continue;
		if (cloak) kp_try_start_cloak(g, n);
		else kp_set_secondary_order(g, n, KP_ORD_DECLOAK);
	}
}

/* ExecBurrow 0x472410, ExecUnburrow 0x472600 (which reads no queue flag). */
static void burrow_selection(KpGame *g, int burrow, int queued)
{
	KpUnits *u = &g->u;
	int64_t units[KP_SLOTS], player = kp_scalar(g, KP_COMMAND_PLAYER);
	int count = kp_selected_units(g, player, units), k;
	for (k = 0; k < count; k++) {
		int64_t n = units[k];
		if (kp_band(KP_DAT(g, units, special_ability_flags, u->type[n]), DAT_BURROWABLE) == 0) continue;
		if (kp_can_use_tech(g, KP_TECH_BURROWING, player, n) != 1) continue;
		if (!burrow) {
			kp_request_unburrow(g, n);
		} else if (kp_band(u->status[n], STATUS_BURROWED) == 0 && u->order[n] != KP_ORD_BURROWING) {
			issue_command_on_unit(g, n, queued, KP_ORD_BURROWING, 0);
		}
	}
}

/* ExecuteTargetedOrderForSelection 0x49cc40. */
static void targeted_order(KpGame *g, int64_t x, int64_t y, int64_t target, int64_t fow, int64_t order, int64_t queued)
{
	KpUnits *u = &g->u;
	KpFormation f;
	int64_t units[KP_SLOTS], player = kp_scalar(g, KP_COMMAND_PLAYER);
	int count, k;
	kp_group_formation(g, x, y, KP_DAT(g, orders, obstructable, order) != 0, &f);
	if (ignored_targeted(order)) return;
	count = kp_selected_units(g, player, units);
	for (k = 0; k < count; k++) {
		int64_t n = units[k], tech = KP_DAT(g, orders, technology_energy, order), ok, status, t;
		int building;
		ok = tech == KP_TECH_NONE ? kp_can_issue_order(g, order, player, n) : kp_can_use_tech(g, tech, player, n);
		status = u->status[n];
		building = kp_band(status, STATUS_GROUNDED_BUILDING) != 0;
		t = u->type[n];
		if (ok != 1) {
			/* CommandErrorFeedback 0x472210: interface feedback only. */
		} else if (n == target
			&& !((building && (order == KP_ORD_RALLY_POINT_UNIT || order == KP_ORD_RALLY_POINT_TILE))
				|| ((t == KP_TYPE_DEFILER || t == KP_TYPE_UNCLEAN_ONE) && order == KP_ORD_CAST_DARK_SWARM)
				|| (kp_is_transport(g, n) && order == KP_ORD_MOVE_UNLOAD))) {
			/* a unit does not target itself */
		} else if (!(building && building_targeted(order)) && kp_right_click_action(g, n) == 0) {
			/* units that take no right click take no targeted order */
		} else if (u->order[n] == KP_ORD_NUKE_LAUNCH && order != 0) {
			/* a launching silo keeps its order */
		} else {
			int64_t main = order, sub = KP_ORD_NOTHING;
			int skip = 0, q;
			if (t == KP_TYPE_MEDIC && (order == KP_ORD_ATTACK_MOVE || order == KP_ORD_ATTACK_DEFAULT)) {
				main = KP_ORD_HEAL_MOVE;
			} else if (order == KP_ORD_ATTACK_DEFAULT) {
				int64_t turret;
				main = target != 0 ? KP_DAT(g, units, attack_unit, t) : KP_DAT(g, units, attack_move, t);
				skip = main == KP_ORD_NOTHING;
				turret = u->subunit[n];
				if (!skip && turret != 0 && kp_band(KP_DAT(g, units, special_ability_flags, u->type[turret]), 0x10) != 0) {
					sub = target != 0 ? KP_DAT(g, units, attack_unit, u->type[turret]) : KP_DAT(g, units, attack_move, u->type[turret]);
				}
			}
			if (!skip) {
				q = queued != 0;
				if (main == KP_ORD_RALLY_POINT_UNIT) {
					kp_set_rally_unit(g, n, target);
				} else if (main == KP_ORD_RALLY_POINT_TILE) {
					kp_set_rally_point(g, n, x, y);
				} else if (target != 0) {
					issue_command_on_unit(g, n, q, main, target);
					if (sub != KP_ORD_NOTHING) issue_command_on_unit(g, u->subunit[n], q, sub, target);
				} else {
					int64_t px = x, py = y;
					if (fow == KP_TYPE_NONE) kp_formation_point(g, n, &f, &px, &py);
					issue_command_at_point(g, n, q, main, fow, px, py);
					if (sub != KP_ORD_NOTHING) issue_command_at_point(g, u->subunit[n], q, sub, fow, px, py);
				}
				u->user_action[n] = kp_bor(u->user_action[n], USER_RIGHT_CLICKED);
			}
		}
	}
}

void kp_execute_command(KpGame *g, int64_t player, const uint8_t *b, size_t size)
{
	int id;
	int i;
	if (size == 0) kp_game_fail(g, "commands: an empty command of player %lld", (long long)player);
	id = b[0];
	/* CheckSyncCommand 0x47a510 compares the sync values of the players' games; a game run alone
	 * has none to compare, and the check changes no game state. */
	if (id == SYNC) return;
	/* The keep-alive 0x05 only counts the network's turns (0x672988, atlas P25); no game state. */
	if (id == KEEP_ALIVE) return;
	kp_scalar_set(g, KP_COMMAND_PLAYER, player);
	switch (id) {
	case SELECT: case SELECT_ADD: case SELECT_REMOVE: {
		int64_t refs[256];
		int count = (int)byte_at(g, b, size, 1), k;
		for (k = 1; k <= count; k++) refs[k - 1] = u16(g, b, size, (size_t)(2 * k));
		if (id == SELECT) kp_command_select(g, player, refs, count);
		else if (id == SELECT_ADD) kp_command_select_add(g, player, refs, count);
		else kp_command_select_remove(g, player, refs, count);
		break;
	}
	case RIGHT_CLICK: {
		int64_t x = u16(g, b, size, 1), y = u16(g, b, size, 3);
		if (x >= g->map_w_px || y >= g->map_h_px) return;
		right_click(g, x, y, kp_unit_from_ref(g, u16(g, b, size, 5)), u16(g, b, size, 7), flag(b, size, 9));
		break;
	}
	case TARGETED_ORDER: {
		int64_t x = u16(g, b, size, 1), y = u16(g, b, size, 3);
		if (x >= g->map_w_px || y >= g->map_h_px) return;
		targeted_order(g, kp_s16v(x), kp_s16v(y), kp_unit_from_ref(g, u16(g, b, size, 5)), u16(g, b, size, 7),
			byte_at(g, b, size, 9), flag(b, size, 10));
		break;
	}
	case CHEAT_COMMAND: kp_exec_cheat(g, u32(g, b, size, 1)); break;
	case RESTART_COMMAND: kp_event(g, KP_EVENT_GAME_EXIT, EXIT_RESTART, 0, 0, NULL); break; /* ExecRestartGame 0x472e20 */
	case SAVE_COMMAND: case LOAD_COMMAND: { /* ExecSaveGame 0x472f50, ExecLoadGame 0x4729e0: the host's files */
		char name[SAVE_NAME + 1];
		size_t n = 0;
		while (n < SAVE_NAME && 5 + n < size && b[5 + n] != 0) {
			name[n] = (char)b[5 + n];
			n++;
		}
		name[n] = 0;
		if (id == SAVE_COMMAND) kp_save_game_effects(g); /* the writers run whether or not the host's file opens */
		kp_event(g, id == SAVE_COMMAND ? KP_EVENT_SAVE_GAME : KP_EVENT_LOAD_GAME, u32(g, b, size, 1), 0, 0, name);
		break;
	}
	case SPEED_COMMAND: /* ExecGameSpeed 0x473310 (a single player's) */
		if (byte_at(g, b, size, 1) < SPEEDS) kp_scalar_set(g, KP_SPEED, byte_at(g, b, size, 1));
		break;
	case STOP_COMMAND: order_selection(g, flag(b, size, 1), 0); break;
	case HOLD_POSITION_COMMAND: order_selection(g, flag(b, size, 1), 1); break;
	case SIEGE_COMMAND: case UNSIEGE_COMMAND: siege_selection(g, id == SIEGE_COMMAND, flag(b, size, 1)); break;
	case STIM_COMMAND: stim_selection(g); break;
	case RETURN_CARGO_COMMAND: return_cargo_selection(g, flag(b, size, 1)); break;
	case BUILD_COMMAND:
		kp_command_build(g, player, byte_at(g, b, size, 1), u16(g, b, size, 2), u16(g, b, size, 4), u16(g, b, size, 6),
			single_selected(g, player));
		break;
	case CONTROL_GROUP_COMMAND: kp_command_control_group(g, player, byte_at(g, b, size, 1), byte_at(g, b, size, 2)); break;
	case TRAIN_COMMAND: train(g, u16(g, b, size, 1)); break;
	case TRAIN_FIGHTER_COMMAND: train_fighters(g); break;
	case UNIT_MORPH_COMMAND: unit_morph(g, u16(g, b, size, 1)); break;
	case CANCEL_MORPH_COMMAND: cancel_morph(g, 1); break;
	case CANCEL_CONSTRUCTION_COMMAND: cancel_morph(g, 0); break;
	case CANCEL_TRAIN_COMMAND: cancel_train(g, u16(g, b, size, 1)); break;
	case LIFT_OFF_COMMAND:
		kp_command_lift_off(g, player, single_selected(g, player), kp_s16v(u16(g, b, size, 1)), kp_s16v(u16(g, b, size, 3)));
		break;
	case CANCEL_NUKE_COMMAND: kp_command_cancel_nuke(g, player); break;
	case CANCEL_ADDON_COMMAND: kp_command_cancel_addon(g, player, single_selected(g, player)); break;
	case BUILDING_MORPH_COMMAND:
		kp_command_building_morph(g, player, single_selected(g, player), u16(g, b, size, 1));
		break;
	case RESEARCH_COMMAND: case UPGRADE_COMMAND:
		kp_command_research(g, player, single_selected(g, player), byte_at(g, b, size, 1), id == UPGRADE_COMMAND);
		break;
	case CANCEL_RESEARCH_COMMAND: case CANCEL_UPGRADE_COMMAND:
		kp_command_cancel_research(g, player, single_selected(g, player), id == CANCEL_UPGRADE_COMMAND);
		break;
	case MERGE_ARCHON_COMMAND: case MERGE_DARK_ARCHON_COMMAND:
		kp_command_merge(g, player, id == MERGE_DARK_ARCHON_COMMAND);
		break;
	case UNLOAD_ALL_COMMAND: unload_selection(g, flag(b, size, 1)); break;
	case UNLOAD_COMMAND: unload_passenger(g, kp_unit_from_ref(g, u16(g, b, size, 1))); break;
	case CLOAK_COMMAND: case DECLOAK_COMMAND: cloak_selection(g, id == CLOAK_COMMAND); break;
	case BURROW_COMMAND: case UNBURROW_COMMAND: burrow_selection(g, id == BURROW_COMMAND, flag(b, size, 1)); break;
	default: {
		int64_t address = EXECUTOR;
		for (i = 0; i < (int)(sizeof HANDLERS / sizeof HANDLERS[0]); i++) {
			if (HANDLERS[i].id == id) address = HANDLERS[i].address;
		}
		kp_game_fail(g, "commands: command 0x%02x of player %lld (0x%llx)", id, (long long)player, (long long)address);
	}
	}
}
