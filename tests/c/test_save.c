/* Saves at a step boundary (version 1, kp_save/kp_load; plan D17, ADR 0008): a game saved in the middle of every
 * scenario of tests/golden/oracle/scenarios/ and loaded into a game of the same mission, fresh from its start or run on
 * to the scenario's end, continues exactly as the original did (the per-step CRCs of its state dumps), and saving the
 * loaded game gives the same bytes. The initial state of every mission survives a save and a load unchanged. Needs the
 * converter's generated data; skipped without it. */
#include "kp_test.h"

#define DIR "tests/golden/oracle/"
#define DATA "test data" /* the tests load saves into the same generated data */
#define WASTELAND_SAVE_STEP 3700 /* the Barracks of terran01's scripted victory under construction */
#define INTO_LAST (-1)

static const char *const FILES[] = { KP_ORACLE_DATA "dat.kpd", NULL };

static const KpJson *load_golden(KpT *T, const char *path)
{
	char full[512];
	snprintf(full, sizeof full, DIR "%s.json", path);
	return kp_t_golden(T, full);
}

/* The replay of golden g (its logs and CRCs), released at the end. */
static KpReplay *replay_of(KpT *T, const KpJson *g)
{
	KpReplay *r = kp_t_replay(T);
	KpError err;
	if (kp_replay_from_golden(r, ".", g, &err) != 0) kp_t_fail(T, "%s", err.msg);
	return r;
}

static KpOracleGame *start(KpT *T, const KpReplay *r)
{
	return kp_t_game(T, r->mission, r->seed, r->expansion, NULL);
}

/* The save of a game (released at the end). */
static uint8_t *save(KpT *T, KpGame *g, const char *mission, const char *data, size_t *size)
{
	uint8_t *bytes;
	KpError err;
	if (kp_save(g, mission, data, &bytes, size, &err) != 0) kp_t_fail(T, "%s", err.msg);
	kp_t_defer(T, free, bytes);
	return bytes;
}

static void load(KpT *T, KpGame *g, const uint8_t *bytes, size_t size, const char *mission, const char *data)
{
	KpError err;
	if (kp_load(g, bytes, size, mission, data, &err) != 0) kp_t_fail(T, "%s", err.msg);
}

/* Loads the save of `game` (golden g's game, run to step `mid` by `run`) into games of the same mission run to each
 * step of intos (INTO_LAST: the last); each must save the same bytes again and continue as the original's per-step
 * CRCs up to step stop (0: the last). */
static void continue_from(KpT *T, const char *name, const KpJson *g, const KpReplay *r, KpGame *game,
	const KpRunner *run, int64_t mid, const int64_t *intos, int count, KpBuf *bad, int64_t stop)
{
	const char *mission = kp_json_field_str(g, "mission", "");
	int64_t last = r->last;
	size_t size, again_size;
	uint8_t *bytes = save(T, game, mission, DATA, &size);
	int i;
	for (i = 0; i < count; i++) { /* the game that takes the save: fresh, or run on to the end */
		int mark = kp_t_mark(T);
		KpOracleGame *loaded = start(T, r);
		KpRunner to, cont;
		uint8_t *again;
		int64_t step, end = stop && stop < last ? stop : last;
		char into[32];
		if (intos[i] == INTO_LAST) snprintf(into, sizeof into, "last");
		else snprintf(into, sizeof into, "%lld", (long long)intos[i]);
		kp_runner_init(&to, loaded->g, r);
		kp_runner_to(&to, intos[i] == INTO_LAST ? last : intos[i]);
		load(T, loaded->g, bytes, size, mission, DATA);
		again = save(T, loaded->g, mission, DATA, &again_size);
		if (again_size != size || memcmp(again, bytes, size) != 0) kp_buf_printf(bad, "%s%s round trip", bad->size ? ", " : "", name);
		kp_runner_init(&cont, loaded->g, r);
		cont.step = run->step;
		cont.next_command = run->next_command;
		cont.next_ui = run->next_ui;
		for (step = mid + 1; step <= end; step++) {
			int n, at;
			kp_runner_to(&cont, step);
			at = kp_replay_step_crcs(r, step, &n);
			if (at >= 0) {
				KpStateCrcs crcs;
				KpBuf diffs;
				kp_t_ok(T, kp_state_crcs(loaded->g, step >= KP_ORACLE_DRAWN_STEP, &crcs) == 0, "out of memory");
				kp_buf_init(&diffs);
				if (kp_step_differences(&crcs, &r->crcs[at], n, &diffs) > 0) {
					kp_buf_printf(bad, "%s%s into %s step %lld%s", bad->size ? ", " : "", name, into, (long long)step,
						kp_buf_text(&diffs));
				}
				kp_buf_free(&diffs);
				if (bad->size) break;
			}
		}
		kp_t_release(T, mark);
	}
}

