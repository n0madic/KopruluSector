/*
 * The interface's subcommands of kp_oracle, with their arguments (and the Python checkers that run them):
 *   hud_check GOLDEN HUD.txt OUT FRAME [FRAME ...] [--run DIR] [--modeless TICK:FRAME,...]   (tools/ui_check.py)
 *   input_check GOLDEN HUD.txt RUN_DIR [LAST_FRAME]                                         (tools/input_check.py)
 *   menus_replay MENUS.txt                                                                  (tools/menus_check.py)
 *   room_replay ROOM.txt, result_replay RESULT.txt, score_replay SCORE.txt                  (tools/room_check.py)
 * A score file's "data" line holds the score data as JSON.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kp_flow.h"
#include "kp_host.h"
#include "kp_netui.h"
#include "kp_oracle_cli.h"
#include "kp_oracle_net.h"

#define LINES_FLAGS 0x2000 /* the text lines' layer: index 0 shows the game view (ui_check.py TRANSPARENT) */
#define CURSOR_BYTES (256 * 256)
#define IDLE 13
#define RESULT_START 101 /* a ready room's Start (every player ready) */

/* A golden, a hud log and a run directory's logs, loaded. */
typedef struct {
	KpGolden golden;
	KpHudLog hud;
	uint8_t *commands, *ui;
	size_t commands_size, ui_size;
} Inputs;

static int load_inputs(Inputs *in, const char *golden, const char *hud, const char *run_dir)
{
	KpError err;
	size_t size;
	uint8_t *text;
	memset(in, 0, sizeof *in);
	if (kp_golden_load(&in->golden, golden, &err) != 0) return kp_cli_fail("%s", err.msg);
	text = kp_cli_read(hud, &size);
	if (kp_hud_log_parse(&in->hud, (const char *)text, size, &err) != 0) return kp_cli_fail("%s", err.msg);
	free(text);
	if (run_dir) {
		char path[1024];
		snprintf(path, sizeof path, "%s/commands.jsonl", run_dir);
		in->commands = kp_cli_read(path, &in->commands_size);
		snprintf(path, sizeof path, "%s/ui.jsonl", run_dir);
		in->ui = kp_cli_read(path, &in->ui_size);
	}
	return 0;
}

static void free_inputs(Inputs *in)
{
	kp_golden_free(&in->golden);
	kp_hud_log_free(&in->hud);
	free(in->commands);
	free(in->ui);
}

/* --- hud_check --- */

typedef struct {
	FILE *out;
	int64_t modeless[256];
	int modeless_count, next_modeless;
	int cursor_logged;
	uint8_t *cursor;
} HudCheck;

static void write_panel(FILE *out, const KpUi *ui, int32_t d)
{
	const KpUiControl *k = &ui->controls[d];
	char name[128];
	if (!k->bitmap.pixels) return;
	kp_oracle_ui_bin_name(ui, k->bin, 0, name, sizeof name);
	fprintf(out, "panel %s %d %d %d %d %d\n", name, (int)k->left, (int)k->top, (int)k->bitmap.w, (int)k->bitmap.h,
		(int)k->flags);
	fwrite(k->bitmap.pixels, 1, (size_t)k->bitmap.w * (size_t)k->bitmap.h, out);
}

static void hud_present(void *ud, KpHudReplay *r, int64_t tick)
{
	HudCheck *h = (HudCheck *)ud;
	KpUi *ui = r->ui.ui;
	while (h->next_modeless < h->modeless_count && tick >= h->modeless[h->next_modeless]) {
		fprintf(h->out, "modeless %d\n", h->next_modeless);
		if (ui->modeless >= 0) {
			uint8_t row[256];
			write_panel(h->out, ui, ui->modeless);
			kp_ui_canvas_blend_row(ui, row);
			fprintf(h->out, "blend %d\n", (int)ui->effects.colour[0]);
			fwrite(row, 1, sizeof row, h->out);
			if (ui->console.pixels) { /* drawn over a dialog composited onto the backdrop */
				fprintf(h->out, "console %d %d\n", (int)ui->console.w, (int)ui->console.h);
				fwrite(ui->console.pixels, 1, (size_t)ui->console.w * (size_t)ui->console.h, h->out);
			}
		}
		h->next_modeless++;
	}
}

static void hud_frame(void *ud, KpHudReplay *r, int64_t frame)
{
	HudCheck *h = (HudCheck *)ud;
	KpUi *ui = r->ui.ui;
	int32_t d, w, ht, dx, dy, tx, ty, tw, th;
	const KpUiSurface *tip, *lines;
	fprintf(h->out, "frame %lld\n", (long long)frame);
	for (d = ui->open; d >= 0; d = ui->controls[d].next) write_panel(h->out, ui, d);
	if (h->cursor_logged && kp_ui_cursor_frame(ui, h->cursor, CURSOR_BYTES, &w, &ht, &dx, &dy) == 0) {
		fprintf(h->out, "panel cursor %d %d %d %d %d\n", (int)dx, (int)dy, (int)w, (int)ht, LINES_FLAGS);
		fwrite(h->cursor, 1, (size_t)w * (size_t)ht, h->out);
	}
	tip = kp_ui_tooltip(ui, &tx, &ty, &tw, &th);
	if (tip && tw > 0 && th > 0 && tw <= tip->w && th <= tip->h) {
		int32_t row;
		fprintf(h->out, "panel tooltip %d %d %d %d 0\n", (int)tx, (int)ty, (int)tw, (int)th);
		for (row = 0; row < th; row++) fwrite(tip->pixels + (size_t)row * (size_t)tip->w, 1, (size_t)tw, h->out);
	}
	kp_ui_lines_expire(ui);
	lines = kp_ui_lines_layer(ui);
	if (lines) {
		fprintf(h->out, "panel lines 0 0 %d %d %d\n", (int)lines->w, (int)lines->h, LINES_FLAGS);
		fwrite(lines->pixels, 1, (size_t)lines->w * (size_t)lines->h, h->out);
	}
}

static int by_value(const void *a, const void *b)
{
	int64_t x = *(const int64_t *)a, y = *(const int64_t *)b;
	return x < y ? -1 : x > y;
}

int kp_cli_hud_check(int argc, char **argv)
{
	Inputs in;
	HudCheck h;
	KpHudReplay *r;
	KpHudReplayOpts opts;
	KpHudObservers obs;
	KpError err;
	int64_t *frames;
	int frame_count = 0, k, status = 0;
	const char *run_dir = NULL;
	if (argc < 4) return kp_cli_fail("usage: hud_check GOLDEN HUD.txt OUT FRAME [FRAME ...] [--run DIR] [--modeless TICK:FRAME,...]");
	memset(&h, 0, sizeof h);
	frames = (int64_t *)calloc((size_t)argc + 256, sizeof *frames);
	if (!frames) return kp_cli_fail("out of memory");
	for (k = 3; k < argc; k++) {
		if (strcmp(argv[k], "--run") == 0 && k + 1 < argc) {
			run_dir = argv[++k];
		} else if (strcmp(argv[k], "--modeless") == 0 && k + 1 < argc) { /* the modeless captures' ticks and frames */
			const char *p = argv[++k];
			while (*p) {
				long long tick, frame;
				int used;
				if (sscanf(p, "%lld:%lld%n", &tick, &frame, &used) != 2) break;
				if (h.modeless_count < 256) {
					h.modeless[h.modeless_count++] = tick;
					frames[frame_count++] = frame + 1; /* the replay runs past the capture's (paused) frame */
				}
				p += used;
				while (*p == ',') p++;
			}
		} else {
			frames[frame_count++] = strtoll(argv[k], NULL, 10);
		}
	}
	qsort(frames, (size_t)frame_count, sizeof *frames, by_value);
	for (k = frame_count - 1; k >= 1; k--) {
		if (frames[k] == frames[k - 1]) {
			memmove(frames + k, frames + k + 1, sizeof *frames * (size_t)(frame_count - k - 1));
			frame_count--;
		}
	}
	if (load_inputs(&in, argv[0], argv[1], run_dir) != 0) return 1;
	for (k = 0; k < in.hud.count; k++) h.cursor_logged |= in.hud.entries[k].v[1] == KP_HUD_CURSOR;
	if (!(h.out = fopen(argv[2], "wb"))) return kp_cli_fail("cannot write %s", argv[2]);
	h.cursor = (uint8_t *)malloc(CURSOR_BYTES);
	r = (KpHudReplay *)malloc(sizeof *r);
	if (!h.cursor || !r) return kp_cli_fail("out of memory");
	memset(&opts, 0, sizeof opts);
	opts.golden = in.golden.root;
	opts.hud = &in.hud;
	opts.frames = frames;
	opts.frame_count = frame_count;
	opts.input = 1; /* the interface takes the run's input itself (its modes and presses show on the panels) */
	opts.commands = (const char *)in.commands;
	opts.commands_size = in.commands_size;
	opts.ui = (const char *)in.ui;
	opts.ui_size = in.ui_size;
	memset(&obs, 0, sizeof obs);
	obs.on_present = hud_present;
	obs.on_frame = hud_frame;
	obs.ud = &h;
	if (kp_hud_replay_start(r, ".", &opts, &obs, &err) != 0 || kp_hud_replay_run(r, &err) != 0) status = kp_cli_fail("%s", err.msg);
	if (r->game.g || r->ui.ui) kp_hud_replay_free(r);
	fclose(h.out);
	free(h.cursor);
	free(r);
	free(frames);
	free_inputs(&in);
	return status;
}

/* --- input_check --- */

#define SYNC 0x37
#define SAVE 0x06
#define SHOWN 8

