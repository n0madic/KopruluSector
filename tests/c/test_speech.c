/* A language's speech over the campaign (ADR 0016, localization's stage L5). The speech's triggers
 * (generated/speech/<loc>/missions: the base's records with the TIME of the language's lines) lay over a mission
 * before its start (kp_host_game_speech); a transmission waits its TIME, so the recorded ends of the waits no longer
 * fit. Every play of tests/golden/oracle/plays runs here with the ruRU speech: the original's run first, which pairs
 * each wait (a transmission's portrait, a centre view's pan) with the release that ended it; then the speech's, where
 * the interface's releases are made as the game's would (a portrait's time counted by the play speed's frames, a pan
 * as long as the original's) and the rest of the log, the commands and the interface's events after a wait's end,
 * moves by the frames that end moved. A wait whose end the original's log made otherwise (an Escape, a release among
 * a trigger pause's ticks, whose frame does not advance) keeps the log's. A play fails when an interface's wait of the
 * game stays on longer than any transmission, or when it has no victory and is not one of SCRIPT_LOSSES (or wins and
 * is). Skipped without the generated data and the speech. */
#include "kp_test.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kp_session.h"

#define PLAYS "tests/golden/oracle/plays/"
#define SPEECH "generated/speech/ruRU/missions/"
#define RESULT_VICTORY 3
#define CAMPAIGN_MISSIONS 64
#define VICTORY_MARGIN 2000 /* the steps a play may take past its original's last compared step and its waits' moves */
#define ENDED_BY_LOG (-1)
#define HANG_FRAMES 3000    /* an interface's wait longer than any transmission's (the longest, 80 s, is 1434 frames) */
#define WAIT_FOREVER 0xffffffffLL /* kp_triggers.c: the remaining time of a wait the interface ends */
#define MATCH_AHEAD 4       /* the original's waits a wait may pass over to find its own (two triggers in another order) */
#define PAN_FRAMES 3        /* a pan's frames before the original's run has shown one */

#define TRIGGER_RECORD 2400 /* a CHK TRIG record: 16 conditions of 20 bytes, 64 actions of 32, the flags */
#define TRIGGER_CONDITIONS 320
#define TRIGGER_ACTIONS 64
#define TRIGGER_ACTION 32
#define TRIGGER_ACTION_TIME 0x0c /* KP_ACTION_TIME: u32 milliseconds */
#define SAVE_STEP 600       /* a_save_keeps_its_speech: the step of the save, the steps after it and their stride */
#define SAVE_ON 1200
#define SAVE_EVERY 200
#define SAVE_DATA "test data"

static const char *const FILES[] = { KP_ORACLE_DATA "dat.kpd", PLAYS "index.json", SPEECH "terran/tutorial.kpm", NULL };

/* The plays whose log, moved by the speech's waits, no longer wins: a recorded play is a fixed list of picks and
 * orders, made against the original's timing by tools/research/sc_emulator/campaign_plays.py's reactive toolkit; the
 * waits move the triggers' units, while the AI's and the combat's own clock does not move, so picks fall on units that
 * died earlier or later and the battles go another way. Their waits are released all the same. */
static const char *const SCRIPT_LOSSES[] = {
	"terran-m06", "terran-m10", "terran-m11", "terran-wasteland", "xprotoss-m36", "xterran-m41", "xterran-m42",
	"xzerg-m50", "xzerg-m58", "xzerg-m60", "xzerg-m64", "zerg-m12", "zerg-m17", "zerg-m21", NULL,
};

static int script_loss(const char *name)
{
	int k;
	for (k = 0; SCRIPT_LOSSES[k] != NULL; k++) {
		if (strcmp(SCRIPT_LOSSES[k], name) == 0) return 1;
	}
	return 0;
}

enum { WAIT_PORTRAIT, WAIT_CAMERA };

