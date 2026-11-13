/* The interface layer kp_ui (ADR 0010) on its own: the UI timer queue of timer.cpp (atlas P43) on synthetic cases and
 * the original's timer passes of an emulator run, the text engine against the original's probes, the panels and the
 * console over a replayed game against the original's captures, the keys, the selection commands, the result dialogs,
 * the game menus and the console's work over native games. The console's host and the presentation's canvas and layout
 * are tested in test_hud.c, over the interface's C host (native/kp_app/kp_hud.h) and kp_present's canvas and layout. */
#include "kp_test_ui.h"
#include "kp_text.h"

#include "../../native/kp_core/src/core/kp_ui_text.h"

#define TERRAN01 "missions/terran/terran01"

static const char *const UI_FILES[] = { KP_T_DATA, NULL };
static const char *const GAME_FILES[] = { KP_T_DATA, TERRAN01, NULL };

/* --- the timer queue --- */

typedef struct {
	KpUiTimers q;
	char order[512];
} Queue;

static const char *owners_of(Queue *q)
{
	int32_t order[KP_UI_TIMERS];
	int n = kp_ui_timer_order(&q->q, order, KP_UI_TIMERS), k;
	size_t at = 0;
	q->order[0] = '\0';
	for (k = 0; k < n && at < sizeof q->order; k++) {
		const KpUiTimer *t = &q->q.nodes[order[k]];
		at += (size_t)snprintf(q->order + at, sizeof q->order - at, "%s%lld/%d", k ? " " : "", (long long)t->owner, (int)t->id);
	}
	return q->order;
}

static Queue *queue(KpT *T)
{
	Queue *q = (Queue *)calloc(1, sizeof *q);
	kp_t_ok(T, q != NULL, "out of memory");
	kp_t_defer(T, free, q);
	kp_ui_timers_init(&q->q);
	return q;
}

static void set(KpT *T, Queue *q, int64_t owner, uint16_t id, uint32_t period, int callback, uint32_t now)
{
	kp_t_ok(T, kp_ui_set_timer(&q->q, owner, id, period, callback, now) == 0, "timer pool full");
}

static void timers_sort_by_due_time(KpT *T)
{
	Queue *q = queue(T);
	set(T, q, 1, 1, 100, 0, 1000); /* due 1100 */
	set(T, q, 2, 1, 50, 0, 1000);  /* due 1050 */
	set(T, q, 3, 1, 50, 0, 1000);  /* due 1050: before the equal one */
	kp_t_eq_str(T, owners_of(q), "3/1 2/1 1/1", NULL);
}

typedef struct {
	int count;
	int64_t owner[16];
	uint16_t id[16];
	int callback[16];
	uint32_t period[16];
	Queue *q;
	KpT *T;
} Fired;

static void record_fire(void *ctx, int64_t owner, uint16_t id, int callback, uint32_t period)
{
	Fired *f = (Fired *)ctx;
	if (f->count < 16) {
		f->owner[f->count] = owner;
		f->id[f->count] = id;
		f->callback[f->count] = callback;
		f->period[f->count] = period;
	}
	f->count++;
}

static void timers_fire_when_strictly_late(KpT *T)
{
	Queue *q = queue(T);
	Fired f;
	int32_t order[KP_UI_TIMERS];
	memset(&f, 0, sizeof f);
	set(T, q, 1, 7, 100, 1, 1000);
	kp_t_eq(T, kp_ui_run_timers(&q->q, 1100, record_fire, &f), 0, "lateness 0 does not fire");
	kp_t_eq(T, kp_ui_run_timers(&q->q, 1101, record_fire, &f), 1, NULL);
	kp_t_eq(T, f.owner[0], 1, NULL);
	kp_t_eq(T, f.id[0], 7, NULL);
	kp_t_eq(T, f.callback[0] != 0, 1, NULL);
	kp_t_eq(T, f.period[0], 100, NULL);
	kp_ui_timer_order(&q->q, order, KP_UI_TIMERS);
	kp_t_eq(T, q->q.nodes[order[0]].last, 1101, "the next period starts at the pass's tick: missed periods are not caught up");
}

static void delete_next(void *ctx, int64_t owner, uint16_t id, int callback, uint32_t period)
{
	Fired *f = (Fired *)ctx;
	record_fire(ctx, owner, id, callback, period);
	if (owner == 2) kp_ui_delete_timer(&f->q->q, 1, 1); /* deletes the next node: the walk stops (0x4c5ae0) */
}

static void timers_replaced_by_own_work(KpT *T)
{
	Queue *q = queue(T);
	Fired f;
	memset(&f, 0, sizeof f);
	f.q = q;
	set(T, q, 1, 1, 10, 0, 0);
	set(T, q, 2, 1, 10, 0, 0);
	kp_ui_run_timers(&q->q, 20, delete_next, &f);
	kp_t_eq(T, f.count, 1, NULL);
	kp_t_eq(T, f.owner[0], 2, NULL);
	kp_t_eq_str(T, owners_of(q), "2/1", NULL);
}

static void stale_keys(void *ctx, int64_t owner, uint16_t id, int callback, uint32_t period)
{
	Fired *f = (Fired *)ctx;
	(void)owner;
	(void)callback;
	(void)period;
	if (id == 2) {
		kp_ui_delete_timer(&f->q->q, 5, 2); /* the active queue is empty now; 5/1 waits in the fired queue */
		set(f->T, f->q, 5, 1, 100, 0, 30);
	}
}

/* SetUiTimer stores its search keys only when the active queue is not empty (0x4c5975): with an empty active queue
 * during a pass, it searches the fired queue with the keys of the search before, here the delete's 5/2, so it misses
 * the fired node of 5/1 and makes a second one. */
static void timers_stale_search_keys(KpT *T)
{
	Queue *q = queue(T);
	Fired f;
	memset(&f, 0, sizeof f);
	f.q = q;
	f.T = T;
	set(T, q, 5, 1, 10, 0, 0);
	set(T, q, 5, 2, 20, 0, 0);
	kp_ui_run_timers(&q->q, 30, stale_keys, &f);
	kp_t_eq_str(T, owners_of(q), "5/1 5/1", NULL);
}

static void timers_delete_owner(KpT *T)
{
	Queue *q = queue(T);
	uint16_t id;
	for (id = 1; id <= 3; id++) set(T, q, 4, id, 100u * id, 0, 0);
	set(T, q, 5, 1, 100, 0, 0);
	kp_ui_delete_owner_timers(&q->q, 4);
	kp_t_eq_str(T, owners_of(q), "5/1", NULL);
}

/* A golden record: ["set", owner, id, period, callback, now], ["del", owner, id], ["delall", owner]. */
static void apply(KpT *T, Queue *q, const KpJson *r)
{
	const char *op = kp_json_str(kp_json_at(r, 0), "");
	int64_t a = kp_json_int(kp_json_at(r, 1), 0), b = kp_json_int(kp_json_at(r, 2), 0);
	if (strcmp(op, "set") == 0) {
		set(T, q, a, (uint16_t)b, (uint32_t)kp_json_int(kp_json_at(r, 3), 0), kp_json_int(kp_json_at(r, 4), 0) == 1,
			(uint32_t)kp_json_int(kp_json_at(r, 5), 0));
	} else if (strcmp(op, "del") == 0) {
		kp_ui_delete_timer(&q->q, a, (uint16_t)b);
	} else if (strcmp(op, "delall") == 0) {
		kp_ui_delete_owner_timers(&q->q, a);
	}
}

typedef struct {
	KpT *T;
	Queue *q;
	const KpJson *r;
	int k, n;
	const char *name;
} Replay;

static void replay_fire(void *ctx, int64_t owner, uint16_t id, int callback, uint32_t period)
{
	Replay *p = (Replay *)ctx;
	const KpJson *want = kp_json_at(kp_json_at(p->r, 2), p->k), *defer = kp_json_at(p->r, 3);
	int i;
	(void)callback;
	(void)period;
	p->k++;
	if (!want || kp_json_int(kp_json_at(want, 0), -1) != owner || kp_json_int(kp_json_at(want, 1), -1) != id) {
		kp_t_fail(p->T, "%s record %d (tick %lld): fire %d is %lld/%d, the original's %lld/%lld", p->name, p->n,
			(long long)kp_json_int(kp_json_at(p->r, 1), 0), p->k, (long long)owner, (int)id,
			(long long)kp_json_int(kp_json_at(want, 0), -1), (long long)kp_json_int(kp_json_at(want, 1), -1));
	}
	for (i = 0; i < kp_json_len(defer); i++) {
		const KpJson *d = kp_json_at(defer, i);
		if (kp_json_int(kp_json_at(d, 0), -1) == p->k - 1) apply(p->T, p->q, kp_json_at(d, 1));
	}
}

static void timers_replay_the_original(KpT *T)
{
	const char *name = "tests/golden/oracle/ui/timers_build_terran.json";
	const KpJson *g = kp_t_golden(T, name), *cases = kp_json_get(g, "cases");
	Queue *q = queue(T);
	int64_t fires = 0;
	int n;
	for (n = 0; n < kp_json_len(cases); n++) {
		const KpJson *r = kp_json_at(cases, n);
		if (strcmp(kp_json_str(kp_json_at(r, 0), ""), "pass") == 0) {
			Replay p;
			int fired;
			p.T = T;
			p.q = q;
			p.r = r;
			p.k = 0;
			p.n = n + 1;
			p.name = name;
			fired = kp_ui_run_timers(&q->q, (uint32_t)kp_json_int(kp_json_at(r, 1), 0), replay_fire, &p);
			if (fired != kp_json_len(kp_json_at(r, 2))) {
				kp_t_fail(T, "%s record %d: %d fired, the original %d", name, n + 1, fired, kp_json_len(kp_json_at(r, 2)));
			}
			fires += fired;
		} else {
			apply(T, q, r);
		}
	}
	kp_t_eq(T, fires, kp_json_field_int(g, "fires", -1), NULL);
}

/* --- the text engine (tests/golden/oracle/ui/text_cases.json, tools/oracle/export_text_cases.py) --- */

#define SENTINEL 0xa5

/* A font of the original data's interface by its name (FNTP's order: the decoded FNT and one NUL). */
static const uint8_t *font(KpT *T, const char *name, size_t *n)
{
	static const char *const NAMES[KP_UI_FNT_FONTS] = { "font8", "font10", "font14", "font16", "font16x" };
	const KpUiData *d = &kp_t_data(T)->ui;
	int k;
	for (k = 0; k < KP_UI_FNT_FONTS; k++) {
		if (strcmp(NAMES[k], name) == 0) {
			*n = d->fonts[k].size;
			return d->fonts[k].data;
		}
	}
	kp_t_fail(T, "no font %s", name);
}

/* A TBL of the original data's interface by its file's name (STAT, NETW, GLUA, GHST, TIPS, HELP), in UTF-8. */
static const uint8_t *table(KpT *T, const char *name, size_t *n)
{
	static const char *const NAMES[KP_UI_TBLS] = { "STAT", "NETW", "GLUA", "GHST", "TIPS", "HELP" };
	const KpUiData *d = &kp_t_data(T)->ui;
	int k;
	for (k = 0; k < KP_UI_TBLS; k++) {
		if (strcmp(NAMES[k], name) == 0) {
			*n = d->tbl[k].size;
			return d->tbl[k].data;
		}
	}
	kp_t_fail(T, "no table %s", name);
}

/* Hex digits into bytes (a NUL after them); the count. */
static size_t unhex(const char *h, uint8_t *out, size_t max)
{
	size_t n = 0;
	unsigned v;
	while (h[0] && h[1] && n + 1 < max && sscanf(h, "%2x", &v) == 1) {
		out[n++] = (uint8_t)v;
		h += 2;
	}
	out[n] = 0;
	return n;
}

/* The probe's context (export_text_cases.py): pen (5, 4), clip 2..120 x ..1000, the bottom extent at the pen. */
static void setup(KpT *T, KpUiText *t, const uint8_t *f, size_t n)
{
	kp_ui_text_init(t);
	kp_t_ok(T, kp_ui_text_select_font(t, f, n) == 0, "text: not an FNT image");
	t->x = 5;
	t->y = 4;
	t->clip_left = 2;
	t->clip_right = 120;
	t->clip_bottom = 1000;
	t->ext_bottom = 4;
	t->measuring = 1;
	t->wrap = 1;
	t->row = 0;
	t->previous_row = 1;
	t->spacing = 1;
}

/* String id of a TBL section ("" past its count). */
static const uint8_t *tbl_string(const uint8_t *blob, size_t size, int64_t id)
{
	uint32_t offset;
	id %= 65536;
	if (id == 0 || size < 2) return NULL;
	if (id - 1 >= kp_le_u16(blob)) return (const uint8_t *)"";
	offset = kp_le_u16(blob + 2 * id);
	return offset < size ? blob + offset : (const uint8_t *)"";
}

static void check_value(KpT *T, int n, const char *kind, const KpJson *c, const char *what, int64_t got, int64_t want)
{
	if (got != want) {
		KpBuf b;
		kp_buf_init(&b);
		kp_json_write(&b, kp_json_at(c, 1));
		kp_t_defer(T, free, b.data);
		kp_t_fail(T, "text case %d (%s %s): %s %lld, the original's %lld", n, kind, kp_buf_text(&b), what, (long long)got, (long long)want);
	}
}

static void text_matches_the_original_probes(KpT *T)
{
	const KpJson *g, *cases;
	size_t tfgm_n, tfnt_n;
	const uint8_t *tfgm, *tfnt; /* the probes' TFNT bank: the title's rows (export_text_cases.py) */
	int n, packets = 0, tbls = 0;
	uint8_t *buf;
	if (!kp_t_ready(T, "test_kp_ui.text_matches_the_original_probes", UI_FILES)) return;
	g = kp_t_golden(T, "tests/golden/oracle/ui/text_cases.json");
	tfgm = kp_t_data_117(T)->ui.font_rows.data; /* the probes ran over 1.17's texts and fonts */
	tfgm_n = kp_t_data(T)->ui.font_rows.size;
	tfnt = kp_t_data(T)->ui.title_font_rows.data;
	tfnt_n = kp_t_data(T)->ui.title_font_rows.size;
	cases = kp_json_get(g, "cases");
	buf = (uint8_t *)malloc(1 << 16);
	kp_t_ok(T, buf != NULL, "out of memory");
	kp_t_defer(T, free, buf);
	for (n = 0; n < kp_json_len(cases); n++) {
		const KpJson *c = kp_json_at(cases, n);
		const char *kind = kp_json_str(kp_json_at(c, 0), "");
#define C(k) kp_json_int(kp_json_at(c, k), 0)
#define S(k) kp_json_str(kp_json_at(c, k), "")
#define CHECK(what, got, want) check_value(T, n + 1, kind, c, what, (int64_t)(got), (int64_t)(want))
		KpUiText t;
		size_t fn;
		const uint8_t *f;
		if (strcmp(kind, "glyph") == 0) {
			f = font(T, S(1), &fn);
			setup(T, &t, f, fn);
			CHECK("offset", kp_ui_glyph(&t, (int)C(2)), C(3));
		} else if (strcmp(kind, "width") == 0) {
			f = font(T, S(1), &fn);
			setup(T, &t, f, fn);
			t.spacing = (uint8_t)C(3);
			CHECK("width", kp_ui_char_width(&t, (int)C(2)), C(4));
		} else if (strcmp(kind, "span") == 0) {
			f = font(T, S(1), &fn);
			setup(T, &t, f, fn);
			buf[0] = (uint8_t)C(2);
			buf[1] = 'A';
			buf[2] = 0;
			CHECK("consumed", kp_ui_text_span(&t, buf, 0, 3), C(5));
			CHECK("x", t.x, C(3));
			CHECK("bottom", t.ext_bottom, C(4));
		} else if (strcmp(kind, "wrap") == 0) {
			f = font(T, S(1), &fn);
			setup(T, &t, f, fn);
			t.clip_right = (int16_t)C(3);
			unhex(S(2), buf, 1 << 16);
			CHECK("consumed", kp_ui_find_line_break(&t, buf, 0), C(4));
			CHECK("x", t.x, C(5));
		} else if (strcmp(kind, "tbl") == 0) {
			size_t sn;
			const uint8_t *sec = table(T, S(1), &sn), *text = tbl_string(sec, sn, C(2));
			f = font(T, "font10", &fn);
			setup(T, &t, f, fn);
			CHECK("consumed", kp_ui_find_line_break(&t, text ? text : (const uint8_t *)"", 0), C(3));
			CHECK("x", t.x, C(4));
			tbls++;
		} else if (strcmp(kind, "tab") == 0) {
			f = font(T, S(1), &fn);
			setup(T, &t, f, fn);
			t.x = (int32_t)C(2);
			t.clip_left = (int16_t)C(3);
			t.clip_right = 32767;
			t.wrap = (int)C(4);
			CHECK("consumed", kp_ui_find_line_break(&t, (const uint8_t *)"A\tB\nREST", 0), C(5));
			CHECK("x", t.x, C(6));
		} else if (strcmp(kind, "lines") == 0) {
			int32_t count = 0;
			size_t consumed;
			f = font(T, S(1), &fn);
			setup(T, &t, f, fn);
			t.flags = (uint8_t)C(2);
			unhex(S(3), buf, 1 << 16);
			consumed = kp_ui_layout_lines(&t, buf, 0, &count);
			CHECK("consumed", consumed, C(4));
			CHECK("lines", count, C(5));
			CHECK("x", t.x, C(6));
			CHECK("y", t.y, C(7));
			CHECK("bounds left", t.ext_left, C(8));
			CHECK("bounds top", t.ext_top, C(9));
			CHECK("bounds right", t.ext_right, C(10));
			CHECK("bounds bottom", t.ext_bottom, C(11));
		} else if (strcmp(kind, "highlight") == 0) {
			size_t len;
			f = font(T, S(1), &fn);
			setup(T, &t, f, fn);
			t.highlight = 65;
			len = unhex(S(2), buf, 1 << 16);
			kp_ui_text_span(&t, buf, 0, len);
			CHECK("x", t.x, C(3));
			CHECK("bottom", t.ext_bottom, C(4));
		} else if (strcmp(kind, "valign") == 0) {
			int32_t y = (int32_t)C(2);
			f = font(T, "font8", &fn);
			setup(T, &t, f, fn);
			t.flags = (uint8_t)C(1);
			t.clip_bottom = 31;
			t.y = (int32_t)C(3);
			kp_ui_align_vertically(&t, &y);
			CHECK("y", y, C(4));
		} else if (strcmp(kind, "colour") == 0) {
			f = font(T, "font8", &fn);
			setup(T, &t, f, fn);
			kp_ui_select_color(&t, (int)C(1));
			CHECK("row", t.row, C(2));
			CHECK("previous", t.previous_row, C(3));
		} else if (strcmp(kind, "grey") == 0) {
			f = font(T, "font8", &fn);
			setup(T, &t, f, fn);
			t.measuring = 0;
			t.row = (int)C(2);
			buf[0] = (uint8_t)C(1);
			buf[1] = 0;
			kp_ui_text_span(&t, buf, 0, 2);
			CHECK("row", t.row, C(3));
			CHECK("previous", t.previous_row, C(4));
		} else if (strcmp(kind, "packets") == 0 || strcmp(kind, "zero") == 0) {
			int w, h, pad;
			const uint8_t *data, *bank;
			size_t data_n, bank_n;
			KpUiSurface s;
			uint8_t *pixels;
			if (strcmp(kind, "packets") == 0) {
				const uint8_t *ff = font(T, S(1), &fn);
				int64_t i = 8 + 4 * (C(2) - ff[4]);
				size_t off = (size_t)ff[i] + 256 * (size_t)ff[i + 1] + 65536 * (size_t)ff[i + 2];
				w = ff[off];
				h = ff[off + 1];
				pad = (int)C(4);
				data = ff + off + 4;
				data_n = fn - off - 4;
				bank = strcmp(S(3), "TFNT") == 0 ? tfnt : tfgm;
				bank_n = strcmp(S(3), "TFNT") == 0 ? tfnt_n : tfgm_n;
				packets++;
			} else {
				size_t len;
				w = (int)C(1);
				h = (int)C(2);
				pad = (int)C(3);
				len = unhex(S(4), buf, (1 << 16) - 96);
				memset(buf + len, 1, 96);
				data = buf;
				data_n = len + 96;
				bank = tfgm;
				bank_n = tfgm_n;
			}
			f = font(T, "font8", &fn);
			setup(T, &t, f, fn);
			kp_ui_text_set_rows(&t, bank, bank_n);
			s.w = w + pad;
			s.h = h;
			pixels = (uint8_t *)malloc((size_t)s.w * (size_t)s.h + 1);
			kp_t_ok(T, pixels != NULL, "out of memory");
			memset(pixels, SENTINEL, (size_t)s.w * (size_t)s.h);
			s.pixels = pixels;
			CHECK("consumed", kp_ui_plot_glyph(&t, &s, 0, 0, data, data_n, w, h), C(6));
			CHECK("crc", kp_crc32(pixels, (size_t)s.w * (size_t)s.h), C(5));
			free(pixels);
		}
#undef C
#undef S
#undef CHECK
	}
	kp_t_eq(T, packets, 4460, NULL);
	kp_t_eq(T, tbls, 1749, NULL);
}

/* --- the vector text's glyph log and the canvas (ADR 0011, 2026-10-26) --- */

#define VT_W 160
#define VT_H 24
#define VT_BACK 0x55

/* "Hello, world" in font10 at (4, 4) of a VT_W x VT_H surface, through a text state with the log (NULL: off). */
static void vt_draw(KpUi *ui, KpUiSurface *s, KpUiGlyphLog *log)
{
	KpUiText t;
	kp_ui_text_init(&t);
	kp_ui_text_select_font(&t, ui->font_files[1], ui->font_file_sizes[1]);
	kp_ui_text_set_rows(&t, ui->game_rows, ui->game_rows_size);
	t.target = s;
	t.log = log;
	kp_ui_draw_text(&t, 4, 4, (const uint8_t *)"Hello, world");
}

static KpUiSurface vt_sized(KpT *T, int32_t w, int32_t h, uint8_t back)
{
	KpUiSurface s;
	s.w = w;
	s.h = h;
	s.pixels = (uint8_t *)malloc((size_t)w * (size_t)h);
	kp_t_ok(T, s.pixels != NULL, "out of memory");
	kp_t_defer(T, free, s.pixels);
	memset(s.pixels, back, (size_t)w * (size_t)h);
	return s;
}

static KpUiSurface vt_surface(KpT *T, uint8_t back) { return vt_sized(T, VT_W, VT_H, back); }

static void vt_canvas_free(void *p)
{
	kp_ui_canvas_free((KpUiCanvas *)p);
	free(p);
}

/* A canvas of w x h freed when the test ends. */
static KpUiCanvas *vt_canvas(KpT *T, int32_t w, int32_t h)
{
	KpUiCanvas *c = (KpUiCanvas *)calloc(1, sizeof *c);
	kp_t_ok(T, c != NULL, "out of memory");
	kp_t_defer(T, vt_canvas_free, c);
	kp_t_ok(T, kp_ui_canvas_resize(c, w, h) == 0, "out of memory");
	return c;
}

/* The HD interface's item plane: each pixel takes the item of the draw that wrote it, a frame's items number from 1,
 * a change of items alone marks the canvas changed, and the plane follows a resize. */
static void canvas_hd_items_follow_the_draws(KpT *T)
{
	KpUiCanvas *c = vt_canvas(T, 8, 4);
	KpUiCanvasItem it;
	uint16_t a, b;
	memset(&it, 0, sizeof it);
	kp_t_eq(T, kp_ui_canvas_add_item(c, &it), 0, "no items without the plane");
	kp_t_eq(T, kp_ui_canvas_hd(c, 1), 0, "plane on");
	kp_ui_canvas_begin(NULL, c);
	it.frame = 7;
	a = kp_ui_canvas_add_item(c, &it);
	it.frame = 9;
	b = kp_ui_canvas_add_item(c, &it);
	kp_t_ok(T, a == 1 && b == 2 && c->items[2].frame == 9, "numbered from 1");
	kp_ui_canvas_item(c, a);
	kp_ui_canvas_fill(c, 0, 0, 4, 4, 5);
	kp_ui_canvas_item(c, b);
	kp_ui_canvas_fill(c, 2, 0, 4, 2, 6);
	kp_ui_canvas_item(c, 0);
	kp_ui_canvas_fill(c, 6, 3, 1, 1, 7);
	kp_ui_canvas_end(c);
	kp_t_ok(T, c->ids[0] == 1 && c->ids[2] == 2 && c->ids[2 * 8 + 2] == 1 && c->ids[3 * 8 + 6] == 0, "the last draw's item");
	kp_ui_canvas_begin(NULL, c);
	kp_t_eq(T, c->item_count, 0, "a new frame's items");
	kp_ui_canvas_item(c, kp_ui_canvas_add_item(c, &it));
	kp_ui_canvas_fill(c, 0, 0, 4, 4, 5); /* the same pixels, another item */
	kp_ui_canvas_item(c, 0);
	kp_ui_canvas_fill(c, 2, 0, 4, 2, 6);
	kp_ui_canvas_fill(c, 6, 3, 1, 1, 7);
	kp_t_eq(T, kp_ui_canvas_end(c), 1, "changed by the items alone");
	kp_t_ok(T, c->ids[2] == 0 && c->ids[0] == 1, "the plane updated");
	kp_t_eq(T, kp_ui_canvas_resize(c, 16, 2), 0, "resized");
	kp_t_ok(T, c->ids != NULL && c->ids_next != NULL, "the plane kept");
	kp_t_eq(T, kp_ui_canvas_hd(c, 0), 0, "plane off");
	kp_t_ok(T, c->ids == NULL, "freed");
}

