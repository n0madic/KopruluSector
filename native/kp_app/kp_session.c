/*
 * The product's session (kp_session.h): the clock, the mission manager, the save container and the profile.
 *
 * The simulation never reads the wall clock (the trigger elapsed counter and waits advance per logical step); the
 * clock only decides how many steps to run in a rendered frame and the interpolation fraction:
 *   - the step's length comes from the speed table 0x503360;
 *   - at most max_catchup steps run per frame, the excess backlog is dropped (a stall never becomes a burst);
 *   - an iconified window suspends the clock and its return discards the time away; a mobile device's lost focus
 *     pauses the game until its owner resumes it.
 * A step that fails stops the game: the session keeps the error and the game's last state and runs no more steps.
 */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200112L /* fileno and fsync (kp_write_file_atomic) under -std=c99 */
#endif
#include "kp_session.h"
#include "kp_ids.h"
#include "kp_sha256.h"
#include "kp_text.h"
#include "kp_utf8.h"
#include "zlib.h"

#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

const int32_t KP_CLOCK_SPEED_MS[KP_CLOCK_SPEEDS] = { 167, 111, 83, 67, 56, 48, 42 };

#define REPL_VERSION 1
#define MAPC_VERSION 1
#define CORE_HEADER 12 /* "KPSV", u32 version, u32 the mission name's length */
#define CORE_PREFIX 4  /* CORE: u32 the native save's size, then the save as one zlib stream */
#define CORE_LEVEL 1   /* zlib's level of CORE (ADR 0021: 8 MB in about 10 ms to 1/47) */
#define CORE_SCHEMA 2  /* CORE's section schema: the compressed form */
#define PROFILE_KIND "PROF"
#define PROFILE_SCHEMA 11 /* 1: one ID, its progress in PROG; 1 and 2: the saves without their IDs; 1..5: no SOPT; 1..3: no OPTS; 4: OPTS
                          * with the intro's bit only; 1..6: the names in windows-1252, not UTF-8; 1..7: no LANG; 1..8: no
                          * CUST; 9: CUST without the multiplayer values; 10: CUST without the host address */
#define PROFILE_NAMES_UTF8 7
#define PROFILE_ID "koprulu"
#define PROFILE_SECTIONS 8     /* NAME, CHAR, SAVE, VOPT, OPTS, SOPT, LANG, CUST */
#define FILE_PART_MAX 84       /* 28 bytes as _XX: no name of the original's limits, in windows-1252, goes past it */
#define FNV_BASIS 0xcbf29ce484222325ull
#define FNV_PRIME 0x100000001b3ull
#define PROGRESS_WORDS (2 * KP_PROFILE_WORD_RACES) /* kp_profile_words */
#define CHAR_FIXED (4 + 4 * PROGRESS_WORDS) /* an ID's bytes after its name: the time, the progress */
#define VOPT_VALUES 6
/* A SOPT value with no range (the others': the registry table 0x50f168's, the sliders'). */
#define SOPT_ANY 0xffffffffu

/* --- the clock --- */

void kp_clock_init(KpClock *c, int speed, int max_catchup)
{
	memset(c, 0, sizeof *c);
	c->speed = speed >= 1 && speed <= KP_CLOCK_SPEEDS ? speed : KP_CLOCK_DEFAULT_SPEED;
	c->multiplier = 1;
	c->max_catchup = max_catchup > 0 ? max_catchup : KP_CLOCK_DEFAULT_MAX_CATCHUP;
}

/* SetReplaySpeed 0x481040's table: Slowest times the multiplier, the others divided by it (unsigned); never below 1 ms,
 * the step kp_clock_advance divides by */
int32_t kp_clock_step_ms(const KpClock *c)
{
	int32_t ms = KP_CLOCK_SPEED_MS[c->speed - 1];
	if (c->multiplier > 1) ms = c->speed == 1 ? ms * c->multiplier : ms / c->multiplier;
	return ms < 1 ? 1 : ms;
}

void kp_clock_set_multiplier(KpClock *c, int64_t multiplier)
{
	c->multiplier = multiplier < 1 ? 1 : multiplier > KP_CLOCK_MAX_MULTIPLIER ? KP_CLOCK_MAX_MULTIPLIER : (int)multiplier;
}

int kp_clock_set_speed(KpClock *c, int speed)
{
	if (speed < 1 || speed > KP_CLOCK_SPEEDS) return -1;
	c->speed = speed;
	return 0;
}

void kp_clock_reset(KpClock *c)
{
	c->accumulator_ms = 0;
	c->has_wall = 0;
	c->resets++;
}

void kp_clock_pause(KpClock *c) { c->paused = 1; }

void kp_clock_resume(KpClock *c)
{
	c->paused = 0;
	kp_clock_reset(c);
}

void kp_clock_on_window(KpClock *c, int event, int autopause)
{
	if (event == KP_WINDOW_ICONIFIED) {
		c->suspended = 1;
	} else if (event == KP_WINDOW_DEICONIFIED && c->suspended) {
		c->suspended = 0;
		kp_clock_reset(c);
	} else if (event == KP_WINDOW_FOCUS_LOST && autopause) {
		kp_clock_pause(c);
	}
}

int kp_clock_stopped(const KpClock *c) { return c->paused || c->suspended; }

double kp_clock_alpha(const KpClock *c)
{
	return c->accumulator_ms > 0 ? c->accumulator_ms / kp_clock_step_ms(c) : 0;
}

int kp_clock_advance(KpClock *c, double dt, double *alpha)
{
	double step, acc;
	int n;
	if (kp_clock_stopped(c)) {
		*alpha = 0;
		return 0;
	}
	if (dt < 0) dt = 0;
	step = kp_clock_step_ms(c);
	acc = c->accumulator_ms + dt * 1000;
	n = acc > 0 ? (int)(acc / step) : 0; /* a step put later (kp_clock_after_wait) leaves it below 0 */
	if (n > c->max_catchup) {
		double excess = (n - c->max_catchup) * step;
		c->dropped_ms += excess;
		acc -= excess;
		n = c->max_catchup;
	}
	acc -= n * step;
	c->accumulator_ms = acc;
	c->steps += n;
	*alpha = kp_clock_alpha(c);
	return n;
}

int kp_clock_advance_wall(KpClock *c, double now, double *alpha)
{
	double last = c->last_wall;
	int had = c->has_wall;
	c->last_wall = now;
	c->has_wall = 1;
	if (kp_clock_stopped(c) || !had) {
		*alpha = kp_clock_alpha(c);
		return 0;
	}
	return kp_clock_advance(c, now - last, alpha);
}

void kp_clock_after_wait(KpClock *c, double waited_ms, int frames_per_turn)
{
	int32_t step = kp_clock_step_ms(c);
	if (waited_ms < KP_CLOCK_SHORT_WAIT_MS) c->accumulator_ms -= (double)(step / 2);
	else c->accumulator_ms = (double)step - (double)((uint32_t)(step * frames_per_turn) >> 1);
}

/* --- the session --- */

void kp_session_start_defaults(KpSessionStart *s)
{
	memset(s, 0, sizeof *s);
	s->objective_count = -1;
}

static void fail(KpSession *s, const char *msg)
{
	s->failed = 1;
	snprintf(s->error, sizeof s->error, "%s", msg);
}

/* The game's speed byte 0x6d3130 (from 0) of a clock speed (from 1). */
static int64_t game_speed(int speed) { return speed - 1; }

typedef struct {
	KpGame *g;
	const KpSessionStart *start;
} FirstTurn;

/* The briefing's objectives before the game (the given ones, or the briefing's first tick's), then PrimeGameLoop's two
 * object steps and RunGame's first turn. */
static void first_turn(void *ud)
{
	const FirstTurn *f = (const FirstTurn *)ud;
	int k;
	if (f->start->objective_count >= 0) {
		for (k = 0; k < f->start->objective_count; k++) kp_briefing_objectives(f->g, f->start->objectives[k]);
	} else if (kp_briefing_lead(f->g) != 0) {
		kp_briefing_objectives(f->g, kp_briefing_lead(f->g));
	}
	kp_prime(f->g);
	/* a seat's comes from the hub (kp_session_seat_begin), a network game's from the network (kp_session_net_begin) */
	if (!f->start->hub && !f->start->net) kp_run_turn(f->g, NULL, 0);
}

typedef struct {
	KpSession *s;
	int status;
} NetFrame;

int kp_session_load_from(KpSession *s, const KpHostShared *sh, const KpDat *dat, KpMission *mission, const char *name,
	const KpFiles *files)
{
	KpError err;
	memset(s, 0, sizeof *s);
	if (kp_host_game_load_from(&s->host, sh, dat, mission, name, &err) != 0
		|| (files && kp_host_game_tileset_files(&s->host, files, &err) != 0)) {
		kp_host_game_free(&s->host);
		fail(s, err.msg);
		return -1;
	}
	return 0;
}

/* A start's turn source (a replay's playback: its record) and recorder (a recorded game's, empty, recording). */
static int start_turns(KpSession *s, const KpSessionStart *start)
{
	if (start->hub) {
		kp_turns_seats(&s->turns, start->hub, start->seat);
		return 0;
	}
	if (start->net && start->replay) return kp_turns_net_replay(&s->turns, start->net, start->replay);
	if (start->watch && start->replay) return kp_turns_watch(&s->turns, start->replay);
	if (start->net) kp_turns_net(&s->turns, start->net);
	else if (start->replay) return kp_turns_replay(&s->turns, start->replay);
	if (!start->record) return 0;
	if (!(s->rec = (KpRecorder *)malloc(sizeof *s->rec))) return -1;
	if (kp_record_init(s->rec) != 0) {
		free(s->rec);
		s->rec = NULL;
		return -1;
	}
	return 0;
}

int kp_session_start(KpSession *s, const KpSessionStart *start)
{
	KpHostOpts o;
	KpError err;
	FirstTurn first;
	kp_clock_init(&s->clock, start->speed, start->max_catchup);
	if (start_turns(s, start) != 0) {
		fail(s, "replay: out of memory");
		return -1;
	}
	kp_host_opts(&o);
	o.events = 1;
	o.speed = game_speed(s->clock.speed);
	o.mission_index = start->mission_index;
	o.cheats = start->cheats;
	o.carry = start->has_carry ? &start->carry : NULL;
	o.replay = start->replay != NULL && !start->replay_as_game;
	o.setup = start->has_setup ? &start->setup : NULL;
	o.local_storm = start->local_storm;
	if (kp_host_game_start(&s->host, start->seed, start->expansion, &o, &err) != 0) {
		fail(s, err.msg);
		return -1;
	}
	s->g = s->host.g;
	if (start->hub && kp_hub_seat(start->hub, start->seat, s->g->local_record, (int32_t)start->local_storm) != 0) {
		fail(s, "seats: a seat or Storm ID twice");
		return -1;
	}
	kp_turns_begin(&s->turns, s->g->local_record); /* the seat's record issues its commands */
	if (start->replay) {
		s->g->replay_frames = kp_turns_frames(&s->turns);
		s->g->replay_header_speed = kp_rep_speed(start->replay);
	} else if (start->record) {
		KpRepGame game;
		kp_session_replay_game(s, start->seed, &game);
		kp_session_replay_header(s, &game);
	}
	first.g = s->g;
	first.start = start;
	(void)kp_session_guard(s, first_turn, &first);
	s->start_frame = kp_scalar(s->g, KP_FRAME);
	s->start_turn = kp_scalar(s->g, KP_TURN);
	return s->failed ? -1 : 0;
}

const KpMission *kp_session_mission(const KpSession *s) { return s->g ? &s->host.data.mission : NULL; }

const KpGrpMeta *kp_session_grp(const KpSession *s) { return s->g ? &s->host.shared->data.grp : NULL; }

void kp_session_free(KpSession *s)
{
	if (s->rec) kp_record_free(s->rec);
	free(s->rec);
	kp_turns_free(&s->turns);
	kp_host_game_free(&s->host);
	memset(s, 0, sizeof *s);
}

int kp_session_command(KpSession *s, const uint8_t *bytes, size_t size, int64_t player)
{
	if (!s->g) return -1;
	return kp_turns_submit(&s->turns, bytes, size, player < 0 ? s->g->local_player : player);
}

int kp_session_playing(const KpSession *s) { return kp_turns_playing(&s->turns); }
int kp_session_replay_ended(const KpSession *s) { return kp_turns_ended(&s->turns); }
uint32_t kp_session_replay_frames(const KpSession *s) { return kp_turns_frames(&s->turns); }

typedef struct {
	KpGame *g;
	const KpCommand *list;
	int count, between, first;
} Frame;

