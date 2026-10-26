/* The two requirement interpreters against the original's code (plan decision D16): the general
 * order/research/tech-use/upgrade evaluator 0x407570 with its wrappers 0x407bb0, 0x407dc0, 0x407f90, 0x408140 and its
 * offset initializers, the in-progress bits 0x4a0560/0x4a0590, and the unit-production gate CanTrainUnit 0x4b32a0
 * (docs/research/native-dat-requirements.md, native-requirements.md).
 *
 * tests/golden/oracle/requirements_general.json (482 cases) and requirements_units.json (182 cases) hold every isolated
 * original-code probe, exported by tools/oracle/export_requirement_cases.py. Each case is rebuilt on a native game
 * started from a scenario map: its first unit is the probe's unit record and its second the probe's zeroed `target`
 * record; the header's `fixture` is the probe's new() and the case's `patch` its case builder. The probes' synthetic
 * requirement tables run through the evaluator's program override (kp_requirement_program). Each case checks the
 * return value and the requirement error (0x6be654); offsets cases check every offset of a domain, in-progress cases
 * the bit. No case is skipped. Needs the converter's generated data; skipped without it. */
#include "kp_test.h"

#define GENERAL "tests/golden/oracle/requirements_general.json"
#define UNITS "tests/golden/oracle/requirements_units.json"
#define MAP "generated/scenarios/idle_marines.kpm"
#define SEED 946684819
#define PLAYERS 12
#define UNIT_TYPES 228
#define TECHS 44
#define UPGRADES 61
#define TECH_PROGRESS_BYTES 6
#define UPGRADE_PROGRESS_BYTES 8
#define PLAYER 0 /* the probes' player unless a case names one */
#define SHOWN 20 /* failures listed in a failing test's message */

/* The requirement domains (kp_game.h KP_REQ_*) with their offset columns and capacities. */
typedef struct {
	const char *name;
	int id;
	const char *table, *column;
	int count;
} Domain;

static const Domain DOMAINS[] = {
	{ "orders", KP_REQ_ORDERS, "orders", "requirements", 189 },
	{ "research", KP_REQ_RESEARCH, "techdata", "research_requirements", TECHS },
	{ "tech_use", KP_REQ_TECH_USE, "techdata", "use_requirements", TECHS },
	{ "upgrades", KP_REQ_UPGRADES, "upgrades", "requirements", UPGRADES },
	{ "units", KP_REQ_UNITS, "units", "requirements", UNIT_TYPES },
};
#define DOMAIN_COUNT 5

static const Domain *domain(const char *name)
{
	int k;
	for (k = 0; k < DOMAIN_COUNT; k++) {
		if (strcmp(DOMAINS[k].name, name) == 0) return &DOMAINS[k];
	}
	return NULL;
}

/* The unit fields the requirement code reads, zero in the probes' unit records unless their new() sets them. */
static const char *const UNIT_FIELDS[] = { "owner", "order", "status", "type", "queue0", "queue1", "queue2", "queue3",
	"queue4", "queue_slot", "secondary_order", "union_0xc0", "union_0xc8", "union_0xd0", "build_unit", "order_unit",
	"ai", "subunit", "lockdown_timer", "stasis_timer", "maelstrom_timer" };

static const char *const FILES[] = { KP_ORACLE_DATA "dat.kpd", MAP, NULL };

/* A game of the probes' map (events on, no campaign row). */
static KpOracleGame *new_game(KpT *T, int expansion)
{
	KpOracleOpts o;
	kp_oracle_opts(&o);
	o.events = 1;
	o.mission_index = 0;
	o.name = "mission";
	return kp_t_game(T, MAP, SEED, expansion, &o);
}

static int64_t floor_div(int64_t a, int64_t b) { return a / b - ((a % b != 0) && ((a < 0) != (b < 0))); }

/* Writes value v into byte b of a unit field (the probes' w8 into a wider field). */
static void set_byte(int64_t *array, int64_t n, int64_t b, int64_t v)
{
	int64_t scale = (int64_t)1 << (8 * b), old = array[n];
	int64_t cur = floor_div(old, scale) % 256;
	if (cur < 0) cur += 256;
	array[n] = old + (v - cur) * scale;
}

static int64_t op_int(const KpJson *op, int k) { return kp_json_int(kp_json_at(op, k), 0); }
static const char *op_str(const KpJson *op, int k) { return kp_json_str(kp_json_at(op, k), ""); }

static int64_t expansion_of(const KpJson *f, const KpJson *c)
{
	const KpJson *patch = kp_json_get(c, "patch");
	int k;
	for (k = 0; k < kp_json_len(patch); k++) {
		const KpJson *op = kp_json_at(patch, k);
		if (strcmp(op_str(op, 0), "expansion") == 0) return op_int(op, 1);
	}
	return kp_json_field_int(f, "expansion", 0);
}