static void owners_free(void *p)
{
	KpUi *ui = (KpUi *)p;
	kp_ui_image_log_free(&ui->image_log);
	free(ui);
}

/* An image of the log written by hand: its rectangle, every pixel written with value. */
static void owners_image(KpT *T, KpUi *ui, const KpUiSurface *s, int32_t frame, const int32_t r[4], uint8_t value,
	int tinted)
{
	KpUiImageLog *log = &ui->image_log;
	KpUiImage *im;
	size_t n = (size_t)r[2] * (size_t)r[3], k;
	if (log->count == log->capacity) {
		log->capacity = log->capacity ? 2 * log->capacity : 4;
		log->images = (KpUiImage *)realloc(log->images, sizeof *log->images * (size_t)log->capacity);
		kp_t_ok(T, log->images != NULL, "out of memory");
	}
	im = &log->images[log->count++];
	memset(im, 0, sizeof *im);
	im->surface = s->pixels;
	im->surface_w = s->w;
	im->surface_h = s->h;
	im->path = "dlgs\\test.grp";
	im->path_len = strlen(im->path);
	im->frame = frame;
	im->x = im->item[0] = r[0];
	im->y = im->item[1] = r[1];
	im->w = im->item[2] = r[2];
	im->h = im->item[3] = r[3];
	im->values = (uint8_t *)malloc(n);
	im->mask = (uint8_t *)malloc(n);
	im->below = (uint16_t *)malloc(n * sizeof *im->below);
	kp_t_ok(T, im->values != NULL && im->mask != NULL && im->below != NULL, "out of memory");
	memset(im->values, value, n);
	memset(im->mask, 1, n);
	for (k = 0; k < n; k++) im->below[k] = KP_UI_BELOW_WRITTEN; /* nothing known under it */
	im->tinted = tinted;
	im->serial = ++log->serial;
}

/* A freed surface's images leave the log: the next surface made may get the same pointer, and a closed dialog's
 * buttons came back as frames on the dialog opened next. */
static void image_forget_drops_a_freed_surface(KpT *T)
{
	static const int32_t BUTTON[4] = { 1, 1, 6, 3 }, FRAME[4] = { 0, 0, 8, 8 };
	KpUi *ui = (KpUi *)calloc(1, sizeof *ui);
	KpUiSurface dialog = vt_sized(T, 8, 8, 0), other = vt_sized(T, 8, 8, 0);
	KpUiFile file;
	kp_t_ok(T, ui != NULL, "out of memory");
	kp_t_defer(T, owners_free, ui);
	ui->hd = 1;
	owners_image(T, ui, &dialog, 0, FRAME, 3, 0);
	owners_image(T, ui, &other, 0, FRAME, 3, 0);
	owners_image(T, ui, &dialog, 1, BUTTON, 5, 0);
	memset(&file, 0, sizeof file);
	kp_ui_picture(ui, dialog.pixels, 8, 8, &file);
	kp_ui_picture(ui, other.pixels, 8, 8, &file);
	kp_ui_image_forget(ui, dialog.pixels);
	kp_t_eq(T, ui->image_log.count, 1, "the dialog's images went");
	kp_t_ok(T, ui->image_log.images[0].surface == other.pixels, "the other surface's image stays");
	kp_t_eq(T, ui->image_log.picture_count, 1, "the dialog's picture went");
	kp_t_ok(T, ui->image_log.pictures[0].pixels == other.pixels, "the other picture stays");
	kp_ui_image_forget(ui, NULL);
	kp_t_eq(T, ui->image_log.count, 1, "no surface, no change");
}

/* A surface freed while an image is drawn (its drawer holds the image's pointer): the surface's images are emptied in
 * place, the open one keeps its pixels, and all of them leave the log when it ends. */
static void image_forget_while_an_image_is_open(KpT *T)
{
	static const int32_t BUTTON[4] = { 1, 1, 6, 3 }, FRAME[4] = { 0, 0, 8, 8 };
	KpUi *ui = (KpUi *)calloc(1, sizeof *ui);
	KpUiSurface dialog = vt_sized(T, 8, 8, 0), other = vt_sized(T, 8, 8, 0);
	KpUiImage *open;
	kp_t_ok(T, ui != NULL, "out of memory");
	kp_t_defer(T, owners_free, ui);
	ui->hd = 1;
	owners_image(T, ui, &dialog, 0, FRAME, 3, 0);
	owners_image(T, ui, &other, 0, FRAME, 3, 0);
	owners_image(T, ui, &dialog, 1, BUTTON, 5, 0);
	ui->image_log.images[2].written = 1;
	open = ui->image_log.open = &ui->image_log.images[2];
	kp_ui_image_forget(ui, dialog.pixels);
	kp_t_eq(T, ui->image_log.count, 3, "the open image keeps its place");
	kp_t_ok(T, open == &ui->image_log.images[2] && open->values != NULL, "and its pixels");
	kp_t_ok(T, kp_ui_image_last(ui, dialog.pixels) == NULL, "no image of the freed surface matches");
	kp_t_ok(T, ui->image_log.images[0].surface == NULL && ui->image_log.images[0].mask == NULL, "emptied");
	kp_ui_image_end(ui, open, 0);
	kp_t_eq(T, ui->image_log.count, 1, "the freed surface's images went");
	kp_t_ok(T, ui->image_log.images[0].surface == other.pixels, "the other surface's image stays");
	kp_t_eq(T, ui->image_log.forgotten, 0, NULL);
}

/* The owners of a composed surface: a smaller image lies over a larger one whose rectangle holds it when that one was
 * drawn first or the pixel is still the smaller one's (a wireframe under its panel recorded again with the wireframe's
 * pixels), with the larger one under it; a smaller image the larger one drew over later keeps only its own pixels (a
 * frame's piece under a portrait); a tinted image takes the plain background of its rectangle; a pixel no image wrote
 * has no item. */
static void image_owners_layer_by_area(KpT *T)
{
	enum { W = 16, H = 8 };
	static const int32_t PANEL[4] = { 0, 0, 12, 8 }, WIRE[4] = { 2, 2, 4, 4 }, ICON[4] = { 12, 2, 3, 3 },
		EDGE[4] = { 7, 5, 4, 1 };
	KpUi *ui = (KpUi *)calloc(1, sizeof *ui);
	KpUiCanvas *c = vt_canvas(T, W, H);
	KpUiSurface s = vt_sized(T, W, H, 0);
	const uint16_t *owners;
	KpUiImage *panel;
	int32_t x, y;
	kp_t_ok(T, ui != NULL, "out of memory");
	kp_t_defer(T, owners_free, ui);
	ui->hd = 1;
	kp_t_eq(T, kp_ui_canvas_hd(c, 1), 0, "plane on");
	kp_ui_canvas_begin(NULL, c);
	owners_image(T, ui, &s, 3, EDGE, 4, 0);  /* a frame's edge, drawn over by the panel but one pixel */
	owners_image(T, ui, &s, 1, WIRE, 5, 0);  /* the wireframe ... */
	owners_image(T, ui, &s, 0, PANEL, 9, 0); /* ... the panel recorded again over it with the wireframe's pixels */
	panel = &ui->image_log.images[ui->image_log.count - 1];
	for (y = WIRE[1]; y < WIRE[1] + WIRE[3]; y++) memset(panel->values + (size_t)y * PANEL[2] + WIRE[0], 5, WIRE[2]);
	owners_image(T, ui, &s, 2, ICON, 7, 1);  /* a tinted icon over a background drawn without the log */
	for (y = 0; y < PANEL[3]; y++) memset(s.pixels + (size_t)y * W, 9, PANEL[2]);
	for (y = WIRE[1]; y < WIRE[1] + WIRE[3]; y++) memset(s.pixels + (size_t)y * W + WIRE[0], 5, WIRE[2]);
	s.pixels[3 * W + 13] = 7; /* one pixel still the icon's, one the edge's */
	s.pixels[5 * W + 8] = 4;
	owners = kp_ui_image_owners(ui, c, &s, s.pixels, NULL, 0, 0);
	kp_t_ok(T, owners != NULL, "owners");
	if (owners == NULL) return;
	kp_t_eq(T, c->item_count, 4, "an item each");
	for (y = 0; y < H; y++) {
		for (x = 0; x < W; x++) {
			uint16_t id = owners[y * W + x];
			int in_wire = x >= 2 && x < 6 && y >= 2 && y < 6, in_icon = x >= 12 && x < 15 && y >= 2 && y < 5;
			int frame = y == 5 && x == 8 ? 3 : in_wire ? 1 : in_icon ? 2 : x < 12 ? 0 : -1;
			kp_t_ok(T, frame < 0 ? id == 0 : id > 0 && c->items[id].frame == frame, "(%d, %d)'s item", (int)x,
				(int)y);
		}
	}
	{
		const KpUiCanvasItem *wire = &c->items[owners[2 * W + 2]], *icon = &c->items[owners[2 * W + 12]];
		kp_t_ok(T, wire->under > 0 && c->items[wire->under].frame == 0, "the panel under the wireframe");
		kp_t_eq(T, icon->under, 0, "nothing under the icon");
		kp_t_ok(T, c->items[c->items[owners[5 * W + 8]].under].frame == 0, "the edge over the panel");
	}
}

/* The pieces of one picture (a panel carried again by partial redraws, each piece with its clip's pixels) count as one
 * image under a smaller one: a wireframe drawn after them takes the pixels of both pieces in its rectangle, and a
 * tinted frame those of its whole GRP's rectangle past the SD frame's. */
static void image_owners_pieces_and_whole_grp(KpT *T)
{
	enum { W = 16, H = 8 };
	static const int32_t PANEL[4] = { 0, 0, W, H }, WIRE[4] = { 6, 2, 4, 4 };
	KpUi *ui = (KpUi *)calloc(1, sizeof *ui);
	KpUiCanvas *c = vt_canvas(T, W, H);
	KpUiSurface s = vt_sized(T, W, H, 9);
	const uint16_t *owners;
	KpUiImage *im;
	int32_t x, y, k;
	kp_t_ok(T, ui != NULL, "out of memory");
	kp_t_defer(T, owners_free, ui);
	ui->hd = 1;
	kp_t_eq(T, kp_ui_canvas_hd(c, 1), 0, "plane on");
	kp_ui_canvas_begin(NULL, c);
	for (k = 0; k < 2; k++) { /* the left and the right half */
		owners_image(T, ui, &s, 0, PANEL, 9, 0);
		im = &ui->image_log.images[ui->image_log.count - 1];
		for (y = 0; y < H; y++) memset(im->mask + (size_t)y * W + (k ? 0 : W / 2), 0, W / 2);
	}
	owners_image(T, ui, &s, 1, WIRE, 5, 1); /* a tinted frame whose GRP is two pixels wider each side */
	im = &ui->image_log.images[ui->image_log.count - 1];
	for (y = 0; y < WIRE[3]; y++) memset(im->mask + (size_t)y * WIRE[2] + 1, 0, 2); /* its middle columns not drawn */
	im->canvas[0] = WIRE[0] - 2;
	im->canvas[1] = WIRE[1];
	im->canvas[2] = WIRE[2] + 4;
	im->canvas[3] = WIRE[3];
	for (y = WIRE[1]; y < WIRE[1] + WIRE[3]; y++) {
		s.pixels[y * W + WIRE[0]] = 5;
		s.pixels[y * W + WIRE[0] + 3] = 5;
	}
	owners = kp_ui_image_owners(ui, c, &s, s.pixels, NULL, 0, 0);
	kp_t_ok(T, owners != NULL, "owners");
	if (owners == NULL) return;
	for (y = 0; y < H; y++) {
		for (x = 0; x < W; x++) {
			int in_grp = x >= WIRE[0] - 2 && x < WIRE[0] + WIRE[2] + 2 && y >= WIRE[1] && y < WIRE[1] + WIRE[3];
			uint16_t id = owners[y * W + x];
			kp_t_ok(T, id > 0 && c->items[id].frame == (in_grp ? 1 : 0), "(%d, %d)'s item", (int)x, (int)y);
		}
	}
	kp_t_ok(T, c->items[c->items[owners[2 * W + 7]].under].frame == 0, "the panel under the wireframe");
}

/* A dialog's background tiled from frames (kp_ui_tile_frame): the tiles drawn after the serial become one tiled image
 * per frame, a frame drawn before it stays apart, and the composed surface has an item per frame, repeating. */
static void image_tiles_make_one_image_per_frame(KpT *T)
{
	enum { W = 32, H = 16, TILE = 8 };
	static const int FRAMES[2][4] = { { 0, 1, 1, 2 }, { 3, 4, 4, 5 } };
	KpUi *ui = (KpUi *)calloc(1, sizeof *ui);
	KpUiCanvas *c = vt_canvas(T, W, H);
	KpUiSurface s = vt_sized(T, W, H, 0);
	const uint16_t *owners;
	const KpUiImage *im;
	const KpUiCanvasItem *it;
	uint32_t after;
	int32_t x, y, k, r[4];
	kp_t_ok(T, ui != NULL, "out of memory");
	kp_t_defer(T, owners_free, ui);
	ui->hd = 1;
	r[0] = 8;
	r[1] = 0;
	r[2] = r[3] = TILE;
	owners_image(T, ui, &s, 1, r, 99, 0); /* frame 1 before the tiling (its pixels drawn over) */
	after = ui->image_log.serial;
	for (y = 0; y < 2; y++) {
		for (x = 0; x < 4; x++) {
			r[0] = x * TILE;
			r[1] = y * TILE;
			owners_image(T, ui, &s, FRAMES[y][x], r, (uint8_t)(10 + FRAMES[y][x]), 0);
			for (k = 0; k < TILE; k++) memset(s.pixels + (size_t)(y * TILE + k) * W + (size_t)x * TILE, 10 + FRAMES[y][x], TILE);
		}
	}
	kp_ui_image_tiles(ui, &s, after, TILE, TILE);
	kp_t_eq(T, ui->image_log.count, 7, "the earlier frame and one image per tiled frame");
	im = &ui->image_log.images[2];
	kp_t_ok(T, im->frame == 1 && im->tiled && im->x == 8 && im->w == 16 && im->h == TILE && im->item[0] == 8
		&& im->item[2] == TILE, "frame 1's tiles: one image over both, its item the first tile's");
	kp_t_ok(T, !ui->image_log.images[0].tiled && ui->image_log.images[0].w == TILE, "the earlier frame apart");
	kp_t_eq(T, kp_ui_canvas_hd(c, 1), 0, "plane on");
	kp_ui_canvas_begin(NULL, c);
	owners = kp_ui_image_owners(ui, c, &s, s.pixels, NULL, 0, 0);
	kp_t_ok(T, owners != NULL, "owners");
	if (owners == NULL) return;
	kp_t_eq(T, c->item_count, 6, "an item per frame");
	it = &c->items[owners[12 * W + 20]];
	kp_t_ok(T, it->frame == 4 && it->tiled && it->x == 8 && it->w == TILE, "the second frame 4 tile: the repeating item");
	kp_t_eq(T, owners[12 * W + 12], owners[12 * W + 20], "both frame 4 tiles one item");
}

/* Plain frames on one grid (a menu button's middle drawn every 8 pixels) compose into one repeating item; one off the
 * grid and a tinted one keep their own. */
static void image_owners_merge_a_grid(KpT *T)
{
	enum { W = 40, H = 8 };
	static const int32_t R[4][4] = { { 0, 0, 8, 4 }, { 8, 0, 8, 4 }, { 4, 4, 8, 4 }, { 24, 4, 8, 4 } };
	KpUi *ui = (KpUi *)calloc(1, sizeof *ui);
	KpUiCanvas *c = vt_canvas(T, W, H);
	KpUiSurface s = vt_sized(T, W, H, 3);
	const uint16_t *owners;
	int32_t k;
	kp_t_ok(T, ui != NULL, "out of memory");
	kp_t_defer(T, owners_free, ui);
	ui->hd = 1;
	kp_t_eq(T, kp_ui_canvas_hd(c, 1), 0, "plane on");
	kp_ui_canvas_begin(NULL, c);
	for (k = 0; k < 4; k++) owners_image(T, ui, &s, 5, R[k], 3, k == 3);
	owners = kp_ui_image_owners(ui, c, &s, s.pixels, NULL, 0, 0);
	kp_t_ok(T, owners != NULL, "owners");
	if (owners == NULL) return;
	kp_t_eq(T, owners[0], owners[12], "the two on the grid: one item");
	kp_t_ok(T, c->items[owners[0]].tiled && c->items[owners[0]].x == 0, "repeating from the first");
	kp_t_ok(T, owners[4 * W + 4] != owners[0] && !c->items[owners[4 * W + 4]].tiled, "off the grid: its own");
	kp_t_ok(T, owners[4 * W + 24] != owners[0] && c->items[owners[4 * W + 24]].tinted, "tinted: its own");
}

/* Under an HD console piece (its item set) a pixel of the game view's layer drawn first (the placement) stays, with
 * the piece's item; without the piece the console's pixel covers it. */
static void console_keeps_the_view_layer_under_hd(KpT *T)
{
	enum { W = 4, H = 2 };
	KpUi *ui = (KpUi *)calloc(1, sizeof *ui);
	KpUiCanvas *c = vt_canvas(T, W, H);
	KpUiSurface layer = vt_sized(T, W, H, 0);
	KpUiCanvasItem it;
	uint16_t piece;
	int hd;
	kp_t_ok(T, ui != NULL, "out of memory");
	kp_t_defer(T, free, ui);
	ui->console = vt_sized(T, W, H, 9);
	layer.pixels[1] = 4;
	memset(&it, 0, sizeof it);
	kp_t_eq(T, kp_ui_canvas_hd(c, 1), 0, "plane on");
	for (hd = 0; hd < 2; hd++) {
		kp_ui_canvas_begin(NULL, c);
		kp_ui_canvas_layer(ui, c, &layer, 0, 0);
		piece = hd ? kp_ui_canvas_add_item(c, &it) : 0;
		kp_ui_canvas_item(c, piece);
		kp_ui_canvas_console(ui, c, 0, 0, 0, W);
		kp_ui_canvas_item(c, 0);
		kp_ui_canvas_end(c);
		kp_t_ok(T, c->pixels[2 * 1] == (hd ? 4 : 9) && c->pixels[2 * 1 + 1] == (hd ? KP_UI_CANVAS_OVERLAY_A : 255),
			"the layer's pixel %s", hd ? "kept" : "covered");
		kp_t_ok(T, c->ids[1] == piece && c->pixels[2 * 2] == 9 && c->ids[2] == piece, "the piece's item over both");
	}
}

/* An item drawn over another item's pixels (a dialog's button over the console) takes it as the item under it when
 * its own surface gave it none, and keeps the one its surface gave. */
static void items_over_items_lie_over_them(KpT *T)
{
	KpUiCanvas *c = vt_canvas(T, 8, 2);
	KpUiCanvasItem it;
	uint16_t console, button, icon;
	memset(&it, 0, sizeof it);
	kp_t_eq(T, kp_ui_canvas_hd(c, 1), 0, "plane on");
	kp_ui_canvas_begin(NULL, c);
	console = kp_ui_canvas_add_item(c, &it);
	button = kp_ui_canvas_add_item(c, &it);
	it.under = button;
	icon = kp_ui_canvas_add_item(c, &it);
	kp_ui_canvas_item(c, console);
	kp_ui_canvas_fill(c, 0, 0, 8, 2, 5);
	kp_ui_canvas_item(c, button);
	kp_ui_canvas_fill(c, 2, 0, 4, 2, 6);
	kp_ui_canvas_item(c, icon);
	kp_ui_canvas_fill(c, 3, 0, 2, 2, 7);
	kp_ui_canvas_item(c, 0);
	kp_ui_canvas_fill(c, 7, 0, 1, 1, 8); /* no item: nothing to link */
	kp_ui_canvas_end(c);
	kp_t_eq(T, c->items[button].under, console, "the button over the console");
	kp_t_eq(T, c->items[icon].under, button, "the icon keeps its surface's");
	kp_t_eq(T, c->items[console].under, 0, "the console over nothing");
}

/* A blit carrying its source's images with the log full: the source, the oldest image, is dropped for the new one,
 * and the carry stops there (the source is not read past the log: ASan) with the log under its limit. */
static void image_blit_with_a_full_log(KpT *T)
{
	static const int32_t R[4] = { 0, 0, 2, 2 };
	KpUi *ui = (KpUi *)calloc(1, sizeof *ui);
	KpUiSurface src = vt_sized(T, 4, 4, 3), dst = vt_sized(T, 4, 4, 0), other = vt_sized(T, 4, 4, 0);
	int32_t rect[4] = { 0, 0, 2, 2 }, k;
	kp_t_ok(T, ui != NULL, "out of memory");
	kp_t_defer(T, owners_free, ui);
	ui->hd = 1;
	owners_image(T, ui, &src, 0, R, 3, 0); /* the source's image, the oldest */
	for (k = 1; k < KP_UI_IMAGES_MAX; k++) owners_image(T, ui, &other, k, R, 1, 0);
	kp_t_eq(T, ui->image_log.count, KP_UI_IMAGES_MAX, "the log full");
	kp_ui_image_blit(ui, &src, &dst, 0, 0, rect, 0);
	kp_t_eq(T, ui->image_log.count, KP_UI_IMAGES_MAX - 1, "the source dropped, nothing carried");
	kp_t_ok(T, kp_ui_image_last(ui, dst.pixels) == NULL && kp_ui_image_last(ui, src.pixels) == NULL, "no image of either");
}

/* The latest image of a path on surface s: its index in the log, -1 none. */
static int32_t image_of(const KpUi *ui, const KpUiSurface *s, const char *path)
{
	int32_t k;
	for (k = ui->image_log.count - 1; k >= 0; k--) {
		const KpUiImage *im = &ui->image_log.images[k];
		if (im->surface == s->pixels && im->path_len == strlen(path) && memcmp(im->path, path, im->path_len) == 0) {
			return k;
		}
	}
	return -1;
}

/* What the owners of s (composed now) give as lying under the picture's pixels: the background's values (known), or
 * nothing known; the picture's keyed pixel, which it does not write, is the background's (nothing known under it),
 * which the picture's item may take lying over it (KP_UI_BELOW_UNDER). */
static void check_below(KpT *T, KpUi *ui, KpUiCanvas *c, const KpUiSurface *s, const uint8_t *back, int known,
	const char *what)
{
	enum { W = 8, H = 4, PX = 2, PY = 1, PW = 4, PH = 2 };
	int32_t x, y;
	kp_ui_canvas_begin(NULL, c);
	kp_t_ok(T, kp_ui_image_owners(ui, c, s, s->pixels, NULL, 0, 0) != NULL, "%s: owners", what);
	for (y = 0; y < H; y++) {
		for (x = 0; x < W; x++) {
			int pic = x >= PX && y >= PY && x < PX + PW && y < PY + PH, keyed = x == PX && y == PY;
			uint16_t want = pic && known && !keyed ? (uint16_t)(KP_UI_BELOW_KNOWN | back[y * W + x]) : 0, got;
			got = ui->image_log.below[y * W + x];
			if (keyed) got &= (uint16_t)~KP_UI_BELOW_UNDER;
			kp_t_eq(T, got, want, "%s: under (%d, %d)", what, (int)x, (int)y);
		}
	}
}

/* What lay under a picture's pixels (KpUiImage.below, KpUiCanvas.below): StarCraft: Remastered draws a picture's HD
 * art over what lies beneath it, so its translucent texels show the background, not the picture's own SD pixel (the
 * briefing's TutBtn.pcx over its background showed its own SD through 4,368 pixels). A picture blitted over a
 * background picture keeps the background's values; drawn again in pieces (a partial repaint: the background's piece
 * first) it keeps them, and the background's piece knows nothing under it, not the picture it was drawn over; a blit
 * carries them; the canvas composes them with the pixels' items; a fill before the picture leaves nothing known, until
 * a repaint draws the background's piece under the picture's (the episode's pHist.pcx corners showed their SD). */