/* Golden g's game started and run to step mid, with its runner. */
static KpOracleGame *game_at(KpT *T, const KpReplay *r, int64_t mid, KpRunner *run)
{
	KpOracleGame *og = start(T, r);
	kp_runner_init(run, og->g, r);
	kp_runner_to(run, mid);
	return og;
}

/* Saves golden g's game at step mid and loads it into games of the same mission run to each step of intos. */
static void check_continuations(KpT *T, const char *name, const KpJson *g, int64_t mid, const int64_t *intos, int count,
	KpBuf *bad)
{
	int mark = kp_t_mark(T);
	const KpReplay *r = replay_of(T, g);
	KpRunner run;
	KpOracleGame *og = game_at(T, r, mid, &run);
	continue_from(T, name, g, r, og->g, &run, mid, intos, count, bad, 0);
	kp_t_release(T, mark);
}

static void saved_scenarios_continue_as_the_original(KpT *T)
{
	static const int64_t INTOS[2] = { 1, INTO_LAST };
	const KpJson *index;
	KpBuf *bad;
	int k;
	if (!kp_t_ready(T, "test_save", FILES)) return;
	index = load_golden(T, "scenarios/index");
	bad = kp_t_buf(T);
	for (k = 0; k < kp_json_len(index); k++) {
		const char *name = kp_json_str(kp_json_at(index, k), "");
		char path[256];
		const KpJson *g;
		int mark = kp_t_mark(T);
		int64_t last = 0;
		int i;
		if (!kp_t_mine_sampled(T, k + 1)) continue;
		snprintf(path, sizeof path, "scenarios/%s", name);
		g = load_golden(T, path);
		for (i = 0; i < kp_json_len(kp_json_get(g, "steps")); i++) {
			int64_t step = strtoll(kp_json_get(g, "steps")->keys[i], NULL, 10);
			if (step > last) last = step;
		}
		check_continuations(T, name, g, last / 2, INTOS, 2, bad);
		kp_t_release(T, mark);
	}
	kp_t_ok(T, bad->size == 0, "continuations after a load differ: %s", kp_buf_text(bad));
}

/* terran01 played to victory, saved while its Barracks is being built, Marines trained and SCVs harvest (plan phase 6
 * item 7), loaded into a fresh game. */
static void saved_wasteland_continues_as_the_original(KpT *T)
{
	static const int64_t INTOS[1] = { 1 };
	KpBuf *bad;
	if (!kp_t_ready(T, "test_save", FILES)) return;
	bad = kp_t_buf(T);
	check_continuations(T, "terran-wasteland", load_golden(T, "plays/terran-wasteland"), WASTELAND_SAVE_STEP, INTOS, 1, bad);
	kp_t_ok(T, bad->size == 0, "continuations after a load differ: %s", kp_buf_text(bad));
}

/* The scripted wins of the campaign rows (plan phase 8 item 4): each saved at every PLAY_SAVE_EVERY-th step and loaded
 * into a fresh game, which continues as the original did up to the next save step; the first save continues to the
 * play's end. One game per play runs on from save to save. */
#define PLAY_SAVE_EVERY 2000
#define STATUS_COMPLETED 0x1
#define STATUS_IN_TRANSPORT 0x40
#define NO_UNIT 228 /* an empty queue entry */
#define AI_ROSTER_SLOTS (8 * 64) /* the players' attack rosters (g.pai.roster) */

enum { CONSTRUCTION, TRAINING, CARGO, HIDDEN, AI_ROSTER, INTERRUPTED_KINDS };
static const char *const INTERRUPTED_NAMES[INTERRUPTED_KINDS] = { "construction", "training", "cargo", "hidden", "ai_roster" };
/* Where a save of a play catches each unfinished process of plan phase 8 item 4 (found by scanning the plays at
 * every PLAY_SAVE_EVERY-th step; the test below keeps them true). */
static const struct {
	const char *play;
	int64_t step;
} INTERRUPTED[INTERRUPTED_KINDS] = {
	{ "protoss-m22", 2000 }, { "protoss-m22", 2000 }, { "protoss-m29", 2000 }, { "protoss-m22", 2000 },
	{ "protoss-m22", 2000 },
};