typedef struct {
	KpOracleGame *og;
	int64_t unit, other;
} Fixture;

/* The probe's new(): fixture f (the golden's header) for case c. */
static void fixture(KpT *T, const KpJson *f, const KpJson *c, Fixture *fx)
{
	KpOracleGame *og = new_game(T, (int)expansion_of(f, c));
	KpGame *g = og->g;
	KpPlayers *pl = &g->players;
	const KpJson *dat = kp_json_get(f, "producer_dat");
	int64_t unit = g->lists[KP_LIST_ACTIVE].head, other = g->u.next[unit];
	int64_t producer = kp_json_field_int(c, "producer", kp_json_field_int(f, "producer", 0));
	size_t k;
	int p, i, d;
	for (k = 0; k < sizeof UNIT_FIELDS / sizeof UNIT_FIELDS[0]; k++) {
		int64_t *a = kp_t_unit_field(T, g, UNIT_FIELDS[k]);
		a[unit] = 0;
		a[other] = 0;
	}
	g->u.type[unit] = producer;
	g->u.status[unit] = kp_json_field_int(f, "status", 0);
	g->u.order[unit] = kp_json_field_int(f, "order", 0);
	g->u.union_0xc8[unit] = kp_json_field_int(f, "tech_byte", 0) + kp_json_field_int(f, "upgrade_byte", 0) * 256;
	g->u.queue0[unit] = g->u.queue1[unit] = g->u.queue2[unit] = g->u.queue3[unit] = g->u.queue4[unit] =
		kp_json_field_int(f, "queue", 0);
	for (i = 0; i < UNIT_TYPES * PLAYERS; i++) {
		g->count_completed[i] = kp_json_field_int(f, "count_completed", 0);
		g->count_all[i] = kp_json_field_int(f, "count_all", 0);
	}
	for (p = 0; p < PLAYERS; p++) {
		for (i = 0; i < TECHS; i++) {
			if (kp_json_get(f, "tech_available")) pl->tech_available[p][i] = kp_json_field_int(f, "tech_available", 0);
			if (kp_json_get(f, "tech_researched")) pl->tech_researched[p][i] = kp_json_field_int(f, "tech_researched", 0);
		}
		for (i = 0; i < UPGRADES; i++) {
			pl->upgrades[p][i] = kp_json_field_int(f, "upgrade_level", 0);
			if (kp_json_get(f, "upgrade_max")) pl->upgrade_max[p][i] = kp_json_field_int(f, "upgrade_max", 0);
		}
		if (kp_json_get(f, "unit_available")) {
			for (i = 0; i < UNIT_TYPES; i++) pl->unit_available[p][i] = kp_json_field_int(f, "unit_available", 0);
		}
		if (kp_json_get(f, "research_progress")) {
			for (i = 0; i < TECH_PROGRESS_BYTES; i++) pl->research_progress[p][i] = kp_json_field_int(f, "research_progress", 0);
		}
		if (kp_json_get(f, "upgrade_progress")) {
			for (i = 0; i < UPGRADE_PROGRESS_BYTES; i++) pl->upgrade_progress[p][i] = kp_json_field_int(f, "upgrade_progress", 0);
		}
	}
	for (i = 0; i < kp_json_len(dat); i++) kp_t_dat_set(T, og, "units", dat->keys[i], producer, kp_json_int(dat->items[i], 0));
	for (d = 0; d < DOMAIN_COUNT; d++) {
		for (i = 0; i < DOMAINS[d].count; i++) {
			kp_t_dat_set(T, og, DOMAINS[d].table, DOMAINS[d].column, i, kp_json_field_int(f, "offsets", 0));
		}
	}
	kp_scalar_set(g, KP_CHEATS, kp_json_field_int(f, "cheats", 0));
	kp_scalar_set(g, KP_REQUIREMENT_ERROR, 0);
	fx->og = og;
	fx->unit = unit;
	fx->other = other;
}

