/* A language over the base (ADR 0016; ADR 0019, stage C3): the glyph supplements of the text engine and the vector
 * text, kp_fnt_supplement, a language's parts in the interface (kp_ui_set_locale) and in a mission
 * (kp_host_game_text), the language built from the install's own files (kp_locale.c) equal to what the converter wrote
 * before stage C3 deleted it, and what a language must not change: every mission's state under a language's text is
 * the base's. The small cases build their parts here over the converter's fixtures; the ones over the real data (the
 * original data's interface, kp_t_data, and the languages of the Remastered install KP_TEST_SCR names) skip without
 * them. */
#include "kp_test_app.h"
#include "kp_test_ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kp_data.h"
#include "kp_json.h"
#include "kp_locale.h"
#include "kp_mission.h"
#include "kp_present.h"
#include "kp_session.h"
#include "kp_sha256.h"
#include "kp_ui.h"
#include "kp_ui_data.h"
#include "kp_ui_text.h"
#include "kp_text.h"
#include "kp_utf8.h"

#define DIR KP_T_FIXTURES
#define SCR "KP_TEST_SCR"
#define CONVERTER "tests/golden/locale/converter.json" /* converter locale's output, recorded before stage C3 */
#define SEED 0x12345678u
#define NO_STRING 0xffffu
#define STR_LIMIT 0xffffu
#define MISSION_STEPS 1000
#define TEXT_STEPS 3000 /* a mission with a text of its triggers has shown one by then */
#define ERROR_SIZE 256
#define CAMPAIGN_MISSIONS 64

static const char *const FIXTURES[] = { DIR "ui.kpd", DIR "fonts.kpd", NULL };
/* The original data (kp_t_data): the base vector fonts' and the games'. */
static const char *const REAL_BASE[] = { KP_T_DATA, NULL };

/* The languages of the profile (defs/l10n/locales.json, but the base): the real-data tests run each one the install
 * holds (kp_locale_held), and skip the others. */
static const char *const LANGUAGES[] = { "deDE", "esES", "esMX", "frFR", "itIT", "plPL", "ptBR", "ruRU", NULL };

/* The Remastered install (KP_TEST_SCR): its data, the languages' base as the product reads it, and its own files
 * (kp_res_raw_files) the languages are built from. */
typedef struct {
	const KpData *d;
	KpFiles raw;
} Install;

static void free_data(void *p)
{
	kp_data_free((KpData *)p);
	free(p);
}

/* Skips the test without the install. */
static void install(KpT *T, Install *in)
{
	KpRes *res = kp_t_install(T, SCR);
	KpData *d = (KpData *)calloc(1, sizeof *d);
	KpError e;
	if (!d) kp_t_fail(T, "out of memory");
	if (kp_data_build(d, res, NULL, 0, NULL, 0, &e) != 0) {
		free(d);
		kp_t_fail(T, "the install's data: %s", e.msg);
	}
	kp_t_defer(T, free_data, d);
	in->d = d;
	in->raw = d->raw;
}

typedef void (*LanguageTest)(KpT *T, const Install *in, const char *locale, void *ctx);

/* `test` (with `ctx`) for each language the install holds; one skip line when it runs for none. */
static void for_each_language(KpT *T, const Install *in, const char *label, LanguageTest test, void *ctx)
{
	int k, ran = 0;
	for (k = 0; LANGUAGES[k] != NULL; k++) {
		if (!kp_locale_held(&in->raw, LANGUAGES[k])) continue;
		int mark = kp_t_mark(T); /* a language's resources (a test holds at most 256) go with it */
		kp_t_log(T, "%s: %s", label, LANGUAGES[k]);
		test(T, in, LANGUAGES[k], ctx);
		kp_t_release(T, mark);
		ran++;
	}
	if (!ran) kp_t_log(T, "SKIP %s: the install holds no language", label);
}

static void free_ui(void *p) { kp_ui_free((KpUi *)p); }

static void free_session(void *p)
{
	kp_session_free((KpSession *)p);
	free(p);
}

static void free_vtext(void *p)
{
	kp_vtext_free((KpVText *)p);
	free(p);
}

static void free_locale(void *p)
{
	kp_locale_free((KpLocale *)p);
	free(p);
}

static void free_glyphs(void *p)
{
	kp_locale_glyphs_free((KpLocaleGlyphs *)p);
	free(p);
}

/* Fails on each problem a build found, after listing them all. */
static void no_problems(KpT *T, const KpLocaleFound *found, const char *what)
{
	int k;
	for (k = 0; k < found->problem_count; k++) kp_t_log(T, "%s: %s", what, found->problems[k]);
	kp_t_eq(T, found->problem_count, 0, "%s's problems", what);
}

/* The language built from the install over the base, released at the end; it has no problem. */
static KpLocale *built(KpT *T, const Install *in, const char *locale)
{
	KpLocale *l = (KpLocale *)calloc(1, sizeof *l);
	KpError e;
	if (!l) kp_t_fail(T, "out of memory");
	kp_t_defer(T, free_locale, l);
	e.msg[0] = 0;
	kp_t_eq(T, kp_locale_build(l, &in->d->ui, &in->raw, locale, &e), 0, "%s: %s", locale, e.msg);
	no_problems(T, &l->found, locale);
	return l;
}

/* The glyphs of the install's TrueType fonts for l's text (NULL: the base's), released at the end; no problem. */
static KpLocaleGlyphs *glyphs_of(KpT *T, const Install *in, const KpLocale *l)
{
	KpLocaleGlyphs *g = (KpLocaleGlyphs *)calloc(1, sizeof *g);
	KpLocaleFound found;
	KpError e;
	int rc;
	if (!g) kp_t_fail(T, "out of memory");
	kp_t_defer(T, free_glyphs, g);
	memset(&found, 0, sizeof found);
	e.msg[0] = 0;
	rc = kp_locale_glyphs(g, &in->d->ui, &in->raw, l, &found, &e);
	kp_t_eq(T, rc, 1, "the glyphs: %s", e.msg);
	no_problems(T, &found, "the glyphs");
	kp_locale_found_free(&found);
	return g;
}

/* An interface over a data's, released at the end. */
static KpUi *ui_over(KpT *T, const KpUiData *d)
{
	char err[ERROR_SIZE];
	KpUi *ui = kp_ui_new_data(d, err, sizeof err);
	kp_t_ok(T, ui != NULL, "%s", err);
	kp_t_defer(T, free_ui, ui);
	return ui;
}

/* An interface over the original data's (kp_t_data: its strings, sounds and portraits the base's). */
static KpUi *real_ui(KpT *T) { return ui_over(T, &kp_t_data(T)->ui); }

/* The base's interface with the language built from the install, its texts and glyphs; released at the end. */
static KpUi *real_language(KpT *T, const Install *in, const char *locale)
{
	const KpLocale *l = built(T, in, locale);
	const KpLocaleGlyphs *g = glyphs_of(T, in, l);
	KpUiLocale parts;
	KpUi *ui = ui_over(T, &in->d->ui);
	memset(&parts, 0, sizeof parts); /* no speech */
	kp_locale_to_ui(l, g, &parts);
	kp_ui_set_locale(ui, &parts);
	kp_t_eq_str(T, ui->locale_problem, "", "%s is accepted", locale);
	return ui;
}

static void free_mission(void *p)
{
	kp_mission_free((KpMission *)p);
	free(p);
}

/* A campaign mission of the data ("terran/terran01"), released at the end. */
static KpMission *data_mission(KpT *T, const KpData *d, const char *name)
{
	KpMission *m = (KpMission *)calloc(1, sizeof *m);
	const KpDataMission *entry;
	KpError e;
	char key[128];
	if (!m) kp_t_fail(T, "out of memory");
	snprintf(key, sizeof key, "missions/%s", name);
	entry = kp_data_find(d, key);
	if (!entry) {
		free(m);
		kp_t_fail(T, "no %s in the data", key);
	}
	if (kp_data_mission(d, entry, NULL, 0, m, &e) != 0) {
		free(m);
		kp_t_fail(T, "%s: %s", key, e.msg);
	}
	kp_t_defer(T, free_mission, m);
	return m;
}

/* A campaign mission's strings ("terran/terran01") in the language built from the install over m, the base's mission
 * (kp_locale_mission; counts: translated, kept, looked up, may be NULL): malloc'd, *size bytes; no problem. */
static uint8_t *language_strings(KpT *T, const Install *in, const char *locale, const char *name, const KpMission *m,
	size_t *size, int counts[3])
{
	KpLocaleFound found;
	KpError e;
	uint8_t *strs = NULL;
	int rc, k, problems;
	memset(&found, 0, sizeof found);
	e.msg[0] = 0;
	*size = 0;
	rc = kp_locale_mission(&in->raw, locale, name, m, &found, &strs, size, counts, &e);
	for (k = 0; k < found.problem_count; k++) kp_t_log(T, "%s %s: %s", locale, name, found.problems[k]);
	problems = found.problem_count;
	kp_locale_found_free(&found);
	if (rc != 1 || problems != 0) {
		free(strs);
		kp_t_fail(T, "%s %s: %s", locale, name, rc != 1 ? (rc == 0 ? "no map" : e.msg) : "a string is refused");
	}
	return strs;
}

static void put_u16(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
}

static void put_u32(uint8_t *p, uint32_t v)
{
	put_u16(p, v & 0xffffu);
	put_u16(p + 2, v >> 16);
}

/* --- the glyph supplement in the text engine --- */

enum { SUP_FIRST = 0x416, SUP_SECOND = 0x44f, SUP_NONE = 0x417 };

/* A supplement of the fixture font's glyph 34 at SUP_FIRST (3 x 2 at x 1: its packet and the NUL after it in the font)
 * and a 2 x 1 glyph at SUP_SECOND, lying before it. */
enum { SUP_HEAD_AND_SECOND = 26, SUP_FIRST_PACKET_END = SUP_HEAD_AND_SECOND + 5 };
static const uint8_t SUPPLEMENT[] = {
	2, 0, 0, 0,
	0x16, 0x04, 0, 0, SUP_HEAD_AND_SECOND, 0, 0, 0,
	0x4f, 0x04, 0, 0, 20, 0, 0, 0,
	2, 1, 0, 1, 0x00, 0x01,
	3, 2, 1, 0, 0x0a, 0x00,
};

static void plot_text(KpT *T, KpUi *ui, const char *text, uint8_t *pixels, int w, int h, const uint8_t *data, size_t size)
{
	KpUiSurface s;
	KpUiText t;
	static const uint8_t BANK[8 * 16] = { 0 };
	kp_ui_text_init(&t);
	kp_t_eq(T, kp_ui_text_select_font(&t, ui->font_files[0], ui->font_file_sizes[0]), 0, NULL);
	if (data != NULL) kp_t_eq(T, kp_ui_text_add_supplement(&t, ui->font_files[0], data, size), 0, NULL);
	kp_ui_text_set_rows(&t, BANK, sizeof BANK);
	memset(pixels, 0xee, (size_t)w * (size_t)h);
	s.w = w;
	s.h = h;
	s.pixels = pixels;
	t.target = &s;
	t.clip_right = (int16_t)w;
	t.clip_bottom = (int16_t)h;
	t.measuring = 0;
	kp_ui_text_span(&t, (const uint8_t *)text, 0, strlen(text));
}