static void pictures_keep_what_lay_under_them(KpT *T)
{
	enum { W = 8, H = 4, PX = 2, PY = 1, PW = 4, PH = 2 };
	static const int16_t BACK[4] = { 0, 0, W - 1, H - 1 }, PIC[4] = { 0, 0, PW - 1, PH - 1 };
	static const int16_t BACK_PIECE[4] = { PX, PY, PX + 1, PY + PH - 1 }, PIC_PIECE[4] = { 0, 0, 1, PH - 1 };
	static const int32_t ALL[4] = { 0, 0, W, H };
	static const char BACK_PATH[] = "glue\\PalCs\\Backgnd.pcx", PIC_PATH[] = "glue\\campaign\\pHist.pcx";
	KpUi *ui = (KpUi *)calloc(1, sizeof *ui);
	KpUiCanvas *c = vt_canvas(T, W, H);
	KpUiSurface s = vt_sized(T, W, H, 0), back = vt_sized(T, W, H, 0), pic = vt_sized(T, PW, PH, 3);
	KpUiSurface copy = vt_sized(T, W, H, 0);
	KpUiFile back_file, pic_file;
	const KpUiImage *im;
	int32_t k, x, y;
	kp_t_ok(T, ui != NULL, "out of memory");
	kp_t_defer(T, owners_free, ui);
	ui->hd = 1;
	ui->text.target = &s;
	kp_t_eq(T, kp_ui_canvas_hd(c, 1), 0, "plane on");
	for (k = 0; k < W * H; k++) back.pixels[k] = (uint8_t)(10 + k);
	pic.pixels[0] = 0; /* keyed: not written */
	memset(&back_file, 0, sizeof back_file);
	memset(&pic_file, 0, sizeof pic_file);
	back_file.path = BACK_PATH;
	back_file.path_len = strlen(BACK_PATH);
	pic_file.path = PIC_PATH;
	pic_file.path_len = strlen(PIC_PATH);
	kp_ui_picture(ui, back.pixels, W, H, &back_file);
	kp_ui_picture(ui, pic.pixels, PW, PH, &pic_file);
	kp_ui_blit_opaque(ui, 0, 0, back.pixels, W, H, BACK);
	kp_ui_blit_transparent(ui, PX, PY, pic.pixels, PW, PH, PIC);
	kp_t_eq(T, ui->image_log.count, 2, "the background and the picture");
	check_below(T, ui, c, &s, back.pixels, 1, "the picture over the background");
	kp_t_eq(T, ui->image_log.below[PY * W + PX], KP_UI_BELOW_UNDER, "the keyed pixel the picture's item took: the background's");
	/* the canvas: the dialog's pixels with their items and what lay under them */
	ui->controls[0].bitmap = s;
	kp_ui_canvas_begin(NULL, c);
	kp_ui_canvas_dialog(ui, c, 0, 0, 0);
	kp_ui_canvas_end(c);
	for (k = 0; k < W * H; k++) {
		kp_t_eq(T, c->below[k], ui->image_log.below[k], "the canvas's plane at (%d, %d)", (int)(k % W), (int)(k / W));
	}
	kp_t_eq(T, c->below[(PY + 1) * W + PX + 1], KP_UI_BELOW_KNOWN | back.pixels[(PY + 1) * W + PX + 1], "the background's");
	/* a partial repaint: the background's piece, then the picture's */
	kp_ui_blit_opaque(ui, PX, PY, back.pixels, W, H, BACK_PIECE);
	k = image_of(ui, &s, BACK_PATH);
	kp_t_ok(T, k >= 0 && ui->image_log.images[k].w == 2, "the background's piece");
	if (k < 0) return;
	im = &ui->image_log.images[k];
	for (y = 0; y < PH; y++) {
		for (x = 0; x < 2; x++) {
			kp_t_eq(T, im->below[y * im->w + x], KP_UI_BELOW_WRITTEN, "nothing under the background's piece (%d, %d), not the picture",
				(int)x, (int)y);
		}
	}
	kp_ui_blit_transparent(ui, PX, PY, pic.pixels, PW, PH, PIC_PIECE);
	check_below(T, ui, c, &s, back.pixels, 1, "the picture drawn again in pieces");
	/* a blit carries them */
	memcpy(copy.pixels, s.pixels, (size_t)W * H);
	kp_ui_image_blit(ui, &s, &copy, 0, 0, ALL, 0);
	check_below(T, ui, c, &copy, back.pixels, 1, "carried");
	/* a fill under the picture drawn again: nothing known under it */
	kp_ui_fill_rect(ui, PX, PY, PW, PH, 7);
	kp_ui_blit_transparent(ui, PX, PY, pic.pixels, PW, PH, PIC);
	kp_t_ok(T, (k = image_of(ui, &s, PIC_PATH)) >= 0, "the picture");
	if (k < 0) return;
	im = &ui->image_log.images[k];
	for (k = 0; k < PW * PH; k++) {
		kp_t_eq(T, im->below[k], k == 0 ? 0 : KP_UI_BELOW_WRITTEN, "nothing known under the picture over the fill (%d)", (int)k);
	}
	/* one repaint: the background's piece lies under the picture's, not what lay under the picture before it */
	ui->image_log.repaint = ui->image_log.serial + 1;
	kp_ui_blit_opaque(ui, PX, PY, back.pixels, W, H, BACK_PIECE);
	kp_ui_blit_transparent(ui, PX, PY, pic.pixels, PW, PH, PIC_PIECE);
	ui->image_log.repaint = 0;
	kp_t_ok(T, (k = image_of(ui, &s, PIC_PATH)) >= 0 && ui->image_log.images[k].w == 2, "the picture's piece");
	if (k < 0) return;
	im = &ui->image_log.images[k];
	for (y = 0; y < PH; y++) {
		for (x = 0; x < 2; x++) {
			uint16_t want = x == 0 && y == 0 ? 0
				: (uint16_t)(KP_UI_BELOW_WRITTEN | KP_UI_BELOW_KNOWN | back.pixels[(PY + y) * W + PX + x]);
			kp_t_eq(T, im->below[y * im->w + x], want, "the background's piece under the picture's (%d, %d)", (int)x, (int)y);
		}
	}
	kp_ui_image_forget(ui, s.pixels);
	kp_ui_image_forget(ui, copy.pixels);
}

/* A popup's background (the episode's pHist.pcx) is drawn on the popup's own bitmap, with nothing under it, and the
 * bitmap composited over its screen keyed (BlitDialogToScreen 0x4c5630): what lay under the background's pixels is the
 * screen's. Over a fill nothing is known; the screen's background piece drawn in the same present
 * (KpUiImageLog.repaint) lies under the background composited again, not what lay under its earlier copy (pHist's
 * bottom corners showed their own SD through the HD art's). */
static void popups_lie_over_their_screen(KpT *T)
{
	enum { W = 8, H = 4, PX = 2, PY = 1, PW = 4, PH = 2 };
	static const int16_t BACK_PIECE[4] = { PX, PY, PX + 1, PY + PH - 1 }, PIC[4] = { 0, 0, PW - 1, PH - 1 };
	static const int16_t POPUP[4] = { PX, PY, PX + PW - 1, PY + PH - 1 };
	static const char BACK_PATH[] = "glue\\PalCs\\Backgnd.pcx", PIC_PATH[] = "glue\\campaign\\pHist.pcx";
	KpUi *ui = (KpUi *)calloc(1, sizeof *ui);
	KpUiSurface s = vt_sized(T, W, H, 0), back = vt_sized(T, W, H, 0), popup = vt_sized(T, PW, PH, 0);
	KpUiSurface pic = vt_sized(T, PW, PH, 3);
	KpUiFile back_file, pic_file;
	const KpUiImage *im;
	int32_t round, k, x, y;
	kp_t_ok(T, ui != NULL, "out of memory");
	kp_t_defer(T, owners_free, ui);
	ui->hd = 1;
	for (k = 0; k < W * H; k++) back.pixels[k] = (uint8_t)(10 + k);
	memset(&back_file, 0, sizeof back_file);
	memset(&pic_file, 0, sizeof pic_file);
	back_file.path = BACK_PATH;
	back_file.path_len = strlen(BACK_PATH);
	pic_file.path = PIC_PATH;
	pic_file.path_len = strlen(PIC_PATH);
	kp_ui_picture(ui, back.pixels, W, H, &back_file);
	kp_ui_picture(ui, pic.pixels, PW, PH, &pic_file);
	ui->text.target = &s;
	kp_ui_fill_rect(ui, PX, PY, PW, PH, 7);
	for (round = 0; round < 2; round++) { /* two presents: the second repaints the screen's background piece */
		ui->image_log.repaint = ui->image_log.serial + 1;
		ui->text.target = &s;
		if (round == 1) kp_ui_blit_opaque(ui, PX, PY, back.pixels, W, H, BACK_PIECE);
		ui->text.target = &popup;
		kp_ui_blit_transparent(ui, 0, 0, pic.pixels, PW, PH, PIC);
		ui->text.target = &s;
		kp_ui_blit_clipped(ui, popup.pixels, PW, PH, 0, 0, POPUP, KP_UI_TRANSPARENT);
		ui->image_log.repaint = 0;
		kp_t_ok(T, (k = image_of(ui, &s, PIC_PATH)) >= 0, "present %d: the popup's background over the screen", (int)round);
		if (k < 0) return;
		im = &ui->image_log.images[k];
		for (y = 0; y < PH; y++) {
			for (x = 0; x < PW; x++) {
				uint16_t want = KP_UI_BELOW_WRITTEN;
				if (round == 1 && x < 2) want |= (uint16_t)(KP_UI_BELOW_KNOWN | back.pixels[(PY + y) * W + PX + x]);
				kp_t_eq(T, im->below[y * im->w + x], want, "present %d: under (%d, %d)", (int)round, (int)x, (int)y);
			}
		}
	}
	kp_ui_image_forget(ui, s.pixels);
	kp_ui_image_forget(ui, popup.pixels);
}

enum { NP_W = 640, NP_H = 480, NP_SCREEN = 0, NP_LABEL = 1, NP_POPUP = 2, NP_BACK = 5, NP_TEXT = 6, NP_PANEL = 7 };
enum { NP_TEXT_X = 150, NP_TEXT_Y = 150, NP_NESTED = 0x8000000 };

static void np_screen_draw(KpUi *ui, KpUiControl *c, int32_t x, int32_t y, const int16_t clip[4])
{
	KpUiSurface *t = ui->text.target;
	int32_t i, j;
	(void)c;
	(void)x;
	(void)y;
	for (j = clip[1]; j <= clip[3]; j++) {
		for (i = clip[0]; i <= clip[2]; i++) t->pixels[j * t->w + i] = NP_BACK;
	}
}

/* As a control's text does (DrawFlcButtonText 0x4c8650): plotted whole, wherever the repaint's rectangle lies. */
static void np_label_draw(KpUi *ui, KpUiControl *c, int32_t x, int32_t y, const int16_t clip[4])
{
	KpUiSurface *t = ui->text.target;
	(void)c;
	(void)x;
	(void)y;
	(void)clip;
	t->pixels[NP_TEXT_Y * t->w + NP_TEXT_X] = NP_TEXT;
}

/* A popup composited into its screen's bitmap (the episode's history over the campaign's menu): the canvas shows the
 * whole bitmap, so a repaint that touches a control under the popup (a rectangle of the popup's own buttons, the
 * episode button's large one) leaves its text, plotted whole, under the popup's pixels too. A screen without a popup
 * repaints only its rectangles; a popup with nothing to repaint draws nothing. */
static void popups_cover_text_plotted_past_a_repaint(KpT *T)
{
	KpUi *ui = (KpUi *)calloc(1, sizeof *ui);
	KpUiSurface screen = vt_sized(T, NP_W, NP_H, 0), panel = vt_sized(T, 200, 200, NP_PANEL);
	KpUiControl *s, *l, *p;
	kp_t_ok(T, ui != NULL, "out of memory");
	kp_t_defer(T, free, ui);
	s = &ui->controls[NP_SCREEN];
	l = &ui->controls[NP_LABEL];
	p = &ui->controls[NP_POPUP];
	s->flags = l->flags = KP_UI_VISIBLE;
	s->right = NP_W - 1;
	s->bottom = NP_H - 1;
	s->bitmap = screen;
	s->draw = np_screen_draw;
	s->child = NP_LABEL;
	s->next = -1;
	l->right = 200;
	l->bottom = 200;
	l->draw = np_label_draw;
	l->next = -1;
	p->flags = KP_UI_VISIBLE | NP_NESTED;
	p->left = 100;
	p->top = 100;
	p->right = 299;
	p->bottom = 299;
	p->bitmap = panel;
	p->child = -1;
	p->next = NP_SCREEN;
	ui->open = NP_POPUP;
	kp_ui_add_dirty_rect(ui, 0, 0, NP_W - 1, NP_H - 1);
	kp_ui_present(ui);
	kp_t_eq(T, screen.pixels[NP_TEXT_Y * NP_W + NP_TEXT_X], NP_PANEL, "the whole screen painted: the popup over the text");
	kp_t_eq(T, screen.pixels[0], NP_BACK, "the screen's background");
	kp_t_eq(T, kp_ui_present(ui), 0, "nothing to repaint: nothing drawn");
	kp_ui_add_dirty_rect(ui, 0, 0, 15, 15); /* touches the label, far from its text and the popup */
	kp_ui_present(ui);
	kp_t_eq(T, screen.pixels[NP_TEXT_Y * NP_W + NP_TEXT_X], NP_PANEL, "a small repaint: the popup still over the text");
	ui->open = NP_SCREEN;
	screen.pixels[300 * NP_W + 300] = NP_TEXT; /* outside what the next repaint covers */
	kp_ui_add_dirty_rect(ui, 0, 0, 15, 15);
	kp_ui_present(ui);
	kp_t_eq(T, screen.pixels[300 * NP_W + 300], NP_TEXT, "no popup: only the rectangles repainted");
}

/* A flic button's overlay (kp_ui_image_movie) is an image of its movie's file and frame: a picture drawn over it later
 * lies over the movie's pixels (the episode's history panel, whose HD corners are transparent, over the episode
 * buttons' movies showed its own SD there); the images under it lose the pixels it wrote, and keyed, its index 0
 * writes nothing. Without the movie's file its pixels are no image's. */
static void movie_frames_are_images(KpT *T)
{
	enum { W = 8, H = 4, MW = 4, MH = 2, MX = 1, MY = 1, PX = 2 };
	static const int32_t ALL[4] = { 0, 0, W, H }, MOVIE[4] = { MX, MY, MX + MW, MY + MH }, ITEM[4] = { MX, MY, MW, MH };
	static const int16_t PIC[4] = { 0, 0, MW - 1, MH - 1 };
	static const char MOVIE_PATH[] = "glue\\campaign\\terr.smk", PIC_PATH[] = "glue\\campaign\\pHist.pcx";
	KpUi *ui = (KpUi *)calloc(1, sizeof *ui);
	KpUiCanvas *c = vt_canvas(T, W, H);
	KpUiSurface s = vt_sized(T, W, H, 5), frame = vt_sized(T, MW, MH, 7), pic = vt_sized(T, MW, MH, 3);
	KpUiVideo v;
	KpUiFile pic_file;
	const KpUiImage *im;
	int32_t i, j, k;
	kp_t_ok(T, ui != NULL, "out of memory");
	kp_t_defer(T, owners_free, ui);
	ui->hd = 1;
	ui->text.target = &s;
	kp_t_eq(T, kp_ui_canvas_hd(c, 1), 0, "plane on");
	owners_image(T, ui, &s, 0, ALL, 5, 0); /* the background */
	frame.pixels[0] = 0; /* keyed: not written */
	for (j = 0; j < MH; j++) {
		for (i = 0; i < MW; i++) {
			if (frame.pixels[j * MW + i] != 0) s.pixels[(MY + j) * W + MX + i] = frame.pixels[j * MW + i];
		}
	}
	memset(&v, 0, sizeof v);
	v.path = MOVIE_PATH;
	v.path_len = strlen(MOVIE_PATH);
	v.shown = 4;
	kp_ui_image_movie(ui, &v, &s, ITEM, MOVIE, &frame, 0, 0);
	k = image_of(ui, &s, MOVIE_PATH);
	kp_t_ok(T, k >= 0, "the movie's image");
	if (k < 0) return;
	im = &ui->image_log.images[k];
	kp_t_ok(T, im->frame == 4 && memcmp(im->item, ITEM, sizeof ITEM) == 0, "its frame %d and item", (int)im->frame);
	kp_t_ok(T, im->mask[0] == 0 && im->mask[1] == KP_UI_IMAGE_PIXEL, "its keyed pixel not written");
	kp_t_eq(T, im->below[1], KP_UI_BELOW_WRITTEN | KP_UI_BELOW_KNOWN | 5, "the background under it");
	im = &ui->image_log.images[0];
	kp_t_ok(T, im->mask[MY * W + MX] == KP_UI_IMAGE_PIXEL && im->mask[MY * W + MX + 1] == 0, "the background lost its pixels");
	/* a picture over the movie and past it */
	memset(&pic_file, 0, sizeof pic_file);
	pic_file.path = PIC_PATH;
	pic_file.path_len = strlen(PIC_PATH);
	kp_ui_picture(ui, pic.pixels, MW, MH, &pic_file);
	kp_ui_blit_opaque(ui, PX, MY, pic.pixels, MW, MH, PIC);
	kp_ui_canvas_begin(NULL, c);
	kp_t_ok(T, kp_ui_image_owners(ui, c, &s, s.pixels, NULL, 0, 0) != NULL, "owners");
	for (i = PX; i < PX + MW; i++) {
		uint16_t want = (uint16_t)(KP_UI_BELOW_KNOWN | (i < MX + MW ? 7 : 5));
		kp_t_eq(T, ui->image_log.below[MY * W + i], want, "under the picture at (%d, %d)", (int)i, MY);
	}
	/* without the movie's file: no image, its pixels taken from the images */
	v.path = NULL;
	v.path_len = 0;
	k = ui->image_log.count;
	kp_ui_image_movie(ui, &v, &s, ITEM, MOVIE, NULL, 0, 0);
	kp_t_ok(T, ui->image_log.count <= k && image_of(ui, &s, MOVIE_PATH) < 0, "no movie's image");
	k = image_of(ui, &s, PIC_PATH);
	kp_t_ok(T, k >= 0 && ui->image_log.images[k].mask[0] == 0 && ui->image_log.images[k].mask[MW - 1] == KP_UI_IMAGE_PIXEL,
		"the picture lost the pixels it drew over");
	kp_ui_image_forget(ui, s.pixels);
}

/* A theme frame (DrawThemeFrame 0x4c3d60: the image begun, the frame blitted inside it) lies over what lay under it, as
 * a blit's picture does: the images under it lose its pixels only at its end, once it found what lay under them, and
 * keep what lay under those pixels (a plain erase in the draw left a dialog's frames with nothing known under them,
 * their translucent HD texels over their own SD). */
static void theme_frames_lie_over_what_lay_under_them(KpT *T)
{
	enum { W = 8, H = 4, FX = 2, FY = 1, FW = 4, FH = 2 };
	static const int16_t BACK[4] = { 0, 0, W - 1, H - 1 }, FRAME[4] = { FX, FY, FX + FW - 1, FY + FH - 1 };
	static const char BACK_PATH[] = "glue\\PalNl\\Backgnd.pcx", GRP_PATH[] = "dlgs\\terran.grp";
	static const uint8_t GRP_BYTES[8] = { 1, 0, FW, 0, FH, 0, 0, 0 }; /* a file the log knows by its bytes */
	KpUi *ui = (KpUi *)calloc(1, sizeof *ui);
	KpUiSurface s = vt_sized(T, W, H, 0), back = vt_sized(T, W, H, 0), frame = vt_sized(T, FW, FH, 3);
	KpUiFile back_file, grp;
	KpUiImage *open;
	const KpUiImage *im;
	int32_t k, x, y;
	kp_t_ok(T, ui != NULL, "out of memory");
	kp_t_defer(T, owners_free, ui);
	ui->hd = 1;
	ui->text.target = &s;
	for (k = 0; k < W * H; k++) back.pixels[k] = (uint8_t)(10 + k);
	frame.pixels[0] = 0; /* keyed: not written */
	memset(&back_file, 0, sizeof back_file);
	back_file.path = BACK_PATH;
	back_file.path_len = strlen(BACK_PATH);
	kp_ui_picture(ui, back.pixels, W, H, &back_file);
	kp_ui_blit_opaque(ui, 0, 0, back.pixels, W, H, BACK);
	memset(&grp, 0, sizeof grp);
	grp.path = GRP_PATH;
	grp.path_len = strlen(GRP_PATH);
	grp.data = GRP_BYTES;
	grp.size = sizeof GRP_BYTES;
	ui->grps = &grp;
	ui->grp_count = 1;
	open = kp_ui_image_begin(ui, GRP_BYTES, 0, FX, FY, FW, FH);
	kp_t_ok(T, open != NULL, "the frame's image");
	if (open == NULL) return;
	kp_ui_blit_clipped(ui, frame.pixels, FW, FH, 0, 0, FRAME, KP_UI_TRANSPARENT);
	kp_ui_image_end(ui, open, 0);
	kp_t_ok(T, (k = image_of(ui, &s, GRP_PATH)) >= 0, "the frame's image kept");
	if (k < 0) return;
	im = &ui->image_log.images[k];
	for (y = 0; y < FH; y++) {
		for (x = 0; x < FW; x++) {
			uint16_t want = x == 0 && y == 0 ? 0
				: (uint16_t)(KP_UI_BELOW_WRITTEN | KP_UI_BELOW_KNOWN | back.pixels[(FY + y) * W + FX + x]);
			kp_t_eq(T, im->below[y * FW + x], want, "the background under the frame's (%d, %d)", (int)x, (int)y);
		}
	}
	kp_t_ok(T, (k = image_of(ui, &s, BACK_PATH)) >= 0, "the background's image");
	if (k < 0) return;
	im = &ui->image_log.images[k];
	for (y = 0; y < FH; y++) {
		for (x = 0; x < FW; x++) {
			int32_t at = (FY + y) * W + FX + x;
			int keyed = x == 0 && y == 0;
			kp_t_eq(T, im->mask[at], keyed ? KP_UI_IMAGE_PIXEL : 0, "the background's pixel (%d, %d) the frame took", (int)x, (int)y);
			kp_t_eq(T, im->below[at], KP_UI_BELOW_WRITTEN, "what lay under the background (%d, %d) kept", (int)x, (int)y);
		}
	}
	kp_ui_image_forget(ui, s.pixels);
}

/* A keyed blit carrying its source's images (a popup's bitmap over its screen, BlitDialogToScreen 0x4c5630) leaves the
 * target's pixels under its source's index 0 to the target's images, also where the source's image wrote that 0 (a
 * picture drawn opaque on the popup): the copy took the screen's pixel there, with the screen's SD colour as what lay
 * under its transparent HD texel instead of the screen's HD art. */
static void keyed_blits_leave_the_pixels_they_skip(KpT *T)
{
	enum { W = 8, H = 4, PX = 2, PY = 1, PW = 4, PH = 2 };
	static const int16_t BACK[4] = { 0, 0, W - 1, H - 1 }, PIC[4] = { 0, 0, PW - 1, PH - 1 };
	static const int16_t POPUP[4] = { PX, PY, PX + PW - 1, PY + PH - 1 };
	static const char BACK_PATH[] = "glue\\PalCs\\Backgnd.pcx", PIC_PATH[] = "glue\\campaign\\pHist.pcx";
	KpUi *ui = (KpUi *)calloc(1, sizeof *ui);
	KpUiCanvas *c = vt_canvas(T, W, H);
	KpUiSurface s = vt_sized(T, W, H, 0), back = vt_sized(T, W, H, 0), popup = vt_sized(T, PW, PH, 0);
	KpUiSurface pic = vt_sized(T, PW, PH, 3);
	KpUiFile back_file, pic_file;
	const uint16_t *owners;
	int32_t k, b;
	kp_t_ok(T, ui != NULL, "out of memory");
	kp_t_defer(T, owners_free, ui);
	ui->hd = 1;
	kp_t_eq(T, kp_ui_canvas_hd(c, 1), 0, "plane on");
	for (k = 0; k < W * H; k++) back.pixels[k] = (uint8_t)(10 + k);
	pic.pixels[0] = 0; /* drawn opaque: the picture's 0 */
	memset(&back_file, 0, sizeof back_file);
	memset(&pic_file, 0, sizeof pic_file);
	back_file.path = BACK_PATH;
	back_file.path_len = strlen(BACK_PATH);
	pic_file.path = PIC_PATH;
	pic_file.path_len = strlen(PIC_PATH);
	kp_ui_picture(ui, back.pixels, W, H, &back_file);
	kp_ui_picture(ui, pic.pixels, PW, PH, &pic_file);
	ui->text.target = &s;
	kp_ui_blit_opaque(ui, 0, 0, back.pixels, W, H, BACK);
	ui->text.target = &popup;
	kp_ui_blit_opaque(ui, 0, 0, pic.pixels, PW, PH, PIC);
	ui->text.target = &s;
	kp_ui_blit_clipped(ui, popup.pixels, PW, PH, 0, 0, POPUP, KP_UI_TRANSPARENT);
	kp_t_ok(T, (k = image_of(ui, &s, PIC_PATH)) >= 0, "the popup's picture carried");
	if (k < 0) return;
	kp_t_eq(T, ui->image_log.images[k].mask[0], 0, "the copy left the pixel its 0 did not write");
	kp_t_eq(T, ui->image_log.images[k].mask[1], KP_UI_IMAGE_PIXEL, "and took the others");
	kp_t_ok(T, (b = image_of(ui, &s, BACK_PATH)) >= 0 && ui->image_log.images[b].mask[PY * W + PX] == KP_UI_IMAGE_PIXEL,
		"the screen's picture kept it");
	kp_ui_canvas_begin(NULL, c);
	owners = kp_ui_image_owners(ui, c, &s, s.pixels, NULL, 0, 0);
	kp_t_ok(T, owners != NULL, "owners");
	if (owners == NULL) return;
	kp_t_ok(T, owners[PY * W + PX + 1] != 0 && owners[PY * W + PX + 1] != owners[0], "the copy's pixel");
	/* the copy's item lies over the screen's picture there: its HD art under the copy's, not an SD colour */
	kp_t_eq(T, owners[PY * W + PX], owners[PY * W + PX + 1], "the copy's item over the screen's picture's pixel");
	kp_t_eq(T, ui->image_log.below[PY * W + PX], KP_UI_BELOW_UNDER, "the screen's picture's pixel under it");
	kp_ui_image_forget(ui, s.pixels);
	kp_ui_image_forget(ui, popup.pixels);
}

