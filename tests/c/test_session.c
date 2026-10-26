/* The product's session (native/kp_app/kp_session.h; plan phase S, stage S2): the clock, the saves and loads on a
 * scenario map (the save container with the queued commands, the campaign and the presentation's state), the atomic
 * save file and the profile. */
#include "kp_test.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "kp_session.h"
#include "kp_ui.h"

#define MAP "generated/scenarios/marine_group.kpm"
#define SEED 0x12345678u

/* --- the clock --- */

static KpClock clock_new(int speed, int max_catchup)
{
	KpClock c;
	kp_clock_init(&c, speed, max_catchup);
	return c;
}

static void clock_fastest_steps_at_42ms(KpT *T)
{
	KpClock c = clock_new(0, 0);
	double a;
	int total = 0, k;
	for (k = 0; k < 600; k++) total += kp_clock_advance(&c, 1.0 / 60, &a); /* 10 s at 60 Hz */
	kp_t_eq(T, total, 10000 / 42, NULL);
	kp_t_ok(T, c.dropped_ms == 0, "nothing dropped");
}

static void clock_speed_table(KpT *T)
{
	KpClock c = clock_new(1, 0);
	kp_t_eq(T, KP_CLOCK_SPEEDS, 7, NULL);
	kp_t_eq(T, KP_CLOCK_SPEED_MS[0], 167, NULL);
	kp_t_eq(T, KP_CLOCK_SPEED_MS[6], 42, NULL);
	kp_t_eq(T, kp_clock_step_ms(&c), 167, NULL);
	kp_t_eq(T, kp_clock_set_speed(&c, 8), -1, "unknown game speed");
}

static void clock_interpolation_fraction(KpT *T)
{
	KpClock c = clock_new(0, 0);
	double a;
	kp_t_eq(T, kp_clock_advance(&c, 0.021, &a), 0, NULL);
	kp_t_ok(T, fabs(a - 0.5) < 1e-9, "half a step");
	kp_t_eq(T, kp_clock_advance(&c, 0.042, &a), 1, NULL);
	kp_t_ok(T, fabs(a - 0.5) < 1e-9, "half a step again");
}

static void clock_catch_up_is_bounded(KpT *T)
{
	KpClock c = clock_new(0, 4);
	double a;
	kp_t_eq(T, kp_clock_advance(&c, 2.0, &a), 4, "a 2 s stall");
	kp_t_ok(T, c.accumulator_ms < 42, "backlog beyond the limit is dropped");
	kp_t_ok(T, c.dropped_ms > 0, "dropped");
	kp_t_eq(T, kp_clock_advance(&c, 0, &a), 0, NULL);
}

static void clock_suspend_discards_background_time(KpT *T)
{
	KpClock c = clock_new(0, 0);
	double a;
	kp_clock_advance(&c, 0.030, &a);
	kp_clock_pause(&c);
	kp_t_eq(T, kp_clock_advance(&c, 30, &a), 0, "frames while suspended do nothing");
	kp_clock_resume(&c);
	kp_t_ok(T, c.accumulator_ms == 0, "the accumulator is reset");
	kp_t_eq(T, kp_clock_advance(&c, 0.041, &a), 0, NULL);
	kp_t_eq(T, kp_clock_advance(&c, 0.001, &a), 1, NULL);
	kp_t_eq(T, c.resets, 1, NULL);
}

static void clock_wall_reference_resets_on_resume(KpT *T)
{
	KpClock c = clock_new(0, 0);
	double a;
	kp_t_eq(T, kp_clock_advance_wall(&c, 100.0, &a), 0, "the first call only sets the reference");
	kp_t_eq(T, kp_clock_advance_wall(&c, 100.042, &a), 1, NULL);
	kp_clock_pause(&c);
	kp_t_eq(T, kp_clock_advance_wall(&c, 103.0, &a), 0, NULL);
	kp_clock_resume(&c);
	kp_t_eq(T, kp_clock_advance_wall(&c, 103.5, &a), 0, "the 3 s in the background are not replayed");
	kp_t_eq(T, kp_clock_advance_wall(&c, 103.542, &a), 1, NULL);
	kp_t_ok(T, c.dropped_ms == 0, "nothing dropped");
}