static void supplement_finds_measures_and_draws(KpT *T)
{
	enum { W = 16, H = 4 };
	uint8_t ours[W * H], theirs[W * H];
	KpUi *ui;
	KpUiText t;
	if (!kp_t_ready(T, "test_locale", FIXTURES)) return;
	ui = kp_t_fixture_ui(T);
	kp_ui_text_init(&t);
	kp_t_eq(T, kp_ui_text_select_font(&t, ui->font_files[0], ui->font_file_sizes[0]), 0, NULL);
	kp_t_ok(T, !kp_ui_has_glyph(&t, SUP_FIRST), "no supplement: no glyph");
	kp_t_eq(T, kp_ui_text_width(&t, (const uint8_t *)"\xd0\x96"), 0, "no supplement: no width");
	kp_t_eq(T, kp_ui_text_add_supplement(&t, ui->font_files[0], SUPPLEMENT, sizeof SUPPLEMENT), 0, NULL);
	kp_t_eq(T, kp_ui_supplement_count(SUPPLEMENT, sizeof SUPPLEMENT), 2, NULL);
	kp_t_eq(T, kp_ui_supplement_code(SUPPLEMENT, 1), SUP_SECOND, NULL);
	kp_t_ok(T, kp_ui_has_glyph(&t, SUP_FIRST) && kp_ui_has_glyph(&t, SUP_SECOND), "both code points");
	kp_t_ok(T, !kp_ui_has_glyph(&t, SUP_NONE) && !kp_ui_has_glyph(&t, 0x10ffff), "only them");
	kp_t_ok(T, kp_ui_has_glyph(&t, 34), "the font's slots stay");
	kp_t_eq(T, kp_ui_text_width(&t, (const uint8_t *)"\xd0\x96"), kp_ui_char_width(&t, 34), "the same glyph's advance");
	kp_t_eq(T, kp_ui_text_width(&t, (const uint8_t *)"\xd1\x8f"), kp_ui_char_width(&t, 33), "the second's");
	/* a font without the supplement does not see it */
	kp_t_eq(T, kp_ui_text_select_font(&t, ui->font_files[1], ui->font_file_sizes[1]), 0, NULL);
	kp_t_ok(T, !kp_ui_has_glyph(&t, SUP_FIRST), "another font");
	kp_t_eq(T, kp_ui_text_select_font(&t, ui->font_files[0], ui->font_file_sizes[0]), 0, NULL);
	kp_t_ok(T, kp_ui_has_glyph(&t, SUP_FIRST), "its font again");
	/* drawn as the slot's glyph of the same bytes */
	plot_text(T, ui, "\xd0\x96", ours, W, H, SUPPLEMENT, sizeof SUPPLEMENT);
	plot_text(T, ui, "\"", theirs, W, H, NULL, 0);
	kp_t_ok(T, memcmp(ours, theirs, sizeof ours) == 0, "the same pixels as the slot's");
}

/* windows-1252's 0x80..0xa0 in the base's FNT fonts are boxes or other letters (font10's 0x92 is a "b"): their code
 * points come from the supplement before the slot, and U+00A0 takes a space's width. ruRU's en dash (U+2013, slot
 * 0x96) and deDE's and frFR's no-break spaces drew boxes; a stray byte 0x96 stays the slot, as the original drew it. */
static void supplement_comes_before_an_untrue_slot(KpT *T)
{
	enum { W = 16, H = 4 };
	static const uint8_t DASH[] = { /* U+2013 as the fixture font's glyph 34 */
		1, 0, 0, 0,
		0x13, 0x20, 0, 0, 12, 0, 0, 0,
		3, 2, 1, 0, 0x0a, 0x00,
	};
	uint8_t ours[W * H], theirs[W * H];
	KpUi *ui;
	KpUiText t;
	if (!kp_t_ready(T, "test_locale", FIXTURES)) return;
	ui = kp_t_fixture_ui(T);
	kp_ui_text_init(&t);
	kp_t_eq(T, kp_ui_text_select_font(&t, ui->font_files[0], ui->font_file_sizes[0]), 0, NULL);
	kp_t_eq(T, kp_ui_text_add_supplement(&t, ui->font_files[0], DASH, sizeof DASH), 0, NULL);
	kp_t_eq(T, kp_ui_text_width(&t, (const uint8_t *)"\xe2\x80\x93"), kp_ui_char_width(&t, 34), "the supplement's advance");
	kp_t_eq(T, kp_ui_text_width(&t, (const uint8_t *)"\x96"), kp_ui_char_width(&t, 0x96), "a stray byte's is its slot's");
	kp_t_eq(T, kp_ui_text_width(&t, (const uint8_t *)"\xc2\xa0"), kp_ui_char_width(&t, ' '), "U+00A0 a space's width");
	kp_t_ok(T, kp_ui_has_glyph(&t, 0xa0), "U+00A0 typed");
	plot_text(T, ui, "\xe2\x80\x93", ours, W, H, DASH, sizeof DASH);
	plot_text(T, ui, "\"", theirs, W, H, NULL, 0);
	kp_t_ok(T, memcmp(ours, theirs, sizeof ours) == 0, "the supplement's pixels");
	plot_text(T, ui, "\x96", ours, W, H, DASH, sizeof DASH);
	plot_text(T, ui, "\x96", theirs, W, H, NULL, 0);
	kp_t_ok(T, memcmp(ours, theirs, sizeof ours) == 0, "a stray byte draws its slot");
	plot_text(T, ui, "\xe2\x80\x93", ours, W, H, NULL, 0);
	kp_t_ok(T, memcmp(ours, theirs, sizeof ours) == 0, "with no supplement the slot, as the base draws it");
}

static void supplement_tells_a_missing_code_once(KpT *T)
{
	KpUi *ui;
	KpUiText t;
	static const uint8_t TEXT[] = "\xd0\x97\xd0\x97\xd0\x98";
	if (!kp_t_ready(T, "test_locale", FIXTURES)) return;
	ui = kp_t_fixture_ui(T);
	kp_ui_text_init(&t);
	kp_t_eq(T, kp_ui_text_select_font(&t, ui->font_files[0], ui->font_file_sizes[0]), 0, NULL);
	t.measuring = 1;
	t.clip_right = 100;
	kp_ui_text_span(&t, TEXT, 0, sizeof TEXT - 1);
	kp_t_eq(T, t.x, 0, "a character with no glyph has no width");
	kp_t_eq(T, kp_ui_text_take_missing(&t), SUP_NONE, "told");
	kp_t_eq(T, kp_ui_text_take_missing(&t), 0x418, "the next one, the repeat not");
	kp_t_eq(T, kp_ui_text_take_missing(&t), 0, "no more");
	kp_ui_text_span(&t, TEXT, 0, sizeof TEXT - 1);
	kp_t_eq(T, kp_ui_text_take_missing(&t), 0, "told once");
}

static void supplement_malformed_is_not_taken(KpT *T)
{
	uint8_t bad[sizeof SUPPLEMENT];
	KpUi *ui;
	KpUiText t;
	if (!kp_t_ready(T, "test_locale", FIXTURES)) return;
	ui = kp_t_fixture_ui(T);
	kp_ui_text_init(&t);
	kp_t_eq(T, kp_ui_text_select_font(&t, ui->font_files[0], ui->font_file_sizes[0]), 0, NULL);
	kp_t_eq(T, kp_ui_supplement_count(NULL, 0), -1, "none");
	kp_t_eq(T, kp_ui_supplement_count(SUPPLEMENT, 3), -1, "no count");
	memcpy(bad, SUPPLEMENT, sizeof bad);
	put_u32(bad, 0xffffffffu);
	kp_t_eq(T, kp_ui_supplement_count(bad, sizeof bad), -1, "a count past the bytes");
	memcpy(bad, SUPPLEMENT, sizeof bad);
	put_u32(bad + 12, 0x416);
	kp_t_eq(T, kp_ui_supplement_count(bad, sizeof bad), -1, "the same code twice");
	memcpy(bad, SUPPLEMENT, sizeof bad);
	put_u32(bad + 12, 0x300);
	kp_t_eq(T, kp_ui_supplement_count(bad, sizeof bad), -1, "codes out of order");
	memcpy(bad, SUPPLEMENT, sizeof bad);
	put_u32(bad + 8, (uint32_t)sizeof bad);
	kp_t_eq(T, kp_ui_supplement_count(bad, sizeof bad), -1, "an offset at the end");
	memcpy(bad, SUPPLEMENT, sizeof bad);
	put_u32(bad + 8, (uint32_t)sizeof bad - 3);
	kp_t_eq(T, kp_ui_supplement_count(bad, sizeof bad), -1, "a glyph's head past the end");
	kp_t_eq(T, kp_ui_text_add_supplement(&t, ui->font_files[0], bad, sizeof bad), -1, "refused");
	kp_t_ok(T, !kp_ui_has_glyph(&t, SUP_FIRST), "none taken");
	/* a glyph whose packets run past the bytes reads nothing past them (the sanitizers' check) */
	{
		enum { W = 16, H = 4 };
		uint8_t pixels[W * H];
		uint8_t *cut = (uint8_t *)malloc(SUP_FIRST_PACKET_END);
		if (!cut) kp_t_fail(T, "out of memory");
		kp_t_defer(T, free, cut);
		memcpy(cut, SUPPLEMENT, SUP_FIRST_PACKET_END);
		plot_text(T, ui, "\xd0\x96", pixels, W, H, cut, SUP_FIRST_PACKET_END);
	}
}

/* --- kp_fnt_supplement --- */

static void fnt_supplement_from_a_truetype_font(KpT *T)
{
	static const int32_t CODES[] = { 0x41d, 0x416, 0x10f000 };
	/* Ж, Н, then р, у, ф: descenders; the last the font lacks */
	static const int32_t ASCENDING[] = { 0x416, 0x41d, 0x440, 0x443, 0x444, 0x10f000 };
	enum { ASCENDING_COUNT = sizeof ASCENDING / sizeof ASCENDING[0], KEPT = ASCENDING_COUNT - 1 };
	const uint8_t *const *files;
	const size_t *sizes;
	uint8_t *out = NULL;
	size_t out_size = 0;
	int32_t missing[ASCENDING_COUNT], missing_count = -1;
	char err[ERROR_SIZE];
	KpUi *ui, *fixture;
	KpUiText t;
	int font;
	if (!kp_t_ready(T, "test_locale.fnt_supplement", REAL_BASE) || !kp_t_ready(T, "test_locale", FIXTURES)) return;
	ui = real_ui(T);
	fixture = kp_t_fixture_ui(T);
	kp_t_eq(T, kp_ui_vector_fonts(ui, &files, &sizes), 2, "ui.kpd's VFNT");
	/* the refusals */
	{
		KpVText *v = (KpVText *)calloc(1, sizeof *v);
		const uint8_t *fnt = ui->font_files[1];
		size_t fnt_size = ui->font_file_sizes[1];
		if (!v) kp_t_fail(T, "out of memory");
		kp_t_defer(T, free, v);
		kp_t_eq(T, kp_vtext_init(v, &fnt, &fnt_size, 1), -1, "the vector text: no TrueType font");
	}
	kp_t_eq(T, kp_fnt_supplement(files[0], sizes[0], ui->font_files[1], ui->font_file_sizes[1], CODES, 3, &out, &out_size,
		missing, &missing_count, err, sizeof err), -1, "codes out of order");
	kp_t_eq(T, kp_fnt_supplement(ui->font_files[1], ui->font_file_sizes[1], ui->font_files[1], ui->font_file_sizes[1],
		ASCENDING, ASCENDING_COUNT, &out, &out_size, missing, &missing_count, err, sizeof err), -1, "no TrueType font");
	kp_t_eq(T, kp_fnt_supplement(files[0], sizes[0], fixture->font_files[0], fixture->font_file_sizes[0], ASCENDING, ASCENDING_COUNT, &out,
		&out_size, missing, &missing_count, err, sizeof err), -1, "an FNT font without Latin letters");
	for (font = 0; font < KP_UI_FNT_FONTS; font++) {
		const uint8_t *fnt = ui->font_files[font];
		int k;
		out = NULL;
		kp_t_eq(T, kp_fnt_supplement(files[0], sizes[0], fnt, ui->font_file_sizes[font], ASCENDING, ASCENDING_COUNT, &out, &out_size, missing,
			&missing_count, err, sizeof err), 0, "%s", err);
		kp_t_defer(T, free, out);
		kp_t_eq(T, missing_count, 1, "font %d: one code the font lacks", font);
		kp_t_eq(T, missing[0], 0x10f000, NULL);
		kp_t_eq(T, kp_ui_supplement_count(out, out_size), KEPT, "a supplement of the others");
		for (k = 0; k < KEPT; k++) kp_t_eq(T, kp_ui_supplement_code(out, k), ASCENDING[k], NULL);
		kp_ui_text_init(&t);
		kp_t_eq(T, kp_ui_text_select_font(&t, fnt, ui->font_file_sizes[font]), 0, NULL);
		kp_t_eq(T, kp_ui_text_add_supplement(&t, fnt, out, out_size), 0, NULL);
		/* U+041D looks as H: the fit takes it to the font's own H */
		{
			int32_t ours = kp_ui_text_width(&t, (const uint8_t *)"\xd0\x9d"), its = kp_ui_char_width(&t, 'H');
			kp_t_ok(T, ours >= its - 2 && ours <= its + 2, "font %d: U+041D %d wide, H %d", font, ours, its);
		}
		/* the glyphs stay in the font's cell, a descender's too */
		for (k = 0; k < KEPT; k++) {
			const uint8_t *entry = out + KP_FNT_SUPPLEMENT_HEAD + (size_t)k * KP_FNT_SUPPLEMENT_ENTRY;
			const uint8_t *glyph = out + kp_le_u32(entry + 4);
			kp_t_ok(T, glyph[1] + glyph[3] <= t.max_height, "font %d: U+%04X's height %d + %d of %d", font,
				(unsigned)ASCENDING[k], glyph[1], glyph[3], t.max_height);
		}
	}
}

