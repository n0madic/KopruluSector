/* Test scenarios (phase 3, level C) against the original, object step by object step: each scenario of
 * tests/golden/oracle/scenarios/index.json (and each campaign run of runs/index.json) starts from its CHK with the
 * original's seed, replays the original's command and interface logs and is compared at every recorded step with the
 * CRCs of the original's state dumps (tools/state_check.py; from step 3 on without the drawn image bounds); a scenario
 * with triggers also compares its trigger trace (plan phase 5 item 8), and one with AI scripts its AI trace (plan phase
 * 7) by the trace's CRC-32. The campaign plays and chains (plan phase 8) are checked the same way, the plays to their
 * victory and the chains through the campaign's progression. Needs the converter's generated data; skipped without
 * it. */
#include "kp_test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DIR "tests/golden/oracle/"
#define RESULT_VICTORY 3
#define RESULT_STEPS 100 /* a play's dumps (every 20th step) end when the result dialog pauses the game */

static const char *const DATA_FILES[] = { KP_T_DATA, NULL };

static const KpJson *golden(KpT *T, const char *kind, const char *name)
{
	char path[512];
	snprintf(path, sizeof path, DIR "%s/%s.json", kind, name);
	return kp_t_golden(T, path);
}

/* check_run: the run of golden g checked against its steps and traces; differences appended to bad. */
static KpGoldenRun *check_run(KpT *T, const KpJson *g, const char *name, const KpOracleOpts *opts, KpBuf *bad, int *found)
{
	int trace = kp_json_get(g, "trace") != NULL || kp_json_get(g, "ai_trace") != NULL;
	KpGoldenRun *gr = kp_t_golden_run(T, g, opts, trace);
	*found = kp_golden_run_check(gr, g, name, bad);
	return gr;
}

/* The runs of an index of goldens (scenarios, or runs: campaign missions run idle), each checked on its own. */
static void check_runs(KpT *T, const char *kind, KpBuf *bad)
{
	const KpJson *index;
	char path[512];
	int k;
	snprintf(path, sizeof path, DIR "%s/index.json", kind);
	index = kp_t_golden(T, path);
	for (k = 0; k < kp_json_len(index); k++) {
		const char *name = kp_json_str(kp_json_at(index, k), "");
		int found, mark = kp_t_mark(T);
		if (!kp_t_mine_sampled(T, k + 1)) continue;
		check_run(T, golden(T, kind, name), name, NULL, bad, &found);
		kp_t_release(T, mark); /* the run's game and golden go now, not at the test's end */
	}
}

static void scenarios_equal_the_original(KpT *T)
{
	KpBuf *bad;
	if (!kp_t_ready(T, "test_scenarios", DATA_FILES)) return;
	bad = kp_t_buf(T);
	check_runs(T, "scenarios", bad);
	kp_t_ok(T, bad->size == 0, "categories differ from the original: %s", kp_buf_text(bad));
}

/* Campaign missions run idle (tests/golden/oracle/runs: their own triggers, plan phase 5) at the compared steps. */
static void campaign_runs_equal_the_original(KpT *T)
{
	KpBuf *bad;
	if (!kp_t_ready(T, "test_scenarios", DATA_FILES)) return;
	bad = kp_t_buf(T);
	check_runs(T, "runs", bad);
	kp_t_ok(T, bad->size == 0, "categories differ from the original: %s", kp_buf_text(bad));
}

/* The scripted wins of the campaign rows (tests/golden/oracle/plays, plan phase 8): the emulator's plays at every 20th
 * step by the state's fingerprint and every 500th by its categories, and each play's result is the local player's
 * victory. */