static void run_frame(void *ud)
{
	const Frame *f = (const Frame *)ud;
	if (f->first) kp_run_turn(f->g, f->list, f->count);
	else if (f->between) kp_run_frame_between_turns(f->g);
	else kp_run_frame(f->g, f->list, f->count);
}

int kp_session_seat_begin(KpSession *s)
{
	Frame frame;
	int64_t f;
	if (s->failed || !s->g || s->turns.kind != KP_TURNS_SEATS) return -1;
	memset(&frame, 0, sizeof frame);
	frame.g = s->g;
	frame.first = 1;
	f = kp_scalar(s->g, KP_FRAME);
	if (kp_hub_first_turn(s->turns.hub, s->turns.seat, &frame.list, &frame.count) != KP_HUB_TURN) {
		fail(s, "seats: the first turn is not there");
		return -1;
	}
	if (s->rec) kp_turns_record(&s->turns, s->rec, f, frame.list, frame.count);
	(void)kp_session_guard(s, run_frame, &frame);
	s->start_turn = kp_scalar(s->g, KP_TURN);
	return s->failed ? -1 : 0;
}

/* The view's top tile row the sprites' checksum starts from (the screen's draws keep it, 0x5264b2) */
static int64_t screen_tile_y(const KpGame *g)
{
	int64_t y = kp_scalar(g, KP_SCREEN_Y);
	return y == KP_NIL ? 0 : y;
}

/* TurnManagerStep 0x476c00 as the network game polls it: the turn's commands recorded at the frame and run, the sync
 * ring advanced (at a poll, PollTurn 0x444cc0's minimap statistics after it) */
static int net_turn(KpSession *s, int poll)
{
	KpTurns *t = &s->turns;
	const KpCommand *list;
	int count;
	switch (kp_netgame_game_turn(t->net, s->g, &list, &count)) {
	case KP_NETGAME_TURN: break;
	case KP_NETGAME_WAIT: return KP_TURNS_WAIT;
	default: return KP_TURNS_NO_MEMORY;
	}
	if (s->rec) kp_turns_record(t, s->rec, kp_scalar(s->g, KP_FRAME), list, count);
	kp_run_turn(s->g, list, count);
	kp_netgame_game_turn_end(t->net, s->g, poll);
	return KP_TURNS_READY;
}

/* RunGame 0x445310's step 7 at the first call (the game primed: kp_netgame_game_begin), then its step 8's turn */
static void net_first(void *ud)
{
	NetFrame *f = (NetFrame *)ud;
	KpSession *s = f->s;
	if (!s->net_begun) {
		s->net_begun = 1;
		kp_netgame_game_begin(s->turns.net, s->g);
		kp_turns_net_begin(&s->turns, kp_scalar(s->g, KP_SPEED));
	}
	f->status = net_turn(s, 0);
}

int kp_session_net_begin(KpSession *s)
{
	NetFrame f;
	if (s->failed || !s->g || s->turns.kind != KP_TURNS_NET) return -1;
	f.s = s;
	f.status = KP_TURNS_WAIT;
	(void)kp_session_guard(s, net_first, &f);
	if (s->failed) return -1;
	if (f.status == KP_TURNS_NO_MEMORY) {
		fail(s, "network: out of memory");
		return -1;
	}
	if (f.status != KP_TURNS_READY) return 0;
	s->start_turn = kp_scalar(s->g, KP_TURN);
	return 1;
}

/* A received chat line's command 0x5c, recorded under the local Storm ID at the frame (HandleChatMessage 0x475b40) */
static void record_chat(void *ud, const uint8_t *cmd, size_t size)
{
	KpSession *s = (KpSession *)ud;
	KpCommand c;
	if (!s->rec) return;
	c.player = s->g->local_record;
	c.bytes = cmd;
	c.size = size;
	kp_turns_record(&s->turns, s->rec, kp_scalar(s->g, KP_FRAME), &c, 1);
}

static void net_messages(void *ud)
{
	KpSession *s = ((NetFrame *)ud)->s;
	kp_netgame_game_messages(s->turns.net, s->g, record_chat, s);
}

void kp_session_net_messages(KpSession *s)
{
	NetFrame f;
	if (s->failed || !s->g || s->turns.kind != KP_TURNS_NET) return;
	f.s = s;
	f.status = 0;
	(void)kp_session_guard(s, net_messages, &f);
}

#define KP_REPLAY_RESULT 3 /* the victory dialog's result (KP_EVENT_GAME_RESULT's a) */

/* The game's speed command (ExecGameSpeed 0x473310) sets the speed the clock steps at, a replay's speed command
 * (SetReplaySpeed 0x481040) the multiplier too */
static void follow_speed(KpSession *s)
{
	int64_t speed = kp_scalar(s->g, KP_SPEED);
	if (speed != KP_NIL && game_speed(s->clock.speed) != speed && speed >= 0 && speed < KP_CLOCK_SPEEDS) {
		kp_clock_set_speed(&s->clock, (int)speed + 1);
	}
	if (kp_turns_playing(&s->turns)) kp_clock_set_multiplier(&s->clock, s->g->replay_multiplier);
}

/* A hosted replay's FeedReplayCommands 0x4769b0 (RunPendingFrames 0x444e30 step 1): the record's commands of the
 * frame run before the turn is polled; at its end the playback pauses and the victory dialog opens once. 0, or -1 out
 * of memory. */
static int net_feed(KpSession *s)
{
	const KpCommand *list;
	int count, status = kp_turns_feed(&s->turns, kp_scalar(s->g, KP_FRAME), &list, &count);
	if (status == KP_TURNS_NO_MEMORY) return -1;
	if (count) kp_run_commands(s->g, list, count);
	if (status == KP_TURNS_ENDED) kp_event(s->g, KP_EVENT_GAME_RESULT, KP_REPLAY_RESULT, 0, 0, NULL);
	if (kp_turns_ended(&s->turns)) s->g->replay_paused = 1;
	return 0;
}

/* A frame's step after PollTurn let it run: none while a replay's playback is paused (RunPendingFrames step 3),
 * else the frame (a paused game's flashes and triggers) */
static void net_step_frame(KpSession *s)
{
	if (s->turns.feed && s->g->replay_paused) return;
	kp_run_frame_between_turns(s->g);
}

/* A network game's frame (RunPendingFrames 0x444e30's iteration): a hosted replay's record, PollTurn's countdown, a
 * turn at its end, then the frame (a paused one's flashes and triggers), and after a turn's first frame the draw's
 * sprites' checksum */
static void net_frame(void *ud)
{
	NetFrame *f = (NetFrame *)ud;
	KpSession *s = f->s;
	KpTurns *t = &s->turns;
	if (t->feed && net_feed(s) != 0) {
		f->status = KP_TURNS_NO_MEMORY;
		return;
	}
	if (t->countdown > 1) {
		t->countdown--;
		net_step_frame(s);
		f->status = KP_TURNS_BETWEEN;
		return;
	}
	f->status = net_turn(s, 1);
	if (f->status != KP_TURNS_READY) {
		t->countdown = 1; /* polled again at the next frame */
		return;
	}
	net_step_frame(s);
	kp_netgame_game_draw(t->net, s->g, screen_tile_y(s->g));
	t->countdown = t->frames_per_turn;
}

/* After a step: the record's mark for the viewers' server (a failed one ends what it publishes) */
static void watch_mark(KpSession *s)
{
	if (s->watch_server && s->rec && kp_watch_server_mark(s->watch_server, s->rec, (uint32_t)kp_scalar(s->g, KP_FRAME)) != 0)
		(void)kp_watch_server_end(s->watch_server, NULL, 0);
}

static int net_step(KpSession *s, double (*clock_ms)(void))
{
	NetFrame f;
	double t0 = clock_ms ? clock_ms() : 0, ms;
	f.s = s;
	f.status = KP_TURNS_WAIT;
	(void)kp_session_guard(s, net_frame, &f);
	if (f.status == KP_TURNS_NO_MEMORY && !s->failed) fail(s, "step: out of memory");
	if (s->failed || f.status == KP_TURNS_WAIT) return 0;
	s->tick++;
	ms = clock_ms ? clock_ms() - t0 : 0;
	s->step_ms += ms;
	if (ms > s->step_max_ms) s->step_max_ms = ms;
	if (s->turns.feed) { /* a hosted replay's speed command: the clock, and PollTurn's frames per turn 0x666404[speed] */
		follow_speed(s);
		s->turns.frames_per_turn = kp_hub_frames_per_turn((int)game_speed(s->clock.speed), (int)s->turns.net->turns_per_second);
	}
	watch_mark(s);
	return 1;
}

int kp_session_step(KpSession *s, double (*clock_ms)(void))
{
	Frame frame;
	double t0, ms;
	int64_t f;
	int status;
	if (s->failed || !s->g) return 0;
	if (s->turns.kind == KP_TURNS_NET) return net_step(s, clock_ms);
	f = kp_scalar(s->g, KP_FRAME);
	memset(&frame, 0, sizeof frame);
	frame.g = s->g;
	status = kp_turns_frame(&s->turns, f, &frame.list, &frame.count);
	if (status == KP_TURNS_NO_MEMORY) {
		fail(s, "step: out of memory");
		return 0;
	}
	if (status == KP_TURNS_WAIT) return 0; /* a seat's turn is missing: the frame waits (WaitForLaggingPlayers 0x47a810) */
	frame.between = status == KP_TURNS_BETWEEN;
	/* at the end of the record the playback pauses (SetReplayPaused 0x480e00) and the victory dialog opens once
	 * (ShowVictoryDialog 0x4bcff0, the presentation's on the event) */
	if (status == KP_TURNS_ENDED) kp_event(s->g, KP_EVENT_GAME_RESULT, KP_REPLAY_RESULT, 0, 0, NULL);
	if (kp_turns_ended(&s->turns)) s->g->replay_paused = 1;
	/* RecordCommand 0x478340 of the dispatched commands, at the frame before the step */
	if (s->rec) kp_turns_record(&s->turns, s->rec, f, frame.list, frame.count);
	s->tick++;
	t0 = clock_ms ? clock_ms() : 0;
	(void)kp_session_guard(s, run_frame, &frame);
	/* the commands are the step's (the list points into the queue until the step is done) */
	kp_turns_done(&s->turns);
	ms = clock_ms ? clock_ms() - t0 : 0;
	s->step_ms += ms;
	if (ms > s->step_max_ms) s->step_max_ms = ms;
	if (s->failed) return 0;
	follow_speed(s);
	watch_mark(s);
	return 1;
}

int kp_session_steps_per_tick(const KpSession *s)
{
	return kp_turns_playing(&s->turns) && s->g && s->g->replay_multiplier >= 8 && kp_scalar(s->g, KP_SPEED) != 0 ? 10 : 1;
}

/* Whether a network game's step queued a save (KP_EVENT_SAVE_GAME, still waiting for the host) */
static int net_save_queued(KpSession *s)
{
	KpEvent e;
	int64_t k;
	if (s->turns.kind != KP_TURNS_NET || !s->g) return 0;
	for (k = 0; kp_event_peek(s->g, k, &e); k++) {
		if (e.kind == KP_EVENT_SAVE_GAME) return 1;
	}
	return 0;
}

int kp_session_advance(KpSession *s, double now, double *alpha, double (*clock_ms)(void))
{
	int due = kp_clock_advance_wall(&s->clock, now, alpha), ran = 0, k;
	while (ran < due) {
		/* RunPendingFrames 0x444e30 runs up to 0x50335c frames each time */
		int per = kp_session_steps_per_tick(s);
		for (k = 0; k < per && kp_session_step(s, clock_ms); k++) {
		}
		if (k == 0) {
			/* PollTurn 0x444cc0: a network game's turn did not come, the wait begins (0x666400, 0x66642c) */
			if (s->turns.kind == KP_TURNS_NET && !s->failed && !s->net_waiting) {
				s->net_waiting = 1;
				s->net_wait_start = now;
			}
			break;
		}
		ran++;
		if (s->net_waiting) {
			/* the turn came: the next step's time from the wait's length, and RunPendingFrames' call ends after the
			 * turn's first frame (its out flag in multiplayer) */
			s->net_waiting = 0;
			kp_clock_after_wait(&s->clock, (now - s->net_wait_start) * 1000, s->turns.frames_per_turn);
			*alpha = kp_clock_alpha(&s->clock);
			break;
		}
		if (k < per) break;
		/* every seat writes a network save at the end of the step that ran its command (the original's ExecSaveGame
		 * 0x472f50 saves within the turn): the host writes it before the next step */
		if (net_save_queued(s)) break;
	}
	if (s->failed) *alpha = 1;
	return ran;
}