/* The masks of an FNT glyph's pixels (its head at `glyph`), as the blitter reads its packets. */
enum { MASKS = 8, MASK_BODY = 1, MASK_EDGE_DARK = 4, MASK_SHADOW_BLACK = 5, MASK_SHADOW = 7, SHADOW_SHARE = 10 };
static void glyph_masks(const uint8_t *glyph, const uint8_t *end, uint32_t counts[MASKS])
{
	int total = glyph[0] * glyph[1], pos = 0;
	const uint8_t *at = glyph + KP_FNT_GLYPH_HEAD;
	while (pos < total && at < end) {
		int skip = *at >> 3, mask = *at & (MASKS - 1);
		at++;
		pos += skip;
		if (pos >= total) break;
		if (skip == 0 || mask != 0) {
			counts[mask]++;
			pos++;
		}
	}
}

static double body_share(const uint32_t counts[MASKS])
{
	uint32_t ink = 0;
	int k;
	for (k = MASK_BODY; k <= MASK_EDGE_DARK; k++) ink += counts[k];
	return ink ? (double)counts[MASK_BODY] / ink : 0.0;
}

static void fnt_supplement_keeps_the_fonts_colours(KpT *T)
{
	/* the Cyrillic letters U+0410..U+044F */
	enum { FIRST = 0x410, COUNT = 64 };
	static const double SHARE_SLACK = 0.1;
	const uint8_t *const *files;
	const size_t *sizes;
	int32_t codes[COUNT], missing[COUNT], missing_count = 0, k;
	char err[ERROR_SIZE];
	KpUi *ui;
	int font;
	if (!kp_t_ready(T, "test_locale.fnt_supplement", REAL_BASE)) return;
	ui = real_ui(T);
	kp_t_eq(T, kp_ui_vector_fonts(ui, &files, &sizes), 2, "ui.kpd's VFNT");
	for (k = 0; k < COUNT; k++) codes[k] = FIRST + k;
	for (font = 0; font < KP_UI_FNT_FONTS; font++) {
		const uint8_t *fnt = ui->font_files[font];
		size_t fnt_size = ui->font_file_sizes[font];
		uint32_t base[MASKS] = { 0 }, ours[MASKS] = { 0 }, total = 0;
		uint8_t *out = NULL;
		size_t out_size = 0;
		int code, shadow = 0;
		kp_t_eq(T, kp_fnt_supplement(files[0], sizes[0], fnt, fnt_size, codes, COUNT, &out, &out_size, missing, &missing_count,
			err, sizeof err), 0, "%s", err);
		kp_t_defer(T, free, out);
		for (code = 'A'; code <= 'z'; code++) { /* its letters: font14's boxes of 0x7f..0xa0 have a black its letters lack */
			uint32_t off = kp_le_u32(fnt + KP_FNT_HEADER + 4 * (code - fnt[4]));
			if ((code <= 'Z' || code >= 'a') && off != 0 && off + KP_FNT_GLYPH_HEAD <= fnt_size) {
				glyph_masks(fnt + off, fnt + fnt_size, base);
			}
		}
		for (k = 0; k < MASKS; k++) total += base[k];
		if (base[MASK_SHADOW] >= total / SHADOW_SHARE) shadow = MASK_SHADOW;
		else if (base[MASK_SHADOW_BLACK] >= total / SHADOW_SHARE) shadow = MASK_SHADOW_BLACK;
		for (k = 0; k < kp_ui_supplement_count(out, out_size); k++) {
			const uint8_t *entry = out + KP_FNT_SUPPLEMENT_HEAD + (size_t)k * KP_FNT_SUPPLEMENT_ENTRY;
			uint32_t glyph[MASKS] = { 0 };
			int m;
			glyph_masks(out + kp_le_u32(entry + 4), out + out_size, glyph);
			for (m = 0; m < MASKS; m++) ours[m] += glyph[m];
			/* the body casts the shadow: a glyph of edges alone has none, as font10's had */
			if (shadow && glyph[MASK_BODY] > 0) {
				kp_t_ok(T, glyph[shadow] > 0, "font %d: U+%04X has no shadow", font, (unsigned)kp_ui_supplement_code(out, k));
			}
		}
		/* the colours of a font's rows go by its masks: our glyphs have as much body as its own, font10's nearly all,
		 * font14's half (a threshold for both drew font10's in its edge colour) */
		if (base[MASK_BODY] < total) {
			double want = body_share(base), got = body_share(ours);
			kp_t_ok(T, got >= want - SHARE_SLACK && got <= want + SHARE_SLACK, "font %d: body %.2f of the ink, its own %.2f",
				font, got, want);
		}
	}
}

/* --- kp_ui_set_locale --- */

/* A table in the original's form: u16 count, count u16 offsets, the rows. */
static size_t narrow_table(uint8_t *out, const char *const *rows, int count)
{
	size_t at = 2 + 2 * (size_t)count;
	int k;
	put_u16(out, (uint32_t)count);
	for (k = 0; k < count; k++) {
		size_t size = strlen(rows[k]) + 1;
		put_u16(out + 2 + 2 * k, (uint32_t)at);
		memcpy(out + at, rows[k], size);
		at += size;
	}
	return at;
}

/* The language's: u32 count, count u32 offsets, the rows; `pad` bytes before them take the offsets past u16. */
static size_t wide_table(uint8_t *out, const char *const *rows, int count, size_t pad)
{
	size_t at = 4 + 4 * (size_t)count + pad;
	int k;
	put_u32(out, (uint32_t)count);
	memset(out + 4 + 4 * count, 'x', pad);
	for (k = 0; k < count; k++) {
		size_t size = strlen(rows[k]) + 1;
		put_u32(out + 4 + 4 * k, (uint32_t)at);
		memcpy(out + at, rows[k], size);
		at += size;
	}
	return at;
}

enum { PAST_U16 = 70000, TABLE_ROOM = 256 };

static const char *const BASE_ROWS[] = { "first", "second" };
static const char *const OUR_ROWS[] = { "\xd0\xbf\xd0\xb5\xd1\x80\xd0\xb2\xd0\xb0\xd1\x8f", "\xd0\xb2\xd1\x82\xd0\xbe\xd1\x80\xd0\xb0\xd1\x8f" };

/* An interface over the fixture's data whose STAT and NETW are BASE_ROWS (the other tables empty). */
static KpUi *strings_ui(KpT *T)
{
	static uint8_t base[TABLE_ROOM];
	size_t table = narrow_table(base, BASE_ROWS, 2);
	KpUiData *d = kp_t_fixture_ui_data(T);
	char err[ERROR_SIZE];
	KpUi *ui;
	int k;
	for (k = 0; k < KP_UI_TBLS; k++) {
		d->tbl[k].data = k < 2 ? base : (const uint8_t *)"\0";
		d->tbl[k].size = k < 2 ? table : 2;
	}
	ui = kp_ui_new_data(d, err, sizeof err);
	kp_t_ok(T, ui != NULL, "%s", err);
	kp_t_defer(T, free_ui, ui);
	return ui;
}

/* The language's STAT over the interface. */
static void language_stat(KpUi *ui, const uint8_t *table, size_t size)
{
	KpUiLocale locale;
	memset(&locale, 0, sizeof locale);
	locale.tbl[0] = table;
	locale.tbl_size[0] = size;
	kp_ui_set_locale(ui, &locale);
}

static void tables_past_u16_replace_the_base(KpT *T)
{
	uint8_t *table;
	size_t size;
	KpUi *ui;
	if (!kp_t_ready(T, "test_locale", FIXTURES)) return;
	ui = strings_ui(T);
	kp_t_eq_str(T, (const char *)kp_ui_tbl(ui, 0, 2), "second", "the base's row");
	table = (uint8_t *)malloc(PAST_U16 + TABLE_ROOM);
	if (!table) kp_t_fail(T, "out of memory");
	kp_t_defer(T, free, table);
	size = wide_table(table, OUR_ROWS, 2, PAST_U16);
	language_stat(ui, table, size);
	kp_t_eq_str(T, ui->locale_problem, "", "accepted");
	kp_t_eq_str(T, (const char *)kp_ui_tbl(ui, 0, 1), OUR_ROWS[0], "the language's first row");
	kp_t_eq_str(T, (const char *)kp_ui_tbl(ui, 0, 2), OUR_ROWS[1], "its second, past u16");
	kp_t_ok(T, kp_ui_tbl(ui, 0, 0) == NULL, "id 0");
	kp_t_eq_str(T, (const char *)kp_ui_tbl(ui, 0, 3), "", "past the count");
	kp_t_eq_str(T, (const char *)kp_ui_tbl(ui, 1, 2), "second", "a table the language lacks stays the base's");
}

static void other_tables_are_refused(KpT *T)
{
	static const char *const THREE[] = { "a", "b", "c" };
	uint8_t table[TABLE_ROOM], narrow[TABLE_ROOM];
	size_t size, narrow_size;
	KpUi *ui;
	if (!kp_t_ready(T, "test_locale", FIXTURES)) return;
	/* another row count */
	ui = strings_ui(T);
	size = wide_table(table, THREE, 3, 0);
	language_stat(ui, table, size);
	kp_t_ok(T, strstr(ui->locale_problem, "row count") != NULL, "%s", ui->locale_problem);
	kp_t_eq_str(T, (const char *)kp_ui_tbl(ui, 0, 2), "second", "the base stays");
	/* the original's form: its count and first offset read as a u32 count */
	ui = strings_ui(T);
	narrow_size = narrow_table(narrow, OUR_ROWS, 2);
	language_stat(ui, narrow, narrow_size);
	kp_t_ok(T, ui->locale_problem[0] != 0, "a table of u16 offsets");
	kp_t_eq_str(T, (const char *)kp_ui_tbl(ui, 0, 2), "second", "the base stays");
	/* offsets past the table read nothing */
	ui = strings_ui(T);
	size = wide_table(table, OUR_ROWS, 2, 0);
	put_u32(table + 8, 0xfffffff0u);
	language_stat(ui, table, size);
	kp_t_eq_str(T, (const char *)kp_ui_tbl(ui, 0, 2), "", "an offset past the table");
	/* a table cut under its count */
	ui = strings_ui(T);
	size = wide_table(table, OUR_ROWS, 2, 0);
	language_stat(ui, table, 7);
	kp_t_ok(T, ui->locale_problem[0] != 0, "no room for the offsets");
	kp_t_eq_str(T, (const char *)kp_ui_tbl(ui, 0, 2), "second", "the base stays");
}

/* A language's dialogs (`dlgs` over the fixture's DLGS, `pool` over its POOL) and files over the interface. */
static void language_ui(KpUi *ui, const uint8_t *dlgs, size_t dlgs_size, const uint8_t *pool, size_t pool_size,
	const KpUiFile *files, int file_count)
{
	KpUiLocale locale;
	memset(&locale, 0, sizeof locale);
	locale.dlgs = dlgs;
	locale.dlgs_size = dlgs_size;
	locale.pool = pool;
	locale.pool_size = pool_size;
	locale.files = files;
	locale.file_count = file_count;
	kp_ui_set_locale(ui, &locale);
}

static uint8_t *copy_section(KpT *T, const char *fourcc, size_t *size)
{
	const uint8_t *s = kp_t_fixture_section(T, "ui.kpd", fourcc, size);
	uint8_t *copy = (uint8_t *)malloc(*size ? *size : 1);
	if (!copy) kp_t_fail(T, "out of memory");
	kp_t_defer(T, free, copy);
	memcpy(copy, s, *size);
	return copy;
}