/* An image whose pixels all lie under glyphs of the original font stays in the log: they are still the image's under
 * the glyphs, which the vector text gives back to it (it was dropped as drawn over, its pixels SD from then on). */
static void images_under_glyphs_stay(KpT *T)
{
	enum { W = 8, H = 4 };
	static const int32_t STRIP[4] = { 0, 0, 2, 1 }, OFFSETS[2] = { 0, 1 };
	KpUi *ui = (KpUi *)calloc(1, sizeof *ui);
	KpUiCanvas *c = vt_canvas(T, W, H);
	KpUiSurface s = vt_sized(T, W, H, 4);
	uint8_t copy[W * H], vector[W * H];
	const uint16_t *owners;
	kp_t_ok(T, ui != NULL, "out of memory");
	kp_t_defer(T, owners_free, ui);
	ui->hd = 1;
	kp_t_eq(T, kp_ui_canvas_hd(c, 1), 0, "plane on");
	owners_image(T, ui, &s, 1, STRIP, 5, 0);
	kp_ui_image_text(ui, &s, OFFSETS, 2);
	s.pixels[0] = s.pixels[1] = 9; /* the glyphs' */
	kp_ui_canvas_begin(NULL, c);
	owners = kp_ui_image_owners(ui, c, &s, s.pixels, NULL, 0, 0);
	kp_t_eq(T, ui->image_log.count, 1, "the image under the bitmap glyphs kept");
	kp_t_ok(T, owners != NULL && owners[0] == 0 && owners[1] == 0, "its pixels the glyphs'");
	memcpy(copy, s.pixels, sizeof copy); /* the vector text's copy: what lay under its glyphs */
	memset(vector, 0, sizeof vector);
	copy[0] = copy[1] = 5;
	vector[0] = vector[1] = 1;
	kp_ui_canvas_begin(NULL, c);
	owners = kp_ui_image_owners(ui, c, &s, copy, vector, 0, 0);
	kp_t_ok(T, owners != NULL && owners[0] != 0 && owners[1] != 0, "the vector text gives them back");
}

/* The audit's plane (KpUiCanvas.hd_plain) is the canvas's size while the item planes are on: the planes off or a
 * resize free it and a frame begun makes it again (a canvas grown with the planes off wrote past it). */
static void audit_plane_follows_the_canvas(KpT *T)
{
	KpUiCanvas *c = vt_canvas(T, 8, 4);
	c->hd_audit = 1;
	kp_ui_canvas_begin(NULL, c);
	kp_t_ok(T, c->hd_plain == NULL, "none without the item planes");
	kp_t_eq(T, kp_ui_canvas_hd(c, 1), 0, "planes on");
	kp_ui_canvas_begin(NULL, c);
	kp_t_ok(T, c->hd_plain != NULL && c->hd_plain_size == 8 * 4, "the canvas's size");
	kp_t_eq(T, kp_ui_canvas_hd(c, 0), 0, "planes off");
	kp_t_ok(T, c->hd_plain == NULL && c->hd_plain_size == 0, "freed with them");
	kp_t_eq(T, kp_ui_canvas_resize(c, 16, 8), 0, "grown");
	kp_ui_canvas_begin(NULL, c);
	kp_ui_canvas_fill(c, 0, 0, 16, 8, 5); /* every pixel written (ASan: none past the plane) */
	kp_t_ok(T, c->hd_plain == NULL, "none while the planes are off");
	kp_t_eq(T, kp_ui_canvas_hd(c, 1), 0, "planes on");
	kp_ui_canvas_begin(NULL, c);
	kp_t_eq(T, (int)c->hd_plain_size, 16 * 8, "made at the new size");
	kp_t_eq(T, kp_ui_canvas_resize(c, 4, 2), 0, "shrunk");
	kp_t_ok(T, c->hd_plain == NULL, "a resize frees it");
	kp_ui_canvas_begin(NULL, c);
	kp_ui_canvas_fill(c, 0, 0, 4, 2, 5);
	kp_t_eq(T, (int)c->hd_plain_size, 4 * 2, "made again");
	c->hd_audit = 0;
	kp_ui_canvas_begin(NULL, c);
	kp_t_ok(T, c->hd_plain == NULL, "none without the audit");
}

/* A fixed screen with its margins (kp_ui_canvas_screen) takes its dialog's owners once: its items are added once, the
 * development checks count its pixels drawn outside the log once, and a margin pixel is its edge pixel's item. */
static void screens_take_their_owners_once(KpT *T)
{
	enum { W = 8, H = 4, X = 2, CW = W + 2 * X };
	static const int32_t ALL[4] = { 0, 0, W, H };
	KpUi *ui = (KpUi *)calloc(1, sizeof *ui);
	KpUiCanvas *c = vt_canvas(T, CW, H);
	KpUiSurface s = vt_sized(T, W, H, 5);
	int32_t items, unlogged;
	kp_t_ok(T, ui != NULL, "out of memory");
	kp_t_defer(T, owners_free, ui);
	ui->hd = 1;
	kp_t_eq(T, kp_ui_canvas_hd(c, 1), 0, "plane on");
	c->hd_audit = 1;
	owners_image(T, ui, &s, 1, ALL, 5, 0);
	s.pixels[W + 3] = 6; /* a draw outside the log */
	ui->controls[0].bitmap = s;
	kp_ui_canvas_begin(NULL, c);
	kp_ui_canvas_dialog(ui, c, 0, X, 0);
	kp_ui_canvas_end(c);
	items = c->item_count;
	unlogged = c->hd_unlogged;
	kp_t_ok(T, items > 0 && unlogged == 1, "the dialog alone: %d items, %d drawn outside the log", (int)items, (int)unlogged);
	kp_ui_canvas_begin(NULL, c);
	kp_ui_canvas_screen(ui, c, 0, X, 0);
	kp_ui_canvas_end(c);
	kp_t_eq(T, c->item_count, items, "the items once");
	kp_t_eq(T, c->hd_unlogged, unlogged, "counted once");
	kp_t_ok(T, c->ids[0] != 0 && c->ids[0] == c->ids[X] && c->ids[CW - 1] == c->ids[X + W - 1], "the margins' items");
}

/* A pixel an image wrote stops being the image's once a draw outside the images writes it, even with the image's own
 * value (the minimap's pings under its terrain drawn again, a wireframe under its panel's fill): a fill takes its
 * rectangle, a keyed blit only its source's non-zero pixels; an image left with no pixel goes. */
static void plain_draws_erase_images(KpT *T)
{
	enum { W = 8, H = 4 };
	static const int32_t ALL[4] = { 0, 0, W, H }, LEFT[4] = { 0, 0, 4, 4 };
	static const int16_t SRC[4] = { 0, 0, 1, 1 };
	KpUi *ui = (KpUi *)calloc(1, sizeof *ui);
	KpUiCanvas *c = vt_canvas(T, W, H);
	KpUiSurface s = vt_sized(T, W, H, 5), key = vt_sized(T, 2, 2, 5);
	const uint16_t *owners;
	int32_t x, y;
	kp_t_ok(T, ui != NULL, "out of memory");
	kp_t_defer(T, owners_free, ui);
	ui->hd = 1;
	ui->text.target = &s;
	kp_t_eq(T, kp_ui_canvas_hd(c, 1), 0, "plane on");
	owners_image(T, ui, &s, 1, ALL, 5, 0);
	kp_ui_fill_rect(ui, 0, 0, 2, H, 5); /* the image's value, written by a fill */
	key.pixels[0] = 0; /* a keyed blit writes (3, 2), (2, 3) and (3, 3) */
	kp_ui_blit_transparent(ui, 2, 2, key.pixels, key.w, key.h, SRC);
	kp_ui_canvas_begin(NULL, c);
	owners = kp_ui_image_owners(ui, c, &s, s.pixels, NULL, 0, 0);
	kp_t_ok(T, owners != NULL, "owners");
	if (owners == NULL) return;
	for (y = 0; y < H; y++) {
		for (x = 0; x < W; x++) {
			int erased = x < 2 || (y >= 2 && x < 4 && !(x == 2 && y == 2));
			kp_t_eq(T, owners[y * W + x] != 0, !erased, "(%d, %d) the image's", (int)x, (int)y);
		}
	}
	owners_image(T, ui, &s, 2, LEFT, 5, 0);
	kp_t_eq(T, ui->image_log.count, 2, "two images");
	kp_ui_fill_rect(ui, 0, 0, 4, 4, 5);
	kp_t_eq(T, ui->image_log.count, 1, "the one written over whole gone");
	kp_t_eq(T, ui->image_log.images[0].frame, 1, "the other kept");
}

/* The development checks' audit (KpUiCanvas.hd_audit) counts an image's pixels a draw outside the image log changed:
 * such a draw (written here by hand) leaves the image's HD art where it wrote the image's own value, which the audit
 * cannot see, so it counts the pixels it wrote with another value; a fill (kp_ui_image_erase) and a later image are the
 * log's. The audit reads the surface, not the pixels composed from it (where the vector text put back what lay under
 * its glyphs). The plain background a tinted image takes is marked plain (SD by design under its HD art). */
static void draws_outside_the_log_are_audited(KpT *T)
{
	enum { W = 8, H = 4 };
	static const int32_t ALL[4] = { 0, 0, W, H }, RIGHT[4] = { 4, 0, 4, H }, ICON[4] = { 0, 0, 2, 2 };
	KpUi *ui = (KpUi *)calloc(1, sizeof *ui);
	KpUiCanvas *c = vt_canvas(T, W, H);
	KpUiSurface s = vt_sized(T, W, H, 5);
	const uint16_t *owners;
	int32_t x, y;
	kp_t_ok(T, ui != NULL, "out of memory");
	kp_t_defer(T, owners_free, ui);
	ui->hd = 1;
	ui->text.target = &s;
	kp_t_eq(T, kp_ui_canvas_hd(c, 1), 0, "plane on");
	owners_image(T, ui, &s, 1, ALL, 5, 0);
	s.pixels[0] = 6; /* a draw outside the log: (0, 0) another value, (1, 0) the image's */
	s.pixels[1] = 5;
	kp_ui_fill_rect(ui, 2, 0, 1, H, 7); /* the log's: erased */
	for (y = 0; y < H; y++) { /* a later image over the right half, as its draw wrote it */
		for (x = RIGHT[0]; x < W; x++) s.pixels[y * W + x] = 9;
	}
	owners_image(T, ui, &s, 2, RIGHT, 9, 0);
	kp_ui_canvas_begin(NULL, c);
	owners = kp_ui_image_owners(ui, c, &s, s.pixels, NULL, 0, 0);
	kp_t_eq(T, c->hd_unlogged, 0, "not counted without the audit");
	c->hd_audit = 1;
	kp_ui_canvas_begin(NULL, c);
	owners = kp_ui_image_owners(ui, c, &s, s.pixels, NULL, 0, 0);
	kp_t_ok(T, owners != NULL, "owners");
	if (owners == NULL) return;
	kp_t_eq(T, c->hd_unlogged, 1, "the pixel the draw outside the log changed");
	kp_t_eq(T, strcmp(c->hd_unlogged_paths, "dlgs\\test.grp:1"), 0, "its image named: %s", c->hd_unlogged_paths);
	kp_t_ok(T, owners[0] == 0 && owners[1] != 0, "(1, 0), the same value, still the image's (what the audit witnesses)");
	{ /* the vector text's copy, where a glyph's pixel holds what lay under it: the bitmap holds the image's value */
		uint8_t copy[W * H], vector[W * H];
		memcpy(copy, s.pixels, sizeof copy);
		memset(vector, 0, sizeof vector);
		copy[W + 5] = 1;
		vector[W + 5] = 1;
		kp_ui_canvas_begin(NULL, c);
		(void)kp_ui_image_owners(ui, c, &s, copy, vector, 0, 0);
		kp_t_eq(T, c->hd_unlogged, 1, "a glyph's pixel the vector text put back is no draw outside the log");
	}
	/* a tinted image over the plain background of its rectangle: the pixels it took are plain */
	kp_ui_image_log_free(&ui->image_log);
	memset(s.pixels, 3, (size_t)W * H);
	s.pixels[0] = 4;
	owners_image(T, ui, &s, 3, ICON, 4, 1);
	memset(ui->image_log.images[0].mask, 0, 4);
	ui->image_log.images[0].mask[0] = KP_UI_IMAGE_PIXEL;
	kp_ui_canvas_begin(NULL, c);
	owners = kp_ui_image_owners(ui, c, &s, s.pixels, NULL, 0, 0);
	kp_t_ok(T, owners != NULL, "owners");
	if (owners == NULL) return;
	for (y = 0; y < ICON[3]; y++) {
		for (x = 0; x < ICON[2]; x++) {
			kp_t_ok(T, owners[y * W + x] != 0, "(%d, %d) the icon's", (int)x, (int)y);
			kp_t_eq(T, ui->image_log.plain[y * W + x], x != 0 || y != 0, "(%d, %d) plain", (int)x, (int)y);
		}
	}
	kp_t_eq(T, c->hd_unlogged, 0, "no draw outside the log");
}

/* A glyph of the original font plotted over an image (StarCraft: Remastered draws its text over the art) shows its own
 * colours: its pixels are no image's, also those where it wrote the image's value (here every body pixel: the image
 * and the background are the font's body colour); the vector text, which puts back what lay under its glyphs, gives
 * those pixels back to the image; a blit carries the glyphs' marks with the image. */
static void glyphs_lie_over_images(KpT *T)
{
	static const int32_t ALL[4] = { 0, 0, VT_W, VT_H };
	static const int32_t WHOLE[4] = { 0, 0, VT_W, VT_H };
	KpOracleUi *u;
	KpUi *ui;
	KpUiCanvas *c;
	KpUiSurface ref, s, copy, *saved;
	uint8_t body, none, vector[VT_W * VT_H], under[VT_W * VT_H];
	const uint16_t *owners;
	int32_t p, glyph_pixels = 0, same = 0, k;
	if (!kp_t_ready(T, "test_kp_ui.glyphs_lie_over_images", UI_FILES)) return;
	u = kp_t_ui(T, 0);
	ui = u->ui;
	kp_t_ok(T, ui->game_rows_size > 8, "the game's colour rows");
	body = ui->game_rows[1];
	for (none = 1; none != 0; none++) { /* a background no glyph pixel has */
		for (k = 0; k < 8 && ui->game_rows[k] != none; k++) {}
		if (k == 8) break;
	}
	ref = vt_surface(T, none);
	vt_draw(ui, &ref, NULL);
	kp_ui_set_hd(ui, 1);
	kp_t_ok(T, ui->text.plotted != NULL, "the HD interface hears of the glyphs' pixels");
	s = vt_surface(T, body);
	owners_image(T, ui, &s, 0, ALL, body, 0);
	saved = ui->text.target;
	(void)kp_ui_text_select_font(&ui->text, ui->font_files[1], ui->font_file_sizes[1]);
	kp_ui_text_set_rows(&ui->text, ui->game_rows, ui->game_rows_size);
	ui->text.target = &s;
	kp_ui_draw_text(&ui->text, 4, 4, (const uint8_t *)"Hello, world");
	ui->text.target = saved;
	(void)kp_ui_text_select_font(&ui->text, NULL, 0);
	c = vt_canvas(T, VT_W, VT_H);
	kp_t_eq(T, kp_ui_canvas_hd(c, 1), 0, "plane on");
	kp_ui_canvas_begin(NULL, c);
	owners = kp_ui_image_owners(ui, c, &s, s.pixels, NULL, 0, 0);
	kp_t_ok(T, owners != NULL, "owners");
	if (owners == NULL) return;
	for (p = 0; p < VT_W * VT_H; p++) {
		int glyph = ref.pixels[p] != none;
		glyph_pixels += glyph;
		same += glyph && s.pixels[p] == body;
		kp_t_eq(T, owners[p] != 0, !glyph, "(%d, %d): %s", (int)(p % VT_W), (int)(p / VT_W), glyph ? "the glyph's"
			: "the image's");
		vector[p] = (uint8_t)glyph;
		under[p] = glyph ? body : s.pixels[p];
	}
	kp_t_ok(T, glyph_pixels > 0 && same > 0, "glyph pixels of the image's value: %d of %d", (int)same,
		(int)glyph_pixels);
	/* the vector text: what lay under each glyph pixel put back, the image's */
	kp_ui_canvas_begin(NULL, c);
	owners = kp_ui_image_owners(ui, c, &s, under, vector, 0, 0);
	kp_t_ok(T, owners != NULL, "owners");
	if (owners == NULL) return;
	for (p = 0; p < VT_W * VT_H; p++) kp_t_ok(T, owners[p] != 0, "(%d, %d) the image's", (int)(p % VT_W), (int)(p / VT_W));
	/* a blit carries the image and its glyphs' marks */
	copy = vt_surface(T, 0);
	memcpy(copy.pixels, s.pixels, (size_t)VT_W * VT_H);
	kp_ui_image_blit(ui, &s, &copy, 0, 0, WHOLE, 0);
	kp_ui_canvas_begin(NULL, c);
	owners = kp_ui_image_owners(ui, c, &copy, copy.pixels, NULL, 0, 0);
	kp_t_ok(T, owners != NULL, "owners of the copy");
	if (owners == NULL) return;
	for (p = 0; p < VT_W * VT_H; p++) {
		kp_t_eq(T, owners[p] != 0, ref.pixels[p] == none, "the copy's (%d, %d)", (int)(p % VT_W), (int)(p / VT_W));
	}
	kp_ui_image_forget(ui, s.pixels);
	kp_ui_image_forget(ui, copy.pixels);
}

/* A movie's frame shown is an image of its file and frame over its whole bitmap while the log is on; none before a
 * frame is shown or with the log off. */
static void video_image_logs_the_frame(KpT *T)
{
	KpUi *ui = (KpUi *)calloc(1, sizeof *ui);
	KpUiSurface s = vt_sized(T, 6, 4, 9);
	KpUiVideo v;
	const KpUiImage *im;
	kp_t_ok(T, ui != NULL, "out of memory");
	kp_t_defer(T, owners_free, ui);
	memset(&v, 0, sizeof v);
	v.path = "portrait\\tmarine\\tmatlk00.smk";
	v.path_len = strlen(v.path);
	v.shown = -1;
	ui->hd = 1;
	kp_ui_video_image(ui, &v, &s);
	kp_t_eq(T, ui->image_log.count, 0, "no frame shown");
	v.shown = 3;
	ui->hd = 0;
	kp_ui_video_image(ui, &v, &s);
	kp_t_eq(T, ui->image_log.count, 0, "the log off");
	ui->hd = 1;
	kp_ui_video_image(ui, &v, &s);
	im = kp_ui_image_last(ui, s.pixels);
	kp_t_ok(T, im != NULL && im->frame == 3 && im->path_len == v.path_len && im->w == 6 && im->h == 4
		&& im->item[2] == 6 && im->item[3] == 4, "the frame over the bitmap");
	kp_ui_video_image(ui, &v, &s);
	kp_t_eq(T, ui->image_log.count, 1, "drawn again: the earlier one covered");
}

/* The log keeps each glyph's pen, byte and the pixels it wrote over the background, and the surface the original's
 * pixels; a glyph drawn again over itself keeps the background under it; a glyph painted over is no longer live. */
static void vector_text_logs_the_glyphs(KpT *T)
{
	KpOracleUi *u;
	KpUiGlyphLog *log;
	KpUiSurface plain, logged;
	size_t k;
	uint32_t j;
	if (!kp_t_ready(T, "test_kp_ui.vector_text_logs_the_glyphs", UI_FILES)) return;
	u = kp_t_ui(T, 0);
	log = &u->ui->glyph_log;
	plain = vt_surface(T, VT_BACK);
	logged = vt_surface(T, VT_BACK);
	vt_draw(u->ui, &plain, NULL);
	vt_draw(u->ui, &logged, log);
	kp_t_ok(T, memcmp(plain.pixels, logged.pixels, VT_W * VT_H) == 0, "the log changed the surface");
	kp_t_eq(T, (int64_t)log->count, 11, "the glyphs of \"Hello,world\"");
	for (k = 0; k < log->count; k++) {
		const KpUiGlyph *g = &log->glyphs[k];
		kp_t_eq(T, g->code, (int64_t)"Hello,world"[k], NULL);
		kp_t_ok(T, g->pen_y == 4 && (k == 0 ? g->pen_x == 4 : g->pen_x > log->glyphs[k - 1].pen_x), "glyph %d's pen",
			(int)k);
		kp_t_ok(T, !g->wrapped && !g->dead && !g->part && g->count > 0 && kp_ui_glyph_live(log, g, &logged),
			"glyph %d live", (int)k);
		for (j = g->first; j < g->first + g->count; j++) kp_t_eq(T, log->under[j], VT_BACK, "the background under it");
	}
	vt_draw(u->ui, &logged, log); /* again over itself, its background kept */
	kp_t_eq(T, (int64_t)log->count, 22, NULL);
	for (k = 0; k < 11; k++) kp_t_ok(T, log->glyphs[k].dead, "glyph %d drawn over", (int)k);
	for (k = 11; k < 22; k++) {
		const KpUiGlyph *g = &log->glyphs[k];
		for (j = g->first; j < g->first + g->count; j++) kp_t_eq(T, log->under[j], VT_BACK, "the background inherited");
	}
	{ /* a surface at a freed one's address with fewer rows holds none of its glyphs (none of them is read) */
		KpUiSurface shorter = logged;
		shorter.h = 4;
		kp_t_ok(T, !kp_ui_glyph_live(log, &log->glyphs[11], &shorter), "a shorter surface at its address");
	}
	for (j = 0; j < VT_H; j++) memset(logged.pixels + VT_W * j, VT_BACK, 10); /* the first glyph painted over */
	kp_t_ok(T, !kp_ui_glyph_live(log, &log->glyphs[11], &logged), "a glyph painted over");
	kp_ui_glyph_log_prune(log, 0);
	kp_t_eq(T, (int64_t)log->count, 11, "the dead glyphs pruned");
}

/* With the vector text on, the canvas takes the live glyphs out of a surface it composes (what lay under them in their
 * place) and lists them at their canvas pens; a later draw over a glyph drops it or cuts it to the rectangles of its
 * box the draw left uncovered.
 * Off, the canvas keeps the glyphs' pixels and lists none. */
static void vector_text_takes_the_glyphs_out_of_the_canvas(KpT *T)
{
	KpOracleUi *u;
	KpUi *ui;
	KpUiCanvas *c;
	KpUiSurface s;
	KpUiCanvasGlyph first[11];
	uint8_t codes[256];
	int32_t k, x, y, inked = 0, cut, wide, line;
	if (!kp_t_ready(T, "test_kp_ui.vector_text_takes_the_glyphs_out_of_the_canvas", UI_FILES)) return;
	u = kp_t_ui(T, 0);
	ui = u->ui;
	c = vt_canvas(T, 200, 40);
	memset(codes, 1, sizeof codes);
	s = vt_surface(T, 0); /* a layer: index 0 transparent */
	kp_ui_set_vector_text(ui, 1, 1, codes);
	vt_draw(ui, &s, &ui->glyph_log);
	kp_ui_canvas_begin(ui, c);
	kp_ui_canvas_layer(ui, c, &s, 10, 5);
	kp_ui_canvas_text_end(ui, c);
	kp_ui_canvas_end(c);
	kp_t_eq(T, c->glyph_count, 11, NULL);
	for (k = 0; k < c->glyph_count; k++) {
		kp_t_ok(T, c->glyphs[k].whole && c->glyphs[k].y == 9 && c->glyphs[k].x == 10 + ui->glyph_log.glyphs[k].pen_x,
			"glyph %d at its canvas pen", (int)k);
	}
	memcpy(first, c->glyphs, sizeof first);
	for (y = 0; y < 40; y++) {
		for (x = 0; x < 200; x++) inked += c->pixels[2 * (y * 200 + x) + 1] != 0;
	}
	kp_t_eq(T, inked, 0, "the glyphs' pixels left the canvas");
	/* a fill over the first four glyphs and the left part of the fifth ("Hell" and the o's left column) */
	cut = c->glyphs[4].x + c->glyphs[4].box[2] + 1;
	kp_ui_canvas_begin(ui, c);
	kp_ui_canvas_layer(ui, c, &s, 10, 5);
	kp_ui_canvas_fill(c, 0, 0, cut, 40, 7);
	kp_ui_canvas_text_end(ui, c);
	kp_ui_canvas_end(c);
	kp_t_eq(T, c->glyph_count, 7, "the covered glyphs dropped");
	kp_t_ok(T, !c->glyphs[0].whole && c->glyphs[0].clip[0] >= cut, "the o cut where the fill ends");
	for (k = 1; k < c->glyph_count; k++) kp_t_ok(T, c->glyphs[k].whole, "glyph %d uncovered", (int)k + 4);
	/* a line across a glyph (a popup's frame over the menu's text, the user's report of 2026-11-13): the glyph is
	 * listed once on each side, neither piece over the line (its uncovered pixels' box held the line) */
	for (k = 1, wide = 0; k < 11; k++) {
		if (first[k].box[0] > first[wide].box[0]) wide = k;
	}
	line = first[wide].x + first[wide].box[2] + first[wide].box[0] / 2;
	kp_ui_canvas_begin(ui, c);
	kp_ui_canvas_layer(ui, c, &s, 10, 5);
	kp_ui_canvas_fill(c, line, 0, 1, 40, 7);
	kp_ui_canvas_text_end(ui, c);
	kp_ui_canvas_end(c);
	kp_t_eq(T, c->glyph_count, 12, "the glyph across the line listed twice");
	for (k = 0, cut = 0; k < c->glyph_count; k++) {
		const KpUiCanvasGlyph *g = &c->glyphs[k];
		if (g->code != first[wide].code || g->x != first[wide].x) continue;
		kp_t_ok(T, !g->whole && (g->clip[2] <= line || g->clip[0] >= line + 1), "piece %d beside the line: %d..%d",
			(int)cut, (int)g->clip[0], (int)g->clip[2]);
		cut++;
	}
	kp_t_eq(T, cut, 2, "the pieces of the glyph across the line");
	/* off: the bitmaps */
	kp_ui_set_vector_text(ui, 1, 0, codes);
	kp_ui_canvas_begin(ui, c);
	kp_ui_canvas_layer(ui, c, &s, 10, 5);
	kp_ui_canvas_text_end(ui, c);
	kp_ui_canvas_end(c);
	kp_t_eq(T, c->glyph_count, 0, NULL);
	inked = 0;
	for (y = 0; y < 40; y++) {
		for (x = 0; x < 200; x++) inked += c->pixels[2 * (y * 200 + x) + 1] != 0;
	}
	kp_t_ok(T, inked > 0, "the bitmaps on the canvas");
	kp_ui_set_vector_text(ui, 0, 0, NULL);
	kp_t_ok(T, ui->text.log == NULL && ui->glyph_log.count == 0, "recording off frees the log");
}

