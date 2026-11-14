/* A language's speech over the campaign (ADR 0016, localization's stage L5; ADR 0019, stage C2: read from the data as
 * Remastered keeps it). The TIME of the language's map's actions lays over a mission before its start
 * (kp_host_game_speech); a transmission waits its TIME, so a speech changes the game's timing, and a play recorded
 * against the base's no longer fits. The original's game played every campaign play again with ruRU's TIME
 * (tools/research/sc_emulator/speech.py writes it into the loaded map's trigger lists; campaign_plays.py's reactive
 * plays adapt to it): tests/golden/oracle/plays-ruRU (tools/state_check.py --kind plays --speech ruRU). Koprulu with
 * the speech equals each at every compared step and in the trigger and AI traces, and wins; the plays are those of
 * tests/golden/oracle/plays, every campaign mission. The merged records and the voices' records equal what the
 * converter wrote before stage C2 (tests/golden/speech/converter.json). Skipped without the original data and
 * KP_TEST_SCR (StarCraft: Remastered, which holds the languages). */
#include "kp_test.h"
#include "kp_test_app.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kp_chk.h"
#include "kp_sha256.h"
#include "kp_ui.h"
#include "kp_ui_data.h"

#define PLAYS "tests/golden/oracle/plays/"
#define PLAYS_SPEECH "tests/golden/oracle/plays-ruRU/"
#define CONVERTER "tests/golden/speech/converter.json"
#define SPEECH "ruRU"
#define SCR "KP_TEST_SCR"
#define MISSION "missions/terran/terran01"
#define MISSION_NAME "terran/terran01" /* its campaign and name: the language's map's folder */
#define EXCEPTED "missions/expterran/terran03" /* defs/l10n/locales.json's speech: Remastered's own fixes there */
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
#define SEED 1

static const char *const FILES[] = { KP_T_DATA, PLAYS "index.json", PLAYS_SPEECH "index.json", NULL };

/* The Remastered install the speech is read from (KP_TEST_SCR); skips the test without it. */
static const char *scr(KpT *T)
{
	const char *dir = getenv(SCR);
	if (!dir || !dir[0]) kp_t_skip(T, "no " SCR);
	return dir;
}

static void host_free(void *og) { kp_host_game_free((KpHostGame *)og); }

/* A campaign mission's game loaded (not started), released at the end. */
static KpHostGame *loaded(KpT *T, const char *key)
{
	const KpData *d = kp_t_data(T);
	KpHostGame *og = (KpHostGame *)calloc(1, sizeof *og);
	KpMission m;
	KpError e;
	if (!og) kp_t_fail(T, "out of memory");
	kp_t_defer(T, free, og);
	if (!kp_oracle_mission(".", key, &m, &e) || kp_host_game_load_from(og, &d->shared, &d->dat, &m, key, &e) != 0) {
		kp_t_fail(T, "%s: %s", key, e.msg);
	}
	kp_t_defer(T, host_free, og);
	return og;
}

/* The SHA-256 of a game's TRIG then MBRF, in hexadecimal. */
static void records_digest(const KpHostGame *og, char hex[2 * KP_SHA256_SIZE + 1])
{
	KpSha256 s;
	uint8_t d[KP_SHA256_SIZE];
	kp_sha256_init(&s);
	kp_sha256_update(&s, og->data.mission.triggers.data, og->data.mission.triggers.size);
	kp_sha256_update(&s, og->data.mission.briefing.data, og->data.mission.briefing.size);
	kp_sha256_final(&s, d);
	kp_sha256_hex(d, hex);
}

/* Every language of the install and every campaign mission: the records with the language's TIME and the voices'
 * records equal the converter's (its overlays' digests, recorded before stage C2 deleted it); a mission the profile
 * excepts is the one whose records differ in more than TIME. */