static void dialogs_take_the_languages_texts(KpT *T)
{
	size_t dlgs_size, pool_size;
	uint8_t *dlgs, *pool, *at;
	KpUi *ui;
	KpUiControl *button;
	int d;
	if (!kp_t_ready(T, "test_locale", FIXTURES)) return;
	dlgs = copy_section(T, "DLGS", &dlgs_size);
	pool = copy_section(T, "POOL", &pool_size);
	at = (uint8_t *)memchr(pool, 'q', pool_size);
	kp_t_ok(T, at != NULL && memcmp(at, "quit", 4) == 0, "the fixture's text");
	memcpy(at, "q\xd0\x96x", 4);
	/* the same dialogs */
	ui = kp_t_fixture_ui(T);
	language_ui(ui, dlgs, dlgs_size, pool, pool_size, NULL, 0);
	kp_t_eq_str(T, ui->locale_problem, "", "accepted");
	d = kp_ui_load_dialog(ui, "rez\\x.bin");
	kp_t_ok(T, d >= 0, "the dialog loads: %s", ui->error);
	button = &ui->controls[ui->controls[d].child];
	kp_t_eq_str(T, (const char *)button->text, "q\xd0\x96x", "the language's text");
	/* other dialogs: the name of the only one */
	ui = kp_t_fixture_ui(T);
	at = (uint8_t *)memchr(dlgs, 'x', dlgs_size);
	kp_t_ok(T, at != NULL, "the dialog's name");
	*at = 'y';
	language_ui(ui, dlgs, dlgs_size, pool, pool_size, NULL, 0);
	kp_t_ok(T, strstr(ui->locale_problem, "other dialogs") != NULL, "%s", ui->locale_problem);
	kp_t_ok(T, ui->bin_count == 1 && kp_ui_load_dialog(ui, "rez\\x.bin") >= 0, "the base's stay");
	kp_t_eq_str(T, (const char *)ui->controls[ui->controls[kp_ui_load_dialog(ui, "rez\\x.bin")].child].text, "quit", "and their text");
	*at = 'x';
	/* cut dialogs */
	ui = kp_t_fixture_ui(T);
	language_ui(ui, dlgs, dlgs_size / 2, pool, pool_size, NULL, 0);
	kp_t_ok(T, strstr(ui->locale_problem, "malformed") != NULL, "%s", ui->locale_problem);
	kp_t_ok(T, ui->bin_count == 1 && kp_ui_load_dialog(ui, "rez\\x.bin") >= 0, "the base's stay");
	/* a POOL whose last text has no NUL */
	ui = kp_t_fixture_ui(T);
	kp_t_ok(T, pool_size >= 2 && pool[pool_size - 1] == 0 && pool[pool_size - 2] != 0, "the fixture's last text");
	language_ui(ui, dlgs, dlgs_size, pool, pool_size - 1, NULL, 0);
	kp_t_ok(T, strstr(ui->locale_problem, "malformed") != NULL, "%s", ui->locale_problem);
	kp_t_eq_str(T, (const char *)ui->controls[ui->controls[kp_ui_load_dialog(ui, "rez\\x.bin")].child].text, "quit", "the base's text");
}

static void scripts_are_read_from_the_language_first(KpT *T)
{
	static const char OURS[] = "glue\\x.pcx", NEW[] = "rez\\n.txt";
	KpUiFile files[2];
	KpUi *ui;
	const KpUiFile *f;
	if (!kp_t_ready(T, "test_locale", FIXTURES)) return;
	ui = kp_t_fixture_ui(T);
	f = kp_ui_glue_file(ui, OURS);
	kp_t_ok(T, f != NULL && f->size == 3 && memcmp(f->data, "abc", 3) == 0, "the base's file");
	kp_t_ok(T, kp_ui_glue_file(ui, NEW) == NULL, "a file the base lacks");
	memset(files, 0, sizeof files);
	files[0].path = NEW;
	files[0].path_len = sizeof NEW - 1;
	files[0].data = (const uint8_t *)"hi";
	files[0].size = 2;
	files[1].path = OURS;
	files[1].path_len = sizeof OURS - 1;
	files[1].data = (const uint8_t *)"wxyz";
	files[1].size = 4;
	language_ui(ui, NULL, 0, NULL, 0, files, 2);
	memset(files, 0, sizeof files); /* the interface keeps its own list */
	kp_t_eq_str(T, ui->locale_problem, "", "accepted");
	f = kp_ui_glue_file(ui, OURS);
	kp_t_ok(T, f != NULL && f->size == 4 && memcmp(f->data, "wxyz", 4) == 0, "the language's before the base's");
	f = kp_ui_glue_file(ui, NEW);
	kp_t_ok(T, f != NULL && f->size == 2 && memcmp(f->data, "hi", 2) == 0, "and its own");
}

static void glyphs_join_every_font_and_replace_the_vector_fonts(KpT *T)
{
	KpUiFile vector[KP_UI_VECTOR_FONTS];
	KpUiLocale locale;
	const uint8_t *const *files;
	const size_t *sizes;
	KpUi *ui;
	uint8_t bad[sizeof SUPPLEMENT];
	int k;
	if (!kp_t_ready(T, "test_locale", FIXTURES)) return;
	ui = kp_t_fixture_ui(T);
	memset(&locale, 0, sizeof locale);
	memset(vector, 0, sizeof vector);
	for (k = 0; k < KP_UI_FNT_FONTS; k++) {
		locale.supplements[k] = SUPPLEMENT;
		locale.supplement_sizes[k] = sizeof SUPPLEMENT;
	}
	vector[0].path = "a.t";
	vector[0].path_len = 3;
	vector[0].data = (const uint8_t *)"v0";
	vector[0].size = 2;
	vector[1].path = "b.t";
	vector[1].path_len = 3;
	vector[1].data = (const uint8_t *)"v1";
	vector[1].size = 2;
	locale.vector_fonts = vector;
	locale.vector_font_count = KP_UI_VECTOR_FONTS;
	kp_ui_set_locale(ui, &locale);
	kp_t_eq_str(T, ui->locale_problem, "", "accepted");
	kp_t_eq(T, ui->supplement_code_count, 2, "the code points");
	kp_t_ok(T, ui->supplement_codes[0] == SUP_FIRST && ui->supplement_codes[1] == SUP_SECOND, "in order");
	for (k = 0; k < KP_UI_FNT_FONTS; k++) {
		kp_t_eq(T, kp_ui_text_select_font(&ui->text, ui->font_files[k], ui->font_file_sizes[k]), 0, NULL);
		kp_t_ok(T, kp_ui_has_glyph(&ui->text, SUP_SECOND), "font %d", k);
	}
	kp_t_eq(T, kp_ui_vector_fonts(ui, &files, &sizes), 2, "the language's vector fonts");
	kp_t_ok(T, sizes[0] == 2 && memcmp(files[0], "v0", 2) == 0 && sizes[1] == 2 && memcmp(files[1], "v1", 2) == 0, "their bytes");
	/* one malformed font: refused, the others stay */
	ui = kp_t_fixture_ui(T);
	memcpy(bad, SUPPLEMENT, sizeof bad);
	put_u32(bad, 0xffffu);
	locale.supplements[1] = bad;
	locale.vector_fonts = NULL;
	locale.vector_font_count = 0;
	kp_ui_set_locale(ui, &locale);
	kp_t_ok(T, strstr(ui->locale_problem, "malformed") != NULL, "%s", ui->locale_problem);
	kp_t_eq(T, kp_ui_text_select_font(&ui->text, ui->font_files[1], ui->font_file_sizes[1]), 0, NULL);
	kp_t_ok(T, !kp_ui_has_glyph(&ui->text, SUP_SECOND), "the malformed one's font");
	kp_t_eq(T, kp_ui_text_select_font(&ui->text, ui->font_files[2], ui->font_file_sizes[2]), 0, NULL);
	kp_t_ok(T, kp_ui_has_glyph(&ui->text, SUP_SECOND), "the next font's");
}

/* --- the language built from the install --- */

static const char *const TABLE_NAMES[KP_UI_TBLS] = { "STAT", "NETW", "GLUA", "GHST", "TIPS", "HELP" };
static const char *const FONT_NAMES[KP_UI_FNT_FONTS] = { "U8", "U10", "U14", "U16", "U16X" };

/* data's SHA-256 is the hexadecimal string of `digests` under key. */
static void same_digest(KpT *T, const uint8_t *data, size_t size, const KpJson *digests, const char *key, const char *what)
{
	uint8_t digest[KP_SHA256_SIZE];
	char hex[2 * KP_SHA256_SIZE + 1];
	kp_t_ok(T, data != NULL, "%s: %s is built", what, key);
	kp_sha256(data, size, digest);
	kp_sha256_hex(digest, hex);
	kp_t_eq_str(T, hex, kp_json_field_str(digests, key, ""), "%s: %s", what, key);
}

/* A supplement's code points as the golden writes them: hexadecimal ranges, "102-107,10c". */
static void code_ranges(KpT *T, const uint8_t *s, size_t size, KpBuf *out)
{
	int32_t n = kp_ui_supplement_count(s, size), k = 0;
	kp_t_ok(T, n > 0, "a supplement's code points");
	while (k < n) {
		int32_t j = k;
		while (j + 1 < n && kp_ui_supplement_code(s, j + 1) == kp_ui_supplement_code(s, j) + 1) j++;
		kp_buf_printf(out, "%s%x", k ? "," : "", (unsigned)kp_ui_supplement_code(s, k));
		if (j > k) kp_buf_printf(out, "-%x", (unsigned)kp_ui_supplement_code(s, j));
		k = j + 1;
	}
}

/* Acceptance of stage C3: each language the converter wrote (the golden's, the install's) built from the install's own
 * files equals its overlays byte for byte: the tables, the dialogs, the scripts, the subtitles, every campaign mission's
 * strings, and the glyphs (its supplements of the same code points, the same TrueType fonts). */
static void the_languages_equal_the_converters(KpT *T)
{
	static const char *const NEED[] = { CONVERTER, NULL };
	const KpJson *golden, *locales, *glyphs;
	Install in;
	int l, k;
	if (!kp_t_ready(T, "test_locale.converter", NEED)) return;
	install(T, &in);
	golden = kp_t_golden(T, CONVERTER);
	locales = kp_json_get(golden, "locales");
	glyphs = kp_json_get(golden, "glyphs");
	kp_t_ok(T, kp_json_len(locales) > 0 && glyphs != NULL, "the golden's languages and glyphs");
	for (l = 0; l < kp_json_len(locales); l++) {
		const char *locale = locales->keys[l];
		const KpJson *g = locales->items[l], *scripts = kp_json_get(g, "scripts"), *missions = kp_json_get(g, "missions");
		const KpLocale *built_l;
		const KpLocaleGlyphs *gl;
		int mark = kp_t_mark(T), translated = 0;
		kp_t_ok(T, kp_locale_held(&in.raw, locale), "the install holds %s", locale);
		built_l = built(T, &in, locale);
		for (k = 0; k < KP_UI_TBLS; k++) {
			same_digest(T, built_l->tbl[k], built_l->tbl_size[k], kp_json_get(g, "tables"), TABLE_NAMES[k], locale);
		}
		same_digest(T, built_l->dlgs, built_l->dlgs_size, g, "dlgs", locale);
		same_digest(T, built_l->pool, built_l->pool_size, g, "pool", locale);
		kp_t_eq(T, built_l->script_count, kp_json_len(scripts), "%s's scripts", locale);
		for (k = 0; k < built_l->script_count; k++) {
			const KpUiFile *f = &built_l->scripts[k];
			char name[64];
			snprintf(name, sizeof name, "%.*s", (int)f->path_len, f->path);
			same_digest(T, f->data, f->size, scripts, name, locale);
		}
		if (kp_json_get(g, "subs") != NULL) same_digest(T, built_l->subs, built_l->subs_size, g, "subs", locale);
		else kp_t_ok(T, built_l->subs == NULL, "%s has no subtitles", locale);
		kp_t_eq(T, kp_json_len(missions), CAMPAIGN_MISSIONS, "%s's missions", locale);
		for (k = 0; k < kp_json_len(missions); k++) {
			const char *name = missions->keys[k];
			int counts[3] = { 0, 0, 0 }, at = kp_t_mark(T);
			KpMission *m = data_mission(T, in.d, name);
			uint8_t *strs;
			size_t size;
			strs = language_strings(T, &in, locale, name, m, &size, counts);
			kp_t_defer(T, free, strs);
			same_digest(T, strs, size, missions, name, locale);
			kp_t_release(T, at);
			translated += counts[0];
		}
		kp_t_ok(T, translated > 1000, "%s's missions' strings translated: %d", locale, translated);
		/* the glyphs of the text of each language are the converter's of them all */
		gl = glyphs_of(T, &in, built_l);
		{
			KpBuf *codes = kp_t_buf(T);
			code_ranges(T, gl->supplements[0], gl->supplement_sizes[0], codes);
			kp_t_eq_str(T, (const char *)kp_buf_text(codes), kp_json_field_str(glyphs, "codes", ""), "%s's glyphs' code points", locale);
		}
		for (k = 0; k < KP_UI_FNT_FONTS; k++) {
			same_digest(T, gl->supplements[k], gl->supplement_sizes[k], kp_json_get(glyphs, "fonts"), FONT_NAMES[k], "a supplement");
		}
		for (k = 0; k < KP_UI_VECTOR_FONTS; k++) {
			char name[64];
			snprintf(name, sizeof name, "%.*s", (int)gl->vector_fonts[k].path_len, gl->vector_fonts[k].path);
			same_digest(T, gl->vector_fonts[k].data, gl->vector_fonts[k].size, kp_json_get(glyphs, "vfnt"), name, "a vector font");
		}
		kp_t_log(T, "%s: %d scripts, %d subtitled movies, %d missions' strings translated, %d glyphs", locale,
			built_l->script_count, built_l->subtitles, translated, gl->code_count);
		kp_t_release(T, mark);
	}
}

