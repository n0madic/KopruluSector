/* Unit references after slots are freed and reused (plan §7 phase 3 acceptance): at every step of every scenario of
 * tests/golden/oracle/scenarios/, the unit references held by live units (targets, turrets, union words that hold
 * units, queued order targets) and by the selections name live units only; a packed reference (UnitToPackedRef
 * 0x428bd0) to a unit whose record was recycled and allocated again resolves to no unit. Needs the converter's
 * generated data; skipped without it. */
#include "kp_test.h"

#define DIR "tests/golden/oracle/scenarios/"
#define UNITS 1700 /* the unit record pool */
#define UNION_WORDS 7

/* The units of the active, hidden and revealer lists and their turrets. */
static void live_units(KpGame *g, uint8_t *live)
{
	static const int LISTS[3] = { KP_LIST_ACTIVE, KP_LIST_HIDDEN, KP_LIST_SPECIAL };
	int k;
	memset(live, 0, KP_UNITS + 1);
	for (k = 0; k < 3; k++) {
		int64_t n = g->lists[LISTS[k]].head;
		while (n != 0) {
			live[n] = 1;
			if (g->u.subunit[n] != 0) live[g->u.subunit[n]] = 1;
			n = g->u.next[n];
		}
	}
}

typedef struct {
	const uint8_t *live;
	KpBuf *bad;
	int count;
} Check;

static void check(Check *c, const char *holder, int64_t n, const char *what, int64_t k, int64_t v)
{
	if (v == 0 || (v > 0 && v <= KP_UNITS && c->live[v])) return;
	if (c->count++ == 0) {
		if (n >= 0) kp_buf_printf(c->bad, "%lld %s -> %lld", (long long)n, what, (long long)v);
		else kp_buf_printf(c->bad, "%s %lld -> %lld", holder, (long long)k, (long long)v);
	}
}

/* The dangling references of game g: their count, the first as text in bad. */
static int dangling(KpGame *g, KpBuf *bad)
{
	static uint8_t live[KP_UNITS + 1];
	const int64_t *unions[UNION_WORDS];
	Check c;
	int64_t n;
	int p, k;
	unions[0] = g->u.union_0xc0;
	unions[1] = g->u.union_0xc4;
	unions[2] = g->u.union_0xc8;
	unions[3] = g->u.union_0xcc;
	unions[4] = g->u.union_0xd0;
	unions[5] = g->u.union_0xd4;
	unions[6] = g->u.union_0xd8;
	live_units(g, live);
	c.live = live;
	c.bad = bad;
	c.count = 0;
	for (n = 1; n <= KP_UNITS; n++) {
		static const char *const UNION_NAMES[UNION_WORDS] = { "union_0xc0", "union_0xc4", "union_0xc8", "union_0xcc",
			"union_0xd0", "union_0xd4", "union_0xd8" };
		int64_t node;
		if (!live[n]) continue;
		check(&c, NULL, n, "move_unit", 0, g->u.move_unit[n]);
		check(&c, NULL, n, "order_unit", 0, g->u.order_unit[n]);
		check(&c, NULL, n, "auto_target", 0, g->u.auto_target[n]);
		check(&c, NULL, n, "connected", 0, g->u.connected[n]);
		check(&c, NULL, n, "build_unit", 0, g->u.build_unit[n]);
		check(&c, NULL, n, "irradiated_by", 0, g->u.irradiated_by[n]);
		check(&c, NULL, n, "subunit", 0, g->u.subunit[n]);
		for (k = 0; k < UNION_WORDS; k++) {
			if (g->u.union_units[n] & ((int64_t)1 << k)) check(&c, NULL, n, UNION_NAMES[k], 0, unions[k][n]);
		}
		for (node = g->u.queue_head[n]; node != -1; node = g->orders.next[node]) {
			check(&c, NULL, n, "queued order target", 0, g->orders.unit[node]);
		}
	}
	for (k = 0; k < KP_SLOTS; k++) check(&c, "local selection", -1, NULL, k, g->local_selection[k]);
	for (p = 0; p < 8; p++) {
		char holder[32];
		snprintf(holder, sizeof holder, "selection of player %d", p);
		for (k = 0; k < KP_SLOTS; k++) check(&c, holder, -1, NULL, k, g->selections[p][k]);
	}
	return c.count;
}