typedef struct {
	int64_t frame;
	uint8_t bytes[512];
	size_t size;
} Command;

typedef struct {
	Command *got;
	int got_count, got_cap;
	int64_t differences, selections, cursors, scrolls, releases, tips, sounds, talks;
	int64_t local_player;
} InputCheck;

static void differ(InputCheck *c, const char *fmt, ...)
{
	va_list ap;
	c->differences++;
	if (c->differences > SHOWN) return;
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	putchar('\n');
}

static void join_ints(char *out, size_t size, const int64_t *v, int n)
{
	size_t at = 0;
	int k;
	out[0] = '\0';
	for (k = 0; k < n && at < size; k++) at += (size_t)snprintf(out + at, size - at, "%s%lld", k ? "," : "", (long long)v[k]);
}

static void on_select(void *ud, KpHudReplay *r, int64_t frame, const int64_t *units, int count)
{
	InputCheck *c = (InputCheck *)ud;
	const int64_t *have = r->game.g->local_selection;
	int k;
	c->selections++;
	for (k = 0; k < 12; k++) {
		if ((k < count ? units[k] : 0) != have[k]) {
			char a[256], b[256];
			join_ints(a, sizeof a, have, 12);
			join_ints(b, sizeof b, units, count);
			differ(c, "frame %lld: the local selection %s, the original's %s", (long long)frame, a, b);
			break;
		}
	}
}

/* a command goes out with the next frame's turn, or with the same frame's while the game is paused */
static void on_command(void *ud, KpHudReplay *r, int64_t frame, const uint8_t *bytes, size_t size)
{
	InputCheck *c = (InputCheck *)ud;
	Command *k;
	if (c->got_count >= c->got_cap) {
		c->got_cap = c->got_cap ? c->got_cap * 2 : 256;
		c->got = (Command *)realloc(c->got, sizeof *c->got * (size_t)c->got_cap);
		if (!c->got) exit(kp_cli_fail("out of memory"));
	}
	k = &c->got[c->got_count++];
	k->frame = frame + (kp_scalar(r->game.g, KP_PAUSED) == 1 ? 0 : 1);
	k->size = size < sizeof k->bytes ? size : sizeof k->bytes;
	memcpy(k->bytes, bytes, k->size);
}

static void on_cursor(void *ud, KpHudReplay *r, int64_t frame, int64_t have, int64_t want)
{
	InputCheck *c = (InputCheck *)ud;
	KpGame *g = r->game.g;
	/* TODO: the result dialog (its own cursor) that the game's end opens */
	if (g->results[g->local_player] != 0) return;
	c->cursors++;
	if (have != want) differ(c, "frame %lld: the cursor %lld, the original's %lld", (long long)frame, (long long)have, (long long)want);
}

static void on_scroll(void *ud, KpHudReplay *r, int64_t frame, const KpHudEntry *e)
{
	InputCheck *c = (InputCheck *)ud;
	const KpUi *ui = r->ui.ui;
	const KpUiScroll *s = &ui->input.scroll;
	int64_t have[8];
	int k;
	have[0] = ui->input.screen_x;
	have[1] = ui->input.screen_y;
	have[2] = s->step;
	have[3] = s->dx;
	have[4] = s->dy;
	have[5] = s->suppressed;
	have[6] = s->cursor_x;
	have[7] = s->cursor_y;
	c->scrolls++;
	for (k = 0; k < 8; k++) {
		if (have[k] != e->v[k + 2]) {
			char a[256], b[256];
			join_ints(a, sizeof a, have, 8);
			join_ints(b, sizeof b, e->v + 2, 8);
			differ(c, "frame %lld: the scroll %s, the original's %s", (long long)frame, a, b);
			break;
		}
	}
}

static void on_screen(void *ud, KpHudReplay *r, int64_t frame, int64_t x, int64_t y, const KpHudEntry *e)
{
	InputCheck *c = (InputCheck *)ud;
	const KpUi *ui = r->ui.ui;
	(void)e;
	if (ui->input.screen_x != x || ui->input.screen_y != y) {
		differ(c, "frame %lld: an input event in the view %d,%d, the original's %lld,%lld", (long long)frame,
			(int)ui->input.screen_x, (int)ui->input.screen_y, (long long)x, (long long)y);
	}
}

static void on_release(void *ud, KpHudReplay *r, int64_t frame, int released)
{
	InputCheck *c = (InputCheck *)ud;
	(void)r;
	c->releases++;
	if (!released) differ(c, "frame %lld: the trigger wait's release, not the interface's", (long long)frame);
}

static void on_tip(void *ud, KpHudReplay *r, int64_t frame, const int32_t *have, const int64_t *want)
{
	InputCheck *c = (InputCheck *)ud;
	char h[96], w[96];
	(void)r;
	c->tips++;
	if (have) snprintf(h, sizeof h, "%d,%d-%d,%d", (int)have[0], (int)have[1], (int)(have[0] + have[2] - 1), (int)(have[1] + have[3] - 1));
	else snprintf(h, sizeof h, "none");
	if (want) snprintf(w, sizeof w, "%lld,%lld-%lld,%lld", (long long)want[0], (long long)want[1], (long long)want[2], (long long)want[3]);
	else snprintf(w, sizeof w, "none");
	if (strcmp(h, w) != 0) differ(c, "frame %lld: the tooltip %s, the original's %s", (long long)frame, h, w);
}

static void on_sound(void *ud, KpHudReplay *r, int64_t frame, const int64_t *have, const KpHudEntry *e)
{
	InputCheck *c = (InputCheck *)ud;
	char a[256], b[256];
	(void)r;
	c->sounds++;
	join_ints(a, sizeof a, have, 6);
	join_ints(b, sizeof b, e->v + 2, 6); /* id, attenuation, pan, unit type, path, result */
	if (strcmp(a, b) != 0) differ(c, "frame %lld: the sound %s, the original's %s", (long long)frame, a, b);
}

static void on_talk(void *ud, KpHudReplay *r, int64_t frame, const int64_t *have, const KpHudEntry *e)
{
	InputCheck *c = (InputCheck *)ud;
	char a[256], b[256];
	(void)r;
	c->talks++;
	join_ints(a, sizeof a, have, 5);
	join_ints(b, sizeof b, e->v + 2, 5); /* unit, duration, sound, ping, colour */
	if (strcmp(a, b) != 0) differ(c, "frame %lld: the talking request %s, the original's %s", (long long)frame, a, b);
}

static void on_sound_end(void *ud, KpHudReplay *r)
{
	InputCheck *c = (InputCheck *)ud;
	char a[256];
	int k;
	for (k = r->sw_at; k < r->sw; k++) {
		join_ints(a, sizeof a, r->sounds_want[k].entry->v + 2, 6);
		differ(c, "frame %lld: the original's sound %s, not the interface's", (long long)r->sounds_want[k].frame, a);
	}
	for (k = r->sh_at; k < r->sh; k++) {
		join_ints(a, sizeof a, r->sounds_have[k].v, 6);
		differ(c, "the interface's sound %s, not the original's", a);
	}
	for (k = r->tw_at; k < r->tw; k++) {
		join_ints(a, sizeof a, r->talks_want[k].entry->v + 2, 5);
		differ(c, "frame %lld: the original's talking request %s, not the interface's", (long long)r->talks_want[k].frame, a);
	}
	for (k = r->th_at; k < r->th; k++) {
		join_ints(a, sizeof a, r->talks_have[k].v, 5);
		differ(c, "the interface's talking request %s, not the original's", a);
	}
}

static void on_input_frame(void *ud, KpHudReplay *r, int64_t frame)
{
	InputCheck *c = (InputCheck *)ud;
	(void)frame;
	c->local_player = r->game.g->local_player;
}

static void hex(char *out, size_t size, const uint8_t *bytes, size_t n)
{
	size_t k, at = 0;
	out[0] = '\0';
	for (k = 0; k < n && at + 3 < size; k++) at += (size_t)snprintf(out + at, size - at, "%02x", bytes[k]);
}

/* a command as compared: the save command's time stamp (SendSaveGameCommand 0x4740e0: time(NULL)) left out */
static int same_command(const uint8_t *a, size_t an, const uint8_t *b, size_t bn)
{
	if (an > 0 && bn > 0 && a[0] == SAVE && b[0] == SAVE) {
		if (an < 5 || bn < 5) return an == bn && memcmp(a, b, an) == 0;
		return an == bn && memcmp(a + 5, b + 5, an - 5) == 0;
	}
	return an == bn && memcmp(a, b, an) == 0;
}