/* Every character of a text is valid UTF-8 and drawn by every font: a byte no sequence takes would be read as a slot,
 * a code point with no glyph would have no width. */
static void check_text(KpT *T, KpUi *ui, const char *where, const uint8_t *text)
{
	size_t n = strlen((const char *)text), at = 0;
	while (at < n) {
		int32_t cp = kp_utf8_next(text, n, &at);
		int k;
		if (cp == KP_UTF8_BAD) kp_t_fail(T, "%s: an invalid byte at %d of \"%s\"", where, (int)at, (const char *)text);
		if (cp <= ' ') continue; /* a colour, layout or the space, which has its width */
		for (k = 0; k < KP_UI_FNT_FONTS; k++) {
			if (kp_ui_text_select_font(&ui->text, ui->font_files[k], ui->font_file_sizes[k]) != 0) kp_t_fail(T, "font %d", k);
			if (!kp_ui_has_glyph(&ui->text, cp)) kp_t_fail(T, "%s: font %d has no glyph for U+%04X", where, k, (unsigned)cp);
		}
	}
}

/* A word of the language: ruRU's and plPL's have characters past the FNT's slots (the glyph supplement's). */
static const char *language_word(const char *locale)
{
	if (strcmp(locale, "ruRU") == 0) return "\xd0\x9c\xd0\x95\xd0\x9d\xd0\xae"; /* МЕНЮ */
	if (strcmp(locale, "plPL") == 0) return "\xc5\x81\xc3\xb3\x64\xc5\xba";          /* Łódź */
	return "Gr\xc3\xb6\xc3\x9f" "e";                                                    /* Größe: the FNT's own slots */
}

/* The acceptance's zero invalid bytes, past what a menu run reaches: every row of every table and every dialog's text. */
static void language_text_is_valid_and_has_glyphs(KpT *T, const Install *in, const char *locale, void *ctx)
{
	(void)ctx;
	KpUi *ui;
	int table, read = 0;
	uint32_t id;
	ui = real_language(T, in, locale);
	for (table = 0; table < KP_UI_TBLS; table++) {
		const uint8_t *t = ui->tbl[table];
		uint32_t count;
		kp_t_ok(T, t != NULL && ui->tbl_wide[table], "table %d: the language's", table);
		count = kp_le_u32(t);
		for (id = 1; id <= count; id++) {
			const uint8_t *row = kp_ui_tbl(ui, table, id);
			char where[64];
			snprintf(where, sizeof where, "table %d row %u", table, id);
			check_text(T, ui, where, row);
			read++;
		}
	}
	kp_t_ok(T, read > 1500, "rows read: %d", read);
	{
		size_t at = 0, n = ui->pool_size;
		int texts = 0;
		while (at < n) {
			const uint8_t *text = ui->pool + at;
			size_t size = strlen((const char *)text);
			check_text(T, ui, "a dialog's text", text);
			at += size + 1;
			texts++;
		}
		kp_t_ok(T, texts > 300, "dialog texts read: %d", texts);
	}
	/* a word of the language measures (one with a character past the FNT's slots where the language has one) */
	kp_t_eq(T, kp_ui_text_select_font(&ui->text, ui->font_files[1], ui->font_file_sizes[1]), 0, NULL);
	kp_t_ok(T, kp_ui_text_width(&ui->text, (const uint8_t *)language_word(locale)) > 10, "%s's word has a width", locale);
}

static void the_languages_text_is_valid_and_has_glyphs(KpT *T)
{
	Install in;
	install(T, &in);
	for_each_language(T, &in, "test_locale.real_language", language_text_is_valid_and_has_glyphs, NULL);
}

/* --- the widths of a language's texts (localization's stage L3) --- */

#define CUTS_MAX 4096
#define CUT_TEXT 96
#define WIDTHS_REPORT "generated/reports/widths-%s.json"
#define MISSION_WIDTHS_REPORT "generated/reports/widths-%s-missions.json"
#define BASE_WIDTHS_REPORT "generated/reports/widths-enUS.json" /* the base's own cuts, which a language may keep */
#define FIXTURE_DIALOG_W 200
#define FIXTURE_DIALOG_H 100
#define FIXTURE_TEXT "q  " /* the fixture button's hotkey, then spaces */
#define TOOLTIP_LINES (KP_UI_TOOLTIP_H + 1) /* more lines than the tooltip has pixels: past it in any font */
#define GLUE_BUTTON 14 /* a glue screen's FLC or text button (the briefing's portraits have movies) */
#define TOOLTIP_TEXT_X 2 /* kp_ui_tooltip.c's text from x 2 */
#define STATUS_DIALOG "rez\\statdata.bin"
#define KILLS_NUMBER " 9999" /* the widest count a status line shows after its label (kp_ui_status.c's MAX_SHOWN) */
#define SUPPLY_NUMBER " 200" /* the most supply a line counts */
#define STATUS_LINE 192
#define UNIT_TYPES 228

/* A control's text that did not fit its clip: its dialog, its ID and the text. */
typedef struct {
	char dialog[40];
	int row;                /* the table row measured in the control, -1 the control's own text */
	int index, id;          /* the control's place among the dialog's children (-1 a tooltip), its ID */
	int width, rect[4];     /* the text's width in the control's font, the control's rectangle */
	char text[CUT_TEXT];
} Cut;

typedef struct {
	Cut v[CUTS_MAX];
	int n;
	const char *dialog;
	int32_t root;
	int row;
} Cuts;

static void on_cut(void *ctx, KpUi *ui, int32_t control, const uint8_t *text, size_t at)
{
	Cuts *cuts = (Cuts *)ctx;
	Cut *k;
	int32_t i;
	(void)at;
	if (cuts->n == CUTS_MAX) return;
	k = &cuts->v[cuts->n++];
	memset(k, 0, sizeof *k);
	snprintf(k->dialog, sizeof k->dialog, "%s", cuts->dialog);
	k->row = cuts->row;
	k->index = k->id = -1;
	k->width = kp_ui_text_width(&ui->text, text);
	if (control >= 0) {
		const KpUiControl *c = &ui->controls[control];
		k->id = (int16_t)c->id;
		k->index = 0;
		for (i = ui->controls[cuts->root].child; i >= 0 && i != control; i = ui->controls[i].next) k->index++;
		k->rect[0] = c->left;
		k->rect[1] = c->top;
		k->rect[2] = c->right;
		k->rect[3] = c->bottom;
	} else {
		k->rect[0] = TOOLTIP_TEXT_X;
		k->rect[2] = KP_UI_TOOLTIP_W - 1;
		k->rect[3] = KP_UI_TOOLTIP_H - 1;
	}
	kp_text_copy(k->text, sizeof k->text, (const char *)text);
}

/* The multiplayer's dialogs, which the product never opens (no engine path loads them): their texts are not measured. */
static const char *const UNOPENED[] = { "rez\\allyfltr.bin", "rez\\teamfltr.bin", "rez\\netdlg.bin", NULL };

static int unopened(const char *name)
{
	int k;
	for (k = 0; UNOPENED[k] != NULL; k++) {
		if (strcmp(UNOPENED[k], name) == 0) return 1;
	}
	return 0;
}

/* The glue's dialogs draw with the glue's font set, the others with the game's (kp_ui_font_set). */
static int glue_dialog(const char *name)
{
	return strncmp(name, "rez\\glu", 7) == 0 || strncmp(name, "koprulu\\", 8) == 0;
}

/* The stat_txt rows kp_ui_status.c sets into statdata.bin's text controls (its S_ constants and control IDs), a count
 * after the label where the panel prints one. */
typedef struct {
	int16_t id;
	uint16_t first, count;
	const char *after; /* what the panel prints after the label, NULL nothing */
} StatusRows;

static const StatusRows STATUS_ROWS[] = {
	{ -5, 1, UNIT_TYPES, NULL },                                                  /* C_NAME: the unit types' names */
	{ -20, 0x340, 1, NULL }, { -20, 0x33c, 3, NULL }, { -20, 0x514, 1, " (9)" },  /* C_STATUS: hallucination, disabled, acid spores */
	{ -20, 0x513, 1, NULL }, { -20, 0x33b, 1, NULL }, { -20, 0x33f, 1, NULL },    /* blind, parasite, detector */
	{ -20, 0x23b, 2, NULL }, { -20, 0x516, 23, NULL },                            /* the heroes' labels, the ranks */
	{ -21, 0x2fc, 1, KILLS_NUMBER },                                              /* C_KILLS */
	{ -10, 0x335, 3, SUPPLY_NUMBER }, { -11, 0x32f, 3, SUPPLY_NUMBER },           /* C_SUPPLY_1..4 */
	{ -12, 0x332, 3, SUPPLY_NUMBER }, { -13, 0x338, 3, SUPPLY_NUMBER },
	{ -11, 0x31c, 2, KILLS_NUMBER }, { -11, 0x31e, 1, NULL },                     /* minerals and gas left, depleted */
	{ -15, 0x2fd, 12, NULL }, { -30, 0x31f, 3, NULL }, { -30, 0x316, 1, NULL },   /* the queue's labels, under construction, summoning */
	{ -35, 0x2fd, 12, NULL }, { -40, 0x313, 3, NULL }, { -40, 0x303, 3, NULL },   /* research, adding on, morphing */
};

/* A dialog painted with only control c shown, its text `text`. */
static void paint_alone(KpUi *ui, int32_t d, int32_t c, const uint8_t *text)
{
	int32_t o;
	for (o = ui->controls[d].child; o >= 0; o = ui->controls[o].next) ui->controls[o].flags &= ~KP_UI_VISIBLE;
	ui->controls[c].text = text;
	ui->controls[c].flags |= KP_UI_VISIBLE;
	kp_ui_paint_dialog(ui, d);
	ui->controls[c].text = NULL;
}

/* Each row of STATUS_ROWS painted in its control of the status panel, in the game's font set. */
static void status_texts(KpT *T, KpUi *ui, Cuts *cuts)
{
	size_t k;
	int32_t d;
	kp_ui_font_set(ui, 1);
	cuts->dialog = STATUS_DIALOG;
	d = kp_ui_load_dialog(ui, STATUS_DIALOG);
	if (d < 0) kp_t_fail(T, "%s: %s", STATUS_DIALOG, ui->error);
	cuts->root = d;
	kp_ui_init_dialog(ui, d, NULL);
	for (k = 0; k < sizeof STATUS_ROWS / sizeof STATUS_ROWS[0]; k++) {
		const StatusRows *r = &STATUS_ROWS[k];
		int32_t c = kp_ui_find_control(ui, d, r->id), i;
		if (c < 0) kp_t_fail(T, "%s has no control %d", STATUS_DIALOG, r->id);
		for (i = 0; i < r->count; i++) {
			char line[STATUS_LINE];
			const uint8_t *row = kp_ui_tbl(ui, KP_UI_STAT_TXT, (uint32_t)(r->first + i));
			snprintf(line, sizeof line, "%s%s", row != NULL ? (const char *)row : "", r->after != NULL ? r->after : "");
			cuts->row = r->first + i;
			paint_alone(ui, d, c, (const uint8_t *)line);
		}
	}
	kp_ui_destroy_dialog(ui, d);
}

