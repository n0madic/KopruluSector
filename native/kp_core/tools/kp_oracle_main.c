/*
 * kp_oracle: the oracle tools of the native game (plan phase S, stage S1), the subcommands the Python checkers run.
 * Run from the repository root.
 *
 *   kp_oracle start_state MISSION.kpm SEED EXPANSION [STEP [COMMANDS.jsonl [UI.jsonl]]]
 *       the state lines of the game at the object step (tools/state_check.py; kp_dump)
 *   kp_oracle traces MISSION.kpm SEED EXPANSION STEP [COMMANDS.jsonl [UI.jsonl]]
 *       the trigger trace ("frame player trigger action type result") and the AI trace ("ai frame player slot bin pc op")
 *   kp_oracle play GOLDEN [STEPS] | --mission M.kpm SEED EXPANSION STEPS [COMMANDS [UI]] [--carry-out FILE] |
 *                  --chain GOLDEN...
 *       headless runs to a step or the local player's result, and campaign chains
 *   kp_oracle fog_levels < REQUEST
 *       the fog of one view or the minimap's (tools/fog_check.py's protocol)
 *   kp_oracle export_replay NAME OUT
 *       a scenario golden's KPRP 1 replay for kp_replay and kp_bench
 *   kp_oracle hud_check | input_check | menus_replay | room_replay | result_replay | score_replay ...
 *       the interface's oracles (kp_oracle_cli_ui.c)
 * A campaign chain's game takes the environment's KP_CARRY and KP_TURN (start_state, traces, play --mission).
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kp_oracle_cli.h"
#include "kp_present.h"
#include "../src/core/kp_fog_view.h"

#define RESULT_LIMIT 100000 /* the steps a run "until the result" may take */

int kp_cli_fail(const char *fmt, ...)
{
	va_list ap;
	fprintf(stderr, "kp_oracle: ");
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
	return 1;
}

uint8_t *kp_cli_read(const char *path, size_t *size)
{
	uint8_t *data = kp_read_file(path, size);
	if (!data) {
		kp_cli_fail("cannot read %s", path);
		exit(1);
	}
	return data;
}

/* The game of a mission with the chain context, and its replay of the run's logs (none: an empty replay). */
typedef struct {
	KpOracleGame game;
	KpReplay replay;
	KpCarry carry;
	KpRunner run;
} Run;

static int start_run(Run *r, const char *path, uint32_t seed, int expansion, int trace, const char *commands,
	const char *ui)
{
	KpOracleOpts o;
	KpError err;
	kp_oracle_opts(&o);
	o.trace = trace;
	kp_replay_init(&r->replay);
	if (kp_oracle_chain_opts(&o, &r->carry, &err) != 0) return kp_cli_fail("%s", err.msg);
	if (commands) {
		size_t size;
		uint8_t *text = kp_cli_read(commands, &size);
		int bad = kp_replay_add_commands_jsonl(&r->replay, (const char *)text, size, &err);
		free(text);
		if (bad) return kp_cli_fail("%s: %s", commands, err.msg);
	}
	if (ui) {
		size_t size;
		uint8_t *text = kp_cli_read(ui, &size);
		int bad = kp_replay_add_ui_jsonl(&r->replay, (const char *)text, size, &err);
		free(text);
		if (bad) return kp_cli_fail("%s: %s", ui, err.msg);
	}
	if (kp_oracle_game_start(&r->game, ".", path, seed, expansion, &o, &err) != 0) return kp_cli_fail("%s", err.msg);
	kp_runner_init(&r->run, r->game.g, &r->replay);
	return 0;
}

static void free_run(Run *r)
{
	kp_oracle_game_free(&r->game);
	kp_replay_free(&r->replay);
}