int kp_cli_input_check(int argc, char **argv)
{
	Inputs in;
	InputCheck c;
	KpHudReplay *r;
	KpHudReplayOpts opts;
	KpHudObservers obs;
	KpReplay want;
	KpError err;
	int64_t frame_end, last = -1;
	int k, mine_count = 0, n;
	int *mine;
	if (argc < 3) return kp_cli_fail("usage: input_check GOLDEN HUD.txt RUN_DIR [LAST_FRAME]");
	if (argc > 3) last = strtoll(argv[3], NULL, 10);
	if (load_inputs(&in, argv[0], argv[1], argv[2]) != 0) return 1;
	memset(&c, 0, sizeof c);
	c.local_player = -1;
	/* the original's commands (frame, player, bytes), the game's own sync command left out */
	kp_replay_init(&want);
	{
		/* commands.jsonl's frames: kp_replay keeps turns, so the lines are read here too */
		const char *p = (const char *)in.commands, *end = p + in.commands_size;
		if (kp_replay_add_commands_jsonl(&want, (const char *)in.commands, in.commands_size, &err) != 0) return kp_cli_fail("%s", err.msg);
		mine = (int *)calloc((size_t)want.command_count + 1, sizeof *mine);
		if (!mine) return kp_cli_fail("out of memory");
		for (k = 0; p < end && k < want.command_count;) {
			const char *nl = (const char *)memchr(p, '\n', (size_t)(end - p));
			size_t len = nl ? (size_t)(nl - p) : (size_t)(end - p);
			if (len > 0 && !(len == 1 && *p == '\r')) {
				KpJsonDoc doc;
				KpJson *v;
				kp_json_doc_init(&doc);
				v = kp_json_parse(&doc, p, len, NULL, 0);
				want.commands[k].turn = kp_json_field_int(v, "frame", 0); /* the frame in place of the turn */
				kp_json_doc_free(&doc);
				k++;
			}
			p += len + 1;
		}
	}
	frame_end = last >= 0 ? last : in.hud.count ? in.hud.entries[in.hud.count - 1].v[0] : 0;
	r = (KpHudReplay *)malloc(sizeof *r);
	if (!r) return kp_cli_fail("out of memory");
	memset(&opts, 0, sizeof opts);
	opts.golden = in.golden.root;
	opts.hud = &in.hud;
	opts.frames = &frame_end;
	opts.frame_count = 1;
	opts.input = 1;
	opts.commands = (const char *)in.commands;
	opts.commands_size = in.commands_size;
	opts.ui = (const char *)in.ui;
	opts.ui_size = in.ui_size;
	memset(&obs, 0, sizeof obs);
	obs.on_select = on_select;
	obs.on_command = on_command;
	obs.on_cursor = on_cursor;
	obs.on_scroll = on_scroll;
	obs.on_screen = on_screen;
	obs.on_release = on_release;
	obs.on_tip = on_tip;
	obs.on_sound = on_sound;
	obs.on_talk = on_talk;
	obs.on_sound_end = on_sound_end;
	obs.on_frame = on_input_frame;
	obs.ud = &c;
	if (kp_hud_replay_start(r, ".", &opts, &obs, &err) != 0 || kp_hud_replay_run(r, &err) != 0) return kp_cli_fail("%s", err.msg);
	for (k = 0; k < want.command_count; k++) {
		const KpReplayCommand *w = &want.commands[k];
		if (w->command.size > 0 && w->command.bytes[0] == SYNC) continue;
		if (last >= 0 && w->turn > last) continue;
		if (w->command.player == c.local_player) mine[mine_count++] = k;
	}
	n = mine_count > c.got_count ? mine_count : c.got_count;
	for (k = 0; k < n; k++) {
		char a[2048], b[2048];
		if (k >= mine_count) {
			hex(a, sizeof a, c.got[k].bytes, c.got[k].size);
			differ(&c, "command %d: Koprulu queued %s at frame %lld, the original nothing more", k + 1, a, (long long)c.got[k].frame);
		} else if (k >= c.got_count) {
			const KpReplayCommand *w = &want.commands[mine[k]];
			hex(b, sizeof b, w->command.bytes, w->command.size);
			differ(&c, "command %d: the original queued %s at frame %lld, Koprulu nothing more", k + 1, b, (long long)w->turn);
		} else {
			const KpReplayCommand *w = &want.commands[mine[k]];
			if (!same_command(c.got[k].bytes, c.got[k].size, w->command.bytes, w->command.size) || c.got[k].frame != w->turn) {
				hex(a, sizeof a, c.got[k].bytes, c.got[k].size);
				hex(b, sizeof b, w->command.bytes, w->command.size);
				differ(&c, "command %d: Koprulu %s at frame %lld, the original %s at frame %lld", k + 1, a, (long long)c.got[k].frame, b,
					(long long)w->turn);
			}
		}
	}
	printf("input_check: %lld selections, %lld cursors, %lld scrolls, %lld releases, %lld tooltips, %lld sounds, %lld talks, "
		"%d commands (the original's %d), %lld differences\n", (long long)c.selections, (long long)c.cursors,
		(long long)c.scrolls, (long long)c.releases, (long long)c.tips, (long long)c.sounds, (long long)c.talks, c.got_count,
		mine_count, (long long)c.differences);
	kp_hud_replay_free(r);
	free(r);
	free(c.got);
	free(mine);
	kp_replay_free(&want);
	free_inputs(&in);
	return c.differences == 0 ? 0 : 1;
}

/* --- the glue's replays --- */

typedef struct {
	KpTraceFile file;
	KpOracleUi u;
	KpTraceStreams want, have;
	KpTraceClocks clocks;
	KpTraceCapture *caps;
	int cap_count, next_cap;
	int64_t pixel_differences;
	KpTraceKeep keep;
} Glue;

static int glue_start(Glue *g, const char *path, const char *const *words, int flags, KpTraceKeep keep)
{
	KpError err;
	size_t size;
	char *text;
	memset(g, 0, sizeof *g);
	text = (char *)kp_cli_read(path, &size);
	if (kp_trace_file_parse(&g->file, text, words, &err) != 0) return kp_cli_fail("%s", err.msg);
	if (kp_oracle_ui_new(&g->u, ".", flags, &err) != 0) return kp_cli_fail("%s", err.msg);
	g->u.ui->sound.voices = 0;
	g->u.ui->timers.trace = (KpUiTimerTrace *)calloc(1, sizeof *g->u.ui->timers.trace);
	if (!g->u.ui->timers.trace) return kp_cli_fail("ui: out of memory");
	g->u.ui->timers.trace->controls = g->u.ui->controls;
	g->u.ui->video_trace = 1;
	g->keep = keep;
	kp_trace_want(&g->file, keep, &g->want, &g->clocks);
	g->cap_count = kp_trace_captures(&g->file, &g->caps);
	return 0;
}

static void glue_free(Glue *g)
{
	kp_trace_streams_free(&g->want);
	kp_trace_streams_free(&g->have);
	kp_trace_clocks_free(&g->clocks);
	free(g->caps);
	kp_oracle_ui_free(&g->u);
	kp_trace_file_free(&g->file);
}

static void take(Glue *g) { kp_trace_take(&g->u, &g->have, g->keep); }

/* The first number of a record's rest. */
static int64_t first_number(const char *rest) { return strtoll(rest, NULL, 10); }

/* The tick of a record (pass/turn: its number, page: its first, ev/credits: their first), or -1. */
static int64_t record_tick(const KpTraceRecord *r)
{
	if (strcmp(r->word, "pass") == 0 || strcmp(r->word, "turn") == 0 || strcmp(r->word, "page") == 0
		|| strcmp(r->word, "ev") == 0 || strcmp(r->word, "credits") == 0) {
		return first_number(r->rest);
	}
	return -1;
}

/* An "ev" record's tick, kind and position. */
static void ev_fields(const char *rest, int64_t *t, int64_t *kind, int64_t *a, int64_t *b)
{
	long long v[4] = { 0, 0, 0, 0 };
	sscanf(rest, "%lld %lld %lld %lld", &v[0], &v[1], &v[2], &v[3]);
	*t = v[0];
	*kind = v[1];
	*a = v[2];
	*b = v[3];
}

/* The menus' CRT state from the first draw's. */
static void glue_crt(Glue *g)
{
	int k;
	for (k = 0; k < g->file.count; k++) {
		if (strcmp(g->file.records[k].word, "rand") == 0) {
			kp_rng_crt_init(&g->u.ui->glue.crt, (uint32_t)strtoull(g->file.records[k].rest, NULL, 10));
			return;
		}
	}
}

/* A dialog's bitmap as the expected picture, rectangles of it skipped (screen coordinates, inclusive). */
#define BITMAP_MASKS 4
typedef struct {
	const KpUiSurface *s;
	int64_t mask[BITMAP_MASKS][4];
	int masks;
} Bitmap;

static int bitmap_pixel(void *ud, int64_t x, int64_t y)
{
	const Bitmap *b = (const Bitmap *)ud;
	int k;
	for (k = 0; k < b->masks; k++)
		if (x >= b->mask[k][0] && x <= b->mask[k][2] && y >= b->mask[k][1] && y <= b->mask[k][3]) return -1;
	return b->s->pixels[y * b->s->w + x];
}

static void bitmap_init(Bitmap *b, const KpUiSurface *s)
{
	b->s = s;
	b->masks = 0;
}

/* Skips a control's rectangle on the screen (its dialog's offset added). */
static void bitmap_mask_control(Bitmap *b, KpUi *ui, int32_t dialog, int16_t id)
{
	const KpUiControl *d = &ui->controls[dialog], *c;
	int32_t k = kp_ui_find_control(ui, dialog, id);
	if (k < 0 || b->masks >= BITMAP_MASKS) return;
	c = &ui->controls[k];
	b->mask[b->masks][0] = d->left + c->left;
	b->mask[b->masks][1] = d->top + c->top;
	b->mask[b->masks][2] = d->left + c->right;
	b->mask[b->masks][3] = d->top + c->bottom;
	b->masks++;
}

/* A game for a room or a result dialog (oracle.start_mission with no options). */
/* The game of the catalog's mission of a mapdata index (a replay's "mission"). */
static KpOracleGame *start_game(int64_t mission, int expansion)
{
	KpOracleGame *og = (KpOracleGame *)calloc(1, sizeof *og);
	const KpData *d;
	KpError err;
	if (!og || !(d = kp_oracle_data(".", &err))) exit(kp_cli_fail("%s", og ? err.msg : "out of memory"));
	if (mission < 1 || mission > d->mission_count || !d->missions[mission - 1].catalog) {
		exit(kp_cli_fail("no catalog mission %lld", (long long)mission));
	}
	if (kp_oracle_game_start(og, ".", d->missions[mission - 1].key, 0, expansion, NULL, &err) != 0) {
		exit(kp_cli_fail("%s", err.msg));
	}
	return og;
}

