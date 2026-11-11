/* The stops a player can reach (kp_game_fail), found by trying what the interface lets a player do: for every unit
 * type a player commands, the commands a selected unit takes (a right click, every targeted order the order's own
 * requirement gate permits, the unit-bound commands) against each kind of target, each on a fresh game and followed by
 * STEPS steps. A case that stops the game is reported with its type, command and target; the test fails for any.
 * Runs on idle_marines' map; needs the converter's generated data and is skipped without it. */
#include "kp_test.h"
#include "kp_ids.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAP "scenarios/idle_marines"
#define SEED 946684819
#define STEPS 60
#define ORDERS 189
#define TYPES 228
#define NO_TYPE 228
#define TARGETED_ORDER 0x15
#define RIGHT_CLICK 0x14
#define UNIT_X 1000
#define UNIT_Y 1100
#define DAT_SUBUNIT 0x10
#define FIRST_FLAG_BEACON 197 /* the Flag Beacons, the Capture the Flag type of game's: no mission places or creates them */
#define LAST_FLAG_BEACON 199

typedef enum { T_GROUND, T_ENEMY, T_OWN, T_MINERAL, T_GEYSER, T_BUILDING, T_KINDS } Target;
static const char *const TARGET_NAMES[T_KINDS] = { "ground", "enemy", "own", "mineral", "geyser", "building" };

/* A command: a targeted order or right click at a target, a parameterless command, or one with a 16-bit parameter. */
typedef enum { C_RIGHT_CLICK, C_ORDER, C_SIMPLE, C_PARAM } Kind;
typedef struct {
	int type;
	Kind kind;
	int id; /* the order for C_ORDER, the command byte else */
	int param; /* C_PARAM: the 16-bit parameter */
	Target target;
} Case;

#define MESSAGES 64
typedef struct {
	char text[160];
	char example[160];
	int count;
} Message;

typedef struct {
	Message messages[MESSAGES];
	int used, cases, failed;
} Report;

static const char *const FILES[] = { KP_T_DATA, MAP, NULL };

/* Whether a player can have a unit of the type under his command: a subunit (a turret) is made with its base, and the
 * Flag Beacons (idle order CTFCOP_INIT, 0x99, ported with the Capture the Flag game type) are no campaign's. */
static int commandable(KpGame *g, int type)
{
	if (type >= FIRST_FLAG_BEACON && type <= LAST_FLAG_BEACON) return 0;
	return (KP_DAT(g, units, special_ability_flags, type) & DAT_SUBUNIT) == 0;
}

static const uint8_t SIMPLE[] = {
	0x1a, 0x1b, 0x1c, 0x1e, 0x21, 0x22, 0x25, 0x26, 0x27, 0x28, 0x2a, 0x2b, 0x2c, 0x2d, 0x36, 0x5a, 0x2e, 0x34, 0x31, 0x33,
	0x18, 0x19,
};
/* Commands that carry a unit type (train, morph, building morph) and one that carries a technology or upgrade id. */
static const uint8_t TYPED[] = { 0x1f, 0x23, 0x35 };
static const uint8_t IDS[] = { 0x30, 0x32 };

typedef struct {
	KpOracleGame *og;
	KpGame *g;
	int64_t unit, own, enemy, mineral, geyser, building;
} World;

static void prime_player(KpGame *g)
{
	int k;
	g->players.minerals[0] = 50000;
	g->players.gas[0] = 50000;
	for (k = 0; k < KP_TECH_IDS; k++) g->players.tech_available[0][k] = g->players.tech_researched[0][k] = 1;
	for (k = 0; k < KP_UPGRADE_IDS; k++) kp_set_upgrade_level(g, 0, k, g->players.upgrade_max[0][k]);
}