/* Runs fn(ud) under a failure handler of the game's world; 1 with the message printed when it failed. */
static int guarded(KpGame *g, void (*fn)(void *ud), void *ud)
{
	KpTry t;
	volatile int failed = 0;
	kp_images_begin(g->w, &t);
	if (setjmp(t.buf)) {
		failed = 1;
	} else {
		fn(ud);
	}
	kp_images_end(g->w, &t);
	if (failed) return kp_cli_fail("%s", g->w->error);
	return 0;
}

typedef struct {
	KpRunner *run;
	int64_t step;
} RunTo;

static void run_to(void *ud)
{
	RunTo *t = (RunTo *)ud;
	kp_runner_to(t->run, t->step);
}

/* --- start_state --- */

static int cmd_start_state(int argc, char **argv)
{
	Run r;
	RunTo t;
	KpBuf lines;
	int status;
	if (argc < 3) return kp_cli_fail("usage: start_state MISSION.kpm SEED EXPANSION [STEP [COMMANDS.jsonl [UI.jsonl]]]");
	if (start_run(&r, argv[0], (uint32_t)strtoull(argv[1], NULL, 10), atoi(argv[2]), 0, argc > 4 ? argv[4] : NULL,
		argc > 5 ? argv[5] : NULL) != 0) {
		return 1;
	}
	t.run = &r.run;
	t.step = argc > 3 ? strtoll(argv[3], NULL, 10) : 1;
	if ((status = guarded(r.game.g, run_to, &t)) == 0) {
		kp_buf_init(&lines);
		if (kp_state_lines(r.game.g, 0, &lines) != 0) status = kp_cli_fail("out of memory");
		else fwrite(kp_buf_text(&lines), 1, lines.size, stdout);
		kp_buf_free(&lines);
	}
	free_run(&r);
	return status;
}

/* --- traces --- */

typedef struct {
	KpBuf out;
	int count;
} Traces;

static void trace_event(void *ud, const KpEvent *e)
{
	Traces *t = (Traces *)ud;
	char line[160];
	if (e->kind == KP_EVENT_TRIGGER) {
		kp_trace_line(e, line, sizeof line);
	} else if (e->kind == KP_EVENT_AI) {
		strcpy(line, "ai ");
		kp_ai_trace_line(e, line + 3, sizeof line - 3);
	} else {
		return;
	}
	if (t->count++) kp_buf_char(&t->out, '\n');
	kp_buf_str(&t->out, line);
}

static int cmd_traces(int argc, char **argv)
{
	Run r;
	RunTo t;
	Traces traces;
	int status;
	if (argc < 4) return kp_cli_fail("usage: traces MISSION.kpm SEED EXPANSION STEP [COMMANDS.jsonl [UI.jsonl]]");
	if (start_run(&r, argv[0], (uint32_t)strtoull(argv[1], NULL, 10), atoi(argv[2]), 1, argc > 4 ? argv[4] : NULL,
		argc > 5 ? argv[5] : NULL) != 0) {
		return 1;
	}
	kp_buf_init(&traces.out);
	traces.count = 0;
	r.run.on_event = trace_event;
	r.run.ud = &traces;
	t.run = &r.run;
	t.step = strtoll(argv[3], NULL, 10);
	if ((status = guarded(r.game.g, run_to, &t)) == 0) {
		fwrite(kp_buf_text(&traces.out), 1, traces.out.size, stdout);
		if (traces.count > 0) fputc('\n', stdout);
	}
	kp_buf_free(&traces.out);
	free_run(&r);
	return status;
}

/* --- play --- */

/* play(game, run, steps): the run to `steps` (-1: until the local player's result); prints the lines. */
typedef struct {
	KpRunner *run;
	int64_t steps;
	int no_result;
} Play;