static void the_speech_equals_the_converters(KpT *T)
{
	static const char *const NEED[] = { KP_T_DATA, CONVERTER, NULL };
	const KpJson *golden, *sounds, *triggers;
	const KpData *d;
	KpFiles raw;
	int l, k;
	if (!kp_t_ready(T, "test_speech", NEED)) return;
	kp_res_raw_files(kp_t_install(T, SCR), &raw);
	d = kp_t_data(T);
	golden = kp_t_golden(T, CONVERTER);
	sounds = kp_json_get(golden, "sounds");
	triggers = kp_json_get(golden, "triggers");
	kp_t_ok(T, kp_json_len(triggers) > 0 && kp_json_len(sounds) == kp_json_len(triggers), "the golden's languages");
	for (l = 0; l < kp_json_len(triggers); l++) {
		const char *locale = triggers->keys[l];
		const KpJson *missions = triggers->items[l];
		char hex[2 * KP_SHA256_SIZE + 1];
		uint8_t digest[KP_SHA256_SIZE], *sfx;
		size_t size = 0;
		int voices = 0;
		KpError e;
		sfx = kp_ui_data_speech_sounds(&d->ui, &raw, locale, &size, &voices, &e);
		kp_t_ok(T, sfx != NULL && e.msg[0] == 0, "%s's voices: %s", locale, e.msg);
		kp_sha256(sfx, size, digest);
		free(sfx);
		kp_sha256_hex(digest, hex);
		kp_t_eq_str(T, hex, kp_json_field_str(sounds, locale, ""), "%s's voices' records (%d voices)", locale, voices);
		kp_t_eq(T, kp_json_len(missions), CAMPAIGN_MISSIONS, "%s's missions", locale);
		for (k = 0; k < kp_json_len(missions); k++) {
			int mark = kp_t_mark(T);
			char key[128];
			KpHostSpeech sp;
			KpHostGame *og;
			snprintf(key, sizeof key, "missions/%s", missions->keys[k]);
			og = loaded(T, key);
			kp_t_eq(T, kp_host_game_speech(og, &raw, locale, missions->keys[k], &sp, &e), 1, "%s %s: %s", locale, key, e.msg);
			records_digest(og, hex);
			kp_t_eq_str(T, hex, kp_json_str(missions->items[k], ""), "%s %s's records", locale, key);
			kp_t_ok(T, sp.excepted == (strcmp(key, EXCEPTED) == 0), "%s %s: excepted %d", locale, key, sp.excepted);
			kp_t_ok(T, sp.differ == 0 || sp.excepted, "%s %s: %d records differ in more than TIME, the first %s", locale, key,
				sp.differ, sp.first);
			kp_t_release(T, mark);
		}
	}
}

/* A reader of one scenario's bytes at a language's map name (and nothing else), for the refusals. */
typedef struct {
	const uint8_t *chk;
	size_t size;
} OneMap;

static uint8_t *one_map_read(void *ctx, const char *name, size_t *size, char *err, size_t err_size)
{
	const OneMap *m = (const OneMap *)ctx;
	uint8_t *b;
	char want[256];
	snprintf(want, sizeof want, KP_HOST_SPEECH_MAP, SPEECH, MISSION_NAME);
	if (err_size) err[0] = 0;
	if (strcmp(name, want) != 0 || !(b = (uint8_t *)malloc(m->size + 1))) return NULL;
	memcpy(b, m->chk, m->size);
	b[m->size] = 0;
	*size = m->size;
	return b;
}

/* kp_host_game_speech: the language's map's TIME over the mission's records (a copy taken with the game, a second
 * frees the first); no map leaves the mission's (0); a damaged map or one of other sizes too, with the error; an
 * action that differs in more than its TIME keeps the mission's and its record is counted; the game starts over it. */