/* A game on the map with the unit under test and one of each kind of target around it. */
static int make_world(KpT *T, World *w, int type)
{
	KpGame *g;
	KpReplay empty;
	KpRunner run;
	w->og = kp_t_game(T, MAP, SEED, 1, NULL);
	g = w->g = w->og->g;
	prime_player(g);
	w->unit = kp_create_map_unit(g, type, UNIT_X, UNIT_Y, 0);
	if (w->unit == 0) return 0;
	w->own = kp_create_map_unit(g, KP_TYPE_MARINE, UNIT_X - 200, UNIT_Y, 0);
	w->enemy = kp_create_map_unit(g, KP_TYPE_MARINE, UNIT_X + 200, UNIT_Y, 1);
	w->building = kp_create_map_unit(g, KP_TYPE_SUPPLY_DEPOT, UNIT_X, UNIT_Y + 200, 0);
	w->mineral = kp_create_map_unit(g, KP_TYPE_FIRST_MINERAL, UNIT_X + 200, UNIT_Y + 200, 11);
	w->geyser = kp_create_map_unit(g, KP_TYPE_GEYSER, UNIT_X - 200, UNIT_Y + 200, 11);
	kp_replay_init(&empty);
	kp_runner_init(&run, g, &empty);
	kp_runner_to(&run, 4);
	return 1;
}

static int64_t target_unit(const World *w, Target t)
{
	switch (t) {
	case T_ENEMY: return w->enemy;
	case T_OWN: return w->own;
	case T_MINERAL: return w->mineral;
	case T_GEYSER: return w->geyser;
	case T_BUILDING: return w->building;
	default: return 0;
	}
}

static void put_point(KpGame *g, uint8_t *cmd, int64_t unit)
{
	kp_le_put16(cmd + 1, (uint32_t)(UNIT_X + 150));
	kp_le_put16(cmd + 3, (uint32_t)(UNIT_Y - 100));
	kp_le_put16(cmd + 5, unit ? (uint32_t)kp_unit_ref(g, unit) : 0);
	kp_le_put16(cmd + 7, NO_TYPE);
}

typedef struct {
	const Case *c;
	World w;
	KpRunner run;
	KpReplay empty;
} Run;

static void run_case(KpT *T, void *ud)
{
	Run *r = (Run *)ud;
	const Case *c = r->c;
	World *w = &r->w;
	KpGame *g;
	uint8_t cmd[16];
	int64_t ref, step;
	size_t size = 3;
	if (!make_world(T, w, c->type)) return;
	g = w->g;
	ref = kp_unit_ref(g, w->unit);
	kp_command_select(g, 0, &ref, 1);
	memset(cmd, 0, sizeof cmd);
	switch (c->kind) {
	case C_RIGHT_CLICK:
		cmd[0] = RIGHT_CLICK;
		put_point(g, cmd, target_unit(w, c->target));
		size = 10;
		break;
	case C_ORDER:
		cmd[0] = TARGETED_ORDER;
		put_point(g, cmd, target_unit(w, c->target));
		cmd[9] = (uint8_t)c->id;
		size = 11;
		break;
	case C_SIMPLE:
		cmd[0] = (uint8_t)c->id;
		break;
	case C_PARAM:
		cmd[0] = (uint8_t)c->id;
		kp_le_put16(cmd + 1, (uint32_t)c->param);
		size = 3;
		break;
	}
	kp_execute_command(g, 0, cmd, size);
	kp_replay_init(&r->empty);
	kp_runner_init(&r->run, g, &r->empty);
	for (step = 4; step < 4 + STEPS; step++) kp_runner_to(&r->run, step + 1);
}

static void record(Report *rep, const Case *c, const char *msg)
{
	char example[160];
	int k;
	snprintf(example, sizeof example, "type %d command %d%s%d target %s", c->type, c->kind == C_ORDER ? c->id : c->id,
		c->kind == C_PARAM ? " param " : " ", c->kind == C_PARAM ? c->param : 0, TARGET_NAMES[c->target]);
	for (k = 0; k < rep->used; k++) {
		if (strcmp(rep->messages[k].text, msg) == 0) {
			rep->messages[k].count++;
			return;
		}
	}
	if (rep->used == MESSAGES) return;
	snprintf(rep->messages[rep->used].text, sizeof rep->messages[0].text, "%s", msg);
	snprintf(rep->messages[rep->used].example, sizeof rep->messages[0].example, "%s", example);
	rep->messages[rep->used++].count = 1;
}

static void try_case(KpT *T, Report *rep, const Case *c)
{
	Run r;
	char msg[256];
	int mark = kp_t_mark(T);
	memset(&r, 0, sizeof r);
	r.c = c;
	rep->cases++;
	if (kp_t_pcall(T, run_case, &r, msg, sizeof msg) != 0) {
		rep->failed++;
		record(rep, c, msg);
	}
	kp_t_release(T, mark);
}