static void play_body(void *ud)
{
	Play *p = (Play *)ud;
	KpGame *g = p->run->g;
	int64_t result = 0, limit = p->steps < 0 ? RESULT_LIMIT : p->steps, step;
	KpStateCrcs crcs;
	for (step = 2; step <= limit; step++) {
		int64_t res;
		kp_runner_to(p->run, step);
		res = g->results[g->local_player];
		if (res != 0 && result == 0) {
			result = res;
			printf("result %lld step %lld frame %lld\n", (long long)res, (long long)step, (long long)kp_scalar(g, KP_FRAME));
			if (p->steps < 0) break;
		}
	}
	if (p->steps < 0 && result == 0) {
		p->no_result = 1;
		return;
	}
	printf("step %lld frame %lld\n", (long long)p->run->step, (long long)kp_scalar(g, KP_FRAME));
	if (g->trig.next_scenario[0]) printf("next_scenario %s\n", (const char *)g->trig.next_scenario);
	if (kp_state_crcs(g, p->run->step >= KP_ORACLE_DRAWN_STEP, &crcs) != 0) kp_game_fail(g, "out of memory");
	printf("fingerprint %u\n", (unsigned)kp_state_fingerprint(&crcs));
}

static int play(KpRunner *run, int64_t steps)
{
	Play p;
	p.run = run;
	p.steps = steps;
	p.no_result = 0;
	if (guarded(run->g, play_body, &p) != 0) return 1;
	if (p.no_result) {
		fprintf(stderr, "play: no result within %d steps\n", RESULT_LIMIT);
		exit(1);
	}
	return 0;
}

static int64_t steps_arg(const char *s) { return strcmp(s, "result") == 0 ? -1 : strtoll(s, NULL, 10); }

/* A golden's game and runner. */
static int start_golden(KpGoldenRun *gr, const KpJson *g, const KpOracleOpts *opts)
{
	KpError err;
	if (kp_golden_run_start(gr, ".", g, opts, 0, &err) != 0) return kp_cli_fail("%s", err.msg);
	gr->run.on_event = NULL;
	return 0;
}

typedef struct {
	KpGoldenRun *runs[64];
	int count;
} Chain;

static KpOracleGame *chain_play(void *ud, const KpJson *golden, const char *name, const KpOracleOpts *opts, KpBuf *why)
{
	Chain *c = (Chain *)ud;
	KpGoldenRun *gr = (KpGoldenRun *)malloc(sizeof *gr);
	printf("game %s\n", name);
	if (!gr || c->count >= 64) {
		free(gr);
		kp_buf_str(why, "out of memory");
		return NULL;
	}
	if (start_golden(gr, golden, opts) != 0) exit(1);
	c->runs[c->count++] = gr;
	if (play(&gr->run, -1) != 0) exit(1);
	return &gr->game;
}

static void chain_release(void *ud, KpOracleGame *game)
{
	Chain *c = (Chain *)ud;
	int k;
	for (k = 0; k < c->count; k++) {
		if (c->runs[k] && &c->runs[k]->game == game) {
			kp_golden_run_free(c->runs[k]);
			free(c->runs[k]);
			c->runs[k] = NULL;
		}
	}
}

static void chain_after(void *ud, const char *name, const KpCampaignStep *step, int64_t expected, const KpCampaign *data)
{
	char mission[256];
	(void)ud;
	(void)name;
	if (expected == KP_NIL) printf("next nil ");
	else printf("next %lld ", (long long)expected);
	if (expected != KP_NIL && kp_campaign_mission_name(data, expected, mission, sizeof mission)) printf("%s\n", mission);
	else if (step->kind == KP_CAMPAIGN_STEP_EPILOG) printf("epilog\n");
	else if (step->kind == KP_CAMPAIGN_STEP_MENU) printf("menu\n");
	else printf("glue %lld\n", (long long)step->glue);
}

