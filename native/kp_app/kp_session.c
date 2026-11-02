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

#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

const int32_t KP_CLOCK_SPEED_MS[KP_CLOCK_SPEEDS] = { 167, 111, 83, 67, 56, 48, 42 };

#define CORE_HEADER 12 /* "KPSV", u32 version, u32 the mission name's length */
#define PROFILE_KIND "PROF"
#define PROFILE_SCHEMA 1
#define PROFILE_ID "koprulu"
#define PROFILE_SECTIONS 4 /* NAME, PROG, SAVE, VOPT */
#define VOPT_VALUES 6

/* --- the clock --- */

void kp_clock_init(KpClock *c, int speed, int max_catchup)
{
	memset(c, 0, sizeof *c);
	c->speed = speed >= 1 && speed <= KP_CLOCK_SPEEDS ? speed : KP_CLOCK_DEFAULT_SPEED;
	c->max_catchup = max_catchup > 0 ? max_catchup : KP_CLOCK_DEFAULT_MAX_CATCHUP;
}

int32_t kp_clock_step_ms(const KpClock *c) { return KP_CLOCK_SPEED_MS[c->speed - 1]; }

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
	n = (int)(acc / step);
	if (n > c->max_catchup) {
		double excess = (n - c->max_catchup) * step;
		c->dropped_ms += excess;
		acc -= excess;
		n = c->max_catchup;
	}
	acc -= n * step;
	c->accumulator_ms = acc;
	c->steps += n;
	*alpha = acc / step;
	return n;
}

int kp_clock_advance_wall(KpClock *c, double now, double *alpha)
{
	double last = c->last_wall;
	int had = c->has_wall;
	c->last_wall = now;
	c->has_wall = 1;
	if (kp_clock_stopped(c) || !had) {
		*alpha = c->accumulator_ms / kp_clock_step_ms(c);
		return 0;
	}
	return kp_clock_advance(c, now - last, alpha);
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
	kp_run_turn(f->g, NULL, 0);
}

int kp_session_load(KpSession *s, const KpHostShared *sh, const uint8_t *dat, size_t dat_size, uint8_t *mission,
	size_t mission_size, const char *name)
{
	KpError err;
	memset(s, 0, sizeof *s);
	if (kp_host_game_load(&s->host, sh, dat, dat_size, mission, mission_size, name, &err) != 0) {
		fail(s, err.msg);
		return -1;
	}
	return 0;
}

int kp_session_start(KpSession *s, uint8_t *tileset, size_t tileset_size, const KpSessionStart *start)
{
	KpHostOpts o;
	KpError err;
	FirstTurn first;
	kp_clock_init(&s->clock, start->speed, start->max_catchup);
	kp_host_opts(&o);
	o.events = 1;
	o.speed = game_speed(s->clock.speed);
	o.mission_index = start->mission_index;
	o.cheats = start->cheats;
	o.carry = start->has_carry ? &start->carry : NULL;
	if (kp_host_game_start(&s->host, tileset, tileset_size, start->seed, start->expansion, &o, &err) != 0) {
		fail(s, err.msg);
		return -1;
	}
	s->g = s->host.g;
	first.g = s->g;
	first.start = start;
	(void)kp_session_guard(s, first_turn, &first);
	s->start_frame = kp_scalar(s->g, KP_FRAME);
	s->start_turn = kp_scalar(s->g, KP_TURN);
	return s->failed ? -1 : 0;
}

const KpMission *kp_session_mission(const KpSession *s) { return s->g ? &s->host.data.mission : NULL; }

const KpGrpMeta *kp_session_grp(const KpSession *s) { return s->g ? &s->host.shared->data.grp : NULL; }

const uint8_t *kp_session_tileset(const KpSession *s, size_t *size)
{
	*size = s->g ? s->host.tileset_size : 0;
	return s->g ? s->host.tileset : NULL;
}

void kp_session_free(KpSession *s)
{
	kp_host_game_free(&s->host);
	memset(s, 0, sizeof *s);
}