/* The edit box's caret ({4, glyph, 1} after the text) is drawn without advancing the pen, and the glyph log keeps it so:
 * the vector layout reads a glyph that advances as one more letter of the last word and moves the line's letters. */
static void vector_text_logs_the_caret_without_advance(KpT *T)
{
	KpOracleUi *u;
	KpUi *ui;
	KpUiGlyphLog *log;
	KpUiSurface s;
	KpUiText t;
	uint8_t codes[256];
	const uint8_t text[] = { 'H', 'e', 'l', 'l', 'o', 4, 'H', 1, 0 };
	size_t k;
	if (!kp_t_ready(T, "test_kp_ui.vector_text_logs_the_caret_without_advance", UI_FILES)) return;
	u = kp_t_ui(T, 0);
	ui = u->ui;
	log = &ui->glyph_log;
	memset(codes, 1, sizeof codes);
	kp_ui_set_vector_text(ui, 1, 1, codes);
	s = vt_surface(T, 0);
	kp_ui_text_init(&t);
	kp_ui_text_select_font(&t, ui->font_files[1], ui->font_file_sizes[1]);
	kp_ui_text_set_rows(&t, ui->game_rows, ui->game_rows_size);
	t.target = &s;
	t.log = log;
	t.highlight = 'H';
	(void)kp_ui_draw_text(&t, 4, 4, text);
	kp_t_eq(T, (int64_t)log->count, 6, "the text's five glyphs and the caret");
	for (k = 0; k < 5; k++) kp_t_ok(T, log->glyphs[k].advance > 0, "glyph %lu advances", (unsigned long)k);
	kp_t_eq(T, log->glyphs[5].advance, 0, "the caret does not advance");
	kp_t_eq(T, log->glyphs[5].pen_x, log->glyphs[4].pen_x + log->glyphs[4].advance, "the caret at the pen after the text");
}

/* A blit carries its source's glyphs into its target (a glue popup into its screen): they are the target's, at their
 * place there, with what lay under them blitted alike; the target's glyphs it wrote over keep only their other pixels
 * (a part, cut where the blit begins); a translucent blit over glyphs leaves them a part in the blended colours. */
static void vector_text_follows_a_blit(KpT *T)
{
	KpOracleUi *u;
	KpUi *ui;
	KpUiGlyphLog *log;
	KpUiSurface screen, popup, veil;
	const KpUiGlyphSurface *sf;
	KpUiCanvas *c;
	uint8_t codes[256], rgb[768], colour = 200;
	int16_t rect[4] = { 60, 10, 159, 33 };
	uint32_t k, own = 0, cut = 0, copied = 0, tinted = 0, j;
	int32_t pens[11];
	int n;
	if (!kp_t_ready(T, "test_kp_ui.vector_text_follows_a_blit", UI_FILES)) return;
	u = kp_t_ui(T, 0);
	ui = u->ui;
	log = &ui->glyph_log;
	memset(codes, 1, sizeof codes);
	kp_ui_set_vector_text(ui, 1, 1, codes);
	screen = vt_sized(T, 200, 40, VT_BACK);
	popup = vt_sized(T, 100, 24, 0x33);
	vt_draw(ui, &screen, log);
	vt_draw(ui, &popup, log);
	for (n = 0; n < 11; n++) pens[n] = log->glyphs[11 + n].pen_x;
	ui->text.target = &screen;
	kp_ui_blit_clipped(ui, popup.pixels, popup.w, popup.h, 0, 0, rect, 0);
	sf = kp_ui_glyph_surface(log, screen.pixels, screen.w);
	kp_t_ok(T, sf != NULL, "the screen's glyphs");
	for (k = sf->head; k != KP_UI_GLYPH_NONE; k = log->glyphs[k].next) {
		const KpUiGlyph *g = &log->glyphs[k];
		if (g->dead) continue;
		kp_t_ok(T, kp_ui_glyph_live(log, g, &screen), "glyph %u live on the screen", k);
		if (k >= 22) { /* the popup's, at its place on the screen, the popup's background under them */
			kp_t_ok(T, !g->part && g->pen_x == pens[copied] + 60 && g->pen_y == 4 + 10, "the popup's glyph %u's pen", copied);
			for (j = g->first; j < g->first + g->count; j++) kp_t_eq(T, log->under[j], 0x33, "the popup's background");
			copied++;
		} else if (g->part) { /* the screen's own under the popup's top: cut at its row 10 */
			int32_t left = g->pen_x + g->box[2];
			kp_t_ok(T, left >= 60 ? g->clip[3] == 10 : g->clip[2] <= 60 && left + g->box[0] > 60,
				"glyph %u cut where the popup begins (its top above row 10, or its left of column 60)", k);
			cut++;
		} else {
			own++;
		}
	}
	kp_t_eq(T, copied, 11, "the popup's glyphs on the screen");
	kp_t_ok(T, cut > 0 && own > 0, "the screen's glyphs cut (%u) and whole (%u)", cut, own);
	c = vt_canvas(T, 200, 40);
	kp_ui_canvas_begin(ui, c);
	kp_ui_canvas_layer(ui, c, &screen, 0, 0);
	kp_ui_canvas_text_end(ui, c);
	kp_t_eq(T, c->glyph_count, (int64_t)(own + cut + copied), "all of them on the canvas");
	/* a translucent veil of one colour over the popup's glyphs blends them (BlitSingleColourBlend) */
	for (n = 0; n < 256; n++) rgb[3 * n] = rgb[3 * n + 1] = rgb[3 * n + 2] = (uint8_t)n;
	kp_t_ok(T, kp_ui_game_effects(ui, rgb, NULL) == 0, "the effect tables");
	ui->effects.colour[0] = colour;
	veil = vt_sized(T, 100, 24, colour);
	kp_ui_blit_clipped(ui, veil.pixels, veil.w, veil.h, 0, 0, rect, KP_UI_TRANSLUCENT);
	for (k = sf->head; k != KP_UI_GLYPH_NONE; k = log->glyphs[k].next) {
		const KpUiGlyph *g = &log->glyphs[k];
		if (g->dead || g->pen_y != 14) continue;
		kp_t_ok(T, g->part && kp_ui_glyph_live(log, g, &screen), "glyph %u blended", k);
		kp_t_eq(T, g->fill, kp_ui_single_colour(ui, 0, colour, log->glyphs[11].fill), "in the blended colour");
		tinted++;
	}
	kp_t_eq(T, tinted, 11, "the popup's glyphs under the veil");
	ui->text.target = NULL;
}

/* A glyph larger than the log's direct box is wrapped (a bitmap) and safe to record; a font with no cap height (no
 * 'H') keeps its glyphs' bitmaps on the canvas. */
static void vector_text_keeps_odd_fonts_bitmaps(KpT *T)
{
	enum { BIG = 40, SMALL = 6, TABLE = 8 + 2 * 4 };
	KpOracleUi *u;
	KpUi *ui;
	KpUiText t;
	KpUiSurface s;
	KpUiCanvas *c;
	uint8_t *font, codes[256];
	const uint8_t *saved;
	size_t size = TABLE + 4 + SMALL * SMALL + 4 + BIG * BIG, saved_size, at;
	int32_t x, inked = 0;
	if (!kp_t_ready(T, "test_kp_ui.vector_text_keeps_odd_fonts_bitmaps", UI_FILES)) return;
	u = kp_t_ui(T, 0);
	ui = u->ui;
	font = (uint8_t *)calloc(1, size);
	kp_t_ok(T, font != NULL, "out of memory");
	kp_t_defer(T, free, font);
	memcpy(font, "FONT", 4);
	font[4] = 'A'; /* 'A' a small glyph, 'B' a 40 x 40 one: no 'H' */
	font[5] = 'B';
	font[6] = font[7] = BIG;
	at = TABLE;
	font[8] = (uint8_t)at;
	font[at] = font[at + 1] = SMALL;
	memset(font + at + 4, 1, SMALL * SMALL); /* every pixel mask 1 */
	at += 4 + SMALL * SMALL;
	font[12] = (uint8_t)at;
	font[13] = (uint8_t)(at >> 8);
	font[at] = font[at + 1] = BIG;
	memset(font + at + 4, 1, BIG * BIG);
	memset(codes, 1, sizeof codes);
	saved = ui->font_files[1];
	saved_size = ui->font_file_sizes[1];
	ui->font_files[1] = font;
	ui->font_file_sizes[1] = size;
	kp_ui_set_vector_text(ui, 1, 1, codes);
	kp_t_ok(T, !ui->vector_font_ok[1] && ui->vector_font_ok[0], "no cap height without an 'H'");
	s = vt_sized(T, 120, 60, 0);
	kp_ui_text_init(&t);
	kp_t_eq(T, kp_ui_text_select_font(&t, font, size), 0, NULL);
	kp_ui_text_set_rows(&t, ui->game_rows, ui->game_rows_size);
	t.target = &s;
	t.log = &ui->glyph_log;
	kp_ui_draw_text(&t, 4, 4, (const uint8_t *)"AB");
	kp_t_eq(T, (int64_t)ui->glyph_log.count, 2, NULL);
	kp_t_ok(T, !ui->glyph_log.glyphs[0].wrapped && ui->glyph_log.glyphs[1].wrapped, "the 40 x 40 glyph wrapped");
	c = vt_canvas(T, 120, 60);
	kp_ui_canvas_begin(ui, c);
	kp_ui_canvas_layer(ui, c, &s, 0, 0);
	kp_ui_canvas_text_end(ui, c);
	kp_ui_canvas_end(c);
	kp_t_eq(T, c->glyph_count, 0, "no vector glyphs");
	for (x = 0; x < 120 * 60; x++) inked += c->pixels[2 * x + 1] != 0;
	kp_t_eq(T, inked, SMALL * SMALL + BIG * BIG, "both bitmaps on the canvas");
	ui->font_files[1] = saved;
	ui->font_file_sizes[1] = saved_size;
	kp_ui_set_vector_text(ui, 0, 0, NULL);
}

/* The canvas lists the tooltip's glyphs inside its box: a glyph past the box's right edge stays out, one it cuts is
 * clipped. While the vector text is off, the log keeps the live glyphs of the surfaces composed. */
static void vector_text_keeps_to_the_tooltip_box(KpT *T)
{
	KpOracleUi *u;
	KpUi *ui;
	KpUiCanvas *c;
	KpUiTooltip *tip;
	uint8_t codes[256];
	int32_t k, x, y, w, h, inside = 0, cut = 0;
	size_t logged;
	if (!kp_t_ready(T, "test_kp_ui.vector_text_keeps_to_the_tooltip_box", UI_FILES)) return;
	u = kp_t_ui(T, 0);
	ui = u->ui;
	memset(codes, 1, sizeof codes);
	kp_ui_set_vector_text(ui, 1, 1, codes);
	kp_ui_font_set(ui, 1);
	kp_ui_tooltip_init(ui);
	kp_ui_tooltip_text(ui, 10, 10, (const uint8_t *)"Hello, world");
	tip = &ui->tooltip;
	kp_t_ok(T, kp_ui_tooltip(ui, &x, &y, &w, &h) != NULL && ui->glyph_log.count == 11, "the tooltip's glyphs");
	tip->w = 30; /* the box cut short */
	c = vt_canvas(T, 200, 60);
	kp_ui_canvas_begin(ui, c);
	kp_ui_canvas_tooltip(ui, c, 0, 0);
	kp_ui_canvas_text_end(ui, c);
	for (k = 0; k < c->glyph_count; k++) {
		const KpUiCanvasGlyph *g = &c->glyphs[k];
		kp_t_ok(T, g->x + g->box[2] < 30, "glyph %d starts inside the box", (int)k);
		if (g->whole) inside++;
		else cut++, kp_t_ok(T, g->clip[2] <= 30, "glyph %d clipped at the box", (int)k);
	}
	kp_t_ok(T, inside > 0 && inside + cut < 11, "the glyphs inside (%d), cut (%d)", (int)inside, (int)cut);
	/* recording with the vector text off: the composed surfaces' live glyphs stay in the log */
	kp_ui_set_vector_text(ui, 1, 0, codes);
	logged = ui->glyph_log.count;
	for (k = 0; k < 200; k++) {
		kp_ui_canvas_begin(ui, c);
		kp_ui_canvas_tooltip(ui, c, 0, 0);
		kp_ui_canvas_text_end(ui, c);
	}
	kp_t_eq(T, (int64_t)ui->glyph_log.count, (int64_t)logged, "the glyphs kept while off");
	kp_ui_set_vector_text(ui, 0, 0, NULL);
}

/* --- interfaces over the converted data --- */

static void theme(KpT *T, KpUi *ui, int race)
{
	kp_t_ok(T, kp_ui_apply_theme(ui, kp_oracle_race_theme(race)) == 0, "ui: no GRP %s", kp_oracle_race_theme(race));
}

static uint32_t dialog_crc(const KpUi *ui, int32_t d)
{
	const KpUiSurface *s = &ui->controls[d].bitmap;
	return s->pixels ? kp_crc32(s->pixels, (size_t)s->w * (size_t)s->h) : 0;
}

/* The panels kp_ui draws against the original's (tests/golden/oracle/ui/panels.json, tools/oracle/export_ui_panels.py):
 * the CRC-32 of each panel's surface equals that of the capture's crop at the dialog's rectangle (ADR 0011, level 2). */
static void panels_equal_the_original(KpT *T)
{
	const KpJson *cases;
	int k, checked = 0;
	if (!kp_t_ready(T, "test_kp_ui.panels_equal_the_original", UI_FILES)) return;
	cases = kp_json_get(kp_t_golden(T, "tests/golden/oracle/ui/panels.json"), "cases");
	kp_t_data_117(T); /* the captures are of 1.17's console */
	for (k = 0; k < kp_json_len(cases); k++) {
		const KpJson *c = kp_json_at(cases, k);
		int mark = kp_t_mark(T);
		if (strcmp(kp_json_str(kp_json_at(c, 3), ""), "rez\\stat_f10.bin") == 0) {
			KpOracleUi *u = kp_t_ui(T, 0);
			int race = (int)kp_json_int(kp_json_at(c, 2), 0);
			int32_t d;
			const KpUiControl *ctl;
			kp_ui_font_set(u->ui, 1);
			theme(T, u->ui, race);
			kp_t_ok(T, kp_ui_load_console(u->ui, race) == 0, "ui: no console art");
			d = kp_ui_create_menu_button(u->ui);
			kp_t_ok(T, d >= 0, "%s", u->ui->error);
			ctl = &u->ui->controls[d];
			kp_t_eq(T, ctl->left, kp_json_int(kp_json_at(c, 5), -1), "left");
			kp_t_eq(T, ctl->top, kp_json_int(kp_json_at(c, 6), -1), "top");
			kp_t_eq(T, ctl->right, kp_json_int(kp_json_at(c, 7), -1), "right");
			kp_t_eq(T, ctl->bottom, kp_json_int(kp_json_at(c, 8), -1), "bottom");
			kp_ui_paint_dialog(u->ui, d);
			if (dialog_crc(u->ui, d) != (uint32_t)kp_json_int(kp_json_at(c, 9), -1)) {
				kp_t_fail(T, "%s frame %lld: %s differs from the original's", kp_json_str(kp_json_at(c, 0), ""),
					(long long)kp_json_int(kp_json_at(c, 1), 0), kp_json_str(kp_json_at(c, 3), ""));
			}
			checked++;
		}
		kp_t_release(T, mark);
	}
	kp_t_ok(T, checked > 0, NULL);
}

/* --- the console over a replayed game (kp_oracle_ui.h's hud replay over tests/golden/oracle/ui/hud_*.json) --- */

typedef struct {
	KpT *T;
	const KpJson *golden;
	const char *name;
	int checked;
	void (*on_frame)(void *ud, KpHudReplay *r, int64_t frame);
	void *ud;
} Hud;

static void free_replay(void *p)
{
	KpHudReplay *r = (KpHudReplay *)p;
	if (r->game.g || r->ui.ui) kp_hud_replay_free(r);
	free(r);
}

static void free_log(void *p)
{
	kp_hud_log_free((KpHudLog *)p);
	free(p);
}

/* A console replay of the run name (its hud golden and its scenario golden) to the frames, input taken. */
static int hud_glyph_log;     /* hud_run's interfaces record the vector text's glyph log */
static size_t hud_glyphs;     /* the glyphs they recorded */
static int hud_image_log;     /* and the HD interface's image log */
static int32_t hud_images;    /* the images they hold at the end */

static void hud_run(KpT *T, const char *name, const int64_t *frames, int count, const KpHudObservers *obs)
{
	char path[512];
	KpHudLog *log = (KpHudLog *)calloc(1, sizeof *log);
	KpHudReplay *r = (KpHudReplay *)calloc(1, sizeof *r);
	KpHudReplayOpts *opts = (KpHudReplayOpts *)calloc(1, sizeof *opts);
	KpError err;
	kp_t_ok(T, log && r && opts, "out of memory");
	kp_t_defer(T, free, opts);
	snprintf(path, sizeof path, "tests/golden/oracle/ui/hud_%s.json", name);
	kp_hud_log_from_golden(log, kp_t_golden(T, path));
	kp_t_defer(T, free_log, log);
	snprintf(path, sizeof path, "tests/golden/oracle/scenarios/%s.json", name);
	opts->golden = kp_t_golden(T, path);
	opts->hud = log;
	opts->frames = frames;
	opts->frame_count = count;
	opts->input = 1;
	if (kp_hud_replay_start(r, ".", opts, obs, &err) != 0) {
		free(r);
		kp_t_fail(T, "%s", err.msg);
	}
	kp_t_defer(T, free_replay, r);
	kp_t_guard(T, r->game.g);
	if (hud_glyph_log) kp_ui_set_vector_text(r->ui.ui, 1, 0, NULL);
	if (hud_image_log) kp_ui_set_hd(r->ui.ui, 1);
	kp_t_ok(T, kp_hud_replay_run(r, &err) == 0, "%s", err.msg);
	hud_glyphs += r->ui.ui->glyph_log.count;
	hud_images += r->ui.ui->image_log.count;
}

typedef struct {
	KpT *T;
	const KpJson *panels;
	const char *name;
	int checked;
} Panels;

static void panels_frame(void *ud, KpHudReplay *r, int64_t frame)
{
	Panels *p = (Panels *)ud;
	KpT *T = p->T;
	KpUi *ui = r->ui.ui;
	int32_t d;
	int k;
	for (k = 0; k < kp_json_len(p->panels); k++) { /* the tooltip of the frame */
		const KpJson *tip = kp_json_at(p->panels, k);
		if (kp_json_int(kp_json_at(tip, 0), -1) != frame || strcmp(kp_json_str(kp_json_at(tip, 1), ""), "tooltip") != 0) continue;
		{
			int32_t x = -1, y = -1, w = 0, h = 0, row;
			const KpUiSurface *t = kp_ui_tooltip(ui, &x, &y, &w, &h);
			uint32_t crc = 0;
			int shown = t && w > 0 && h > 0 && w <= t->w && h <= t->h;
			if (!shown) x = y = -1, w = h = 0;
			kp_t_eq(T, x, kp_json_int(kp_json_at(tip, 2), -2), "the tooltip's left");
			kp_t_eq(T, y, kp_json_int(kp_json_at(tip, 3), -2), "its top");
			kp_t_eq(T, x + w - 1, kp_json_int(kp_json_at(tip, 4), -2), "its right");
			kp_t_eq(T, y + h - 1, kp_json_int(kp_json_at(tip, 5), -2), "its bottom");
			for (row = 0; shown && row < h; row++) crc = kp_crc32_update(crc, t->pixels + (size_t)row * (size_t)t->w, (size_t)w);
			if (crc != (uint32_t)kp_json_int(kp_json_at(tip, 6), -1)) {
				kp_t_fail(T, "%s frame %lld: the tooltip differs from the original's", p->name, (long long)frame);
			}
			p->checked++;
		}
	}
	for (d = ui->open; d >= 0; d = ui->controls[d].next) {
		const KpUiControl *c = &ui->controls[d];
		char name[128];
		if (!(c->bin >= 0 && c->bin < ui->bin_count)) continue;
		snprintf(name, sizeof name, "%.*s", (int)ui->bins[c->bin].name_len, ui->bins[c->bin].name);
		for (k = 0; k < kp_json_len(p->panels); k++) {
			const KpJson *w = kp_json_at(p->panels, k);
			if (kp_json_int(kp_json_at(w, 0), -1) != frame || strcmp(kp_json_str(kp_json_at(w, 1), ""), name) != 0) continue;
			kp_t_eq(T, c->left, kp_json_int(kp_json_at(w, 2), -1), "%s's left", name);
			kp_t_eq(T, c->top, kp_json_int(kp_json_at(w, 3), -1), "%s's top", name);
			kp_t_eq(T, c->right, kp_json_int(kp_json_at(w, 4), -1), "%s's right", name);
			kp_t_eq(T, c->bottom, kp_json_int(kp_json_at(w, 5), -1), "%s's bottom", name);
			if (dialog_crc(ui, d) != (uint32_t)kp_json_int(kp_json_at(w, 6), -1)) {
				kp_t_fail(T, "%s frame %lld: %s differs from the original's", p->name, (long long)frame, name);
			}
			p->checked++;
			break;
		}
	}
}

/* The console over a replayed game: the golden twin of tools/ui_check.py, a few short runs whose console dialogs
 * equal the original's crops. */
static void hud_panels(KpT *T)
{
	static const char *const RUNS[] = { "train_terran", "zerg_morph", "protoss_build", "tooltips" };
	size_t r;
	int checked = 0;
	if (!kp_t_ready(T, "test_kp_ui.hud_panels_equal_the_original", UI_FILES)) return;
	for (r = 0; r < sizeof RUNS / sizeof RUNS[0]; r++) {
		char path[256];
		const KpJson *panels;
		int64_t frames[512];
		int count = 0, k, i, mark = kp_t_mark(T);
		KpHudObservers obs;
		Panels p;
		snprintf(path, sizeof path, "tests/golden/oracle/ui/hud_%s.json", RUNS[r]);
		if (!kp_t_has(T, path)) continue;
		kp_t_data_117(T); /* the captures are of 1.17's console and texts */
		panels = kp_json_get(kp_t_golden(T, path), "panels");
		for (k = 0; k < kp_json_len(panels); k++) { /* the frames, in order */
			int64_t f = kp_json_int(kp_json_at(kp_json_at(panels, k), 0), 0);
			for (i = 0; i < count && frames[i] != f; i++) {
			}
			if (i == count && count < 512) frames[count++] = f;
		}
		for (k = 1; k < count; k++) { /* sorted (insertion) */
			int64_t f = frames[k];
			for (i = k; i > 0 && frames[i - 1] > f; i--) frames[i] = frames[i - 1];
			frames[i] = f;
		}
		memset(&obs, 0, sizeof obs);
		p.T = T;
		p.panels = panels;
		p.name = RUNS[r];
		p.checked = 0;
		obs.on_frame = panels_frame;
		obs.ud = &p;
		hud_run(T, RUNS[r], frames, count, &obs);
		kp_t_eq(T, p.checked, kp_json_len(panels), "%s: every captured panel is one Koprulu draws", RUNS[r]);
		checked += p.checked;
		kp_t_release(T, mark);
	}
	if (checked == 0) kp_t_log(T, "SKIP test_kp_ui.hud_panels_equal_the_original: no hud goldens");
}