/* The unfinished processes in game g at a save (found[kind] set). */
static void interrupted(KpGame *g, int *found)
{
	int64_t n = g->lists[KP_LIST_ACTIVE].head;
	int k;
	while (n != 0) {
		if ((g->u.status[n] & STATUS_COMPLETED) == 0) found[CONSTRUCTION] = 1;
		if (g->u.queue0[n] != NO_UNIT && g->u.queue0[n] != 0 && (g->u.status[n] & STATUS_COMPLETED) != 0) found[TRAINING] = 1;
		if ((g->u.status[n] & STATUS_IN_TRANSPORT) != 0 || g->u.loaded0[n] != 0) found[CARGO] = 1;
		n = g->u.next[n];
	}
	if (g->lists[KP_LIST_HIDDEN].head != 0) found[HIDDEN] = 1;
	for (k = 0; k < AI_ROSTER_SLOTS; k++) {
		if ((&g->pai.roster[0][0])[k] != 0) {
			found[AI_ROSTER] = 1;
			break;
		}
	}
}

static void saved_plays_continue_as_the_original(KpT *T)
{
	static const int64_t INTOS[1] = { 1 };
	const KpJson *index;
	KpBuf *bad;
	int k;
	if (!kp_t_ready(T, "test_save", FILES) || !kp_t_has(T, DIR "plays/index.json")) return;
	index = load_golden(T, "plays/index");
	bad = kp_t_buf(T);
	for (k = 0; k < kp_json_len(index); k++) {
		const char *name = kp_json_str(kp_json_at(index, k), "");
		char path[256];
		const KpJson *g;
		const KpReplay *r;
		KpRunner run;
		KpOracleGame *og;
		int64_t mid;
		int mark = kp_t_mark(T);
		if (!kp_t_mine_sampled(T, k + 1)) continue;
		snprintf(path, sizeof path, "plays/%s", name);
		g = load_golden(T, path);
		r = replay_of(T, g);
		og = game_at(T, r, 1, &run);
		for (mid = PLAY_SAVE_EVERY; mid <= r->last - 1; mid += PLAY_SAVE_EVERY) {
			kp_runner_to(&run, mid);
			continue_from(T, name, g, r, og->g, &run, mid, INTOS, 1, bad,
				mid == PLAY_SAVE_EVERY ? r->last : mid + PLAY_SAVE_EVERY);
		}
		kp_t_release(T, mark);
	}
	kp_t_ok(T, bad->size == 0, "continuations after a load differ: %s", kp_buf_text(bad));
}

/* The play saves between them catch every kind of unfinished process of INTERRUPTED (plan phase 8 item 4); a shard of
 * the test above sees only some plays, so this test runs the listed saves of its own. */
static void saved_plays_catch_every_interrupted_process(KpT *T)
{
	int found[INTERRUPTED_KINDS][INTERRUPTED_KINDS];
	int kind, k;
	if (!kp_t_ready(T, "test_save", FILES) || !kp_t_has(T, DIR "plays/index.json")) return;
	memset(found, 0, sizeof found);
	for (kind = 0; kind < INTERRUPTED_KINDS; kind++) {
		for (k = 0; k < kind; k++) { /* each save's game runs once */
			if (strcmp(INTERRUPTED[k].play, INTERRUPTED[kind].play) == 0 && INTERRUPTED[k].step == INTERRUPTED[kind].step) break;
		}
		if (k == kind) {
			char path[256];
			int mark = kp_t_mark(T);
			KpRunner run;
			KpOracleGame *og;
			snprintf(path, sizeof path, "plays/%s", INTERRUPTED[kind].play);
			og = game_at(T, replay_of(T, load_golden(T, path)), INTERRUPTED[kind].step, &run);
			interrupted(og->g, found[kind]);
			kp_t_release(T, mark);
		}
		kp_t_ok(T, found[k][kind], "%s at step %lld: a save in the middle of %s", INTERRUPTED[kind].play,
			(long long)INTERRUPTED[kind].step, INTERRUPTED_NAMES[kind]);
	}
}

/* Campaign runs whose computers are in the middle of AI work at step AI_SAVE_STEP (plan phase 7E), saved there and
 * loaded into a fresh game: scripts waiting, towns building and training, attack groups gathering, region states 3 and
 * 7, nukes armed. */