/* A wait of the original's run: its kind, the frame it began, the frame its release ended it (ENDED_BY_LOG) and its ms. */
typedef struct {
	int kind;
	int64_t start, end, ms;
	int clocked; /* a portrait whose release came when its time ran out */
} Wait;

typedef struct {
	KpT *T;
	KpGoldenRun *gr;
	int speech;          /* the second run */
	/* the original's run */
	Wait *waits;
	int wait_count, open; /* open: the wait not ended yet, -1 none */
	int off_clock;        /* the original's portraits whose release portrait_end does not give */
	uint8_t *paired;      /* the log's releases that ended a wait, by the log's index */
	/* the speech's run */
	int64_t *frames, *turns; /* the log's frames and the commands' turns as recorded */
	int64_t turn_lead;       /* a command's turn less its frame */
	int next_wait;
	int64_t due;             /* the frame of the pending release, -1 none */
	int64_t moved, most;     /* the frames the last wait moved the log by, the most of them */
	int unmatched;           /* the waits of another order than the original's (no wait of theirs to move the log by) */
	int64_t pan_frames;      /* the last pan's frames in the original's run (an unmatched pan's) */
	int64_t waiting, longest, longest_from; /* the frames the game's interface wait has been on, the longest, from */
	int64_t base_longest, base_longest_from;
	int dead_picks;          /* the log's picks of units no longer (or not yet) there, left out */
} Play;

static int64_t frame_of(const Play *p) { return kp_scalar(p->gr->game.g, KP_FRAME); }

/* A wait's start as the game's events tell it: a transmission's portrait or a centre view's pan (c 1 is its end). */
static int wait_kind(const KpEvent *e)
{
	if (e->kind == KP_EVENT_PORTRAIT) return WAIT_PORTRAIT;
	if (e->kind == KP_EVENT_CAMERA && e->c == 0) return WAIT_CAMERA;
	return -1;
}

/* The interface's end of a portrait shown at frame `start` for ms: its timer fires at the first frame of the play
 * speed's clock past ms, in the message loop of the frame before (the original's runs: release frame = start +
 * max(0, ceil(ms / 56) - 1) at Fast). */
static int64_t portrait_end(int64_t start, int64_t ms)
{
	int64_t frame_ms = KP_CLOCK_SPEED_MS[KP_DEFAULT_SPEED], frames = (ms + frame_ms - 1) / frame_ms;
	return start + (frames > 1 ? frames - 1 : 0);
}

/* The log after the original's frame `after` moves by `by` frames (the earlier entries keep theirs), kept in order. */
static void move_log(Play *p, int64_t after, int64_t by)
{
	KpReplay *r = &p->gr->replay;
	int k;
	for (k = p->gr->run.next_ui; k < r->ui_count; k++) {
		if (p->frames[k] > after) r->ui[k].frame = p->frames[k] + by;
		if (k > p->gr->run.next_ui && r->ui[k].frame < r->ui[k - 1].frame) r->ui[k].frame = r->ui[k - 1].frame;
	}
	for (k = p->gr->run.next_command; k < r->command_count; k++) {
		if (p->turns[k] - p->turn_lead > after) r->commands[k].turn = p->turns[k] + by;
		if (k > p->gr->run.next_command && r->commands[k].turn < r->commands[k - 1].turn) {
			r->commands[k].turn = r->commands[k - 1].turn;
		}
	}
}