static int cmd_play(int argc, char **argv)
{
	KpError err;
	if (argc < 1) return kp_cli_fail("usage: play GOLDEN [STEPS] | --mission ... | --chain GOLDEN...");
	if (strcmp(argv[0], "--mission") == 0) {
		const char *carry_out = NULL, *args[8];
		Run r;
		int n = 0, k, status;
		for (k = 1; k < argc; k++) { /* --carry-out FILE anywhere after the mission's arguments */
			if (strcmp(argv[k], "--carry-out") == 0 && k + 1 < argc) carry_out = argv[++k];
			else if (n < 8) args[n++] = argv[k];
		}
		if (n < 4) return kp_cli_fail("usage: play --mission M.kpm SEED EXPANSION STEPS [COMMANDS [UI]] [--carry-out FILE]");
		if (start_run(&r, args[0], (uint32_t)strtoull(args[1], NULL, 10), atoi(args[2]), 0, n > 4 ? args[4] : NULL,
			n > 5 ? args[5] : NULL) != 0) {
			return 1;
		}
		status = play(&r.run, steps_arg(args[3]));
		if (status == 0 && carry_out) {
			KpCarry c;
			FILE *f = fopen(carry_out, "wb");
			kp_carry_take(r.game.g, &c);
			if (!f || fwrite(&c, sizeof c, 1, f) != 1) status = kp_cli_fail("cannot write %s", carry_out);
			if (f) fclose(f);
		}
		free_run(&r);
		return status;
	}
	if (strcmp(argv[0], "--chain") == 0) {
		KpGolden *goldens = (KpGolden *)calloc((size_t)argc, sizeof *goldens);
		KpChainGame *games = (KpChainGame *)calloc((size_t)argc, sizeof *games);
		Chain chain;
		KpBuf failed;
		int k, r;
		if (!goldens || !games) return kp_cli_fail("out of memory");
		memset(&chain, 0, sizeof chain);
		for (k = 1; k < argc; k++) {
			if (kp_golden_load(&goldens[k - 1], argv[k], &err) != 0) return kp_cli_fail("%s", err.msg);
			games[k - 1].golden = goldens[k - 1].root;
			games[k - 1].name = argv[k];
		}
		kp_buf_init(&failed);
		r = kp_campaign_chain(".", games, argc - 1, chain_play, chain_after, chain_release, &chain, &failed, &err);
		if (r < 0) return kp_cli_fail("%s", err.msg);
		if (r > 0) {
			fprintf(stderr, "play: %s\n", kp_buf_text(&failed));
			return 1;
		}
		for (k = 0; k < argc - 1; k++) kp_golden_free(&goldens[k]);
		free(goldens);
		free(games);
		kp_buf_free(&failed);
		return 0;
	}
	{
		KpGolden g;
		KpGoldenRun gr;
		int status;
		if (kp_golden_load(&g, argv[0], &err) != 0) return kp_cli_fail("%s", err.msg);
		if (start_golden(&gr, g.root, NULL) != 0) return 1;
		status = play(&gr.run, argc > 1 ? steps_arg(argv[1]) : gr.last);
		kp_golden_run_free(&gr);
		kp_golden_free(&g);
		return status;
	}
}

/* --- fog_levels --- */

/* stdin's bytes. */
static uint8_t *read_stdin(size_t *size)
{
	size_t cap = 1 << 20, n = 0, got;
	uint8_t *data = (uint8_t *)malloc(cap);
	while (data && (got = fread(data + n, 1, cap - n, stdin)) > 0) {
		n += got;
		if (n == cap) {
			uint8_t *bigger = (uint8_t *)realloc(data, cap * 2);
			if (!bigger) {
				free(data);
				return NULL;
			}
			data = bigger;
			cap *= 2;
		}
	}
	*size = n;
	return data;
}

/* The request's u32 tile flags as the vision's tiles. */
static int64_t *tile_flags(const uint8_t *raw, int64_t count)
{
	int64_t *tiles = (int64_t *)malloc(sizeof *tiles * (size_t)(count > 0 ? count : 1)), k;
	for (k = 0; tiles && k < count; k++) tiles[k] = (int64_t)kp_le_u32(raw + 4 * k);
	return tiles;
}

