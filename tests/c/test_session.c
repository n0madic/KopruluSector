/* The product's session (native/kp_app/kp_session.h; plan phase S, stage S2): the clock, the saves and loads on a
 * scenario map (the save container with the queued commands, the campaign and the presentation's state), the atomic
 * save file and the profile. */
#include "kp_test_app.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "kp_kpf.h"
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

static void free_session(void *p);

/* A loaded session of the map (not started); name: the mission's name (NULL: the host's default). */
static KpSession *loaded(KpT *T, const char *name)
{
	KpSession *s = (KpSession *)calloc(1, sizeof *s);
	const uint8_t *dat;
	uint8_t *mission;
	size_t dat_size, mission_size;
	if (!s) kp_t_fail(T, "out of memory");
	kp_t_defer(T, free_session, s);
	dat = kp_t_read(T, "generated/data/dat.kpd", &dat_size);
	if (!(mission = kp_read_file(MAP, &mission_size))) kp_t_fail(T, "no %s", MAP);
	if (kp_session_load(s, kp_t_shared(T), dat, dat_size, mission, mission_size, name) != 0) {
		kp_t_fail(T, "the game does not load: %s", s->error);
	}
	return s;
}

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
	KpSession *s = loaded(T, name ? name : MAP);
	KpSessionStart start;
	char tileset[128];
	uint8_t *tileset_bytes;
	size_t tileset_size;
	kp_host_tileset_path(&s->host, tileset, sizeof tileset);
	if (!(tileset_bytes = kp_read_file(tileset, &tileset_size))) kp_t_fail(T, "no %s", tileset);
	kp_session_start_defaults(&start);
	start.seed = SEED;
	start.mission_index = kp_oracle_mission_index(".", MAP);
	if (kp_session_start(s, tileset_bytes, tileset_size, &start) != 0) kp_t_fail(T, "the game does not start: %s", s->error);
	kp_t_guard(T, s->g);
	return s;
}

/* A mission file that does not load fails the load with the loader's reason: nothing of the game stands. */
static void session_a_damaged_mission_does_not_load(KpT *T)
{
	KpSession s;
	const uint8_t *dat;
	uint8_t *mission = (uint8_t *)calloc(1, 16);
	size_t dat_size;
	if (!ready(T)) return;
	if (!mission) kp_t_fail(T, "out of memory");
	dat = kp_t_read(T, "generated/data/dat.kpd", &dat_size);
	kp_t_eq(T, kp_session_load(&s, kp_t_shared(T), dat, dat_size, mission, 16, MAP), -1, NULL);
	kp_t_ok(T, s.failed && s.error[0], "the reason: %s", s.error);
	kp_t_ok(T, !s.g && !kp_session_mission(&s), "no game");
	kp_session_free(&s);
}