static void free_game(KpOracleGame *og)
{
	if (!og) return;
	kp_oracle_game_free(og);
	free(og);
}

/* The capture's palette (its .pal beside the indices) against the glue's: the entries' channels that differ. */
static int64_t palette_differences(const KpTraceCapture *cap, const KpUi *ui)
{
	char path[600];
	size_t size, n = strlen(cap->path);
	uint8_t *theirs;
	int64_t differ = 0;
	int k, c;
	if (n > 4 && strcmp(cap->path + n - 4, ".raw") == 0) snprintf(path, sizeof path, "%.*s.pal", (int)(n - 4), cap->path);
	else snprintf(path, sizeof path, "%s", cap->path);
	theirs = kp_cli_read(path, &size);
	for (k = 0; k < 256; k++) {
		for (c = 0; c < 3; c++) {
			if ((size_t)(4 * k + c) >= size || theirs[4 * k + c] != ui->glue.palette[4 * k + c]) differ++;
		}
	}
	free(theirs);
	return differ;
}

/* --- menus_replay --- */

#define ROOM_STATE_FIRST 7 /* glue states 7..9: the ready rooms (terran, zerg, protoss) */
#define TITLE_REF "rez\\titledlg.bin#" /* a record of the title's dialog or its controls */
static int room_race(int state) { return state == 7 ? 1 : state == 8 ? 0 : state == 9 ? 2 : -1; }

/* Whether a "del" record's rest is the deletion of the timer ref (BIN#record ID) and nothing more. */
static int is_deletion(const char *rest, const char *ref)
{
	size_t n = strlen(ref);
	return strncmp(rest, ref, n) == 0 && (rest[n] == '\0' || rest[n] == ' ' || rest[n] == '\t' || rest[n] == '\r');
}

typedef struct {
	int field; /* 0-based */
	uint32_t value;
	int set;
} Progress;

/* The progress StartCampaignEpisode read in a run: kp_ui's profile "Koprulu" gets it (the old value kept in saved). */
static void set_progress(KpUi *ui, const Progress *p, Progress *saved)
{
	int k;
	for (k = 0; k < ui->menus.profile_count; k++) {
		KpUiProfile *q = &ui->menus.profiles[k];
		if (strcmp(q->name, "Koprulu") == 0) {
			if (!saved->set) {
				saved->set = 1;
				saved->field = p->field;
				saved->value = q->progress[p->field];
			}
			q->progress[p->field] = p->value;
		}
	}
	ui->menus.profiles_changed = 0; /* a read of the profile list clears it */
}

/* The network screens' host (phase N, stage N3): Storm's providers as the original enumerated them (the run's "provider
 * ID FLAGS MAX NAME" lines in SNetEnumProviders' order: caps.dat's flags and maximum players, the name's bytes as
 * Storm's), every provider initialized; with a line "net GOLDEN PLAYER" (the network oracle's golden of the run's game
 * and the player's name) the selected provider's game is Koprulu's KpNetGame over the player's recorded SNet
 * (kp_oracle_net.h), behind the screens' adapter (kp_netui.h). */
typedef struct {
	KpNetUi nu; /* first: KpUiNet's host */
	KpUiProvider providers[KP_UI_PROVIDERS];
	int provider_count;
	KpUiNet net;
	KpJsonDoc doc;
	int has_doc; /* doc holds the recording (freed with the host, whether or not it was usable) */
	const KpJson *player;
	char name[KP_NETGAME_NAME];
	uint32_t key_hash, seed;
	KpOracleNet *rec;
	KpNetGame game;
	int inited;
	const KpTemplates *templates;
	KpFlow *flow; /* the map lists' (MapsHost's) */
} NetHost;

static NetHost *net_of(void *host) { return (NetHost *)((KpNetUi *)host)->owner; }

static int net_providers(void *host, KpUiProvider *out, int max)
{
	const NetHost *h = net_of(host);
	int n = h->provider_count < max ? h->provider_count : max;
	memcpy(out, h->providers, (size_t)n * sizeof *out);
	return n;
}

/* InitializeSelectedProvider 0x4bafa0: the player's game over its recording (every provider initialized without one) */
static int net_select_provider(void *host, uint32_t id)
{
	NetHost *h = net_of(host);
	KpNetCalls calls;
	KpNetGameHooks hooks;
	KpNetGameConfig config;
	(void)id;
	if (!h->rec) return 1;
	if (h->inited) {
		kp_netgame_free(&h->game);
		h->inited = 0;
		h->nu.game = NULL;
	}
	calls = kp_oracle_net_calls(h->rec);
	hooks = kp_netui_game_hooks(&h->nu);
	memset(&config, 0, sizeof config);
	snprintf(config.player_name, sizeof config.player_name, "%s", h->name);
	config.key_hash = h->key_hash;
	config.templates = h->templates;
	if (h->flow) config.transfer = kp_flow_transfer_io(h->flow);
	if (kp_netgame_init(&h->game, &calls, &hooks, &config) != 0) return 0;
	h->inited = 1;
	h->nu.game = &h->game;
	return 1;
}

static int net_entry_map(void *owner, KpUi *ui, int index, KpMapHeader *mh, char *path, size_t size)
{
	NetHost *h = (NetHost *)owner;
	(void)ui;
	return h->flow != NULL && kp_flow_entry_map(h->flow, index, mh, path, size);
}

static int net_entry_save(void *owner, KpUi *ui, int index, KpNetGameHeader *h)
{
	NetHost *n = (NetHost *)owner;
	(void)ui;
	return n->flow != NULL && kp_flow_entry_save(n->flow, index, h);
}

static int net_find_save(void *owner, uint32_t id, KpNetGameSave *out)
{
	NetHost *n = (NetHost *)owner;
	return n->flow != NULL && kp_flow_find_save(n->flow, id, out);
}

static uint32_t net_time(void *owner) { return ((NetHost *)owner)->seed; }

/* Without a recording, the game the run created: Storm took it */
static uint32_t net_created(void *host, KpUi *ui, int index, uint8_t speed)
{
	(void)host;
	(void)ui;
	(void)index;
	(void)speed;
	return 0;
}

/* The game's recording of the line "net GOLDEN PLAYER": 1, 0 none, -1 unreadable */
static int net_recording(NetHost *h, const char *rest, KpUi *ui)
{
	char path[512], name[KP_NETGAME_NAME];
	const KpJson *root, *players;
	uint8_t *text;
	size_t size;
	char err[256];
	int k;
	if (sscanf(rest, "%511s %24s", path, name) != 2) return -1;
	if (!(text = kp_read_file(path, &size))) return -1;
	kp_json_doc_init(&h->doc);
	h->has_doc = 1;
	root = kp_json_parse(&h->doc, (const char *)text, size, err, sizeof err);
	free(text);
	if (!root || kp_json_field_int(root, "version", 0) != KP_ORACLE_NET_VERSION) return -1;
	players = kp_json_get(root, "players");
	for (k = 0; k < kp_json_len(players); k++) {
		const KpJson *p = kp_json_at(players, k), *calls = kp_json_get(p, "calls");
		KpNetGameHeader g;
		int j;
		if (strcmp(kp_json_field_str(p, "name", ""), name) == 0) h->player = p;
		for (j = 0; j < kp_json_len(calls); j++)
			if (kp_oracle_net_is(kp_json_at(calls, j), "SNetCreateLadderGame") &&
				kp_oracle_net_header(kp_json_at(calls, j), &g)) {
				h->key_hash = g.key_hash;
				h->seed = kp_oracle_net_seed(calls);
			}
	}
	if (!h->player) return -1;
	snprintf(h->name, sizeof h->name, "%s", name);
	if (!(h->rec = kp_oracle_net_new(h->player, &h->game, &ui->now))) return -1;
	return 1;
}

/* The run's providers: 1 when it has any (the screens get the host), 0 none, -1 a malformed line or recording. */
static int net_host_init(NetHost *h, const KpTraceFile *f, KpUi *ui)
{
	int k;
	memset(h, 0, sizeof *h);
	for (k = 0; k < f->count; k++) {
		const KpTraceRecord *r = &f->records[k];
		unsigned long long id, flags, max;
		int name = 0;
		KpUiProvider *p;
		if (strcmp(r->word, "net") == 0) {
			if (h->rec || net_recording(h, r->rest, ui) < 0) return -1;
			continue;
		}
		if (strcmp(r->word, "provider") != 0) continue;
		if (h->provider_count >= KP_UI_PROVIDERS || sscanf(r->rest, "%llu %llu %llu %n", &id, &flags, &max, &name) != 3
			|| name == 0) {
			return -1;
		}
		p = &h->providers[h->provider_count++];
		p->id = (uint32_t)id;
		p->flags = (uint32_t)flags;
		p->max_players = (uint32_t)max;
		snprintf(p->name, sizeof p->name, "%s", r->rest + name);
		p->name[strcspn(p->name, "\r")] = 0;
	}
	h->nu.ui = ui;
	h->nu.owner = h;
	h->nu.entry_map = net_entry_map;
	h->nu.entry_save = net_entry_save;
	h->nu.find_save = net_find_save;
	h->nu.time = net_time;
	kp_netui_hooks(&h->nu, &h->net);
	h->net.providers = net_providers;
	h->net.select_provider = net_select_provider;
	if (!h->rec) h->net.create_game = net_created;
	return h->provider_count > 0;
}