#define AI_SAVE_STEP 1001

static void saved_ai_runs_continue_as_the_original(KpT *T)
{
	static const char *const RUNS[] = { "terran-m04", "protoss-m28", "xterran-m44", "xterran-m46", "xzerg-m60" };
	static const int64_t INTOS[1] = { 1 };
	KpBuf *bad;
	size_t k;
	if (!kp_t_ready(T, "test_save", FILES)) return;
	bad = kp_t_buf(T);
	for (k = 0; k < sizeof RUNS / sizeof RUNS[0]; k++) {
		char path[256];
		snprintf(path, sizeof path, "runs/%s", RUNS[k]);
		check_continuations(T, RUNS[k], load_golden(T, path), AI_SAVE_STEP, INTOS, 1, bad);
	}
	kp_t_ok(T, bad->size == 0, "continuations after a load differ: %s", kp_buf_text(bad));
}

static void initial_states_survive_a_save(KpT *T)
{
	const KpJson *index;
	KpBuf *bad;
	int k;
	if (!kp_t_ready(T, "test_save", FILES)) return;
	index = load_golden(T, "initial_state/index");
	bad = kp_t_buf(T);
	for (k = 0; k < kp_json_len(index); k++) {
		const char *name = kp_json_str(kp_json_at(index, k), "");
		char path[512];
		const KpJson *g;
		KpOracleGame *game, *loaded;
		KpBuf *a, *b;
		uint8_t *bytes;
		size_t size;
		int mark = kp_t_mark(T);
		uint32_t seed;
		int expansion;
		snprintf(path, sizeof path, "initial_state/%s", name);
		g = load_golden(T, path);
		seed = (uint32_t)kp_json_field_int(g, "seed", 0);
		expansion = (int)kp_json_field_int(g, "expansion", 0);
		snprintf(path, sizeof path, "generated/missions/%s.kpm", kp_json_field_str(g, "mission", ""));
		game = kp_t_game(T, path, seed, expansion, NULL);
		bytes = save(T, game->g, kp_json_field_str(g, "mission", ""), DATA, &size);
		loaded = kp_t_game(T, path, seed, expansion, NULL);
		load(T, loaded->g, bytes, size, kp_json_field_str(g, "mission", ""), DATA);
		a = kp_t_buf(T);
		b = kp_t_buf(T);
		kp_t_ok(T, kp_state_lines(loaded->g, 0, a) == 0 && kp_state_lines(game->g, 0, b) == 0, "out of memory");
		if (a->size != b->size || memcmp(a->data, b->data, a->size) != 0) kp_buf_printf(bad, "%s%s", bad->size ? ", " : "", name);
		kp_t_release(T, mark);
	}
	kp_t_ok(T, bad->size == 0, "initial states changed by a save and a load: %s", kp_buf_text(bad));
}

/* A load that must be refused: 1 when it is, with its message in err. */
static int refused(KpGame *g, const uint8_t *bytes, size_t size, const char *mission, const char *data, KpError *err)
{
	return kp_load(g, bytes, size, mission, data, err) != 0;
}