static void clock_window_events(KpT *T)
{
	KpClock c = clock_new(0, 0);
	double a;
	kp_clock_on_window(&c, KP_WINDOW_FOCUS_LOST, 0);
	kp_t_ok(T, !kp_clock_stopped(&c), "a desktop keeps running without the focus");
	kp_clock_on_window(&c, KP_WINDOW_ICONIFIED, 0);
	kp_t_ok(T, kp_clock_stopped(&c), "iconified");
	kp_t_eq(T, kp_clock_advance(&c, 1, &a), 0, "no steps while iconified");
	kp_clock_on_window(&c, KP_WINDOW_DEICONIFIED, 0);
	kp_t_ok(T, !kp_clock_stopped(&c) && c.resets == 1, "back, the time away discarded");
	kp_clock_on_window(&c, KP_WINDOW_FOCUS_GAINED, 0);
	kp_t_eq(T, c.resets, 1, "a running clock is not reset");
	/* the game's pause outlives the window's events */
	kp_clock_pause(&c);
	kp_clock_on_window(&c, KP_WINDOW_ICONIFIED, 0);
	kp_clock_on_window(&c, KP_WINDOW_DEICONIFIED, 0);
	kp_clock_on_window(&c, KP_WINDOW_FOCUS_GAINED, 0);
	kp_t_ok(T, kp_clock_stopped(&c), "a pause stays paused when the window returns");
	kp_clock_resume(&c);
	kp_t_ok(T, !kp_clock_stopped(&c), "resumed");
	kp_clock_on_window(&c, KP_WINDOW_FOCUS_LOST, 1);
	kp_t_ok(T, c.paused, "autopause");
	kp_clock_on_window(&c, KP_WINDOW_FOCUS_GAINED, 1);
	kp_t_ok(T, c.paused, "the autopause lasts until the owner resumes");
}

static void clock_negative_dt_ignored(KpT *T)
{
	KpClock c = clock_new(0, 0);
	double a;
	kp_t_eq(T, kp_clock_advance(&c, -1, &a), 0, NULL);
	kp_t_ok(T, c.accumulator_ms == 0, "no time");
}

/* --- the session over a real game --- */

static const char *const FILES[KP_HOST_FILES] = { "generated/data/dat.kpd", "generated/data/grpmeta.kpd",
	"generated/data/iscript.kpd", MAP, NULL, "generated/data/aiscript.kpd" };

static int ready(KpT *T)
{
	static const char *const NEED[] = { "generated/data/dat.kpd", MAP, NULL };
	return kp_t_ready(T, "test_session", NEED);
}

static void free_session(void *p)
{
	kp_session_free((KpSession *)p);
	free(p);
}

/* A session of the map with the tileset its mission names; name: the mission's name (NULL: the map's path). */
static KpSession *session(KpT *T, const char *name)
{
	KpSession *s = (KpSession *)calloc(1, sizeof *s);
	KpSessionStart start;
	uint8_t *files[KP_HOST_FILES];
	size_t sizes[KP_HOST_FILES];
	char tileset[128];
	KpMission m;
	KpError err;
	int k;
	if (!s) kp_t_fail(T, "out of memory");
	kp_t_defer(T, free_session, s);
	memset(files, 0, sizeof files);
	memset(sizes, 0, sizeof sizes);
	for (k = 0; k < KP_HOST_FILES; k++) {
		if (FILES[k] && !(files[k] = kp_read_file(FILES[k], &sizes[k]))) kp_t_fail(T, "no %s", FILES[k]);
	}
	if (kp_mission_load(&m, files[KP_HOST_MISSION], sizes[KP_HOST_MISSION], MAP, NULL, &err) != 0) {
		kp_t_fail(T, "%s", err.msg);
	}
	snprintf(tileset, sizeof tileset, KP_TILESET_PATH, KP_TILESET_NAMES[m.tileset]);
	kp_mission_free(&m);
	if (!(files[KP_HOST_TILESET] = kp_read_file(tileset, &sizes[KP_HOST_TILESET]))) kp_t_fail(T, "no %s", tileset);
	kp_session_start_defaults(&start);
	start.name = name ? name : MAP;
	start.seed = SEED;
	start.mission_index = kp_oracle_mission_index(".", MAP);
	if (kp_session_start(s, files, sizes, &start) != 0) kp_t_fail(T, "the game does not start: %s", s->error);
	kp_t_guard(T, s->g);
	return s;
}