static void campaign_plays_equal_the_original(KpT *T)
{
	const KpJson *index;
	KpBuf *bad;
	int k;
	if (!kp_t_ready(T, "test_scenarios", DATA_FILES)) return;
	if (!kp_t_has(T, DIR "plays/index.json")) return;
	index = kp_t_golden(T, DIR "plays/index.json");
	bad = kp_t_buf(T);
	for (k = 0; k < kp_json_len(index); k++) {
		const char *name = kp_json_str(kp_json_at(index, k), "");
		int found, mark = kp_t_mark(T);
		KpGoldenRun *gr;
		if (!kp_t_mine_sampled(T, k + 1)) continue;
		gr = check_run(T, golden(T, "plays", name), name, NULL, bad, &found);
		if (found == 0) { /* the result comes within the compared stride after the last dumped step */
			KpGame *g = gr->game.g;
			int64_t step = gr->replay.last;
			while (g->results[g->local_player] == 0 && step < gr->replay.last + RESULT_STEPS) kp_runner_to(&gr->run, ++step);
			if (g->results[g->local_player] != RESULT_VICTORY) kp_buf_printf(bad, "%s%s no victory", bad->size ? ", " : "", name);
		}
		kp_t_release(T, mark);
	}
	kp_t_ok(T, bad->size == 0, "plays differ from the original: %s", kp_buf_text(bad));
}

/* A chain's games played by check_run: the runs alive until the chain lets them go (the next game's carry). */
#define CHAIN_GAMES 32
typedef struct {
	KpT *T;
	const char *before; /* the name of the game before ("" none) */
	KpGoldenRun *runs[CHAIN_GAMES];
	int count;
} Chain;

static KpOracleGame *chain_play(void *ud, const KpJson *g, const char *name, const KpOracleOpts *opts, KpBuf *why)
{
	Chain *c = (Chain *)ud;
	KpT *T = c->T;
	const char *from = kp_json_field_str(g, "carry_from", "");
	KpGoldenRun *gr;
	KpError err;
	int found;
	if (strcmp(from, c->before) != 0) {
		kp_buf_printf(why, "carries from %s", from[0] ? from : "nil");
		return NULL;
	}
	gr = (KpGoldenRun *)malloc(sizeof *gr);
	if (!gr) kp_t_fail(T, "out of memory");
	if (kp_golden_run_start(gr, ".", g, opts, kp_json_get(g, "trace") != NULL || kp_json_get(g, "ai_trace") != NULL,
		&err) != 0) {
		free(gr);
		kp_buf_str(why, err.msg);
		return NULL;
	}
	if (c->count >= CHAIN_GAMES) kp_t_fail(T, "a chain of more than %d games", CHAIN_GAMES);
	c->runs[c->count++] = gr;
	kp_t_guard(T, gr->game.g);
	found = kp_golden_run_check(gr, g, name, why);
	if (found) return NULL;
	kp_runner_to(&gr->run, gr->replay.last + 1); /* the step whose trigger pass opened the result dialog */
	c->before = name;
	return &gr->game;
}

static void chain_release(void *ud, KpOracleGame *game)
{
	Chain *c = (Chain *)ud;
	int k;
	for (k = 0; k < c->count; k++) {
		if (c->runs[k] && &c->runs[k]->game == game) {
			kp_t_golden_run_release(c->runs[k]);
			c->runs[k] = NULL;
		}
	}
}

static void free_chain(void *p)
{
	Chain *c = (Chain *)p;
	int k;
	for (k = 0; k < c->count; k++) {
		if (c->runs[k]) kp_t_golden_run_release(c->runs[k]);
	}
	free(c);
}

/* The games of the campaign chains (tests/golden/oracle/chains, plan phase 8): the emulator played each chain through
 * the original's progression (every mission won with "there is no cow level", one lost with "game over man"). Each
 * game starts with what the game before it in its chain left (KpCarry: Koprulu's own game before, as the campaign runs
 * them), equals the original at the compared steps, and after its result step the campaign's next mission
 * (kp_campaign.h, with the game's own result and next scenario) is the chain's next game. */