static int cmd_fog_levels(int argc, char **argv)
{
	size_t size, nl;
	uint8_t *input = read_stdin(&size);
	int64_t f[16];
	int n = 0;
	const char *p;
	KpFogVision vision;
	(void)argc;
	(void)argv;
	if (!input) return kp_cli_fail("out of memory");
	for (nl = 0; nl < size && input[nl] != '\n'; nl++) {
	}
	if (nl >= size) return kp_cli_fail("fog_levels: no request line");
	input[nl] = '\0';
	for (p = (const char *)input; *p && n < 16;) { /* the line's numbers ("%d+") */
		if (*p >= '0' && *p <= '9') {
			char *end;
			f[n++] = strtoll(p, &end, 10);
			p = end;
		} else {
			p++;
		}
	}
	memset(&vision, 0, sizeof vision);
	if (strncmp((const char *)input, "minimap", 7) == 0) {
		int64_t map_w = f[0], map_h = f[1], terrain_size = f[8], w, h;
		size_t at = nl + 1;
		uint8_t *out;
		if (n < 9 || at + 4 * (size_t)(map_w * map_h) + (size_t)terrain_size + 32 * 256 > size) return kp_cli_fail("fog_levels: a short request");
		vision.tiles = tile_flags(input + at, map_w * map_h);
		vision.map_w = map_w;
		vision.map_h = map_h;
		vision.not_visible = f[2];
		vision.not_explored = f[3];
		vision.replay = f[4] != 0;
		vision.replay_vision = f[5];
		vision.replay_reveal = f[6];
		at += 4 * (size_t)(map_w * map_h);
		out = (uint8_t *)malloc((size_t)terrain_size + 1);
		if (!vision.tiles || !out) return kp_cli_fail("out of memory");
		if (kp_fog_minimap(&vision, input + at, input + at + terrain_size, (int)f[7], out, &w, &h) != 0) {
			return kp_cli_fail("fog view: no minimap for a map side of %lld", (long long)(map_w > map_h ? map_w : map_h));
		}
		fwrite(out, 1, (size_t)terrain_size, stdout);
		free(out);
		free((void *)vision.tiles);
		free(input);
		return 0;
	}
	{
		int64_t cam_x = f[0], cam_y = f[1], w = f[2], h = f[3], map_w = f[4], map_h = f[5], rect[4];
		size_t at = nl + 1;
		KpFogView *view = (KpFogView *)malloc(sizeof *view);
		uint8_t *pixels;
		const uint8_t *dark;
		if (n < 11 || at + 4 * (size_t)(map_w * map_h) + 32 * 256 + (size_t)(w * h) > size) return kp_cli_fail("fog_levels: a short request");
		vision.tiles = tile_flags(input + at, map_w * map_h);
		vision.map_w = map_w;
		vision.map_h = map_h;
		vision.not_visible = f[6];
		vision.not_explored = f[7];
		vision.replay = f[8] != 0;
		vision.replay_vision = f[9];
		vision.replay_reveal = f[10];
		at += 4 * (size_t)(map_w * map_h);
		dark = input + at;
		at += 32 * 256;
		pixels = (uint8_t *)malloc((size_t)(w * h) + 1);
		if (!view || !vision.tiles || !pixels) return kp_cli_fail("out of memory");
		memcpy(pixels, input + at, (size_t)(w * h));
		kp_fog_view_init(view);
		if (kp_fog_view_update(view, &vision, cam_x, cam_y, w, h, rect) < 0) {
			return kp_cli_fail("fog view: a view of %lldx%lld is too large", (long long)w, (long long)h);
		}
		printf("%lld %lld %lld %lld\n", (long long)view->cols, (long long)view->rows, (long long)view->level_w,
			(long long)view->level_h);
		fwrite(view->raw, 1, (size_t)(view->cols * view->rows), stdout);
		fwrite(view->smooth, 1, (size_t)(view->cols * view->rows), stdout);
		fwrite(view->levels, 1, (size_t)(view->level_w * view->level_h), stdout);
		kp_fog_view_shade(view, cam_x, cam_y, dark, pixels, w, h, w);
		fwrite(pixels, 1, (size_t)(w * h), stdout);
		free(pixels);
		free(view);
		free((void *)vision.tiles);
		free(input);
		return 0;
	}
}