static void hud_panels_equal_the_original(KpT *T) { hud_panels(T); }

/* The vector text's glyph log (ADR 0011, 2026-10-26) changes no pixel of the console: its panels still equal the
 * original's with the log recording, and it recorded their glyphs. */
static void hud_glyph_log_off(void *unused)
{
	(void)unused;
	hud_glyph_log = 0;
}

static void hud_panels_hold_with_the_glyph_log(KpT *T)
{
	hud_glyph_log = 1;
	kp_t_defer(T, hud_glyph_log_off, NULL); /* off when a check fails */
	hud_glyphs = 0;
	hud_panels(T);
	hud_glyph_log = 0;
	if (kp_t_has(T, "tests/golden/oracle/ui/hud_train_terran.json")) kp_t_ok(T, hud_glyphs > 0, "no glyph recorded");
}

static void hud_image_log_off(void *unused)
{
	(void)unused;
	hud_image_log = 0;
}

/* The console's panels equal the original's with the HD interface's image log recording (it only watches the
 * drawing), and it holds images. */
static void hud_panels_hold_with_the_image_log(KpT *T)
{
	hud_image_log = 1;
	kp_t_defer(T, hud_image_log_off, NULL);
	hud_images = 0;
	hud_panels(T);
	hud_image_log = 0;
	if (kp_t_has(T, "tests/golden/oracle/ui/hud_train_terran.json")) kp_t_ok(T, hud_images > 0, "no image recorded");
}

typedef struct {
	KpT *T;
	int checked;
} Check;

static void keys_frame(void *ud, KpHudReplay *r, int64_t frame)
{
	Check *c = (Check *)ud;
	KpT *T = c->T;
	KpUi *ui = r->ui.ui;
	uint8_t first[KP_UI_COMMAND_BYTES];
	size_t size;
	int64_t speed = kp_scalar(r->game.g, KP_SPEED);
	int got;
	(void)frame;
	kp_t_ui_take_commands(ui, NULL, &size);
	kp_ui_window_char(ui, 45, 0);
	got = kp_t_ui_take_commands(ui, first, &size);
	kp_t_eq(T, got, speed > 0 ? 1 : 0, NULL);
	if (speed > 0) {
		kp_t_eq(T, (int64_t)size, 2, NULL);
		kp_t_eq(T, first[0], 0x0f, NULL);
		kp_t_eq(T, first[1], speed - 1, NULL);
	}
	kp_t_eq(T, kp_ui_window_key(ui, 1, 0x11, 0), 1, NULL);
	kp_t_eq(T, kp_ui_window_key(ui, 1, 0x31, 0), 0, "Ctrl+1: the accelerator's key has no WM_CHAR");
	kp_ui_window_key(ui, 0, 0x31, 0);
	kp_ui_window_key(ui, 0, 0x11, 0);
	kp_t_ui_take_commands(ui, first, &size);
	kp_t_eq(T, (int64_t)size, 3, NULL);
	kp_t_ok(T, first[0] == 0x13 && first[1] == 0 && first[2] == 1, "the control group's command");
	c->checked = 1;
}

/* The window's keyboard goes through the accelerator tables (TranslateAcceleratorA, ACCL): '-' is the game speed's
 * command (40058 -> {0x0f, speed - 1}), Ctrl+1 assigns control group 1 ({0x13, 0, 1}), a plain key is a key event. */
static void keys_translate_through_the_accelerators(KpT *T)
{
	static const int64_t FRAMES[1] = { 20 };
	KpHudObservers obs;
	Check c;
	if (!kp_t_ready(T, "test_kp_ui.keys_translate_through_the_accelerators", UI_FILES)) return;
	if (!kp_t_has(T, "tests/golden/oracle/ui/hud_tooltips.json")) return;
	memset(&obs, 0, sizeof obs);
	c.T = T;
	c.checked = 0;
	obs.on_frame = keys_frame;
	obs.ud = &c;
	hud_run(T, "tooltips", FRAMES, 1, &obs);
	kp_t_eq(T, c.checked, 1, NULL);
}

/* KP_EVENT_SELECTION_SENT's fields of a selection: four 11-bit units a field. */
static void pack(const int64_t *units, int n, int64_t *f)
{
	int k;
	f[0] = f[1] = f[2] = 0;
	for (k = 0; k < n; k++) f[k / 4] += units[k] << (11 * (k % 4));
}

static void deselections_frame(void *ud, KpHudReplay *r, int64_t frame)
{
	Check *c = (Check *)ud;
	KpT *T = c->T;
	KpGame *g = r->game.g;
	KpUi *ui = r->ui.ui;
	int64_t own[KP_UNITS], f[3], n;
	int count = 0;
	uint8_t first[KP_UI_COMMAND_BYTES];
	size_t size;
	(void)frame;
	for (n = 1; n <= KP_UNITS; n++) {
		if (g->u.hp[n] > 0 && g->u.owner[n] == 0 && (g->u.type[n] == 106 || g->u.type[n] == 7 || g->u.type[n] == 0)) own[count++] = n;
	}
	kp_t_eq(T, count, 3, NULL);
	kp_t_ui_take_commands(ui, NULL, &size);
	pack(own, 3, f);
	kp_ui_game_event(ui, KP_EVENT_SELECTION_SENT, f[0], f[1], f[2], NULL);
	kp_t_eq(T, kp_t_ui_take_commands(ui, first, &size), 1, NULL);
	kp_t_eq(T, first[0], 0x09, "three new units: one select command");
	kp_t_eq(T, first[1], 3, NULL);
	pack(own, 2, f);
	kp_ui_game_event(ui, KP_EVENT_SELECTION_SENT, f[0], f[1], f[2], NULL);
	kp_t_eq(T, kp_t_ui_take_commands(ui, first, &size), 1, NULL);
	kp_t_eq(T, first[0], 0x0b, "one of three out: a remove command");
	kp_t_eq(T, (int64_t)size, 4, NULL);
	kp_ui_game_event(ui, KP_EVENT_SELECTION_GONE, own[1], 0, 0, NULL);
	pack(own, 1, f);
	kp_ui_game_event(ui, KP_EVENT_SELECTION_SENT, f[0], f[1], f[2], NULL);
	kp_t_eq(T, kp_t_ui_take_commands(ui, first, &size), 0, "the mirror lost the dead unit already");
	c->checked = 1;
}

/* The game's deselections reach the network as the interface's selection commands (SendSelectionCommands 0x474130):
 * KP_EVENT_SELECTION_SENT carries the selection the call left, each event its own command; KP_EVENT_SELECTION_GONE
 * takes a dead unit out of the network's mirror (0x472100), with no command of its own. */
static void game_deselections_send_selection_commands(KpT *T)
{
	static const int64_t FRAMES[1] = { 20 };
	KpHudObservers obs;
	Check c;
	if (!kp_t_ready(T, "test_kp_ui.game_deselections_send_selection_commands", UI_FILES)) return;
	if (!kp_t_has(T, "tests/golden/oracle/ui/hud_tooltips.json")) return;
	memset(&obs, 0, sizeof obs);
	c.T = T;
	c.checked = 0;
	obs.on_frame = deselections_frame;
	obs.ud = &c;
	hud_run(T, "tooltips", FRAMES, 1, &obs);
	kp_t_eq(T, c.checked, 1, NULL);
}

/* --- the interface's dialogs over native games --- */

#define KEY_DOWN 0
#define KEY_UP 2
#define LEFT_DOWN 4
#define LEFT_UP 5
#define RIGHT_DOWN 7
#define IDLE 13
#define CHAR 15
#define COMMAND 16
#define VICTORY 3
#define DEFEAT 2

static KpOracleGame *terran01(KpT *T) { return kp_t_game(T, TERRAN01, 0, 0, NULL); }

/* The result dialogs (kp_ui_result.c, atlas P41): the victory's OK (its hotkey V) leaves exit code 6; the defeat's
 * 20-second timer without an answer leaves 7 and destroys the dialog; EndGame closes the modeless dialog. */
static void result_dialogs_leave_exit_codes(KpT *T)
{
	KpOracleUi *u;
	int32_t d;
	char name[128];
	if (!kp_t_ready(T, "test_kp_ui.result_dialogs_leave_exit_codes", UI_FILES)) return;
	u = kp_t_ui(T, 0);
	kp_ui_font_set(u->ui, 1);
	theme(T, u->ui, 1);
	u->ui->now = 1000;
	d = kp_ui_show_result(u->ui, VICTORY);
	kp_t_ok(T, d >= 0, NULL);
	kp_t_eq(T, u->ui->modeless, d, NULL);
	kp_t_eq(T, kp_ui_show_result(u->ui, VICTORY), -1, "once");
	kp_t_eq_str(T, kp_t_ui_bin(u->ui, d, name, sizeof name), "rez\\wmission.bin", NULL);
	kp_t_ui_input(u->ui, KEY_DOWN, 0x56, 0);
	kp_t_ui_input(u->ui, CHAR, 0x76, 0);
	kp_t_ui_input(u->ui, KEY_UP, 0x56, 0);
	u->ui->now = 1100;
	kp_t_ui_input(u->ui, IDLE, 320, 240); /* the scheduled press releases */
	kp_t_eq(T, u->ui->game_exit, 6, NULL);
	kp_ui_end_game(u->ui);
	kp_t_eq(T, u->ui->modeless, -1, NULL);

	u = kp_t_ui(T, 0);
	kp_ui_font_set(u->ui, 1);
	theme(T, u->ui, 0);
	u->ui->now = 1000;
	d = kp_ui_show_result(u->ui, DEFEAT);
	kp_t_eq_str(T, kp_t_ui_bin(u->ui, d, name, sizeof name), "rez\\lmission.bin", NULL);
	kp_ui_tick(u->ui, 21000); /* exactly 20 s: not yet late */
	kp_t_eq(T, u->ui->game_exit, 0, NULL);
	kp_ui_tick(u->ui, 21001);
	kp_t_eq(T, u->ui->game_exit, 7, NULL);
	kp_t_eq(T, u->ui->modeless, -1, NULL);
}

typedef struct {
	KpUi *ui;
	uint32_t now;
} Clock;

static void frames(Clock *c, int n)
{
	int k;
	for (k = 0; k < n; k++) {
		c->now += 16;
		c->ui->now = c->now;
		kp_ui_tick(c->ui, c->now);
		kp_t_ui_idle(c->ui);
		kp_ui_present(c->ui);
	}
}

static void hud(KpT *T, KpUi *ui, KpGame *g)
{
	kp_t_ok(T, kp_ui_hud_create(ui, g, NULL) == 0, "ui: the console: %s", ui->error);
}

static void score_game(KpT *T, KpUi *ui, KpGame *g, int result, uint32_t elapsed)
{
	KpUiScoreData d;
	(void)T;
	kp_ui_score_data(ui, g, result, "Player", elapsed, &d);
	kp_ui_score_open(ui, &d);
}

/* Only a layout host's -1 is a panel's point: a host that never answers (the UI oracle's) keeps the original's console
 * art, and its -1 is no panel (the placement's preview follows its pointer as the original's). */
static void panel_point_is_the_layouts(KpT *T)
{
	KpUi *ui = (KpUi *)calloc(1, sizeof *ui);
	kp_t_ok(T, ui != NULL, "out of memory");
	kp_t_defer(T, free, ui);
	ui->input.over_console = -1;
	kp_t_ok(T, !kp_ui_on_panel(ui), "no layout: no panel");
	kp_ui_set_over_console(ui, -1);
	kp_t_ok(T, kp_ui_on_panel(ui), "the layout's panel");
	kp_ui_set_over_console(ui, 1);
	kp_t_ok(T, !kp_ui_on_panel(ui), "the console's art on none of its panels");
	kp_ui_set_over_console(ui, 0);
	kp_t_ok(T, !kp_ui_on_panel(ui), "the game view");
}

/* A console that cannot be made (no console art) leaves no live mark and no game input procedures: a ready room's
 * capture would install them later for a game its Cancel ends (kp_ui_install_game_input refuses without the mark). */
static void failed_console_leaves_no_game_input(KpT *T)
{
	KpOracleUi *u;
	KpOracleGame *game;
	int count;
	if (!kp_t_ready(T, "test_kp_ui.failed_console_leaves_no_game_input", GAME_FILES)) return;
	u = kp_t_ui(T, KP_ORACLE_UI_STRINGS);
	kp_ui_font_set(u->ui, 1);
	theme(T, u->ui, 1);
	game = terran01(T);
	count = u->ui->pcx_count;
	u->ui->pcx_count = 0; /* the console's PCX gone */
	kp_t_ok(T, kp_ui_hud_create(u->ui, game->g, NULL) != 0, "no console");
	u->ui->pcx_count = count;
	kp_t_eq(T, u->ui->hud.live, 0, "no live mark");
	kp_t_ok(T, u->ui->input.proc[KP_UI_IDLE] == NULL, "no game procedure left");
	kp_ui_install_game_input(u->ui);
	kp_t_ok(T, u->ui->input.proc[KP_UI_IDLE] == NULL, "none installed for a game without its console");
	(void)kp_ui_hud_create(u->ui, NULL, NULL); /* the game detached */
}

/* One interface runs the glue's games one after another (the product): a new game's console forgets the last game's
 * exit code, its result dialog's answer and its paused seconds, and the score's seconds leave the paused ones out. */
static void next_game_forgets_the_last_result(KpT *T)
{
	KpOracleUi *u;
	KpOracleGame *game;
	Clock c;
	if (!kp_t_ready(T, "test_kp_ui.next_game_forgets_the_last_result", GAME_FILES)) return;
	u = kp_t_ui(T, KP_ORACLE_UI_STRINGS);
	kp_ui_font_set(u->ui, 1);
	theme(T, u->ui, 1);
	c.ui = u->ui;
	c.now = 1000;
	u->ui->now = c.now;
	game = terran01(T);
	hud(T, u->ui, game->g);
	kp_ui_window_key(u->ui, 1, 0x79, 0); /* F10: the game menu pauses the game */
	kp_ui_window_key(u->ui, 0, 0x79, 0);
	frames(&c, 10);
	kp_t_ok(T, u->ui->modeless >= 0, "the game menu");
	c.now += 5000;
	kp_ui_window_key(u->ui, 1, 0x1b, 0); /* Escape: back to the game 5 s later */
	kp_ui_window_char(u->ui, 27, 0);
	kp_ui_window_key(u->ui, 0, 0x1b, 0);
	frames(&c, 10);
	kp_t_eq(T, u->ui->modeless, -1, NULL);
	kp_t_ok(T, kp_ui_show_result(u->ui, VICTORY) >= 0, NULL);
	kp_t_ui_input(u->ui, KEY_DOWN, 0x56, 0);
	kp_t_ui_input(u->ui, CHAR, 0x76, 0);
	kp_t_ui_input(u->ui, KEY_UP, 0x56, 0);
	frames(&c, 10);
	kp_t_eq(T, u->ui->game_exit, 6, NULL);
	kp_ui_end_game(u->ui);
	score_game(T, u->ui, game->g, VICTORY, 20);
	kp_t_eq(T, u->ui->score.data.elapsed, 15, "the 5 paused seconds left out");
	game = terran01(T);
	hud(T, u->ui, game->g);
	kp_t_eq(T, u->ui->game_exit, 0, NULL);
	kp_t_ok(T, kp_ui_show_result(u->ui, DEFEAT) >= 0, "the next game's result dialog");
	kp_ui_end_game(u->ui);
	score_game(T, u->ui, game->g, DEFEAT, 20);
	kp_t_eq(T, u->ui->score.data.elapsed, 20, "no paused seconds of the last game");
}

/* EndGame 0x443db0's ResetInputHandlers 0x43f330: the game's input procedures leave with the game, so the glue's idle
 * events and clicks after end_game reach no procedure of the freed game. */
static void end_game_resets_the_input_procedures(KpT *T)
{
	static const int64_t EVENTS[6][3] = { { IDLE, 320, 240 }, { LEFT_DOWN, 100, 100 }, { LEFT_UP, 100, 100 },
		{ RIGHT_DOWN, 100, 100 }, { CHAR, 0x61, 0 }, { COMMAND, 0, 0 } };
	KpOracleGame *game;
	KpOracleUi *u;
	size_t size;
	int k;
	if (!kp_t_ready(T, "test_kp_ui.end_game_resets_the_input_procedures", GAME_FILES)) return;
	game = terran01(T);
	u = kp_t_ui(T, KP_ORACLE_UI_STRINGS);
	kp_ui_font_set(u->ui, 1);
	theme(T, u->ui, 1);
	u->ui->now = 1000;
	hud(T, u->ui, game->g);
	kp_t_ui_input(u->ui, IDLE, 320, 240);
	kp_ui_end_game(u->ui);
	for (k = 0; k < 6; k++) kp_t_ui_input(u->ui, (int)EVENTS[k][0], EVENTS[k][1], EVENTS[k][2]);
	kp_t_eq(T, kp_t_ui_take_commands(u->ui, NULL, &size), 0, NULL);
}

/* The host's side of DeleteSaveFile 0x498b30 for the save dialog's test: the names asked, the answer given. */
typedef struct {
	int calls, result;
	char name[32];
} Deletes;

static int test_delete_save(void *host, const char *name)
{
	Deletes *d = (Deletes *)host;
	d->calls++;
	snprintf(d->name, sizeof d->name, "%s", name);
	return d->result;
}

/* A press and release at the centre of control id of the top dialog, then the frames its release takes. */
static void dialog_click(KpT *T, Clock *c, int16_t id)
{
	KpUi *ui = c->ui;
	int32_t d = ui->open, k = d >= 0 ? kp_ui_find_control(ui, d, id) : -1, x, y;
	if (k < 0) {
		kp_t_fail(T, "no control %d", id);
		return;
	}
	x = ui->controls[d].left + (ui->controls[k].left + ui->controls[k].right) / 2;
	y = ui->controls[d].top + (ui->controls[k].top + ui->controls[k].bottom) / 2;
	kp_t_ui_input(ui, 3, x, y);
	kp_t_ui_input(ui, LEFT_DOWN, x, y);
	kp_t_ui_input(ui, LEFT_UP, x, y);
	frames(c, 10);
}

/* The in-game save dialog's file that stays (ConfirmDeleteSaveCallback 0x49ae00, ConfirmOverwriteSaveCallback
 * 0x49b010): DeleteSaveFile 0x498b30 answering 0 shows an OK box with network.tbl string 1 (the delete) or 2 (the
 * overwrite) and the save's name, sends no save, keeps the name in the list and opens the save dialog again after it;
 * a delete done drops the name. */
static void save_dialog_reports_a_file_that_stays(KpT *T)
{
	enum { ID_OK = -2, ID_LIST = 1, ID_DELETE = 3 };
	KpOracleGame *game;
	KpOracleUi *u;
	KpUi *ui;
	Deletes del;
	Clock c;
	char name[128], want[256];
	size_t size;
	int pass;
	if (!kp_t_ready(T, "test_kp_ui.save_dialog_reports_a_file_that_stays", GAME_FILES)) return;
	game = terran01(T);
	u = kp_t_ui(T, KP_ORACLE_UI_STRINGS);
	ui = u->ui;
	kp_ui_font_set(ui, 1);
	theme(T, ui, 1);
	ui->now = 1000;
	hud(T, ui, game->g);
	c.ui = ui;
	c.now = 1000;
	memset(&del, 0, sizeof del);
	ui->delete_save = test_delete_save;
	ui->host = &del;
	snprintf(ui->menus.saves[0], sizeof ui->menus.saves[0], "alpha");
	snprintf(ui->menus.saves[1], sizeof ui->menus.saves[1], "beta");
	ui->menus.save_count = 2;
	kp_ui_save_dialog(ui, 0);
	frames(&c, 10);
	kp_t_eq_str(T, kp_t_ui_bin(ui, ui->open, name, sizeof name), "rez\\savegame.bin", NULL);
	(void)kp_t_ui_take_commands(ui, NULL, &size);
	for (pass = 0; pass < 2; pass++) { /* the delete, then the overwrite */
		int32_t list = kp_ui_find_control(ui, ui->open, ID_LIST);
		kp_t_ok(T, list >= 0, "the list");
		kp_ui_list_select(ui, list, 0);
		frames(&c, 2);
		dialog_click(T, &c, pass == 0 ? ID_DELETE : ID_OK);
		kp_t_eq_str(T, kp_t_ui_bin(ui, ui->open, name, sizeof name), "rez\\okcancel.bin", "asked first");
		dialog_click(T, &c, ID_OK);
		kp_t_eq(T, del.calls, pass + 1, "the host asked to delete");
		kp_t_eq_str(T, del.name, "alpha", NULL);
		kp_t_eq_str(T, kp_t_ui_bin(ui, ui->open, name, sizeof name), "rez\\ok.bin", "the error box");
		kp_ui_format_name_xy(want, sizeof want, kp_ui_tbl(ui, KP_UI_NETWORK, pass + 1), (const uint8_t *)"alpha", 0, 0);
		kp_t_ok(T, want[0] != 0, "network.tbl string %d", pass + 1);
		kp_t_eq_str(T, ui->box.text, want, "network.tbl string %d with the name", pass + 1);
		kp_t_eq(T, kp_t_ui_take_commands(ui, NULL, &size), 0, "no save sent");
		dialog_click(T, &c, ID_OK);
		kp_t_eq_str(T, kp_t_ui_bin(ui, ui->open, name, sizeof name), "rez\\savegame.bin", "the save dialog again");
		kp_t_eq(T, ui->menus.save_count, 2, "the name kept");
		kp_t_eq_str(T, ui->menus.saves[0], "alpha", NULL);
	}
	del.result = 1;
	kp_ui_list_select(ui, kp_ui_find_control(ui, ui->open, ID_LIST), 0);
	frames(&c, 2);
	dialog_click(T, &c, ID_DELETE);
	dialog_click(T, &c, ID_OK);
	kp_t_eq_str(T, kp_t_ui_bin(ui, ui->open, name, sizeof name), "rez\\savegame.bin", "a delete done: the dialog again");
	kp_t_eq(T, ui->menus.save_count, 1, "the name dropped");
	kp_t_eq_str(T, ui->menus.saves[0], "beta", NULL);
}

/* The view the interface moved to, or 0. */
static int take_camera(KpUi *ui, int32_t *x, int32_t *y)
{
	if (!ui->input.camera_moved) return 0;
	ui->input.camera_moved = 0;
	*x = ui->input.screen_x;
	*y = ui->input.screen_y;
	return 1;
}

static int64_t max64(int64_t a, int64_t b) { return a > b ? a : b; }
static int64_t min64(int64_t a, int64_t b) { return a < b ? a : b; }

/* The game's first view (HandleStartLocationRecord 0x46a280, InitScreenPosition 0x445810): the local player's start
 * location's tile less (10, 6) for the original's 640 x 400 view, less half a wider view's tiles, clamped to the map. */
static void first_view_on_the_start_location(KpT *T)
{
	KpOracleGame *og;
	KpGame *g;
	KpOracleUi *u;
	int64_t sx, sy, wx, wy;
	int32_t x = 0, y = 0;
	if (!kp_t_ready(T, "test_kp_ui.first_view_on_the_start_location", GAME_FILES)) return;
	og = terran01(T);
	g = og->g;
	u = kp_t_ui(T, KP_ORACLE_UI_STRINGS);
	u->ui->now = 1000;
	hud(T, u->ui, g);
	sx = g->players.start_x[g->local_player];
	sy = g->players.start_y[g->local_player];
	kp_ui_start_screen(u->ui);
	kp_t_ok(T, take_camera(u->ui, &x, &y), "the start's view");
	wx = min64(max64(0, sx / 32 - 10) * 32, g->map_w * 32 - 640);
	wy = min64(max64(0, sy / 32 - 6) * 32, (g->map_h - 12) * 32 + 8);
	kp_t_eq(T, x, wx - wx % 8, NULL);
	kp_t_eq(T, y, wy - wy % 8, NULL);
	kp_ui_set_view(u->ui, 1024, 496);
	kp_ui_start_screen(u->ui);
	kp_t_ok(T, take_camera(u->ui, &x, &y), "the wide view's start");
	wx = min64(max64(0, sx / 32 - 16) * 32, g->map_w * 32 - 1024);
	wy = min64(max64(0, sy / 32 - 7) * 32, (g->map_h - 12) * 32 + 8 - 96);
	kp_t_eq(T, x, wx - wx % 8, NULL);
	kp_t_eq(T, y, wy - wy % 8, NULL);
}