/* The case's patch over the fixture. */
static void apply(KpT *T, const Fixture *fx, const KpJson *c)
{
	KpOracleGame *og = fx->og;
	KpGame *g = og->g;
	KpPlayers *pl = &g->players;
	const KpJson *patch = kp_json_get(c, "patch"), *table = kp_json_get(c, "table");
	int k;
	for (k = 0; k < kp_json_len(patch); k++) {
		const KpJson *op = kp_json_at(patch, k);
		const char *kind = op_str(op, 0);
		if (strcmp(kind, "u") == 0) {
			kp_t_unit_field(T, g, op_str(op, 1))[fx->unit] = op_int(op, 2);
		} else if (strcmp(kind, "u8") == 0) {
			set_byte(kp_t_unit_field(T, g, op_str(op, 1)), fx->unit, op_int(op, 2), op_int(op, 3));
		} else if (strcmp(kind, "u_other") == 0) {
			kp_t_unit_field(T, g, op_str(op, 1))[fx->unit] = fx->other;
		} else if (strcmp(kind, "other") == 0) {
			kp_t_unit_field(T, g, op_str(op, 1))[fx->other] = op_int(op, 2);
		} else if (strcmp(kind, "dat") == 0) {
			kp_t_dat_set(T, og, op_str(op, 1), op_str(op, 2), op_int(op, 3), op_int(op, 4));
		} else if (strcmp(kind, "count_completed") == 0) {
			g->count_completed[op_int(op, 1) * PLAYERS + PLAYER] = op_int(op, 2);
		} else if (strcmp(kind, "count_all") == 0) {
			g->count_all[op_int(op, 1) * PLAYERS + PLAYER] = op_int(op, 2);
		} else if (strcmp(kind, "available") == 0) {
			pl->tech_available[PLAYER][op_int(op, 1)] = op_int(op, 2);
		} else if (strcmp(kind, "researched") == 0) {
			pl->tech_researched[PLAYER][op_int(op, 1)] = op_int(op, 2);
		} else if (strcmp(kind, "upgrade") == 0) {
			pl->upgrades[PLAYER][op_int(op, 1)] = op_int(op, 2);
		} else if (strcmp(kind, "unit_available") == 0) {
			pl->unit_available[PLAYER][op_int(op, 1)] = op_int(op, 2);
		} else if (strcmp(kind, "research_progress") == 0) {
			pl->research_progress[op_int(op, 1)][op_int(op, 2)] = op_int(op, 3);
		} else if (strcmp(kind, "upgrade_progress") == 0) {
			pl->upgrade_progress[op_int(op, 1)][op_int(op, 2)] = op_int(op, 3);
		} else if (strcmp(kind, "cheats") == 0) {
			kp_scalar_set(g, KP_CHEATS, op_int(op, 1));
		} else if (strcmp(kind, "offset") == 0) {
			const Domain *d = domain(kp_json_field_str(c, "call", ""));
			kp_t_ok(T, d != NULL, "an offset patch of call %s", kp_json_field_str(c, "call", ""));
			kp_t_dat_set(T, og, d->table, d->column, op_int(op, 1), op_int(op, 2));
		} else if (strcmp(kind, "expansion") != 0) { /* the game's start took the expansion */
			kp_t_fail(T, "unknown patch operation %s", kind);
		}
	}
	if (table) {
		int64_t words[KP_REQ_PROGRAM_WORDS];
		int n = kp_json_len(table), i;
		kp_t_ok(T, n <= KP_REQ_PROGRAM_WORDS, "a program of %d words", n);
		for (i = 0; i < n; i++) words[i] = kp_json_int(kp_json_at(table, i), 0);
		kp_requirement_program(g, words, n);
	}
}

/* Runs case c into diff ("" when it equals the original). */
static void run(KpT *T, const KpJson *f, const KpJson *c, char *diff, size_t size)
{
	const char *call = kp_json_field_str(c, "call", "");
	int64_t player = kp_json_field_int(c, "player", PLAYER), id = kp_json_field_int(c, "id", 0), ret, err;
	Fixture fx;
	KpGame *g;
	fixture(T, f, c, &fx);
	apply(T, &fx, c);
	g = fx.og->g;
	diff[0] = '\0';
	if (strcmp(call, "offsets") == 0) {
		const Domain *d = domain(kp_json_field_str(c, "domain", ""));
		const KpJson *offsets = kp_json_get(c, "offsets");
		int i;
		kp_t_ok(T, d != NULL, "an offsets case of domain %s", kp_json_field_str(c, "domain", ""));
		for (i = 0; i < d->count; i++) {
			int64_t got = kp_requirement_offset(g, d->id, i), want = kp_json_int(kp_json_at(offsets, i), -1);
			if (got != want) {
				snprintf(diff, size, "offset %d: got %lld, original %lld", i, (long long)got, (long long)want);
				return;
			}
		}
		return;
	}
	if (strcmp(call, "research_progress") == 0 || strcmp(call, "upgrade_progress") == 0) {
		int got = kp_in_progress(g, player, id, strcmp(call, "upgrade_progress") == 0) != 0;
		int64_t want = kp_json_field_int(c, "ret", 0);
		if (got != (want != 0)) snprintf(diff, size, "in progress %s, original bit %lld", got ? "true" : "false", (long long)want);
		return;
	}
	if (strcmp(call, "evaluate") == 0) ret = kp_evaluate_requirements(g, id, player, fx.unit, KP_REQ_ORDERS, kp_json_field_int(c, "at", 0));
	else if (strcmp(call, "orders") == 0) ret = kp_can_issue_order(g, id, player, fx.unit);
	else if (strcmp(call, "research") == 0) ret = kp_can_research(g, id, player, fx.unit);
	else if (strcmp(call, "tech_use") == 0) ret = kp_can_use_tech(g, id, player, fx.unit);
	else if (strcmp(call, "upgrades") == 0) ret = kp_can_upgrade(g, id, player, fx.unit);
	else if (strcmp(call, "units") == 0) ret = kp_can_train_unit(g, id, player, fx.unit);
	else kp_t_fail(T, "unknown call %s", call);
	err = kp_scalar(g, KP_REQUIREMENT_ERROR);
	if (ret != kp_json_field_int(c, "ret", -1) || err != kp_json_field_int(c, "err", -1)) {
		snprintf(diff, size, "got %lld error %lld, original %lld error %lld", (long long)ret, (long long)err,
			(long long)kp_json_field_int(c, "ret", -1), (long long)kp_json_field_int(c, "err", -1));
	}
}