static void loads_reject_other_saves(KpT *T)
{
	const char *path = "generated/scenarios/marine_group.kpm";
	KpOracleGame *game, *other;
	uint8_t *bytes, *damaged, *mine;
	size_t size, mine_size;
	KpError err;
	KpBuf *before, *after;
	char other_path[512];
	const KpJson *first;
	const char *id;
	int k;
	if (!kp_t_ready(T, "test_save", FILES)) return;
	game = kp_t_game(T, path, 1, 0, NULL);
	bytes = save(T, game->g, "a", DATA, &size);
	kp_t_ok(T, refused(kp_t_game(T, path, 1, 0, NULL)->g, bytes, size, "b", DATA, &err) && strstr(err.msg, "mission a"),
		"another mission's save is refused: %s", err.msg);
	kp_t_ok(T, refused(kp_t_game(T, path, 1, 0, NULL)->g, bytes, size, "a", "other", &err) && strstr(err.msg, "other game data"),
		"a save of other data is refused: %s", err.msg);
	damaged = (uint8_t *)malloc(size);
	kp_t_ok(T, damaged != NULL, "out of memory");
	kp_t_defer(T, free, damaged);
	memcpy(damaged, bytes, size);
	damaged[100] = (uint8_t)(damaged[100] + 1);
	kp_t_ok(T, refused(kp_t_game(T, path, 1, 0, NULL)->g, damaged, size, "a", DATA, &err) && strstr(err.msg, "fingerprint"),
		"a damaged save is refused: %s", err.msg);
	kp_t_ok(T, refused(kp_t_game(T, path, 1, 0, NULL)->g, bytes, 50, "a", DATA, &err), "a truncated save is refused: %s", err.msg);
	/* A save whose header passes but whose state does not fit the game (another map under the same names) is refused
	 * before anything is written: the game keeps its state. */
	first = load_golden(T, "initial_state/index");
	snprintf(other_path, sizeof other_path, "initial_state/%s", kp_json_str(kp_json_at(first, 0), ""));
	snprintf(other_path, sizeof other_path, "generated/missions/%s.kpm", kp_json_field_str(load_golden(T, other_path), "mission", ""));
	other = kp_t_game(T, other_path, 1, 0, NULL);
	before = kp_t_buf(T);
	after = kp_t_buf(T);
	kp_t_ok(T, kp_state_lines(other->g, 0, before) == 0, "out of memory");
	kp_t_ok(T, refused(other->g, bytes, size, "a", DATA, &err), "a save of another map is refused: %s", err.msg);
	kp_t_ok(T, kp_state_lines(other->g, 0, after) == 0, "out of memory");
	kp_t_ok(T, before->size == after->size && memcmp(before->data, after->data, before->size) == 0, "the refused save changed nothing");
	/* The default data identity is the game's files: it travels with a save made without one. */
	game = kp_t_game(T, path, 1, 0, NULL);
	id = game->identity;
	for (k = 0; id[k]; k++) {
		char c = id[k];
		kp_t_ok(T, (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || c == '-', "the identity: %s", id);
	}
	kp_t_ok(T, strlen(id) == 6 * 8 + 5, "the identity: %s", id);
	mine = save(T, game->g, "a", game->identity, &mine_size);
	{
		KpOracleGame *fresh = kp_t_game(T, path, 1, 0, NULL);
		kp_t_ok(T, !refused(fresh->g, mine, mine_size, "a", fresh->identity, &err), "the same files load: %s", err.msg);
	}
	kp_t_ok(T, refused(other->g, mine, mine_size, "a", other->identity, &err) && strstr(err.msg, "other game data"),
		"other files are refused: %s", err.msg);
}

/* The countdown's text is not saved: a load sets it from the loaded countdown, not the game's before the load (an
 * in-game load writes into the game shown). */
static void loads_show_the_loaded_countdown(KpT *T)
{
	const char *path = "generated/scenarios/marine_group.kpm";
	KpOracleGame *game, *into;
	uint8_t *none, *running;
	size_t none_size, running_size;
	if (!kp_t_ready(T, "test_save", FILES)) return;
	game = kp_t_game(T, path, 1, 0, NULL);
	game->g->trig.countdown = 0;
	none = save(T, game->g, "a", DATA, &none_size);
	game->g->trig.countdown = 125;
	running = save(T, game->g, "a", DATA, &running_size);
	into = kp_t_game(T, path, 1, 0, NULL);
	into->g->trig.countdown = 720;
	into->g->trig.countdown_text = 720; /* 12:00 shown */
	load(T, into->g, none, none_size, "a", DATA);
	kp_t_eq(T, into->g->trig.countdown_text, -1, "a save with no countdown hides the text at once");
	load(T, into->g, running, running_size, "a", DATA);
	kp_t_eq(T, into->g->trig.countdown_text, 125, "a save's countdown is shown from the load");
}

static const KpTest TESTS[] = {
	{ "saved_scenarios_continue_as_the_original", saved_scenarios_continue_as_the_original, 6 },
	{ "saved_wasteland_continues_as_the_original", saved_wasteland_continues_as_the_original, 0 },
	{ "saved_plays_continue_as_the_original", saved_plays_continue_as_the_original, 8 },
	{ "saved_plays_catch_every_interrupted_process", saved_plays_catch_every_interrupted_process, 0 },
	{ "saved_ai_runs_continue_as_the_original", saved_ai_runs_continue_as_the_original, 0 },
	{ "initial_states_survive_a_save", initial_states_survive_a_save, 0 },
	{ "loads_reject_other_saves", loads_reject_other_saves, 0 },
	{ "loads_show_the_loaded_countdown", loads_show_the_loaded_countdown, 0 },
};
KP_SUITE(kp_suite_save, "test_save", TESTS);