static void live_units_reference_live_units(KpT *T)
{
	static const char *const FILES[] = { KP_ORACLE_DATA "dat.kpd", NULL };
	const KpJson *index;
	KpBuf *bad;
	int k;
	if (!kp_t_ready(T, "test_references", FILES)) return;
	index = kp_t_golden(T, DIR "index.json");
	bad = kp_t_buf(T);
	for (k = 0; k < kp_json_len(index); k++) {
		const char *name = kp_json_str(kp_json_at(index, k), "");
		char path[512];
		KpGoldenRun *gr;
		int64_t step;
		int mark = kp_t_mark(T);
		if (!kp_t_mine(T, k + 1)) continue;
		snprintf(path, sizeof path, DIR "%s.json", name);
		gr = kp_t_golden_run(T, kp_t_golden(T, path), NULL, 0);
		for (step = 1; step <= gr->replay.last; step++) {
			KpBuf *first = kp_t_buf(T);
			kp_runner_to(&gr->run, step);
			if (dangling(gr->game.g, first) > 0) {
				kp_buf_printf(bad, "%s%s step %lld: %s", bad->size ? "; " : "", name, (long long)step, kp_buf_text(first));
				break;
			}
			kp_t_release(T, kp_t_mark(T) - 1);
		}
		kp_t_release(T, mark); /* the run goes now */
	}
	kp_t_ok(T, bad->size == 0, "dangling unit references: %s", kp_buf_text(bad));
}

static void packed_references_to_reused_records_resolve_to_no_unit(KpT *T)
{
	static const char *const FILES[] = { KP_ORACLE_DATA "dat.kpd", NULL };
	KpOracleGame *og;
	KpGame *g;
	KpReplay empty;
	KpRunner run;
	int64_t ns[KP_UNITS], refs[KP_UNITS], n;
	int count = 0, k, reused = 0, records = 0, list;
	KpBuf none;
	if (!kp_t_ready(T, "test_references.packed", FILES)) return;
	og = kp_t_game(T, "generated/scenarios/placement_fail.kpm", 946684819, 0, NULL);
	g = og->g;
	/* The units on unwalkable ground die at once; remember their packed references. */
	for (n = g->lists[KP_LIST_HIDDEN].head; n != 0; n = g->u.next[n]) {
		ns[count] = n;
		refs[count++] = kp_unit_ref(g, n);
	}
	kp_t_ok(T, count > 0, "the scenario has units that cannot be placed");
	kp_replay_init(&empty);
	kp_runner_init(&run, g, &empty);
	kp_runner_to(&run, 4);
	for (k = 0; k < count; k++) kp_t_eq(T, kp_unit_from_ref(g, refs[k]), 0, "a recycled unit's reference resolves to no unit");
	/* Allocations take the recycled records again, with a new uniqueness. */
	for (k = 0; k < count; k++) {
		int64_t m = kp_unit_create(g, 0, 1000, 1150, 0);
		int i;
		for (i = 0; i < count; i++) {
			if (m == ns[i]) {
				reused++;
				kp_t_eq(T, kp_unit_from_ref(g, refs[i]), 0, "the old reference to a reused record resolves to no unit");
				kp_t_eq(T, kp_unit_from_ref(g, kp_unit_ref(g, m)), m, "the new reference resolves to the new unit");
			}
		}
	}
	kp_t_ok(T, reused > 0, "recycled records are allocated again");
	kp_buf_init(&none);
	k = dangling(g, &none);
	kp_buf_free(&none);
	kp_t_eq(T, k, 0, "no dangling references after the reuse");
	/* Every record of the pool is on exactly one of the unit lists, and there are UNITS of them. */
	for (list = 0; list < KP_LIST_COUNT; list++) {
		for (n = g->lists[list].head; n != 0; n = g->u.next[n]) records++;
	}
	kp_t_eq(T, records, UNITS, "the pool size");
}

static const KpTest TESTS[] = {
	{ "live_units_reference_live_units", live_units_reference_live_units, 4 },
	{ "packed_references_to_reused_records_resolve_to_no_unit", packed_references_to_reused_records_resolve_to_no_unit, 0 },
};
KP_SUITE(kp_suite_references, "test_references", TESTS);