static int record_refused(const uint8_t *record, size_t record_size, size_t *at, int growing);

int kp_session_watch_grow(KpSession *s, const uint8_t *bytes, size_t size, uint32_t frames, int whole)
{
	const KpRecorder *r;
	if (s->failed || !s->g) return -1;
	if (kp_turns_watch_grow(&s->turns, bytes, size, frames, whole) != 0) {
		fail(s, "watch: out of memory");
		return -1;
	}
	r = s->turns.feed;
	if (record_refused(r->buf, r->sentinel ? r->sentinel - 1 : r->used, &s->watch_checked, 1)) {
		fail(s, "watch: the record holds a command the playback refuses");
		return -1;
	}
	s->g->replay_frames = kp_turns_frames(&s->turns); /* the progress bar's (0x480e20): the frames known so far */
	return 0;
}

/* --- the replay's file --- */

static int err_msg(KpError *err, const char *msg);

/* Every id the recorder never writes (RecordCommand 0x478340's table), which no file the original wrote holds: a
 * crafted file's save 0x06 or load 0x07 would write over or load the player's saves. kp_record_split's callback over
 * an entry, as the playback walks it (an entry may hold more commands than its first). */
static void refused_command(void *ud, const uint8_t *cmd, size_t n)
{
	(void)n;
	if (kp_record_excluded(cmd[0])) *(int *)ud = 1;
}

/* The entries of a record from *at as the feed reads them (ReadRecordedFrame 0x478480, kp_record.c read_frame: an
 * entry's length may run past its block's size, the next block's header follows it; an unknown id or an entry past
 * the record ends the data), each walked as the dispatcher walks it; *at left at the first block not walked whole (a
 * growing record's next bytes go on there). 1 when one holds a refused command. A growing record's unknown id is
 * refused too: the feed's read would go on past it as bytes come, where this walk cannot follow (the recorder never
 * writes one, so only a forged record holds it); the feed drops a refused command besides (kp_turns.c feed). */
static int record_refused(const uint8_t *record, size_t record_size, size_t *at, int growing)
{
	int refused = 0;
	while (*at + 5 <= record_size) {
		size_t pos = *at + 5, size = record[*at + 4], sum = 0;
		while (sum < size) {
			int len;
			if (pos + 2 > record_size) return 0; /* the Storm ID and the command's id come */
			pos++; /* the Storm ID */
			len = kp_record_command_length(record + pos, record_size - pos);
			if (len < 0 && record[pos] >= 9 && record[pos] <= 0xb) return 0; /* its count comes */
			if (len < 0) {
				if (growing) return 1;
				*at = record_size;
				return 0;
			}
			if ((size_t)len > record_size - pos) return 0;
			(void)kp_record_split(record + pos, (size_t)len, refused_command, &refused);
			if (refused) return 1;
			pos += (size_t)len;
			sum += (size_t)len + 1;
		}
		*at = pos;
	}
	return 0;
}

int kp_session_replay_start(KpSessionStart *st, const KpRep *rep)
{
	size_t at = 0;
	int k;
	for (k = 0; k < KP_REP_PLAYERS; k++) { /* a record with a Storm ID plays as its slot: a game's player, 0..7 */
		KpRepPlayer p;
		kp_rep_player(rep, k, &p);
		if (p.storm != -1 && p.slot >= KP_TURNS_SLOTS) return -1;
	}
	if (record_refused(rep->record, rep->record_size, &at, 0)) return -1;
	st->seed = kp_rep_seed(rep);
	st->expansion = kp_rep_brood_war(rep);
	st->mission_index = kp_rep_campaign(rep);
	/* a custom map's game (campaign row 0) starts from the header's setup; a campaign's from its map */
	st->has_setup = st->mission_index == 0;
	if (st->has_setup) kp_rep_setup(rep, &st->setup);
	st->speed = kp_rep_speed(rep) + 1;
	st->replay = rep;
	st->record = 0;
	st->objective_count = 0; /* Load Replay starts the game with no briefing (CreateGameFromEntry 0x45bfc0) */
	/* Koprulu's tail: the globals the game before left and the cheats at the start, which the header does not hold
	 * (the original, which never reads the tail, plays a continued game from none) */
	if (rep->has_tail && rep->tail && rep->tail->version == KP_REP_TAIL_VERSION) {
		st->has_carry = rep->tail->has_carry;
		if (st->has_carry) st->carry = rep->tail->carry;
		st->cheats = rep->tail->cheats;
	}
	return 0;
}

#define ANONYMOUS "Anonymous" /* network.tbl 72: the name without a login */

void kp_session_replay_game(KpSession *s, uint32_t seed, KpRepGame *game)
{
	KpGame *g = s->g;
	const KpMission *m = &s->host.data.mission;
	int p;
	memset(game, 0, sizeof *game);
	game->brood_war = (int)g->expansion;
	game->campaign = (int)kp_scalar(g, KP_MISSION_INDEX);
	game->seed = seed;
	game->player_name = ANONYMOUS;
	game->map_w = (int)m->width;
	game->map_h = (int)m->height;
	game->tileset = (int)(m->era % 8);
	game->speed = (int)kp_scalar(g, KP_SPEED);
	game->map_name = kp_map_string(g, m->name_string);
	game->template_record = g->setup.template_record;
	game->slots = (int)g->setup.slots;
	game->colors = g->setup.color;
	game->user_select = g->setup.user_select;
	if (s->turns.kind == KP_TURNS_NET && s->turns.net) game->seed_record = s->turns.net->start_info;
	/* the started game's records (RunGameSession 0x467ba0 records 0x6726c8 after StartGame's slot steps); the header
	 * has the slots 8..11 empty (kp_rep_header), a team game's too: the emulated 1.17's 0x6726c8 at StartGame has them
	 * so in every network game of phase N's suite, Team Melee, Top vs Bottom and Capture The Flag among them */
	for (p = 0; p < 8; p++) {
		KpRepPlayer *r = &game->players[p];
		r->slot = (uint32_t)p;
		r->type = (uint8_t)g->setup.type[p];
		r->race = (uint8_t)g->setup.race[p];
		r->force = (uint8_t)g->setup.force[p];
		r->storm = (int32_t)g->setup.storm[p];
		if (g->setup.name[p][0]) memcpy(r->name, g->setup.name[p], sizeof r->name - 1); /* r zeroed: terminated */
		else if (r->type == KP_CTRL_HUMAN) snprintf(r->name, sizeof r->name, "%s", ANONYMOUS);
	}
}

void kp_session_replay_header(KpSession *s, const KpRepGame *game)
{
	KpRep r;
	memset(&r, 0, sizeof r);
	kp_rep_header(&r, game);
	memcpy(s->rec_header, r.header, sizeof s->rec_header);
}

int kp_session_replay_file(KpSession *s, const KpRepTail *tail, KpRep *out, KpError *err)
{
	const KpMission *m = &s->host.data.mission;
	const uint8_t *bytes;
	size_t size;
	memset(out, 0, sizeof *out);
	if (!s->g || !s->rec) return err_msg(err, "replay: the game is not recorded");
	memcpy(out->header, s->rec_header, sizeof out->header);
	kp_rep_set_frames(out, (uint32_t)kp_scalar(s->g, KP_FRAME));
	bytes = kp_record_bytes(s->rec, &size);
	out->record = (uint8_t *)malloc(size ? size : 1);
	out->chk = (uint8_t *)malloc(m->chk_size ? m->chk_size : 1);
	if (tail) out->tail = (KpRepTail *)malloc(sizeof *out->tail);
	if (!out->record || !out->chk || (tail && !out->tail)) {
		kp_rep_free(out);
		return err_msg(err, "replay: out of memory");
	}
	memcpy(out->record, bytes, size);
	out->record_size = size;
	memcpy(out->chk, m->blob, m->chk_size);
	out->chk_size = m->chk_size;
	if (tail) {
		*out->tail = *tail;
		out->has_tail = 1;
	}
	return 0;
}

void kp_session_pause(KpSession *s) { kp_clock_pause(&s->clock); }
void kp_session_resume(KpSession *s) { kp_clock_resume(&s->clock); }

void kp_session_set_speed(KpSession *s, int speed)
{
	if (kp_clock_set_speed(&s->clock, speed) == 0 && s->g) kp_scalar_set(s->g, KP_SPEED, game_speed(speed));
}

int kp_game_guard(KpGame *g, void (*fn)(void *ud), void *ud)
{
	KpTry t;
	kp_images_begin(g->w, &t);
	if (setjmp(t.buf)) {
		kp_images_end(g->w, &t);
		return -1;
	}
	fn(ud);
	kp_images_end(g->w, &t);
	return 0;
}

int kp_session_guard(KpSession *s, void (*fn)(void *ud), void *ud)
{
	if (!s->g) return -1;
	if (kp_game_guard(s->g, fn, ud) == 0) return 0;
	fail(s, s->g->w->error);
	return -1;
}

/* The interface's part of the triggers between two steps: the end of a transmission's portrait or of a camera pan
 * releases the local player's trigger wait; Escape skips the transmissions it can. */
typedef struct {
	KpGame *g;
	int what, r;
} Trigger;

static void trigger_call(void *ud)
{
	Trigger *t = (Trigger *)ud;
	if (t->what == 0) kp_trigger_release_wait(t->g);
	else if (t->what == 1) t->r = kp_trigger_skippable(t->g);
	else kp_trigger_skip(t->g);
}

static int guarded(KpSession *s, int what)
{
	Trigger t;
	if (!s->g || s->failed) return 0;
	t.g = s->g;
	t.what = what;
	t.r = 0;
	return kp_session_guard(s, trigger_call, &t) == 0 ? t.r : 0;
}

void kp_session_release_wait(KpSession *s) { guarded(s, 0); }
int kp_session_can_skip(KpSession *s) { return guarded(s, 1); }
void kp_session_skip(KpSession *s) { guarded(s, 2); }

/* --- the save file --- */

static int err_msg(KpError *err, const char *msg)
{
	snprintf(err->msg, sizeof err->msg, "%s", msg);
	return -1;
}

void kp_save_identity(KpSaveIdentity *id, const KpFiles *files, const KpDat *dat)
{
	memset(id, 0, sizeof *id);
	snprintf(id->profile_id, sizeof id->profile_id, "%s", files->profile->id);
	kp_sha256(dat->blob, dat->size, id->source_hash);
}

typedef struct {
	uint8_t *b;
	size_t n, cap;
	int failed;
} Buf;

static void put(Buf *b, const void *p, size_t n)
{
	if (b->failed) return;
	if (b->n + n > b->cap) {
		size_t cap = b->cap ? b->cap * 2 : 256;
		uint8_t *g;
		while (cap < b->n + n) cap *= 2;
		g = (uint8_t *)realloc(b->b, cap);
		if (!g) {
			b->failed = 1;
			return;
		}
		b->b = g;
		b->cap = cap;
	}
	if (n) memcpy(b->b + b->n, p, n);
	b->n += n;
}

static void put8(Buf *b, uint32_t v)
{
	uint8_t x = (uint8_t)v;
	put(b, &x, 1);
}

static void put16(Buf *b, uint32_t v)
{
	put8(b, v & 0xff);
	put8(b, (v >> 8) & 0xff);
}

static void put32(Buf *b, uint32_t v)
{
	put16(b, v & 0xffff);
	put16(b, v >> 16);
}

static void put64(Buf *b, int64_t v)
{
	put32(b, (uint32_t)(uint64_t)v);
	put32(b, (uint32_t)((uint64_t)v >> 32));
}

static int64_t get64(const uint8_t *d) { return (int64_t)((uint64_t)kp_le_u32(d) | (uint64_t)kp_le_u32(d + 4) << 32); }

/* A setup's bytes (MAPC): the template, the multiplayer byte, slots and started (i64), per slot its Storm ID, type,
 * race and force (i64), the names, then the 8 user-select flags and colours (i64). */
#define SETUP_BYTES (KP_TEMPLATE_SIZE + 8 * 3 + KP_SETUP_SLOTS * 4 * 8 + KP_SETUP_SLOTS * KP_SETUP_NAME + 8 * 2 * 8)

static void put_setup(Buf *b, const KpGameSetup *s)
{
	int k;
	put(b, s->template_record, KP_TEMPLATE_SIZE);
	put64(b, s->multiplayer);
	put64(b, s->slots);
	put64(b, s->started);
	for (k = 0; k < KP_SETUP_SLOTS; k++) {
		put64(b, s->storm[k]);
		put64(b, s->type[k]);
		put64(b, s->race[k]);
		put64(b, s->force[k]);
	}
	put(b, s->name, sizeof s->name);
	for (k = 0; k < 8; k++) put64(b, s->user_select[k]);
	for (k = 0; k < 8; k++) put64(b, s->color[k]);
}