/* Every stat_txt row as a tooltip (kp_ui_tooltip_text, the game's font set): the buttons' and the status panel's. */
static void tooltip_texts(KpUi *ui, Cuts *cuts)
{
	const uint8_t *table = ui->tbl[KP_UI_STAT_TXT];
	uint32_t id, count = ui->tbl_wide[KP_UI_STAT_TXT] ? kp_le_u32(table) : kp_le_u16(table); /* the base's is u16 */
	kp_ui_font_set(ui, 1);
	kp_ui_tooltip_init(ui);
	cuts->dialog = "tooltip";
	for (id = 1; id <= count; id++) {
		const uint8_t *row = kp_ui_tbl(ui, KP_UI_STAT_TXT, id);
		if (row == NULL) continue;
		cuts->row = (int)id;
		kp_ui_tooltip_text(ui, 0, 0, row);
	}
}

/* Every dialog of the interface painted with all its controls shown, the texts that do not fit told to `cuts`. */
static void cut_texts(KpT *T, KpUi *ui, Cuts *cuts)
{
	int b;
	cuts->n = 0;
	ui->text_cut = on_cut;
	ui->text_cut_ctx = cuts;
	for (b = 0; b < ui->bin_count; b++) {
		char name[64];
		int32_t d, c;
		snprintf(name, sizeof name, "%.*s", (int)ui->bins[b].name_len, ui->bins[b].name);
		if (unopened(name)) continue;
		cuts->dialog = name;
		kp_ui_font_set(ui, !glue_dialog(name));
		d = kp_ui_load_dialog(ui, name);
		if (d < 0) kp_t_fail(T, "%s: %s", name, ui->error);
		cuts->root = d;
		kp_ui_init_dialog(ui, d, NULL);
		for (c = ui->controls[d].child; c >= 0; c = ui->controls[c].next) {
			KpUiControl *k = &ui->controls[c];
			k->flags |= KP_UI_VISIBLE;
			if (glue_dialog(name) && k->type == GLUE_BUTTON && k->text != NULL) {
				/* a glue screen's button draws its text in its response rectangle (DrawFlcButtonText 0x4c8650) */
				KpUiEvent e;
				memset(&e, 0, sizeof e);
				e.type = KP_UI_USER;
				e.code = KP_UI_POST_LOAD;
				(void)kp_ui_text_button_proc(ui, k, &e);
			}
		}
		cuts->row = -1;
		kp_ui_paint_dialog(ui, d);
		kp_ui_destroy_dialog(ui, d);
	}
	status_texts(T, ui, cuts);
	tooltip_texts(ui, cuts);
	ui->text_cut = NULL;
}

static int has_cut(const Cuts *cuts, const Cut *k)
{
	int i;
	for (i = 0; i < cuts->n; i++) {
		if (cuts->v[i].index == k->index && cuts->v[i].row == k->row && strcmp(cuts->v[i].dialog, k->dialog) == 0) return 1;
	}
	return 0;
}

static void free_cuts(void *p) { free(p); }

/* The cuts of `cuts` that `except` lacks (all of them with NULL) as JSON at `path`; their count. */
static int write_cuts(const char *path, const char *locale, const Cuts *cuts, const Cuts *except)
{
	FILE *f = fopen(path, "w");
	int i, written = 0;
	if (f != NULL) fprintf(f, "{\"locale\": \"%s\", \"cut\": [", locale);
	for (i = 0; i < cuts->n; i++) {
		const Cut *k = &cuts->v[i];
		const char *ch;
		if (except != NULL && has_cut(except, k)) continue;
		written++;
		if (f == NULL) continue;
		fprintf(f, "%s\n  {\"dialog\": \"", written > 1 ? "," : "");
		for (ch = k->dialog; *ch; ch++) fprintf(f, *ch == '\\' ? "\\\\" : "%c", *ch);
		fprintf(f, "\", \"row\": %d, \"control\": %d, \"id\": %d, \"width\": %d, \"rect\": [%d, %d, %d, %d], \"text\": \"",
			k->row, k->index, k->id, k->width, k->rect[0], k->rect[1], k->rect[2], k->rect[3]);
		for (ch = k->text; *ch; ch++) {
			if (*ch == '"' || *ch == '\\') fprintf(f, "\\%c", *ch);
			else if ((unsigned char)*ch < ' ') fprintf(f, "\\u%04x", (unsigned char)*ch);
			else fputc(*ch, f);
		}
		fprintf(f, "\"}");
	}
	if (f != NULL) {
		fprintf(f, "\n]}\n");
		fclose(f);
	}
	return written;
}

/* The base's cuts, which every language is held to, and the languages' new ones: each language writes its report before
 * the test fails, so one run lists them all. */
typedef struct {
	const Cuts *base;
	int fresh;
	char languages[64];
} LanguageCuts;

static void note_fresh(LanguageCuts *lc, const char *locale, int fresh)
{
	if (fresh == 0) return;
	lc->fresh += fresh;
	snprintf(lc->languages + strlen(lc->languages), sizeof lc->languages - strlen(lc->languages), " %s", locale);
}

/* Every dialog's text of the language fits its control where the base's does: the controls the base's text overflows
 * too (the original's own) are not the language's. The new ones go to generated/reports/widths-<locale>.json. */
static void language_texts_fit(KpT *T, const Install *in, const char *locale, void *ctx)
{
	LanguageCuts *lc = (LanguageCuts *)ctx;
	Cuts *language;
	char report[96];
	int fresh;
	language = (Cuts *)calloc(1, sizeof *language);
	if (language == NULL) kp_t_fail(T, "out of memory");
	kp_t_defer(T, free_cuts, language);
	cut_texts(T, real_language(T, in, locale), language);
	snprintf(report, sizeof report, WIDTHS_REPORT, locale);
	fresh = write_cuts(report, locale, language, lc->base);
	kp_t_ok(T, language->n < CUTS_MAX && lc->base->n < CUTS_MAX, "cuts: %d, %d", lc->base->n, language->n);
	kp_t_log(T, "%s: %d texts do not fit, listed in %s", locale, fresh, report);
	note_fresh(lc, locale, fresh);
}

static void the_languages_texts_fit(KpT *T)
{
	Cuts *base_cuts;
	LanguageCuts lc;
	Install in;
	install(T, &in);
	base_cuts = (Cuts *)calloc(1, sizeof *base_cuts);
	if (base_cuts == NULL) kp_t_fail(T, "out of memory");
	kp_t_defer(T, free_cuts, base_cuts);
	cut_texts(T, ui_over(T, &in.d->ui), base_cuts);
	(void)write_cuts(BASE_WIDTHS_REPORT, "enUS", base_cuts, NULL);
	memset(&lc, 0, sizeof lc);
	lc.base = base_cuts;
	for_each_language(T, &in, "test_locale.real_language", language_texts_fit, &lc);
	kp_t_eq(T, lc.fresh, 0, "texts that do not fit, listed in " WIDTHS_REPORT " of:%s", "<locale>", lc.languages);
}

/* The hook of the width report (KpUiTextCut): a control's text past its clip is told with the control and where the
 * layout stopped, one that fits is not; a tooltip's is told as control -1. The fixture's font has no letters (a
 * text stops at one), so the texts are spaces and run past the bottom. */
static void a_text_that_does_not_fit_is_told(KpT *T)
{
	char lines[2 * TOOLTIP_LINES + 1];
	Cuts *cuts;
	KpUi *ui;
	KpUiControl *k;
	int32_t d, c;
	int i;
	if (!kp_t_ready(T, "test_locale", FIXTURES)) return;
	cuts = (Cuts *)calloc(1, sizeof *cuts);
	if (cuts == NULL) kp_t_fail(T, "out of memory");
	kp_t_defer(T, free_cuts, cuts);
	ui = kp_t_fixture_ui(T);
	ui->text_cut = on_cut;
	ui->text_cut_ctx = cuts;
	cuts->dialog = "rez\\x.bin";
	cuts->row = -1;
	kp_ui_font_set(ui, 0);
	d = kp_ui_load_dialog(ui, "rez\\x.bin");
	kp_t_ok(T, d >= 0, "%s", ui->error);
	ui->controls[d].right = FIXTURE_DIALOG_W - 1; /* the fixture's root has no size: its bitmap would hold nothing */
	ui->controls[d].bottom = FIXTURE_DIALOG_H - 1;
	ui->controls[d].words36[0] = FIXTURE_DIALOG_W; /* the bitmap's size, as InitDialog reads it */
	ui->controls[d].words36[1] = FIXTURE_DIALOG_H;
	kp_ui_init_dialog(ui, d, NULL);
	cuts->root = d;
	c = ui->controls[d].child;
	k = &ui->controls[c];
	k->bottom = (int16_t)(k->top + FIXTURE_DIALOG_H / 2);
	paint_alone(ui, d, c, (const uint8_t *)FIXTURE_TEXT);
	kp_t_eq(T, cuts->n, 0, "a text that fits");
	k->bottom = k->top; /* not a line's height */
	paint_alone(ui, d, c, (const uint8_t *)FIXTURE_TEXT);
	kp_t_eq(T, cuts->n, 1, "a text past its control");
	kp_t_eq(T, cuts->v[0].index, 0, "the dialog's first control");
	kp_t_eq(T, cuts->v[0].id, k->id, NULL);
	kp_t_eq_str(T, cuts->v[0].text, &FIXTURE_TEXT[1], "the text after its hotkey");
	kp_ui_destroy_dialog(ui, d);
	kp_ui_tooltip_init(ui);
	kp_ui_tooltip_text(ui, 0, 0, (const uint8_t *)&FIXTURE_TEXT[1]);
	kp_t_eq(T, cuts->n, 1, "a tooltip that fits");
	for (i = 0; i < TOOLTIP_LINES; i++) {
		lines[2 * i] = ' ';
		lines[2 * i + 1] = '\n';
	}
	lines[2 * TOOLTIP_LINES] = 0;
	kp_ui_tooltip_text(ui, 0, 0, (const uint8_t *)lines);
	kp_t_eq(T, cuts->n, 2, "lines past the tooltip's height");
	kp_t_eq(T, cuts->v[1].index, -1, "a tooltip is no control");
	ui->text_cut = NULL;
}

/* --- the widths of the missions' texts --- */

/* The campaign's missions of a data's catalog, a name per line ("terran/terran01"), as a NUL-ended text the test owns. */
static char *mission_index(KpT *T, const KpData *d)
{
	KpBuf *b = kp_t_buf(T);
	int k;
	for (k = 0; k < d->mission_count; k++) {
		if (d->missions[k].catalog) kp_buf_printf(b, "%s\n", d->missions[k].name);
	}
	return (char *)kp_buf_text(b);
}

#define TRIGGER_SIZE 2400
#define CONDITIONS_SIZE 320 /* 16 conditions of 20 bytes before a trigger's actions */
#define ACTION_SIZE 32
#define ACTIONS 64
#define ACTION_STRING 4
#define ACTION_TIME 0x0c     /* u32 milliseconds: all a speech changes */
#define ACTION_ID 26
#define TRIG_OBJECTIVES 12   /* Set Mission Objectives */
#define MBRF_TEXT 3          /* Text Message */
#define MBRF_OBJECTIVES 4    /* Mission Objectives */
#define MBRF_TRANSMISSION 8
#define UNIT_NAMES_AT 3192   /* UNIS, UNIx: 228 default flags, hit points u32, shields, armour, time, minerals, gas, names */
#define TEXT_HEIGHTS 20      /* kp_ui_briefing.c's transcript bitmap: its control's height 20 times */
#define TRANSCRIPT_FONT 1    /* and its font and format (text_show) */
#define TRANSCRIPT_FORMAT 0x11
#define ROOM_TEXT (-10)
#define ROOM_OBJECTIVES (-11)
#define OBJECTIVES_DIALOG "rez\\objctdlg.bin"
#define OBJECTIVES_TEXT 2
#define MISSION_TEXTS 512

static const char *const ROOMS[] = { "rez\\glurdyz.bin", "rez\\glurdyt.bin", "rez\\glurdyp.bin" };

enum { PLACE_OBJECTIVES, PLACE_TRANSCRIPT, PLACE_NAME };

/* A map string a mission shows and where. */
typedef struct {
	uint16_t id;
	int place;
} MissionText;