static void net_host_free(NetHost *h)
{
	if (h->inited) kp_netgame_free(&h->game);
	kp_oracle_net_free(h->rec);
	kp_netui_free(&h->nu);
	if (h->has_doc) kp_json_doc_free(&h->doc);
}

/* The map lists' host (phase N, stage N3): the product's own (kp_flow_interface_host) over a flow of the run's folders
 * only: "maps_pref DIR" the user's folder with its separator (its maps: the emulator's Documents\\Starcraft), "maps_install
 * DIR" the install's (its Maps). */
typedef struct {
	KpFlow *flow;
	KpAppFlags flags;
	char pref[512];
} MapsHost;

static void maps_log(void *ctx, const char *line)
{
	(void)ctx;
	fprintf(stderr, "menus_replay: %s\n", line);
}

static void maps_sync(void *ctx) { (void)ctx; }

/* The run's folders: 1 when it has them (ui gets the host), 0 none, -1 a malformed or long line. */
static int maps_host_init(MapsHost *h, const KpTraceFile *f, const KpData *d, KpUi *ui)
{
	int k, has = 0;
	memset(h, 0, sizeof *h);
	for (k = 0; k < f->count; k++) {
		const KpTraceRecord *r = &f->records[k];
		char *to;
		size_t size;
		if (strcmp(r->word, "maps_pref") == 0) {
			to = h->pref;
			size = sizeof h->pref;
		} else if (strcmp(r->word, "maps_install") == 0 && h->flags.res_dir_count == 0) {
			to = h->flags.res_dirs[h->flags.res_dir_count++];
			size = sizeof h->flags.res_dirs[0];
		} else {
			continue;
		}
		if (snprintf(to, size, "%s", r->rest) >= (int)size) return -1;
		to[strcspn(to, "\r")] = 0;
		has = 1;
	}
	if (!has) return 0;
	if (!h->pref[0] || d == NULL) return -1;
	h->flags.original_saves = 1; /* the emulator's user folder holds the original's saved network games */
	if (!(h->flow = (KpFlow *)calloc(1, sizeof *h->flow))) return -1;
	h->flow->host.log = maps_log;
	h->flow->host.sync_files = maps_sync;
	h->flow->flags = &h->flags;
	h->flow->data = d;
	h->flow->pref = h->pref;
	h->flow->ui = ui;
	kp_flow_interface_host(h->flow, ui);
	return 1;
}

typedef struct {
	Glue g;
	NetHost net;
	MapsHost maps;
	int64_t states[256];
	int state_count, expansion, at;
	KpOracleGame *games[64];
	int game_count;
	int room, room_state, room_opened, room_net;
	int chat_open; /* the chat room's first modal pass made (menus_chat_first_pass) */
	Progress saved;
} Menus;

/* What Koprulu draws differently on purpose, skipped in the glue's captures: on the main menu (gluMain.bin) the
 * version label (the remake's own text) and the labels of the buttons outside the remake (Campaign Editor, and
 * Multiplayer without a network), hidden; on the connection screen (gluConn.bin) the gateway list (control 12):
 * Battle.net's gateways from the registry, outside phase N (the atlas excludes them). */
#define MAIN_MULTI 4
#define MAIN_EDITOR 5
#define MAIN_VERSION 10
#define CONN_GATEWAYS 12
static void koprulu_masks(KpUi *ui, int32_t screen, Bitmap *b)
{
	char bin[64];
	kp_oracle_ui_bin_name(ui, ui->controls[screen].bin, 1, bin, sizeof bin);
	if (strcmp(bin, "rez\\glumain.bin") == 0) {
		bitmap_mask_control(b, ui, screen, MAIN_VERSION);
		bitmap_mask_control(b, ui, screen, MAIN_EDITOR);
		if (ui->net == NULL) bitmap_mask_control(b, ui, screen, MAIN_MULTI);
	} else if (strcmp(bin, "rez\\gluconn.bin") == 0) {
		bitmap_mask_control(b, ui, screen, CONN_GATEWAYS);
	}
}

static void menus_captures(Menus *m, int64_t before)
{
	Glue *g = &m->g;
	KpUi *ui = g->u.ui;
	while (g->next_cap < g->cap_count && g->caps[g->next_cap].tick < before) {
		const KpTraceCapture *cap = &g->caps[g->next_cap++];
		int32_t d, bottom = -1;
		for (d = ui->open; d >= 0; d = ui->controls[d].next) bottom = d; /* the glue screen the popups are composited into */
		if (bottom >= 0) {
			const KpUiSurface *s = &ui->controls[bottom].bitmap;
			kp_ui_add_dirty_rect(ui, 0, 0, KP_UI_SCREEN_W - 1, KP_UI_SCREEN_H - 1);
			kp_ui_present(ui);
			if (s->pixels && s->w == 640 && s->h == 480) {
				Bitmap b;
				bitmap_init(&b, s);
				koprulu_masks(ui, bottom, &b);
				if (kp_trace_compare_pixels(cap, bitmap_pixel, &b, 0, 0, s->w - 1, s->h - 1, getenv("ROOM_DUMP")) > 0) {
					g->pixel_differences++;
				}
			}
			if (bottom == ui->title.dialog && palette_differences(cap, ui) > 0) { /* title.pcx's, set by InitTitleScreen */
				fprintf(stderr, "%s: the title's palette differs\n", cap->name);
				g->pixel_differences++;
			}
		}
	}
}

/* The game a network lobby started (its map, setup and 0x48 seed) for its race ready room's briefing */
static KpOracleGame *start_net_game(const KpNetGame *ng, int expansion)
{
	KpOracleGame *og = (KpOracleGame *)calloc(1, sizeof *og);
	KpOracleOpts opts;
	KpGameSetup setup;
	KpError err;
	char key[sizeof KP_ORACLE_MAPS + KP_TRANSFER_PATH];
	if (!og) exit(kp_cli_fail("out of memory"));
	kp_oracle_opts(&opts);
	kp_netgame_game_setup(ng, &setup);
	opts.host.setup = &setup;
	opts.host.local_storm = ng->local_storm;
	snprintf(key, sizeof key, "%s%s", KP_ORACLE_MAPS, kp_netgame_map_path(ng));
	if (kp_oracle_game_start(og, ".", key, ng->seed, expansion, &opts, &err) != 0) exit(kp_cli_fail("%s", err.msg));
	return og;
}

static void menus_follow(Menus *m)
{
	Glue *g = &m->g;
	KpUi *ui = g->u.ui;
	KpUiMenus *mm = &ui->menus;
	if (kp_ui_menus_poll(ui)) {
		int state = mm->state, action = mm->action, k;
		mm->action = KP_UI_MENU_NONE;
		if (m->state_count < 256) m->states[m->state_count++] = state;
		m->expansion = mm->expansion;
		if (action == KP_UI_MENU_EXIT) return;
		if (action == KP_UI_MENU_NETWORK && room_race(state) >= 0 && m->net.inited) {
			/* a Use Map Settings game's race ready room over the game the lobby started */
			KpOracleGame *og = start_net_game(&m->net.game, mm->expansion);
			if (m->game_count < 64) m->games[m->game_count++] = og;
			ui->game = og->g;
			if (kp_ui_ready_room_open(ui, room_race(state)) < 0) exit(kp_cli_fail("ui: the ready room: %s", ui->error));
			kp_ui_ready_room_continue(ui);
			m->room = m->room_net = 1;
			m->room_state = state;
			m->room_opened = 0;
			take(g);
			return;
		}
		/* TODO: stage N4 plays the network game the chat room or its ready room started; the run's menus end there */
		if (action == KP_UI_MENU_NETWORK) return;
		if (action == KP_UI_MENU_MISSION && room_race(state) >= 0) {
			KpOracleGame *og;
			if (m->saved.set) {
				Progress back = m->saved;
				set_progress(ui, &back, &m->saved);
				m->saved.set = 0;
			}
			og = start_game(mm->mission, mm->expansion);
			if (m->game_count < 64) m->games[m->game_count++] = og;
			/* the establishing text's first page is shown when the credits screen has loaded: its record's tick */
			for (k = m->at + 1; k < g->file.count; k++) {
				const KpTraceRecord *r = &g->file.records[k];
				if (strcmp(r->word, "page") == 0) ui->now = (uint32_t)first_number(r->rest);
				if (strcmp(r->word, "page") == 0 || strcmp(r->word, "pass") == 0 || strcmp(r->word, "ev") == 0) break;
			}
			ui->game = og->g;
			if (kp_ui_ready_room_open(ui, room_race(state)) < 0) exit(kp_cli_fail("ui: the ready room: %s", ui->error));
			kp_ui_ready_room_continue(ui);
			m->room = 1;
			m->room_state = state;
			m->room_opened = 0;
		} else {
			kp_ui_menus_enter(ui, state);
		}
		take(g);
	}
	if (m->room) {
		int32_t d;
		int open = 0;
		kp_ui_ready_room_continue(ui);
		for (d = ui->open; d >= 0; d = ui->controls[d].next) open |= d == ui->briefing.dialog;
		if (open) {
			m->room_opened = 1;
		} else if (m->room_opened) { /* the room ended: Cancel goes back to the campaign menu, a network game's by its
		                               * provider (SetGlueStateFromProvider 0x457e50) */
			m->room = 0;
			if (m->room_net && ui->result == RESULT_START) return; /* TODO: stage N4's network game */
			kp_ui_menus_enter(ui, m->room_net ? kp_ui_menus_provider_state(ui) : m->expansion == 0 ? 6 : 22);
			m->room_net = 0;
			take(g);
		}
	}
}