static void campaign_chains_equal_the_original(KpT *T)
{
	static const char *const FILES[] = { KP_T_DATA, NULL };
	const KpJson *index;
	KpBuf *bad;
	int k, chains = 0;
	if (!kp_t_ready(T, "test_scenarios", FILES)) return;
	index = kp_t_golden(T, DIR "chains/index.json");
	bad = kp_t_buf(T);
	for (k = 0; k < kp_json_len(index);) {
		/* a chain: the names with one prefix before ".", in the index's order */
		const char *first = kp_json_str(kp_json_at(index, k), "");
		size_t prefix = strcspn(first, ".");
		KpChainGame games[CHAIN_GAMES];
		int count = 0, i;
		while (k < kp_json_len(index)) {
			const char *name = kp_json_str(kp_json_at(index, k), "");
			if (strcspn(name, ".") != prefix || strncmp(name, first, prefix) != 0) break;
			if (count < CHAIN_GAMES) {
				games[count].name = name;
				games[count].golden = NULL;
				count++;
			}
			k++;
		}
		chains++;
		if (!kp_t_mine(T, chains)) continue;
		{
			Chain *c = (Chain *)calloc(1, sizeof *c);
			KpBuf failed;
			KpError err;
			int r;
			if (!c) kp_t_fail(T, "out of memory");
			c->T = T;
			c->before = "";
			kp_t_defer(T, free_chain, c);
			for (i = 0; i < count; i++) games[i].golden = golden(T, "chains", games[i].name);
			kp_buf_init(&failed);
			r = kp_campaign_chain(".", games, count, chain_play, NULL, chain_release, c, &failed, &err);
			if (r < 0) kp_t_fail(T, "%s", err.msg);
			if (r > 0) kp_buf_printf(bad, "%s%s", bad->size ? ", " : "", kp_buf_text(&failed));
			kp_buf_free(&failed);
		}
	}
	kp_t_ok(T, bad->size == 0, "chain games differ from the original: %s", kp_buf_text(bad));
}

/* The flags of the trigger copy (kp_dump's "trigger <copy> ... flags=N" line), or -1. */
static int64_t trigger_flags(KpT *T, KpGame *g, const char *copy)
{
	KpBuf lines;
	char prefix[64];
	const char *p;
	int64_t flags = -1;
	kp_buf_init(&lines);
	kp_t_ok(T, kp_state_lines(g, 0, &lines) == 0, "out of memory");
	snprintf(prefix, sizeof prefix, "trigger %s ", copy);
	for (p = kp_buf_text(&lines); *p;) {
		const char *end = strchr(p, '\n');
		size_t n = end ? (size_t)(end - p) : strlen(p);
		if (strncmp(p, prefix, strlen(prefix)) == 0) {
			const char *f = strstr(p, "flags=");
			if (f && f < p + n) {
				flags = strtoll(f + 6, NULL, 10);
				break;
			}
		}
		p += n + (end ? 1 : 0);
	}
	kp_buf_free(&lines);
	return flags;
}

/* The interface's Escape skips only while a trigger of the local player still runs (kp_trigger_skippable): a finished
 * trigger keeps its executing flag 0x01 next to the disabled flag 0x08, which the original's skip marks too. */
static void escape_skips_running_triggers_only(KpT *T)
{
	KpGoldenRun *gr;
	if (!kp_t_ready(T, "test_scenarios", DATA_FILES)) return;
	gr = kp_t_golden_run(T, golden(T, "scenarios", "trigger_basics"), NULL, 0);
	kp_runner_to(&gr->run, 5);
	kp_t_eq(T, kp_trigger_skippable(gr->game.g), 1, "a Wait runs");
	kp_runner_to(&gr->run, 128);
	kp_t_eq(T, trigger_flags(T, gr->game.g, "0.1"), 9, "a finished trigger keeps 0x01");
	kp_t_eq(T, kp_trigger_skippable(gr->game.g), 0, "only finished triggers");
	gr = kp_t_golden_run(T, golden(T, "scenarios", "trigger_cinematic_full"), NULL, 0);
	kp_runner_to(&gr->run, 5);
	kp_t_eq(T, kp_trigger_skippable(gr->game.g), 0, "a finished trigger and an unskippable transmission");
}

static const KpTest TESTS[] = {
	{ "scenarios_equal_the_original", scenarios_equal_the_original, 6 },
	{ "campaign_runs_equal_the_original", campaign_runs_equal_the_original, 4 },
	{ "campaign_chains_equal_the_original", campaign_chains_equal_the_original, 4 },
	{ "campaign_plays_equal_the_original", campaign_plays_equal_the_original, 8 },
	{ "escape_skips_running_triggers_only", escape_skips_running_triggers_only, 0 },
};
KP_SUITE(kp_suite_scenarios, "test_scenarios", TESTS);