static void step(KpT *T, KpSession *s, int n)
{
	int k;
	for (k = 0; k < n; k++) kp_t_ok(T, kp_session_step(s, NULL), "step: %s", s->error);
}

/* The Marines of the scenario walk away: a select command of the first unit and a right click. */
static void order(KpT *T, KpSession *s)
{
	static const uint8_t RIGHT_CLICK[10] = { 0x14, 0x14, 0x05, 0x56, 0x04, 0, 0, 0xe4, 0, 0 }; /* to (1300, 1110) */
	int64_t units[1];
	uint16_t ref;
	uint8_t select[4];
	kp_t_ok(T, kp_t_list_units(s->g, KP_LIST_ACTIVE, units, 1) == 1, "a unit");
	ref = kp_ui_unit_ref(s->g, units[0]);
	select[0] = 0x09;
	select[1] = 1;
	select[2] = (uint8_t)(ref & 0xff);
	select[3] = (uint8_t)(ref >> 8);
	kp_session_command(s, select, sizeof select, -1);
	step(T, s, 1);
	kp_session_command(s, RIGHT_CLICK, sizeof RIGHT_CLICK, -1);
}

static void crcs(KpT *T, KpSession *s, KpStateCrcs *out)
{
	kp_t_ok(T, kp_state_crcs(s->g, 0, out) == 0, "state CRCs");
}

static void same_crcs(KpT *T, const KpStateCrcs *a, const KpStateCrcs *b, const char *what)
{
	int k, found;
	kp_t_eq(T, a->count, b->count, "%s: categories", what);
	for (k = 0; k < a->count; k++) {
		kp_t_eq(T, kp_state_crc(b, a->names[k], &found), a->crcs[k], "%s: %s", what, a->names[k]);
	}
}

static KpSaveIdentity identity(KpT *T)
{
	KpSaveIdentity id;
	KpError err;
	size_t size;
	uint8_t *dat = kp_t_read(T, "generated/data/dat.kpd", &size);
	if (kp_save_identity(&id, dat, size, &err) != 0) kp_t_fail(T, "%s", err.msg);
	return id;
}

static void session_speed_and_commands(KpT *T)
{
	KpSession *s;
	uint8_t bytes[2] = { 0x09, 0 };
	if (!ready(T)) return;
	s = session(T, NULL);
	kp_t_eq(T, s->tick, 0, NULL);
	kp_t_eq(T, kp_scalar(s->g, KP_SPEED), 6, "the game's speed byte counts from 0 (Fastest)");
	kp_session_set_speed(s, 3);
	kp_t_eq(T, s->clock.speed, 3, NULL);
	kp_t_eq(T, kp_scalar(s->g, KP_SPEED), 2, NULL);
	kp_t_eq(T, kp_session_command(s, bytes, sizeof bytes, -1), 0, NULL);
	kp_t_eq(T, s->pending[0].player, s->g->local_player, "the local player's");
	step(T, s, 1);
	kp_t_eq(T, s->pending_count, 0, "sent once");
	kp_t_eq(T, s->tick, 1, NULL);
}