/* RunModalDialog 0x4c35c0's first pass over the chat room: its idle callback (LobbyUpdate 0x450d80, installed by
 * LobbyJoinCheck 0x450ea0 when the dialog started) runs at the tick the room opened, before the loop waits (the input
 * log's idle events come every 100 ms after it); a resumed save's first refresh locks the rows there */
static void menus_chat_first_pass(Menus *m)
{
	KpUi *ui = m->g.u.ui;
	const KpUiChat *c = &ui->menus.chat;
	int open = ui->net != NULL && c->polling && c->dialog >= 0;
	if (open && !m->chat_open) {
		m->chat_open = 1;
		kp_ui_lobby_idle(ui);
		take(&m->g);
	} else if (!open) {
		m->chat_open = 0;
	}
}

/* The chat room's passes at the original's messages before an action at tick: its modal loop handled each as it came
 * (PollLobby 0x474f00 takes one per pass, LobbyUpdate 0x450d80 running about 670 times a second) */
static void menus_messages(Menus *m, int64_t tick)
{
	Glue *g = &m->g;
	KpUi *ui = g->u.ui;
	uint32_t at;
	int index;
	if (m->net.rec == NULL || ui->net == NULL) return;
	while ((index = kp_oracle_net_next_message(m->net.rec, &at)) >= 0 && (int64_t)at < tick) {
		menus_captures(m, at);
		if ((int32_t)(at - ui->now) > 0) ui->now = at;
		kp_ui_lobby_idle(ui);
		take(g);
		if (kp_oracle_net_next_message(m->net.rec, &at) == index) break; /* the room took none: not polling */
	}
}

int kp_cli_menus_replay(int argc, char **argv)
{
	static const char *const WORDS[] = { "start", "title", NULL };
	Menus *m;
	Glue *g;
	KpUi *ui;
	Progress *progress_at;
	uint32_t *seeds;
	int k, seed_count = 0, run = 0, last_ev = -1, status;
	int64_t page_tick = -1, differences, start, title;
	int dumped = 0, title_lost = 0;
	char title_end[64] = "";
	if (argc < 1) return kp_cli_fail("usage: menus_replay MENUS.txt");
	m = (Menus *)calloc(1, sizeof *m);
	if (!m) return kp_cli_fail("out of memory");
	g = &m->g;
	if (glue_start(g, argv[0], WORDS, KP_ORACLE_UI_STRINGS | KP_ORACLE_UI_PORTRAITS | KP_ORACLE_UI_SOUNDS
		| KP_ORACLE_UI_CAMPAIGN, NULL) != 0) {
		return 1;
	}
	ui = g->u.ui;
	ui->menus.profile_count = 0;
	switch (net_host_init(&m->net, &g->file, ui)) {
	case -1:
		net_host_free(&m->net);
		return kp_cli_fail("menus_replay: a malformed provider or net line, or its recording unreadable");
	case 1: { /* the multiplayer screens: Create Game's templates are the data's */
		KpError err;
		const KpData *d = kp_oracle_data(".", &err);
		if (d == NULL) return kp_cli_fail("%s", err.msg);
		ui->net = &m->net.net;
		ui->templates = &d->templates;
		m->net.templates = &d->templates;
		if (maps_host_init(&m->maps, &g->file, d, ui) < 0) return kp_cli_fail("menus_replay: malformed maps lines");
		m->net.flow = m->maps.flow;
		break;
	}
	default: break;
	}
	kp_trace_clocks(&g->u, &g->clocks);
	glue_crt(g);
	/* the font keys' bursts of CRT draws end with srand(time(NULL)): the clock is the next draw's state */
	seeds = (uint32_t *)calloc((size_t)g->file.count + 1, sizeof *seeds);
	progress_at = (Progress *)calloc((size_t)g->file.count + 1, sizeof *progress_at);
	if (!seeds || !progress_at) return kp_cli_fail("out of memory");
	for (k = 0; k < g->file.count; k++) {
		const KpTraceRecord *r = &g->file.records[k];
		if (strcmp(r->word, "rand") == 0) {
			if (run == -1) {
				seeds[seed_count - 1] = (uint32_t)strtoull(r->rest, NULL, 10);
				run = 0;
			}
			run++;
		} else if (run >= 100) {
			seeds[seed_count++] = 0;
			run = -1;
		} else if (run > 0) {
			run = 0;
		}
	}
	free(g->u.time_seeds);
	g->u.time_seeds = seeds;
	ui->menus.time_seeds = seeds;
	ui->menus.time_seed_count = seed_count;
	ui->menus.time_seed_next = 0;
	/* the profile's progress StartCampaignEpisode read in a run: kp_ui's profile has it while the key that starts the
	 * episode is handled, until the episode's mission starts */
	for (k = 0; k < g->file.count; k++) {
		const KpTraceRecord *r = &g->file.records[k];
		if (strcmp(r->word, "ev") == 0) last_ev = k;
		if (strcmp(r->word, "progress") == 0 && last_ev >= 0) {
			long long field, value;
			if (sscanf(r->rest, "%lld %lld", &field, &value) == 2) {
				progress_at[last_ev].field = (int)field;
				progress_at[last_ev].value = (uint32_t)value;
				progress_at[last_ev].set = 1;
			}
		}
	}
	/* the title (CreateTitleScreen 0x4b5570) when the run has it, until RunGlueLoop 0x457510's DestroyTitleDialog
	 * 0x4b55b0: DestroyTitleScreen 0x4b54c0 deletes its loading text's timer; then the main menu at the tick of its
	 * first timer */
	start = kp_trace_header(&g->file, "start", 0);
	title = kp_trace_header(&g->file, "title", -1);
	ui->now = (uint32_t)(title >= 0 ? title : start);
	if (title >= 0) {
		if (kp_ui_title_open(ui) < 0) return kp_cli_fail("ui: the title: %s", ui->error);
		if (ui->title.text < 0) return kp_cli_fail("ui: the title has no loading text");
		snprintf(title_end, sizeof title_end, "%s%d %d", TITLE_REF, (int)ui->controls[ui->title.text].record,
			KP_UI_TITLE_BLINK_TIMER);
	} else {
		kp_ui_menus_enter(ui, 0);
	}
	take(g);
	for (k = 0; k < g->file.count; k++) {
		const KpTraceRecord *r = &g->file.records[k];
		int64_t tick = record_tick(r);
		m->at = k;
		if (tick >= 0) {
			menus_messages(m, tick);
			menus_captures(m, tick);
		}
		if (strcmp(r->word, "del") == 0 && kp_ui_title_is_open(ui) && is_deletion(r->rest, title_end)) {
			menus_captures(m, start); /* the captures before the main menu are the title's (its Sleep runs no timer) */
			kp_ui_title_close(ui);
			ui->now = (uint32_t)start;
			kp_ui_menus_enter(ui, 0);
			take(g);
		} else if (strcmp(r->word, "pass") == 0) {
			const char *dump_at = getenv("DUMP_AT");
			if (dump_at && first_number(r->rest) >= strtoll(dump_at, NULL, 10) && !dumped) {
				int32_t order[KP_UI_TIMERS];
				int n = kp_ui_timer_order(&ui->timers, order, KP_UI_TIMERS), i;
				dumped = 1;
				for (i = 0; i < n; i++) {
					const KpUiTimer *t = &ui->timers.nodes[order[i]];
					if (t->owner >= 0 && t->owner < KP_UI_CONTROLS && ui->controls[t->owner].used) {
						fprintf(stderr, "%lld#%d/%d@%u ", (long long)t->owner, (int)ui->controls[t->owner].record, (int)t->id,
							(unsigned)(t->last + t->period));
					} else {
						fprintf(stderr, "%lld#nil/%d@%u ", (long long)t->owner, (int)t->id, (unsigned)(t->last + t->period));
					}
				}
				fprintf(stderr, "\n");
			}
			kp_ui_tick(ui, (uint32_t)first_number(r->rest));
			take(g);
		} else if (strcmp(r->word, "page") == 0) {
			page_tick = first_number(r->rest);
		} else if (strcmp(r->word, "ev") == 0) {
			int64_t t, kind, a, b;
			ev_fields(r->rest, &t, &kind, &a, &b);
			ui->now = (uint32_t)(page_tick >= 0 ? page_tick : t);
			page_tick = -1;
			if (progress_at[k].set) set_progress(ui, &progress_at[k], &m->saved); /* the log's field is 0-based */
			kp_oracle_ui_input(ui, kind, a, b);
			/* the chat room's idle callback (LobbyUpdate 0x450d80) at the run's idle events */
			if (kind == IDLE && ui->net != NULL) kp_ui_lobby_idle(ui);
			take(g);
		} else if (strcmp(r->word, "turn") == 0) {
			ui->now = (uint32_t)first_number(r->rest);
			kp_ui_ready_room_turn(ui);
			if (ui->net != NULL) kp_ui_lobby_idle(ui); /* the chat room's pass that made the turn */
			take(g);
		}
		menus_follow(m);
		menus_chat_first_pass(m);
	}
	menus_captures(m, INT64_MAX);
	if (m->net.rec != NULL) {
		char first[512];
		long n = kp_oracle_net_differences(m->net.rec, first, sizeof first);
		if (n > 0) {
			fprintf(stderr, "menus_replay: %ld SNet calls differ from the original's; the first %s\n", n, first);
			g->pixel_differences += n;
		}
	}
	if (kp_ui_title_is_open(ui)) { /* no record of DestroyTitleScreen's deletion: the main menu never opened */
		fprintf(stderr, "menus_replay: the title never closed (no \"del %s\")\n", title_end);
		title_lost = 1;
	}
	differences = kp_trace_compare(&g->want, &g->have) + g->pixel_differences + title_lost;
	printf("menus_replay: %d timer records, %d videos, %d captures, states", g->want.s[KP_TRACE_TIMERS].count,
		g->want.s[KP_TRACE_VIDEOS].count, g->cap_count);
	for (k = 0; k < m->state_count; k++) printf(" %lld", (long long)m->states[k]);
	printf(", %lld differences\n", (long long)differences);
	status = differences == 0 ? 0 : 1;
	for (k = 0; k < m->game_count; k++) free_game(m->games[k]);
	ui->game = NULL;
	free(progress_at);
	glue_free(g);
	net_host_free(&m->net);
	free(m->maps.flow);
	free(m);
	return status;
}

