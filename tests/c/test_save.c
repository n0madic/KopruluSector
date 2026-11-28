/* The state walk (kp_save.c; plan D17, ADR 0008, ADR 0021): it covers every byte of the game's state or leaves it out
 * on purpose; the state hash is the hash of the save's bytes; the walk's fingerprint belongs to its save version. Saves
 * at a step boundary (version 2): a game saved in the middle of every scenario of tests/golden/oracle/scenarios/ and
 * loaded into a game of the same mission, fresh from its start or run on to the scenario's end, continues exactly as
 * the original did (the per-step CRCs of its state dumps), with the same state hash, and saving the loaded game gives
 * the same bytes. The initial state of every mission survives a save and a load unchanged. Needs the converter's
 * generated data; skipped without it. */
#include "kp_test.h"
#include "kp_ids.h"

#define DIR "tests/golden/oracle/"
#define DATA "test data" /* the tests load saves into the same generated data */
#define WASTELAND_SAVE_STEP 3700 /* the Barracks of terran01's scripted victory under construction */
#define INTO_LAST (-1)

static const char *const FILES[] = { KP_T_DATA, NULL };

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

/* The state hash's definition over a section's bytes in a save (kp_game.h), the test's own: 64-bit little-endian
 * words through a multiply-xorshift, the tail zero-padded, seeded with "kphash" and the version and the tag, closed with
 * the length and murmur3's finaliser; the total folds the sections' hashes the same way. */
#define REF_SEED (0x6b70686173680000ULL | (uint64_t)KP_HASH_VERSION)
#define SAVE_SECTIONS_MAX 64

static uint64_t ref_mix(uint64_t h, uint64_t w)
{
	h = (h ^ w) * 0x9e3779b97f4a7c15ULL;
	return h ^ (h >> 32);
}

static uint64_t ref_finish(uint64_t h, uint64_t n)
{
	h = ref_mix(h, n);
	h ^= h >> 33;
	h *= 0xff51afd7ed558ccdULL;
	h ^= h >> 33;
	h *= 0xc4ceb9fe1a85ec53ULL;
	return h ^ (h >> 33);
}

static uint64_t le_bytes(const uint8_t *p, size_t n)
{
	uint64_t w = 0;
	size_t j;
	for (j = 0; j < n; j++) w |= (uint64_t)p[j] << (8 * j);
	return w;
}

static uint64_t ref_hash(const uint8_t *tag, const uint8_t *p, size_t n)
{
	uint64_t h = ref_mix(REF_SEED, le_bytes(tag, 4));
	size_t k;
	for (k = 0; k < n; k += 8) h = ref_mix(h, le_bytes(p + k, n - k < 8 ? n - k : 8));
	return ref_finish(h, (uint64_t)n);
}

typedef struct {
	int count;
	char tag[SAVE_SECTIONS_MAX][5];
	uint64_t hash[SAVE_SECTIONS_MAX];
	size_t bytes[SAVE_SECTIONS_MAX];
} Sections;

static void collect(void *ud, const char *tag, uint64_t hash, size_t bytes)
{
	Sections *s = (Sections *)ud;
	if (s->count == SAVE_SECTIONS_MAX) return;
	memcpy(s->tag[s->count], tag, 4);
	s->tag[s->count][4] = 0;
	s->hash[s->count] = hash;
	s->bytes[s->count++] = bytes;
}

static uint64_t state_hash(KpT *T, KpGame *g, Sections *s)
{
	uint64_t total = 0;
	if (s) memset(s, 0, sizeof *s);
	kp_t_ok(T, kp_state_hash(g, s ? collect : NULL, s, &total) == 0, "the state hash ran out of memory");
	return total;
}

/* Game g, restored to step mid of replay r (whose runner was at `run` there), run on to step end: its per-step CRCs
 * against the original's; the first difference into bad. */
