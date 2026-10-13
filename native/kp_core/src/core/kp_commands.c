/* Game commands of the turn (kp_game.h; game/simulation/commands.lua, the specification). */
#include "kp_game_internal.h"

#define EXECUTOR 0x475d30
#define SYNC 0x37
#define SELECT 0x09
#define RIGHT_CLICK 0x14
#define TARGETED_ORDER 0x15
#define STOP_COMMAND 0x1a
#define HOLD_POSITION_COMMAND 0x2b
#define NO_TECH 44
#define ORDER_ATTACK 8
#define ORDER_ATTACK_MOVE 14
#define ORDER_HEAL_MOVE 177
#define ORDER_RALLY_UNIT 39
#define ORDER_RALLY_POINT 40
#define ORDER_PICKUP 112
#define ORDER_CONSUME 119
#define ORDER_NUKE_LAUNCH 125
#define ORDER_FOLLOW 49
#define BEACON_FIRST 0xc2
#define BEACON_LAST 0xc7
#define DAT_POWERUP 0x800
#define STATUS_BURROWED 0x10
#define STATUS_GROUNDED_BUILDING 0x2
#define MEDIC 0x22
#define DEFILER 0x2e
#define UNCLEAN_ONE 0x34
#define ORDER_STOP 1
#define ORDER_MOVE 6
#define ORDER_MOVE_FOW 0x85
#define NO_ORDER 0x17
#define NO_TYPE 0xe4
#define STATUS_HALLUCINATION 0x40000000
#define USER_RIGHT_CLICKED 2
#define HOLD_POSITION_ORDER 0x6b
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
	{ 0x05, 0x4720c0 }, { 0x06, 0x472f50 }, { 0x07, 0x4729e0 }, { 0x08, 0x472e20 }, { 0x09, 0x472fc0 },
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
	case 0: case 15: case 20: case 25: case 29: case 30: case 31: case 32: case 33: case 36: case 46: case 71: case 72:
	case 73: case 80: case 81: case 82: case 83: case 85: case 86: case 87: case 88: case 89: case 155:
		return 1;
	}
	return 0;
}

static int building_targeted(int64_t o) { return o == 39 || o == 40 || o == 67 || o == 95; }

/* Current orders that a queued command replaces instead of queueing behind (0x42a8b0). */
static int idle_order(int64_t o) { return o == 2 || o == 3 || o == 0x17 || o == 0x5d || o == 0x98 || o == 0xaf; }

/* GetHoldPositionOrder 0x41fae0. */
static int64_t hold_order(int64_t t)
{
	switch (t) {
	case 0x48: case 0x52: return 0x39;
	case 0x53: case 0x51: return 0x3e;
	case 0x2d: case 0x31: return 0x6c;
	case 0x2f: case 0x32: return 0x88;
	case 0x22: return 0xb2;
	}
	return HOLD_POSITION_ORDER;
}

/* The WORD at byte offset `at` of a command (a short command fails as the Lua code's nil
 * arithmetic does). */
static int64_t u16(KpGame *g, const uint8_t *b, size_t size, size_t at)
{
	if (at + 1 >= size) kp_game_fail(g, "commands: command of %u bytes read at %u", (unsigned)size, (unsigned)at);
	return b[at] | (int64_t)b[at + 1] << 8;
}

/* A flag byte: a missing byte reads nil in Lua, and nil ~= 0. */
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
			KP_LUA2(g, "report_error", u->owner[n], STRING_QUEUE_FULL);
		} else if (g->orders.allocated >= QUEUE_POOL_LIMIT) {
			KP_LUA2(g, "report_error", u->owner[n], STRING_QUEUE_POOL_FULL);
		} else if (kind == KIND_UNIT) {
			int64_t tx = 0, ty = 0;
			if (target != 0) {
				int32_t s = kp_unit_sprite(g, target);
				tx = g->w->spr.x[s];
				ty = g->w->spr.y[s];
			}
			kp_order_queue(g, n, order, tx, ty, target, NO_TYPE);
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
	issue_command(g, n, queued, order, KIND_UNIT, 0, 0, target, NO_TYPE);
}

