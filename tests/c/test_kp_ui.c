/* The interface layer kp_ui (ADR 0010) on its own: the UI timer queue of timer.cpp (atlas P43) on synthetic cases and
 * the original's timer passes of an emulator run, the text engine against the original's probes, the panels and the
 * console over a replayed game against the original's captures, the keys, the selection commands, the result dialogs,
 * the game menus and the console's work over native games. The console's host and the presentation's canvas and layout
 * are tested in test_hud.c, over the interface's C host (native/kp_app/kp_hud.h) and kp_present's canvas and layout. */
#include "kp_test_ui.h"

#include "../../native/kp_core/src/core/kp_ui_text.h"

#define DATA "generated/data/"
#define TERRAN01 "generated/missions/terran/terran01.kpm"

static const char *const UI_FILES[] = { DATA "ui.kpd", NULL };
static const char *const GAME_FILES[] = { DATA "ui.kpd", TERRAN01, NULL };

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

/* A section of a converted file. */
static const uint8_t *section_of(KpT *T, const uint8_t *data, size_t size, const char *fourcc, size_t *n)
{
	const uint8_t *p = kp_kpf_section(data, size, fourcc, n);
	kp_t_ok(T, p != NULL, "no section %s", fourcc);
	return p;
}

/* The fonts' sections of fonts.kpd by name. */
static const uint8_t *font(KpT *T, const uint8_t *fonts, size_t size, const char *name, size_t *n)
{
	static const char *const NAMES[5][2] = { { "font8", "F8  " }, { "font10", "F10 " }, { "font14", "F14 " },
		{ "font16", "F16 " }, { "font16x", "F16X" } };
	int k;
	for (k = 0; k < 5; k++) {
		if (strcmp(NAMES[k][0], name) == 0) return section_of(T, fonts, size, NAMES[k][1], n);
	}
	kp_t_fail(T, "no font %s", name);
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
	size_t fonts_size, strings_size, tfgm_n, tfnt_n;
	uint8_t *fonts, *strings;
	const uint8_t *tfgm, *tfnt;
	int n, packets = 0, tbls = 0;
	uint8_t *buf;
	if (!kp_t_has(T, DATA "fonts.kpd")) {
		kp_t_log(T, "SKIP test_kp_ui.text_matches_the_original_probes: no generated/data");
		return;
	}
	fonts = kp_t_read(T, DATA "fonts.kpd", &fonts_size);
	strings = kp_t_read(T, DATA "strings.kpd", &strings_size);
	tfgm = section_of(T, fonts, fonts_size, "TFGM", &tfgm_n);
	tfnt = section_of(T, fonts, fonts_size, "TFNT", &tfnt_n);
	g = kp_t_golden(T, "tests/golden/oracle/ui/text_cases.json");
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
			f = font(T, fonts, fonts_size, S(1), &fn);
			setup(T, &t, f, fn);
			CHECK("offset", kp_ui_glyph(&t, (int)C(2)), C(3));
		} else if (strcmp(kind, "width") == 0) {
			f = font(T, fonts, fonts_size, S(1), &fn);
			setup(T, &t, f, fn);
			t.spacing = (uint8_t)C(3);
			CHECK("width", kp_ui_char_width(&t, (int)C(2)), C(4));
		} else if (strcmp(kind, "span") == 0) {
			f = font(T, fonts, fonts_size, S(1), &fn);
			setup(T, &t, f, fn);
			buf[0] = (uint8_t)C(2);
			buf[1] = 'A';
			buf[2] = 0;
			CHECK("consumed", kp_ui_text_span(&t, buf, 0, 3), C(5));
			CHECK("x", t.x, C(3));
			CHECK("bottom", t.ext_bottom, C(4));
		} else if (strcmp(kind, "wrap") == 0) {
			f = font(T, fonts, fonts_size, S(1), &fn);
			setup(T, &t, f, fn);
			t.clip_right = (int16_t)C(3);
			unhex(S(2), buf, 1 << 16);
			CHECK("consumed", kp_ui_find_line_break(&t, buf, 0), C(4));
			CHECK("x", t.x, C(5));
		} else if (strcmp(kind, "tbl") == 0) {
			size_t sn;
			const uint8_t *sec = section_of(T, strings, strings_size, S(1), &sn), *text = tbl_string(sec, sn, C(2));
			f = font(T, fonts, fonts_size, "font10", &fn);
			setup(T, &t, f, fn);
			CHECK("consumed", kp_ui_find_line_break(&t, text ? text : (const uint8_t *)"", 0), C(3));
			CHECK("x", t.x, C(4));
			tbls++;
		} else if (strcmp(kind, "tab") == 0) {
			f = font(T, fonts, fonts_size, S(1), &fn);
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
			f = font(T, fonts, fonts_size, S(1), &fn);
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
			f = font(T, fonts, fonts_size, S(1), &fn);
			setup(T, &t, f, fn);
			t.highlight = 65;
			len = unhex(S(2), buf, 1 << 16);
			kp_ui_text_span(&t, buf, 0, len);
			CHECK("x", t.x, C(3));
			CHECK("bottom", t.ext_bottom, C(4));
		} else if (strcmp(kind, "valign") == 0) {
			int32_t y = (int32_t)C(2);
			f = font(T, fonts, fonts_size, "font8", &fn);
			setup(T, &t, f, fn);
			t.flags = (uint8_t)C(1);
			t.clip_bottom = 31;
			t.y = (int32_t)C(3);
			kp_ui_align_vertically(&t, &y);
			CHECK("y", y, C(4));
		} else if (strcmp(kind, "colour") == 0) {
			f = font(T, fonts, fonts_size, "font8", &fn);
			setup(T, &t, f, fn);
			kp_ui_select_color(&t, (int)C(1));
			CHECK("row", t.row, C(2));
			CHECK("previous", t.previous_row, C(3));
		} else if (strcmp(kind, "grey") == 0) {
			f = font(T, fonts, fonts_size, "font8", &fn);
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
				const uint8_t *ff = font(T, fonts, fonts_size, S(1), &fn);
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
			f = font(T, fonts, fonts_size, "font8", &fn);
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
 * place) and lists them at their canvas pens; a later draw over a glyph drops it or clips it to its uncovered pixels.
 * Off, the canvas keeps the glyphs' pixels and lists none. */