int kp_session_command(KpSession *s, const uint8_t *bytes, size_t size, int64_t player)
{
	KpQueuedCommand *c;
	if (!s->g || s->pending_count >= KP_SESSION_PENDING || size > KP_SESSION_COMMAND_BYTES) return -1;
	c = &s->pending[s->pending_count++];
	c->player = player < 0 ? s->g->local_player : player;
	c->size = (uint16_t)size;
	memcpy(c->bytes, bytes, size);
	return 0;
}

typedef struct {
	KpGame *g;
	const KpCommand *list;
	int count;
} Frame;

static void run_frame(void *ud)
{
	const Frame *f = (const Frame *)ud;
	kp_run_frame(f->g, f->list, f->count);
}

int kp_session_step(KpSession *s, double (*clock_ms)(void))
{
	KpCommand list[KP_SESSION_PENDING];
	Frame frame;
	double t0, ms;
	int count, k;
	if (s->failed || !s->g) return 0;
	count = s->pending_count;
	for (k = 0; k < count; k++) {
		list[k].player = s->pending[k].player;
		list[k].bytes = s->pending[k].bytes;
		list[k].size = s->pending[k].size;
	}
	s->tick++;
	t0 = clock_ms ? clock_ms() : 0;
	frame.g = s->g;
	frame.list = list;
	frame.count = count;
	(void)kp_session_guard(s, run_frame, &frame);
	/* the commands are the step's (the list points into the queue until the step is done) */
	s->pending_count = 0;
	ms = clock_ms ? clock_ms() - t0 : 0;
	s->step_ms += ms;
	if (ms > s->step_max_ms) s->step_max_ms = ms;
	if (s->failed) return 0;
	/* the game's speed command (ExecGameSpeed 0x473310) sets the speed the clock steps at */
	{
		int64_t speed = kp_scalar(s->g, KP_SPEED);
		if (speed != KP_NIL && game_speed(s->clock.speed) != speed && speed >= 0 && speed < KP_CLOCK_SPEEDS) {
			kp_clock_set_speed(&s->clock, (int)speed + 1);
		}
	}
	return 1;
}