static void session_wall_clock_runs_the_steps(KpT *T)
{
	KpSession *s;
	double a;
	if (!ready(T)) return;
	s = session(T, NULL);
	kp_t_eq(T, kp_session_advance(s, 100, &a, NULL), 0, "the first call sets the reference");
	kp_t_eq(T, kp_session_advance(s, 100 + 3.5 * 0.042, &a, NULL), 3, NULL);
	kp_t_ok(T, fabs(a - 0.5) < 1e-6, "the fraction");
	kp_t_eq(T, s->tick, 3, NULL);
	kp_t_eq(T, kp_session_advance(s, 200, &a, NULL), s->clock.max_catchup, "the catch-up limit");
	kp_session_pause(s);
	kp_t_eq(T, kp_session_advance(s, 300, &a, NULL), 0, "paused");
	kp_session_resume(s);
	kp_t_eq(T, kp_session_advance(s, 400, &a, NULL), 0, "the time away is dropped");
	kp_t_eq(T, kp_session_advance(s, 400 + 1.5 * 0.042, &a, NULL), 1, NULL);
}

static void session_a_loaded_save_continues_as_the_saved_game(KpT *T)
{
	KpSession *m, *other, *foreign;
	KpSaveIdentity id;
	KpStateCrcs expected, got, before;
	KpError err;
	uint8_t *bytes, *again, *mine;
	size_t size, again_size, mine_size;
	int64_t tick;
	KpImages *world;
	uint8_t empty[2] = { 0x09, 0 };
	if (!ready(T)) return;
	id = identity(T);
	m = session(T, NULL);
	order(T, m);
	step(T, m, 20);
	kp_t_ok(T, kp_save_encode(m, &id, NULL, NULL, &bytes, &size, &err) == 0, "%s", err.msg);
	kp_t_defer(T, free, bytes);
	tick = m->tick;
	step(T, m, 40);
	crcs(T, m, &expected);
	/* the save goes into a game that has run on: the same game and world objects take it */
	other = session(T, NULL);
	step(T, other, 70);
	kp_session_set_speed(other, 3);
	world = other->g->w;
	kp_session_command(other, empty, sizeof empty, -1);
	kp_t_ok(T, kp_save_load(other, bytes, size, &id, NULL, NULL, &err) == 0, "%s", err.msg);
	kp_t_ok(T, other->g->w == world, "the world stays, for the presentation");
	kp_t_eq(T, other->tick, tick, "the saved tick");
	kp_t_eq(T, kp_scalar(other->g, KP_SPEED), 2, "the player's speed, not the saved game's");
	kp_t_eq(T, other->loads, 1, NULL);
	kp_session_set_speed(other, m->clock.speed);
	kp_t_eq(T, other->pending_count, 0, "the queued commands are the save's (none)");
	step(T, other, 40);
	crcs(T, other, &got);
	same_crcs(T, &expected, &got, "continued");
	kp_t_ok(T, kp_save_encode(other, &id, NULL, NULL, &again, &again_size, &err) == 0, "%s", err.msg);
	kp_t_defer(T, free, again);
	kp_t_ok(T, kp_save_encode(m, &id, NULL, NULL, &mine, &mine_size, &err) == 0, "%s", err.msg);
	kp_t_defer(T, free, mine);
	kp_t_ok(T, again_size == mine_size && memcmp(again, mine, mine_size) == 0, "the same save");
	/* refusals leave the game */
	crcs(T, other, &before);
	foreign = session(T, "another mission");
	kp_t_ok(T, kp_save_load(foreign, bytes, size, &id, NULL, NULL, &err) != 0, "another mission's save is refused");
	kp_t_ok(T, kp_save_load(other, bytes, 100, &id, NULL, NULL, &err) != 0, "a truncated save is refused");
	crcs(T, other, &got);
	same_crcs(T, &before, &got, "unchanged");
}

/* A game save keeps the commands queued for the next step, the campaign and the presentation's state; its load runs
 * on with them as the saved game would have, and refuses a save of another data set before the game changes. */