static void the_speech_merges_the_maps_time(KpT *T)
{
	static const char *const NEED[] = { MISSION, NULL };
	const char *name = MISSION_NAME;
	KpHostGame *og;
	KpHostSpeech sp;
	KpFiles raw, one;
	KpError e;
	KpChk chk;
	OneMap map;
	char path[256], why[256];
	uint8_t *bytes, *changed;
	const uint8_t *own_trig, *own_mbrf, *merged;
	size_t size = 0, trig_at;
	const KpChkSection *trig;
	int k, times = 0;
	if (!kp_t_ready(T, "test_speech", NEED)) return;
	kp_res_raw_files(kp_t_install(T, SCR), &raw);
	og = loaded(T, MISSION);
	own_trig = og->data.mission.triggers.data;
	own_mbrf = og->data.mission.briefing.data;
	kp_t_eq(T, kp_host_game_speech(og, &raw, "xxXX", name, &sp, &e), 0, "a language the data lacks");
	kp_t_ok(T, og->data.mission.triggers.data == own_trig && og->speech == NULL, "the mission's own");
	/* the install's ruRU map: its TIME, nothing else */
	snprintf(path, sizeof path, KP_HOST_SPEECH_MAP, SPEECH, name);
	bytes = raw.read(raw.ctx, path, &size, why, sizeof why);
	kp_t_ok(T, bytes != NULL, "%s: %s", path, why);
	kp_t_defer(T, free, bytes);
	kp_t_eq(T, kp_host_game_speech(og, &raw, SPEECH, name, &sp, &e), 1, "%s", e.msg);
	merged = og->data.mission.triggers.data;
	kp_t_ok(T, merged != own_trig && og->data.mission.briefing.data != own_mbrf, "a copy");
	kp_t_ok(T, sp.times > 0 && sp.differ == 0 && !sp.excepted, "%d TIMEs, %d records apart", sp.times, sp.differ);
	for (k = 0; k < (int)(og->data.mission.triggers.size / TRIGGER_RECORD) * TRIGGER_ACTIONS; k++) {
		size_t at = (size_t)(k / TRIGGER_ACTIONS) * TRIGGER_RECORD + TRIGGER_CONDITIONS + (size_t)(k % TRIGGER_ACTIONS) * TRIGGER_ACTION;
		times += memcmp(merged + at + TRIGGER_ACTION_TIME, own_trig + at + TRIGGER_ACTION_TIME, 4) != 0;
		kp_t_ok(T, memcmp(merged + at, own_trig + at, TRIGGER_ACTION_TIME) == 0
			&& memcmp(merged + at + TRIGGER_ACTION_TIME + 4, own_trig + at + TRIGGER_ACTION_TIME + 4,
				TRIGGER_ACTION - TRIGGER_ACTION_TIME - 4) == 0, "action %d: its TIME alone", k);
	}
	kp_t_ok(T, times > 0 && times <= sp.times, "TRIG's TIMEs %d of %d", times, sp.times);
	/* the map damaged, then an action other in more than TIME */
	kp_t_eq(T, kp_chk_read(&chk, bytes, size, raw.profile, 1, why, sizeof why), 0, "%s", why);
	kp_t_ok(T, (trig = kp_chk_section(&chk, "TRIG")) != NULL, "the map's TRIG");
	trig_at = (size_t)(trig->data - bytes);
	one.read = one_map_read;
	one.head = NULL;
	one.ctx = &map;
	one.profile = raw.profile;
	map.chk = bytes;
	map.size = 16;
	kp_t_eq(T, kp_host_game_speech(og, &one, SPEECH, name, &sp, &e), -1, "a damaged map");
	kp_t_ok(T, og->data.mission.triggers.data == merged, "the speech before kept: %s", e.msg);
	if (!(changed = (uint8_t *)malloc(size))) kp_t_fail(T, "out of memory");
	kp_t_defer(T, free, changed);
	memcpy(changed, bytes, size);
	changed[trig_at + TRIGGER_CONDITIONS + 4] ^= 1; /* the first record's first action's string */
	map.chk = changed;
	map.size = size;
	kp_t_eq(T, kp_host_game_speech(og, &one, SPEECH, name, &sp, &e), 1, "%s", e.msg);
	kp_t_ok(T, sp.differ == 1 && strcmp(sp.first, "TRIG record 0") == 0, "%d apart, the first %s", sp.differ, sp.first);
	kp_t_ok(T, og->data.mission.triggers.data[TRIGGER_CONDITIONS + 4] == own_trig[TRIGGER_CONDITIONS + 4], "the mission's action");
	kp_t_eq(T, kp_host_game_tileset_files(og, &kp_t_data(T)->files, &e), 0, "%s", e.msg);
	kp_t_eq(T, kp_host_game_start(og, SEED, 0, NULL, &e), 0, "%s", e.msg);
	kp_t_guard(T, og->g);
}

/* The durations of an interface's sounds that differ from another's. */
static int durations_apart(const KpUi *a, const KpUi *b)
{
	int k, n = 0;
	for (k = 0; k < a->sound_count; k++) n += kp_ui_sound_duration(a, (uint32_t)k) != kp_ui_sound_duration(b, (uint32_t)k);
	return n;
}

static void ui_free(void *ui) { kp_ui_free((KpUi *)ui); }