/* A start without its tileset fails and releases the loaded data; the mission's name defaults when none is given. */
static void session_a_start_without_its_tileset_fails(KpT *T)
{
	KpSession *s;
	KpSessionStart start;
	size_t size;
	if (!ready(T)) return;
	s = loaded(T, NULL);
	kp_t_eq_str(T, s->host.name, "mission", NULL);
	kp_session_start_defaults(&start);
	kp_t_eq(T, kp_session_start(s, NULL, 0, &start), -1, NULL);
	kp_t_ok(T, s->failed && strstr(s->error, "no tileset") != NULL, "the reason: %s", s->error);
	kp_t_ok(T, !s->g && !kp_session_mission(s) && !kp_session_grp(s) && !kp_session_tileset(s, &size) && size == 0,
		"the data released");
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

static void fail_in_the_game(void *ud) { kp_game_fail((KpGame *)ud, "test: a failure in the console's input"); }

static void reach_the_game(void *ud) { (*(int *)ud)++; }

/* A failure under kp_session_guard (the console's input, kp_app_input.c) stops the game as a step's does: the input
 * of a stopped game still runs (its menus end it), and no more steps do. */
static void session_guard_stops_the_game(KpT *T)
{
	KpSession *s;
	int reached = 0;
	if (!ready(T)) return;
	s = session(T, NULL);
	kp_t_eq(T, kp_session_guard(s, reach_the_game, &reached), 0, NULL);
	kp_t_eq(T, reached, 1, "run");
	kp_t_eq(T, kp_session_guard(s, fail_in_the_game, s->g), -1, "the failure returns");
	kp_t_ok(T, s->failed && strstr(s->error, "console's input") != NULL, "the session stopped: %s", s->error);
	kp_t_eq(T, kp_session_step(s, NULL), 0, "no more steps");
	kp_t_eq(T, kp_session_guard(s, reach_the_game, &reached), 0, "the input of a stopped game still runs");
	kp_t_eq(T, reached, 2, NULL);
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
	p.character_count = 2;
	snprintf(p.characters[0].name, sizeof p.characters[0].name, "Kerrigan");
	p.characters[0].time = 3;
	p.characters[0].progress.original[0] = 2;
	snprintf(p.characters[1].name, sizeof p.characters[1].name, "Raynor");
	p.characters[1].time = 5;
	snprintf(p.name, sizeof p.name, "raynor"); /* the name compared without case */
	kp_profile_progress(&p)->original[1] = 7;
	kp_profile_progress(&p)->expansion[2] = 44;
	kp_t_eq(T, p.characters[1].progress.original[1], 7, "the logged-in ID's progress");
	kp_profile_add_save(&p, "First");
	kp_profile_add_save(&p, "Second");
	kp_profile_add_save(&p, "first"); /* names compare without case */
	kp_t_eq(T, p.save_count, 2, NULL);
	kp_t_eq_str(T, p.saves[0].name, "Second", "newest first");
	kp_t_eq_str(T, p.saves[0].owner, "raynor", "the logged-in ID's");
	snprintf(p.name, sizeof p.name, "Kerrigan");
	kp_profile_add_save(&p, "First"); /* another ID's of the same name */
	kp_t_eq(T, p.save_count, 3, NULL);
	snprintf(p.name, sizeof p.name, "raynor");
	kp_t_ok(T, kp_profile_encode(&p, &bytes, &size) == 0, "encoded");
	kp_t_ok(T, kp_profile_decode(&q, bytes, size, &err) == 0, "%s", err.msg);
	free(bytes);
	kp_t_eq_str(T, q.name, "raynor", NULL);
	kp_t_eq(T, q.character_count, 2, "both IDs");
	kp_t_eq_str(T, q.characters[0].name, "Kerrigan", "in their order");
	kp_t_eq(T, q.characters[0].time, 3, NULL);
	kp_t_eq(T, q.characters[0].progress.original[0], 2, "each its own progress");
	kp_t_eq(T, q.characters[0].progress.original[1], 0, NULL);
	kp_t_eq(T, q.characters[1].time, 5, NULL);
	kp_t_eq(T, kp_profile_progress(&q)->original[1], 7, NULL);
	kp_t_eq(T, kp_profile_progress(&q)->expansion[2], 44, NULL);
	kp_t_eq(T, q.save_count, 3, NULL);
	kp_t_eq_str(T, q.saves[0].owner, "Kerrigan", "each save its ID");
	kp_t_eq_str(T, q.saves[2].name, "First", NULL);
	kp_t_eq(T, q.old_save_files, 0, "schema 3's files have their names");
	kp_profile_remove_save(&q, "FIRST"); /* the logged-in ID's only */
	kp_t_eq(T, q.save_count, 2, NULL);
	kp_t_eq_str(T, q.saves[0].owner, "Kerrigan", "the other ID's stays");
	kp_t_ok(T, kp_profile_decode(&q, (const uint8_t *)"junk", 4, &err) != 0, "a damaged profile");
	kp_t_eq(T, q.save_count, 0, "a new profile");
	kp_save_file_name("Kerrigan", "My Save!", file, sizeof file);
	kp_t_eq_str(T, file, "save_kerrigan-my_20save_21.kpsave", "each byte but a letter or digit as _XX");
	kp_save_file_name("", "a", file, sizeof file);
	kp_t_eq_str(T, file, "save_-a.kpsave", "without a login");
	kp_save_old_file_name("My Save!", file, sizeof file);
	kp_t_eq_str(T, file, "save_my_20save_21.kpsave", "before schema 3");
}

/* The video options (VOPT) come back; a profile from before the section has the defaults, one from before the lighting
 * its Auto; a value out of its range reads as its default. */
static void session_profile_video_options(KpT *T)
{
	static const uint8_t PROG[8 * KP_CAMPAIGN_RACES] = { 0, 0, 0, 0, 9 }; /* the original's Terran at 9 */
	static const uint8_t SAVES[2] = { 0, 0 };
	static const uint8_t VOPT6[12] = { 125, 0, 1, 0, 1, 0, 0, 0, 0, 0, 1, 0 }; /* the six values before the lighting */
	KpProfile p, q;
	KpKpfOut old[4];
	KpError err;
	uint8_t *bytes;
	size_t size;
	int k;
	kp_profile_init(&p);
	kp_t_eq(T, p.video.gamma, KP_UI_GAMMA_DEFAULT, "a new profile's gamma");
	kp_t_eq(T, p.video.portraits, KP_UI_PORTRAITS_ANIMATED, NULL);
	kp_t_eq(T, p.video.graphics, KP_VIDEO_AUTO, NULL);
	p.video.gamma = 125;
	p.video.portraits = KP_UI_PORTRAITS_STILL;
	p.video.graphics = KP_GRAPHICS_HD2;
	p.video.fog = KP_FOG_ORIGINAL;
	p.video.water = KP_WATER_STILL;
	p.video.font = KP_VTEXT_VECTOR;
	p.video.lighting = KP_LIGHTING_OFF;
	kp_t_ok(T, kp_profile_encode(&p, &bytes, &size) == 0, "encoded");
	kp_t_ok(T, kp_profile_decode(&q, bytes, size, &err) == 0, "%s", err.msg);
	free(bytes);
	kp_t_ok(T, memcmp(&q.video, &p.video, sizeof p.video) == 0, "the video options come back");
	/* the profile of the build before VOPT: NAME, PROG and SAVE only */
	memset(old, 0, sizeof old);
	old[0].fourcc = "NAME";
	old[0].payload = (const uint8_t *)"Kerrigan";
	old[0].size = 8;
	old[1].fourcc = "PROG";
	old[1].payload = PROG;
	old[1].size = sizeof PROG;
	old[2].fourcc = "SAVE";
	old[2].payload = SAVES;
	old[2].size = sizeof SAVES;
	for (k = 0; k < 3; k++) old[k].schema_version = 1;
	kp_t_ok(T, kp_kpf_build("PROF", 1, 0, "koprulu", NULL, old, 3, &bytes, &size) == 0, "an older profile");
	kp_t_ok(T, kp_profile_decode(&q, bytes, size, &err) == 0, "%s", err.msg);
	free(bytes);
	kp_t_eq_str(T, q.name, "Kerrigan", NULL);
	kp_t_eq(T, q.character_count, 1, "schema 1's one ID");
	kp_t_eq_str(T, q.characters[0].name, "Kerrigan", NULL);
	kp_t_eq(T, kp_profile_progress(&q)->original[1], 9, "its progress kept");
	kp_t_eq(T, q.video.gamma, KP_UI_GAMMA_DEFAULT, "its gamma the default");
	kp_t_eq(T, q.video.font, KP_VIDEO_AUTO, "its text auto");
	/* the profile of the build before the lighting: VOPT of six values */
	old[3].fourcc = "VOPT";
	old[3].payload = VOPT6;
	old[3].size = sizeof VOPT6;
	old[3].schema_version = 1;
	kp_t_ok(T, kp_kpf_build("PROF", 1, 0, "koprulu", NULL, old, 4, &bytes, &size) == 0, "a profile before the lighting");
	kp_t_ok(T, kp_profile_decode(&q, bytes, size, &err) == 0, "%s", err.msg);
	free(bytes);
	kp_t_eq(T, q.video.gamma, 125, "its gamma");
	kp_t_eq(T, q.video.font, KP_VTEXT_VECTOR, "its text");
	kp_t_eq(T, q.video.lighting, KP_VIDEO_AUTO, "its lighting auto");
	/* values out of range: their defaults */
	p.video.gamma = 20;
	p.video.graphics = 9;
	p.video.portraits = 7;
	kp_t_ok(T, kp_profile_encode(&p, &bytes, &size) == 0, "encoded");
	kp_t_ok(T, kp_profile_decode(&q, bytes, size, &err) == 0, "%s", err.msg);
	free(bytes);
	kp_t_eq(T, q.video.gamma, KP_UI_GAMMA_DEFAULT, "a gamma out of range");
	kp_t_eq(T, q.video.graphics, KP_VIDEO_AUTO, "an art out of range");
	kp_t_eq(T, q.video.portraits, KP_UI_PORTRAITS_ANIMATED, "portraits out of range");
	kp_t_eq(T, q.video.water, KP_WATER_STILL, "the others kept");
}

/* The options (OPTS, the original's word 0x6d3148): a new profile plays the first start's intro, the word comes back as
 * it was kept, and a profile before schema 4 (without the section) plays the intro once after the update. */
static void session_profile_options(KpT *T)
{
	static const uint8_t CHARS[2] = { 0, 0 };
	static const uint8_t SAVES[2] = { 0, 0 };
	KpProfile p, q;
	KpKpfOut old[4];
	uint8_t opts[4];
	KpError err;
	uint8_t *bytes;
	size_t size;
	int k;
	kp_profile_init(&p);
	kp_t_eq(T, p.options, KP_UI_OPTION_EXPANSION_INTRO | KP_UI_OPTIONS_DEFAULT,
		"a new profile plays the first start's intro, the three sound options on");
	p.options = KP_UI_OPTION_SUBTITLES | 0x40000000u; /* the intro played, the sounds off; a later build's bit kept */
	kp_t_ok(T, kp_profile_encode(&p, &bytes, &size) == 0, "encoded");
	kp_t_ok(T, kp_profile_decode(&q, bytes, size, &err) == 0, "%s", err.msg);
	free(bytes);
	kp_t_eq(T, q.options, KP_UI_OPTION_SUBTITLES | 0x40000000u, "the word comes back");
	memset(old, 0, sizeof old);
	old[0].fourcc = "NAME";
	old[0].payload = (const uint8_t *)"";
	old[1].fourcc = "CHAR";
	old[1].payload = CHARS;
	old[1].size = sizeof CHARS;
	old[2].fourcc = "SAVE";
	old[2].payload = SAVES;
	old[2].size = sizeof SAVES;
	for (k = 0; k < 3; k++) old[k].schema_version = 1;
	kp_t_ok(T, kp_kpf_build("PROF", 3, 0, "koprulu", NULL, old, 3, &bytes, &size) == 0, "a schema 3 profile");
	kp_t_ok(T, kp_profile_decode(&q, bytes, size, &err) == 0, "%s", err.msg);
	free(bytes);
	kp_t_eq(T, q.options, KP_UI_OPTION_EXPANSION_INTRO | KP_UI_OPTIONS_DEFAULT, "it plays the intro once");
	/* schema 4: OPTS with the intro's bit only (and the bit a build before this one hid the cinematics' subtitles by) */
	opts[0] = opts[1] = opts[2] = 0;
	opts[3] = 0x80;
	old[3].fourcc = "OPTS";
	old[3].payload = opts;
	old[3].size = sizeof opts;
	old[3].schema_version = 1;
	kp_t_ok(T, kp_kpf_build("PROF", 4, 0, "koprulu", NULL, old, 4, &bytes, &size) == 0, "a schema 4 profile");
	kp_t_ok(T, kp_profile_decode(&q, bytes, size, &err) == 0, "%s", err.msg);
	free(bytes);
	kp_t_eq(T, q.options, KP_UI_OPTIONS_DEFAULT, "its intro played, a new profile's check boxes");
	opts[1] = (uint8_t)(KP_UI_OPTION_EXPANSION_INTRO >> 8);
	kp_t_ok(T, kp_kpf_build("PROF", 4, 0, "koprulu", NULL, old, 4, &bytes, &size) == 0, NULL);
	kp_t_ok(T, kp_profile_decode(&q, bytes, size, &err) == 0, "%s", err.msg);
	free(bytes);
	kp_t_eq(T, q.options, KP_UI_OPTION_EXPANSION_INTRO | KP_UI_OPTIONS_DEFAULT, "its intro to play kept");
	kp_t_ok(T, kp_kpf_build("PROF", 7, 0, "koprulu", NULL, old, 3, &bytes, &size) == 0, "a later schema's profile");
	kp_t_ok(T, kp_profile_decode(&q, bytes, size, &err) != 0, "is not read");
	free(bytes);
}

/* SOPT's bytes of its seven values. */
static void sopt_bytes(uint8_t *out, const uint32_t values[7])
{
	int k;
	for (k = 0; k < 7; k++) {
		out[4 * k] = (uint8_t)values[k];
		out[4 * k + 1] = (uint8_t)(values[k] >> 8);
		out[4 * k + 2] = (uint8_t)(values[k] >> 16);
		out[4 * k + 3] = (uint8_t)(values[k] >> 24);
	}
}

/* The options of the sound and speed dialogs and the tips (SOPT, schema 6): the original's defaults in a new profile and
 * in one before the section, the round trip, a value out of the original's range alone a new profile's, a short section's
 * missing values too. */
static void session_profile_settings(KpT *T)
{
	static const uint8_t CHARS[2] = { 0, 0 };
	static const uint8_t SAVES[2] = { 0, 0 };
	static const uint32_t IN_RANGE[7] = { 100, 100, 6, 0, 6, 0xfffffffeu, 2 }; /* each range's ends */
	static const uint32_t OUT_OF_RANGE[7] = { 101, 101, 7, 7, 7, 1, 0 };
	KpProfile p, q;
	KpKpfOut old[4];
	uint8_t values[4 * 7];
	KpError err;
	uint8_t *bytes;
	size_t size;
	int k;
	kp_profile_init(&p);
	kp_t_eq(T, p.settings.music, 25, "0x49dfa0's music");
	kp_t_eq(T, p.settings.effects, 50, "0x49dfa0's effects");
	kp_t_eq(T, p.settings.mouse_scroll, 3, "the table's default");
	kp_t_eq(T, p.settings.key_scroll, 3, NULL);
	kp_t_eq(T, p.settings.game_speed, 4, NULL);
	kp_t_eq(T, p.settings.tip, 0, NULL);
	kp_t_eq(T, p.settings.cpu_throttle, 0, NULL);
	p.settings.music = 0;
	p.settings.effects = 100;
	p.settings.mouse_scroll = 6;
	p.settings.key_scroll = 1;
	p.settings.game_speed = 2;
	p.settings.tip = 77;
	p.settings.cpu_throttle = 1;
	kp_t_ok(T, kp_profile_encode(&p, &bytes, &size) == 0, "encoded");
	kp_t_ok(T, kp_profile_decode(&q, bytes, size, &err) == 0, "%s", err.msg);
	free(bytes);
	kp_t_eq(T, q.settings.music, 0, "the round trip");
	kp_t_eq(T, q.settings.effects, 100, NULL);
	kp_t_eq(T, q.settings.mouse_scroll, 6, NULL);
	kp_t_eq(T, q.settings.key_scroll, 1, NULL);
	kp_t_eq(T, q.settings.game_speed, 2, NULL);
	kp_t_eq(T, q.settings.tip, 77, NULL);
	kp_t_eq(T, q.settings.cpu_throttle, 1, NULL);
	memset(old, 0, sizeof old);
	old[0].fourcc = "NAME";
	old[0].payload = (const uint8_t *)"";
	old[1].fourcc = "CHAR";
	old[1].payload = CHARS;
	old[1].size = sizeof CHARS;
	old[2].fourcc = "SAVE";
	old[2].payload = SAVES;
	old[2].size = sizeof SAVES;
	old[3].fourcc = "SOPT";
	old[3].payload = values;
	old[3].size = sizeof values;
	for (k = 0; k < 4; k++) old[k].schema_version = 1;
	kp_t_ok(T, kp_kpf_build("PROF", 5, 0, "koprulu", NULL, old, 3, &bytes, &size) == 0, "a schema 5 profile");
	kp_t_ok(T, kp_profile_decode(&q, bytes, size, &err) == 0, "%s", err.msg);
	free(bytes);
	kp_profile_init(&p);
	kp_t_ok(T, memcmp(&q.settings, &p.settings, sizeof p.settings) == 0, "before the section: a new profile's");
	sopt_bytes(values, IN_RANGE);
	kp_t_ok(T, kp_kpf_build("PROF", 6, 0, "koprulu", NULL, old, 4, &bytes, &size) == 0, "each range's ends");
	kp_t_ok(T, kp_profile_decode(&q, bytes, size, &err) == 0, "%s", err.msg);
	free(bytes);
	kp_t_eq(T, q.settings.music, 100, "read");
	kp_t_eq(T, q.settings.effects, 100, NULL);
	kp_t_eq(T, q.settings.mouse_scroll, 6, NULL);
	kp_t_eq(T, q.settings.key_scroll, 0, NULL);
	kp_t_eq(T, q.settings.game_speed, 6, NULL);
	kp_t_eq(T, q.settings.tip, 0xfffffffeu, "any tip");
	kp_t_eq(T, q.settings.cpu_throttle, 1, "non-zero is on");
	sopt_bytes(values, OUT_OF_RANGE);
	kp_t_ok(T, kp_kpf_build("PROF", 6, 0, "koprulu", NULL, old, 4, &bytes, &size) == 0, "values out of range");
	kp_t_ok(T, kp_profile_decode(&q, bytes, size, &err) == 0, "%s", err.msg);
	free(bytes);
	kp_t_eq(T, q.settings.music, 25, "101: the default");
	kp_t_eq(T, q.settings.effects, 50, "101: the default");
	kp_t_eq(T, q.settings.mouse_scroll, 3, "7: the default");
	kp_t_eq(T, q.settings.key_scroll, 3, "7: the default");
	kp_t_eq(T, q.settings.game_speed, 4, "7: the default");
	kp_t_eq(T, q.settings.tip, 1, "the others read");
	kp_t_eq(T, q.settings.cpu_throttle, 0, NULL);
	sopt_bytes(values, IN_RANGE);
	old[3].size = 4 * 2;
	kp_t_ok(T, kp_kpf_build("PROF", 6, 0, "koprulu", NULL, old, 4, &bytes, &size) == 0, "a short section");
	kp_t_ok(T, kp_profile_decode(&q, bytes, size, &err) == 0, "%s", err.msg);
	free(bytes);
	kp_t_eq(T, q.settings.effects, 100, "what it has");
	kp_t_eq(T, q.settings.game_speed, 4, "the rest a new profile's");
	kp_t_eq(T, q.settings.cpu_throttle, 0, NULL);
}

/* The login list carries the IDs both ways: one deleted and a new one created start the new one with no progress, a
 * new ID of a deleted one's name too; the deleted logged-in ID leaves none; the menus' raise reaches its ID only. */
static void session_profile_login_list_deletes_and_creates(KpT *T)
{
	KpProfile *p = (KpProfile *)calloc(1, sizeof *p);
	KpUiProfile list[KP_UI_PROFILES];
	KpCampaignProfile none;
	char names[2][64];
	int n;
	kp_t_ok(T, p != NULL, "out of memory");
	kp_t_defer(T, free, p);
	kp_profile_init(p);
	kp_campaign_new_profile(&none);
	p->character_count = 2;
	snprintf(p->characters[0].name, sizeof p->characters[0].name, "Old");
	p->characters[0].time = 1;
	p->characters[0].progress.expansion[2] = 32;
	snprintf(p->characters[1].name, sizeof p->characters[1].name, "Other");
	p->characters[1].time = 2;
	p->characters[1].progress.original[1] = 4;
	snprintf(p->name, sizeof p->name, "Other");
	kp_profile_add_save(p, "kept");
	snprintf(p->name, sizeof p->name, "Old");
	kp_profile_add_save(p, "gone");
	n = kp_profile_login_list(p, list, KP_UI_PROFILES);
	kp_t_eq(T, n, 2, NULL);
	kp_t_eq_str(T, list[0].name, "Old", NULL);
	kp_t_eq(T, list[0].progress[5], 32, "the expansion's Protoss");
	kp_t_eq(T, list[1].time, 2, NULL);
	/* Old deleted: the list's one ID, the logged-in one gone */
	list[0] = list[1];
	kp_profile_from_login_list(p, list, 1, NULL);
	kp_t_eq(T, p->character_count, 1, NULL);
	kp_t_eq_str(T, p->name, "", "no logged-in ID");
	kp_t_ok(T, kp_profile_progress(p) == &p->unnamed, "the progress of none");
	kp_t_eq(T, p->save_count, 1, "the deleted ID's saves leave");
	kp_t_eq_str(T, p->saves[0].name, "kept", NULL);
	/* a new ID logged in (the same name as the deleted one's): no progress; Other keeps its own */
	memset(&list[1], 0, sizeof list[1]);
	snprintf(list[1].name, sizeof list[1].name, "old");
	list[1].time = 3;
	list[1].progress[2] = 1; /* the menus' raise: the episode it started */
	kp_profile_from_login_list(p, list, 2, "old");
	kp_t_eq(T, p->character_count, 2, NULL);
	kp_t_eq_str(T, p->name, "old", NULL);
	kp_t_eq(T, kp_profile_progress(p)->expansion[2], 0, "no progress of the deleted ID");
	kp_t_eq(T, kp_profile_progress(p)->original[2], 1, "the menus' raise");
	kp_t_eq(T, p->characters[0].progress.original[1], 4, "the other ID's own");
	kp_t_eq(T, p->characters[0].progress.original[2], 0, NULL);
	none.original[2] = 1;
	kp_t_ok(T, memcmp(kp_profile_progress(p), &none, sizeof none) == 0, "nothing else");
	kp_t_eq(T, kp_profile_save_list(p, names, 2), 0, "no saves of the deleted ID");
	/* the menus left without a login keep the logged-in ID */
	kp_profile_from_login_list(p, list, 2, "");
	kp_t_eq_str(T, p->name, "old", NULL);
}

static int renamed_count;
static char renamed[2][2][200];

static int rename_but_b(void *ud, const char *from, const char *to)
{
	(void)ud;
	if (renamed_count < 2) {
		snprintf(renamed[renamed_count][0], sizeof renamed[0][0], "%s", from);
		snprintf(renamed[renamed_count][1], sizeof renamed[0][1], "%s", to);
		renamed_count++;
	}
	return strcmp(from, "save_b.kpsave") != 0;
}

static int dropped_count;

static void count_dropped(void *ud, const KpProfileSave *save)
{
	(void)ud;
	(void)save;
	dropped_count++;
}

/* The saves are each ID's (the original's save\<ID>\): its list, its oldest dropped at its limit, its saves dropped
 * with it; a profile before schema 3 gives its one list to the logged-in ID, whose files are renamed once (one that
 * fails leaves the list). */
static void session_profile_saves_per_id(KpT *T)
{
	static const uint8_t SAVES[6] = { 2, 0, 1, 'a', 1, 'b' };
	static const uint8_t CHARS[2] = { 0, 0 };
	static const uint8_t PROG[8 * KP_CAMPAIGN_RACES] = { 0 };
	static char limit[KP_PROFILE_SAVES + 1][64];
	KpProfile *p = (KpProfile *)calloc(1, sizeof *p);
	char names[4][64];
	KpKpfOut old[3];
	KpError err;
	uint8_t *bytes;
	size_t size;
	int k;
	kp_t_ok(T, p != NULL, "out of memory");
	kp_t_defer(T, free, p);
	kp_profile_init(p);
	p->character_count = 2;
	snprintf(p->characters[0].name, sizeof p->characters[0].name, "Fenix");
	snprintf(p->characters[1].name, sizeof p->characters[1].name, "Zeratul");
	kp_t_eq_str(T, kp_profile_save_owner(p), "", "no login: none's");
	kp_profile_add_save(p, "loose");
	snprintf(p->name, sizeof p->name, "zeratul");
	kp_t_eq_str(T, kp_profile_save_owner(p), "zeratul", NULL);
	kp_profile_add_save(p, "shared");
	snprintf(p->name, sizeof p->name, "Fenix");
	kp_profile_add_save(p, "shared");
	kp_profile_add_save(p, "mine");
	kp_t_eq(T, kp_profile_save_list(p, names, 4), 2, "Fenix's two");
	kp_t_eq_str(T, names[0], "mine", "newest first");
	kp_t_eq_str(T, names[1], "shared", NULL);
	snprintf(p->name, sizeof p->name, "Nobody");
	kp_t_eq(T, kp_profile_save_list(p, names, 4), 1, "an ID not listed: none's");
	kp_t_eq_str(T, names[0], "loose", NULL);
	/* an ID at its limit loses its oldest, the others' stay */
	snprintf(p->name, sizeof p->name, "Fenix");
	for (k = 0; k < KP_PROFILE_SAVES; k++) {
		char name[16];
		snprintf(name, sizeof name, "n%d", k);
		kp_profile_add_save(p, name);
	}
	kp_t_eq(T, kp_profile_save_list(p, limit, KP_PROFILE_SAVES + 1), KP_PROFILE_SAVES, "Fenix at its limit");
	kp_t_eq_str(T, limit[0], "n253", "the newest first");
	kp_t_eq_str(T, limit[KP_PROFILE_SAVES - 1], "n0", "shared and mine, the oldest, gone");
	kp_t_eq(T, p->save_count, KP_PROFILE_SAVES + 2, "Zeratul's and none's kept");
	kp_t_eq_str(T, p->saves[p->save_count - 1].name, "loose", NULL);
	kp_t_eq_str(T, p->saves[p->save_count - 2].owner, "zeratul", NULL);
	dropped_count = 0;
	kp_profile_drop_saves(p, "FENIX", count_dropped, NULL);
	kp_t_eq(T, dropped_count, KP_PROFILE_SAVES, "each given before it leaves");
	kp_t_eq(T, p->save_count, 2, NULL);
	/* schema 2: one list, every ID's */
	memset(old, 0, sizeof old);
	old[0].fourcc = "NAME";
	old[0].payload = (const uint8_t *)"Kerrigan";
	old[0].size = 8;
	old[1].fourcc = "CHAR";
	old[1].payload = CHARS;
	old[1].size = sizeof CHARS;
	old[2].fourcc = "SAVE";
	old[2].payload = SAVES;
	old[2].size = sizeof SAVES;
	for (k = 0; k < 3; k++) old[k].schema_version = 1;
	kp_t_ok(T, kp_kpf_build("PROF", 2, 0, "koprulu", NULL, old, 3, &bytes, &size) == 0, "a schema 2 profile");
	kp_t_ok(T, kp_profile_decode(p, bytes, size, &err) == 0, "%s", err.msg);
	free(bytes);
	kp_t_eq(T, p->save_count, 2, NULL);
	kp_t_eq_str(T, p->saves[0].owner, "", "no IDs: none's");
	kp_t_eq(T, p->old_save_files, 1, "the files keep the old names");
	renamed_count = 0;
	kp_t_eq(T, kp_profile_rename_old_saves(p, rename_but_b, NULL), 1, "renamed");
	kp_t_eq(T, renamed_count, 2, NULL);
	kp_t_eq_str(T, renamed[1][0], "save_a.kpsave", NULL);
	kp_t_eq_str(T, renamed[1][1], "save_-a.kpsave", NULL);
	kp_t_eq(T, p->save_count, 1, "a file that cannot be renamed leaves the list");
	kp_t_eq_str(T, p->saves[0].name, "a", NULL);
	kp_t_eq(T, kp_profile_rename_old_saves(p, rename_but_b, NULL), 0, "once");
	/* schema 1: the one ID's */
	old[1].fourcc = "PROG";
	old[1].payload = PROG;
	old[1].size = sizeof PROG;
	kp_t_ok(T, kp_kpf_build("PROF", 1, 0, "koprulu", NULL, old, 3, &bytes, &size) == 0, "a schema 1 profile");
	kp_t_ok(T, kp_profile_decode(p, bytes, size, &err) == 0, "%s", err.msg);
	free(bytes);
	kp_t_eq_str(T, p->saves[1].owner, "Kerrigan", "the logged-in ID's");
	kp_t_eq(T, kp_profile_save_list(p, names, 4), 2, NULL);
}

/* At OK the profile keeps the run's video options, but not a command-line flag's value for its one run: an option a
 * flag gave keeps the profile's value unless the dialog changed it. */
static void session_profile_commit_keeps_the_flags_out(KpT *T)
{
	KpVideoSettings profile, run, flags;
	memset(&flags, 0, sizeof flags);
	profile.gamma = KP_UI_GAMMA_DEFAULT;
	profile.portraits = KP_UI_PORTRAITS_ANIMATED;
	profile.graphics = profile.fog = profile.water = profile.font = profile.lighting = KP_VIDEO_AUTO;
	flags.fog = KP_FOG_ORIGINAL; /* --fog original */
	flags.graphics = KP_GRAPHICS_ORIGINAL; /* --graphics original */
	flags.lighting = KP_LIGHTING_OFF; /* --lighting off */
	run = profile;
	run.lighting = KP_LIGHTING_OFF;      /* the flag's, untouched in the dialog */
	run.gamma = 120;
	run.fog = KP_FOG_ORIGINAL;           /* the flag's, untouched in the dialog */
	run.graphics = KP_GRAPHICS_HD2;      /* the dialog's over the flag's */
	run.water = KP_WATER_ANIMATED;       /* the dialog's, no flag */
	kp_profile_commit_video(&profile, &run, &flags, KP_VIDEO_GIVEN_FOG | KP_VIDEO_GIVEN_GRAPHICS
		| KP_VIDEO_GIVEN_LIGHTING);
	kp_t_eq(T, profile.gamma, 120, "the gamma kept");
	kp_t_eq(T, profile.fog, KP_VIDEO_AUTO, "the fog's flag not kept");
	kp_t_eq(T, profile.graphics, KP_GRAPHICS_HD2, "the art the dialog changed kept");
	kp_t_eq(T, profile.water, KP_WATER_ANIMATED, "the water kept");
	kp_t_eq(T, profile.font, KP_VIDEO_AUTO, NULL);
	kp_t_eq(T, profile.lighting, KP_VIDEO_AUTO, "the lighting's flag not kept");
	kp_profile_commit_video(&profile, &run, &flags, 0);
	kp_t_eq(T, profile.fog, KP_FOG_ORIGINAL, "without the flag the run's fog kept");
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
	{ "session_guard_stops_the_game", session_guard_stops_the_game, 0 },
	{ "session_a_damaged_mission_does_not_load", session_a_damaged_mission_does_not_load, 0 },
	{ "session_a_start_without_its_tileset_fails", session_a_start_without_its_tileset_fails, 0 },
	{ "session_wall_clock_runs_the_steps", session_wall_clock_runs_the_steps, 0 },
	{ "session_a_loaded_save_continues_as_the_saved_game", session_a_loaded_save_continues_as_the_saved_game, 0 },
	{ "session_a_game_save_keeps_the_queued_commands_and_the_campaign",
		session_a_game_save_keeps_the_queued_commands_and_the_campaign, 0 },
	{ "session_a_save_file_is_written_whole", session_a_save_file_is_written_whole, 0 },
	{ "session_profile_round_trip", session_profile_round_trip, 0 },
	{ "session_profile_video_options", session_profile_video_options, 0 },
	{ "session_profile_options", session_profile_options, 0 },
	{ "session_profile_settings", session_profile_settings, 0 },
	{ "session_profile_login_list_deletes_and_creates", session_profile_login_list_deletes_and_creates, 0 },
	{ "session_profile_saves_per_id", session_profile_saves_per_id, 0 },
	{ "session_profile_commit_keeps_the_flags_out", session_profile_commit_keeps_the_flags_out, 0 },
};

KP_SUITE(kp_suite_session, "test_session", TESTS);
