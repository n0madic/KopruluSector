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
#include "kp_text.h"

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
#define PROFILE_SCHEMA 7 /* 1: one ID, its progress in PROG; 1 and 2: the saves without their IDs; 1..5: no SOPT; 1..3: no OPTS; 4: OPTS
                          * with the intro's bit only; 1..6: the names in windows-1252, not UTF-8 */
#define PROFILE_NAMES_UTF8 7
#define PROFILE_ID "koprulu"
#define PROFILE_SECTIONS 6     /* NAME, CHAR, SAVE, VOPT, OPTS, SOPT */
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
	kp_campaign_new_profile(&p->unnamed);
	p->video.gamma = KP_UI_GAMMA_DEFAULT;
	p->video.portraits = KP_UI_PORTRAITS_ANIMATED;
	p->video.graphics = p->video.fog = p->video.water = p->video.font = p->video.lighting = KP_VIDEO_AUTO;
	p->options = KP_UI_OPTION_EXPANSION_INTRO | KP_UI_OPTIONS_DEFAULT;
	p->settings.music = KP_UI_MUSIC_VOLUME;
	p->settings.effects = KP_UI_EFFECTS_VOLUME;
	p->settings.mouse_scroll = p->settings.key_scroll = KP_UI_SCROLL_SPEED;
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
 * the water, the text, the lighting, an s16 each; optional, a profile before it has the defaults, one before the
 * lighting its Auto), OPTS (the options word, u32: the intro's bit and the check boxes', KP_UI_OPTIONS_KEPT; a
 * profile before schema 4 has a new one's, one of schema 4 its intro's bit and a new one's check boxes), SOPT (the
 * options of the sound and speed dialogs and the tips, u32 each: the music, the effects, the mouse and key scroll speeds,
 * the game speed, the tip, the CPU throttle; optional, a value absent or out of range has a new profile's). Schema 1 had
 * PROG (the progress) for the one ID NAME; schemas 1 and 2 had SAVE without the IDs (u8 length + name each), one list
 * for every ID. */
int kp_profile_encode(const KpProfile *p, uint8_t **out, size_t *size)
{
	Buf chars, saves, video, options, settings;
	KpKpfOut sections[PROFILE_SECTIONS];
	int k, rc;
	memset(&chars, 0, sizeof chars);
	memset(&saves, 0, sizeof saves);
	memset(&video, 0, sizeof video);
	memset(&options, 0, sizeof options);
	memset(&settings, 0, sizeof settings);
	put32(&settings, p->settings.music);
	put32(&settings, p->settings.effects);
	put32(&settings, p->settings.mouse_scroll);
	put32(&settings, p->settings.key_scroll);
	put32(&settings, p->settings.game_speed);
	put32(&settings, p->settings.tip);
	put32(&settings, p->settings.cpu_throttle);
	put32(&options, p->options);
	put16(&video, (uint32_t)(uint16_t)p->video.gamma);
	put16(&video, (uint32_t)(uint16_t)p->video.portraits);
	put16(&video, (uint32_t)(uint16_t)p->video.graphics);
	put16(&video, (uint32_t)(uint16_t)p->video.fog);
	put16(&video, (uint32_t)(uint16_t)p->video.water);
	put16(&video, (uint32_t)(uint16_t)p->video.font);
	put16(&video, (uint32_t)(uint16_t)p->video.lighting);
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
	for (k = 0; k < PROFILE_SECTIONS; k++) sections[k].schema_version = 1;
	rc = chars.failed || saves.failed || video.failed || options.failed || settings.failed ? -1
		: kp_kpf_build(PROFILE_KIND, PROFILE_SCHEMA, 0, PROFILE_ID, NULL, sections, PROFILE_SECTIONS, out, size);
	free(chars.b);
	free(saves.b);
	free(video.b);
	free(options.b);
	free(settings.b);
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
	const KpKpfSection *name, *saves, *video, *options, *settings;
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
	p->settings.cpu_throttle = p->settings.cpu_throttle != 0; /* 0x49f180: non-zero is on */
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