static KpUi *new_ui(KpT *T)
{
	char err[256];
	KpUi *ui = kp_ui_new_data(&kp_t_data(T)->ui, err, sizeof err);
	kp_t_ok(T, ui != NULL, "%s", err);
	kp_t_defer(T, ui_free, ui);
	return ui;
}

/* The speech's sound records (kp_ui_data_speech_sounds over the install's ruRU) replace the base's: its voices'
 * durations; a table of another sound count leaves the base's, told in ui->speech_problem (the speech's), not in the
 * language's locale_problem. */
static void the_speech_sound_records_replace_the_bases(KpT *T)
{
	static const char *const NEED[] = { KP_T_DATA, NULL };
	KpUiLocale locale;
	KpUi *own, *spoken, *refused;
	KpFiles raw;
	KpError e;
	uint8_t *sfx;
	int voices = 0;
	if (!kp_t_ready(T, "test_speech", NEED)) return;
	kp_res_raw_files(kp_t_install(T, SCR), &raw);
	own = new_ui(T);
	spoken = new_ui(T);
	memset(&locale, 0, sizeof locale);
	sfx = kp_ui_data_speech_sounds(&kp_t_data(T)->ui, &raw, SPEECH, &locale.speech_size, &voices, &e);
	kp_t_ok(T, sfx != NULL && voices > 0, "the voices: %s", e.msg);
	kp_t_defer(T, free, sfx);
	locale.speech = sfx;
	kp_ui_set_locale(spoken, &locale);
	kp_t_eq_str(T, spoken->speech_problem, "", "accepted");
	kp_t_ok(T, durations_apart(own, spoken) > 0, "the speech's voices' durations");
	refused = new_ui(T);
	sfx[0] ^= 1; /* another sound count */
	kp_ui_set_locale(refused, &locale);
	kp_t_ok(T, strstr(refused->speech_problem, "sound count") != NULL, "%s", refused->speech_problem);
	kp_t_eq_str(T, refused->locale_problem, "", "not the language's");
	kp_t_eq(T, durations_apart(own, refused), 0, "the base's durations");
	sfx = kp_ui_data_speech_sounds(&kp_t_data(T)->ui, &raw, "xxXX", &locale.speech_size, &voices, &e);
	kp_t_defer(T, free, sfx);
	kp_t_ok(T, sfx != NULL, "the base's records");
	kp_t_eq(T, voices, 0, "a language the data lacks has no voices");
}

/* Every play the original made with the speech: Koprulu with the speech equals it at every 20th step by the state's
 * fingerprint, every 500th by its categories and in the traces, and it ends in the local player's victory. The plays
 * are the base's, so every campaign mission is played. */
static void campaign_plays_with_the_speech_equal_the_original(KpT *T)
{
	const KpJson *index, *base;
	const unsigned char *mine;
	KpBuf *bad;
	KpOracleOpts o;
	const char *speech_data;
	int k, missions = 0;
	char seen[CAMPAIGN_MISSIONS][64];
	if (!kp_t_ready(T, "test_speech", FILES)) return;
	speech_data = scr(T);
	index = kp_t_golden(T, PLAYS_SPEECH "index.json");
	base = kp_t_golden(T, PLAYS "index.json");
	mine = kp_t_mine_goldens(T, index, PLAYS_SPEECH "%s.json");
	bad = kp_t_buf(T);
	kp_oracle_opts(&o);
	o.speech = SPEECH;
	o.speech_data = speech_data;
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
		if (!mine[k]) {
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

/* A golden play's game with the install's speech or without, run to step `to` by its own log. */
static KpGoldenRun *run_to(KpT *T, const KpJson *g, const char *speech, int64_t to)
{
	KpOracleOpts o;
	KpGoldenRun *gr;
	kp_oracle_opts(&o);
	o.speech = speech;
	o.speech_data = scr(T);
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
	{ "the_speech_equals_the_converters", the_speech_equals_the_converters, 0 },
	{ "the_speech_merges_the_maps_time", the_speech_merges_the_maps_time, 0 },
	{ "the_speech_sound_records_replace_the_bases", the_speech_sound_records_replace_the_bases, 0 },
	{ "campaign_plays_with_the_speech_equal_the_original", campaign_plays_with_the_speech_equal_the_original, 12 },
	{ "a_save_keeps_its_speech", a_save_keeps_its_speech, 0 },
};

KP_SUITE(kp_suite_speech, "test_speech", TESTS);