/* The order's gate as the executor tests it (ExecuteTargetedOrderForSelection 0x49cc40). */
static int order_allowed(KpGame *g, int64_t unit, int order)
{
	int64_t tech = KP_DAT(g, orders, technology_energy, order);
	return (tech == KP_TECH_NONE ? kp_can_issue_order(g, order, 0, unit) : kp_can_use_tech(g, tech, 0, unit)) == 1;
}

typedef struct {
	int type, allowed, trainable_count;
	int allowed_orders[ORDERS], trainable[TYPES];
} Probe;

/* What the unit of a type may be told: the orders its gate permits and the unit types it may train. */
static void probe_type(KpT *T, void *ud)
{
	Probe *p = (Probe *)ud;
	World w;
	int order, t;
	p->allowed = p->trainable_count = 0;
	if (!make_world(T, &w, p->type)) return;
	for (order = 0; order < ORDERS; order++) {
		if (order_allowed(w.g, w.unit, order)) p->allowed_orders[p->allowed++] = order;
	}
	for (t = 0; t < TYPES; t++) {
		if (kp_can_train_unit(w.g, t, 0, w.unit) == 1) p->trainable[p->trainable_count++] = t;
	}
}

static void player_reach(KpT *T)
{
	Report rep;
	Probe *p;
	int type, t, k, n = 0;
	KpGame *data;
	if (!kp_t_ready(T, "test_player_reach", FILES)) return;
	p = (Probe *)calloc(1, sizeof *p);
	memset(&rep, 0, sizeof rep);
	data = kp_t_game(T, MAP, SEED, 1, NULL)->g;
	for (type = 0; type < TYPES; type++) {
		Case c;
		char msg[256];
		int mark;
		if (!commandable(data, type) || !kp_t_mine_sampled(T, ++n)) continue;
		mark = kp_t_mark(T);
		p->type = type;
		c.type = type;
		c.kind = C_SIMPLE;
		c.id = 0;
		c.param = 0;
		c.target = T_GROUND;
		if (kp_t_pcall(T, probe_type, p, msg, sizeof msg) != 0) {
			rep.cases++;
			rep.failed++;
			record(&rep, &c, msg);
			kp_t_release(T, mark);
			continue;
		}
		kp_t_release(T, mark);
		for (t = 0; t < T_KINDS; t++) {
			c.target = (Target)t;
			c.kind = C_RIGHT_CLICK;
			c.id = RIGHT_CLICK;
			c.param = 0;
			try_case(T, &rep, &c);
			for (k = 0; k < p->allowed; k++) {
				c.kind = C_ORDER;
				c.id = p->allowed_orders[k];
				try_case(T, &rep, &c);
			}
		}
		c.target = T_GROUND;
		c.kind = C_SIMPLE;
		for (k = 0; k < (int)sizeof SIMPLE; k++) {
			c.id = SIMPLE[k];
			try_case(T, &rep, &c);
		}
		c.kind = C_PARAM;
		for (k = 0; k < (int)sizeof TYPED; k++) {
			int j;
			for (j = 0; j < p->trainable_count; j++) {
				c.id = TYPED[k];
				c.param = p->trainable[j];
				try_case(T, &rep, &c);
			}
		}
		for (k = 0; k < (int)sizeof IDS; k++) {
			int j;
			for (j = 0; j < KP_TECH_IDS; j++) {
				c.id = IDS[k];
				c.param = j;
				try_case(T, &rep, &c);
			}
		}
	}
	free(p);
	kp_t_log(T, "player_reach: %d cases, %d stopped the game", rep.cases, rep.failed);
	for (k = 0; k < rep.used; k++) kp_t_log(T, "  %4d x %s (first: %s)", rep.messages[k].count, rep.messages[k].text, rep.messages[k].example);
	kp_t_ok(T, rep.failed == 0, "%d of %d player cases stop the game, %d distinct messages", rep.failed, rep.cases, rep.used);
}

static const KpTest TESTS[] = {
	{ "player_reach", player_reach, 8 },
};
KP_SUITE(kp_suite_player_reach, "test_player_reach", TESTS);
