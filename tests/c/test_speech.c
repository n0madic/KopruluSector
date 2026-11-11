/* A language's speech over the campaign (ADR 0016, localization's stage L5). The speech's triggers
 * (generated/speech/<loc>/missions: the base's records with the TIME of the language's lines) lay over a mission
 * before its start (kp_host_game_speech); a transmission waits its TIME, so a speech changes the game's timing, and a
 * play recorded against the base's no longer fits. The original's game played every campaign play again with ruRU's
 * TIME (tools/research/sc_emulator/speech.py writes it into the loaded map's trigger lists; campaign_plays.py's
 * reactive plays adapt to it): tests/golden/oracle/plays-ruRU (tools/state_check.py --kind plays --speech ruRU).
 * Koprulu with the speech equals each at every compared step and in the trigger and AI traces, and wins; the plays are
 * those of tests/golden/oracle/plays, every campaign mission. Skipped without the generated data and the speech. */
#include "kp_test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kp_session.h"

#define PLAYS "tests/golden/oracle/plays/"
#define PLAYS_SPEECH "tests/golden/oracle/plays-ruRU/"
#define SPEECH "generated/speech/ruRU/missions/"
#define RESULT_VICTORY 3
#define RESULT_STEPS 100    /* a play's dumps (every 20th step) end when the result dialog pauses the game */
#define CAMPAIGN_MISSIONS 64

#define TRIGGER_RECORD 2400 /* a CHK TRIG record: 16 conditions of 20 bytes, 64 actions of 32, the flags */
#define TRIGGER_CONDITIONS 320
#define TRIGGER_ACTIONS 64
#define TRIGGER_ACTION 32
#define TRIGGER_ACTION_TIME 0x0c /* KP_ACTION_TIME: u32 milliseconds */
#define SAVE_STEP 600       /* a_save_keeps_its_speech: the step of the save, the steps after it and their stride */
#define SAVE_ON 1200
#define SAVE_EVERY 200
#define SAVE_DATA "test data"

static const char *const FILES[] = { KP_T_DATA, PLAYS "index.json", PLAYS_SPEECH "index.json",
	SPEECH "terran/tutorial.kpm", NULL };

/* Every play the original made with the speech: Koprulu with the speech equals it at every 20th step by the state's
 * fingerprint, every 500th by its categories and in the traces, and it ends in the local player's victory. The plays
 * are the base's, so every campaign mission is played. */
static void campaign_plays_with_the_speech_equal_the_original(KpT *T)
{
	const KpJson *index, *base;
	KpBuf *bad;
	KpOracleOpts o;
	int k, missions = 0;
	char seen[CAMPAIGN_MISSIONS][64];
	if (!kp_t_ready(T, "test_speech", FILES)) return;
	index = kp_t_golden(T, PLAYS_SPEECH "index.json");
	base = kp_t_golden(T, PLAYS "index.json");
	bad = kp_t_buf(T);
	kp_oracle_opts(&o);
	o.speech = SPEECH;
	kp_t_eq(T, kp_json_len(index), kp_json_len(base), "the plays with the speech, the base's");
	for (k = 0; k < kp_json_len(index); k++) {
		const char *name = kp_json_str(kp_json_at(index, k), ""), *mission;
		char path[512];
		const KpJson *g;
		KpGoldenRun *gr;
		KpGame *game;
		int64_t step;
		int m, mark = kp_t_mark(T);
		kp_t_ok(T, k < kp_json_len(base) && strcmp(name, kp_json_str(kp_json_at(base, k), "")) == 0,
			"play %d: %s is the base's", k, name);
		snprintf(path, sizeof path, PLAYS_SPEECH "%s.json", name);
		g = kp_t_golden(T, path);
		mission = kp_json_field_str(g, "mission", "");
		for (m = 0; m < missions && strcmp(seen[m], mission) != 0; m++) continue;
		if (m == missions && missions < CAMPAIGN_MISSIONS) snprintf(seen[missions++], sizeof seen[0], "%s", mission);
		if (!kp_t_mine_sampled(T, k + 1)) {
			kp_t_release(T, mark);
			continue;
		}
		gr = kp_t_golden_run(T, g, &o, kp_json_get(g, "trace") != NULL || kp_json_get(g, "ai_trace") != NULL);
		game = gr->game.g;
		step = gr->replay.last;
		if (kp_golden_run_check(gr, g, name, bad) == 0) { /* the result comes within the compared stride after the last step */
			while (game->results[game->local_player] == 0 && step < gr->replay.last + RESULT_STEPS) kp_runner_to(&gr->run, ++step);
			if (game->results[game->local_player] != RESULT_VICTORY) kp_buf_printf(bad, "%s%s no victory", bad->size ? ", " : "", name);
		}
		kp_t_release(T, mark);
	}
	kp_t_eq(T, missions, CAMPAIGN_MISSIONS, "the plays' missions");
	kp_t_ok(T, bad->size == 0, "plays with the speech differ from the original's: %s", kp_buf_text(bad));
}