int kp_session_advance(KpSession *s, double now, double *alpha, double (*clock_ms)(void))
{
	int due = kp_clock_advance_wall(&s->clock, now, alpha), ran = 0;
	while (ran < due && kp_session_step(s, clock_ms)) ran++;
	if (s->failed) *alpha = 1;
	return ran;
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

int kp_save_identity(KpSaveIdentity *id, const uint8_t *dat, size_t size, KpError *err)
{
	KpKpf c;
	if (kp_kpf_parse(&c, dat, size, "dat.kpd", 0, err) != 0) return -1;
	memset(id, 0, sizeof *id);
	snprintf(id->profile_id, sizeof id->profile_id, "%s", c.profile_id);
	memcpy(id->source_hash, c.source_hash, 32);
	kp_kpf_free(&c);
	return 0;
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

int kp_save_encode(KpSession *s, const KpSaveIdentity *id, const KpSaveCampaign *camp, const KpSavePresentation *pres,
	uint8_t **out, size_t *size, KpError *err)
{
	Buf cmds, cmp, prs;
	KpKpfOut sections[4];
	uint8_t *core = NULL;
	size_t core_size = 0;
	int k, rc;
	*out = NULL;
	if (!s->g) return err_msg(err, "mission: no game to save");
	if (s->failed) {
		snprintf(err->msg, sizeof err->msg, "mission: the game stopped in a step and cannot be saved: %s", s->error);
		return -1;
	}
	if (kp_save(s->g, s->host.name, s->host.identity, &core, &core_size, err) != 0) return -1;
	memset(&cmds, 0, sizeof cmds);
	memset(&cmp, 0, sizeof cmp);
	memset(&prs, 0, sizeof prs);
	/* CMDS: u32 count, then per command u8 player, u16 size and its bytes */
	put32(&cmds, (uint32_t)s->pending_count);
	for (k = 0; k < s->pending_count; k++) {
		put8(&cmds, (uint32_t)s->pending[k].player);
		put16(&cmds, s->pending[k].size);
		put(&cmds, s->pending[k].bytes, s->pending[k].size);
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
	sections[0].payload = core;
	sections[0].size = core_size;
	sections[1].fourcc = "CMDS";
	sections[1].payload = cmds.b;
	sections[1].size = cmds.n;
	sections[2].fourcc = "CAMP";
	sections[2].payload = cmp.b;
	sections[2].size = cmp.n;
	sections[3].fourcc = "PRES";
	sections[3].payload = prs.b;
	sections[3].size = prs.n;
	for (k = 0; k < 4; k++) sections[k].schema_version = 1;
	rc = cmds.failed || cmp.failed || prs.failed ? -1
		: kp_kpf_build(KP_SAVE_KIND, KP_SAVE_SCHEMA, 0, id->profile_id, id->source_hash, sections, 4, out, size);
	free(core);
	free(cmds.b);
	free(cmp.b);
	free(prs.b);
	if (rc != 0) return err_msg(err, "save: out of memory");
	return 0;
}

/* The parts of a save after checking its container and data set. */
typedef struct {
	KpKpf c;
	const KpKpfSection *core, *cmds, *camp, *pres;
} SaveParts;

static int decode(SaveParts *p, const uint8_t *save, size_t size, const KpSaveIdentity *id, KpError *err)
{
	KpKpfExpect e;
	if (kp_kpf_parse(&p->c, save, size, "save", 1, err) != 0) return -1;
	memset(&e, 0, sizeof e);
	e.kind = KP_SAVE_KIND;
	e.schema_version = KP_SAVE_SCHEMA;
	e.profile_id = id->profile_id;
	e.source_hash = id->source_hash;
	if (kp_kpf_check(&p->c, &e, err) != 0 || !(p->core = kp_kpf_require(&p->c, "CORE", err))
		|| !(p->cmds = kp_kpf_require(&p->c, "CMDS", err)) || !(p->camp = kp_kpf_require(&p->c, "CAMP", err))
		|| !(p->pres = kp_kpf_require(&p->c, "PRES", err))) {
		kp_kpf_free(&p->c);
		return -1;
	}
	return 0;
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
	uint32_t n;
	int rc = 0;
	if (decode(&p, save, size, id, err) != 0) return -1;
	if (p.core->size < CORE_HEADER || memcmp(p.core->data, "KPSV", 4) != 0) {
		rc = err_msg(err, "save: no native save");
	} else if (p.core->size < CORE_HEADER + (n = kp_le_u32(p.core->data + 8)) || n >= mission_size) {
		rc = err_msg(err, "save: truncated mission name");
	} else {
		memcpy(mission, p.core->data + CORE_HEADER, n);
		mission[n] = 0;
		if (camp) rc = decode_campaign(p.camp, camp, err);
	}
	kp_kpf_free(&p.c);
	return rc;
}

int kp_save_load(KpSession *s, const uint8_t *save, size_t size, const KpSaveIdentity *id, KpSaveCampaign *camp,
	KpSavePresentation *pres, KpError *err)
{
	SaveParts p;
	KpQueuedCommand cmds[KP_SESSION_PENDING];
	uint32_t count, k;
	size_t at = 4;
	KpSaveCampaign c;
	if (!s->g) return err_msg(err, "mission: no game to load into");
	if (decode(&p, save, size, id, err) != 0) return -1;
	if (decode_campaign(p.camp, &c, err) != 0) goto failed;
	if (p.cmds->size < 4 || (count = kp_le_u32(p.cmds->data)) > KP_SESSION_PENDING) {
		err_msg(err, "save: bad commands");
		goto failed;
	}
	for (k = 0; k < count; k++) {
		uint32_t n;
		if (at + 3 > p.cmds->size || at + 3 + (n = kp_le_u16(p.cmds->data + at + 1)) > p.cmds->size
			|| n > KP_SESSION_COMMAND_BYTES) {
			err_msg(err, "save: bad commands");
			goto failed;
		}
		cmds[k].player = p.cmds->data[at];
		cmds[k].size = (uint16_t)n;
		memcpy(cmds[k].bytes, p.cmds->data + at + 3, n);
		at += 3 + n;
	}
	if (kp_load(s->g, p.core->data, p.core->size, s->host.name, s->host.identity, err) != 0) goto failed;
	s->failed = 0;
	s->error[0] = 0;
	memcpy(s->pending, cmds, sizeof cmds[0] * count);
	s->pending_count = (int)count;
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

/* --- the profile --- */

void kp_profile_init(KpProfile *p)
{
	memset(p, 0, sizeof *p);
	kp_campaign_new_profile(&p->progress);
	p->video.gamma = KP_UI_GAMMA_DEFAULT;
	p->video.portraits = KP_UI_PORTRAITS_ANIMATED;
	p->video.graphics = p->video.fog = p->video.water = p->video.font = p->video.lighting = KP_VIDEO_AUTO;
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

/* PROF: NAME (the name's bytes), PROG (u32 per race of the original's, then of the expansion's), SAVE (u16 count,
 * per save u8 length + name, newest first), VOPT (the video options: the gamma, the portraits, the art, the fog, the
 * water, the text, the lighting, an s16 each; optional, a profile before it has the defaults, one before the lighting
 * its Auto). */
int kp_profile_encode(const KpProfile *p, uint8_t **out, size_t *size)
{
	Buf prog, saves, video;
	KpKpfOut sections[PROFILE_SECTIONS];
	int k, rc;
	memset(&prog, 0, sizeof prog);
	memset(&saves, 0, sizeof saves);
	memset(&video, 0, sizeof video);
	put16(&video, (uint32_t)(uint16_t)p->video.gamma);
	put16(&video, (uint32_t)(uint16_t)p->video.portraits);
	put16(&video, (uint32_t)(uint16_t)p->video.graphics);
	put16(&video, (uint32_t)(uint16_t)p->video.fog);
	put16(&video, (uint32_t)(uint16_t)p->video.water);
	put16(&video, (uint32_t)(uint16_t)p->video.font);
	put16(&video, (uint32_t)(uint16_t)p->video.lighting);
	for (k = 0; k < KP_CAMPAIGN_RACES; k++) put32(&prog, (uint32_t)p->progress.original[k]);
	for (k = 0; k < KP_CAMPAIGN_RACES; k++) put32(&prog, (uint32_t)p->progress.expansion[k]);
	put16(&saves, (uint32_t)p->save_count);
	for (k = 0; k < p->save_count; k++) {
		size_t n = strlen(p->saves[k]);
		put8(&saves, (uint32_t)n);
		put(&saves, p->saves[k], n);
	}
	sections[0].fourcc = "NAME";
	sections[0].payload = (const uint8_t *)p->name;
	sections[0].size = strlen(p->name);
	sections[1].fourcc = "PROG";
	sections[1].payload = prog.b;
	sections[1].size = prog.n;
	sections[2].fourcc = "SAVE";
	sections[2].payload = saves.b;
	sections[2].size = saves.n;
	sections[3].fourcc = "VOPT";
	sections[3].payload = video.b;
	sections[3].size = video.n;
	for (k = 0; k < PROFILE_SECTIONS; k++) sections[k].schema_version = 1;
	rc = prog.failed || saves.failed || video.failed ? -1
		: kp_kpf_build(PROFILE_KIND, PROFILE_SCHEMA, 0, PROFILE_ID, NULL, sections, PROFILE_SECTIONS, out, size);
	free(prog.b);
	free(saves.b);
	free(video.b);
	return rc;
}

int kp_profile_decode(KpProfile *p, const uint8_t *data, size_t size, KpError *err)
{
	KpKpf c;
	KpKpfExpect e;
	const KpKpfSection *name, *prog, *saves, *video;
	size_t at = 2;
	int k, rc = -1;
	kp_profile_init(p);
	if (kp_kpf_parse(&c, data, size, "profile", 1, err) != 0) return -1;
	memset(&e, 0, sizeof e);
	e.kind = PROFILE_KIND;
	e.schema_version = PROFILE_SCHEMA;
	if (kp_kpf_check(&c, &e, err) != 0 || !(name = kp_kpf_require(&c, "NAME", err))
		|| !(prog = kp_kpf_require(&c, "PROG", err)) || !(saves = kp_kpf_require(&c, "SAVE", err))) {
		goto done;
	}
	if (prog->size != 8 * KP_CAMPAIGN_RACES || name->size >= sizeof p->name || saves->size < 2) {
		err_msg(err, "profile: damaged");
		goto done;
	}
	memcpy(p->name, name->data, name->size);
	for (k = 0; k < KP_CAMPAIGN_RACES; k++) {
		p->progress.original[k] = kp_le_u32(prog->data + 4 * k);
		p->progress.expansion[k] = kp_le_u32(prog->data + 4 * (KP_CAMPAIGN_RACES + k));
	}
	for (k = 0; k < (int)kp_le_u16(saves->data) && k < KP_PROFILE_SAVES; k++) {
		uint32_t n;
		if (at + 1 > saves->size || at + 1 + (n = saves->data[at]) > saves->size || n >= sizeof p->saves[0]) {
			err_msg(err, "profile: damaged");
			goto done;
		}
		memcpy(p->saves[k], saves->data + at + 1, n);
		p->saves[k][n] = 0;
		at += 1 + n;
		p->save_count = k + 1;
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
	}
	rc = 0;
done:
	kp_kpf_free(&c);
	if (rc != 0) kp_profile_init(p);
	return rc;
}

void kp_save_file_name(const char *name, char *out, size_t size)
{
	static const char HEX[] = "0123456789abcdef";
	size_t n = 0;
	const char *prefix = "save_", *ext = ".kpsave";
	const unsigned char *s;
	while (*prefix && n + 1 < size) out[n++] = *prefix++;
	for (s = (const unsigned char *)name; *s && n + 4 < size; s++) {
		unsigned char ch = *s >= 'A' && *s <= 'Z' ? (unsigned char)(*s + 32) : *s;
		if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')) {
			out[n++] = (char)ch;
		} else {
			out[n++] = '_';
			out[n++] = HEX[ch >> 4];
			out[n++] = HEX[ch & 15];
		}
	}
	while (*ext && n + 1 < size) out[n++] = *ext++;
	out[n] = 0;
}

static int same_name(const char *a, const char *b)
{
	for (; *a && *b; a++, b++) {
		char x = *a >= 'A' && *a <= 'Z' ? (char)(*a + 32) : *a, y = *b >= 'A' && *b <= 'Z' ? (char)(*b + 32) : *b;
		if (x != y) return 0;
	}
	return *a == *b;
}

void kp_profile_add_save(KpProfile *p, const char *name)
{
	int k;
	for (k = 0; k < p->save_count; k++) {
		if (same_name(p->saves[k], name)) return;
	}
	if (p->save_count >= KP_PROFILE_SAVES) p->save_count = KP_PROFILE_SAVES - 1;
	memmove(p->saves[1], p->saves[0], sizeof p->saves[0] * (size_t)p->save_count);
	snprintf(p->saves[0], sizeof p->saves[0], "%s", name);
	p->save_count++;
}

void kp_profile_remove_save(KpProfile *p, const char *name)
{
	int k;
	for (k = p->save_count - 1; k >= 0; k--) {
		if (same_name(p->saves[k], name)) {
			memmove(p->saves[k], p->saves[k + 1], sizeof p->saves[0] * (size_t)(p->save_count - k - 1));
			p->save_count--;
		}
	}
}