static void session_a_game_save_keeps_the_queued_commands_and_the_campaign(KpT *T)
{
	KpSession *m, *other;
	KpSaveIdentity id, wrong;
	KpSaveCampaign camp, got;
	KpSavePresentation pres, got_pres;
	KpStateCrcs expected, after, before;
	KpError err;
	uint8_t *bytes, *a, *b;
	size_t size, as, bs;
	char mission[256];
	if (!ready(T)) return;
	id = identity(T);
	m = session(T, NULL);
	order(T, m); /* the right click is queued, not run */
	memset(&camp, 0, sizeof camp);
	camp.present = 1;
	camp.run.expansion = 1;
	camp.run.row.table = 4; /* table 5 counted from 1 */
	camp.run.row.index = 2; /* index 3 counted from 1 */
	camp.run.mission = 42;
	snprintf(camp.run.pending, sizeof camp.run.pending, "Terran05a.scx");
	camp.progress[1] = 12;
	camp.progress[4] = 43;
	memset(&pres, 0, sizeof pres);
	kp_save_pres_set(&pres, "portrait_ms", 1500);
	kp_t_ok(T, kp_save_encode(m, &id, &camp, &pres, &bytes, &size, &err) == 0, "%s", err.msg);
	kp_t_defer(T, free, bytes);
	kp_t_ok(T, kp_save_mission(bytes, size, &id, mission, sizeof mission, &got, &err) == 0, "%s", err.msg);
	kp_t_eq_str(T, mission, MAP, "the save names its mission");
	kp_t_eq(T, got.run.mission, 42, NULL);
	step(T, m, 30);
	crcs(T, m, &expected);
	other = session(T, NULL);
	step(T, other, 9);
	kp_t_ok(T, kp_save_load(other, bytes, size, &id, &got, &got_pres, &err) == 0, "%s", err.msg);
	kp_t_eq(T, kp_save_pres_get(&got_pres, "portrait_ms", -1), 1500, "the presentation's state");
	kp_t_eq(T, other->pending_count, 1, "the queued command");
	kp_t_eq(T, got.run.mission, 42, NULL);
	kp_t_eq(T, got.run.row.table, 4, NULL);
	kp_t_eq(T, got.run.row.index, 2, NULL);
	kp_t_eq_str(T, got.run.pending, "Terran05a.scx", NULL);
	kp_t_eq(T, got.progress[4], 43, NULL);
	kp_t_eq(T, got.progress[1], 12, NULL);
	step(T, other, 30);
	crcs(T, other, &after);
	same_crcs(T, &expected, &after, "continued");
	kp_t_ok(T, kp_save_encode(other, &id, &got, NULL, &a, &as, &err) == 0, "%s", err.msg);
	kp_t_defer(T, free, a);
	kp_t_ok(T, kp_save_encode(m, &id, &got, NULL, &b, &bs, &err) == 0, "%s", err.msg);
	kp_t_defer(T, free, b);
	kp_t_ok(T, as == bs && memcmp(a, b, as) == 0, "the same save again");
	crcs(T, other, &before);
	wrong = id;
	memset(wrong.source_hash, 'x', sizeof wrong.source_hash);
	kp_t_ok(T, kp_save_load(other, bytes, size, &wrong, NULL, NULL, &err) != 0, "a save of another data set is refused");
	memcpy(bytes + size - 8, "damaged!", 8);
	kp_t_ok(T, kp_save_load(other, bytes, size, &id, NULL, NULL, &err) != 0, "a damaged save is refused");
	crcs(T, other, &after);
	same_crcs(T, &before, &after, "unchanged");
}

/* --- the save file and the profile --- */