/* Text is UTF-8; a byte that is no sequence reads as windows-1252, a bijection of the 256 bytes (ADR 0016). */
static void text_reads_utf8_and_windows_1252_bytes(KpT *T)
{
	static const uint8_t MIXED[] = { 'a', 0xc3, 0xa9, 0x92, 0xe2, 0x82, 0xac, 0xd0, 'b', 0xf0, 0x9f, 0x98, 0x80, 0 };
	static const struct {
		int32_t cp;
		size_t past;
	} WANT[] = { { 'a', 1 }, { 0xe9, 3 }, { 0x2019, 4 }, { 0x20ac, 7 }, { 0xd0, 8 }, { 'b', 9 }, { 0x1f600, 13 } };
	uint8_t bytes[KP_TEXT_UTF8_MAX];
	char out[16], back[16];
	size_t at = 0, k;
	int b;
	for (b = 0; b < 256; b++) {
		int32_t cp = kp_text_from_byte((uint8_t)b);
		kp_t_eq(T, kp_text_byte(cp), b, "byte %d", b);
	}
	kp_t_eq(T, kp_text_from_byte(0x92), 0x2019, NULL);
	kp_t_eq(T, kp_text_from_byte(0x81), 0x81, "undefined in Windows: its C1 control");
	kp_t_eq(T, kp_text_byte(0x041b), -1, "no slot");
	kp_t_eq(T, kp_text_byte(-1), -1, NULL);
	for (k = 0; k < sizeof WANT / sizeof WANT[0]; k++) {
		kp_t_eq(T, kp_text_next(MIXED, KP_TEXT_NUL_TERMINATED, &at), WANT[k].cp, "character %d", (int)k);
		kp_t_eq(T, (int64_t)at, (int64_t)WANT[k].past, "past character %d", (int)k);
	}
	kp_t_eq(T, MIXED[at], 0, "the end");
	kp_t_eq(T, (int64_t)kp_text_chars(MIXED), 7, NULL);
	kp_t_eq(T, (int64_t)kp_text_prev(MIXED, 3), 1, "before the 2 bytes' next");
	kp_t_eq(T, (int64_t)kp_text_prev(MIXED, 7), 4, NULL);
	kp_t_eq(T, (int64_t)kp_text_prev(MIXED, 0), 0, NULL);
	kp_t_eq(T, (int64_t)kp_text_floor(MIXED, 6), 4, "inside the euro");
	kp_t_eq(T, (int64_t)kp_text_floor(MIXED, 7), 7, "a start stays");
	kp_t_eq(T, (int64_t)kp_text_copy(out, 3, "a\303\251b"), 1, "no half a character");
	kp_t_eq_str(T, out, "a", NULL);
	kp_t_eq(T, (int64_t)kp_text_copy(out, 4, "a\303\251b"), 3, NULL);
	kp_t_eq(T, (int64_t)kp_text_copy_chars(out, sizeof out, "\303\251\303\251x", 2), 4, "2 characters of 2 bytes");
	kp_t_eq_str(T, out, "\303\251\303\251", NULL);
	kp_t_eq(T, (int64_t)kp_text_copy_chars(out, sizeof out, "abc", 9), 3, "fewer than the limit");
	kp_t_eq(T, (int64_t)kp_text_copy_chars(out, 3, "a\303\251b", 9), 1, "no half a character at the buffer's end");
	kp_t_eq_str(T, out, "a", NULL);
	kp_t_eq(T, (int64_t)kp_text_copy_chars(out, sizeof out, "a\351b", 2), 2, "a stray byte is a character");
	kp_t_eq(T, (int64_t)kp_text_encode(0xe9, bytes), 2, NULL);
	kp_t_eq(T, (int64_t)kp_text_encode(0x20ac, bytes), 3, NULL);
	kp_t_eq(T, (int64_t)kp_text_encode(0x1f600, bytes), 4, NULL);
	kp_t_eq(T, (int64_t)kp_text_encode(0xd800, bytes), 0, "a surrogate");
	kp_t_eq(T, (int64_t)kp_text_from_legacy("K\351\222", out, sizeof out), 6, NULL);
	kp_t_eq_str(T, out, "K\303\251\342\200\231", NULL);
	kp_t_eq(T, (int64_t)kp_text_to_legacy(out, back, sizeof back), 3, NULL);
	kp_t_eq_str(T, back, "K\351\222", "the way back");
	kp_t_eq(T, (int64_t)kp_text_from_legacy("\351\351", out, 4), 2, "cut on a boundary");
	kp_t_eq(T, (int64_t)kp_text_to_legacy("a\320\233b", back, sizeof back), 2, "no byte: left out");
}

/* A character with no glyph has no width and its text goes on, in the drawing as in the measuring; a stray byte whose
 * slot is empty still stops the text where the original's engine did (ADR 0016). */
static void text_skips_a_character_with_no_glyph(KpT *T)
{
	KpUiText t;
	uint8_t *narrow;
	const uint8_t *f;
	size_t fn;
	int32_t a, b;
	if (!kp_t_ready(T, "test_kp_ui.text_skips_a_character_with_no_glyph", UI_FILES)) return;
	f = font(T, "font10", &fn);
	narrow = (uint8_t *)malloc(fn);
	kp_t_ok(T, narrow != NULL, "out of memory");
	kp_t_defer(T, free, narrow);
	memcpy(narrow, f, fn);
	narrow[5] = 0x7f; /* the FNT header's last code: none of the slots past ASCII */
	setup(T, &t, narrow, fn);
	a = kp_ui_char_width(&t, 'A');
	b = kp_ui_char_width(&t, 'B');
	kp_t_eq(T, kp_ui_text_width(&t, (const uint8_t *)"A\303\251B"), a + b, "the measuring");
	kp_t_eq(T, (int64_t)kp_ui_text_span(&t, (const uint8_t *)"A\303\251B", 0, 4), 4, "the drawing goes on");
	kp_t_eq(T, t.x, 5 + a + b, NULL);
	setup(T, &t, narrow, fn);
	kp_t_eq(T, (int64_t)kp_ui_text_span(&t, (const uint8_t *)"A\320\233B", 0, 4), 4, "a code point past the slots");
	kp_t_eq(T, t.x, 5 + a + b, NULL);
	setup(T, &t, narrow, fn);
	kp_t_eq(T, (int64_t)kp_ui_text_span(&t, (const uint8_t *)"A\351B", 0, 3), 1, "a stray byte stops it");
	kp_t_eq(T, t.x, 5 + a, NULL);
}

/* The score screen's roster holds what a profile's name holds: 24 characters of any width (ADR 0016). */
static void score_roster_holds_a_name_of_wide_characters(KpT *T)
{
	KpOracleUi *u;
	KpOracleGame *game;
	KpUiScoreData d;
	char name[2 * 25 + 1];
	const char *kept;
	int k;
	if (!kp_t_ready(T, "test_kp_ui.score_roster_holds_a_name_of_wide_characters", UI_FILES)) return;
	u = kp_t_ui(T, 0);
	game = terran01(T);
	hud(T, u->ui, game->g);
	for (k = 0; k < 25; k++) memcpy(name + 2 * k, "\303\251", 3);
	kp_ui_score_data(u->ui, game->g, VICTORY, name, 20, &d);
	kept = d.roster[game->g->local_player].name;
	kp_t_eq(T, (int64_t)kp_text_chars((const uint8_t *)kept), 24, "the original's 24 characters");
	kp_t_eq(T, (int64_t)strlen(kept), 48, "each whole");
}

/* A pause kp_core takes itself (a trigger's Pause Game, the result dialog's) takes PauseGame 0x47cbb0's tick: the
 * interface's UnpauseGame 0x47cc10 later discounts the seconds since it, not since an older pause or the clock's start
 * (issue #4: the score screen's elapsed time 00:00). */
static void core_pause_takes_the_pause_tick(KpT *T)
{
	KpOracleUi *u;
	KpOracleGame *game;
	KpUi *ui;
	if (!kp_t_ready(T, "test_kp_ui.core_pause_takes_the_pause_tick", UI_FILES)) return;
	u = kp_t_ui(T, 0);
	ui = u->ui;
	game = terran01(T);
	hud(T, ui, game->g);
	ui->paused_seconds = 0;
	ui->now = 100000;
	kp_scalar_set(game->g, KP_PAUSED, 1); /* TrigActPauseGame 0x4b6020 */
	kp_ui_game_event(ui, KP_EVENT_TRIGGER_PAUSE, 1, 0, 0, NULL);
	ui->now = 103500;
	kp_ui_unpause_game(ui, 1);
	kp_t_eq(T, (int64_t)ui->paused_seconds, 3, "the trigger's pause");
	ui->now = 200000;
	kp_scalar_set(game->g, KP_PAUSED, 1); /* TickResultDialogTimer 0x4be340 */
	kp_ui_game_event(ui, KP_EVENT_GAME_RESULT, VICTORY, 0, 0, NULL);
	kp_t_eq(T, (int64_t)ui->paused_seconds, 3, "the result dialog opens over no paused time");
	kp_t_eq(T, kp_scalar(game->g, KP_PAUSED), 1, "its game paused");
}

/* The canvas lists a glyph by its code point and a code point past 255 stays a code point: its byte slot says whether the
 * vector font draws it. */
static void vector_text_takes_code_points_past_a_byte(KpT *T)
{
	KpOracleUi *u;
	KpUi *ui;
	KpUiCanvas *c;
	KpUiSurface s;
	KpUiText t;
	uint8_t codes[256];
	int32_t k;
	if (!kp_t_ready(T, "test_kp_ui.vector_text_takes_code_points_past_a_byte", UI_FILES)) return;
	u = kp_t_ui(T, 0);
	ui = u->ui;
	c = vt_canvas(T, 200, 40);
	memset(codes, 1, sizeof codes);
	s = vt_surface(T, 0);
	kp_ui_set_vector_text(ui, 1, 1, codes);
	kp_ui_text_init(&t);
	kp_ui_text_select_font(&t, ui->font_files[1], ui->font_file_sizes[1]);
	kp_ui_text_set_rows(&t, ui->game_rows, ui->game_rows_size);
	t.target = &s;
	t.log = &ui->glyph_log;
	kp_ui_draw_text(&t, 4, 4, (const uint8_t *)"A\342\200\231\320\233\222B"); /* A, U+2019, U+041B, byte 0x92, B */
	kp_t_eq(T, (int64_t)ui->glyph_log.count, 4, "U+041B has no glyph yet");
	kp_t_eq(T, ui->glyph_log.glyphs[1].code, 0x2019, "the log keeps the code point");
	kp_t_eq(T, ui->glyph_log.glyphs[2].code, 0x2019, "a stray byte reads as the same");
	kp_ui_canvas_begin(ui, c);
	kp_ui_canvas_layer(ui, c, &s, 10, 5);
	kp_ui_canvas_text_end(ui, c);
	kp_ui_canvas_end(c);
	kp_t_eq(T, c->glyph_count, 4, NULL);
	kp_t_eq(T, c->glyphs[1].code, 0x2019, NULL);
	codes[0x92] = 0;
	kp_ui_set_vector_text(ui, 1, 1, codes);
	kp_ui_draw_text(&t, 4, 4, (const uint8_t *)"A\342\200\231");
	kp_ui_canvas_begin(ui, c);
	kp_ui_canvas_layer(ui, c, &s, 10, 5);
	kp_ui_canvas_text_end(ui, c);
	kp_ui_canvas_end(c);
	for (k = 0; k < c->glyph_count; k++) kp_t_ok(T, c->glyphs[k].code != 0x2019, "the slot the vector font lacks stays a bitmap");
	kp_ui_set_vector_text(ui, 0, 0, NULL);
}

/* The original's bytes and their UTF-8 take the same glyphs; a code point in no font has no width and the text goes on
 * (a byte slot outside the font still ends a drawn text, as the original's). */
static void text_finds_glyphs_by_code_point(KpT *T)
{
	size_t fn;
	const uint8_t *f;
	KpUiText t;
	int32_t a, b, e_acute, quote;
	if (!kp_t_ready(T, "test_kp_ui.text_finds_glyphs_by_code_point", UI_FILES)) return;
	f = font(T, "font10", &fn);
	setup(T, &t, f, fn);
	a = kp_ui_char_width(&t, 'A');
	b = kp_ui_char_width(&t, 'B');
	e_acute = kp_ui_char_width(&t, 0xe9);
	quote = kp_ui_char_width(&t, 0x92);
	kp_t_ok(T, a > 0 && e_acute > 0 && quote > 0, "the font has them");
	kp_t_eq(T, kp_ui_text_width(&t, (const uint8_t *)"\303\251"), e_acute, "UTF-8");
	kp_t_eq(T, kp_ui_text_width(&t, (const uint8_t *)"\351"), e_acute, "the byte");
	kp_t_eq(T, kp_ui_text_width(&t, (const uint8_t *)"\342\200\231"), quote, "UTF-8 of byte 0x92");
	kp_t_eq(T, kp_ui_text_width(&t, (const uint8_t *)"\222"), quote, "byte 0x92");
	kp_t_eq(T, kp_ui_text_width(&t, (const uint8_t *)"A\320\233B"), a + b, "no glyph anywhere: no width");
	kp_t_ok(T, kp_ui_has_glyph(&t, 0xe9) && kp_ui_has_glyph(&t, 'A'), NULL);
	kp_t_ok(T, !kp_ui_has_glyph(&t, 0x041b) && !kp_ui_has_glyph(&t, 0x1b), NULL);
	kp_t_eq(T, (int64_t)kp_ui_text_fit_length(&t, (const uint8_t *)"A\303\251B", a + e_acute), 3, "whole characters");
	kp_t_eq(T, (int64_t)kp_ui_text_fit_length(&t, (const uint8_t *)"A\303\251B", a + e_acute - 1), 1, NULL);
	t.x = 0;
	t.clip_left = 0;
	t.clip_right = 32767;
	kp_t_eq(T, (int64_t)kp_ui_find_line_break(&t, (const uint8_t *)"\303\251A", 0), 3, "the whole line");
	kp_t_eq(T, t.x, e_acute + a, "its width");
	t.x = 0;
	t.wrap = 0;
	kp_t_eq(T, (int64_t)kp_ui_find_line_break(&t, (const uint8_t *)"\303\251A", 0), 3, "unwrapped too");
	kp_t_eq(T, t.x, e_acute + a, NULL);
}

/* IsInvalidFileName 0x408300 (the new ID's and the save's names): control bytes, network.tbl string 17's characters,
 * an empty name and the DOS device names before the last dot. */
static void file_names_the_game_refuses(KpT *T)
{
	static const struct {
		const char *name;
		int invalid;
	} CASES[] = {
		{ "Kop", 0 }, { "my save 1", 0 }, { "console", 0 }, { "com10", 0 }, { "lpt", 0 }, { "con", 1 }, { "CON", 1 },
		{ "con.txt", 1 }, { "nul.a.b", 0 }, { "clock$", 1 }, { "a*b", 1 }, { "a/b", 1 }, { "a?", 1 }, { "", 1 },
		{ "a\1b", 1 }, { "c:name", 0 },
	};
	KpOracleUi *u;
	size_t k;
	if (!kp_t_ready(T, "test_kp_ui.file_names_the_game_refuses", UI_FILES)) return;
	u = kp_t_ui(T, KP_ORACLE_UI_STRINGS);
	for (k = 0; k < sizeof CASES / sizeof CASES[0]; k++) {
		kp_t_eq(T, kp_ui_invalid_file_name(u->ui, CASES[k].name, 28, 0) != 0, CASES[k].invalid, "%s", CASES[k].name);
	}
	kp_t_eq(T, kp_ui_invalid_file_name(u->ui, "dir/con", 28, 1) != 0, 1, NULL);
	kp_t_eq(T, kp_ui_invalid_file_name(u->ui, "dir/name", 28, 1) != 0, 0, NULL);
	kp_t_eq(T, kp_ui_invalid_file_name(u->ui, "abcdefghij", 5, 0) != 0, 1, "not terminated within the limit");
	kp_t_eq(T, kp_ui_invalid_file_name(u->ui, "\320\233\320\233\320\233\320\233", 5, 0) != 0, 0, "the limit counts characters");
	kp_t_eq(T, kp_ui_invalid_file_name(u->ui, "\320\233\320\233\320\233\320\233\320\233", 5, 0) != 0, 1, NULL);
}

/* A wider layout's game view (kp_ui_set_view, plan section 2a): the pointer over a unit right of the original's 640
 * columns chooses the unit's cursor (GetPointerCursorId 0x43e6d0's view rectangle), which the original's 640 x 400 view
 * leaves to the arrow. */
static void view_widens_the_game_area(KpT *T)
{
	KpOracleGame *og;
	KpGame *g;
	KpOracleUi *u;
	int64_t px = -1, py = -1, x, y, sx, sy, lx, ly;
	int k;
	if (!kp_t_ready(T, "test_kp_ui.view_widens_the_game_area", GAME_FILES)) return;
	og = terran01(T);
	g = og->g;
	u = kp_t_ui(T, KP_ORACLE_UI_STRINGS);
	kp_ui_font_set(u->ui, 1);
	theme(T, u->ui, 1);
	u->ui->now = 1000;
	hud(T, u->ui, g);
	for (y = 64; y <= 64 * 32 && px < 0; y += 16) { /* a map point of a unit the local player owns */
		for (x = 700; x <= 64 * 32; x += 16) {
			int64_t n = kp_ui_pick_unit(u->ui, (int32_t)x, (int32_t)y);
			if (n != 0 && g->u.owner[n] == g->local_player) {
				px = x;
				py = y;
				break;
			}
		}
	}
	kp_t_ok(T, px >= 0, "a unit of the local player right of x 700");
	sx = px - 800;
	sy = max64(0, py - 200);
	lx = px - sx;
	ly = py - sy;
	for (k = 0; k < 2; k++) {
		if (k == 1) kp_ui_set_view(u->ui, 1024, 400);
		kp_ui_set_screen(u->ui, (int32_t)sx, (int32_t)sy);
		kp_ui_set_over_console(u->ui, 0);
		kp_t_ui_input(u->ui, 3, lx, ly); /* the mouse moves over the unit */
		kp_ui_cursor_update(u->ui, (int32_t)(lx * 640 / 1024), (int32_t)ly);
		kp_t_eq(T, u->ui->cursor, k == 0 ? 0 : 6, k == 0 ? "the original's view ends at x 639"
			: "the local player's unit (GetUnitHoverCursorId 0x43ea90)");
	}
}

/* A loaded save (kp_load) replaces the selection under the console: the console takes the loaded game's (its status
 * unit), and the network's mirror is the loaded game's row (the selection commands go by it). */
static void loaded_save_refreshes_the_console(KpT *T)
{
	KpOracleGame *og;
	KpGame *g;
	KpOracleUi *u;
	int64_t own[KP_UNITS], n, ref;
	uint8_t *bytes, *row;
	size_t size, row_size;
	KpError err;
	int count = 0, k;
	if (!kp_t_ready(T, "test_kp_ui.loaded_save_refreshes_the_console", GAME_FILES)) return;
	og = terran01(T);
	g = og->g;
	u = kp_t_ui(T, KP_ORACLE_UI_STRINGS);
	kp_ui_font_set(u->ui, 1);
	theme(T, u->ui, 1);
	u->ui->now = 1000;
	hud(T, u->ui, g);
	for (n = 1; n <= KP_UNITS; n++) {
		if (g->u.owner[n] == g->local_player && g->u.type[n] == 0) own[count++] = n;
	}
	kp_t_ok(T, count >= 2, "two Marines of the local player");
	kp_select_locally(g, &own[0], 1);
	kp_ui_hud_frame(u->ui);
	kp_t_eq(T, u->ui->hud.status_unit, own[0], NULL);
	kp_t_ok(T, kp_save(g, og->name, og->identity, &bytes, &size, &err) == 0, "%s", err.msg);
	kp_t_defer(T, free, bytes);
	kp_select_locally(g, &own[1], 1);
	kp_ui_hud_frame(u->ui);
	kp_t_eq(T, u->ui->hud.status_unit, own[1], NULL);
	ref = kp_unit_ref(g, own[0]);
	kp_command_select(g, g->local_player, &ref, 1); /* the network's row the save keeps */
	kp_t_ok(T, kp_save(g, og->name, og->identity, &row, &row_size, &err) == 0, "%s", err.msg);
	kp_t_defer(T, free, row);
	ref = kp_unit_ref(g, own[1]);
	kp_command_select(g, g->local_player, &ref, 1);
	kp_t_ok(T, kp_load(g, bytes, size, og->name, og->identity, &err) == 0, "%s", err.msg);
	kp_ui_hud_frame(u->ui);
	kp_t_eq(T, u->ui->hud.status_unit, own[0], "the loaded game's selection");
	kp_t_ok(T, kp_load(g, row, row_size, og->name, og->identity, &err) == 0, "%s", err.msg);
	kp_ui_hud_frame(u->ui);
	kp_t_ok(T, u->ui->input.mirror[0] != 0, "the network's mirror holds the loaded row");
	for (k = 0; k < 12; k++) kp_t_eq(T, u->ui->input.mirror[k], g->selections[g->local_player][k], "the mirror's slot %d", k + 1);
}

/* A wider view centres a unit at its own centre (CenterScreenOnUnit 0x422b10 by tiles: 10, 6 of the original's
 * 640 x 400; 20, 10 of 1280 x 640). */
static void wide_view_centres_the_status_unit(KpT *T)
{
	KpOracleGame *og;
	KpGame *g;
	KpOracleUi *u;
	int64_t n, unit = 0;
	int32_t x = 0, y = 0;
	if (!kp_t_ready(T, "test_kp_ui.wide_view_centres_the_status_unit", GAME_FILES)) return;
	og = terran01(T);
	g = og->g;
	u = kp_t_ui(T, KP_ORACLE_UI_STRINGS);
	kp_ui_font_set(u->ui, 1);
	theme(T, u->ui, 1);
	u->ui->now = 1000;
	hud(T, u->ui, g);
	kp_ui_set_view(u->ui, 1280, 640);
	for (n = 1; n <= KP_UNITS; n++) { /* a unit whose centred view stays inside the map's limits */
		int64_t tx = g->u.x[n] / 32, ty = g->u.y[n] / 32;
		if (g->u.type[n] < 228 && tx >= 21 && tx <= 43 && ty >= 11 && ty <= 50) {
			unit = n;
			break;
		}
	}
	kp_t_ok(T, unit != 0, "a unit away from the map's edges");
	kp_select_locally(g, &unit, 1);
	kp_ui_hud_frame(u->ui);
	kp_ui_set_screen(u->ui, 0, 0);
	kp_t_ui_input(u->ui, COMMAND, 40036, 0);
	kp_t_ok(T, take_camera(u->ui, &x, &y), "the view moved");
	kp_t_eq(T, x, (g->u.x[unit] / 32 - 20) * 32, NULL);
	kp_t_eq(T, y, (g->u.y[unit] / 32 - 10) * 32, NULL);
}

/* "Show Tips at Startup" opens the tips at a new game's start (RunGame 0x445310), not at a saved game's
 * (WasLoadedFromSave 0x499870), and the mark goes with that console (ClearLoadedFromSave 0x499880). */
static const char *console_tips(KpT *T, KpOracleUi **out, int from_save, char *name, size_t size)
{
	KpOracleGame *game = terran01(T);
	KpOracleUi *u = kp_t_ui(T, KP_ORACLE_UI_STRINGS);
	kp_ui_font_set(u->ui, 1);
	theme(T, u->ui, 1);
	u->ui->now = 1000;
	u->ui->options |= 0x100;
	if (from_save) u->ui->from_save = 1;
	hud(T, u->ui, game->g);
	*out = u;
	return u->ui->modeless >= 0 ? kp_t_ui_bin(u->ui, u->ui->modeless, name, size) : "";
}

static void tips_wait_for_a_new_game(KpT *T)
{
	KpOracleUi *u;
	char name[128];
	if (!kp_t_ready(T, "test_kp_ui.tips_wait_for_a_new_game", GAME_FILES)) return;
	kp_t_eq_str(T, console_tips(T, &u, 0, name, sizeof name), "rez\\tips_dlg.bin", "a new game's tips");
	kp_t_eq_str(T, console_tips(T, &u, 1, name, sizeof name), "", "none for a saved game");
	kp_ui_end_game(u->ui);
	hud(T, u->ui, terran01(T)->g);
	kp_t_eq_str(T, u->ui->modeless >= 0 ? kp_t_ui_bin(u->ui, u->ui->modeless, name, sizeof name) : "", "rez\\tips_dlg.bin",
		"the next new game's again");
}

typedef struct {
	KpT *T;
	KpUi *ui;
	Clock clock;
} Menu;

static void key(Menu *m, uint16_t vk, int ch, int n)
{
	kp_ui_window_key(m->ui, 1, vk, 0);
	if (ch) kp_ui_window_char(m->ui, (uint16_t)ch, 0);
	kp_ui_window_key(m->ui, 0, vk, 0);
	frames(&m->clock, n);
}

static const char *modeless(KpUi *ui, char *name, size_t size)
{
	return ui->modeless >= 0 ? kp_t_ui_bin(ui, ui->modeless, name, size) : "";
}

static Menu *game_menu(KpT *T, KpGame *g)
{
	Menu *m = (Menu *)calloc(1, sizeof *m);
	KpOracleUi *u = kp_t_ui(T, KP_ORACLE_UI_STRINGS);
	kp_t_ok(T, m != NULL, "out of memory");
	kp_t_defer(T, free, m);
	kp_ui_font_set(u->ui, 1);
	theme(T, u->ui, 1);
	m->T = T;
	m->ui = u->ui;
	m->clock.ui = u->ui;
	m->clock.now = 1000;
	u->ui->now = 1000;
	hud(T, u->ui, g);
	return m;
}