/* A string of a CHK STR (u16 count, u16 offsets by id from 1), NULL past it. */
static const uint8_t *chk_string(const uint8_t *strs, size_t n, uint32_t id)
{
	uint32_t at;
	if (strs == NULL || n < 2 || id == 0 || id > kp_le_u16(strs) || 2 * (size_t)id + 2 > n) return NULL;
	at = kp_le_u16(strs + 2 * id);
	return at < n && memchr(strs + at, 0, n - at) != NULL ? strs + at : NULL;
}

static void add_text(MissionText *v, int *n, uint32_t id, int place)
{
	int k;
	if (id == 0 || id > 0xffffu || *n == MISSION_TEXTS) return;
	for (k = 0; k < *n; k++) {
		if (v[k].id == id && v[k].place == place) return;
	}
	v[*n].id = (uint16_t)id;
	v[(*n)++].place = place;
}

/* The objectives, the transcript's texts and the units' names a mission's map strings give. */
static int mission_texts(const KpMission *m, MissionText *v)
{
	int n = 0, k;
	uint32_t at, a;
	for (at = 0; at + TRIGGER_SIZE <= m->triggers.size; at += TRIGGER_SIZE) {
		for (a = 0; a < ACTIONS; a++) {
			const uint8_t *r = m->triggers.data + at + CONDITIONS_SIZE + a * ACTION_SIZE;
			if (r[ACTION_ID] == TRIG_OBJECTIVES) add_text(v, &n, kp_le_u32(r + ACTION_STRING), PLACE_OBJECTIVES);
		}
	}
	for (at = 0; at + TRIGGER_SIZE <= m->briefing.size; at += TRIGGER_SIZE) {
		for (a = 0; a < ACTIONS; a++) {
			const uint8_t *r = m->briefing.data + at + CONDITIONS_SIZE + a * ACTION_SIZE;
			if (r[ACTION_ID] == MBRF_OBJECTIVES) add_text(v, &n, kp_le_u32(r + ACTION_STRING), PLACE_OBJECTIVES);
			if (r[ACTION_ID] == MBRF_TEXT || r[ACTION_ID] == MBRF_TRANSMISSION) {
				add_text(v, &n, kp_le_u32(r + ACTION_STRING), PLACE_TRANSCRIPT);
			}
		}
	}
	for (k = 0; k < m->rule_count; k++) {
		const KpMissionRule *r = &m->rules[k];
		int t;
		if (strcmp(r->tag, "UNIS") != 0 && strcmp(r->tag, "UNIx") != 0) continue;
		for (t = 0; t < UNIT_TYPES && UNIT_NAMES_AT + 2 * (uint32_t)(t + 1) <= r->data.size; t++) {
			if (r->data.data[t] == 0) add_text(v, &n, kp_le_u16(r->data.data + UNIT_NAMES_AT + 2 * t), PLACE_NAME);
		}
	}
	return n;
}

/* A text in a control of a dialog painted alone, in the game's font set or the glue's. */
static void measure_in(KpT *T, KpUi *ui, Cuts *cuts, const char *bin, int16_t id, int game, const uint8_t *text)
{
	int32_t d, c;
	kp_ui_font_set(ui, game);
	d = kp_ui_load_dialog(ui, bin);
	if (d < 0) kp_t_fail(T, "%s: %s", bin, ui->error);
	kp_ui_init_dialog(ui, d, NULL);
	cuts->root = d;
	c = kp_ui_find_control(ui, d, id);
	if (c < 0) kp_t_fail(T, "%s has no control %d", bin, id);
	paint_alone(ui, d, c, text);
	kp_ui_destroy_dialog(ui, d);
}

/* A text laid out as the briefing room's transcript (kp_ui_briefing.c's text_show): into its control's width and 20
 * times its height, in the glue's font 1. */
static void measure_transcript(KpT *T, KpUi *ui, Cuts *cuts, const char *bin, const uint8_t *text)
{
	int32_t d, c;
	const KpUiControl *k;
	KpUiText *t = &ui->text;
	size_t at;
	kp_ui_font_set(ui, 0);
	d = kp_ui_load_dialog(ui, bin);
	if (d < 0) kp_t_fail(T, "%s: %s", bin, ui->error);
	cuts->root = d;
	c = kp_ui_find_control(ui, d, ROOM_TEXT);
	if (c < 0) kp_t_fail(T, "%s has no control %d", bin, ROOM_TEXT);
	k = &ui->controls[c];
	(void)kp_ui_text_select_font(t, ui->fonts[TRANSCRIPT_FONT], ui->font_sizes[TRANSCRIPT_FONT]);
	t->target = NULL;
	t->flags = TRANSCRIPT_FORMAT;
	t->clip_left = 0;
	t->clip_top = 0;
	t->clip_right = (int16_t)(k->right - k->left);
	t->clip_bottom = (int16_t)((k->bottom - k->top + 1) * TEXT_HEIGHTS - 1);
	at = kp_ui_draw_text(t, 0, 0, text);
	if (at != SIZE_MAX) on_cut(cuts, ui, c, text, at);
	t->font = NULL;
	kp_ui_destroy_dialog(ui, d);
}

/* Each text of every mission of the install in every place it shows, in the base's text or the language's (NULL). */
static void mission_cuts(KpT *T, KpUi *ui, Cuts *cuts, const Install *in, const char *language)
{
	char *line, *next;
	int missions = 0, measured[3] = { 0, 0, 0 };
	cuts->n = 0;
	ui->text_cut = on_cut;
	ui->text_cut_ctx = cuts;
	for (line = mission_index(T, in->d); line != NULL && *line != 0; line = next) {
		char name[128], where[40];
		const KpMission *m;
		MissionText texts[MISSION_TEXTS];
		size_t strs_size;
		uint8_t *ours = NULL;
		const uint8_t *strs;
		int n, k, r;
		next = strchr(line, '\n');
		if (next != NULL) *next++ = 0;
		if (sscanf(line, "%127s", name) != 1) continue;
		int mark = kp_t_mark(T);
		m = data_mission(T, in->d, name);
		strs = m->strings.data;
		strs_size = m->strings.size;
		if (language != NULL) {
			ours = language_strings(T, in, language, name, m, &strs_size, NULL);
			kp_t_defer(T, free, ours);
			strs = ours;
		}
		n = mission_texts(m, texts);
		for (k = 0; k < n; k++) {
			const uint8_t *text = chk_string(strs, strs_size, texts[k].id);
			if (text == NULL || text[0] == 0) continue;
			cuts->row = texts[k].id;
			measured[texts[k].place]++;
			switch (texts[k].place) {
			case PLACE_OBJECTIVES:
				snprintf(where, sizeof where, "%.26s objectives", name);
				cuts->dialog = where;
				measure_in(T, ui, cuts, OBJECTIVES_DIALOG, OBJECTIVES_TEXT, 1, text);
				for (r = 0; r < 3; r++) {
					snprintf(where, sizeof where, "%.26s room %d", name, r);
					measure_in(T, ui, cuts, ROOMS[r], ROOM_OBJECTIVES, 0, text);
				}
				break;
			case PLACE_TRANSCRIPT:
				for (r = 0; r < 3; r++) {
					snprintf(where, sizeof where, "%.26s transcript %d", name, r);
					cuts->dialog = where;
					measure_transcript(T, ui, cuts, ROOMS[r], text);
				}
				break;
			default:
				snprintf(where, sizeof where, "%.26s name", name);
				cuts->dialog = where;
				measure_in(T, ui, cuts, STATUS_DIALOG, -5, 1, text);
				break;
			}
		}
		kp_t_release(T, mark);
		missions++;
	}
	ui->text_cut = NULL;
	kp_t_eq(T, missions, 64, "the campaigns' missions");
	kp_t_ok(T, measured[PLACE_OBJECTIVES] > 64 && measured[PLACE_TRANSCRIPT] > 400 && measured[PLACE_NAME] > 10,
		"texts measured: %d objectives, %d transcripts, %d names", measured[PLACE_OBJECTIVES], measured[PLACE_TRANSCRIPT],
		measured[PLACE_NAME]);
	kp_t_log(T, "%d objectives, %d transcripts, %d names, %d cut", measured[PLACE_OBJECTIVES], measured[PLACE_TRANSCRIPT],
		measured[PLACE_NAME], cuts->n);
}

static void language_missions_texts_fit(KpT *T, const Install *in, const char *locale, void *ctx)
{
	LanguageCuts *lc = (LanguageCuts *)ctx;
	Cuts *language;
	char report[96];
	int fresh;
	language = (Cuts *)calloc(1, sizeof *language);
	if (language == NULL) kp_t_fail(T, "out of memory");
	kp_t_defer(T, free_cuts, language);
	mission_cuts(T, real_language(T, in, locale), language, in, locale);
	snprintf(report, sizeof report, MISSION_WIDTHS_REPORT, locale);
	fresh = write_cuts(report, locale, language, lc->base);
	kp_t_ok(T, language->n < CUTS_MAX && lc->base->n < CUTS_MAX, "cuts: %d, %d", lc->base->n, language->n);
	kp_t_log(T, "%s: %d missions' texts do not fit, listed in %s", locale, fresh, report);
	note_fresh(lc, locale, fresh);
}

/* Every mission's objectives, briefing transcript and units' names of each language fit where the base's do; the new
 * cuts go to generated/reports/widths-<locale>-missions.json. */
static void the_missions_texts_fit(KpT *T)
{
	Cuts *base_cuts;
	LanguageCuts lc;
	Install in;
	install(T, &in);
	base_cuts = (Cuts *)calloc(1, sizeof *base_cuts);
	if (base_cuts == NULL) kp_t_fail(T, "out of memory");
	kp_t_defer(T, free_cuts, base_cuts);
	mission_cuts(T, ui_over(T, &in.d->ui), base_cuts, &in, NULL);
	memset(&lc, 0, sizeof lc);
	lc.base = base_cuts;
	for_each_language(T, &in, "test_locale.missions_texts", language_missions_texts_fit, &lc);
	kp_t_eq(T, lc.fresh, 0, "missions' texts that do not fit, listed in " MISSION_WIDTHS_REPORT " of:%s", "<locale>", lc.languages);
}

/* The atlas of the vector text with the language's fonts and the supplement's code points, at the largest scale a phone
 * or a Retina window asks for (ADR 0016's measure). */
static void the_atlas_holds_the_supplement_at_scale_4(KpT *T)
{
	static const double SCALES[] = { 2.0, 3.0, 4.0 };
	const uint8_t *const *files;
	const size_t *sizes;
	KpUi *ui;
	KpVText *v;
	Install in;
	size_t k;
	install(T, &in);
	if (!kp_locale_held(&in.raw, "ruRU")) kp_t_skip(T, "the install holds no ruRU");
	ui = real_language(T, &in, "ruRU");
	kp_t_eq(T, kp_ui_vector_fonts(ui, &files, &sizes), 2, "the language's VFNT");
	kp_t_ok(T, ui->supplement_code_count > 100, "the code points: %d", ui->supplement_code_count);
	v = (KpVText *)calloc(1, sizeof *v);
	if (!v) kp_t_fail(T, "out of memory");
	kp_t_defer(T, free_vtext, v);
	kp_t_eq(T, kp_vtext_init(v, files, sizes, 2), 0, "the TrueType fonts");
	kp_t_eq(T, kp_vtext_add_codes(v, ui->supplement_codes, ui->supplement_code_count), 0, NULL);
	kp_t_eq(T, v->extra_count, ui->supplement_code_count, "both fonts have them all");
	for (k = 0; k < sizeof SCALES / sizeof SCALES[0]; k++) {
		kp_t_eq(T, kp_vtext_atlas(v, ui->font_files, ui->font_file_sizes, SCALES[k]), 0, "scale %g", SCALES[k]);
		kp_t_log(T, "scale %g: atlas %d x %d, %d KB", SCALES[k], v->atlas_w, v->atlas_h, v->atlas_w * v->atlas_h / 1024);
		kp_t_ok(T, v->atlas_w <= 4096 && v->atlas_h <= 4096, "%d x %d", v->atlas_w, v->atlas_h);
	}
	/* a supplement's code point is a quad now */
	{
		KpUiCanvas c;
		KpUiCanvasGlyph g;
		memset(&c, 0, sizeof c);
		memset(&g, 0, sizeof g);
		g.x = 10;
		g.y = 20;
		g.font = ui->font_files[1];
		g.code = 0x041b;
		g.advance = 7;
		g.fill = 100;
		g.whole = 1;
		c.glyphs = &g;
		c.glyph_count = 1;
		kp_t_eq(T, kp_vtext_quads(v, &c), 1, "U+041B draws");
		g.code = 0x4e00;
		kp_t_eq(T, kp_vtext_quads(v, &c), 0, "one the atlas lacks does not");
	}
}