static void get_setup(const uint8_t *d, KpGameSetup *s)
{
	int k;
	memset(s, 0, sizeof *s);
	memcpy(s->template_record, d, KP_TEMPLATE_SIZE);
	d += KP_TEMPLATE_SIZE;
	s->multiplayer = get64(d);
	s->slots = get64(d + 8);
	s->started = get64(d + 16);
	d += 24;
	for (k = 0; k < KP_SETUP_SLOTS; k++, d += 32) {
		s->storm[k] = get64(d);
		s->type[k] = get64(d + 8);
		s->race[k] = get64(d + 16);
		s->force[k] = get64(d + 24);
	}
	memcpy(s->name, d, sizeof s->name);
	for (k = 0; k < KP_SETUP_SLOTS; k++) s->name[k][KP_SETUP_NAME - 1] = 0;
	d += sizeof s->name;
	for (k = 0; k < 8; k++) s->user_select[k] = get64(d + 8 * k);
	for (k = 0; k < 8; k++) s->color[k] = get64(d + 64 + 8 * k);
}

static int pres_index(const KpSavePresentation *p, const char *name)
{
	int k;
	for (k = 0; k < p->count; k++) {
		if (strcmp(p->names[k], name) == 0) return k;
	}
	return -1;
}

int64_t kp_save_pres_get(const KpSavePresentation *p, const char *name, int64_t fallback)
{
	int k = pres_index(p, name);
	return k < 0 ? fallback : p->values[k];
}

void kp_save_pres_set(KpSavePresentation *p, const char *name, int64_t value)
{
	int k = pres_index(p, name);
	if (k < 0) {
		if (p->count >= KP_PRES_VALUES || strlen(name) >= sizeof p->names[0]) return;
		k = p->count++;
		snprintf(p->names[k], sizeof p->names[k], "%s", name);
	}
	p->values[k] = value;
}

/* CORE: u32 the native save's size, then the save as one zlib stream at level CORE_LEVEL; *out malloc'd. */
static int deflate_core(const uint8_t *core, size_t size, uint8_t **out, size_t *out_size)
{
	uLongf n = compressBound((uLong)size);
	uint8_t *z = (uint8_t *)malloc(CORE_PREFIX + (size_t)n);
	if (!z) return -1;
	if (compress2(z + CORE_PREFIX, &n, core, (uLong)size, CORE_LEVEL) != Z_OK) {
		free(z);
		return -1;
	}
	kp_le_put32(z, (uint32_t)size);
	*out = z;
	*out_size = CORE_PREFIX + (size_t)n;
	return 0;
}

int kp_save_encode(KpSession *s, const KpSaveIdentity *id, const KpSaveCampaign *camp, const KpSavePresentation *pres,
	uint8_t **out, size_t *size, KpError *err)
{
	return kp_save_encode_with(s, id, camp, pres, NULL, NULL, 0, out, size, err);
}

int kp_save_encode_with(KpSession *s, const KpSaveIdentity *id, const KpSaveCampaign *camp,
	const KpSavePresentation *pres, const char *fourcc, const uint8_t *extra, size_t extra_size, uint8_t **out,
	size_t *size, KpError *err)
{
	Buf cmds, cmp, prs, rpl, mpc;
	KpKpfOut sections[7];
	const KpMission *mission = &s->host.data.mission;
	uint8_t *core = NULL, *zcore = NULL;
	size_t core_size = 0, zcore_size = 0;
	int k, rc, nsections = 4;
	*out = NULL;
	if (!s->g) return err_msg(err, "mission: no game to save");
	if (s->failed) {
		snprintf(err->msg, sizeof err->msg, "mission: the game stopped in a step and cannot be saved: %s", s->error);
		return -1;
	}
	if (kp_save(s->g, s->host.name, s->host.identity, &core, &core_size, err) != 0) return -1;
	if (core_size > KP_SAVE_CORE_MAX) { /* decode refuses it: no save this build cannot load */
		free(core);
		snprintf(err->msg, sizeof err->msg, "save: a native save of %lu bytes, a load takes %u at most", (unsigned long)core_size,
			KP_SAVE_CORE_MAX);
		return -1;
	}
	rc = deflate_core(core, core_size, &zcore, &zcore_size);
	free(core);
	if (rc != 0) return err_msg(err, "save: out of memory");
	memset(&cmds, 0, sizeof cmds);
	memset(&cmp, 0, sizeof cmp);
	memset(&prs, 0, sizeof prs);
	memset(&rpl, 0, sizeof rpl);
	memset(&mpc, 0, sizeof mpc);
	/* REPL (a recorded game's): u32 version, the header kept since the start, u32 the loads, u32 whether one lost the
	 * record, i64 the open block's frame, u32 size and the record, its open block closed (SaveRecordedCommands
	 * 0x478980: commands of the same frame recorded after it go on without a header) */
	if (s->rec) {
		size_t n;
		const uint8_t *bytes = kp_record_bytes(s->rec, &n);
		put32(&rpl, REPL_VERSION);
		put(&rpl, s->rec_header, sizeof s->rec_header);
		put32(&rpl, (uint32_t)s->rec_loads);
		put32(&rpl, (uint32_t)s->rec_lost);
		put32(&rpl, (uint32_t)s->rec->open_frame);
		put32(&rpl, (uint32_t)((uint64_t)s->rec->open_frame >> 32));
		put32(&rpl, (uint32_t)n);
		put(&rpl, bytes, n);
		sections[nsections].fourcc = "REPL";
		sections[nsections].payload = rpl.b;
		sections[nsections].size = rpl.n;
		nsections++;
	}
	/* MAPC (a custom map's game, ADR 0024): u32 version, u8 the load (KpChkLoad), u32 size and the scenario as read,
	 * the setup the game started from (put_setup): the load starts the same game before it loads the state */
	if (mission->load != KP_CHK_CAMPAIGN) {
		put32(&mpc, MAPC_VERSION);
		put8(&mpc, (uint32_t)mission->load);
		put32(&mpc, (uint32_t)mission->chk_size);
		put(&mpc, mission->blob, mission->chk_size);
		put_setup(&mpc, &s->g->setup);
		sections[nsections].fourcc = "MAPC";
		sections[nsections].payload = mpc.b;
		sections[nsections].size = mpc.n;
		nsections++;
	}
	/* CMDS: u32 count, then per command u8 player, u16 size and its bytes */
	put32(&cmds, (uint32_t)s->turns.pending_count);
	for (k = 0; k < s->turns.pending_count; k++) {
		const KpQueuedCommand *q = &s->turns.pending[k];
		put8(&cmds, (uint32_t)q->player);
		put16(&cmds, q->size);
		put(&cmds, q->bytes, q->size);
	}
	/* CAMP: u8 present; u8 expansion, u8 table (from 1; 0 no cached row), u16 row (from 1), u16 mission, u8 size and
	 * the pending next scenario's name, the profile's six u32 */
	if (!camp || !camp->present) {
		put8(&cmp, 0);
	} else {
		const KpCampaignRun *r = &camp->run;
		size_t n = strlen(r->pending);
		put8(&cmp, 1);
		put8(&cmp, (uint32_t)r->expansion);
		put8(&cmp, r->row.table >= 0 ? (uint32_t)r->row.table + 1 : 0);
		put16(&cmp, r->row.table >= 0 ? (uint32_t)r->row.index + 1 : 0);
		put16(&cmp, (uint32_t)r->mission);
		put8(&cmp, (uint32_t)n);
		put(&cmp, r->pending, n);
		for (k = 0; k < 6; k++) put32(&cmp, (uint32_t)camp->progress[k]);
	}
	/* PRES: name=integer lines in name order */
	if (pres) {
		int order[KP_PRES_VALUES], j;
		for (k = 0; k < pres->count; k++) order[k] = k;
		for (k = 1; k < pres->count; k++) {
			for (j = k; j > 0 && strcmp(pres->names[order[j]], pres->names[order[j - 1]]) < 0; j--) {
				int x = order[j];
				order[j] = order[j - 1];
				order[j - 1] = x;
			}
		}
		for (k = 0; k < pres->count; k++) {
			char line[64];
			int n = snprintf(line, sizeof line, "%s%s=%lld", k ? "\n" : "", pres->names[order[k]],
				(long long)pres->values[order[k]]);
			put(&prs, line, (size_t)n);
		}
	}
	sections[0].fourcc = "CORE";
	sections[0].payload = zcore;
	sections[0].size = zcore_size;
	sections[1].fourcc = "CMDS";
	sections[1].payload = cmds.b;
	sections[1].size = cmds.n;
	sections[2].fourcc = "CAMP";
	sections[2].payload = cmp.b;
	sections[2].size = cmp.n;
	sections[3].fourcc = "PRES";
	sections[3].payload = prs.b;
	sections[3].size = prs.n;
	if (fourcc) {
		sections[nsections].fourcc = fourcc;
		sections[nsections].payload = extra;
		sections[nsections].size = extra_size;
		nsections++;
	}
	for (k = 0; k < nsections; k++) sections[k].schema_version = 1;
	sections[0].schema_version = CORE_SCHEMA;
	rc = cmds.failed || cmp.failed || prs.failed || rpl.failed || mpc.failed ? -1
		: kp_kpf_build(KP_SAVE_KIND, KP_SAVE_SCHEMA, 0, id->profile_id, id->source_hash, sections, nsections, out, size);
	free(zcore);
	free(cmds.b);
	free(cmp.b);
	free(prs.b);
	free(rpl.b);
	free(mpc.b);
	if (rc != 0) return err_msg(err, "save: out of memory");
	return 0;
}

/* The parts of a save after checking its container and data set: the sections, the native save's size. */
typedef struct {
	KpKpf c;
	const KpKpfSection *core, *cmds, *camp, *pres;
	uint32_t core_size;
} SaveParts;

/* The container checked (its CRCs), the save's schema and data set (a refusal as another build's) and its sections
 * (a refusal as damaged): *refusal the kind (KP_SAVE_LOADABLE when it passes). */
static int decode(SaveParts *p, const uint8_t *save, size_t size, const KpSaveIdentity *id, int *refusal, KpError *err)
{
	KpKpfExpect e;
	int is_save;
	*refusal = KP_SAVE_DAMAGED;
	if (kp_kpf_parse(&p->c, save, size, "save", 1, err) != 0) return -1;
	is_save = memcmp(p->c.kind, KP_SAVE_KIND, 4) == 0;
	if (is_save && p->c.schema_version != KP_SAVE_SCHEMA) {
		snprintf(err->msg, sizeof err->msg, "save: a save of another build (save schema %u, this build reads %d)",
			(unsigned)p->c.schema_version, KP_SAVE_SCHEMA);
		*refusal = KP_SAVE_OTHER_BUILD;
		goto failed;
	}
	if (is_save && (strcmp(p->c.profile_id, id->profile_id) != 0 || memcmp(p->c.source_hash, id->source_hash, 32) != 0)) {
		err_msg(err, "save: made with other game data");
		*refusal = KP_SAVE_OTHER_BUILD;
		goto failed;
	}
	memset(&e, 0, sizeof e);
	e.kind = KP_SAVE_KIND;
	e.schema_version = KP_SAVE_SCHEMA;
	e.profile_id = id->profile_id;
	e.source_hash = id->source_hash;
	if (kp_kpf_check(&p->c, &e, err) != 0 || !(p->core = kp_kpf_require(&p->c, "CORE", err))
		|| !(p->cmds = kp_kpf_require(&p->c, "CMDS", err)) || !(p->camp = kp_kpf_require(&p->c, "CAMP", err))
		|| !(p->pres = kp_kpf_require(&p->c, "PRES", err))) {
		goto failed;
	}
	if (p->core->schema_version != CORE_SCHEMA) {
		snprintf(err->msg, sizeof err->msg, "save: CORE of section schema %u, this build reads %d", (unsigned)p->core->schema_version,
			CORE_SCHEMA);
		goto failed;
	}
	if (p->core->size < CORE_PREFIX || (p->core_size = kp_le_u32(p->core->data)) < CORE_HEADER || p->core_size > KP_SAVE_CORE_MAX) {
		err_msg(err, "save: no native save");
		goto failed;
	}
	*refusal = KP_SAVE_LOADABLE;
	return 0;
failed:
	kp_kpf_free(&p->c);
	return -1;
}