/* The TIME of every action of the game's trigger copies, summed (which speech the game's triggers have). */
static uint64_t trigger_times(const KpGame *g)
{
	uint64_t sum = 0;
	int64_t c;
	int k;
	for (c = 0; c < g->trig.count; c++) {
		const uint8_t *r = g->trig.records + c * TRIGGER_RECORD;
		for (k = 0; k < TRIGGER_ACTIONS; k++) sum += kp_le_u32(r + TRIGGER_CONDITIONS + k * TRIGGER_ACTION + TRIGGER_ACTION_TIME);
	}
	return sum;
}

/* A golden play's game with the speech or without, run to step `to` by its own log. */
static KpGoldenRun *run_to(KpT *T, const KpJson *g, const char *speech, int64_t to)
{
	KpOracleOpts o;
	KpGoldenRun *gr;
	kp_oracle_opts(&o);
	o.speech = speech;
	gr = kp_t_golden_run(T, g, &o, 0);
	kp_runner_to(&gr->run, to);
	return gr;
}

/* A save keeps its game's speech (the TIME is in the saved trigger copies; the save's format does not change): a game
 * saved with the speech and loaded into one without (and the other way) has the saved game's TIME, saves the same
 * bytes again and goes on as the saved game does. */
static void a_save_keeps_its_speech(KpT *T)
{
	static const char *const PLAYS_SAVED[] = { "terran-m01", "xzerg-m49" };
	int n, way;
	if (!kp_t_ready(T, "test_speech", FILES)) return;
	for (n = 0; n < (int)(sizeof PLAYS_SAVED / sizeof PLAYS_SAVED[0]); n++) {
		char path[512];
		const KpJson *g;
		const char *mission;
		snprintf(path, sizeof path, PLAYS "%s.json", PLAYS_SAVED[n]);
		g = kp_t_golden(T, path);
		mission = kp_json_field_str(g, "mission", "");
		for (way = 0; way < 2; way++) { /* 0: saved with the speech, loaded without; 1: the other way */
			int mark = kp_t_mark(T);
			KpGoldenRun *saved = run_to(T, g, way == 0 ? SPEECH : NULL, SAVE_STEP);
			KpGoldenRun *into = run_to(T, g, way == 0 ? NULL : SPEECH, 1);
			uint8_t *bytes, *again;
			size_t size, again_size;
			KpError err;
			int64_t step;
			kp_t_ok(T, trigger_times(saved->game.g) != trigger_times(into->game.g), "%s: the speech changes the TIME",
				PLAYS_SAVED[n]);
			if (kp_save(saved->game.g, mission, SAVE_DATA, &bytes, &size, &err) != 0) kp_t_fail(T, "%s", err.msg);
			kp_t_defer(T, free, bytes);
			if (kp_load(into->game.g, bytes, size, mission, SAVE_DATA, &err) != 0) kp_t_fail(T, "%s", err.msg);
			kp_t_eq(T, trigger_times(into->game.g), trigger_times(saved->game.g), "%s: the saved TIME", PLAYS_SAVED[n]);
			if (kp_save(into->game.g, mission, SAVE_DATA, &again, &again_size, &err) != 0) kp_t_fail(T, "%s", err.msg);
			kp_t_defer(T, free, again);
			kp_t_ok(T, again_size == size && memcmp(again, bytes, size) == 0, "%s: the same bytes again", PLAYS_SAVED[n]);
			into->run.step = saved->run.step;
			into->run.next_command = saved->run.next_command;
			into->run.next_ui = saved->run.next_ui;
			into->run.begun = 1;
			for (step = SAVE_STEP + SAVE_EVERY; step <= SAVE_STEP + SAVE_ON; step += SAVE_EVERY) {
				KpStateCrcs a, b;
				kp_runner_to(&saved->run, step);
				kp_runner_to(&into->run, step);
				kp_t_ok(T, kp_state_crcs(saved->game.g, 1, &a) == 0 && kp_state_crcs(into->game.g, 1, &b) == 0, "state CRCs");
				kp_t_eq(T, kp_state_fingerprint(&b), kp_state_fingerprint(&a), "%s %s: step %lld", PLAYS_SAVED[n],
					way == 0 ? "with the speech, loaded without" : "without, loaded with the speech", (long long)step);
			}
			kp_t_release(T, mark);
		}
	}
}

static const KpTest TESTS[] = {
	{ "campaign_plays_with_the_speech_equal_the_original", campaign_plays_with_the_speech_equal_the_original, 8 },
	{ "a_save_keeps_its_speech", a_save_keeps_its_speech, 0 },
};

KP_SUITE(kp_suite_speech, "test_speech", TESTS);