/* --- room_replay --- */

static void room_captures(Glue *g, int64_t before)
{
	KpUi *ui = g->u.ui;
	while (g->next_cap < g->cap_count && g->caps[g->next_cap].tick < before) {
		const KpTraceCapture *cap = &g->caps[g->next_cap++];
		int32_t d;
		int open = 0;
		for (d = ui->open; d >= 0; d = ui->controls[d].next) open |= d == ui->briefing.dialog;
		if (ui->briefing.dialog >= 0 && open) {
			const KpUiSurface *s = &ui->controls[ui->briefing.dialog].bitmap; /* as the last repaint left it */
			Bitmap b;
			int64_t differ;
			bitmap_init(&b, s);
			if (s->pixels && kp_trace_compare_pixels(cap, bitmap_pixel, &b, 0, 0, s->w - 1, s->h - 1, getenv("ROOM_DUMP")) > 0) {
				g->pixel_differences++;
			}
			/* the screen's palette while the room's movies play (SVid writes their indices, the room's palette stays) */
			differ = palette_differences(cap, ui);
			if (differ > 0) {
				printf("capture %s at %lld: %lld palette bytes differ\n", cap->name, (long long)cap->tick, (long long)differ);
				g->pixel_differences++;
			}
		}
	}
}

int kp_cli_room_replay(int argc, char **argv)
{
	static const char *const WORDS[] = { "expansion", "mission", "race", "start", "room", NULL };
	Glue g;
	KpUi *ui;
	KpOracleGame *og;
	int k, has_turns = 0, room_open = 0, status;
	int64_t page_tick = -1, room_tick, differences;
	if (argc < 1) return kp_cli_fail("usage: room_replay ROOM.txt");
	if (glue_start(&g, argv[0], WORDS, KP_ORACLE_UI_STRINGS | KP_ORACLE_UI_PORTRAITS | KP_ORACLE_UI_SOUNDS, NULL) != 0) return 1;
	ui = g.u.ui;
	og = start_game(kp_trace_header(&g.file, "mission", 0), (int)kp_trace_header(&g.file, "expansion", 0));
	/* the run: the room opens at its first record's tick, the menus' CRT state from the first draw's */
	ui->now = (uint32_t)kp_trace_header(&g.file, "start", 0);
	glue_crt(&g);
	for (k = 0; k < g.file.count; k++) has_turns |= strcmp(g.file.records[k].word, "turn") == 0;
	kp_trace_clocks(&g.u, &g.clocks);
	/* the establishing text's first page is shown when the credits screen has loaded: its record's tick is the clock's */
	for (k = 0; k < g.file.count; k++) {
		const KpTraceRecord *r = &g.file.records[k];
		if (strcmp(r->word, "page") == 0) ui->now = (uint32_t)first_number(r->rest);
		if (strcmp(r->word, "page") == 0 || strcmp(r->word, "pass") == 0 || strcmp(r->word, "ev") == 0) break;
	}
	ui->game = og->g;
	if (kp_ui_ready_room_open(ui, (int)kp_trace_header(&g.file, "race", 0)) < 0) return kp_cli_fail("ui: the ready room: %s", ui->error);
	kp_ui_ready_room_continue(ui);
	take(&g);
	room_tick = kp_trace_header(&g.file, "room", 0);
	for (k = 0; k < g.file.count; k++) {
		const KpTraceRecord *r = &g.file.records[k];
		int64_t tick = record_tick(r);
		if (tick >= 0) {
			room_captures(&g, tick);
			if (!room_open && tick >= room_tick) {
				room_open = 1;
				ui->now = (uint32_t)room_tick;
				kp_ui_ready_room_continue(ui);
				take(&g);
			}
		}
		if (strcmp(r->word, "pass") == 0) {
			kp_ui_tick(ui, (uint32_t)first_number(r->rest));
			take(&g);
		} else if (strcmp(r->word, "page") == 0) { /* the credits' handler runs inside the event's dispatch */
			page_tick = first_number(r->rest);
		} else if (strcmp(r->word, "ev") == 0) {
			int64_t t, kind, a, b;
			ev_fields(r->rest, &t, &kind, &a, &b);
			ui->now = (uint32_t)(page_tick >= 0 ? page_tick : t);
			page_tick = -1;
			kp_oracle_ui_input(ui, kind, a, b);
			if (kind == IDLE && !has_turns) kp_ui_ready_room_idle(ui);
			take(&g);
		} else if (strcmp(r->word, "turn") == 0) { /* the original's turn steps, where the log has them */
			ui->now = (uint32_t)first_number(r->rest);
			kp_ui_ready_room_turn(ui);
			take(&g);
		}
		/* the glue loop repaints the invalid region after its timers and its messages (PaintDialogLayer 0x4c4d90) */
		if (strcmp(r->word, "pass") == 0 || strcmp(r->word, "ev") == 0 || strcmp(r->word, "turn") == 0) kp_ui_present(ui);
	}
	differences = kp_trace_compare(&g.want, &g.have) + g.pixel_differences;
	printf("room_replay: mission %lld race %lld: %d timer records, %d actions, %d videos, %d captures, %lld differences\n",
		(long long)kp_trace_header(&g.file, "mission", 0), (long long)kp_trace_header(&g.file, "race", 0),
		g.want.s[KP_TRACE_TIMERS].count, g.want.s[KP_TRACE_BRIEF].count, g.want.s[KP_TRACE_VIDEOS].count, g.cap_count,
		(long long)differences);
	status = differences == 0 ? 0 : 1;
	ui->game = NULL;
	free_game(og);
	glue_free(&g);
	return status;
}

/* --- result_replay --- */

static int result_owner(const char *line) { return strstr(line, "mission.bin#") != NULL; }
static int score_owner(const char *line) { return strstr(line, "gluscore.bin#") != NULL; }

/* The streams of the game's and the next screen's records, which these replays do not compare. */
static void only_timers(KpTraceStreams *s)
{
	int k;
	KpTraceStreams other;
	memset(&other, 0, sizeof other);
	for (k = 0; k < KP_TRACE_STREAMS; k++) {
		if (k == KP_TRACE_TIMERS || k == KP_TRACE_RANDS) continue;
		other.s[k] = s->s[k];
		memset(&s->s[k], 0, sizeof s->s[k]);
	}
	kp_trace_streams_free(&other);
}

typedef struct {
	const uint8_t *mine, *under, *theirs;
	size_t theirs_size, under_size;
	int64_t w, l, t;
	uint8_t row[256], blends[256];
	int colour;
	int64_t *moved;
} Translucent;

/* The modeless captures: the dialog's opaque pixels must equal the capture's; a translucent one (the border colour)
 * must be a value of the blend row, and over the view the dialog first blended over ("under") it is counted when it
 * differs. */
static int translucent_pixel(void *ud, int64_t x, int64_t y)
{
	Translucent *p = (Translucent *)ud;
	int v = p->mine[(y - p->t) * p->w + (x - p->l)];
	size_t at = (size_t)(y * 640 + x);
	if (v == 0) return -1; /* the view: not recorded */
	if (v == p->colour) {
		int c = at < p->theirs_size ? p->theirs[at] : -1, u = at < p->under_size ? p->under[at] : 0;
		if (c != p->row[u]) (*p->moved)++;
		return c >= 0 && p->blends[c] ? c : -2;
	}
	return v;
}

/* The run of a glue screen's records: the captures before a pass's or an ev's tick go to capture, a pass ticks the
 * interface, an ev is its input (then after_ev, when given). Returns the timer records' differences and the pixels'. */
static int64_t glue_run(Glue *g, void (*capture)(Glue *g, const KpTraceCapture *cap, void *ud),
	void (*after_ev)(Glue *g, void *ud), void *ud)
{
	KpUi *ui = g->u.ui;
	int k;
	for (k = 0; k <= g->file.count; k++) {
		const KpTraceRecord *r = k < g->file.count ? &g->file.records[k] : NULL;
		int64_t tick = !r ? INT64_MAX : strcmp(r->word, "pass") == 0 || strcmp(r->word, "ev") == 0 ? first_number(r->rest) : -1;
		while (tick >= 0 && g->next_cap < g->cap_count && g->caps[g->next_cap].tick < tick) capture(g, &g->caps[g->next_cap++], ud);
		if (!r) break;
		if (strcmp(r->word, "pass") == 0) {
			kp_ui_tick(ui, (uint32_t)first_number(r->rest));
			take(g);
		} else if (strcmp(r->word, "ev") == 0) {
			int64_t t, kind, a, b;
			ev_fields(r->rest, &t, &kind, &a, &b);
			ui->now = (uint32_t)t;
			kp_oracle_ui_input(ui, kind, a, b);
			if (after_ev) after_ev(g, ud);
			take(g);
		}
	}
	return kp_trace_compare(&g->want, &g->have) + g->pixel_differences;
}