/* The first `want` bytes of the native save (its whole size: all of it) inflated from CORE into *out (malloc'd). */
static int inflate_core(const SaveParts *p, size_t want, uint8_t **out, KpError *err)
{
	z_stream z;
	uint8_t *buf = (uint8_t *)malloc(want);
	int rc, whole = want == p->core_size;
	if (!buf) return err_msg(err, "save: out of memory");
	memset(&z, 0, sizeof z);
	if (inflateInit(&z) != Z_OK) {
		free(buf);
		return err_msg(err, "save: out of memory");
	}
	z.next_in = (Bytef *)(p->core->data + CORE_PREFIX);
	z.avail_in = (uInt)(p->core->size - CORE_PREFIX);
	z.next_out = buf;
	z.avail_out = (uInt)want;
	rc = inflate(&z, whole ? Z_FINISH : Z_NO_FLUSH);
	(void)inflateEnd(&z);
	if (whole ? rc != Z_STREAM_END || z.total_out != want : z.avail_out != 0 || (rc != Z_OK && rc != Z_STREAM_END)) {
		free(buf);
		return err_msg(err, "save: a damaged native save");
	}
	*out = buf;
	return 0;
}

/* The native save's head: its version (a refusal as another build's) and the mission's name. */
static int core_head(const SaveParts *p, char *mission, size_t mission_size, int *refusal, KpError *err)
{
	size_t want = p->core_size < CORE_HEADER + KP_SAVE_NAME ? p->core_size : CORE_HEADER + KP_SAVE_NAME;
	uint8_t *head;
	uint32_t version, n;
	int rc = -1;
	*refusal = KP_SAVE_DAMAGED;
	if (inflate_core(p, want, &head, err) != 0) return -1;
	if (memcmp(head, "KPSV", 4) != 0) {
		err_msg(err, "save: no native save");
	} else if ((version = kp_le_u32(head + 4)) != KP_SAVE_VERSION) {
		snprintf(err->msg, sizeof err->msg, "save: a save of another build (version %u, this build reads %d)", (unsigned)version,
			KP_SAVE_VERSION);
		*refusal = KP_SAVE_OTHER_BUILD;
	} else if (want < CORE_HEADER + (size_t)(n = kp_le_u32(head + 8)) || n >= mission_size) {
		err_msg(err, "save: truncated mission name");
	} else {
		memcpy(mission, head + CORE_HEADER, n);
		mission[n] = 0;
		*refusal = KP_SAVE_LOADABLE;
		rc = 0;
	}
	free(head);
	return rc;
}

int kp_save_check(const uint8_t *save, size_t size, const KpSaveIdentity *id, KpError *err)
{
	SaveParts p;
	char mission[KP_SAVE_NAME];
	int refusal;
	if (decode(&p, save, size, id, &refusal, err) != 0) return refusal;
	(void)core_head(&p, mission, sizeof mission, &refusal, err);
	kp_kpf_free(&p.c);
	return refusal;
}

static int decode_campaign(const KpKpfSection *s, KpSaveCampaign *camp, KpError *err)
{
	const uint8_t *d = s->data;
	uint32_t n, table, k;
	memset(camp, 0, sizeof *camp);
	camp->run.row.table = -1;
	if (s->size < 1 || d[0] == 0) return 0;
	if (s->size < 9) return err_msg(err, "save: truncated campaign");
	n = d[7];
	if (s->size < 8 + n + 24 || n >= sizeof camp->run.pending) return err_msg(err, "save: truncated campaign");
	camp->present = 1;
	camp->run.expansion = d[1];
	table = d[2];
	camp->run.row.table = table ? (int)table - 1 : -1;
	camp->run.row.index = table ? (int)kp_le_u16(d + 3) - 1 : 0;
	camp->run.mission = kp_le_u16(d + 5);
	memcpy(camp->run.pending, d + 8, n);
	camp->run.pending[n] = 0;
	for (k = 0; k < 6; k++) camp->progress[k] = kp_le_u32(d + 8 + n + 4 * k);
	return 0;
}

static void decode_presentation(const KpKpfSection *s, KpSavePresentation *p)
{
	size_t at = 0;
	memset(p, 0, sizeof *p);
	while (at < s->size) {
		char line[96];
		size_t end = at, n;
		char *eq;
		while (end < s->size && s->data[end] != '\n') end++;
		n = end - at < sizeof line - 1 ? end - at : sizeof line - 1;
		memcpy(line, s->data + at, n);
		line[n] = 0;
		eq = strchr(line, '=');
		if (eq) {
			*eq = 0;
			kp_save_pres_set(p, line, strtoll(eq + 1, NULL, 10));
		}
		at = end + 1;
	}
}

int kp_save_mission(const uint8_t *save, size_t size, const KpSaveIdentity *id, char *mission, size_t mission_size,
	KpSaveCampaign *camp, KpError *err)
{
	SaveParts p;
	int rc, refusal;
	if (decode(&p, save, size, id, &refusal, err) != 0) return -1;
	rc = core_head(&p, mission, mission_size, &refusal, err);
	if (rc == 0 && camp) rc = decode_campaign(p.camp, camp, err);
	kp_kpf_free(&p.c);
	return rc;
}

int kp_save_map(const uint8_t *save, size_t size, const KpSaveIdentity *id, KpSaveMap *map, KpError *err)
{
	SaveParts p;
	const KpKpfSection *s;
	uint32_t n;
	int refusal, rc = 0;
	memset(map, 0, sizeof *map);
	if (decode(&p, save, size, id, &refusal, err) != 0) return -1;
	if ((s = kp_kpf_find(&p.c, "MAPC")) != NULL) {
		rc = -1;
		if (s->size < 9 || kp_le_u32(s->data) != MAPC_VERSION || s->data[4] == KP_CHK_CAMPAIGN || s->data[4] > KP_CHK_UMS
			|| (n = kp_le_u32(s->data + 5)) > KP_REP_MAX_CHK || s->size != 9 + (size_t)n + SETUP_BYTES) {
			err_msg(err, "save: a damaged custom map");
		} else if (!(map->chk = (uint8_t *)malloc(n ? n : 1))) {
			err_msg(err, "save: out of memory");
		} else {
			if (n) memcpy(map->chk, s->data + 9, n);
			map->chk_size = n;
			map->load = (KpChkLoad)s->data[4];
			get_setup(s->data + 9 + n, &map->setup);
			rc = 1;
		}
	}
	kp_kpf_free(&p.c);
	return rc;
}

#define REPL_FIXED (4 + KP_REP_HEADER_SIZE + 4 + 4 + 8 + 4) /* REPL's fields before the record */

/* A save's REPL as kp_save_encode writes it (NULL: none); -1 damaged. */
static int check_record(const KpKpfSection *r, KpError *err)
{
	if (r && (r->size < REPL_FIXED || kp_le_u32(r->data) != REPL_VERSION
			|| kp_le_u32(r->data + REPL_FIXED - 4) != r->size - REPL_FIXED)) {
		return err_msg(err, "save: a damaged replay record");
	}
	return 0;
}

/* LoadRecordedCommands 0x478640 of a recorded game, once the save's game is loaded: the save's record (REPL, checked by
 * check_record), or an empty one when the save has none (one saved before the replays, or of a game not recorded): the
 * replay then starts from the load and is unverified. Out of memory the record stays as it was and is marked lost. */
static void restore_record(KpSession *s, const KpKpfSection *r)
{
	int64_t open_frame;
	s->rec_loads++;
	if (!r) {
		s->rec_lost = 1;
		(void)kp_record_restore(s->rec, NULL, 0, -1);
		return;
	}
	open_frame = (int64_t)((uint64_t)kp_le_u32(r->data + REPL_FIXED - 12) | (uint64_t)kp_le_u32(r->data + REPL_FIXED - 8) << 32);
	if (kp_record_restore(s->rec, r->data + REPL_FIXED, r->size - REPL_FIXED, open_frame) != 0) {
		s->rec_lost = 1;
		return;
	}
	memcpy(s->rec_header, r->data + 4, KP_REP_HEADER_SIZE);
	s->rec_lost |= (int)kp_le_u32(r->data + 4 + KP_REP_HEADER_SIZE + 4);
}

int kp_save_load(KpSession *s, const uint8_t *save, size_t size, const KpSaveIdentity *id, KpSaveCampaign *camp,
	KpSavePresentation *pres, KpError *err)
{
	SaveParts p;
	KpQueuedCommand cmds[KP_TURNS_PENDING];
	uint32_t count, k;
	size_t at = 4;
	KpSaveCampaign c;
	const KpKpfSection *repl = NULL;
	uint8_t *core = NULL;
	int recorded = s->rec != NULL, refusal;
	if (!s->g) return err_msg(err, "mission: no game to load into");
	if (decode(&p, save, size, id, &refusal, err) != 0) return -1;
	if (decode_campaign(p.camp, &c, err) != 0) goto failed;
	if (p.cmds->size < 4 || (count = kp_le_u32(p.cmds->data)) > KP_TURNS_PENDING) {
		err_msg(err, "save: bad commands");
		goto failed;
	}
	for (k = 0; k < count; k++) {
		uint32_t n;
		/* each as the queue takes it (kp_turns_submit): a seat's player, 1 to KP_TURNS_COMMAND_BYTES bytes */
		if (at + 3 > p.cmds->size || at + 3 + (n = kp_le_u16(p.cmds->data + at + 1)) > p.cmds->size
			|| n == 0 || n > KP_TURNS_COMMAND_BYTES || p.cmds->data[at] >= KP_TURNS_SLOTS) {
			err_msg(err, "save: bad commands");
			goto failed;
		}
		cmds[k].player = p.cmds->data[at];
		cmds[k].size = (uint16_t)n;
		memcpy(cmds[k].bytes, p.cmds->data + at + 3, n);
		at += 3 + n;
	}
	/* the record follows the game: checked first, restored only once kp_load took the save (a save it refuses, of
	 * another mission or damaged, leaves the game and its record as they were) */
	if (recorded && check_record(repl = kp_kpf_find(&p.c, "REPL"), err) != 0) goto failed;
	if (inflate_core(&p, p.core_size, &core, err) != 0
		|| kp_load(s->g, core, p.core_size, s->host.name, s->host.identity, err) != 0) {
		goto failed;
	}
	free(core);
	core = NULL;
	if (recorded) restore_record(s, repl);
	s->failed = 0;
	s->error[0] = 0;
	/* the saved queue, as the source takes commands (a playback lets its live ones through only) */
	kp_turns_done(&s->turns);
	for (k = 0; k < count; k++) (void)kp_turns_submit(&s->turns, cmds[k].bytes, cmds[k].size, cmds[k].player);
	{
		KpEvent e;
		while (kp_event_take(s->g, &e)) {
		}
	}
	s->tick = kp_scalar(s->g, KP_TURN) - s->start_turn;
	kp_scalar_set(s->g, KP_SPEED, game_speed(s->clock.speed)); /* the player's speed, not the saved game's */
	kp_clock_reset(&s->clock);
	s->loads++;
	if (camp) *camp = c;
	if (pres) decode_presentation(p.pres, pres);
	kp_kpf_free(&p.c);
	return 0;
failed:
	free(core);
	kp_kpf_free(&p.c);
	return -1;
}

int kp_write_file_atomic(const char *path, const uint8_t *data, size_t size, KpError *err)
{
	char tmp[1100];
	FILE *f;
	int ok;
	if (strlen(path) + 5 > sizeof tmp) return err_msg(err, "save: a path too long");
	snprintf(tmp, sizeof tmp, "%s.tmp", path);
	f = fopen(tmp, "wb");
	if (!f) {
		snprintf(err->msg, sizeof err->msg, "%s: cannot write", tmp);
		return -1;
	}
	/* the bytes on the disk before the rename: a crash or a power loss after it must not leave an empty file under the
	 * name (an empty profile is read as none, and the next write would replace the player's progress) */
	ok = fwrite(data, 1, size, f) == size && fflush(f) == 0;
#ifdef _WIN32
	ok = ok && _commit(_fileno(f)) == 0;
#else
	ok = ok && fsync(fileno(f)) == 0;
#endif
	ok = fclose(f) == 0 && ok;
	if (!ok) {
		remove(tmp);
		snprintf(err->msg, sizeof err->msg, "%s: write failed", tmp);
		return -1;
	}
#ifdef _WIN32
	/* replaces the old file in one step (rename refuses an existing one; removing it first would leave none) */
	if (MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) return 0;
#else
	if (rename(tmp, path) == 0) return 0;
#endif
	snprintf(err->msg, sizeof err->msg, "%s: rename failed (the new save stays in %s)", path, tmp);
	return -1;
}