/* --- export_replay --- */

static int cmd_export_replay(int argc, char **argv)
{
	char path[512];
	KpGolden g;
	KpReplay r;
	KpError err;
	FILE *f;
	int k, i;
	int64_t last = 0;
	if (argc < 2) return kp_cli_fail("usage: export_replay NAME OUT");
	snprintf(path, sizeof path, "tests/golden/oracle/scenarios/%s.json", argv[0]);
	if (kp_golden_load(&g, path, &err) != 0 || kp_replay_from_golden(&r, ".", g.root, &err) != 0) return kp_cli_fail("%s", err.msg);
	if (!(f = fopen(argv[1], "wb"))) return kp_cli_fail("cannot write %s", argv[1]);
	fprintf(f, "KPRP 1\nmission %s\nseed %u\nexpansion %d\nindex %lld\n", r.mission, (unsigned)r.seed, r.expansion,
		(long long)r.mission_index);
	for (k = 0; k < r.crc_count; k++) {
		if (r.crcs[k].step > last) last = r.crcs[k].step;
	}
	fprintf(f, "last %lld\n", (long long)last);
	for (k = 0; k < r.command_count; k++) {
		const KpReplayCommand *c = &r.commands[k];
		size_t b;
		fprintf(f, "command %lld %lld ", (long long)c->turn, (long long)c->command.player);
		for (b = 0; b < c->command.size; b++) fprintf(f, "%02x", c->command.bytes[b]);
		fputc('\n', f);
	}
	for (k = 0; k < r.ui_count; k++) {
		const KpReplayUi *u = &r.ui[k];
		fprintf(f, "%s %lld", kp_run_word(u->kind), (long long)u->frame);
		for (i = 0; i < u->count; i++) fprintf(f, " %lld", (long long)u->values[i]);
		fputc('\n', f);
	}
	for (k = 0; k < r.crc_count; k++) fprintf(f, "crc %lld %s %u\n", (long long)r.crcs[k].step, r.crcs[k].category, (unsigned)r.crcs[k].crc);
	fclose(f);
	kp_replay_free(&r);
	kp_golden_free(&g);
	return 0;
}


/* kp_oracle draw_list RECORDS.json: the draw list of an emulator capture (tools/compose_capture.py): the captured image
 * and sprite records as an images world over the generated data, the native draw list on the captured view. Prints
 * one line per drawn image meeting the view: "draw rank method owner param
 * grp-path frame left top flip trans50" (path "virtual" for health bars and boxes; trans50 the owner whose colours the
 * trans50 rows 8..15 hold, -1 for unchanged rows), then "plan <composition windows>". */
typedef struct {
	KpImages *w;
	KpRect view;
} CaptureDraw;

static int32_t capture_frame(void *ud, int32_t grp, int32_t frame)
{
	(void)ud;
	(void)grp;
	return frame;
}

static void capture_emit(void *ud, const KpDrawItem *d)
{
	CaptureDraw *c = (CaptureDraw *)ud;
	const KpRect *v = &c->view;
	if (d->left < v->right && d->top < v->bottom && d->left + d->w > v->left && d->top + d->h > v->top) {
		char path[128] = "virtual";
		if (!d->virtual_ && d->grp > 0) {
			const KpLabel *l = &c->w->grp->grps[d->grp].path;
			snprintf(path, sizeof path, "%.*s", (int)l->size, (const char *)l->text);
		}
		printf("draw %d %d %d %d %s %d %d %d %d %d\n", (int)d->rank, (int)d->method, (int)d->owner, (int)d->param, path,
			(int)d->grp_frame, (int)d->left, (int)d->top, (int)d->flip, (int)d->trans50);
	}
}

static int32_t field(const KpJson *o, const char *k) { return (int32_t)kp_json_field_int(o, k, KP_NONE); }

