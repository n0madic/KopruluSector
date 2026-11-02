/* The product's session (native/kp_app/kp_session.h; plan phase S, stage S2): the clock, the saves and loads on a
 * scenario map (the save container with the queued commands, the campaign and the presentation's state), the atomic
 * save file and the profile. */
#include "kp_test.h"

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

/* The data the suite's games share, parsed once in the suite's process (as the product does). */
static const KpHostShared *shared(KpT *T)
{
	static KpHostShared sh;
	static int loaded;
	size_t grp_size, iscript_size, aiscript_size;
	const uint8_t *grp, *iscript, *aiscript;
	KpError err;
	if (loaded) return &sh;
	grp = kp_t_read(T, "generated/data/grpmeta.kpd", &grp_size);
	iscript = kp_t_read(T, "generated/data/iscript.kpd", &iscript_size);
	aiscript = kp_t_read(T, "generated/data/aiscript.kpd", &aiscript_size);
	if (kp_host_shared_load(&sh, grp, grp_size, iscript, iscript_size, aiscript, aiscript_size, &err) != 0) {
		kp_t_fail(T, "%s", err.msg);
	}
	loaded = 1;
	return &sh;
}

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
	if (kp_session_load(s, shared(T), dat, dat_size, mission, mission_size, name) != 0) {
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
	kp_t_eq(T, kp_session_load(&s, shared(T), dat, dat_size, mission, 16, MAP), -1, NULL);
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

/* The video options (VOPT) come back; a profile from before the section has the defaults, one from before the lighting
 * its Auto; a value out of its range reads as its default. */
static void session_profile_video_options(KpT *T)
{
	static const uint8_t PROG[8 * KP_CAMPAIGN_RACES] = { 0 };
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
	{ "session_profile_commit_keeps_the_flags_out", session_profile_commit_keeps_the_flags_out, 0 },
};

KP_SUITE(kp_suite_session, "test_session", TESTS);