static void session_a_save_file_is_written_whole(KpT *T)
{
	char path[256];
	KpError err;
	uint8_t *data;
	size_t size;
	snprintf(path, sizeof path, "build/session_save_%s.kpsave", T->test);
	kp_t_eq(T, kp_write_file_atomic(path, (const uint8_t *)"first", 5, &err), 0, "%s", err.msg);
	kp_t_eq(T, kp_write_file_atomic(path, (const uint8_t *)"second", 6, &err), 0, "%s", err.msg);
	data = kp_read_file(path, &size);
	kp_t_ok(T, data && size == 6 && memcmp(data, "second", 6) == 0, "replaced");
	free(data);
	kp_t_ok(T, kp_write_file_atomic("build/no/such/dir/file", (const uint8_t *)"x", 1, &err) != 0, "refused");
	kp_t_ok(T, err.msg[0] != 0, "the reason");
	data = kp_read_file(path, &size);
	kp_t_ok(T, data != NULL, "the old file stays");
	free(data);
	remove(path);
}

static void session_profile_round_trip(KpT *T)
{
	KpProfile p, q;
	KpError err;
	uint8_t *bytes;
	size_t size;
	char file[128];
	kp_profile_init(&p);
	snprintf(p.name, sizeof p.name, "Raynor");
	p.progress.original[1] = 7;
	p.progress.expansion[2] = 44;
	kp_profile_add_save(&p, "First");
	kp_profile_add_save(&p, "Second");
	kp_profile_add_save(&p, "first"); /* names compare without case */
	kp_t_eq(T, p.save_count, 2, NULL);
	kp_t_eq_str(T, p.saves[0], "Second", "newest first");
	kp_t_ok(T, kp_profile_encode(&p, &bytes, &size) == 0, "encoded");
	kp_t_ok(T, kp_profile_decode(&q, bytes, size, &err) == 0, "%s", err.msg);
	free(bytes);
	kp_t_eq_str(T, q.name, "Raynor", NULL);
	kp_t_eq(T, q.progress.original[1], 7, NULL);
	kp_t_eq(T, q.progress.expansion[2], 44, NULL);
	kp_t_eq(T, q.save_count, 2, NULL);
	kp_t_eq_str(T, q.saves[1], "First", NULL);
	kp_profile_remove_save(&q, "SECOND");
	kp_t_eq(T, q.save_count, 1, NULL);
	kp_t_ok(T, kp_profile_decode(&q, (const uint8_t *)"junk", 4, &err) != 0, "a damaged profile");
	kp_t_eq(T, q.save_count, 0, "a new profile");
	kp_save_file_name("My Save!", file, sizeof file);
	kp_t_eq_str(T, file, "save_my_20save_21.kpsave", "each byte but a letter or digit as _XX");
}

static const KpTest TESTS[] = {
	{ "clock_fastest_steps_at_42ms", clock_fastest_steps_at_42ms, 0 },
	{ "clock_speed_table", clock_speed_table, 0 },
	{ "clock_interpolation_fraction", clock_interpolation_fraction, 0 },
	{ "clock_catch_up_is_bounded", clock_catch_up_is_bounded, 0 },
	{ "clock_suspend_discards_background_time", clock_suspend_discards_background_time, 0 },
	{ "clock_wall_reference_resets_on_resume", clock_wall_reference_resets_on_resume, 0 },
	{ "clock_window_events", clock_window_events, 0 },
	{ "clock_negative_dt_ignored", clock_negative_dt_ignored, 0 },
	{ "session_speed_and_commands", session_speed_and_commands, 0 },
	{ "session_wall_clock_runs_the_steps", session_wall_clock_runs_the_steps, 0 },
	{ "session_a_loaded_save_continues_as_the_saved_game", session_a_loaded_save_continues_as_the_saved_game, 0 },
	{ "session_a_game_save_keeps_the_queued_commands_and_the_campaign",
		session_a_game_save_keeps_the_queued_commands_and_the_campaign, 0 },
	{ "session_a_save_file_is_written_whole", session_a_save_file_is_written_whole, 0 },
	{ "session_profile_round_trip", session_profile_round_trip, 0 },
};

KP_SUITE(kp_suite_session, "test_session", TESTS);