static void issue_command_at_point(KpGame *g, int64_t n, int queued, int64_t order, int64_t fow, int64_t x, int64_t y)
{
	issue_command(g, n, queued, order, KIND_POINT, x, y, 0, fow);
}

/* The right-click handlers by GetUnitRightClickAction 0x422d90 (table 0x508e18). */
static int64_t right_click_order(KpGame *g, int64_t action, int64_t n, int64_t target, int64_t fow, int64_t *keep)
{
	KpUnits *u = &g->u;
	int64_t tt;
	*keep = target;
	switch (action) {
	case 0:
		*keep = 0;
		return NO_ORDER; /* 0x47ef40 */
	case 1: /* RightClickOrderNormal 0x47ef50 */
		if (kp_band(u->status[n], 2) != 0) return NO_ORDER;
		if (target == 0) return fow != NO_TYPE ? ORDER_MOVE_FOW : ORDER_MOVE;
		tt = u->type[target];
		if ((tt >= BEACON_FIRST && tt <= BEACON_LAST) || kp_band(KP_DAT(g, units, special_ability_flags, tt), DAT_POWERUP) != 0) {
			return ORDER_MOVE;
		}
		if (kp_is_enemy(g, n, target)) return ORDER_ATTACK;
		if (kp_is_transport(g, target)) kp_game_fail(g, "commands: right click on a transport (0x437440)");
		return kp_band(u->status[target], STATUS_BURROWED) != 0 ? ORDER_MOVE : ORDER_FOLLOW;
	case 2: kp_game_fail(g, "commands: right click of a rally producer (0x47f070)");
	case 3: kp_game_fail(g, "commands: right click of a burrowed Lurker (0x47f010)");
	case 4: kp_game_fail(g, "commands: right click of a harvester (0x47f2c0)");
	case 5: kp_game_fail(g, "commands: right click of a repairer (0x47f180)");
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
	if (target != 0) fow = NO_TYPE;
	else if (fow != NO_TYPE) kp_game_fail(g, "commands: right click on a fog-of-war thingy (0x418210)");
	count = kp_selected_units(g, player, units);
	for (k = 0; k < count; k++) {
		int64_t n = units[k], action = kp_right_click_action(g, n), order, t;
		if (kp_band(u->status[n], STATUS_HALLUCINATION) != 0 && (action == 4 || action == 5)) action = 1;
		kp_scalar_set(g, KP_SECOND_ORDER, NO_ORDER);
		order = right_click_order(g, action, n, target, fow, &t);
		if (order == 0x27) {
			kp_game_fail(g, "commands: right click into a transport (0x41bbf0)");
		} else if (order != NO_ORDER && n != target) {
			if (order == 0x28) kp_game_fail(g, "commands: right click of a transport (0x41bbc0)");
			if (order == ORDER_ATTACK) kp_game_fail(g, "commands: right-click attack of an enemy (0x47f7b3, phase 4)");
			if (kp_can_issue_order(g, order, player, n) == 1) {
				if (t != 0) {
					issue_command_on_unit(g, n, queued, order, t);
					if (kp_scalar(g, KP_SECOND_ORDER) != NO_ORDER) issue_command_on_unit(g, n, queued, kp_scalar(g, KP_SECOND_ORDER), t);
				} else {
					int64_t px = x, py = y;
					if (fow == NO_TYPE) kp_formation_point(g, n, &f, &px, &py);
					issue_command_at_point(g, n, queued, order, fow, px, py);
					if (kp_scalar(g, KP_SECOND_ORDER) != NO_ORDER) {
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
		int64_t n = units[k], order = hold ? hold_order(g->u.type[n]) : ORDER_STOP;
		if (kp_can_issue_order(g, order, player, n) == 1) issue_command_on_unit(g, n, queued, order, 0);
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
		ok = tech == NO_TECH ? kp_can_issue_order(g, order, player, n) : kp_can_use_tech(g, tech, player, n);
		status = u->status[n];
		building = kp_band(status, STATUS_GROUNDED_BUILDING) != 0;
		t = u->type[n];
		if (ok != 1) {
			/* CommandErrorFeedback 0x472210: interface feedback only. */
		} else if (n == target
			&& !((building && (order == ORDER_RALLY_UNIT || order == ORDER_RALLY_POINT))
				|| ((t == DEFILER || t == UNCLEAN_ONE) && order == ORDER_CONSUME)
				|| (kp_is_transport(g, n) && order == ORDER_PICKUP))) {
			/* a unit does not target itself */
		} else if (!(building && building_targeted(order)) && kp_right_click_action(g, n) == 0) {
			/* units that take no right click take no targeted order */
		} else if (u->order[n] == ORDER_NUKE_LAUNCH && order != 0) {
			/* a launching silo keeps its order */
		} else {
			int64_t main = order, sub = NO_ORDER;
			int skip = 0, q;
			if (t == MEDIC && (order == ORDER_ATTACK_MOVE || order == ORDER_ATTACK)) {
				main = ORDER_HEAL_MOVE;
			} else if (order == ORDER_ATTACK) {
				int64_t turret;
				main = target != 0 ? KP_DAT(g, units, attack_unit, t) : KP_DAT(g, units, attack_move, t);
				skip = main == NO_ORDER;
				turret = u->subunit[n];
				if (!skip && turret != 0 && kp_band(KP_DAT(g, units, special_ability_flags, u->type[turret]), 0x10) != 0) {
					sub = target != 0 ? KP_DAT(g, units, attack_unit, u->type[turret]) : KP_DAT(g, units, attack_move, u->type[turret]);
				}
			}
			if (!skip) {
				if (main == ORDER_RALLY_UNIT || main == ORDER_RALLY_POINT) {
					kp_game_fail(g, "commands: rally point of a targeted order (0x41bbf0, 0x41bbc0)");
				}
				q = queued != 0;
				if (target != 0) {
					issue_command_on_unit(g, n, q, main, target);
					if (sub != NO_ORDER) issue_command_on_unit(g, u->subunit[n], q, sub, target);
				} else {
					int64_t px = x, py = y;
					if (fow == NO_TYPE) kp_formation_point(g, n, &f, &px, &py);
					issue_command_at_point(g, n, q, main, fow, px, py);
					if (sub != NO_ORDER) issue_command_at_point(g, u->subunit[n], q, sub, fow, px, py);
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
	if (id != SELECT && id != RIGHT_CLICK && id != TARGETED_ORDER && id != STOP_COMMAND && id != HOLD_POSITION_COMMAND) {
		int64_t address = EXECUTOR;
		for (i = 0; i < (int)(sizeof HANDLERS / sizeof HANDLERS[0]); i++) {
			if (HANDLERS[i].id == id) address = HANDLERS[i].address;
		}
		kp_game_fail(g, "commands: command 0x%02x of player %lld (0x%llx)", id, (long long)player, (long long)address);
	}
	kp_scalar_set(g, KP_COMMAND_PLAYER, player);
	if (id == SELECT) {
		int64_t refs[256];
		int count = (int)byte_at(g, b, size, 1), k;
		for (k = 1; k <= count; k++) refs[k - 1] = u16(g, b, size, (size_t)(2 * k));
		kp_command_select(g, player, refs, count);
	} else if (id == RIGHT_CLICK) {
		int64_t x = u16(g, b, size, 1), y = u16(g, b, size, 3);
		if (x >= g->map_w_px || y >= g->map_h_px) return;
		right_click(g, x, y, kp_unit_from_ref(g, u16(g, b, size, 5)), u16(g, b, size, 7), flag(b, size, 9));
	} else if (id == TARGETED_ORDER) {
		int64_t x = u16(g, b, size, 1), y = u16(g, b, size, 3);
		if (x >= g->map_w_px || y >= g->map_h_px) return;
		targeted_order(g, kp_s16v(x), kp_s16v(y), kp_unit_from_ref(g, u16(g, b, size, 5)), u16(g, b, size, 7),
			byte_at(g, b, size, 9), flag(b, size, 10));
	} else if (id == STOP_COMMAND) {
		order_selection(g, flag(b, size, 1), 0);
	} else {
		order_selection(g, flag(b, size, 1), 1);
	}
}