typedef struct {
	const KpJson *f, *c;
	char diff[256], failure[512];
} Case;

static void run_case(KpT *T, void *ud)
{
	Case *one = (Case *)ud;
	run(T, one->f, one->c, one->diff, sizeof one->diff);
}

static void check(KpT *T, const char *path)
{
	const KpJson *golden, *cases, *f;
	KpBuf *bad;
	int k, failures = 0;
	if (!kp_t_ready(T, "test_requirements", FILES)) return;
	golden = kp_t_golden(T, path);
	cases = kp_json_get(golden, "cases");
	f = kp_json_get(golden, "fixture");
	bad = kp_t_buf(T);
	for (k = 0; k < kp_json_len(cases); k++) {
		Case one;
		char id[32];
		int mark = kp_t_mark(T);
		one.f = f;
		one.c = kp_json_at(cases, k);
		one.diff[0] = '\0';
		/* a case's failure (kp_t_pcall) is its difference, not the test's end */
		if (kp_t_pcall(T, run_case, &one, one.failure, sizeof one.failure) != 0) snprintf(one.diff, sizeof one.diff, "%s", one.failure);
		kp_t_release(T, mark);
		if (one.diff[0]) {
			if (kp_json_get(one.c, "id")) snprintf(id, sizeof id, "%lld", (long long)kp_json_field_int(one.c, "id", 0));
			else snprintf(id, sizeof id, "nil");
			if (failures++ < SHOWN) kp_buf_printf(bad, "\n%d %s (id %s): %s", k + 1, kp_json_field_str(one.c, "kind", ""), id, one.diff);
		}
	}
	kp_t_ok(T, failures == 0, "%d of %d cases differ:%s", failures, kp_json_len(cases), kp_buf_text(bad));
}

static void general_evaluator_equals_the_original(KpT *T) { check(T, GENERAL); }

/* The program override leaves no offsets behind: those resolved by walking its words while it is set (an id whose DAT
 * offset is 0xffff) give way to the DAT's own once it is cleared, as on a game that never had it. */
static void program_override_drops_its_offsets(KpT *T)
{
	static const int64_t WORDS[6] = { 0, 5, 0xff02, 7, 0xffff, 0xffff };
	const Domain *d = &DOMAINS[0];
	KpOracleGame *g, *fresh;
	int k;
	if (!kp_t_ready(T, "test_requirements", FILES)) return;
	g = new_game(T, 0);
	fresh = new_game(T, 0);
	kp_t_dat_set(T, g, d->table, d->column, 5, 0xffff);
	kp_t_dat_set(T, fresh, d->table, d->column, 5, 0xffff);
	kp_requirement_program(g->g, WORDS, 6);
	kp_t_eq(T, kp_requirement_offset(g->g, d->id, 5), 2, "the offset resolved on the override's words");
	kp_requirement_program(g->g, NULL, -1);
	for (k = 0; k < d->count; k++) {
		kp_t_eq(T, kp_requirement_offset(g->g, d->id, k), kp_requirement_offset(fresh->g, d->id, k), "offset %d after the override", k);
	}
}

static void unit_gate_equals_the_original(KpT *T) { check(T, UNITS); }

static const KpTest TESTS[] = {
	{ "general_evaluator_equals_the_original", general_evaluator_equals_the_original, 0 },
	{ "program_override_drops_its_offsets", program_override_drops_its_offsets, 0 },
	{ "unit_gate_equals_the_original", unit_gate_equals_the_original, 0 },
};
KP_SUITE(kp_suite_requirements, "test_requirements", TESTS);