static int cmd_draw_list(int argc, char **argv)
{
	static const char *const SPRITE[] = { "id", "owner", "selection", "visibility", "elevation", "flags", "sel_timer",
		"index", "width", "height", "x", "y" };
	static const char *const IMAGE[] = { "id", "method", "dir", "flags", "xoff", "yoff", "header", "pc", "ret", "anim",
		"wait", "frameset", "frame", "x", "y", "param" };
	KpJsonDoc doc;
	KpJson *cap;
	KpDat dat;
	KpGrpMeta grp;
	KpIScript script;
	KpRngGame rng;
	KpError err;
	KpDrawList *list;
	const KpJson *a;
	uint8_t *bytes[3];
	size_t sizes[3], size;
	char *text, why[256];
	CaptureDraw cd;
	int k, j;
	int32_t windows;
	if (argc < 1) {
		fprintf(stderr, "usage: kp_oracle draw_list RECORDS.json\n");
		return 2;
	}
	text = (char *)kp_read_file(argv[0], &size);
	kp_json_doc_init(&doc);
	if (!text || !(cap = kp_json_parse(&doc, text, size, why, sizeof why))) {
		fprintf(stderr, "draw_list: %s: %s\n", argv[0], text ? why : "cannot read");
		return 1;
	}
	bytes[0] = kp_read_file(KP_ORACLE_DATA "dat.kpd", &sizes[0]);
	bytes[1] = kp_read_file(KP_ORACLE_DATA "grpmeta.kpd", &sizes[1]);
	bytes[2] = kp_read_file(KP_ORACLE_DATA "iscript.kpd", &sizes[2]);
	memset(&rng, 0, sizeof rng);
	if (!bytes[0] || !bytes[1] || !bytes[2] || kp_dat_load(&dat, bytes[0], sizes[0], "dat.kpd", NULL, &err) != 0
		|| kp_grpmeta_load(&grp, bytes[1], sizes[1], "grpmeta.kpd", NULL, &err) != 0
		|| kp_iscript_load(&script, bytes[2], sizes[2], "iscript.kpd", NULL, &err) != 0
		|| !(cd.w = kp_images_new(&dat, &grp, &script, &rng, &err))) {
		fprintf(stderr, "draw_list: the generated data: %s\n", err.msg);
		return 1;
	}
	a = kp_json_get(cap, "map");
	kp_images_set_map(cd.w, (int32_t)kp_json_int(kp_json_at(a, 0), 0), (int32_t)kp_json_int(kp_json_at(a, 1), 0));
	cd.w->local_vision = (int32_t)kp_json_field_int(cap, "vision", 0xff);
	a = kp_json_get(cap, "sprites");
	for (k = 0; k < kp_json_len(a); k++) {
		const KpJson *s = kp_json_at(a, k);
		int32_t key = field(s, "key");
		int32_t *cols[12] = { cd.w->spr.id, cd.w->spr.owner, cd.w->spr.selection, cd.w->spr.visibility,
			cd.w->spr.elevation, cd.w->spr.flags, cd.w->spr.sel_timer, cd.w->spr.index, cd.w->spr.width, cd.w->spr.height,
			cd.w->spr.x, cd.w->spr.y };
		for (j = 0; j < 12; j++) cols[j][key] = field(s, SPRITE[j]);
		cd.w->spr.prev[key] = field(s, "prev");
		cd.w->spr.next[key] = field(s, "next");
		cd.w->spr.main[key] = field(s, "main");
		cd.w->spr.images[key].head = field(s, "head");
		cd.w->spr.images[key].tail = field(s, "tail");
	}
	a = kp_json_get(cap, "images");
	for (k = 0; k < kp_json_len(a); k++) {
		const KpJson *i = kp_json_at(a, k), *v = kp_json_get(i, "virtual");
		int32_t key = field(i, "key");
		int32_t *cols[16] = { cd.w->img.id, cd.w->img.method, cd.w->img.dir, cd.w->img.flags, cd.w->img.xoff,
			cd.w->img.yoff, cd.w->img.header, cd.w->img.pc, cd.w->img.ret, cd.w->img.anim, cd.w->img.wait,
			cd.w->img.frameset, cd.w->img.frame, cd.w->img.x, cd.w->img.y, cd.w->img.param };
		for (j = 0; j < 16; j++) cols[j][key] = field(i, IMAGE[j]);
		cd.w->img.prev[key] = field(i, "prev");
		cd.w->img.next[key] = field(i, "next");
		cd.w->img.sprite[key] = field(i, "sprite");
		if (v) {
			cd.w->img.grp[key] = KP_GRP_VIRTUAL;
			cd.w->has_vgrp[key] = 1;
			cd.w->vgrp_width[key] = field(v, "width");
			cd.w->vgrp_height[key] = field(v, "height");
			cd.w->vgrp_fw[key] = field(v, "fw");
			cd.w->vgrp_fh[key] = field(v, "fh");
		} else {
			cd.w->img.grp[key] = kp_images_grp_of(cd.w, cd.w->img.id[key]);
		}
	}
	a = kp_json_get(cap, "rows");
	for (k = 0; k < kp_json_len(a) && k < cd.w->map_h; k++) {
		cd.w->rows[k].head = (int32_t)kp_json_int(kp_json_at(kp_json_at(a, k), 0), KP_NONE);
		cd.w->rows[k].tail = (int32_t)kp_json_int(kp_json_at(kp_json_at(a, k), 1), KP_NONE);
	}
	cd.view.left = (int32_t)kp_json_int(kp_json_at(kp_json_get(cap, "camera"), 0), 0);
	cd.view.top = (int32_t)kp_json_int(kp_json_at(kp_json_get(cap, "camera"), 1), 0);
	cd.view.right = cd.view.left + (int32_t)kp_json_int(kp_json_at(kp_json_get(cap, "view"), 0), 640);
	cd.view.bottom = cd.view.top + (int32_t)kp_json_int(kp_json_at(kp_json_get(cap, "view"), 1), 400);
	/* the original refreshes the bounds of the drawn rows before DrawSortedSprites (0x4165d0) */
	kp_images_refresh_bounds(cd.w);
	list = kp_draw_list_new();
	if (!list) return 1;
	if (kp_draw_list_build(list, cd.w, capture_frame, capture_emit, &cd, &cd.view, 0, NULL) < 0) {
		fprintf(stderr, "draw_list: out of memory\n");
		return 1;
	}
	kp_draw_list_windows(list, &windows);
	printf("plan %d\n", (int)windows);
	return 0;
}

static const struct {
	const char *name;
	int (*fn)(int argc, char **argv);
} COMMANDS[] = {
	{ "start_state", cmd_start_state }, { "traces", cmd_traces }, { "play", cmd_play }, { "fog_levels", cmd_fog_levels },
	{ "export_replay", cmd_export_replay }, { "draw_list", cmd_draw_list }, { "hud_check", kp_cli_hud_check }, { "input_check", kp_cli_input_check },
	{ "menus_replay", kp_cli_menus_replay }, { "room_replay", kp_cli_room_replay },
	{ "result_replay", kp_cli_result_replay }, { "score_replay", kp_cli_score_replay },
};

int main(int argc, char **argv)
{
	size_t k;
	if (argc >= 2) {
		for (k = 0; k < sizeof COMMANDS / sizeof COMMANDS[0]; k++) {
			if (strcmp(argv[1], COMMANDS[k].name) == 0) {
				int status = COMMANDS[k].fn(argc - 2, argv + 2);
				fflush(stdout);
				return status;
			}
		}
	}
	fprintf(stderr, "usage: kp_oracle start_state|traces|play|fog_levels|export_replay|draw_list|hud_check|input_check|"
		"menus_replay|room_replay|result_replay|score_replay ARGS...\n");
	return 2;
}