static void vector_text_takes_the_glyphs_out_of_the_canvas(KpT *T)
{
	KpOracleUi *u;
	KpUi *ui;
	KpUiCanvas *c;
	KpUiSurface s;
	uint8_t codes[256];
	int32_t k, x, y, inked = 0, cut;
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
	kp_t_ok(T, kp_hud_replay_run(r, &err) == 0, "%s", err.msg);
	hud_glyphs += r->ui.ui->glyph_log.count;
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
	int32_t x, y;
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

static const KpTest TESTS[] = {
	{ "timers_sort_by_due_time", timers_sort_by_due_time, 0 },
	{ "timers_fire_when_strictly_late", timers_fire_when_strictly_late, 0 },
	{ "timers_replaced_by_own_work", timers_replaced_by_own_work, 0 },
	{ "timers_stale_search_keys", timers_stale_search_keys, 0 },
	{ "timers_delete_owner", timers_delete_owner, 0 },
	{ "timers_replay_the_original", timers_replay_the_original, 0 },
	{ "text_matches_the_original_probes", text_matches_the_original_probes, 0 },
	{ "vector_text_logs_the_glyphs", vector_text_logs_the_glyphs, 0 },
	{ "vector_text_takes_the_glyphs_out_of_the_canvas", vector_text_takes_the_glyphs_out_of_the_canvas, 0 },
	{ "vector_text_follows_a_blit", vector_text_follows_a_blit, 0 },
	{ "vector_text_keeps_odd_fonts_bitmaps", vector_text_keeps_odd_fonts_bitmaps, 0 },
	{ "vector_text_keeps_to_the_tooltip_box", vector_text_keeps_to_the_tooltip_box, 0 },
	{ "panels_equal_the_original", panels_equal_the_original, 0 },
	{ "hud_panels_equal_the_original", hud_panels_equal_the_original, 0 },
	{ "hud_panels_hold_with_the_glyph_log", hud_panels_hold_with_the_glyph_log, 0 },
	{ "keys_translate_through_the_accelerators", keys_translate_through_the_accelerators, 0 },
	{ "game_deselections_send_selection_commands", game_deselections_send_selection_commands, 0 },
	{ "result_dialogs_leave_exit_codes", result_dialogs_leave_exit_codes, 0 },
	{ "next_game_forgets_the_last_result", next_game_forgets_the_last_result, 0 },
	{ "end_game_resets_the_input_procedures", end_game_resets_the_input_procedures, 0 },
	{ "first_view_on_the_start_location", first_view_on_the_start_location, 0 },
	{ "file_names_the_game_refuses", file_names_the_game_refuses, 0 },
	{ "view_widens_the_game_area", view_widens_the_game_area, 0 },
	{ "loaded_save_refreshes_the_console", loaded_save_refreshes_the_console, 0 },
	{ "wide_view_centres_the_status_unit", wide_view_centres_the_status_unit, 0 },
	{ "tips_wait_for_a_new_game", tips_wait_for_a_new_game, 0 },
	{ "game_menus_quit_the_mission", game_menus_quit_the_mission, 0 },
	{ "dialog_keys_walk_the_focus", dialog_keys_walk_the_focus, 0 },
};
KP_SUITE(kp_suite_kp_ui, "test_kp_ui", TESTS);