static void wait_began(Play *p, int kind, int64_t ms)
{
	int64_t now = frame_of(p);
	if (!p->speech) {
		p->waits = (Wait *)realloc(p->waits, sizeof *p->waits * (size_t)(p->wait_count + 1));
		if (!p->waits) kp_t_fail(p->T, "out of memory");
		p->waits[p->wait_count].kind = kind;
		p->waits[p->wait_count].start = now;
		p->waits[p->wait_count].end = ENDED_BY_LOG;
		p->waits[p->wait_count].ms = ms;
		p->open = p->wait_count++;
		return;
	}
	{
		int j = p->next_wait;
		const Wait *w;
		/* by the speech's clock, unless the original's wait says otherwise; the log may end it before (an Escape, the
		 * releases among a trigger pause's ticks) */
		p->due = kind == WAIT_PORTRAIT ? portrait_end(now, ms) : now + p->pan_frames;
		while (j < p->wait_count && j < p->next_wait + MATCH_AHEAD && p->waits[j].kind != kind) j++;
		if (j >= p->wait_count || j >= p->next_wait + MATCH_AHEAD) { /* another order of the triggers: not the original's */
			p->unmatched++;
			return;
		}
		p->next_wait = j + 1;
		w = &p->waits[j];
		if (w->end == ENDED_BY_LOG) return;
		if (w->kind == WAIT_CAMERA) p->pan_frames = w->end - w->start;
		/* a pan, or a portrait something else ended in the original's run (a release of the same frame), as long as
		 * the original's */
		if (!w->clocked) p->due = now + (w->end - w->start);
		/* the player acts no earlier than the original's: a shorter wait keeps the move before it */
		if (p->due - w->end > p->moved) p->moved = p->due - w->end;
		if (p->moved > p->most) p->most = p->moved;
		move_log(p, w->end, p->moved);
	}
}

static void on_event(void *ud, const KpEvent *e)
{
	Play *p = (Play *)ud;
	int kind = wait_kind(e);
	if (kind >= 0) wait_began(p, kind, e->b);
}

#define SPRITE_HIDDEN 0x20

/* A unit the interface can pick: on the map (the active list), its sprite shown with its images. */
static int living(KpGame *g, int64_t n)
{
	int64_t k;
	int32_t s;
	if (n < 1 || n > KP_UNITS || (s = (int32_t)g->u.sprite[n]) < 0 || s >= KP_SPRITES) return 0;
	if (g->w->spr.images[s].head == KP_NONE || (g->w->spr.flags[s] & SPRITE_HIDDEN) != 0) return 0;
	for (k = g->lists[KP_LIST_ACTIVE].head; k != 0; k = g->u.next[k]) {
		if (k == n) return 1;
	}
	return 0;
}

/* The interface's events of this frame name the units the player could click: a moved log's selection or target may
 * name a unit that has died (or not come yet), which the interface cannot select. */
static void living_units(Play *p, int64_t now)
{
	KpReplay *r = &p->gr->replay;
	KpGame *g = p->gr->game.g;
	int k, j, n;
	for (k = p->gr->run.next_ui; k < r->ui_count && r->ui[k].frame <= now; k++) {
		KpReplayUi *u = &r->ui[k];
		if (u->kind == KP_RUN_SELECT) {
			for (j = n = 0; j < u->count; j++) {
				if (living(g, u->values[j])) u->values[n++] = u->values[j];
			}
			if (n < u->count) p->dead_picks += u->count - n;
			u->count = n;
		} else if (u->kind == KP_RUN_MARK && u->values[2] != 0 && !living(g, u->values[2])) {
			u->values[2] = 0;
			p->dead_picks++;
		}
	}
}

/* After the frame's objects, before its interface events: the frame's events (a wait that began), then the original's
 * releases and skips of this frame (its run), or the pending release and the game's interface wait (the speech's). */