typedef struct {
	int64_t moved; /* translucent pixels over a redrawn view */
	int ended;
} ResultRun;

/* The modeless result dialog's pixels against a capture's. */
static void result_capture(Glue *g, const KpTraceCapture *cap, void *ud)
{
	KpUi *ui = g->u.ui;
	ResultRun *run = (ResultRun *)ud;
	Translucent p;
	const KpUiSurface *s;
	size_t i;
	uint8_t *under, *theirs;
	if (ui->modeless < 0 || !cap->has_rect || !cap->has_under) return;
	kp_ui_paint_dialog(ui, ui->modeless);
	s = &ui->controls[ui->modeless].bitmap;
	memset(&p, 0, sizeof p);
	kp_ui_canvas_blend_row(ui, p.row);
	p.colour = ui->effects.colour[0];
	for (i = 0; i < 256; i++) p.blends[p.row[i]] = 1;
	under = kp_cli_read(cap->under, &p.under_size);
	theirs = kp_cli_read(cap->path, &p.theirs_size);
	p.mine = s->pixels;
	p.under = under;
	p.theirs = theirs;
	p.w = s->w;
	p.l = cap->rect[0];
	p.t = cap->rect[1];
	p.moved = &run->moved;
	if (kp_trace_compare_pixels(cap, translucent_pixel, &p, cap->rect[0], cap->rect[1], cap->rect[2], cap->rect[3],
		getenv("ROOM_DUMP")) > 0) {
		g->pixel_differences++;
	}
	free(under);
	free(theirs);
}

/* The game loop ends after the message that set the exit: EndGame. */
static void result_ev(Glue *g, void *ud)
{
	ResultRun *run = (ResultRun *)ud;
	if (g->u.ui->game_exit != 0 && !run->ended) {
		run->ended = 1;
		kp_ui_end_game(g->u.ui);
	}
}

int kp_cli_result_replay(int argc, char **argv)
{
	static const char *const WORDS[] = { "expansion", "mission", "start", "result", "exit", NULL };
	Glue g;
	KpUi *ui;
	KpOracleGame *og;
	KpError err;
	ResultRun run;
	int status;
	int64_t differences, exit_code;
	if (argc < 1) return kp_cli_fail("usage: result_replay RESULT.txt");
	if (glue_start(&g, argv[0], WORDS, KP_ORACLE_UI_STRINGS | KP_ORACLE_UI_SOUNDS, result_owner) != 0) return 1;
	ui = g.u.ui;
	og = start_game(kp_trace_header(&g.file, "mission", 0), (int)kp_trace_header(&g.file, "expansion", 0));
	if (kp_oracle_ui_colours(&g.u, ".", og, 0, &err) != 0) return kp_cli_fail("%s", err.msg);
	kp_ui_font_set(ui, 1);
	if (kp_ui_apply_theme(ui, kp_oracle_race_theme(og->g->players.race[og->g->local_player])) != 0) {
		return kp_cli_fail("ui: no GRP %s", kp_oracle_race_theme(og->g->players.race[og->g->local_player]));
	}
	only_timers(&g.want);
	kp_trace_clocks(&g.u, &g.clocks);
	ui->now = (uint32_t)kp_trace_header(&g.file, "start", 0);
	kp_ui_show_result(ui, (int)kp_trace_header(&g.file, "result", 0));
	take(&g);
	memset(&run, 0, sizeof run);
	differences = glue_run(&g, result_capture, result_ev, &run);
	exit_code = kp_trace_header(&g.file, "exit", -1);
	if (exit_code >= 0 && ui->game_exit != exit_code) {
		differences++;
		printf("exit: Koprulu %d, the original %lld\n", (int)ui->game_exit, (long long)exit_code);
	}
	printf("result_replay: mission %lld result %lld: %d timer records, %d captures, exit %d, %lld translucent pixels over a "
		"redrawn view, %lld differences\n", (long long)kp_trace_header(&g.file, "mission", 0),
		(long long)kp_trace_header(&g.file, "result", 0), g.want.s[KP_TRACE_TIMERS].count, g.cap_count, (int)ui->game_exit,
		(long long)run.moved, (long long)differences);
	status = differences == 0 ? 0 : 1;
	free_game(og);
	glue_free(&g);
	return status;
}

/* --- score_replay --- */

static const char *const SCORE_ARRAYS[KP_UI_SCORE_ARRAYS] = { "minerals", "gas", "cumulative_gas", "cumulative_minerals",
	"men_produced", "men_lost", "men_killed", "unit_score", "kill_score", "buildings_constructed", "buildings_lost",
	"buildings_razed", "structure_score", "razing_score" };

/* The score data of uilog.py's "score" record (tools/room_check.py). */
static void score_data(const KpJson *d, KpUiScoreData *out)
{
	const KpJson *roster = kp_json_get(d, "roster"), *arrays = kp_json_get(d, "arrays");
	int k, j;
	memset(out, 0, sizeof *out);
	for (k = 0; k < KP_UI_SCORE_SLOTS && k < kp_json_len(roster); k++) {
		const KpJson *e = kp_json_at(roster, k);
		if (!kp_json_is(e, KP_JSON_OBJECT)) continue;
		snprintf(out->roster[k].name, sizeof out->roster[k].name, "%s", kp_json_field_str(e, "name", ""));
		snprintf(out->roster[k].force, sizeof out->roster[k].force, "%s", kp_json_field_str(e, "force", ""));
		out->roster[k].index = (uint32_t)kp_json_field_int(e, "index", 0);
		out->roster[k].storm = (uint32_t)kp_json_field_int(e, "storm", 0);
		out->roster[k].type = (uint32_t)kp_json_field_int(e, "type", 0);
		out->roster[k].race = (uint8_t)kp_json_field_int(e, "race", 0);
	}
	for (k = 0; k < KP_UI_SCORE_ARRAYS; k++) {
		const KpJson *a = kp_json_get(arrays, SCORE_ARRAYS[k]);
		for (j = 0; j < 12 && j < kp_json_len(a); j++) out->arrays[k][j] = (int32_t)kp_json_int(kp_json_at(a, j), 0);
	}
	for (k = 0; k < 12 && k < kp_json_len(kp_json_get(d, "colours")); k++) {
		out->colours[k] = (uint8_t)kp_json_int(kp_json_at(kp_json_get(d, "colours"), k), 0);
	}
	for (k = 0; k < 8 && k < kp_json_len(kp_json_get(d, "player_colours")); k++) {
		out->player_colours[k] = (uint8_t)kp_json_int(kp_json_at(kp_json_get(d, "player_colours"), k), 0);
	}
	out->count = (int)kp_json_field_int(d, "count", 0);
	out->result = (int)kp_json_field_int(d, "result", 0);
	out->race = (int)kp_json_field_int(d, "race", 0);
	out->local_storm = (uint32_t)kp_json_field_int(d, "local_storm", 0);
	out->elapsed = (uint32_t)kp_json_field_int(d, "elapsed", 0);
	out->replay = (int)kp_json_field_int(d, "replay", 0);
}

/* The score dialog's pixels against a capture's. */
static void score_capture(Glue *g, const KpTraceCapture *cap, void *ud)
{
	KpUi *ui = g->u.ui;
	int32_t dialog = ui->score.dialog;
	Bitmap b;
	(void)ud;
	if (dialog < 0) return;
	kp_ui_paint_dialog(ui, dialog);
	bitmap_init(&b, &ui->controls[dialog].bitmap);
	if (b.s->pixels && kp_trace_compare_pixels(cap, bitmap_pixel, &b, 0, 0, b.s->w - 1, b.s->h - 1, getenv("ROOM_DUMP")) > 0) {
		g->pixel_differences++;
	}
}

int kp_cli_score_replay(int argc, char **argv)
{
	static const char *const WORDS[] = { "expansion", "mission", "start", NULL };
	Glue g;
	KpUi *ui;
	KpJsonDoc doc;
	const KpJson *data = NULL;
	KpUiScoreData d;
	int k, status;
	int64_t differences;
	if (argc < 1) return kp_cli_fail("usage: score_replay SCORE.txt");
	if (glue_start(&g, argv[0], WORDS, KP_ORACLE_UI_STRINGS | KP_ORACLE_UI_SOUNDS, score_owner) != 0) return 1;
	ui = g.u.ui;
	kp_json_doc_init(&doc);
	for (k = 0; k < g.file.count; k++) {
		if (strcmp(g.file.records[k].word, "data") == 0) {
			char why[200];
			data = kp_json_parse(&doc, g.file.records[k].rest, strlen(g.file.records[k].rest), why, sizeof why);
			if (!data) return kp_cli_fail("score_replay: the data line: %s", why);
		}
	}
	if (!data) return kp_cli_fail("score_replay: no data line");
	only_timers(&g.want);
	kp_trace_clocks(&g.u, &g.clocks);
	ui->now = (uint32_t)kp_trace_header(&g.file, "start", 0);
	score_data(data, &d);
	d.mission = kp_trace_header(&g.file, "mission", 0); /* 0x526524: the chains' campaign rows keep Save Replay off */
	kp_ui_score_open(ui, &d);
	take(&g);
	differences = glue_run(&g, score_capture, NULL, NULL);
	printf("score_replay: mission %lld state %lld: %d timer records, %d captures, %lld differences\n",
		(long long)kp_trace_header(&g.file, "mission", 0), (long long)kp_json_field_int(data, "glue_state", 0),
		g.want.s[KP_TRACE_TIMERS].count, g.cap_count, (long long)differences);
	status = differences == 0 ? 0 : 1;
	kp_json_doc_free(&doc);
	glue_free(&g);
	return status;
}