static void game_menus_quit_the_mission(KpT *T)
{
	KpOracleGame *og;
	Menu *m;
	KpUiSoundCommand commands[KP_UI_SOUND_COMMANDS];
	size_t n, k;
	int clicks = 0;
	char name[128];
	if (!kp_t_ready(T, "test_kp_ui.game_menus_quit_the_mission", GAME_FILES)) return;
	og = terran01(T);
	m = game_menu(T, og->g);
	frames(&m->clock, 5);
	key(m, 0x79, 0, 10); /* F10: the console's MENU */
	kp_t_eq_str(T, modeless(m->ui, name, sizeof name), "rez\\gamemenu.bin", NULL);
	kp_t_eq(T, kp_scalar(og->g, KP_PAUSED), 1, NULL);
	m->ui->sound.device = 1;
	m->ui->sound.effects = 50;
	kp_ui_take_sound_commands(m->ui, commands, KP_UI_SOUND_COMMANDS);
	key(m, 0x4a, 'j', 10);
	kp_t_eq_str(T, modeless(m->ui, name, sizeof name), "rez\\objctdlg.bin", NULL);
	n = kp_ui_take_sound_commands(m->ui, commands, KP_UI_SOUND_COMMANDS);
	for (k = 0; k < n; k++) clicks += commands[k].type == 9 && commands[k].sound == 15;
	kp_t_eq(T, clicks, 1, "the fired button's UI click (GameDialogCallback 0x443f20, PlayUiClickSound 0x49e460)");
	key(m, 0x1b, 27, 10);
	kp_t_eq_str(T, modeless(m->ui, name, sizeof name), "rez\\gamemenu.bin", NULL);
	key(m, 0x45, 'e', 10);
	kp_t_eq_str(T, modeless(m->ui, name, sizeof name), "rez\\abrtmenu.bin", NULL);
	key(m, 0x51, 'q', 10);
	kp_t_eq_str(T, modeless(m->ui, name, sizeof name), "rez\\quit2mnu.bin", NULL);
	key(m, 0x51, 'q', 10);
	kp_t_eq_str(T, modeless(m->ui, name, sizeof name), "", NULL);
	kp_t_eq(T, m->ui->game_exit, 4, NULL);
	kp_t_eq(T, kp_scalar(og->g, KP_PAUSED), 0, NULL);
}

/* Our video options (koprulu\video.bin, in place of the original's rez\video.bin): opened from the options menu by
 * V; its option buttons show the host's choices and set them (each change counted in the serial the host follows),
 * the choices the host lacks are disabled, Cancel puts every choice back and OK counts a commit. */
static int32_t video_control(Menu *m, int16_t id) { return kp_ui_find_control(m->ui, m->ui->modeless, id); }
static int chosen(Menu *m, int16_t id) { return m->ui->controls[video_control(m, id)].words36[0] == 1; }

static void choose(Menu *m, int16_t id)
{
	(void)kp_ui_send_user(m->ui, video_control(m, id), KP_UI_CLOSE, 0); /* the option button's activation */
	frames(&m->clock, 2);
}

static void video_options_set_and_revert(KpT *T)
{
	KpOracleGame *og;
	Menu *m;
	KpUiVideoOptions *o;
	uint32_t serial;
	char name[128];
	if (!kp_t_ready(T, "test_kp_ui.video_options_set_and_revert", GAME_FILES)) return;
	og = terran01(T);
	m = game_menu(T, og->g);
	o = &m->ui->video_options;
	o->available = KP_VIDEO_HAS_HD2; /* no x4 art, no TrueType fonts */
	frames(&m->clock, 5);
	key(m, 0x79, 0, 10); /* F10, Options, Video */
	key(m, 0x4f, 'o', 10);
	kp_t_eq_str(T, modeless(m->ui, name, sizeof name), "rez\\options.bin", NULL);
	key(m, 0x56, 'v', 10);
	kp_t_eq_str(T, modeless(m->ui, name, sizeof name), "koprulu\\video.bin", NULL);
	kp_t_ok(T, chosen(m, 10) && chosen(m, 20) && chosen(m, 30) && chosen(m, 40) && chosen(m, 60), "each choice Auto");
	kp_t_ok(T, chosen(m, 52) && !chosen(m, 50), "the portraits animated");
	kp_t_ok(T, !(m->ui->controls[video_control(m, 12)].flags & KP_UI_DISABLED), "HD (2x) offered");
	kp_t_ok(T, (m->ui->controls[video_control(m, 13)].flags & KP_UI_DISABLED) != 0, "HD (4x) disabled");
	kp_t_ok(T, (m->ui->controls[video_control(m, 42)].flags & KP_UI_DISABLED) != 0, "the vector text disabled");
	kp_t_ok(T, !(m->ui->controls[video_control(m, 32)].flags & KP_UI_DISABLED), "HD (2x)'s water masks: animated water");
	kp_t_ok(T, (m->ui->controls[video_control(m, 62)].flags & KP_UI_DISABLED) != 0, "the lighting disabled");
	kp_t_ok(T, !(m->ui->controls[video_control(m, 61)].flags & KP_UI_DISABLED), "the sprites unlit offered");
	serial = o->serial;
	choose(m, 12);
	kp_t_eq(T, o->graphics, KP_GRAPHICS_HD2, "HD (2x)");
	kp_t_ok(T, chosen(m, 12) && !chosen(m, 10), "one button of the group");
	choose(m, 22);
	kp_t_eq(T, o->fog, KP_FOG_SMOOTH, "the smooth fog");
	kp_t_ok(T, chosen(m, 12), "the art's group untouched");
	choose(m, 31);
	kp_t_eq(T, o->water, KP_WATER_STILL, "still water");
	choose(m, 50);
	kp_t_eq(T, m->ui->portrait.mode, KP_UI_PORTRAITS_NONE, "no portraits");
	choose(m, 61);
	kp_t_eq(T, o->lighting, KP_LIGHTING_OFF, "the sprites unlit");
	kp_t_eq(T, o->serial, serial + 5, "each change counted");
	key(m, 0x43, 'c', 10); /* Cancel */
	kp_t_eq_str(T, modeless(m->ui, name, sizeof name), "rez\\options.bin", NULL);
	kp_t_ok(T, o->graphics == KP_VIDEO_AUTO && o->fog == KP_VIDEO_AUTO && o->water == KP_VIDEO_AUTO
		&& o->lighting == KP_VIDEO_AUTO, "put back");
	kp_t_eq(T, m->ui->portrait.mode, KP_UI_PORTRAITS_ANIMATED, "the portraits put back");
	kp_t_ok(T, o->serial > serial + 5 && m->ui->options_committed == 0, "a change, no commit");
	o->available = KP_VIDEO_HAS_HD2 | KP_VIDEO_HAS_LIGHTING; /* the context takes the BC blocks */
	key(m, 0x56, 'v', 10);
	kp_t_ok(T, !(m->ui->controls[video_control(m, 62)].flags & KP_UI_DISABLED), "the lighting offered");
	choose(m, 12);
	choose(m, 62);
	key(m, 0x4f, 'o', 10); /* OK: back to the game */
	kp_t_eq_str(T, modeless(m->ui, name, sizeof name), "", NULL);
	kp_t_eq(T, o->graphics, KP_GRAPHICS_HD2, "kept");
	kp_t_eq(T, o->lighting, KP_LIGHTING_ON, "the lighting kept");
	kp_t_eq(T, m->ui->options_committed, KP_UI_SECTION_VIDEO, "a commit");
	kp_t_eq(T, m->ui->gamma, KP_UI_GAMMA_DEFAULT, "the gamma untouched");
	o->available = 0; /* nothing imported: no HD art, no water masks */
	key(m, 0x79, 0, 10);
	key(m, 0x4f, 'o', 10);
	key(m, 0x56, 'v', 10);
	kp_t_eq_str(T, modeless(m->ui, name, sizeof name), "koprulu\\video.bin", NULL);
	kp_t_ok(T, (m->ui->controls[video_control(m, 12)].flags & KP_UI_DISABLED) != 0, "HD (2x) disabled");
	kp_t_ok(T, (m->ui->controls[video_control(m, 32)].flags & KP_UI_DISABLED) != 0, "the animated water disabled");
	kp_t_ok(T, !(m->ui->controls[video_control(m, 31)].flags & KP_UI_DISABLED), "the still water offered");
	kp_t_ok(T, (m->ui->controls[video_control(m, 62)].flags & KP_UI_DISABLED) != 0, "no HD art: the lighting disabled");
}

/* Each options dialog alike: everything changed under it stays at its OK with its section committed, and goes back at
 * Cancel and when the dialog closes with no answer, nothing committed. */
static void options_dialogs_keep_at_ok_alone(KpT *T)
{
	enum { END_OK, END_CANCEL, END_NO_ANSWER, ENDS };
	static const struct { int which; uint32_t section; } DIALOGS[4] = { { KP_UI_OPTIONS_SPEED, KP_UI_SECTION_SPEED },
		{ KP_UI_OPTIONS_SOUND, KP_UI_SECTION_SOUND }, { KP_UI_OPTIONS_VIDEO, KP_UI_SECTION_VIDEO },
		{ KP_UI_OPTIONS_TIPS, KP_UI_SECTION_TIPS } };
	KpOracleGame *og;
	Menu *m;
	KpUi *ui;
	int d, end;
	if (!kp_t_ready(T, "test_kp_ui.options_dialogs_keep_at_ok_alone", GAME_FILES)) return;
	og = terran01(T);
	m = game_menu(T, og->g);
	ui = m->ui;
	ui->tip_index = 1; /* a valid one: the tips' opening takes 0 for the first */
	for (d = 0; d < 4; d++) {
		for (end = 0; end < ENDS; end++) {
			const uint32_t bits = ui->options, mouse = ui->mouse_scroll_speed, tip = ui->tip_index;
			const uint32_t gamma = ui->gamma, portrait = ui->portrait.mode, music = ui->sound.music;
			const int8_t fog = ui->video_options.fog;
			const uint8_t throttle = ui->cpu_throttle;
			const int64_t speed = kp_scalar(og->g, KP_SPEED);
			uint32_t now_tip;
			kp_ui_options_dialog(ui, DIALOGS[d].which);
			now_tip = ui->tip_index;
			ui->options ^= KP_UI_OPTION_SPEECH;
			ui->mouse_scroll_speed = mouse + 1;
			ui->sound.music = music + 1;
			ui->gamma = gamma + 1;
			ui->portrait.mode = portrait == 0 ? 1 : 0;
			ui->video_options.fog = (int8_t)(fog == KP_FOG_SMOOTH ? KP_VIDEO_AUTO : KP_FOG_SMOOTH);
			ui->cpu_throttle = !throttle;
			kp_scalar_set(og->g, KP_SPEED, speed + 1);
			if (end == END_NO_ANSWER) {
				kp_ui_close_modeless(ui, 1);
			} else {
				ui->result = end == END_OK ? -2 : -3;
				(void)kp_ui_send_user(ui, ui->modeless, KP_UI_CLOSE, 0);
			}
			kp_t_eq(T, ui->modeless, -1, "dialog %d end %d: closed", d, end);
			kp_t_eq(T, ui->options_pending, 0, "dialog %d end %d: answered", d, end);
			kp_t_eq(T, ui->options_committed, end == END_OK ? DIALOGS[d].section : 0, "dialog %d end %d: committed", d, end);
			if (end == END_OK) {
				kp_t_eq(T, ui->options, bits ^ KP_UI_OPTION_SPEECH, "dialog %d: OK keeps the box", d);
				kp_t_eq(T, ui->mouse_scroll_speed, mouse + 1, "dialog %d: OK keeps the scroll speed", d);
				kp_t_eq(T, ui->cpu_throttle, !throttle, "dialog %d: OK keeps the throttle", d);
				kp_t_ok(T, ui->video_options.fog != fog, "dialog %d: OK keeps the fog", d);
				ui->options = bits; /* the next end's start */
				ui->mouse_scroll_speed = mouse;
				ui->sound.music = music;
				ui->gamma = gamma;
				ui->portrait.mode = portrait;
				ui->video_options.fog = fog;
				ui->cpu_throttle = throttle;
				ui->tip_index = tip;
				kp_scalar_set(og->g, KP_SPEED, speed);
				ui->options_committed = 0;
			} else {
				kp_t_eq(T, ui->options, bits, "dialog %d end %d: the box back", d, end);
				kp_t_eq(T, ui->mouse_scroll_speed, mouse, "dialog %d end %d: the scroll speed back", d, end);
				kp_t_eq(T, ui->sound.music, music, "dialog %d end %d: the volume back", d, end);
				kp_t_eq(T, ui->gamma, gamma, "dialog %d end %d: the gamma back", d, end);
				kp_t_eq(T, ui->portrait.mode, portrait, "dialog %d end %d: the portraits back", d, end);
				kp_t_eq(T, ui->video_options.fog, fog, "dialog %d end %d: the fog back", d, end);
				kp_t_eq(T, ui->cpu_throttle, throttle, "dialog %d end %d: the throttle back", d, end);
				kp_t_eq(T, kp_scalar(og->g, KP_SPEED), speed, "dialog %d end %d: the speed back", d, end);
				if (DIALOGS[d].which == KP_UI_OPTIONS_TIPS) {
					kp_t_ok(T, now_tip != tip, "the tips stepped");
				}
				kp_t_eq(T, ui->tip_index, tip, "dialog %d end %d: the tip back", d, end);
			}
		}
	}
}

/* A dialog's keyboard focus (gamemenu.bin): Tab walks its ring of children to the next focusable control and
 * Shift+Tab back (DialogCharInteract 0x4c1cb0, TabFocus 0x4c2220), the arrows the same (DialogDefaultInteract
 * 0x4c1470), and a hidden focus passes on to the next focusable control (ControlDefaultInteract 0x4c1e90 code 6). */
static int focusable(const KpUi *ui, int32_t k) { return (ui->controls[k].flags & 0x1a) == 0x18; }

static int32_t step(const KpUi *ui, const int32_t *ring, int count, int32_t from, int dir)
{
	int at = 0, n;
	for (n = 0; n < count; n++) {
		if (ring[n] == from) at = n;
	}
	for (n = 1; n <= count; n++) {
		int32_t k = ring[((at + dir * n) % count + count) % count];
		if (focusable(ui, k)) return k;
	}
	return -1;
}

static void dialog_keys_walk_the_focus(KpT *T)
{
	KpOracleGame *og;
	Menu *m;
	int32_t d, c, ring[256], first, second;
	int count = 0;
	char name[128];
	if (!kp_t_ready(T, "test_kp_ui.dialog_keys_walk_the_focus", GAME_FILES)) return;
	og = terran01(T);
	m = game_menu(T, og->g);
	frames(&m->clock, 5);
	key(m, 0x79, 0, 2); /* F10: the console's MENU */
	d = m->ui->modeless;
	kp_t_ok(T, d >= 0, "the game menu");
	kp_t_eq_str(T, kp_t_ui_bin(m->ui, d, name, sizeof name), "rez\\gamemenu.bin", NULL);
	for (c = m->ui->controls[d].child; c >= 0 && count < 256; c = m->ui->controls[c].next) ring[count++] = c;
	first = m->ui->controls[d].active;
	kp_t_ok(T, first >= 0 && focusable(m->ui, first), "the menu's focus");
	second = step(m->ui, ring, count, first, 1);
	kp_t_ok(T, second != first, "another focusable control");
	key(m, 0x09, '\t', 2);
	kp_t_eq(T, m->ui->controls[d].active, second, "Tab: the next");
	kp_ui_set_keys(m->ui, 1, 0, 0, 0);
	key(m, 0x09, '\t', 2);
	kp_ui_set_keys(m->ui, 0, 0, 0, 0);
	kp_t_eq(T, m->ui->controls[d].active, first, "Shift+Tab: back");
	key(m, 0x28, 0, 2);
	kp_t_eq(T, m->ui->controls[d].active, second, "the down arrow: the next");
	key(m, 0x25, 0, 2);
	kp_t_eq(T, m->ui->controls[d].active, first, "the left arrow: back");
	kp_ui_set_visible(m->ui, first, 0);
	kp_t_eq(T, m->ui->controls[d].active, second, "a hidden focus passes on");
}

/* --- the invalid region --- */

static void region_add(KpUi *ui, int l, int t, int r, int b)
{
	ui->region[ui->region_count][0] = (int16_t)l;
	ui->region[ui->region_count][1] = (int16_t)t;
	ui->region[ui->region_count][2] = (int16_t)r;
	ui->region[ui->region_count][3] = (int16_t)b;
	ui->region_count++;
}

static void region_cuts_overlapping_rects(KpT *T)
{
	KpUi *ui = (KpUi *)calloc(1, sizeof *ui);
	int16_t out[KP_UI_REGION][4];
	static uint8_t covered[KP_UI_SCREEN_H][KP_UI_SCREEN_W];
	int n, k, x, y, area = 0, overlaps = 0;
	kp_t_ok(T, ui != NULL, "out of memory");
	kp_t_defer(T, free, ui);
	region_add(ui, 0, 0, 64, 64);
	region_add(ui, 32, 32, 96, 96);
	n = kp_ui_region(ui, out);
	kp_t_ok(T, n >= 2 && n < KP_UI_REGION, "the fragments of two overlapping squares");
	memset(covered, 0, sizeof covered);
	for (k = 0; k < n; k++) { /* inclusive rectangles: the fragments tile the union once */
		for (y = out[k][1]; y <= out[k][3]; y++) {
			for (x = out[k][0]; x <= out[k][2]; x++) {
				overlaps += covered[y][x]++;
				area++;
			}
		}
	}
	kp_t_eq(T, area, 64 * 64 + 64 * 64 - 32 * 32, "the union's area");
	kp_t_eq(T, overlaps, 0, "no pixel in two fragments");
	for (k = 1; k < n; k++) kp_t_ok(T, out[k - 1][3] <= out[k][3], "sorted by bottom");
}

/* A grid of thin crossing stripes cuts into thousands of fragments, past the ones kept in the struct: the region
 * still comes out sorted and capped at what PaintDialogLayer takes. */
static void region_grows_past_its_inline_fragments(KpT *T)
{
	KpUi *ui = (KpUi *)calloc(1, sizeof *ui);
	int16_t out[KP_UI_REGION][4];
	int n, k;
	kp_t_ok(T, ui != NULL, "out of memory");
	kp_t_defer(T, free, ui);
	for (k = 0; k < 64; k++) region_add(ui, 0, 6 * k, KP_UI_SCREEN_W, 6 * k + 3);
	for (k = 0; k < 64; k++) region_add(ui, 6 * k, 0, 6 * k + 3, KP_UI_SCREEN_H);
	n = kp_ui_region(ui, out);
	kp_t_eq(T, n, KP_UI_REGION - 1, "capped at 255 rectangles");
	for (k = 0; k < n; k++) {
		kp_t_ok(T, out[k][0] <= out[k][2] && out[k][1] <= out[k][3], "a rectangle with an area");
		if (k > 0) kp_t_ok(T, out[k - 1][3] <= out[k][3], "sorted by bottom");
	}
}

static const KpTest TESTS[] = {
	{ "region_cuts_overlapping_rects", region_cuts_overlapping_rects, 0 },
	{ "region_grows_past_its_inline_fragments", region_grows_past_its_inline_fragments, 0 },
	{ "timers_sort_by_due_time", timers_sort_by_due_time, 0 },
	{ "timers_fire_when_strictly_late", timers_fire_when_strictly_late, 0 },
	{ "timers_replaced_by_own_work", timers_replaced_by_own_work, 0 },
	{ "timers_stale_search_keys", timers_stale_search_keys, 0 },
	{ "timers_delete_owner", timers_delete_owner, 0 },
	{ "timers_replay_the_original", timers_replay_the_original, 0 },
	{ "text_matches_the_original_probes", text_matches_the_original_probes, 0 },
	{ "canvas_hd_items_follow_the_draws", canvas_hd_items_follow_the_draws, 0 },
	{ "image_forget_drops_a_freed_surface", image_forget_drops_a_freed_surface, 0 },
	{ "image_forget_while_an_image_is_open", image_forget_while_an_image_is_open, 0 },
	{ "image_owners_layer_by_area", image_owners_layer_by_area, 0 },
	{ "plain_draws_erase_images", plain_draws_erase_images, 0 },
	{ "image_owners_pieces_and_whole_grp", image_owners_pieces_and_whole_grp, 0 },
	{ "glyphs_lie_over_images", glyphs_lie_over_images, 0 },
	{ "draws_outside_the_log_are_audited", draws_outside_the_log_are_audited, 0 },
	{ "video_image_logs_the_frame", video_image_logs_the_frame, 0 },
	{ "image_tiles_make_one_image_per_frame", image_tiles_make_one_image_per_frame, 0 },
	{ "image_owners_merge_a_grid", image_owners_merge_a_grid, 0 },
	{ "console_keeps_the_view_layer_under_hd", console_keeps_the_view_layer_under_hd, 0 },
	{ "items_over_items_lie_over_them", items_over_items_lie_over_them, 0 },
	{ "image_blit_with_a_full_log", image_blit_with_a_full_log, 0 },
	{ "pictures_keep_what_lay_under_them", pictures_keep_what_lay_under_them, 0 },
	{ "popups_lie_over_their_screen", popups_lie_over_their_screen, 0 },
	{ "popups_cover_text_plotted_past_a_repaint", popups_cover_text_plotted_past_a_repaint, 0 },
	{ "movie_frames_are_images", movie_frames_are_images, 0 },
	{ "theme_frames_lie_over_what_lay_under_them", theme_frames_lie_over_what_lay_under_them, 0 },
	{ "keyed_blits_leave_the_pixels_they_skip", keyed_blits_leave_the_pixels_they_skip, 0 },
	{ "images_under_glyphs_stay", images_under_glyphs_stay, 0 },
	{ "audit_plane_follows_the_canvas", audit_plane_follows_the_canvas, 0 },
	{ "screens_take_their_owners_once", screens_take_their_owners_once, 0 },
	{ "vector_text_logs_the_glyphs", vector_text_logs_the_glyphs, 0 },
	{ "vector_text_takes_the_glyphs_out_of_the_canvas", vector_text_takes_the_glyphs_out_of_the_canvas, 0 },
	{ "vector_text_logs_the_caret_without_advance", vector_text_logs_the_caret_without_advance, 0 },
	{ "vector_text_follows_a_blit", vector_text_follows_a_blit, 0 },
	{ "vector_text_keeps_odd_fonts_bitmaps", vector_text_keeps_odd_fonts_bitmaps, 0 },
	{ "vector_text_keeps_to_the_tooltip_box", vector_text_keeps_to_the_tooltip_box, 0 },
	{ "panels_equal_the_original", panels_equal_the_original, 0 },
	{ "hud_panels_equal_the_original", hud_panels_equal_the_original, 0 },
	{ "hud_panels_hold_with_the_glyph_log", hud_panels_hold_with_the_glyph_log, 0 },
	{ "hud_panels_hold_with_the_image_log", hud_panels_hold_with_the_image_log, 0 },
	{ "keys_translate_through_the_accelerators", keys_translate_through_the_accelerators, 0 },
	{ "game_deselections_send_selection_commands", game_deselections_send_selection_commands, 0 },
	{ "result_dialogs_leave_exit_codes", result_dialogs_leave_exit_codes, 0 },
	{ "next_game_forgets_the_last_result", next_game_forgets_the_last_result, 0 },
	{ "panel_point_is_the_layouts", panel_point_is_the_layouts, 0 },
	{ "failed_console_leaves_no_game_input", failed_console_leaves_no_game_input, 0 },
	{ "end_game_resets_the_input_procedures", end_game_resets_the_input_procedures, 0 },
	{ "save_dialog_reports_a_file_that_stays", save_dialog_reports_a_file_that_stays, 0 },
	{ "first_view_on_the_start_location", first_view_on_the_start_location, 0 },
	{ "file_names_the_game_refuses", file_names_the_game_refuses, 0 },
	{ "text_reads_utf8_and_windows_1252_bytes", text_reads_utf8_and_windows_1252_bytes, 0 },
	{ "text_finds_glyphs_by_code_point", text_finds_glyphs_by_code_point, 0 },
	{ "text_skips_a_character_with_no_glyph", text_skips_a_character_with_no_glyph, 0 },
	{ "score_roster_holds_a_name_of_wide_characters", score_roster_holds_a_name_of_wide_characters, 0 },
	{ "core_pause_takes_the_pause_tick", core_pause_takes_the_pause_tick, 0 },
	{ "vector_text_takes_code_points_past_a_byte", vector_text_takes_code_points_past_a_byte, 0 },
	{ "view_widens_the_game_area", view_widens_the_game_area, 0 },
	{ "loaded_save_refreshes_the_console", loaded_save_refreshes_the_console, 0 },
	{ "wide_view_centres_the_status_unit", wide_view_centres_the_status_unit, 0 },
	{ "tips_wait_for_a_new_game", tips_wait_for_a_new_game, 0 },
	{ "game_menus_quit_the_mission", game_menus_quit_the_mission, 0 },
	{ "video_options_set_and_revert", video_options_set_and_revert, 0 },
	{ "options_dialogs_keep_at_ok_alone", options_dialogs_keep_at_ok_alone, 0 },
	{ "dialog_keys_walk_the_focus", dialog_keys_walk_the_focus, 0 },
};
KP_SUITE(kp_suite_kp_ui, "test_kp_ui", TESTS);