static void after_objects(void *ud)
{
	Play *p = (Play *)ud;
	const KpReplay *r = &p->gr->replay;
	KpGame *g = p->gr->game.g;
	int64_t now = frame_of(p);
	int k;
	p->turn_lead = kp_scalar(g, KP_TURN) - now;
	kp_runner_drain(&p->gr->run);
	if (g->trig.wait_active[g->local_player] != 0 && g->trig.wait_remaining[g->local_player] == WAIT_FOREVER) {
		if (++p->waiting > p->longest) {
			p->longest = p->waiting;
			p->longest_from = now - p->waiting + 1;
		}
	} else {
		p->waiting = 0;
	}
	if (p->speech) {
		living_units(p, now);
		if (p->due >= 0 && now >= p->due) { /* the interface's release of a wait still on (the log may have ended it) */
			if (g->trig.wait_active[g->local_player] != 0 && g->trig.wait_remaining[g->local_player] == WAIT_FOREVER) {
				kp_trigger_release_wait(g);
			}
			p->due = -1;
		}
		return;
	}
	for (k = p->gr->run.next_ui; k < r->ui_count && r->ui[k].frame <= now; k++) {
		if ((r->ui[k].kind == KP_RUN_RELEASE || r->ui[k].kind == KP_RUN_SKIP) && p->open >= 0) {
			if (r->ui[k].kind == KP_RUN_RELEASE) {
				Wait *w = &p->waits[p->open];
				w->end = now;
				p->paired[k] = 1;
				w->clocked = w->kind == WAIT_PORTRAIT && w->end == portrait_end(w->start, w->ms);
				if (w->kind == WAIT_PORTRAIT && !w->clocked) p->off_clock++;
			}
			p->open = -1;
		}
	}
}

static KpGoldenRun *start_run(Play *p, const KpJson *g, const char *speech)
{
	KpOracleOpts o;
	KpGoldenRun *gr;
	kp_oracle_opts(&o);
	o.host.events = 1;
	o.speech = speech;
	gr = kp_t_golden_run(p->T, g, &o, 0);
	gr->run.on_event = on_event;
	gr->run.after_objects = after_objects;
	gr->run.ud = p;
	p->gr = gr;
	return gr;
}

/* The speech's run's log: the paired releases out (the run makes its own), the recorded frames and turns kept. */
static void speech_log(Play *p)
{
	KpReplay *r = &p->gr->replay;
	int k, n = 0;
	for (k = 0; k < r->ui_count; k++) {
		if (!p->paired[k]) r->ui[n++] = r->ui[k];
	}
	r->ui_count = n;
	p->frames = (int64_t *)malloc(sizeof *p->frames * (size_t)(n + 1));
	p->turns = (int64_t *)malloc(sizeof *p->turns * (size_t)(r->command_count + 1));
	if (!p->frames || !p->turns) kp_t_fail(p->T, "out of memory");
	for (k = 0; k < n; k++) p->frames[k] = r->ui[k].frame;
	for (k = 0; k < r->command_count; k++) p->turns[k] = r->commands[k].turn;
}

/* The play's run to its victory or its limit; 1 at its victory. */
static int run_to_victory(Play *p, int64_t limit, int64_t *at)
{
	KpGoldenRun *gr = p->gr;
	KpGame *g = gr->game.g;
	while (gr->run.step < limit) {
		kp_runner_to(&gr->run, gr->run.step + 1);
		if (g->results[g->local_player] == RESULT_VICTORY) {
			*at = gr->run.step;
			return 1;
		}
	}
	*at = gr->run.step;
	return 0;
}

static void free_play(Play *p)
{
	free(p->waits);
	free(p->paired);
	free(p->frames);
	free(p->turns);
}