static void continue_steps(KpT *T, const char *name, const char *into, const KpReplay *r, KpGame *g, const KpRunner *run,
	int64_t mid, int64_t end, KpBuf *bad)
{
	KpRunner cont;
	int64_t step;
	kp_runner_init(&cont, g, r);
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
			kp_t_ok(T, kp_state_crcs(g, step >= KP_ORACLE_DRAWN_STEP, &crcs) == 0, "out of memory");
			kp_buf_init(&diffs);
			if (kp_step_differences(&crcs, &r->crcs[at], n, &diffs) > 0) {
				kp_buf_printf(bad, "%s%s into %s step %lld%s", bad->size ? ", " : "", name, into, (long long)step,
					kp_buf_text(&diffs));
			}
			kp_buf_free(&diffs);
			if (bad->size) break;
		}
	}
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
	uint64_t hash = state_hash(T, game, NULL);
	int i;
	for (i = 0; i < count; i++) { /* the game that takes the save: fresh, or run on to the end */
		int mark = kp_t_mark(T);
		KpOracleGame *loaded = start(T, r);
		KpRunner to;
		uint8_t *again;
		int64_t end = stop && stop < last ? stop : last;
		char into[32];
		if (intos[i] == INTO_LAST) snprintf(into, sizeof into, "last");
		else snprintf(into, sizeof into, "%lld", (long long)intos[i]);
		kp_runner_init(&to, loaded->g, r);
		kp_runner_to(&to, intos[i] == INTO_LAST ? last : intos[i]);
		load(T, loaded->g, bytes, size, mission, DATA);
		again = save(T, loaded->g, mission, DATA, &again_size);
		if (again_size != size || memcmp(again, bytes, size) != 0) kp_buf_printf(bad, "%s%s round trip", bad->size ? ", " : "", name);
		if (state_hash(T, loaded->g, NULL) != hash) kp_buf_printf(bad, "%s%s state hash after the load", bad->size ? ", " : "", name);
		continue_steps(T, name, into, r, loaded->g, run, mid, end, bad);
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
	const unsigned char *mine;
	KpBuf *bad;
	int k;
	if (!kp_t_ready(T, "test_save", FILES)) return;
	index = load_golden(T, "scenarios/index");
	mine = kp_t_mine_goldens(T, index, DIR "scenarios/%s.json");
	bad = kp_t_buf(T);
	for (k = 0; k < kp_json_len(index); k++) {
		const char *name = kp_json_str(kp_json_at(index, k), "");
		char path[256];
		const KpJson *g;
		int mark = kp_t_mark(T);
		int64_t last = 0;
		int i;
		if (!mine[k]) continue;
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

/* The scripted wins of the campaign rows (tests/golden/oracle/plays, plan phase 8): each play's game equal to the
 * emulator's at every 20th step by the state's fingerprint and every 500th by its categories, its trigger and AI
 * traces equal, and its result the local player's victory; and (plan phase 8 item 4) saved at every PLAY_SAVE_EVERY-th
 * step and loaded into a fresh game, which continues as the original did up to the next save step, the first save to
 * the play's end. The play's game runs on from save to save. */
#define PLAY_SAVE_EVERY 2000
#define RESULT_VICTORY 3
#define RESULT_STEPS 100 /* a play's dumps (every 20th step) end when the result dialog pauses the game */
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

static void campaign_plays_and_saves_equal_the_original(KpT *T)
{
	static const int64_t INTOS[1] = { 1 };
	const KpJson *index;
	const unsigned char *mine;
	KpBuf *bad;
	int k;
	if (!kp_t_ready(T, "test_save", FILES) || !kp_t_has(T, DIR "plays/index.json")) return;
	index = load_golden(T, "plays/index");
	mine = kp_t_mine_goldens(T, index, DIR "plays/%s.json");
	bad = kp_t_buf(T);
	for (k = 0; k < kp_json_len(index); k++) {
		const char *name = kp_json_str(kp_json_at(index, k), "");
		char path[256];
		const KpJson *g;
		const KpReplay *r;
		KpGoldenRun *gr;
		KpGame *game;
		int64_t mid, step;
		int mark = kp_t_mark(T), found = 0;
		if (!mine[k]) continue;
		snprintf(path, sizeof path, "plays/%s", name);
		g = load_golden(T, path);
		gr = kp_t_golden_run(T, g, NULL, kp_json_get(g, "trace") != NULL || kp_json_get(g, "ai_trace") != NULL);
		r = &gr->replay;
		game = gr->game.g;
		for (mid = PLAY_SAVE_EVERY; mid <= r->last - 1 && !found; mid += PLAY_SAVE_EVERY) {
			if ((found = kp_golden_run_check_to(gr, mid, name, bad)) == 0) {
				continue_from(T, name, g, r, game, &gr->run, mid, INTOS, 1, bad,
					mid == PLAY_SAVE_EVERY ? r->last : mid + PLAY_SAVE_EVERY);
			}
		}
		if (!found && kp_golden_run_check(gr, g, name, bad) == 0) { /* the result comes within the stride after the last dump */
			for (step = r->last; game->results[game->local_player] == 0 && step < r->last + RESULT_STEPS;) kp_runner_to(&gr->run, ++step);
			if (game->results[game->local_player] != RESULT_VICTORY) kp_buf_printf(bad, "%s%s no victory", bad->size ? ", " : "", name);
		}
		kp_t_release(T, mark);
	}
	kp_t_ok(T, bad->size == 0, "plays or their continuations after a load differ from the original: %s", kp_buf_text(bad));
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
		snprintf(path, sizeof path, "missions/%s", kp_json_field_str(g, "mission", ""));
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

/* The local player's view of the units (KP_UNIT_VIEW_STATE: the attack notices' silence, the minimap's blink and its
 * colour, which the interface writes) is saved in its own section, VIEW, which the state hash's total leaves out
 * (ADR 0023): a game whose view state differs has the same total, another VIEW hash, and its save brings the view
 * state back. */
static void view_state_is_saved_outside_the_total(KpT *T)
{
	const char *path = "scenarios/marine_group";
	KpGame *g, *loaded;
	Sections before, after;
	uint64_t total;
	uint8_t *bytes;
	size_t size;
	int64_t n;
	int k, views = 0, others = 1;
	if (!kp_t_ready(T, "test_save", FILES)) return;
	g = kp_t_game(T, path, 1, 0, NULL)->g;
	n = g->lists[KP_LIST_ACTIVE].head;
	kp_t_ok(T, n != 0, "a unit");
	total = state_hash(T, g, &before);
	kp_unit_set_blink(g, n, 8, 0xae);
	kp_unit_silence_attack_notice(g, n, 24);
	kp_t_eq(T, (int64_t)state_hash(T, g, &after), (int64_t)total, "the same total");
	kp_t_eq(T, after.count, before.count, "the same sections");
	for (k = 0; k < after.count && k < before.count; k++) {
		int view = strcmp(after.tag[k], "VIEW") == 0;
		views += view;
		if (view) kp_t_ok(T, after.hash[k] != before.hash[k], "VIEW's hash follows the view state");
		else others &= after.hash[k] == before.hash[k];
	}
	kp_t_eq(T, views, 1, "one VIEW section");
	kp_t_ok(T, others, "the other sections' hashes stay");
	bytes = save(T, g, "a", DATA, &size);
	loaded = kp_t_game(T, path, 1, 0, NULL)->g;
	load(T, loaded, bytes, size, "a", DATA);
	kp_t_ok(T, kp_unit_get_last_event_timer(loaded, n) == 8 && kp_unit_get_last_event_color(loaded, n) == 0xae
		&& kp_unit_get_attack_notify_timer(loaded, n) == 24, "the load brings the view state back");
	kp_t_ok(T, kp_unit_blink_step(loaded, n) && kp_unit_get_last_event_timer(loaded, n) == 7, "a minimap draw counts down");
	kp_units_tick_attack_notices(loaded);
	kp_t_eq(T, kp_unit_get_attack_notify_timer(loaded, n), 23, "the idle event counts the silence down");
}

/* A step that failed inside a turn, a town's migration, a placement or a nested query leaves their scratch set; a load
 * or a restore into that game starts it empty, as between steps (a stuck migration lock stopped every later one). */
static void loads_clear_the_step_scratch(KpT *T)
{
	const char *path = "scenarios/marine_group";
	KpGame *g;
	uint8_t *bytes, *snap;
	size_t size, snap_size;
	KpError err;
	if (!kp_t_ready(T, "test_save", FILES)) return;
	g = kp_t_game(T, path, 1, 0, NULL)->g;
	bytes = save(T, g, "a", DATA, &size);
	snap_size = kp_snapshot_size(g);
	snap = (uint8_t *)malloc(snap_size);
	kp_t_ok(T, snap != NULL, "memory for a snapshot");
	if (!snap) return;
	kp_t_defer(T, free, snap);
	kp_t_ok(T, kp_snapshot(g, snap, snap_size, &err) == 0, "a snapshot: %s", err.msg);
	g->turn_cancels_cursor = 1;
	g->ai.migrating = 1;
	g->ai.placement_builder = 5;
	g->spatial.frame_top = 3;
	load(T, g, bytes, size, "a", DATA);
	kp_t_ok(T, g->turn_cancels_cursor == 0 && g->ai.migrating == 0 && g->ai.placement_builder == 0
		&& g->spatial.frame_top == 0, "a load clears the scratch");
	g->turn_cancels_cursor = 1;
	g->ai.migrating = 1;
	g->ai.placement_builder = 5;
	g->spatial.frame_top = 3;
	kp_t_ok(T, kp_restore(g, snap, snap_size, &err) == 0, "a restore: %s", err.msg);
	kp_t_ok(T, g->turn_cancels_cursor == 0 && g->ai.migrating == 0 && g->ai.placement_builder == 0
		&& g->spatial.frame_top == 0, "a restore clears the scratch");
}

/* A load that must be refused: 1 when it is, with its message in err. */
static int refused(KpGame *g, const uint8_t *bytes, size_t size, const char *mission, const char *data, KpError *err)
{
	return kp_load(g, bytes, size, mission, data, err) != 0;
}

static void loads_reject_other_saves(KpT *T)
{
	const char *path = "scenarios/marine_group";
	KpOracleGame *game, *other;
	uint8_t *bytes, *damaged, *mine;
	size_t size, mine_size;
	KpError err;
	KpBuf *before, *after;
	char other_path[512], expect[64];
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
	kp_t_ok(T, refused(kp_t_game(T, path, 1, 0, NULL)->g, damaged, size, "a", DATA, &err) && strstr(err.msg, "section SCAL damaged"),
		"a damaged save is refused: %s", err.msg);
	memcpy(damaged, bytes, size);
	damaged[4] = 1; /* the version */
	snprintf(expect, sizeof expect, "version 1, this build reads %d", KP_SAVE_VERSION);
	kp_t_ok(T, refused(kp_t_game(T, path, 1, 0, NULL)->g, damaged, size, "a", DATA, &err) && strstr(err.msg, expect),
		"a save of another version is refused: %s", err.msg);
	memcpy(damaged, bytes, size);
	damaged[size - 1] = (uint8_t)(damaged[size - 1] + 1); /* the table's total */
	kp_t_ok(T, refused(kp_t_game(T, path, 1, 0, NULL)->g, damaged, size, "a", DATA, &err) && strstr(err.msg, "hash table"),
		"a damaged hash table is refused: %s", err.msg);
	kp_t_ok(T, refused(kp_t_game(T, path, 1, 0, NULL)->g, bytes, 50, "a", DATA, &err), "a truncated save is refused: %s", err.msg);
	/* A save whose header passes but whose state does not fit the game (another map under the same names) is refused
	 * before anything is written: the game keeps its state. */
	first = load_golden(T, "initial_state/index");
	snprintf(other_path, sizeof other_path, "initial_state/%s", kp_json_str(kp_json_at(first, 0), ""));
	snprintf(other_path, sizeof other_path, "missions/%s", kp_json_field_str(load_golden(T, other_path), "mission", ""));
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

/* A save's scalars must be ones a started game holds (a sealed save of someone's making may hold any): a held scalar
 * absent, a presence other than 0 or 1 and a value past the original's 32 bits are refused, the game left as it was;
 * damaged as well, the save is told as damaged (its hashes come first). */
static void loads_refuse_scalars_a_game_cannot_hold(KpT *T)
{
	static const struct {
		KpScalar s;
		uint8_t present;
		int64_t value;
	} BAD[] = {
		{ KP_TILE_COUNTDOWN, 0, 0 },          /* held, absent: the step's countdown would run from KP_NIL */
		{ KP_SCREEN_X, 2, 0 },                /* a presence 2 */
		{ KP_FRAME, 1, KP_NIL },              /* present with no value */
		{ KP_ELAPSED, 1, (int64_t)UINT32_MAX + 1 },
		{ KP_COMMAND_PLAYER, 1, (int64_t)INT32_MIN - 1 },
	};
	const char *path = "scenarios/marine_group";
	KpOracleGame *game, *into;
	KpBuf *before, *after;
	KpError err;
	size_t k;
	if (!kp_t_ready(T, "test_save", FILES)) return;
	game = kp_t_game(T, path, 1, 0, NULL);
	into = kp_t_game(T, path, 1, 0, NULL);
	before = kp_t_buf(T);
	after = kp_t_buf(T);
	kp_t_ok(T, kp_state_lines(into->g, 0, before) == 0, "out of memory");
	for (k = 0; k < sizeof BAD / sizeof BAD[0]; k++) {
		const char *name = KP_SCALARS[BAD[k].s].name;
		int64_t value = game->g->scalar[BAD[k].s];
		uint8_t present = game->g->scalar_present[BAD[k].s], *bytes;
		size_t size;
		game->g->scalar[BAD[k].s] = BAD[k].value;
		game->g->scalar_present[BAD[k].s] = BAD[k].present;
		bytes = save(T, game->g, "a", DATA, &size);
		game->g->scalar[BAD[k].s] = value;
		game->g->scalar_present[BAD[k].s] = present;
		kp_t_ok(T, refused(into->g, bytes, size, "a", DATA, &err) && strstr(err.msg, "the scalar ") && strstr(err.msg, name),
			"%s (present %u, %lld) refused: %s", name, (unsigned)BAD[k].present, (long long)BAD[k].value, err.msg);
		bytes[size / 2] = (uint8_t)(bytes[size / 2] + 1);
		kp_t_ok(T, refused(into->g, bytes, size, "a", DATA, &err) && strstr(err.msg, "damaged"),
			"%s damaged too: told as damaged: %s", name, err.msg);
	}
	kp_t_ok(T, kp_state_lines(into->g, 0, after) == 0, "out of memory");
	kp_t_ok(T, before->size == after->size && memcmp(before->data, after->data, before->size) == 0,
		"the refused saves changed nothing");
	/* the game's own save loads */
	{
		uint8_t *bytes;
		size_t size;
		bytes = save(T, game->g, "a", DATA, &size);
		kp_t_ok(T, !refused(into->g, bytes, size, "a", DATA, &err), "the game's own save loads: %s", err.msg);
	}
}

/* A sealed save may hold any record number, link or index (kp_load_check): one out of its array, a list that does not
 * end, a union word the step writes through out of range are refused once written, and the game is put back as it
 * was, its serials too (the interface takes no load). */
static void loads_refuse_indices_out_of_their_arrays(KpT *T)
{
	enum { CONNECTED, UNIT_LOOP, RING, HEAP, IMAGE_NEXT, ORDER_LOOP, RECEDE, RESEARCH, BOUND_PAST, BOUND_HALF, ENTRIES, BAD_COUNT };
	static const char *const EXPECT[BAD_COUNT] = { "unit connected", "unit list", "bullet ring", "AI heap count",
		"image next", "order free list", "receding creep bucket", "a research's tech", "unit bounds", "unit bounds",
		"the spatial index's entries" };
	const char *path = "scenarios/marine_group";
	KpOracleGame *game, *into;
	KpBuf *before, *after;
	KpError err;
	int bad;
	if (!kp_t_ready(T, "test_save", FILES)) return;
	game = kp_t_game(T, path, 1, 0, NULL);
	into = kp_t_game(T, path, 1, 0, NULL);
	before = kp_t_buf(T);
	after = kp_t_buf(T);
	kp_t_ok(T, kp_state_lines(into->g, 0, before) == 0, "out of memory");
	kp_t_ok(T, game->g->u.bound_left[game->g->lists[KP_LIST_ACTIVE].head] >= 0, "the first unit is in the spatial index");
	for (bad = 0; bad < BAD_COUNT; bad++) {
		KpGame *g = game->g;
		KpSpatial *sp = &g->spatial;
		int64_t n = g->lists[KP_LIST_ACTIVE].head, o = g->orders.free.head, load_serial = into->g->load_serial;
		int64_t was = 0, order = g->u.order[n], tech = kp_union_get(g, n, 0xc8, 1), count = sp->count;
		int32_t image_next = g->w->img.next[0];
		/* two entries past the index's (a unit in range, named by no bound) */
		int16_t entries[4];
		uint8_t *bytes;
		size_t size;
		entries[0] = sp->x_unit[count], entries[1] = sp->x_unit[count + 1];
		entries[2] = sp->y_unit[count], entries[3] = sp->y_unit[count + 1];
		switch (bad) {
		case CONNECTED: was = g->u.connected[n]; g->u.connected[n] = KP_UNITS + 5; break;
		case UNIT_LOOP: was = g->u.next[n]; g->u.next[n] = n; break;
		case RING: was = g->bullets.ring[1]; g->bullets.ring[1] = 14; break;
		case HEAP: was = g->pai.heap_count[0]; g->pai.heap_count[0] = KP_AI_HEAP + 1; break;
		case IMAGE_NEXT: g->w->img.next[0] = KP_IMAGES; break;
		case ORDER_LOOP: was = g->orders.next[o]; g->orders.next[o] = o; break;
		case RECEDE: was = g->terrain.recede_bucket[3]; g->terrain.recede_bucket[3] = KP_RECEDE_BUCKETS + 1; break;
		case RESEARCH: g->u.order[n] = KP_ORD_RESEARCH; kp_union_set(g, n, 0xc8, 1, 200); break;
		case BOUND_PAST: was = g->u.bound_right[n]; g->u.bound_right[n] = (int16_t)count; break;
		case BOUND_HALF: was = g->u.bound_right[n]; g->u.bound_right[n] = -1; break;
		case ENTRIES:
			sp->x_unit[count] = sp->x_unit[count + 1] = sp->y_unit[count] = sp->y_unit[count + 1] = (int16_t)n;
			sp->count = count + 2;
			break;
		}
		bytes = save(T, g, "a", DATA, &size);
		switch (bad) {
		case CONNECTED: g->u.connected[n] = was; break;
		case UNIT_LOOP: g->u.next[n] = was; break;
		case RING: g->bullets.ring[1] = was; break;
		case HEAP: g->pai.heap_count[0] = was; break;
		case IMAGE_NEXT: g->w->img.next[0] = image_next; break;
		case ORDER_LOOP: g->orders.next[o] = was; break;
		case RECEDE: g->terrain.recede_bucket[3] = was; break;
		case RESEARCH: g->u.order[n] = order; kp_union_set(g, n, 0xc8, 1, tech); break;
		case BOUND_PAST:
		case BOUND_HALF: g->u.bound_right[n] = (int16_t)was; break;
		case ENTRIES:
			sp->count = count;
			sp->x_unit[count] = entries[0], sp->x_unit[count + 1] = entries[1];
			sp->y_unit[count] = entries[2], sp->y_unit[count + 1] = entries[3];
			break;
		}
		kp_t_ok(T, refused(into->g, bytes, size, "a", DATA, &err) && strstr(err.msg, EXPECT[bad]), "%s refused: %s",
			EXPECT[bad], err.msg);
		kp_t_eq(T, into->g->load_serial, load_serial, "%s: the refused load leaves the serials", EXPECT[bad]);
	}
	kp_t_ok(T, kp_state_lines(into->g, 0, after) == 0, "out of memory");
	kp_t_ok(T, before->size == after->size && memcmp(before->data, after->data, before->size) == 0,
		"the refused saves changed nothing");
	{
		uint8_t *bytes;
		size_t size;
		bytes = save(T, game->g, "a", DATA, &size);
		kp_t_ok(T, !refused(into->g, bytes, size, "a", DATA, &err), "the game's own save loads: %s", err.msg);
	}
}

/* A record never used keeps kp_units_init's zero bounds, which the load check lets through: a loaded save's reference
 * that hands it to the spatial index's move or remove leaves the other units' entries and the index's count as they
 * are (the zeros name another unit's entry). */
static void a_record_never_used_stays_out_of_the_spatial_index(KpT *T)
{
	const char *path = "scenarios/marine_group";
	KpOracleGame *game;
	KpBuf *before, *after;
	KpGame *g;
	int64_t n, never = 0, count;
	if (!kp_t_ready(T, "test_save", FILES)) return;
	game = kp_t_game(T, path, 1, 0, NULL);
	g = game->g;
	for (n = KP_UNITS; n >= 1 && never == 0; n--)
		if (g->u.bound_left[n] == 0 && g->u.bound_right[n] == 0 && g->u.bound_top[n] == 0 && g->u.bound_bottom[n] == 0) never = n;
	count = g->spatial.count;
	kp_t_ok(T, never != 0 && count >= 2, "a record never used (%lld) beside units in the index (%lld entries)",
		(long long)never, (long long)count);
	if (never == 0) return;
	before = kp_t_buf(T);
	after = kp_t_buf(T);
	kp_t_ok(T, kp_state_lines(g, 0, before) == 0, "out of memory");
	kp_spatial_move(g, never, 5, 5);
	kp_spatial_remove(g, never);
	kp_t_ok(T, kp_state_lines(g, 0, after) == 0, "out of memory");
	kp_t_eq(T, g->spatial.count, count, "the index's count");
	kp_t_ok(T, before->size == after->size && memcmp(before->data, after->data, before->size) == 0, "the game unchanged");
}

/* The countdown's text is not saved: a load sets it from the loaded countdown, not the game's before the load (an
 * in-game load writes into the game shown). */
static void loads_show_the_loaded_countdown(KpT *T)
{
	const char *path = "scenarios/marine_group";
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

/* The walk's coverage (kp_state_ranges, ADR 0021): every byte of KpGame, of its images world and of its generator is
 * either visited by the walk or left out by the exclusion table on purpose, once. A gap is allowed only where it is
 * the padding before its range (up to the range's alignment), padding the table names (a platform's, before a struct
 * aligned past its first walked field) or the struct's tail padding. What this cannot see: the
 * heap blocks the pointers lead to (the exclusion table classifies each pointer), and a field of 4 bytes or less
 * between a saved field and the padding before an 8-byte one (it looks like that padding). */
static const char *const BASE_NAMES[KP_STATE_BASES] = { "KpGame", "KpImages", "KpRngGame" };

static int by_base_offset(const void *a, const void *b)
{
	const KpStateRange *x = (const KpStateRange *)a, *y = (const KpStateRange *)b;
	if (x->base != y->base) return x->base < y->base ? -1 : 1;
	if (x->offset != y->offset) return x->offset < y->offset ? -1 : 1;
	return 0;
}

static size_t round_up(size_t n, size_t a) { return (n + a - 1) / a * a; }

static void walk_covers_the_state(KpT *T)
{
	static const char *const CLASSES[] = { "saved", "derived", "static", "view", "host", "padding" };
	KpOracleGame *game;
	KpStateRange *r;
	size_t n, k, at = 0, size[KP_STATE_BASES];
	const char *prev = "start";
	int base = -1, seen[KP_STATE_BASES] = { 0, 0, 0 }, b;
	KpBuf *bad;
	if (!kp_t_ready(T, "test_save", FILES)) return;
	game = kp_t_game(T, "scenarios/marine_group", 1, 0, NULL);
	kp_t_ok(T, kp_state_ranges(game->g, &r, &n) == 0, "out of memory");
	kp_t_defer(T, free, r);
	qsort(r, n, sizeof *r, by_base_offset);
	size[KP_STATE_GAME] = sizeof(KpGame);
	size[KP_STATE_WORLD] = sizeof(KpImages);
	size[KP_STATE_RNG] = sizeof(KpRngGame);
	bad = kp_t_buf(T);
	for (k = 0; k <= n; k++) {
		if (k == n || r[k].base != base) {
			if (base >= 0 && size[base] - at >= 8) { /* a tail of padding only (KpRngGame is aligned to 4) */
				kp_buf_printf(bad, "\n%s tail +%lu..%lu after %s", BASE_NAMES[base], (unsigned long)at, (unsigned long)size[base], prev);
			}
			if (k == n) break;
			base = r[k].base;
			seen[base] = 1;
			at = 0;
			prev = "start";
		}
		if (r[k].offset < at) {
			kp_buf_printf(bad, "\n%s +%lu %s %s overlaps %s", BASE_NAMES[base], (unsigned long)r[k].offset, CLASSES[r[k].klass],
				r[k].name, prev);
		} else if (r[k].offset > at && r[k].offset != round_up(at, r[k].align)) {
			kp_buf_printf(bad, "\n%s gap +%lu..%lu (%lu bytes) between %s and %s %s", BASE_NAMES[base], (unsigned long)at,
				(unsigned long)r[k].offset, (unsigned long)(r[k].offset - at), prev, CLASSES[r[k].klass], r[k].name);
		}
		if (r[k].offset + r[k].bytes > at) at = r[k].offset + r[k].bytes;
		prev = r[k].name;
	}
	for (b = 0; b < KP_STATE_BASES; b++) kp_t_ok(T, seen[b], "%s has ranges", BASE_NAMES[b]);
	kp_t_ok(T, bad->size == 0, "the walk and its exclusions leave out or repeat: %s", kp_buf_text(bad));
}

/* The hashes of game g's sections against its save's bytes (the reference over each section's slice, its tag where
 * the save has it) and against the save's hash table; differences into bad. */
static void check_hashes(KpT *T, const char *name, KpGame *g, KpBuf *bad)
{
	Sections s;
	size_t size, at = 4 + 4, k;
	uint8_t *bytes = save(T, g, "a", DATA, &size);
	uint64_t total = state_hash(T, g, &s), fold = REF_SEED, folded = 0;
	int i;
	for (k = 0; k < 2; k++) at += 4 + (size_t)le_bytes(bytes + at, 4); /* the mission's and the data's names */
	for (i = 0; i < s.count; i++) {
		if (at + 4 + s.bytes[i] > size || memcmp(bytes + at, s.tag[i], 4) != 0) {
			kp_buf_printf(bad, "%s%s: section %s not where its bytes say", bad->size ? ", " : "", name, s.tag[i]);
			return;
		}
		if (ref_hash(bytes + at, bytes + at + 4, s.bytes[i]) != s.hash[i]) {
			kp_buf_printf(bad, "%s%s: section %s's hash", bad->size ? ", " : "", name, s.tag[i]);
		}
		if (strcmp(s.tag[i], "VIEW") != 0) { /* the local player's view of the units: outside the total (ADR 0023) */
			fold = ref_mix(fold, s.hash[i]);
			folded++;
		}
		at += 4 + s.bytes[i];
	}
	if (ref_finish(fold, folded) != total) kp_buf_printf(bad, "%s%s: the total", bad->size ? ", " : "", name);
	if (at + 4 + 12 * (size_t)s.count + 8 != size || le_bytes(bytes + at, 4) != (uint64_t)s.count) {
		kp_buf_printf(bad, "%s%s: the hash table's size", bad->size ? ", " : "", name);
		return;
	}
	for (at += 4, i = 0; i < s.count; i++, at += 12) {
		if (memcmp(bytes + at, s.tag[i], 4) != 0 || le_bytes(bytes + at + 4, 8) != s.hash[i]) {
			kp_buf_printf(bad, "%s%s: the table's %s", bad->size ? ", " : "", name, s.tag[i]);
		}
	}
	if (le_bytes(bytes + at, 8) != total) kp_buf_printf(bad, "%s%s: the table's total", bad->size ? ", " : "", name);
}

/* The state hash is the hash of the save's bytes, section by section, in the middle of every scenario and at its start
 * (the same on every platform the suite runs on: host, sanitizers, wasm). */
static void hash_is_the_saves_bytes(KpT *T)
{
	const KpJson *index;
	KpBuf *bad;
	int k;
	if (!kp_t_ready(T, "test_save", FILES)) return;
	index = load_golden(T, "scenarios/index");
	bad = kp_t_buf(T);
	for (k = 0; k < kp_json_len(index); k++) {
		const char *name = kp_json_str(kp_json_at(index, k), "");
		char path[256];
		const KpReplay *r;
		KpRunner run;
		KpOracleGame *og;
		int mark;
		if (!kp_t_mine_sampled(T, k)) continue;
		mark = kp_t_mark(T);
		snprintf(path, sizeof path, "scenarios/%s", name);
		r = replay_of(T, load_golden(T, path));
		og = game_at(T, r, 0, &run);
		check_hashes(T, name, og->g, bad);
		kp_runner_to(&run, r->last / 2);
		check_hashes(T, name, og->g, bad);
		kp_t_release(T, mark);
	}
	kp_t_ok(T, bad->size == 0, "state hashes differ from the saves' bytes: %s", kp_buf_text(bad));
}

/* The save walk's fingerprint of each save version (kp_save_schema): a change of the walk changes the fingerprint and
 * must come with a new version, as a save of another walk does not load (ADR 0021). Append a row; never edit one. Two
 * visits of the same kind and count swapped within a section keep the fingerprint: raise the version by hand then. */
static const struct {
	int version;
	uint64_t schema;
} SCHEMAS[] = {
	{ 2, 0x12e396b75619fee4ULL },
	{ 3, 0x009a8709ec54eb16ULL }, /* VIEW: the units' view state out of UNIT (ADR 0023) */
	{ 4, 0x0b8d118c10f9c685ULL }, /* SETP: the game's setup (ADR 0024) */
	{ 5, 0x65b4b52f5940175dULL }, /* the network game's pauses left, a team game's lobby races and leaders (ADR 0025) */
	{ 6, 0x9437b019adb3182cULL }, /* the setup's Storm IDs of slots 8..11, the seat's (phase N, stage N4) */
	{ 7, 0x671dee72d3e8f79cULL }, /* the narrowed fields at their widths (ADR 0008's addition, 2026-11-27) */
};

static void save_schema_matches_its_version(KpT *T)
{
	uint64_t schema = kp_save_schema();
	size_t k, n = sizeof SCHEMAS / sizeof SCHEMAS[0];
	int found = 0;
	kp_t_ok(T, schema != 0, "the schema's walk ran out of memory");
	for (k = 0; k < n; k++) {
		if (SCHEMAS[k].version == KP_SAVE_VERSION) {
			found = 1;
			kp_t_ok(T, SCHEMAS[k].schema == schema,
				"the save walk changed (fingerprint 0x%016llx, version %d's 0x%016llx): raise KP_SAVE_VERSION and add a row with 0x%016llx",
				(unsigned long long)schema, KP_SAVE_VERSION, (unsigned long long)SCHEMAS[k].schema, (unsigned long long)schema);
		} else {
			kp_t_ok(T, SCHEMAS[k].schema != schema, "the walk's fingerprint is version %d's: raise KP_SAVE_VERSION past it",
				SCHEMAS[k].version);
		}
	}
	kp_t_ok(T, found, "no row for save version %d: add { %d, 0x%016llx }", KP_SAVE_VERSION, KP_SAVE_VERSION, (unsigned long long)schema);
}

/* The snapshot of game g into a malloc'd buffer of *size bytes (released at the end). */
static uint8_t *snapshot(KpT *T, KpGame *g, size_t *size)
{
	KpError err;
	uint8_t *buf;
	*size = kp_snapshot_size(g);
	buf = (uint8_t *)malloc(*size);
	kp_t_ok(T, buf != NULL, "out of memory");
	kp_t_defer(T, free, buf);
	kp_t_ok(T, kp_snapshot(g, buf, *size, &err) == 0, "%s", err.msg);
	return buf;
}

/* In the middle of every scenario a snapshot; the game run on to the end and restored from it has the middle's state
 * hash and save and continues as the original did again; the snapshot restored into a fresh game of the mission
 * continues the same (kp_snapshot, kp_restore). */
static void snapshots_rewind_as_the_original(KpT *T)
{
	const KpJson *index;
	const unsigned char *mine;
	KpBuf *bad;
	int k;
	if (!kp_t_ready(T, "test_save", FILES)) return;
	index = load_golden(T, "scenarios/index");
	mine = kp_t_mine_goldens(T, index, DIR "scenarios/%s.json");
	bad = kp_t_buf(T);
	for (k = 0; k < kp_json_len(index) && bad->size == 0; k++) {
		const char *name = kp_json_str(kp_json_at(index, k), "");
		char path[256];
		const KpReplay *r;
		KpRunner run, at_mid;
		KpOracleGame *og, *fresh;
		uint8_t *snap, *bytes, *again;
		size_t snap_size, size, again_size;
		uint64_t hash;
		int64_t mid;
		KpError err;
		int mark = kp_t_mark(T);
		if (!mine[k]) continue;
		snprintf(path, sizeof path, "scenarios/%s", name);
		r = replay_of(T, load_golden(T, path));
		mid = r->last / 2;
		og = game_at(T, r, mid, &run);
		at_mid = run;
		snap = snapshot(T, og->g, &snap_size);
		hash = state_hash(T, og->g, NULL);
		bytes = save(T, og->g, "a", DATA, &size);
		kp_runner_to(&run, r->last);
		kp_t_ok(T, kp_restore(og->g, snap, snap_size, &err) == 0, "%s: %s", name, err.msg);
		again = save(T, og->g, "a", DATA, &again_size);
		if (state_hash(T, og->g, NULL) != hash) kp_buf_printf(bad, "%s%s rewind's state hash", bad->size ? ", " : "", name);
		if (again_size != size || memcmp(again, bytes, size) != 0) kp_buf_printf(bad, "%s%s rewind's save", bad->size ? ", " : "", name);
		continue_steps(T, name, "rewind", r, og->g, &at_mid, mid, r->last, bad);
		fresh = start(T, r);
		kp_t_ok(T, kp_restore(fresh->g, snap, snap_size, &err) == 0, "%s: %s", name, err.msg);
		continue_steps(T, name, "a fresh game", r, fresh->g, &at_mid, mid, r->last, bad);
		kp_t_release(T, mark);
	}
	kp_t_ok(T, bad->size == 0, "continuations after a restore differ: %s", kp_buf_text(bad));
}

/* The dump's coverage of the saved state (kp_dump.c against kp_save.c's walk; kp_dump_fields): a saved field is shown
 * by the dump when a game whose field alone was taken from a later step of the same game dumps differently (or fails
 * to dump: its lines read the field). Each field's whole array comes from a real state, so its values are ones the
 * game can hold (indices in range, lists without cycles); the restore derives the rest from them as a load does. A
 * field that changes between two states only in records the dump does not list (the free ones) is not shown that way:
 * kp_dump_fields names those the dump does show (KP_DUMP_SHOWN), and the test checks them by adding one to every
 * element. */
typedef struct {
	const char *name;
	int line;
	int differed, shown, rebuilt; /* rebuilt: the restore puts back what it derives from the rest */
	int confirmed;                /* a KP_DUMP_SHOWN field's check passed */
	int scratched;                /* a KP_DUMP_SCRATCH field taken from another state in a game that ran on */
	size_t first, count;          /* its visits in `order` */
} DumpGroup;

typedef struct {
	DumpGroup *g;
	size_t n, cap;
	size_t *order; /* the visits by group */
	size_t nfields;
	int scratch_runs;  /* the games run on with their scratch from another state */
	KpBuf *scratch_bad; /* those that ran on differently */
} DumpGroups;

/* The games whose states show every field the dump shows changing (each shows at least one field first, in this
 * order: the replays of the original's campaign games, the idle AI runs, the scenarios). */
static const char *const DUMP_DONORS[] = { "replays/defeat-terran02.01-m02", "replays/exp-protoss.02-m33",
	"replays/exp-protoss.03-m34", "replays/exp-protoss.04-m35", "replays/exp-protoss.05-m36", "replays/exp-protoss.06-m37",
	"replays/exp-protoss.07-m38", "replays/exp-protoss.08-m39", "replays/exp-terran.01-m40", "replays/exp-terran.02-m41",
	"replays/exp-terran.03-m42", "replays/exp-terran.04-m43", "replays/exp-terran.05-m44", "replays/exp-terran.06-m46",
	"replays/exp-zerg.01-m49", "replays/exp-zerg.04-m52", "replays/exp-zerg.05-m58", "replays/exp-zerg.06-m59",
	"replays/exp-zerg.08-m61", "replays/orig-protoss.01-m22", "replays/orig-terran.07-m07", "replays/orig-zerg.08-m19",
	"replays/xprotoss01", "runs/protoss-m22", "runs/protoss-m28", "runs/protoss-m30", "runs/xterran-m45", "runs/xzerg-m59",
	"scenarios/addon_orphan", "scenarios/ai_battlecruiser", "scenarios/ai_defiler", "scenarios/ai_suicide",
	"scenarios/ai_templar", "scenarios/arbiter_stasis", "scenarios/archon_maelstrom", "scenarios/burrow_ensnared",
	"scenarios/devourer_acid", "scenarios/ghost_lockdown", "scenarios/harvest_depletion", "scenarios/irradiate_bunker",
	"scenarios/medic_flare", "scenarios/queen_spells", "scenarios/stim_marines", "scenarios/terran_landing",
	"scenarios/trigger_outcome", "scenarios/vessel_matrix" };
#define DUMP_COVER_POINTS 5 /* the states of a game the fields are taken between: its start, its quarters, its end */
#define DUMP_SCRATCH_STEPS 128 /* the steps a game runs on after its scratch came from another state */

static DumpGroup *dump_group(KpT *T, DumpGroups *gs, const KpSnapshotField *f)
{
	size_t k;
	for (k = 0; k < gs->n; k++) {
		if (gs->g[k].line == f->line && strcmp(gs->g[k].name, f->name) == 0) return &gs->g[k];
	}
	if (gs->n == gs->cap) {
		size_t cap = gs->cap ? gs->cap * 2 : 512;
		DumpGroup *more = (DumpGroup *)realloc(gs->g, cap * sizeof *more);
		kp_t_ok(T, more != NULL, "out of memory");
		gs->g = more;
		gs->cap = cap;
	}
	memset(&gs->g[gs->n], 0, sizeof gs->g[0]);
	gs->g[gs->n].name = f->name;
	gs->g[gs->n].line = f->line;
	return &gs->g[gs->n++];
}

static void dump_groups_free(void *p)
{
	DumpGroups *gs = (DumpGroups *)p;
	free(gs->g);
	free(gs->order);
}

/* The row of kp_dump_fields naming a field (its own, else the longest '*' row it begins with), or NULL. */
static const KpDumpField *dump_row(const char *name)
{
	const KpDumpField *rows, *best = NULL;
	size_t best_len = 0;
	int n, k;
	rows = kp_dump_fields(&n);
	for (k = 0; k < n; k++) {
		size_t len = strlen(rows[k].name);
		if (strcmp(rows[k].name, name) == 0) return &rows[k];
		if (len > 0 && rows[k].name[len - 1] == '*' && strncmp(rows[k].name, name, len - 1) == 0 && len > best_len) {
			best = &rows[k];
			best_len = len;
		}
	}
	return best;
}

/* A probe's game restored from a mixed snapshot: its snapshot back and its dump's hash. */
typedef struct {
	KpGame *g;
	const uint8_t *snap;
	uint8_t *back;
	size_t size;
	uint64_t hash;
	int refused, unread; /* unread: the dump failed on the state (it read the field) */
	char why[sizeof ((KpError *)0)->msg];
} DumpProbe;

static void dump_probe(KpT *T, void *ud)
{
	DumpProbe *p = (DumpProbe *)ud;
	KpError err;
	(void)T; /* kp_t_pcall's callback: a failure of the restore's walk lands in its handler */
	p->refused = kp_restore(p->g, p->snap, p->size, &err) != 0 || (p->back && kp_snapshot(p->g, p->back, p->size, &err) != 0);
	if (p->refused) {
		snprintf(p->why, sizeof p->why, "%s", err.msg);
		return;
	}
	p->unread = kp_dump_hash(p->g, 0, &p->hash) != 0;
}

/* Whether the dump of the game restored from snap differs from base (or fails); *kept whether the restore kept the
 * group's bytes as snap has them (back NULL: not asked). */
static int dump_differs(KpT *T, DumpProbe *probe, const DumpGroups *gs, const DumpGroup *d, const KpSnapshotField *fields,
	uint64_t base, int *kept)
{
	char msg[512];
	size_t j;
	if (kp_t_pcall(T, dump_probe, probe, msg, sizeof msg) != 0 || probe->refused || probe->unread) return 1; /* it read the field */
	for (j = 0; kept && j < d->count && *kept; j++) {
		const KpSnapshotField *f = &fields[gs->order[d->first + j]];
		*kept = memcmp(probe->back + f->offset, probe->snap + f->offset, f->bytes) == 0;
	}
	return probe->hash != base;
}

/* Every element of the group's visits one more (as an unsigned number of its size). */
static void dump_bump(uint8_t *snap, const DumpGroups *gs, const DumpGroup *d, const KpSnapshotField *fields)
{
	size_t j, e, b;
	for (j = 0; j < d->count; j++) {
		const KpSnapshotField *f = &fields[gs->order[d->first + j]];
		for (e = 0; e + f->elem <= f->bytes; e += f->elem) {
			for (b = 0; b < f->elem && ++snap[f->offset + e + b] == 0; b++) { /* the carry: little-endian */
			}
		}
	}
}

/* The game restored from snap and run on from run's step by DUMP_SCRATCH_STEPS steps (to the replay's last at most):
 * its dump's hash. */
static uint64_t dump_run_on(KpT *T, KpGame *g, const KpReplay *r, const KpRunner *run, const uint8_t *snap, size_t size,
	const char *path)
{
	KpRunner cont;
	KpError err;
	uint64_t hash;
	kp_t_ok(T, kp_restore(g, snap, size, &err) == 0, "%s: %s", path, err.msg);
	kp_runner_init(&cont, g, r);
	cont.step = run->step;
	cont.next_command = run->next_command;
	cont.next_ui = run->next_ui;
	kp_runner_to(&cont, run->step + DUMP_SCRATCH_STEPS < r->last ? run->step + DUMP_SCRATCH_STEPS : r->last);
	kp_t_ok(T, kp_dump_hash(g, 0, &hash) == 0, "out of memory");
	return hash;
}

/* State `into` (run's step) with every KP_DUMP_SCRATCH field taken from state `from` of the same game runs on as into
 * itself does: no later step reads what a search or a cache left. */
static void dump_scratch_from(KpT *T, DumpGroups *gs, const KpSnapshotField *fields, KpGame *g, const KpReplay *r,
	const KpRunner *run, const uint8_t *into, const uint8_t *from, uint8_t *mix, size_t size, const char *path)
{
	size_t k, j;
	int differs = 0;
	if (run->step >= r->last) return; /* nothing runs on */
	memcpy(mix, into, size);
	for (k = 0; k < gs->n; k++) {
		DumpGroup *d = &gs->g[k];
		const KpDumpField *row = dump_row(d->name);
		if (!row || row->klass != KP_DUMP_SCRATCH) continue;
		for (j = 0; j < d->count; j++) {
			const KpSnapshotField *f = &fields[gs->order[d->first + j]];
			if (memcmp(into + f->offset, from + f->offset, f->bytes) == 0) continue;
			memcpy(mix + f->offset, from + f->offset, f->bytes);
			d->scratched = differs = 1;
		}
	}
	if (!differs) return;
	gs->scratch_runs++;
	if (dump_run_on(T, g, r, run, mix, size, path) != dump_run_on(T, g, r, run, into, size, path)) {
		kp_buf_printf(gs->scratch_bad, "\n  %s: the state of step %lld with another state's scratch runs on differently",
			path, (long long)run->step);
	}
}

/* Golden path's game at DUMP_COVER_POINTS steps; between each two, every group not yet shown taken alone from the later
 * one into the earlier; and each KP_DUMP_SHOWN row's field one more, until a state shows it. Then each state with the
 * KP_DUMP_SCRATCH fields of its neighbours runs on (dump_scratch_from). */
static void dump_cover_game(KpT *T, DumpGroups *gs, const char *path)
{
	const KpReplay *r;
	KpGoldenRun *gr;
	KpGame *g;
	KpSnapshotField *fields;
	uint8_t *at_point[DUMP_COVER_POINTS], *mix, *back;
	KpRunner at_run[DUMP_COVER_POINTS];
	size_t size = 0, other = 0, nfields, j, k;
	uint64_t base;
	KpError err;
	DumpProbe probe;
	int p, mark = kp_t_mark(T);
	gr = kp_t_golden_run(T, load_golden(T, path), NULL, 0);
	r = &gr->replay;
	g = gr->game.g;
	for (p = 0; p < DUMP_COVER_POINTS; p++) {
		kp_runner_to(&gr->run, 1 + (r->last - 1) * p / (DUMP_COVER_POINTS - 1));
		at_point[p] = snapshot(T, g, p ? &other : &size);
		at_run[p] = gr->run;
		kp_t_ok(T, !p || other == size, "%s: snapshots of %lu and %lu bytes", path, (unsigned long)size, (unsigned long)other);
	}
	mix = snapshot(T, g, &other);
	back = snapshot(T, g, &other);
	kp_t_ok(T, kp_snapshot_fields(g, &fields, &nfields) == 0, "out of memory");
	kp_t_defer(T, free, fields);
	if (!gs->order) { /* the groups from the first game's visits (every game walks the same names) */
		size_t *at;
		gs->order = (size_t *)malloc(nfields * sizeof *gs->order);
		kp_t_ok(T, gs->order != NULL, "out of memory");
		for (j = 0; j < nfields; j++) dump_group(T, gs, &fields[j])->count++;
		at = (size_t *)calloc(gs->n, sizeof *at);
		kp_t_ok(T, at != NULL, "out of memory");
		for (k = 1; k < gs->n; k++) gs->g[k].first = gs->g[k - 1].first + gs->g[k - 1].count;
		for (j = 0; j < nfields; j++) {
			DumpGroup *d = dump_group(T, gs, &fields[j]);
			gs->order[d->first + at[d - gs->g]++] = j;
		}
		free(at);
		gs->nfields = nfields;
	}
	kp_t_ok(T, nfields == gs->nfields, "%s: %lu visits, the first game's %lu", path, (unsigned long)nfields,
		(unsigned long)gs->nfields);
	memset(&probe, 0, sizeof probe);
	probe.g = g;
	probe.snap = mix;
	probe.size = size;
	for (p = 0; p + 1 < DUMP_COVER_POINTS; p++) {
		const uint8_t *a = at_point[p], *b = at_point[p + 1];
		memcpy(mix, a, size);
		kp_t_ok(T, kp_restore(g, mix, size, &err) == 0, "%s: %s", path, err.msg);
		kp_t_ok(T, kp_dump_hash(g, 0, &base) == 0, "out of memory");
		for (k = 0; k < gs->n; k++) {
			DumpGroup *d = &gs->g[k];
			const KpDumpField *row = dump_row(d->name);
			int differs = 0, kept = 1;
			if (row && row->klass == KP_DUMP_SHOWN && !d->confirmed) {
				dump_bump(mix, gs, d, fields);
				probe.back = NULL;
				d->confirmed = dump_differs(T, &probe, gs, d, fields, base, NULL);
				memcpy(mix, a, size);
			}
			if (d->shown || d->rebuilt) continue;
			for (j = 0; j < d->count && !differs; j++) {
				const KpSnapshotField *f = &fields[gs->order[d->first + j]];
				differs = memcmp(a + f->offset, b + f->offset, f->bytes) != 0;
			}
			if (!differs) continue;
			d->differed = 1;
			for (j = 0; j < d->count; j++) {
				const KpSnapshotField *f = &fields[gs->order[d->first + j]];
				memcpy(mix + f->offset, b + f->offset, f->bytes);
			}
			probe.back = back;
			if (dump_differs(T, &probe, gs, d, fields, base, &kept)) d->shown = 1;
			else if (!kept) d->rebuilt = 1;
			for (j = 0; j < d->count; j++) {
				const KpSnapshotField *f = &fields[gs->order[d->first + j]];
				memcpy(mix + f->offset, a + f->offset, f->bytes);
			}
		}
	}
	for (p = 0; p + 1 < DUMP_COVER_POINTS; p++) {
		dump_scratch_from(T, gs, fields, g, r, &at_run[p], at_point[p], at_point[p + 1], mix, size, path);
		dump_scratch_from(T, gs, fields, g, r, &at_run[p + 1], at_point[p + 1], at_point[p], mix, size, path);
	}
	kp_t_release(T, mark);
}

static void dump_shows_the_saved_state(KpT *T)
{
	static DumpGroups gs;
	const KpDumpField *rows;
	KpBuf *bad, *idle, *untaken;
	size_t k;
	int n, i, *used;
	if (!kp_t_ready(T, "test_save", FILES) || !kp_t_has(T, DIR "replays/index.json")) return;
	memset(&gs, 0, sizeof gs);
	kp_t_defer(T, dump_groups_free, &gs);
	gs.scratch_bad = kp_t_buf(T);
	for (i = 0; i < (int)(sizeof DUMP_DONORS / sizeof DUMP_DONORS[0]); i++) dump_cover_game(T, &gs, DUMP_DONORS[i]);
	rows = kp_dump_fields(&n);
	used = (int *)calloc((size_t)n, sizeof *used);
	kp_t_ok(T, used != NULL, "out of memory");
	kp_t_defer(T, free, used);
	bad = kp_t_buf(T);
	idle = kp_t_buf(T);
	untaken = kp_t_buf(T);
	for (k = 0; k < gs.n; k++) {
		const DumpGroup *d = &gs.g[k];
		const KpDumpField *row = dump_row(d->name);
		size_t same = 0;
		if (row) used[row - rows] = 1;
		while (same < k && strcmp(d->name, gs.g[same].name) != 0) same++; /* an earlier group of its name */
		if (same < k) {
			kp_buf_printf(bad, "\n  %s: walked at lines %d and %d under one name", d->name, gs.g[same].line, d->line);
		} else if (d->rebuilt) {
			kp_buf_printf(bad, "\n  %s: the restore derives it, so it need not be saved", d->name);
		} else if (row && row->klass == KP_DUMP_SHOWN) {
			if (d->shown) kp_buf_printf(bad, "\n  %s: a game shows it now; drop its row", d->name);
			else if (!d->confirmed) kp_buf_printf(bad, "\n  %s: the dump does not show it", d->name);
		} else if (row && d->shown) {
			kp_buf_printf(bad, "\n  %s: the dump shows it; drop its row", d->name);
		} else if (!row && d->differed && !d->shown) {
			kp_buf_printf(bad, "\n  %s: the dump does not show it; dump it or list it in kp_dump_fields", d->name);
		} else if (!row && !d->differed) {
			kp_buf_printf(idle, " %s", d->name);
		}
	}
	for (i = 0; i < n; i++) {
		if (!used[i]) kp_buf_printf(bad, "\n  row %s: no saved field", rows[i].name);
	}
	kp_t_log(T, "dump coverage: %lu saved fields; no game changes:%s", (unsigned long)gs.n, kp_buf_text(idle));
	for (k = 0; k < gs.n; k++) {
		const KpDumpField *row = dump_row(gs.g[k].name);
		if (row && row->klass == KP_DUMP_SCRATCH && !gs.g[k].scratched) kp_buf_printf(untaken, " %s", gs.g[k].name);
	}
	kp_t_log(T, "scratch: %d games ran on with another state's; never taken:%s", gs.scratch_runs, kp_buf_text(untaken));
	kp_t_ok(T, gs.scratch_runs > 0, "no game ran on with another state's scratch");
	kp_t_ok(T, gs.scratch_bad->size == 0, "the scratch:%s", kp_buf_text(gs.scratch_bad));
	kp_t_ok(T, bad->size == 0, "the dump's coverage:%s", kp_buf_text(bad));
}

/* A snapshot that does not fit is refused before the game changes: a short buffer, another mission's snapshot. */
static void restores_refuse_other_snapshots(KpT *T)
{
	const char *path = "scenarios/marine_group";
	char other_path[512];
	KpOracleGame *game, *other;
	uint8_t *snap, *small;
	size_t size;
	KpError err;
	KpBuf *before, *after, *game_before, *game_after;
	if (!kp_t_ready(T, "test_save", FILES)) return;
	game = kp_t_game(T, path, 1, 0, NULL);
	snap = snapshot(T, game->g, &size);
	game_before = kp_t_buf(T);
	game_after = kp_t_buf(T);
	kp_t_ok(T, kp_state_lines(game->g, 0, game_before) == 0, "out of memory");
	small = (uint8_t *)malloc(size - 1);
	kp_t_ok(T, small != NULL, "out of memory");
	kp_t_defer(T, free, small);
	kp_t_ok(T, kp_snapshot(game->g, small, size - 1, &err) != 0 && strstr(err.msg, "too small"), "a short buffer takes no snapshot: %s",
		err.msg);
	snprintf(other_path, sizeof other_path, "initial_state/%s", kp_json_str(kp_json_at(load_golden(T, "initial_state/index"), 0), ""));
	snprintf(other_path, sizeof other_path, "missions/%s", kp_json_field_str(load_golden(T, other_path), "mission", ""));
	other = kp_t_game(T, other_path, 1, 0, NULL);
	before = kp_t_buf(T);
	after = kp_t_buf(T);
	kp_t_ok(T, kp_state_lines(other->g, 0, before) == 0, "out of memory");
	kp_t_ok(T, kp_restore(other->g, snap, size, &err) != 0, "another mission's snapshot is refused");
	kp_t_ok(T, kp_restore(game->g, snap, size - 1, &err) != 0 && strstr(err.msg, "buffer of"), "a cut snapshot is refused: %s", err.msg);
	kp_t_ok(T, kp_state_lines(other->g, 0, after) == 0, "out of memory");
	kp_t_ok(T, before->size == after->size && memcmp(before->data, after->data, before->size) == 0, "the refused snapshot changed nothing");
	kp_t_ok(T, kp_state_lines(game->g, 0, game_after) == 0, "out of memory");
	kp_t_ok(T, game_before->size == game_after->size && memcmp(game_before->data, game_after->data, game_before->size) == 0,
		"the cut snapshot changed nothing");
}

static const KpTest TESTS[] = {
	{ "walk_covers_the_state", walk_covers_the_state, 0 },
	{ "hash_is_the_saves_bytes", hash_is_the_saves_bytes, 0 },
	{ "view_state_is_saved_outside_the_total", view_state_is_saved_outside_the_total, 0 },
	{ "loads_clear_the_step_scratch", loads_clear_the_step_scratch, 0 },
	{ "save_schema_matches_its_version", save_schema_matches_its_version, 0 },
	{ "snapshots_rewind_as_the_original", snapshots_rewind_as_the_original, 6 },
	{ "restores_refuse_other_snapshots", restores_refuse_other_snapshots, 0 },
	{ "dump_shows_the_saved_state", dump_shows_the_saved_state, 0 },
	{ "saved_scenarios_continue_as_the_original", saved_scenarios_continue_as_the_original, 6 },
	{ "saved_wasteland_continues_as_the_original", saved_wasteland_continues_as_the_original, 0 },
	{ "campaign_plays_and_saves_equal_the_original", campaign_plays_and_saves_equal_the_original, 12 },
	{ "saved_plays_catch_every_interrupted_process", saved_plays_catch_every_interrupted_process, 0 },
	{ "saved_ai_runs_continue_as_the_original", saved_ai_runs_continue_as_the_original, 0 },
	{ "initial_states_survive_a_save", initial_states_survive_a_save, 0 },
	{ "loads_reject_other_saves", loads_reject_other_saves, 0 },
	{ "loads_refuse_scalars_a_game_cannot_hold", loads_refuse_scalars_a_game_cannot_hold, 0 },
	{ "loads_refuse_indices_out_of_their_arrays", loads_refuse_indices_out_of_their_arrays, 0 },
	{ "a_record_never_used_stays_out_of_the_spatial_index", a_record_never_used_stays_out_of_the_spatial_index, 0 },
	{ "loads_show_the_loaded_countdown", loads_show_the_loaded_countdown, 0 },
};
KP_SUITE(kp_suite_save, "test_save", TESTS);