/* --- a mission's text --- */

/* A loaded, not started game of the mission's key over the original data. */
static KpSession *loaded_game(KpT *T, const char *key)
{
	const KpData *d = kp_t_data(T);
	KpSession *s = (KpSession *)calloc(1, sizeof *s);
	KpMission m;
	KpError err;
	if (!s) kp_t_fail(T, "out of memory");
	kp_t_defer(T, free_session, s);
	if (!kp_oracle_mission(".", key, &m, &err)) kp_t_fail(T, "%s", err.msg);
	if (kp_session_load_from(s, &d->shared, &d->dat, &m, key, &d->files) != 0) {
		kp_t_fail(T, "%s does not load: %s", key, s->error);
	}
	return s;
}

static void start_game(KpT *T, KpSession *s, const char *key, int expansion)
{
	KpSessionStart start;
	kp_session_start_defaults(&start);
	start.seed = SEED;
	start.expansion = expansion;
	start.mission_index = kp_oracle_mission_index(".", key);
	if (kp_session_start(s, &start) != 0) kp_t_fail(T, "%s does not start: %s", key, s->error);
	kp_t_guard(T, s->g);
}

/* The mission's strings with each character above the control bytes as `with` (malloc'd, as kp_host_game_text takes
 * them). */
static uint8_t *text_strings(KpT *T, const KpBytes *strs, const char *with, size_t *size)
{
	size_t room = 2 * STR_LIMIT, with_size = strlen(with);
	uint8_t *out = (uint8_t *)malloc(room);
	uint32_t count = kp_le_u16(strs->data), k;
	size_t at = 2 + 2 * (size_t)count;
	if (!out) kp_t_fail(T, "out of memory");
	put_u16(out, count);
	for (k = 0; k < count; k++) {
		uint32_t from = kp_le_u16(strs->data + 2 + 2 * k);
		size_t n = 0;
		if (from >= strs->size) {
			put_u16(out + 2 + 2 * k, NO_STRING);
			continue;
		}
		put_u16(out + 2 + 2 * k, (uint32_t)at);
		while (from + n < strs->size && strs->data[from + n] != 0) {
			uint8_t b = strs->data[from + n++];
			if (at + with_size + 1 > STR_LIMIT) {
				free(out);
				kp_t_fail(T, "the test's strings are too long");
			}
			if (b < 0x20) {
				out[at++] = b;
			} else if ((b & 0xc0) != 0x80) {
				memcpy(out + at, with, with_size);
				at += with_size;
			}
		}
		out[at++] = 0;
	}
	*size = at;
	return out;
}

#define MAP "scenarios/marine_group"

static const char *const NEED_MAP[] = { MAP, NULL };

/* A malloc'd copy of bytes (kp_host_game_text takes its strings). */
static uint8_t *copied(KpT *T, const void *data, size_t size)
{
	uint8_t *out = (uint8_t *)malloc(size ? size : 1);
	if (!out) kp_t_fail(T, "out of memory");
	memcpy(out, data, size);
	return out;
}

static void mission_text_refusals_leave_the_missions_own(KpT *T)
{
	static const uint8_t ONE[] = { 1, 0, 4, 0, 'a', 0 };
	KpSession *s;
	KpError e;
	uint8_t *strs;
	size_t size;
	const uint8_t *own;
	if (!kp_t_ready(T, "test_locale.mission_text", NEED_MAP)) return;
	s = loaded_game(T, MAP);
	own = s->host.data.mission.strings.data;
	kp_t_ok(T, own != NULL, "the map's strings");
	/* another count */
	kp_t_eq(T, kp_host_game_text(&s->host, copied(T, ONE, sizeof ONE), sizeof ONE, &e), -1, "another count");
	kp_t_ok(T, strstr(e.msg, "count") != NULL, "%s", e.msg);
	/* no count */
	kp_t_eq(T, kp_host_game_text(&s->host, copied(T, ONE, 1), 1, &e), -1, "a byte");
	kp_t_ok(T, s->host.data.mission.strings.data == own, "the mission's own");
	/* twice: the second replaces and frees the first */
	strs = text_strings(T, &s->host.data.mission.strings, "\xd0\x96", &size);
	kp_t_eq(T, kp_host_game_text(&s->host, strs, size, &e), 0, "%s", e.msg);
	kp_t_ok(T, s->host.data.mission.strings.data == strs, "taken");
	strs = text_strings(T, &s->host.data.mission.strings, "z", &size);
	kp_t_eq(T, kp_host_game_text(&s->host, strs, size, &e), 0, "%s", e.msg);
	start_game(T, s, MAP, 0);
	kp_t_ok(T, s->g != NULL, "the game starts over a language's text");
}

/* The state of a game after the steps, of the mission's own text or a language's strings (taken). */
static void run_mission(KpT *T, const char *path, uint8_t *strs, size_t size, int expansion, int steps, KpStateCrcs *out)
{
	int mark = kp_t_mark(T), k;
	KpSession *s = loaded_game(T, path);
	KpError e;
	if (strs != NULL) kp_t_eq(T, kp_host_game_text(&s->host, strs, size, &e), 0, "%s: %s", path, e.msg);
	start_game(T, s, path, expansion);
	for (k = 0; k < steps; k++) {
		if (!kp_session_step(s, NULL)) kp_t_fail(T, "%s: step %d: %s", path, k, s->error);
	}
	kp_t_ok(T, kp_state_crcs(s->g, 0, out) == 0, "state CRCs");
	kp_t_release(T, mark);
}

static void same_crcs(KpT *T, const KpStateCrcs *a, const KpStateCrcs *b, const char *what)
{
	int k, found;
	kp_t_eq(T, a->count, b->count, "%s: categories", what);
	for (k = 0; k < a->count; k++) {
		kp_t_eq(T, kp_state_crc(b, a->names[k], &found), a->crcs[k], "%s: %s", what, a->names[k]);
	}
}

/* Acceptance of stage L2: the text never reaches the simulation. Every mission with a language's text steps to the
 * state of its own. */
static void language_keeps_the_missions_state(KpT *T, const Install *in, const char *locale, void *ctx)
{
	(void)ctx;
	char *line, *next;
	int k = 0, ran = 0;
	for (line = mission_index(T, in->d); line != NULL && *line != 0; line = next) {
		char name[128], path[256];
		KpStateCrcs own, theirs;
		uint8_t *strs;
		size_t size;
		int mark;
		next = strchr(line, '\n');
		if (next != NULL) *next++ = 0;
		if (sscanf(line, "%127s", name) != 1) continue;
		k++;
		if (!kp_t_mine_sampled(T, k)) continue;
		snprintf(path, sizeof path, "missions/%s", name);
		mark = kp_t_mark(T);
		strs = language_strings(T, in, locale, name, data_mission(T, in->d, name), &size, NULL);
		kp_t_release(T, mark);
		run_mission(T, path, NULL, 0, strncmp(name, "exp", 3) == 0, MISSION_STEPS, &own);
		run_mission(T, path, strs, size, strncmp(name, "exp", 3) == 0, MISSION_STEPS, &theirs);
		same_crcs(T, &own, &theirs, name);
		ran++;
	}
	kp_t_eq(T, k, 64, "the campaigns' missions");
	kp_t_log(T, "%s: %d missions kept their state over %d steps", locale, ran, MISSION_STEPS);
}

static void missions_with_the_languages_text_keep_their_state(KpT *T)
{
	Install in;
	if (!kp_t_ready(T, "test_locale.missions", REAL_BASE)) return;
	install(T, &in);
	for_each_language(T, &in, "test_locale.missions", language_keeps_the_missions_state, NULL);
}

/* The first text a trigger shows (its string and time) with the strings (taken), 0 when none does. */
static int first_text(KpT *T, const char *path, uint8_t *strs, size_t size, const char *with, KpEvent *out)
{
	int mark = kp_t_mark(T), k, shown = 0;
	KpSession *s = loaded_game(T, path);
	KpError e;
	kp_t_eq(T, kp_host_game_text(&s->host, strs, size, &e), 0, "%s", e.msg);
	start_game(T, s, path, 0);
	for (k = 0; k < TEXT_STEPS && !shown; k++) {
		KpEvent ev;
		if (!kp_session_step(s, NULL)) kp_t_fail(T, "step %d: %s", k, s->error);
		while (kp_event_take(s->g, &ev)) {
			if (ev.kind == KP_EVENT_TRIGGER_TEXT && !shown) {
				const char *text = kp_map_string(s->g, (uint32_t)ev.a);
				kp_t_ok(T, text != NULL && strstr(text, with) != NULL, "the game reads the language's text: %s", text ? text : "");
				*out = ev;
				shown = 1;
			}
		}
	}
	kp_t_release(T, mark);
	return shown;
}

/* A text stays as long whatever bytes its characters take: Cyrillic's two must not double it. */
static void a_texts_time_counts_characters(KpT *T)
{
	static const char *const PATH = "missions/terran/terran01";
	static const char *const NEED[] = { PATH, NULL };
	KpEvent one, two;
	uint8_t *narrow, *wide;
	size_t narrow_size, wide_size;
	KpSession *s;
	int mark;
	if (!kp_t_ready(T, "test_locale.mission_text", NEED)) return;
	mark = kp_t_mark(T);
	s = loaded_game(T, PATH);
	narrow = text_strings(T, &s->host.data.mission.strings, "z", &narrow_size);
	wide = text_strings(T, &s->host.data.mission.strings, "\xd0\x96", &wide_size);
	kp_t_release(T, mark);
	kp_t_ok(T, wide_size > narrow_size, "more bytes");
	kp_t_ok(T, first_text(T, PATH, narrow, narrow_size, "z", &one), "a text shown");
	kp_t_ok(T, first_text(T, PATH, wide, wide_size, "\xd0\x96", &two), "a text shown");
	kp_t_eq(T, one.a, two.a, "the same string");
	kp_t_eq(T, one.b, two.b, "the same time");
}

static const KpTest TESTS[] = {
	{ "supplement_finds_measures_and_draws", supplement_finds_measures_and_draws, 0 },
	{ "supplement_comes_before_an_untrue_slot", supplement_comes_before_an_untrue_slot, 0 },
	{ "supplement_tells_a_missing_code_once", supplement_tells_a_missing_code_once, 0 },
	{ "supplement_malformed_is_not_taken", supplement_malformed_is_not_taken, 0 },
	{ "fnt_supplement_from_a_truetype_font", fnt_supplement_from_a_truetype_font, 0 },
	{ "fnt_supplement_keeps_the_fonts_colours", fnt_supplement_keeps_the_fonts_colours, 0 },
	{ "tables_past_u16_replace_the_base", tables_past_u16_replace_the_base, 0 },
	{ "other_tables_are_refused", other_tables_are_refused, 0 },
	{ "dialogs_take_the_languages_texts", dialogs_take_the_languages_texts, 0 },
	{ "scripts_are_read_from_the_language_first", scripts_are_read_from_the_language_first, 0 },
	{ "glyphs_join_every_font_and_replace_the_vector_fonts", glyphs_join_every_font_and_replace_the_vector_fonts, 0 },
	{ "the_languages_equal_the_converters", the_languages_equal_the_converters, 0 },
	{ "the_languages_text_is_valid_and_has_glyphs", the_languages_text_is_valid_and_has_glyphs, 0 },
	{ "a_text_that_does_not_fit_is_told", a_text_that_does_not_fit_is_told, 0 },
	{ "the_languages_texts_fit", the_languages_texts_fit, 0 },
	{ "the_missions_texts_fit", the_missions_texts_fit, 0 },
	{ "the_atlas_holds_the_supplement_at_scale_4", the_atlas_holds_the_supplement_at_scale_4, 0 },
	{ "mission_text_refusals_leave_the_missions_own", mission_text_refusals_leave_the_missions_own, 0 },
	{ "missions_with_the_languages_text_keep_their_state", missions_with_the_languages_text_keep_their_state, 6 },
	{ "a_texts_time_counts_characters", a_texts_time_counts_characters, 0 },
};

KP_SUITE(kp_suite_locale, "test_locale", TESTS);