static int lower_byte(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

int kp_compare_nocase(const char *a, const char *b)
{
	while (*a && lower_byte((unsigned char)*a) == lower_byte((unsigned char)*b)) {
		a++;
		b++;
	}
	return lower_byte((unsigned char)*a) - lower_byte((unsigned char)*b);
}

static int compare_names(const void *a, const void *b)
{
	return kp_compare_nocase(*(const char *const *)a, *(const char *const *)b);
}

static int has_extension(const char *name, const char *ext)
{
	size_t n = strlen(name), e = strlen(ext);
	return n > e && kp_compare_nocase(name + n - e, ext) == 0;
}

void kp_list_files_free(char **names, int count)
{
	int k;
	for (k = 0; k < count; k++) free(names[k]);
	free(names);
}

/* The files of dir ending in ext, or with folders set its folders (ext unused), as FindFirstFileA finds them: on
 * Windows those with none of the attributes skip (FILE_ATTRIBUTE_*), elsewhere those whose name does not start with
 * '.' (the file systems' hidden ones); unsorted. */
static char **list_dir(const char *dir, const char *ext, int folders, unsigned skip, int *count)
{
	char **names = NULL;
	int cap = 0, failed = 0;
#ifdef _WIN32
	char pattern[1100];
	WIN32_FIND_DATAA found;
	HANDLE h;
	*count = 0;
	snprintf(pattern, sizeof pattern, "%s\\*", dir);
	if ((h = FindFirstFileA(pattern, &found)) == INVALID_HANDLE_VALUE) return NULL;
	do {
		const char *name = found.cFileName;
		if ((found.dwFileAttributes & skip) || strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
		if (!(found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != !folders) continue;
#else
	DIR *d = opendir(dir);
	struct dirent *e;
	*count = 0;
	if (!d) return NULL;
	while ((e = readdir(d)) != NULL) {
		const char *name = e->d_name;
		char path[1100];
		struct stat st;
		(void)skip;
		if (name[0] == '.') continue; /* hidden, as ScanMapDirectory 0x45c9a0 skips the hidden files */
		snprintf(path, sizeof path, "%s/%s", dir, name);
		if (stat(path, &st) != 0 || !S_ISDIR(st.st_mode) != !folders) continue;
#endif
		if (!folders && !has_extension(name, ext)) continue;
		if (*count == cap) {
			char **grown = (char **)realloc(names, sizeof *names * (size_t)(cap ? 2 * cap : 16));
			if (!grown) {
				failed = 1;
				break;
			}
			names = grown;
			cap = cap ? 2 * cap : 16;
		}
		if (!(names[*count] = (char *)malloc(strlen(name) + 1))) {
			failed = 1;
			break;
		}
		strcpy(names[(*count)++], name);
#ifdef _WIN32
	} while (FindNextFileA(h, &found));
	FindClose(h);
#else
	}
	closedir(d);
#endif
	if (failed) {
		kp_list_files_free(names, *count);
		*count = 0;
		return NULL;
	}
	return names;
}

#ifdef _WIN32
#define HIDDEN_FILES FILE_ATTRIBUTE_HIDDEN
#define HIDDEN_REPLAYS (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)
#else
#define HIDDEN_FILES 0u
#define HIDDEN_REPLAYS 0u
#endif

/* list_dir's names sorted by compare */
static char **list_sorted(const char *dir, const char *ext, int folders, unsigned skip,
	int (*compare)(const void *, const void *), int *count)
{
	char **names = list_dir(dir, ext, folders, skip, count);
	if (*count > 0) qsort(names, (size_t)*count, sizeof *names, compare);
	return names;
}

char **kp_list_files(const char *dir, const char *ext, int *count)
{
	return list_sorted(dir, ext, 0, HIDDEN_FILES, compare_names, count);
}

char **kp_list_folders(const char *dir, int *count) { return list_sorted(dir, NULL, 1, HIDDEN_FILES, compare_names, count); }

/* A UTF-16 unit in upper case as NTFS's $UpCase table has it, for the scripts of the languages: ASCII, Latin-1,
 * Latin Extended-A, Greek and Cyrillic; any other unit as it is. */
static uint32_t upcase_unit(uint32_t c)
{
	if (c >= 'a' && c <= 'z') return c - 32;
	if (c < 0xe0) return c == 0xb5 ? 0x39c : c; /* the micro sign: Greek Mu */
	if (c <= 0xfe) return c == 0xf7 ? c : c - 32; /* but the division sign */
	if (c == 0xff) return 0x178;
	if (c == 0x131) return 'I';
	if (c == 0x17f) return 'S';
	if ((c >= 0x100 && c <= 0x137) || (c >= 0x14a && c <= 0x177)) return c & ~1u; /* pairs from an even capital */
	if ((c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17e)) return c & 1u ? c : c - 1; /* from an odd one */
	if (c == 0x3c2) return 0x3a3; /* final sigma */
	if ((c >= 0x3b1 && c <= 0x3c1) || (c >= 0x3c3 && c <= 0x3cb)) return c - 32;
	if (c == 0x3ac) return 0x386;
	if (c >= 0x3ad && c <= 0x3af) return c - 37;
	if (c == 0x3cc) return 0x38c;
	if (c == 0x3cd || c == 0x3ce) return c - 63;
	if (c >= 0x430 && c <= 0x44f) return c - 32;
	if (c >= 0x450 && c <= 0x45f) return c - 80;
	if (c == 0x4cf) return 0x4c0;
	if ((c >= 0x460 && c <= 0x481) || (c >= 0x48a && c <= 0x4bf) || (c >= 0x4d0 && c <= 0x52f)) return c & ~1u;
	if (c >= 0x4c1 && c <= 0x4ce) return c & 1u ? c : c - 1;
	return c;
}

/* A name's UTF-16 units, one by one (a character past the Basic Multilingual Plane its two surrogates, a byte that is
 * no UTF-8 itself). */
typedef struct {
	const uint8_t *s;
	size_t n, at;
	uint32_t low; /* the second surrogate still to come, or 0 */
} Units;

static uint32_t next_unit(Units *u)
{
	int32_t cp;
	size_t from = u->at;
	if (u->low) {
		cp = (int32_t)u->low;
		u->low = 0;
		return (uint32_t)cp;
	}
	if (u->at >= u->n) return 0;
	if ((cp = kp_utf8_next(u->s, u->n, &u->at)) < 0) {
		u->at = from + 1;
		return u->s[from];
	}
	if (cp < 0x10000) return (uint32_t)cp;
	u->low = 0xdc00 + ((uint32_t)(cp - 0x10000) & 0x3ff);
	return 0xd800 + ((uint32_t)(cp - 0x10000) >> 10);
}

int kp_compare_ntfs(const char *a, const char *b)
{
	Units x = { (const uint8_t *)a, strlen(a), 0, 0 }, y = { (const uint8_t *)b, strlen(b), 0, 0 };
	for (;;) {
		uint32_t p = upcase_unit(next_unit(&x)), q = upcase_unit(next_unit(&y));
		if (p != q) return p < q ? -1 : 1;
		if (p == 0) return 0;
	}
}

static int compare_ntfs_names(const void *a, const void *b)
{
	return kp_compare_ntfs(*(const char *const *)a, *(const char *const *)b);
}

char **kp_list_replays(const char *dir, int *count)
{
	return list_sorted(dir, ".rep", 0, HIDDEN_REPLAYS, compare_ntfs_names, count);
}

char **kp_list_ntfs(const char *dir, int folders, int *count)
{
	return list_sorted(dir, "", folders, HIDDEN_FILES, compare_ntfs_names, count);
}

/* --- the profile --- */

void kp_profile_init(KpProfile *p)
{
	memset(p, 0, sizeof *p);
	kp_campaign_new_profile(&p->unnamed);
	p->video.gamma = KP_UI_GAMMA_DEFAULT;
	p->video.portraits = KP_UI_PORTRAITS_ANIMATED;
	p->video.graphics = p->video.fog = p->video.water = p->video.font = p->video.lighting = KP_VIDEO_AUTO;
	p->video.renderer = KP_VIDEO_AUTO;
	p->options = KP_UI_OPTION_EXPANSION_INTRO | KP_UI_OPTIONS_DEFAULT;
	p->settings.music = KP_UI_MUSIC_VOLUME;
	p->settings.effects = KP_UI_EFFECTS_VOLUME;
	p->settings.mouse_scroll = p->settings.key_scroll = KP_UI_SCROLL_SPEED;
	p->settings.multi_mouse_scroll = p->settings.multi_key_scroll = KP_UI_SCROLL_SPEED;
	p->settings.game_speed = KP_DEFAULT_SPEED;
}

static int same_name(const char *a, const char *b);
static void remove_at(KpProfile *p, int k);

/* The ID of name (compared without case): its index, -1 none. */
static int character_at(const KpProfile *p, const char *name)
{
	int k;
	for (k = 0; name[0] && k < p->character_count; k++) {
		if (same_name(p->characters[k].name, name)) return k;
	}
	return -1;
}

static KpProfileCharacter *character(KpProfile *p, const char *name)
{
	int k = character_at(p, name);
	return k >= 0 ? &p->characters[k] : NULL;
}

KpCampaignProfile *kp_profile_progress(KpProfile *p)
{
	KpProfileCharacter *c = character(p, p->name);
	return c ? &c->progress : &p->unnamed;
}

int kp_profile_login_list(const KpProfile *p, KpUiProfile *list, int max)
{
	int k, n = 0;
	int64_t words[PROGRESS_WORDS];
	for (k = 0; k < p->character_count && n < max; k++, n++) {
		const KpProfileCharacter *c = &p->characters[k];
		int w;
		memset(&list[n], 0, sizeof list[n]);
		snprintf(list[n].name, sizeof list[n].name, "%s", c->name);
		list[n].time = c->time;
		kp_profile_words(&c->progress, words);
		for (w = 0; w < PROGRESS_WORDS; w++) list[n].progress[w] = (uint32_t)words[w];
	}
	return n;
}

void kp_profile_from_login_list(KpProfile *p, const KpUiProfile *list, int count, const char *player)
{
	int k, w;
	int64_t words[PROGRESS_WORDS];
	/* the list holds every ID with its progress: the profile's copied in, the menus' raises, the new ones' none */
	p->character_count = 0;
	for (k = 0; k < count && k < KP_PROFILE_CHARACTERS; k++) {
		KpProfileCharacter *c = &p->characters[p->character_count++];
		snprintf(c->name, sizeof c->name, "%s", list[k].name);
		c->time = list[k].time;
		for (w = 0; w < PROGRESS_WORDS; w++) words[w] = list[k].progress[w];
		kp_profile_from_words(&c->progress, words);
	}
	for (k = p->save_count - 1; k >= 0; k--) { /* a deleted ID's saves (the host deleted their files at the delete) */
		if (p->saves[k].owner[0] && !character(p, p->saves[k].owner)) remove_at(p, k);
	}
	if (player && player[0]) snprintf(p->name, sizeof p->name, "%s", player);
	if (!character(p, p->name)) p->name[0] = 0;
}

/* A video option read back: within KP_VIDEO_AUTO..last, else auto. */
static int32_t video_choice(int32_t v, int32_t last) { return v >= KP_VIDEO_AUTO && v <= last ? v : KP_VIDEO_AUTO; }

static int32_t video_kept(int given, int32_t run, int32_t flag, int32_t profile)
{
	return given && run == flag ? profile : run;
}

void kp_profile_commit_video(KpVideoSettings *profile, const KpVideoSettings *run, const KpVideoSettings *flags,
	int given)
{
	profile->gamma = run->gamma;
	profile->portraits = run->portraits;
	profile->graphics = video_kept(given & KP_VIDEO_GIVEN_GRAPHICS, run->graphics, flags->graphics, profile->graphics);
	profile->fog = video_kept(given & KP_VIDEO_GIVEN_FOG, run->fog, flags->fog, profile->fog);
	profile->water = video_kept(given & KP_VIDEO_GIVEN_WATER, run->water, flags->water, profile->water);
	profile->font = video_kept(given & KP_VIDEO_GIVEN_FONT, run->font, flags->font, profile->font);
	profile->lighting = video_kept(given & KP_VIDEO_GIVEN_LIGHTING, run->lighting, flags->lighting, profile->lighting);
	profile->renderer = video_kept(given & KP_VIDEO_GIVEN_RENDERER, run->renderer, flags->renderer, profile->renderer);
}

void kp_profile_merge(KpCampaignProfile *p, const KpCampaignProfile *other)
{
	int r;
	for (r = 0; r < KP_CAMPAIGN_RACES; r++) {
		if (other->original[r] > p->original[r]) p->original[r] = other->original[r];
		if (other->expansion[r] > p->expansion[r]) p->expansion[r] = other->expansion[r];
	}
}

void kp_profile_words(const KpCampaignProfile *p, int64_t words[6])
{
	int r;
	for (r = 0; r < KP_PROFILE_WORD_RACES; r++) {
		words[r] = p->original[r];
		words[r + KP_PROFILE_WORD_RACES] = p->expansion[r];
	}
}

void kp_profile_from_words(KpCampaignProfile *p, const int64_t words[6])
{
	int r;
	kp_campaign_new_profile(p);
	for (r = 0; r < KP_PROFILE_WORD_RACES; r++) {
		p->original[r] = words[r];
		p->expansion[r] = words[r + KP_PROFILE_WORD_RACES];
	}
}

/* PROF schema 6: NAME (the logged-in ID's bytes), CHAR (u16 count, per ID u8 length + name, u32 time, u32 per race of
 * the original's progress, then of the expansion's; in the list's order), SAVE (u16 count, per save u8 length + its
 * ID's name, u8 length + its name, newest first), VOPT (the video options: the gamma, the portraits, the art, the fog,
 * the water, the text, the lighting, the renderer, an s16 each; optional, a profile before it has the defaults, one
 * before the lighting or the renderer their Auto), OPTS (the options word, u32: the intro's bit and the check boxes', KP_UI_OPTIONS_KEPT; a
 * profile before schema 4 has a new one's, one of schema 4 its intro's bit and a new one's check boxes), SOPT (the
 * options of the sound and speed dialogs and the tips, u32 each: the music, the effects, the mouse and key scroll speeds,
 * the game speed, the tip, the CPU throttle; optional, a value absent or out of range has a new profile's), LANG (the
 * languages the player chose, u8 length + ASCII ID each: the text's, then the speech's; "" none; optional, schema 8,
 * a language that is not an ID of letters is none), CUST (Create Game's registry values, u8 length + UTF-8 each: Play
 * Custom's game type, then subtype; since schema 10 the multiplayer game type, then speed; since schema 11 the join
 * screen's host address; optional, schema 9, a damaged one or a value that is not UTF-8 below 32 bytes (the speed's and
 * the address's 64) is none). Schema 1 had
 * PROG (the progress) for the one ID NAME; schemas 1 and 2 had SAVE without the IDs (u8 length + name each), one list
 * for every ID. */
int kp_profile_encode(const KpProfile *p, uint8_t **out, size_t *size)
{
	Buf chars, saves, video, options, settings, languages, custom;
	KpKpfOut sections[PROFILE_SECTIONS];
	int k, rc;
	memset(&custom, 0, sizeof custom);
	put8(&custom, (uint32_t)strlen(p->custom_type));
	put(&custom, p->custom_type, strlen(p->custom_type));
	put8(&custom, (uint32_t)strlen(p->game_subtype));
	put(&custom, p->game_subtype, strlen(p->game_subtype));
	put8(&custom, (uint32_t)strlen(p->net_type));
	put(&custom, p->net_type, strlen(p->net_type));
	put8(&custom, (uint32_t)strlen(p->game_speed));
	put(&custom, p->game_speed, strlen(p->game_speed));
	put8(&custom, (uint32_t)strlen(p->lan_host));
	put(&custom, p->lan_host, strlen(p->lan_host));
	memset(&chars, 0, sizeof chars);
	memset(&saves, 0, sizeof saves);
	memset(&video, 0, sizeof video);
	memset(&options, 0, sizeof options);
	memset(&settings, 0, sizeof settings);
	memset(&languages, 0, sizeof languages);
	put8(&languages, (uint32_t)strlen(p->text_locale));
	put(&languages, p->text_locale, strlen(p->text_locale));
	put8(&languages, (uint32_t)strlen(p->speech_locale));
	put(&languages, p->speech_locale, strlen(p->speech_locale));
	put32(&settings, p->settings.music);
	put32(&settings, p->settings.effects);
	put32(&settings, p->settings.mouse_scroll);
	put32(&settings, p->settings.key_scroll);
	put32(&settings, p->settings.game_speed);
	put32(&settings, p->settings.tip);
	put32(&settings, p->settings.cpu_throttle);
	put32(&settings, p->settings.multi_mouse_scroll); /* appended: an older SOPT keeps the defaults */
	put32(&settings, p->settings.multi_key_scroll);
	put32(&options, p->options);
	put16(&video, (uint32_t)(uint16_t)p->video.gamma);
	put16(&video, (uint32_t)(uint16_t)p->video.portraits);
	put16(&video, (uint32_t)(uint16_t)p->video.graphics);
	put16(&video, (uint32_t)(uint16_t)p->video.fog);
	put16(&video, (uint32_t)(uint16_t)p->video.water);
	put16(&video, (uint32_t)(uint16_t)p->video.font);
	put16(&video, (uint32_t)(uint16_t)p->video.lighting);
	put16(&video, (uint32_t)(uint16_t)p->video.renderer);
	put16(&chars, (uint32_t)p->character_count);
	for (k = 0; k < p->character_count; k++) {
		const KpProfileCharacter *c = &p->characters[k];
		int64_t words[PROGRESS_WORDS];
		size_t n = strlen(c->name);
		int w;
		put8(&chars, (uint32_t)n);
		put(&chars, c->name, n);
		put32(&chars, c->time);
		kp_profile_words(&c->progress, words);
		for (w = 0; w < PROGRESS_WORDS; w++) put32(&chars, (uint32_t)words[w]);
	}
	put16(&saves, (uint32_t)p->save_count);
	for (k = 0; k < p->save_count; k++) {
		size_t o = strlen(p->saves[k].owner), n = strlen(p->saves[k].name);
		put8(&saves, (uint32_t)o);
		put(&saves, p->saves[k].owner, o);
		put8(&saves, (uint32_t)n);
		put(&saves, p->saves[k].name, n);
	}
	sections[0].fourcc = "NAME";
	sections[0].payload = (const uint8_t *)p->name;
	sections[0].size = strlen(p->name);
	sections[1].fourcc = "CHAR";
	sections[1].payload = chars.b;
	sections[1].size = chars.n;
	sections[2].fourcc = "SAVE";
	sections[2].payload = saves.b;
	sections[2].size = saves.n;
	sections[3].fourcc = "VOPT";
	sections[3].payload = video.b;
	sections[3].size = video.n;
	sections[4].fourcc = "OPTS";
	sections[4].payload = options.b;
	sections[4].size = options.n;
	sections[5].fourcc = "SOPT";
	sections[5].payload = settings.b;
	sections[5].size = settings.n;
	sections[6].fourcc = "LANG";
	sections[6].payload = languages.b;
	sections[6].size = languages.n;
	sections[7].fourcc = "CUST";
	sections[7].payload = custom.b;
	sections[7].size = custom.n;
	for (k = 0; k < PROFILE_SECTIONS; k++) sections[k].schema_version = 1;
	rc = chars.failed || saves.failed || video.failed || options.failed || settings.failed || languages.failed
		|| custom.failed ? -1
		: kp_kpf_build(PROFILE_KIND, PROFILE_SCHEMA, 0, PROFILE_ID, NULL, sections, PROFILE_SECTIONS, out, size);
	free(chars.b);
	free(saves.b);
	free(video.b);
	free(options.b);
	free(settings.b);
	free(languages.b);
	free(custom.b);
	return rc;
}

/* A u8 length and its bytes at *at of s: -1 when they pass its end or do not fit out. */
static int read_name(const KpKpfSection *s, size_t *at, char *out, size_t size)
{
	uint32_t n;
	if (*at + 1 > s->size || *at + 1 + (n = s->data[*at]) > s->size || n >= size) return -1;
	memcpy(out, s->data + *at + 1, n);
	out[n] = 0;
	*at += 1 + n;
	return 0;
}

/* A name of a profile before schema 7 to UTF-8. */
static void name_from_legacy(char *name, size_t size)
{
	char legacy[KP_PROFILE_SAVE_NAME];
	snprintf(legacy, sizeof legacy, "%s", name);
	(void)kp_text_from_legacy(legacy, name, size);
}

static int is_ascii(const char *s)
{
	for (; *s != 0; s++) {
		if ((uint8_t)*s >= KP_TEXT_ASCII_END) return 0;
	}
	return 1;
}

/* A language's ID of LANG at *at into out ("" when it is not 2..7 letters): -1 when it passes the section's end. */
static int read_locale_id(const KpKpfSection *s, size_t *at, char out[KP_PROFILE_LOCALE])
{
	char id[256];
	size_t k;
	if (read_name(s, at, id, sizeof id) != 0) return -1;
	out[0] = 0;
	if (strlen(id) < 2 || strlen(id) >= KP_PROFILE_LOCALE) return 0;
	for (k = 0; id[k] != 0; k++) {
		if (!((id[k] >= 'a' && id[k] <= 'z') || (id[k] >= 'A' && id[k] <= 'Z'))) return 0;
	}
	memcpy(out, id, k + 1);
	return 0;
}

/* A CUST value: UTF-8 below size bytes, else none (out ""); -1 for a damaged section. */
static int read_custom_value(const KpKpfSection *s, size_t *at, char *out, size_t size)
{
	char value[256];
	if (read_name(s, at, value, sizeof value) != 0) return -1;
	out[0] = 0;
	if (strlen(value) < size && kp_text_valid_utf8((const uint8_t *)value, strlen(value))) {
		memcpy(out, value, strlen(value) + 1);
	}
	return 0;
}

/* SOPT's value k into *value when it is there and at most max (as LoadRegistryOptions 0x4ce5a0, every range from 0). */
static void sopt_value(const KpKpfSection *s, int k, uint32_t max, uint32_t *value)
{
	if (s != NULL && s->size >= 4 * (size_t)(k + 1) && kp_le_u32(s->data + 4 * k) <= max) {
		*value = kp_le_u32(s->data + 4 * k);
	}
}

int kp_profile_decode(KpProfile *p, const uint8_t *data, size_t size, KpError *err)
{
	KpKpf c;
	KpKpfExpect e;
	const KpKpfSection *name, *saves, *video, *options, *settings, *languages, *custom;
	size_t at = 2;
	int k, rc = -1;
	kp_profile_init(p);
	if (kp_kpf_parse(&c, data, size, "profile", 1, err) != 0) return -1;
	memset(&e, 0, sizeof e);
	e.kind = PROFILE_KIND; /* schema 1..PROFILE_SCHEMA, below */
	if (kp_kpf_check(&c, &e, err) != 0 || !(name = kp_kpf_require(&c, "NAME", err))
		|| !(saves = kp_kpf_require(&c, "SAVE", err))) {
		goto done;
	}
	if (c.schema_version < 1 || c.schema_version > PROFILE_SCHEMA) {
		snprintf(err->msg, sizeof err->msg, "profile: schema version %u, this build reads 1..%u",
			(unsigned)c.schema_version, (unsigned)PROFILE_SCHEMA);
		goto done;
	}
	if (name->size >= sizeof p->name || saves->size < 2) {
		err_msg(err, "profile: damaged");
		goto done;
	}
	memcpy(p->name, name->data, name->size);
	if (c.schema_version == 1) { /* the one ID */
		const KpKpfSection *prog = kp_kpf_require(&c, "PROG", err);
		KpCampaignProfile *into = p->name[0] ? &p->characters[0].progress : &p->unnamed;
		if (!prog) goto done;
		if (prog->size != 8 * KP_CAMPAIGN_RACES) {
			err_msg(err, "profile: damaged");
			goto done;
		}
		if (p->name[0]) {
			snprintf(p->characters[0].name, sizeof p->characters[0].name, "%s", p->name);
			p->character_count = 1;
		}
		for (k = 0; k < KP_CAMPAIGN_RACES; k++) {
			into->original[k] = kp_le_u32(prog->data + 4 * k);
			into->expansion[k] = kp_le_u32(prog->data + 4 * (KP_CAMPAIGN_RACES + k));
		}
	} else {
		const KpKpfSection *chars = kp_kpf_require(&c, "CHAR", err);
		size_t from = 2;
		if (!chars) goto done;
		if (chars->size < 2 || kp_le_u16(chars->data) > KP_PROFILE_CHARACTERS) {
			err_msg(err, "profile: damaged");
			goto done;
		}
		for (k = 0; k < (int)kp_le_u16(chars->data); k++) {
			KpProfileCharacter *ch = &p->characters[k];
			int64_t words[PROGRESS_WORDS];
			uint32_t n;
			int w;
			if (from + 1 > chars->size || (n = chars->data[from]) >= sizeof ch->name
				|| from + 1 + n + CHAR_FIXED > chars->size) {
				err_msg(err, "profile: damaged");
				goto done;
			}
			memcpy(ch->name, chars->data + from + 1, n);
			ch->name[n] = 0;
			from += 1 + n;
			ch->time = kp_le_u32(chars->data + from);
			for (w = 0; w < PROGRESS_WORDS; w++) words[w] = kp_le_u32(chars->data + from + 4 + 4 * w);
			kp_profile_from_words(&ch->progress, words);
			from += CHAR_FIXED;
			p->character_count = k + 1;
		}
	}
	for (k = 0; k < (int)kp_le_u16(saves->data) && k < KP_PROFILE_SAVE_POOL; k++) {
		KpProfileSave *s = &p->saves[k];
		if (c.schema_version >= 3 && read_name(saves, &at, s->owner, sizeof s->owner) != 0) {
			err_msg(err, "profile: damaged");
			goto done;
		}
		if (read_name(saves, &at, s->name, sizeof s->name) != 0) {
			err_msg(err, "profile: damaged");
			goto done;
		}
		p->save_count = k + 1;
	}
	if (c.schema_version < PROFILE_NAMES_UTF8) {
		name_from_legacy(p->name, sizeof p->name);
		for (k = 0; k < p->character_count; k++) name_from_legacy(p->characters[k].name, sizeof p->characters[k].name);
		for (k = 0; k < p->save_count; k++) {
			KpProfileSave *s = &p->saves[k];
			if (!is_ascii(s->owner) || !is_ascii(s->name)) p->old_save_files = KP_SAVE_FILES_BYTES;
			name_from_legacy(s->owner, sizeof s->owner);
			name_from_legacy(s->name, sizeof s->name);
		}
	}
	if (c.schema_version < 3 && p->save_count > 0) {
		/* the one list's saves go to the logged-in ID, else the first, else none's; their files get the new names */
		const char *owner = character(p, p->name) ? p->name : p->character_count > 0 ? p->characters[0].name : "";
		for (k = 0; k < p->save_count; k++) snprintf(p->saves[k].owner, sizeof p->saves[k].owner, "%s", owner);
		p->old_save_files = KP_SAVE_FILES_BEFORE_IDS;
	}
	if ((video = kp_kpf_find(&c, "VOPT")) != NULL && video->size >= 2 * VOPT_VALUES) {
		const uint8_t *v = video->data;
		int32_t gamma = (int16_t)kp_le_u16(v);
		int32_t portraits = (int16_t)kp_le_u16(v + 2);
		if (gamma >= KP_UI_GAMMA_MIN && gamma <= KP_UI_GAMMA_MAX) p->video.gamma = gamma;
		if (portraits >= KP_UI_PORTRAITS_NONE && portraits <= KP_UI_PORTRAITS_ANIMATED) p->video.portraits = portraits;
		p->video.graphics = video_choice((int16_t)kp_le_u16(v + 4), KP_GRAPHICS_HD);
		p->video.fog = video_choice((int16_t)kp_le_u16(v + 6), KP_FOG_SMOOTH);
		p->video.water = video_choice((int16_t)kp_le_u16(v + 8), KP_WATER_ANIMATED);
		p->video.font = video_choice((int16_t)kp_le_u16(v + 10), KP_VTEXT_VECTOR);
		if (video->size >= 2 * (VOPT_VALUES + 1)) { /* the lighting, appended */
			p->video.lighting = video_choice((int16_t)kp_le_u16(v + 12), KP_LIGHTING_ON);
		}
		if (video->size >= 2 * (VOPT_VALUES + 2)) { /* the renderer, appended (ADR 0028) */
			p->video.renderer = video_choice((int16_t)kp_le_u16(v + 14), KP_RENDERER_GPU);
		}
	}
	if ((options = kp_kpf_find(&c, "OPTS")) != NULL && options->size >= 4) {
		uint32_t bits = kp_le_u32(options->data);
		/* schema 4 kept the intro's bit only (and a bit of a build before this one, the cinematics' subtitles hidden) */
		p->options = c.schema_version >= 5 ? bits
			: (bits & KP_UI_OPTION_EXPANSION_INTRO) | (p->options & ~KP_UI_OPTION_EXPANSION_INTRO);
	}
	settings = kp_kpf_find(&c, "SOPT");
	sopt_value(settings, 0, KP_UI_VOLUME_MAX, &p->settings.music);
	sopt_value(settings, 1, KP_UI_VOLUME_MAX, &p->settings.effects);
	sopt_value(settings, 2, KP_UI_SPEED_MAX, &p->settings.mouse_scroll);
	sopt_value(settings, 3, KP_UI_SPEED_MAX, &p->settings.key_scroll);
	sopt_value(settings, 4, KP_UI_SPEED_MAX, &p->settings.game_speed);
	sopt_value(settings, 5, SOPT_ANY, &p->settings.tip);
	sopt_value(settings, 6, SOPT_ANY, &p->settings.cpu_throttle);
	sopt_value(settings, 7, KP_UI_SPEED_MAX, &p->settings.multi_mouse_scroll);
	sopt_value(settings, 8, KP_UI_SPEED_MAX, &p->settings.multi_key_scroll);
	p->settings.cpu_throttle = p->settings.cpu_throttle != 0; /* 0x49f180: non-zero is on */
	if ((languages = kp_kpf_find(&c, "LANG")) != NULL) { /* a damaged one: no choice, the rest stays */
		size_t from = 0;
		if (read_locale_id(languages, &from, p->text_locale) != 0
			|| read_locale_id(languages, &from, p->speech_locale) != 0) {
			p->text_locale[0] = p->speech_locale[0] = 0;
		}
	}
	if ((custom = kp_kpf_find(&c, "CUST")) != NULL) { /* a damaged one: no choice, the rest stays */
		size_t from = 0;
		if (read_custom_value(custom, &from, p->custom_type, sizeof p->custom_type) != 0
			|| read_custom_value(custom, &from, p->game_subtype, sizeof p->game_subtype) != 0) {
			p->custom_type[0] = p->game_subtype[0] = 0;
		} else if (c.schema_version >= 10 && (read_custom_value(custom, &from, p->net_type, sizeof p->net_type) != 0
			|| read_custom_value(custom, &from, p->game_speed, sizeof p->game_speed) != 0)) {
			p->net_type[0] = p->game_speed[0] = 0;
		} else if (c.schema_version >= 11 && read_custom_value(custom, &from, p->lan_host, sizeof p->lan_host) != 0) {
			p->lan_host[0] = 0;
		}
	}
	rc = 0;
done:
	kp_kpf_free(&c);
	if (rc != 0) kp_profile_init(p);
	return rc;
}

static unsigned char file_lower(unsigned char ch) { return ch >= 'A' && ch <= 'Z' ? (unsigned char)(ch + 32) : ch; }

static int file_plain(unsigned char ch) { return (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9'); }

/* name at out[n..] in lower case, every byte but a letter or digit as _XX; a part past FILE_PART_MAX as _h and its hash
 * (the filesystems' limit on a name, with an owner's beside it; "_h" is no byte's escape): the new n */
static size_t file_part(const char *name, char *out, size_t n, size_t size)
{
	static const char HEX[] = "0123456789abcdef";
	const unsigned char *s;
	size_t length = 0;
	for (s = (const unsigned char *)name; *s; s++) length += file_plain(file_lower(*s)) ? 1 : 3;
	if (length > FILE_PART_MAX) {
		uint64_t hash = FNV_BASIS;
		int shift;
		for (s = (const unsigned char *)name; *s; s++) hash = (hash ^ file_lower(*s)) * FNV_PRIME;
		if (n + 2 + 16 + 1 > size) return n;
		out[n++] = '_';
		out[n++] = 'h';
		for (shift = 60; shift >= 0; shift -= 4) out[n++] = HEX[hash >> shift & 15];
		return n;
	}
	for (s = (const unsigned char *)name; *s && n + 4 < size; s++) {
		unsigned char ch = file_lower(*s);
		if (file_plain(ch)) {
			out[n++] = (char)ch;
		} else {
			out[n++] = '_';
			out[n++] = HEX[ch >> 4];
			out[n++] = HEX[ch & 15];
		}
	}
	return n;
}

static void save_file(const char *owner, const char *name, char *out, size_t size)
{
	size_t n = 0;
	const char *prefix = "save_", *ext = ".kpsave";
	if (size == 0) return;
	while (*prefix && n + 1 < size) out[n++] = *prefix++;
	if (owner) {
		n = file_part(owner, out, n, size);
		if (n + 1 < size) out[n++] = '-';
	}
	n = file_part(name, out, n, size);
	while (*ext && n + 1 < size) out[n++] = *ext++;
	out[n] = 0;
}

void kp_save_file_name(const char *owner, const char *name, char *out, size_t size) { save_file(owner, name, out, size); }

void kp_save_old_file_name(const char *name, char *out, size_t size) { save_file(NULL, name, out, size); }

static int same_name(const char *a, const char *b)
{
	for (; *a && *b; a++, b++) {
		char x = *a >= 'A' && *a <= 'Z' ? (char)(*a + 32) : *a, y = *b >= 'A' && *b <= 'Z' ? (char)(*b + 32) : *b;
		if (x != y) return 0;
	}
	return *a == *b;
}

const char *kp_profile_save_owner(const KpProfile *p) { return character_at(p, p->name) >= 0 ? p->name : ""; }

static void remove_at(KpProfile *p, int k)
{
	memmove(&p->saves[k], &p->saves[k + 1], sizeof p->saves[0] * (size_t)(p->save_count - k - 1));
	p->save_count--;
}

int kp_profile_save_list(const KpProfile *p, char (*names)[KP_PROFILE_SAVE_NAME], int max)
{
	const char *owner = kp_profile_save_owner(p);
	int k, n = 0;
	for (k = 0; k < p->save_count && n < max; k++) {
		if (same_name(p->saves[k].owner, owner)) snprintf(names[n++], sizeof names[0], "%s", p->saves[k].name);
	}
	return n;
}

void kp_profile_add_save(KpProfile *p, const char *name)
{
	const char *owner = kp_profile_save_owner(p);
	int k, own = 0, oldest = -1;
	for (k = 0; k < p->save_count; k++) {
		if (!same_name(p->saves[k].owner, owner)) continue;
		if (same_name(p->saves[k].name, name)) return;
		own++;
		oldest = k;
	}
	if (own >= KP_PROFILE_SAVES || p->save_count >= KP_PROFILE_SAVE_POOL) {
		remove_at(p, oldest >= 0 ? oldest : p->save_count - 1); /* the ID's oldest, else the pool's */
	}
	memmove(&p->saves[1], &p->saves[0], sizeof p->saves[0] * (size_t)p->save_count);
	snprintf(p->saves[0].owner, sizeof p->saves[0].owner, "%s", owner);
	snprintf(p->saves[0].name, sizeof p->saves[0].name, "%s", name);
	p->save_count++;
}

void kp_profile_remove_save(KpProfile *p, const char *name)
{
	const char *owner = kp_profile_save_owner(p);
	int k;
	for (k = p->save_count - 1; k >= 0; k--) {
		if (same_name(p->saves[k].owner, owner) && same_name(p->saves[k].name, name)) remove_at(p, k);
	}
}

void kp_profile_drop_saves(KpProfile *p, const char *owner, void (*each)(void *ud, const KpProfileSave *save),
	void *ud)
{
	int k;
	for (k = p->save_count - 1; k >= 0; k--) {
		if (!same_name(p->saves[k].owner, owner)) continue;
		if (each) each(ud, &p->saves[k]);
		remove_at(p, k);
	}
}

int kp_profile_rename_old_saves(KpProfile *p, int (*rename_file)(void *ud, const char *from, const char *to),
	void *ud)
{
	int k, had = p->old_save_files;
	p->old_save_files = 0;
	for (k = had ? p->save_count - 1 : -1; k >= 0; k--) {
		char from[KP_SAVE_FILE_NAME], to[KP_SAVE_FILE_NAME], owner[KP_PROFILE_NAME], name[KP_PROFILE_SAVE_NAME];
		(void)kp_text_to_legacy(p->saves[k].owner, owner, sizeof owner);
		(void)kp_text_to_legacy(p->saves[k].name, name, sizeof name);
		if (had == KP_SAVE_FILES_BEFORE_IDS) kp_save_old_file_name(name, from, sizeof from);
		else kp_save_file_name(owner, name, from, sizeof from);
		kp_save_file_name(p->saves[k].owner, p->saves[k].name, to, sizeof to);
		if (strcmp(from, to) != 0 && !rename_file(ud, from, to)) remove_at(p, k);
	}
	return had != 0;
}