/* One play: the original's run, then the speech's; differences appended to bad. Returns the waits it moved. */
static int play(KpT *T, const KpJson *g, const char *name, KpBuf *bad, int64_t *moved_frames, int *won_out)
{
	Play p;
	int mark = kp_t_mark(T), won, waits;
	int64_t base_at, speech_at, limit;
	memset(&p, 0, sizeof p);
	p.T = T;
	p.open = -1;
	p.due = -1;
	p.pan_frames = PAN_FRAMES;
	start_run(&p, g, NULL);
	p.paired = (uint8_t *)calloc((size_t)p.gr->replay.ui_count + 1, 1);
	if (!p.paired) kp_t_fail(T, "out of memory");
	won = run_to_victory(&p, p.gr->replay.last + VICTORY_MARGIN, &base_at);
	if (!won) kp_buf_printf(bad, "%s%s: the original's run has no victory", bad->size ? ", " : "", name);
	waits = p.wait_count;
	p.base_longest = p.longest;
	p.base_longest_from = p.longest_from;
	p.waiting = p.longest = 0;
	kp_t_release(T, mark);
	if (won) {
		mark = kp_t_mark(T);
		p.speech = 1;
		start_run(&p, g, SPEECH);
		speech_log(&p);
		/* the limit follows the waits' moves */
		limit = base_at + VICTORY_MARGIN;
		while (!run_to_victory(&p, limit, &speech_at) && limit < base_at + p.most + VICTORY_MARGIN) {
			limit = base_at + p.most + VICTORY_MARGIN;
		}
		won = p.gr->game.g->results[p.gr->game.g->local_player] == RESULT_VICTORY;
		if (p.longest > HANG_FRAMES) {
			kp_buf_printf(bad, "%s%s: a wait of %lld frames from %lld (the original's longest %lld from %lld)", bad->size ? ", " : "", name, (long long)p.longest, (long long)p.longest_from, (long long)p.base_longest, (long long)p.base_longest_from);
		}
		if (!won && !script_loss(name)) {
			kp_buf_printf(bad, "%s%s: no victory by step %lld (the original's at %lld, waits %d of %d)", bad->size ? ", " : "",
				name, (long long)speech_at, (long long)base_at, p.next_wait, p.wait_count);
		} else if (won && script_loss(name)) {
			kp_buf_printf(bad, "%s%s: won, off the list of the plays whose log no longer wins", bad->size ? ", " : "", name);
		}
		kp_t_log(T, "%s: %s at step %lld (the original's victory at %lld), %d waits (%d of another order, %d portraits off the "
			"clock, the longest %lld frames), the log moved %lld frames, %d picks of units not there", name,
			won ? "victory" : "no victory", (long long)speech_at, (long long)base_at, waits, p.unmatched, p.off_clock,
			(long long)p.longest, (long long)p.moved, p.dead_picks);
		*won_out = won;
		*moved_frames = won ? speech_at - base_at : 0;
		kp_t_release(T, mark);
	}
	free_play(&p);
	return waits;
}

static void campaign_plays_win_with_the_speech(KpT *T)
{
	const KpJson *index;
	KpBuf *bad;
	int k, ran = 0, waits = 0, missions = 0, wins = 0;
	int64_t later = 0;
	char seen[CAMPAIGN_MISSIONS][64];
	if (!kp_t_ready(T, "test_speech", FILES)) return;
	bad = kp_t_buf(T);
	index = kp_t_golden(T, PLAYS "index.json");
	for (k = 0; k < kp_json_len(index); k++) {
		const char *name = kp_json_str(kp_json_at(index, k), ""), *mission;
		char path[512];
		const KpJson *g;
		int64_t moved = 0;
		int m, won = 0;
		snprintf(path, sizeof path, PLAYS "%s.json", name);
		g = kp_t_golden(T, path);
		mission = kp_json_field_str(g, "mission", "");
		for (m = 0; m < missions && strcmp(seen[m], mission) != 0; m++) continue;
		if (m == missions && missions < CAMPAIGN_MISSIONS) snprintf(seen[missions++], sizeof seen[0], "%s", mission);
		if (!kp_t_mine_sampled(T, k + 1)) continue;
		waits += play(T, g, name, bad, &moved, &won);
		wins += won;
		later += moved;
		ran++;
	}
	kp_t_eq(T, missions, CAMPAIGN_MISSIONS, "the plays' missions");
	kp_t_ok(T, bad->size == 0, "plays with the speech: %s", kp_buf_text(bad));
	kp_t_log(T, "%d plays with the speech, %d won, %d waits, the victories %lld steps later in all", ran, wins, waits,
		(long long)later);
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
	{ "campaign_plays_win_with_the_speech", campaign_plays_win_with_the_speech, 8 },
	{ "a_save_keeps_its_speech", a_save_keeps_its_speech, 0 },
};

KP_SUITE(kp_suite_speech, "test_speech", TESTS);
