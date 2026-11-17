/* Test scenarios (phase 3, level C) against the original, object step by object step: each scenario of
 * tests/golden/oracle/scenarios/index.json (and each campaign run of runs/index.json) starts from its CHK with the
 * original's seed, replays the original's command and interface logs and is compared at every recorded step with the
 * CRCs of the original's state dumps (tools/state_check.py; from step 3 on without the drawn image bounds); a scenario
 * with triggers also compares its trigger trace (plan phase 5 item 8), and one with AI scripts its AI trace (plan phase
 * 7) by the trace's CRC-32. The campaign plays and chains (plan phase 8) are checked the same way, the plays to their
 * victory and the chains through the campaign's progression, the custom games of the Play Custom screen (phase M) from
 * their lobby's setups. Needs the converter's generated data; skipped without it. */
#include "kp_test.h"
#include "kp_data.h"
#include "kp_gametype.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DIR "tests/golden/oracle/"

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
	const unsigned char *mine;
	char path[512];
	int k;
	snprintf(path, sizeof path, DIR "%s/index.json", kind);
	index = kp_t_golden(T, path);
	snprintf(path, sizeof path, DIR "%s/%%s.json", kind);
	mine = kp_t_mine_goldens(T, index, path);
	for (k = 0; k < kp_json_len(index); k++) {
		const char *name = kp_json_str(kp_json_at(index, k), "");
		int found, mark = kp_t_mark(T);
		if (!mine[k]) continue;
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

/* The original playing replays of its own games (tests/golden/oracle/replays, phase RP): the commands and interface
 * events it logged while playing, run in a replay's mode (the golden's "replay": the replay flag, full vision, no
 * result dialog's clock), equal at every step. */
static void replays_equal_the_original(KpT *T)
{
	const KpJson *index;
	const unsigned char *mine;
	KpBuf *bad;
	int k;
	if (!kp_t_ready(T, "test_scenarios", DATA_FILES)) return;
	if (!kp_t_has(T, DIR "replays/index.json")) return;
	index = kp_t_golden(T, DIR "replays/index.json");
	mine = kp_t_mine_goldens(T, index, DIR "replays/%s.json");
	bad = kp_t_buf(T);
	for (k = 0; k < kp_json_len(index); k++) {
		const char *name = kp_json_str(kp_json_at(index, k), "");
		int found, mark = kp_t_mark(T);
		KpGoldenRun *gr;
		if (!mine[k]) continue;
		gr = check_run(T, golden(T, "replays", name), name, NULL, bad, &found);
		kp_t_ok(T, gr->game.g->w->replay && gr->game.g->local_vision == 0xff, "%s: played as a replay", name);
		kp_t_release(T, mark);
	}
	kp_t_ok(T, bad->size == 0, "replays differ from the original: %s", kp_buf_text(bad));
}

/* The custom games the original played from its Play Custom screen (tests/golden/oracle/custom, phase M): a map of the
 * 1.17 install's maps folder (KP_TEST_ORIGINAL) started from the lobby's records (the golden's setup), equal at every
 * compared step (the melee games at their start until stage M3 ports the melee AI). */
static void custom_games_equal_the_original(KpT *T)
{
	const char *root = getenv("KP_TEST_ORIGINAL");
	const KpJson *index;
	const unsigned char *mine;
	KpOracleOpts opts;
	char maps[1024];
	KpBuf *bad;
	int k;
	if (!kp_t_ready(T, "test_scenarios", DATA_FILES)) return;
	if (!root || !root[0]) kp_t_skip(T, "no KP_TEST_ORIGINAL (the custom games' maps)");
	snprintf(maps, sizeof maps, "%s/maps", root);
	kp_oracle_opts(&opts);
	opts.maps = maps;
	index = kp_t_golden(T, DIR "custom/index.json");
	mine = kp_t_mine_goldens(T, index, DIR "custom/%s.json");
	bad = kp_t_buf(T);
	for (k = 0; k < kp_json_len(index); k++) {
		const char *name = kp_json_str(kp_json_at(index, k), "");
		int found, mark = kp_t_mark(T);
		KpGoldenRun *gr;
		if (!mine[k]) continue;
		gr = check_run(T, golden(T, "custom", name), name, &opts, bad, &found);
		kp_t_eq(T, kp_scalar(gr->game.g, KP_MISSION_INDEX), 0, "%s: campaign row 0", name);
		kp_t_release(T, mark);
	}
	kp_t_ok(T, bad->size == 0, "custom games differ from the original: %s", kp_buf_text(bad));
}

/* Our own AI code over the data's aiscript.bin (kp_oracle_ai_patch, a custom golden's "ai_patch"; scdata/analyze/
 * ai_asm.py assembles it): each Brood War melee script's record names a stub appended to the file (start_town, multirun
 * the probe, goto the entry's next instruction). A probe that stops starts x-melee-2p's game; one that reaches
 * fatal_error refuses it before its start (the melee scripts' check, kp_gametype_victory); a patch over another
 * aiscript.bin or of a script the directory lacks is refused. */
#define STUB_SIZE 7
#define OP_STOP 0x24
#define OP_FATAL_ERROR 0x28
static const char *const MELEE_IDS[3] = { "TMCx", "ZMCx", "PMCx" };

static uint32_t fourcc(const char *id)
{
	return (uint32_t)(uint8_t)id[0] | (uint32_t)(uint8_t)id[1] << 8 | (uint32_t)(uint8_t)id[2] << 16
		| (uint32_t)(uint8_t)id[3] << 24;
}

/* The patch's JSON: the three stubs, then the probe's one opcode; the first record's id first_id. */
static const KpJson *melee_patch(KpT *T, KpJsonDoc *doc, const KpAiBins *base, uint8_t probe, uint32_t crc,
	const char *first_id)
{
	KpBuf *text = kp_t_buf(T);
	uint32_t size = base->bin_size[0], probe_at = size + 3 * STUB_SIZE, k;
	const KpJson *v;
	char why[200];
	kp_buf_printf(text, "{\"name\":\"test\",\"size\":%u,\"crc32\":%u,\"code\":\"", (unsigned)size, (unsigned)crc);
	for (k = 0; k < 3; k++) {
		int bin;
		uint32_t next = kp_ai_bins_find(base, fourcc(MELEE_IDS[k]), &bin) + 1;
		kp_buf_printf(text, "0348%02x%02x00%02x%02x", probe_at & 0xff, probe_at >> 8, next & 0xff, next >> 8);
	}
	kp_buf_printf(text, "%02x\",\"redirect\":[", probe);
	for (k = 0; k < 3; k++)
		kp_buf_printf(text, "%s[\"%s\",%u]", k ? "," : "", k ? MELEE_IDS[k] : first_id, (unsigned)(size + k * STUB_SIZE));
	kp_buf_str(text, "]}");
	kp_json_doc_init(doc);
	if (!(v = kp_json_parse(doc, kp_buf_text(text), text->size, why, sizeof why))) kp_t_fail(T, "the patch: %s", why);
	return v;
}

static void ai_patches_start_the_melee_scripts(KpT *T)
{
	const char *root = getenv("KP_TEST_ORIGINAL");
	const KpData *d;
	const KpAiBins *base;
	const KpJson *g;
	char maps[1024];
	KpError err;
	KpJsonDoc doc;
	KpAiBins bins;
	uint32_t crc;
	int probe, rc;
	if (!kp_t_ready(T, "test_scenarios", DATA_FILES)) return;
	if (!root || !root[0]) kp_t_skip(T, "no KP_TEST_ORIGINAL (the custom games' maps)");
	snprintf(maps, sizeof maps, "%s/maps", root);
	g = golden(T, "custom", "x-melee-2p");
	if (!(d = kp_oracle_data(".", &err))) kp_t_fail(T, "%s", err.msg);
	base = &d->shared.data.ai;
	crc = kp_crc32(base->bin[0], base->bin_size[0]);
	for (probe = 0; probe < 2; probe++) {
		KpGameSetup setup;
		KpOracleOpts opts;
		KpOracleGame og;
		int bin, k;
		rc = kp_oracle_ai_patch(&bins, base, melee_patch(T, &doc, base, probe ? OP_FATAL_ERROR : OP_STOP, crc, MELEE_IDS[0]),
			&err);
		kp_json_doc_free(&doc);
		kp_t_eq(T, rc, 0, "the patch applies: %s", err.msg);
		for (k = 0; k < 3; k++) {
			kp_t_eq(T, kp_ai_bins_find(&bins, fourcc(MELEE_IDS[k]), &bin), base->bin_size[0] + k * STUB_SIZE, "%s's stub",
				MELEE_IDS[k]);
		}
		kp_t_eq(T, bins.bin_size[1], base->bin_size[1], "bwscript.bin kept");
		kp_oracle_opts(&opts);
		opts.maps = maps;
		if (kp_oracle_setup_from_json(kp_json_get(g, "setup"), &setup, &err) != 0) kp_t_fail(T, "%s", err.msg);
		opts.host.setup = &setup;
		opts.host.ai_bins = &bins;
		rc = kp_oracle_game_start(&og, ".", kp_json_field_str(g, "mission", ""), (uint32_t)kp_json_field_int(g, "seed", 0),
			(int)kp_json_field_int(g, "expansion", 0), &opts, &err);
		if (rc == 0) kp_oracle_game_free(&og);
		kp_ai_bins_free(&bins);
		if (probe) kp_t_ok(T, rc != 0 && strstr(err.msg, "fatal_error"), "a probe of fatal_error: %s", rc ? err.msg : "started");
		else kp_t_eq(T, rc, 0, "a probe that stops starts: %s", err.msg);
	}
	rc = kp_oracle_ai_patch(&bins, base, melee_patch(T, &doc, base, OP_STOP, crc + 1, MELEE_IDS[0]), &err);
	kp_json_doc_free(&doc);
	kp_t_ok(T, rc != 0 && strstr(err.msg, "assembled over"), "another aiscript.bin: %s", rc ? err.msg : "applied");
	rc = kp_oracle_ai_patch(&bins, base, melee_patch(T, &doc, base, OP_STOP, crc, "Nope"), &err);
	kp_json_doc_free(&doc);
	kp_t_ok(T, rc != 0 && strstr(err.msg, "cannot redirect Nope"), "an unknown script: %s", rc ? err.msg : "applied");
}

/* The race of a race list's item (FillRaceList 0x456170's 0x503da4: Zerg, Terran, Protoss, Random). */
static const int64_t RACE_ITEMS[4] = { 0, 1, 2, 6 };
#define ID_LOCAL_RACE 28
#define ID_FIRST_OPPONENT_RACE 29
#define ID_LAST_OPPONENT_RACE 35

/* The race a race list of the golden's screen gives (its selected item's), or KP_RACE_NONE when it is hidden. */
static int64_t screen_race(const KpJson *screen, int id, int64_t without_selection)
{
	char key[8];
	const KpJson *c;
	int64_t k;
	snprintf(key, sizeof key, "%d", id);
	c = kp_json_get(screen, key);
	if (!c || !kp_json_field_int(c, "usable", 0)) return KP_RACE_NONE;
	k = kp_json_field_int(c, "selected", 255);
	return k >= 0 && k < 4 ? RACE_ITEMS[k] : without_selection;
}

/* The lobby a Play Custom game leaves for its start (kp_setup_lobby, stage M4): each custom golden's map with its
 * template and the races its screen shows (ApplyCustomGameRaces 0x455020: a melee template's local race list and the
 * shown opponents' lists in order, another template's local race list when it is shown) gives the records the
 * original's StartGame took (the golden's "setup", its custom races applied) and the original's custom races. Each
 * emulator run is its process's first game: the game RNG before the seed is 0. The player's name is the emulator's
 * profile's ("Custom"). */
#define LOBBY_NAME "Custom"
static void lobby_records_are_the_originals(KpT *T)
{
	const char *root = getenv("KP_TEST_ORIGINAL");
	const KpData *d;
	const KpJson *index;
	KpBuf *bad;
	int k;
	if (!kp_t_ready(T, "test_scenarios", DATA_FILES)) return;
	if (!root || !root[0]) kp_t_skip(T, "no KP_TEST_ORIGINAL (the custom games' maps)");
	d = kp_t_data(T);
	index = kp_t_golden(T, DIR "custom/index.json");
	bad = kp_t_buf(T);
	for (k = 0; k < kp_json_len(index); k++) {
		const char *name = kp_json_str(kp_json_at(index, k), "");
		const KpJson *g = golden(T, "custom", name), *v = kp_json_get(g, "setup");
		const KpJson *races = kp_json_get(v, "custom_races"), *screen = kp_json_get(kp_json_get(g, "lobby"), "screen");
		const char *mission = kp_json_field_str(g, "mission", "");
		int64_t opponents[KP_CUSTOM_RACES], local_race;
		KpGameSetup want, got;
		KpDataMap map;
		KpMission m;
		KpError err;
		char path[1024];
		int j, n, p, melee;
		if (!screen) {
			kp_buf_printf(bad, "%s: no lobby in the golden; ", name);
			continue;
		}
		if (kp_oracle_setup_from_json(v, &want, &err) != 0) {
			kp_buf_printf(bad, "%s: %s; ", name, err.msg);
			continue;
		}
		melee = want.template_record[KP_TEMPLATE_VICTORY] == KP_VICTORY_MELEE;
		local_race = screen_race(screen, ID_LOCAL_RACE, KP_RACE_RANDOM);
		for (j = 0; j < KP_CUSTOM_RACES; j++) opponents[j] = KP_RACE_NONE;
		for (j = ID_FIRST_OPPONENT_RACE, n = 0; melee && j <= ID_LAST_OPPONENT_RACE; j++) {
			int64_t r = screen_race(screen, j, KP_RACE_NONE);
			if (r != KP_RACE_NONE) opponents[n++] = r;
		}
		if (melee) { /* the screen's races are the custom races the original's StartGame applied */
			int same = kp_json_int(kp_json_at(races, 0), -1) == local_race;
			for (j = 0; j + 1 < kp_json_len(races); j++) same &= kp_json_int(kp_json_at(races, j + 1), -1) == opponents[j];
			if (!same) kp_buf_printf(bad, "%s: the screen's races are not the custom races; ", name);
		}
		snprintf(path, sizeof path, "%s/maps/%s", root, mission + strlen("maps/"));
		if (kp_data_map_open(&map, &d->files, path, &err) != 0) {
			kp_buf_printf(bad, "%s: %s; ", name, err.msg);
			continue;
		}
		if (kp_data_map_mission(&map, kp_template_ums(want.template_record) ? KP_CHK_UMS : KP_CHK_MELEE, &m, &err) != 0) {
			kp_buf_printf(bad, "%s: %s; ", name, err.msg);
			kp_data_map_close(&map);
			continue;
		}
		if (kp_setup_lobby(&got, &m, want.template_record, LOBBY_NAME, local_race, opponents, 0) != 0) {
			kp_buf_printf(bad, "%s: no open slot; ", name);
		} else {
			if (memcmp(got.template_record, want.template_record, KP_TEMPLATE_SIZE) != 0 || got.slots != want.slots
				|| got.multiplayer != want.multiplayer || got.started != want.started) {
				kp_buf_printf(bad, "%s: the template or slot count; ", name);
			}
			for (p = 0; p < KP_SETUP_SLOTS; p++) {
				if (got.storm[p] != want.storm[p] || got.type[p] != want.type[p] || got.race[p] != want.race[p]
					|| got.force[p] != want.force[p] || strcmp(got.name[p], want.name[p]) != 0) {
					kp_buf_printf(bad, "%s: slot %d (type %lld race %lld force %lld storm %lld, the original's %lld %lld "
						"%lld %lld); ", name, p, (long long)got.type[p], (long long)got.race[p], (long long)got.force[p],
						(long long)got.storm[p], (long long)want.type[p], (long long)want.race[p],
						(long long)want.force[p], (long long)want.storm[p]);
				}
			}
		}
		kp_mission_free(&m);
		kp_data_map_close(&map);
	}
	kp_t_ok(T, bad->size == 0, "lobbies differ from the original's: %s", kp_buf_text(bad));
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
	{ "replays_equal_the_original", replays_equal_the_original, 4 },
	{ "custom_games_equal_the_original", custom_games_equal_the_original, 2 },
	{ "ai_patches_start_the_melee_scripts", ai_patches_start_the_melee_scripts, 0 },
	{ "lobby_records_are_the_originals", lobby_records_are_the_originals, 0 },
	{ "escape_skips_running_triggers_only", escape_skips_running_triggers_only, 0 },
};
KP_SUITE(kp_suite_scenarios, "test_scenarios", TESTS);
