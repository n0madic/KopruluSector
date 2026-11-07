/* A language over the base (ADR 0016, localization's stage L2): the glyph supplements of the text engine and the vector
 * text, kp_fnt_supplement, the overlays of the interface (kp_ui_set_locale) and of a mission (kp_host_game_text), and
 * what a language must not change: every mission's state under a language's text is the base's. The small cases build
 * their overlays here over the converter's fixtures; the ones over the real data skip without generated/. */
#include "kp_test_app.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kp_kpf.h"
#include "kp_present.h"
#include "kp_session.h"
#include "kp_ui.h"
#include "kp_ui_text.h"
#include "kp_utf8.h"

#define DIR "tests/fixtures/generated/kpf/"
#define LOCALE_DIR "generated/locale/"
#define RU LOCALE_DIR "ruRU/"
#define GLYPHS LOCALE_DIR "glyphs.kpd"
#define MISSIONS "generated/missions/"
#define PROFILE "test"
#define SEED 0x12345678u
#define NO_STRING 0xffffu
#define STR_LIMIT 0xffffu
#define MISSION_STEPS 1000
#define TEXT_STEPS 3000 /* a mission with a text of its triggers has shown one by then */
#define ERROR_SIZE 256

static const char *const FIXTURES[] = { DIR "ui.kpd", DIR "fonts.kpd", NULL };
static const char *const REAL_UI[] = { "generated/data/ui.kpd", "generated/data/fonts.kpd", NULL };
static const char *const REAL_LANGUAGE[] = { "generated/data/ui.kpd", "generated/data/fonts.kpd", "generated/data/strings.kpd",
	RU "strings.kpd", RU "ui.kpd", GLYPHS, NULL };
static const char *const REAL_MISSIONS[] = { "generated/data/dat.kpd", MISSIONS "index.txt", RU "strings.kpd", NULL };

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

static KpUi *ui_over(KpT *T, const char *ui_path, const char *fonts_path)
{
	size_t ui_size, fonts_size;
	uint8_t *ui_kpd = kp_t_read(T, ui_path, &ui_size), *fonts = kp_t_read(T, fonts_path, &fonts_size);
	char err[ERROR_SIZE];
	KpUi *ui = kp_ui_new(ui_kpd, ui_size, fonts, fonts_size, err, sizeof err);
	kp_t_ok(T, ui != NULL, "%s", err);
	kp_t_defer(T, free_ui, ui);
	return ui;
}

static KpUi *fixture_ui(KpT *T) { return ui_over(T, DIR "ui.kpd", DIR "fonts.kpd"); }

/* A container of sections (freed with the test). */
static uint8_t *pack(KpT *T, const char *kind, uint32_t schema, const KpKpfOut *sections, int count, size_t *size)
{
	uint8_t *out = NULL;
	kp_t_eq(T, kp_kpf_build(kind, schema, 0, PROFILE, NULL, sections, count, &out, size), 0, "a pack");
	kp_t_defer(T, free, out);
	return out;
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
	ui = fixture_ui(T);
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

static void supplement_tells_a_missing_code_once(KpT *T)
{
	KpUi *ui;
	KpUiText t;
	static const uint8_t TEXT[] = "\xd0\x97\xd0\x97\xd0\x98";
	if (!kp_t_ready(T, "test_locale", FIXTURES)) return;
	ui = fixture_ui(T);
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
	ui = fixture_ui(T);
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
	static const int32_t ASCENDING[] = { 0x416, 0x41d, 0x10f000 };
	const uint8_t *const *files;
	const size_t *sizes;
	uint8_t *out = NULL;
	size_t out_size = 0;
	int32_t missing[4], missing_count = -1;
	char err[ERROR_SIZE];
	KpUi *ui, *fixture;
	KpUiText t;
	int font;
	if (!kp_t_ready(T, "test_locale.fnt_supplement", REAL_UI) || !kp_t_ready(T, "test_locale", FIXTURES)) return;
	ui = ui_over(T, "generated/data/ui.kpd", "generated/data/fonts.kpd");
	fixture = fixture_ui(T);
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
		ASCENDING, 3, &out, &out_size, missing, &missing_count, err, sizeof err), -1, "no TrueType font");
	kp_t_eq(T, kp_fnt_supplement(files[0], sizes[0], fixture->font_files[0], fixture->font_file_sizes[0], ASCENDING, 3, &out,
		&out_size, missing, &missing_count, err, sizeof err), -1, "an FNT font without Latin letters");
	for (font = 0; font < KP_UI_FNT_FONTS; font++) {
		const uint8_t *fnt = ui->font_files[font];
		int64_t offset;
		out = NULL;
		kp_t_eq(T, kp_fnt_supplement(files[0], sizes[0], fnt, ui->font_file_sizes[font], ASCENDING, 3, &out, &out_size, missing,
			&missing_count, err, sizeof err), 0, "%s", err);
		kp_t_defer(T, free, out);
		kp_t_eq(T, missing_count, 1, "font %d: one code the font lacks", font);
		kp_t_eq(T, missing[0], 0x10f000, NULL);
		kp_t_eq(T, kp_ui_supplement_count(out, out_size), 2, "a supplement of the two");
		kp_t_eq(T, kp_ui_supplement_code(out, 0), 0x416, NULL);
		kp_t_eq(T, kp_ui_supplement_code(out, 1), 0x41d, NULL);
		kp_ui_text_init(&t);
		kp_t_eq(T, kp_ui_text_select_font(&t, fnt, ui->font_file_sizes[font]), 0, NULL);
		kp_t_eq(T, kp_ui_text_add_supplement(&t, fnt, out, out_size), 0, NULL);
		/* U+041D looks as H: the fit takes it to the font's own H */
		{
			int32_t ours = kp_ui_text_width(&t, (const uint8_t *)"\xd0\x9d"), its = kp_ui_char_width(&t, 'H');
			kp_t_ok(T, ours >= its - 2 && ours <= its + 2, "font %d: U+041D %d wide, H %d", font, ours, its);
		}
		/* the glyphs stay in the font's cell */
		offset = (int64_t)kp_le_u32(out + 8);
		kp_t_ok(T, out[offset + 1] + out[offset + 3] <= t.max_height, "font %d: U+0416's height %d + %d of %d", font,
			out[offset + 1], out[offset + 3], t.max_height);
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

static KpUi *strings_ui(KpT *T)
{
	static uint8_t base[TABLE_ROOM];
	KpKpfOut sections[2];
	size_t size, table = narrow_table(base, BASE_ROWS, 2);
	const uint8_t *file;
	KpUi *ui = fixture_ui(T);
	memset(sections, 0, sizeof sections);
	sections[0].fourcc = "STAT";
	sections[0].payload = base;
	sections[0].size = table;
	sections[1].fourcc = "NETW";
	sections[1].payload = base;
	sections[1].size = table;
	file = pack(T, "STRS", 1, sections, 2, &size);
	kp_t_eq(T, kp_ui_set_strings(ui, file, size), 0, "%s", ui->error);
	return ui;
}

static void overlay_strings(KpT *T, KpUi *ui, const uint8_t *table, size_t size, const char *section, uint32_t schema)
{
	KpUiLocale locale;
	KpKpfOut out;
	memset(&locale, 0, sizeof locale);
	memset(&out, 0, sizeof out);
	out.fourcc = section;
	out.payload = table;
	out.size = size;
	out.schema_version = schema;
	locale.strings = pack(T, "STRS", 1, &out, 1, &locale.strings_size);
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
	overlay_strings(T, ui, table, size, "STAT", KP_UI_TBL_WIDE);
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
	KpUiLocale locale;
	KpKpfOut out;
	if (!kp_t_ready(T, "test_locale", FIXTURES)) return;
	/* another row count */
	ui = strings_ui(T);
	size = wide_table(table, THREE, 3, 0);
	overlay_strings(T, ui, table, size, "STAT", KP_UI_TBL_WIDE);
	kp_t_ok(T, strstr(ui->locale_problem, "row count") != NULL, "%s", ui->locale_problem);
	kp_t_eq_str(T, (const char *)kp_ui_tbl(ui, 0, 2), "second", "the base stays");
	/* the original's form */
	ui = strings_ui(T);
	narrow_size = narrow_table(narrow, OUR_ROWS, 2);
	overlay_strings(T, ui, narrow, narrow_size, "STAT", 0);
	kp_t_ok(T, ui->locale_problem[0] != 0, "a section of schema 1");
	kp_t_eq_str(T, (const char *)kp_ui_tbl(ui, 0, 2), "second", "the base stays");
	/* offsets past the section read nothing */
	ui = strings_ui(T);
	size = wide_table(table, OUR_ROWS, 2, 0);
	put_u32(table + 8, 0xfffffff0u);
	overlay_strings(T, ui, table, size, "STAT", KP_UI_TBL_WIDE);
	kp_t_eq_str(T, (const char *)kp_ui_tbl(ui, 0, 2), "", "an offset past the section");
	/* a table cut under its count */
	ui = strings_ui(T);
	size = wide_table(table, OUR_ROWS, 2, 0);
	overlay_strings(T, ui, table, 7, "STAT", KP_UI_TBL_WIDE);
	kp_t_ok(T, ui->locale_problem[0] != 0, "no room for the offsets");
	/* another kind, another schema, a damaged file */
	ui = strings_ui(T);
	size = wide_table(table, OUR_ROWS, 2, 0);
	memset(&locale, 0, sizeof locale);
	memset(&out, 0, sizeof out);
	out.fourcc = "STAT";
	out.payload = table;
	out.size = size;
	out.schema_version = KP_UI_TBL_WIDE;
	locale.strings = pack(T, "FONT", 1, &out, 1, &locale.strings_size);
	kp_ui_set_locale(ui, &locale);
	kp_t_ok(T, strstr(ui->locale_problem, "kind") != NULL, "%s", ui->locale_problem);
	kp_t_eq_str(T, (const char *)kp_ui_tbl(ui, 0, 2), "second", "another kind");
	ui = strings_ui(T);
	locale.strings = pack(T, "STRS", 9, &out, 1, &locale.strings_size);
	kp_ui_set_locale(ui, &locale);
	kp_t_ok(T, strstr(ui->locale_problem, "schema") != NULL, "%s", ui->locale_problem);
	ui = strings_ui(T);
	{
		uint8_t *damaged = pack(T, "STRS", 1, &out, 1, &locale.strings_size);
		size_t k = 0;
		while (k < locale.strings_size && damaged[k] != (uint8_t)OUR_ROWS[1][0]) k++; /* in the section, under its CRC */
		kp_t_ok(T, k < locale.strings_size, "the row in the file");
		damaged[k] ^= 0x55;
		locale.strings = damaged;
	}
	kp_ui_set_locale(ui, &locale);
	kp_t_ok(T, ui->locale_problem[0] != 0, "a damaged file");
	kp_t_eq_str(T, (const char *)kp_ui_tbl(ui, 0, 2), "second", "a damaged file");
}

/* A language's ui pack from the fixture's: `pool` over its POOL, `dlgs` over its DLGS, and a GFIL. */
static void overlay_ui(KpT *T, KpUi *ui, const uint8_t *dlgs, size_t dlgs_size, const uint8_t *pool, size_t pool_size,
	const uint8_t *gfil, size_t gfil_size)
{
	KpUiLocale locale;
	KpKpfOut out[3];
	int count = 0;
	memset(&locale, 0, sizeof locale);
	memset(out, 0, sizeof out);
	if (dlgs != NULL) {
		out[count].fourcc = "DLGS";
		out[count].payload = dlgs;
		out[count++].size = dlgs_size;
		out[count].fourcc = "POOL";
		out[count].payload = pool;
		out[count++].size = pool_size;
	}
	if (gfil != NULL) {
		out[count].fourcc = "GFIL";
		out[count].payload = gfil;
		out[count++].size = gfil_size;
	}
	locale.ui = pack(T, "UI  ", ui->ui_kpf.schema_version, out, count, &locale.ui_size);
	kp_ui_set_locale(ui, &locale);
}

static uint8_t *copy_section(KpT *T, const char *fourcc, size_t *size)
{
	KpKpf c;
	KpError e;
	size_t file_size;
	uint8_t *data = kp_t_read(T, DIR "ui.kpd", &file_size), *copy;
	const KpKpfSection *s;
	kp_t_eq(T, kp_kpf_parse(&c, data, file_size, "ui", 0, &e), 0, "%s", e.msg);
	s = kp_kpf_find(&c, fourcc);
	kp_t_ok(T, s != NULL, "the fixture's %s", fourcc);
	copy = (uint8_t *)malloc(s->size);
	if (!copy) kp_t_fail(T, "out of memory");
	kp_t_defer(T, free, copy);
	memcpy(copy, s->data, s->size);
	*size = s->size;
	kp_kpf_free(&c);
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
	ui = fixture_ui(T);
	overlay_ui(T, ui, dlgs, dlgs_size, pool, pool_size, NULL, 0);
	kp_t_eq_str(T, ui->locale_problem, "", "accepted");
	d = kp_ui_load_dialog(ui, "rez\\x.bin");
	kp_t_ok(T, d >= 0, "the dialog loads: %s", ui->error);
	button = &ui->controls[ui->controls[d].child];
	kp_t_eq_str(T, (const char *)button->text, "q\xd0\x96x", "the language's text");
	/* other dialogs: the name of the only one */
	ui = fixture_ui(T);
	at = (uint8_t *)memchr(dlgs, 'x', dlgs_size);
	kp_t_ok(T, at != NULL, "the dialog's name");
	*at = 'y';
	overlay_ui(T, ui, dlgs, dlgs_size, pool, pool_size, NULL, 0);
	kp_t_ok(T, strstr(ui->locale_problem, "other dialogs") != NULL, "%s", ui->locale_problem);
	kp_t_ok(T, ui->bin_count == 1 && kp_ui_load_dialog(ui, "rez\\x.bin") >= 0, "the base's stay");
	kp_t_eq_str(T, (const char *)ui->controls[ui->controls[kp_ui_load_dialog(ui, "rez\\x.bin")].child].text, "quit", "and their text");
	*at = 'x';
	/* a cut section */
	ui = fixture_ui(T);
	overlay_ui(T, ui, dlgs, dlgs_size / 2, pool, pool_size, NULL, 0);
	kp_t_ok(T, strstr(ui->locale_problem, "malformed") != NULL, "%s", ui->locale_problem);
	kp_t_ok(T, ui->bin_count == 1 && kp_ui_load_dialog(ui, "rez\\x.bin") >= 0, "the base's stay");
}

static void scripts_are_read_from_the_language_first(KpT *T)
{
	static const char OURS[] = "glue\\x.pcx", NEW[] = "rez\\n.txt";
	uint8_t gfil[64], *p = gfil;
	KpUi *ui;
	const KpUiFile *f;
	if (!kp_t_ready(T, "test_locale", FIXTURES)) return;
	ui = fixture_ui(T);
	f = kp_ui_glue_file(ui, OURS);
	kp_t_ok(T, f != NULL && f->size == 3 && memcmp(f->data, "abc", 3) == 0, "the base's file");
	kp_t_ok(T, kp_ui_glue_file(ui, NEW) == NULL, "a file the base lacks");
	put_u16(p, 2);
	p += 2;
	*p++ = (uint8_t)strlen(NEW);
	memcpy(p, NEW, strlen(NEW));
	p += strlen(NEW);
	put_u32(p, 2);
	p += 4;
	memcpy(p, "hi", 2);
	p += 2;
	*p++ = (uint8_t)strlen(OURS);
	memcpy(p, OURS, strlen(OURS));
	p += strlen(OURS);
	put_u32(p, 4);
	p += 4;
	memcpy(p, "wxyz", 4);
	p += 4;
	overlay_ui(T, ui, NULL, 0, NULL, 0, gfil, (size_t)(p - gfil));
	kp_t_eq_str(T, ui->locale_problem, "", "accepted");
	f = kp_ui_glue_file(ui, OURS);
	kp_t_ok(T, f != NULL && f->size == 4 && memcmp(f->data, "wxyz", 4) == 0, "the language's before the base's");
	f = kp_ui_glue_file(ui, NEW);
	kp_t_ok(T, f != NULL && f->size == 2 && memcmp(f->data, "hi", 2) == 0, "and its own");
	/* a malformed section leaves the base */
	ui = fixture_ui(T);
	put_u16(gfil, 300);
	overlay_ui(T, ui, NULL, 0, NULL, 0, gfil, 8);
	kp_t_ok(T, strstr(ui->locale_problem, "malformed") != NULL, "%s", ui->locale_problem);
	f = kp_ui_glue_file(ui, OURS);
	kp_t_ok(T, f != NULL && f->size == 3, "the base's file");
}

static void glyphs_join_every_font_and_replace_the_vector_fonts(KpT *T)
{
	static const char *const SECTIONS[KP_UI_FNT_FONTS] = { "U8  ", "U10 ", "U14 ", "U16 ", "U16X" };
	static const uint8_t VFNT[] = { 2, 0, 3, 'a', '.', 't', 2, 0, 0, 0, 'v', '0', 3, 'b', '.', 't', 2, 0, 0, 0, 'v', '1' };
	KpKpfOut out[KP_UI_FNT_FONTS + 1];
	KpUiLocale locale;
	const uint8_t *const *files;
	const size_t *sizes;
	KpUi *ui;
	uint8_t bad[sizeof SUPPLEMENT];
	int k;
	if (!kp_t_ready(T, "test_locale", FIXTURES)) return;
	ui = fixture_ui(T);
	memset(out, 0, sizeof out);
	memset(&locale, 0, sizeof locale);
	for (k = 0; k < KP_UI_FNT_FONTS; k++) {
		out[k].fourcc = SECTIONS[k];
		out[k].payload = SUPPLEMENT;
		out[k].size = sizeof SUPPLEMENT;
	}
	out[KP_UI_FNT_FONTS].fourcc = "VFNT";
	out[KP_UI_FNT_FONTS].payload = VFNT;
	out[KP_UI_FNT_FONTS].size = sizeof VFNT;
	locale.glyphs = pack(T, "FONT", ui->fonts_kpf.schema_version, out, KP_UI_FNT_FONTS + 1, &locale.glyphs_size);
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
	ui = fixture_ui(T);
	memcpy(bad, SUPPLEMENT, sizeof bad);
	put_u32(bad, 0xffffu);
	out[1].payload = bad;
	locale.glyphs = pack(T, "FONT", ui->fonts_kpf.schema_version, out, KP_UI_FNT_FONTS, &locale.glyphs_size);
	kp_ui_set_locale(ui, &locale);
	kp_t_ok(T, strstr(ui->locale_problem, "malformed") != NULL, "%s", ui->locale_problem);
	kp_t_eq(T, kp_ui_text_select_font(&ui->text, ui->font_files[1], ui->font_file_sizes[1]), 0, NULL);
	kp_t_ok(T, !kp_ui_has_glyph(&ui->text, SUP_SECOND), "the malformed one's font");
	kp_t_eq(T, kp_ui_text_select_font(&ui->text, ui->font_files[2], ui->font_file_sizes[2]), 0, NULL);
	kp_t_ok(T, kp_ui_has_glyph(&ui->text, SUP_SECOND), "the next font's");
}

/* --- the language's real packs --- */

static KpUi *real_language(KpT *T)
{
	KpUiLocale locale;
	size_t strings_size;
	const uint8_t *strings = kp_t_read(T, "generated/data/strings.kpd", &strings_size);
	KpUi *ui = ui_over(T, "generated/data/ui.kpd", "generated/data/fonts.kpd");
	memset(&locale, 0, sizeof locale);
	kp_t_eq(T, kp_ui_set_strings(ui, strings, strings_size), 0, "%s", ui->error);
	locale.strings = kp_t_read(T, RU "strings.kpd", &locale.strings_size);
	locale.ui = kp_t_read(T, RU "ui.kpd", &locale.ui_size);
	locale.glyphs = kp_t_read(T, GLYPHS, &locale.glyphs_size);
	kp_ui_set_locale(ui, &locale);
	kp_t_eq_str(T, ui->locale_problem, "", "the language's packs are accepted");
	return ui;
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

/* The acceptance's zero invalid bytes, past what a menu run reaches: every row of every table and every dialog's text. */
static void the_languages_text_is_valid_and_has_glyphs(KpT *T)
{
	KpUi *ui;
	int table, read = 0;
	uint32_t id;
	if (!kp_t_ready(T, "test_locale.real_language", REAL_LANGUAGE)) return;
	ui = real_language(T);
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
	/* a word of the language measures */
	kp_t_eq(T, kp_ui_text_select_font(&ui->text, ui->font_files[1], ui->font_file_sizes[1]), 0, NULL);
	kp_t_ok(T, kp_ui_text_width(&ui->text, (const uint8_t *)"\xd0\x9c\xd0\x95\xd0\x9d\xd0\xae") > 10, "U+041C.. has a width");
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
	size_t k;
	if (!kp_t_ready(T, "test_locale.real_language", REAL_LANGUAGE)) return;
	ui = real_language(T);
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

/* A loaded, not started game of the mission's file. */
static KpSession *loaded_game(KpT *T, const char *path)
{
	KpSession *s = (KpSession *)calloc(1, sizeof *s);
	const uint8_t *dat;
	uint8_t *mission;
	size_t dat_size, mission_size;
	if (!s) kp_t_fail(T, "out of memory");
	kp_t_defer(T, free_session, s);
	dat = kp_t_read(T, "generated/data/dat.kpd", &dat_size);
	if (!(mission = kp_read_file(path, &mission_size))) kp_t_fail(T, "no %s", path);
	if (kp_session_load(s, kp_t_shared(T), dat, dat_size, mission, mission_size, path) != 0) {
		kp_t_fail(T, "%s does not load: %s", path, s->error);
	}
	return s;
}

static void start_game(KpT *T, KpSession *s, const char *path, int expansion)
{
	KpSessionStart start;
	char tileset[128];
	uint8_t *tileset_bytes;
	size_t tileset_size;
	kp_host_tileset_path(&s->host, tileset, sizeof tileset);
	if (!(tileset_bytes = kp_read_file(tileset, &tileset_size))) kp_t_fail(T, "no %s", tileset);
	kp_session_start_defaults(&start);
	start.seed = SEED;
	start.expansion = expansion;
	start.mission_index = kp_oracle_mission_index(".", path);
	if (kp_session_start(s, tileset_bytes, tileset_size, &start) != 0) kp_t_fail(T, "%s does not start: %s", path, s->error);
	kp_t_guard(T, s->g);
}

/* The mission's strings with each character above the control bytes as `with`, in a malloc'd MISN overlay. */
static uint8_t *text_overlay(KpT *T, const KpBytes *strs, const char *with, size_t *size)
{
	size_t room = 2 * STR_LIMIT, with_size = strlen(with);
	uint8_t *out = (uint8_t *)malloc(room), *file = NULL;
	uint32_t count = kp_le_u16(strs->data), k;
	size_t at = 2 + 2 * (size_t)count;
	KpKpfOut section;
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
			if (at + with_size + 1 > STR_LIMIT) kp_t_fail(T, "the test's strings are too long");
			if (b < 0x20) {
				out[at++] = b;
			} else if ((b & 0xc0) != 0x80) {
				memcpy(out + at, with, with_size);
				at += with_size;
			}
		}
		out[at++] = 0;
	}
	memset(&section, 0, sizeof section);
	section.fourcc = "STRS";
	section.payload = out;
	section.size = at;
	kp_t_eq(T, kp_kpf_build("MISN", 1, 0, PROFILE, NULL, &section, 1, &file, size), 0, "the overlay");
	free(out);
	return file;
}

#define MAP "generated/scenarios/marine_group.kpm"

static const char *const NEED_MAP[] = { "generated/data/dat.kpd", MAP, NULL };

static void mission_text_refusals_leave_the_missions_own(KpT *T)
{
	KpSession *s;
	KpError e;
	uint8_t *overlay;
	size_t size;
	const uint8_t *own;
	if (!kp_t_ready(T, "test_locale.mission_text", NEED_MAP)) return;
	s = loaded_game(T, MAP);
	own = s->host.data.mission.strings.data;
	kp_t_ok(T, own != NULL, "the map's strings");
	/* a damaged file */
	overlay = text_overlay(T, &s->host.data.mission.strings, "\xd0\x96", &size);
	overlay[size - 3] ^= 0x55;
	kp_t_eq(T, kp_host_game_text(&s->host, overlay, size, &e), -1, "a damaged file");
	kp_t_ok(T, s->host.data.mission.strings.data == own, "the mission's own");
	/* another count */
	{
		KpKpfOut section;
		uint8_t one[8] = { 1, 0, 4, 0, 'a', 0 }, *file = NULL;
		memset(&section, 0, sizeof section);
		section.fourcc = "STRS";
		section.payload = one;
		section.size = 6;
		kp_t_eq(T, kp_kpf_build("MISN", 1, 0, PROFILE, NULL, &section, 1, &file, &size), 0, NULL);
		kp_t_eq(T, kp_host_game_text(&s->host, file, size, &e), -1, "another count");
		kp_t_ok(T, strstr(e.msg, "count") != NULL, "%s", e.msg);
		/* another kind */
		file = NULL;
		kp_t_eq(T, kp_kpf_build("STRS", 1, 0, PROFILE, NULL, &section, 1, &file, &size), 0, NULL);
		kp_t_eq(T, kp_host_game_text(&s->host, file, size, &e), -1, "another kind");
		/* no strings */
		file = NULL;
		section.fourcc = "TRIG";
		kp_t_eq(T, kp_kpf_build("MISN", 1, 0, PROFILE, NULL, &section, 1, &file, &size), 0, NULL);
		kp_t_eq(T, kp_host_game_text(&s->host, file, size, &e), -1, "no STRS");
	}
	kp_t_ok(T, s->host.data.mission.strings.data == own, "the mission's own");
	/* twice: the second replaces and frees the first */
	overlay = text_overlay(T, &s->host.data.mission.strings, "\xd0\x96", &size);
	kp_t_eq(T, kp_host_game_text(&s->host, overlay, size, &e), 0, "%s", e.msg);
	kp_t_ok(T, s->host.data.mission.strings.data != own, "taken");
	overlay = text_overlay(T, &s->host.data.mission.strings, "z", &size);
	kp_t_eq(T, kp_host_game_text(&s->host, overlay, size, &e), 0, "%s", e.msg);
	start_game(T, s, MAP, 0);
	kp_t_ok(T, s->g != NULL, "the game starts over a language's text");
}

/* The state of a game after the steps, of the mission's own text or a language's overlay (taken). */
static void run_mission(KpT *T, const char *path, uint8_t *overlay, size_t size, int expansion, int steps, KpStateCrcs *out)
{
	int mark = kp_t_mark(T), k;
	KpSession *s = loaded_game(T, path);
	KpError e;
	if (overlay != NULL) kp_t_eq(T, kp_host_game_text(&s->host, overlay, size, &e), 0, "%s: %s", path, e.msg);
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

/* Acceptance of stage L2: the text never reaches the simulation. Every mission with its language's text steps to the
 * state of its own. */
static void missions_with_the_languages_text_keep_their_state(KpT *T)
{
	size_t index_size;
	char *index;
	char *line, *next;
	int k = 0, ran = 0;
	if (!kp_t_ready(T, "test_locale.missions", REAL_MISSIONS)) return;
	{
		uint8_t *raw = kp_t_read(T, MISSIONS "index.txt", &index_size);
		index = (char *)malloc(index_size + 1);
		if (!index) kp_t_fail(T, "out of memory");
		kp_t_defer(T, free, index);
		memcpy(index, raw, index_size);
		index[index_size] = 0;
	}
	for (line = index; line != NULL && *line != 0; line = next) {
		char name[128], path[256], overlay_path[256];
		KpStateCrcs own, theirs;
		uint8_t *overlay;
		size_t size;
		next = strchr(line, '\n');
		if (next != NULL) *next++ = 0;
		if (sscanf(line, "%127s", name) != 1) continue;
		k++;
		if (!kp_t_mine_sampled(T, k)) continue;
		snprintf(path, sizeof path, MISSIONS "%s.kpm", name);
		snprintf(overlay_path, sizeof overlay_path, RU "missions/%s.kpm", name);
		if (!(overlay = kp_read_file(overlay_path, &size))) kp_t_fail(T, "no %s", overlay_path);
		run_mission(T, path, NULL, 0, strncmp(name, "exp", 3) == 0, MISSION_STEPS, &own);
		run_mission(T, path, overlay, size, strncmp(name, "exp", 3) == 0, MISSION_STEPS, &theirs);
		same_crcs(T, &own, &theirs, name);
		ran++;
	}
	kp_t_eq(T, k, 64, "the campaigns' missions");
	kp_t_log(T, "%d missions kept their state over %d steps", ran, MISSION_STEPS);
}

/* The first text a trigger shows (its string and time), 0 when none does. */
static int first_text(KpT *T, const char *path, uint8_t *overlay, size_t size, const char *with, KpEvent *out)
{
	int mark = kp_t_mark(T), k, shown = 0;
	KpSession *s = loaded_game(T, path);
	KpError e;
	kp_t_eq(T, kp_host_game_text(&s->host, overlay, size, &e), 0, "%s", e.msg);
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
	static const char *const PATH = MISSIONS "terran/terran01.kpm";
	static const char *const NEED[] = { "generated/data/dat.kpd", MISSIONS "terran/terran01.kpm", NULL };
	KpEvent one, two;
	uint8_t *narrow, *wide;
	size_t narrow_size, wide_size;
	KpSession *s;
	int mark;
	if (!kp_t_ready(T, "test_locale.mission_text", NEED)) return;
	mark = kp_t_mark(T);
	s = loaded_game(T, PATH);
	narrow = text_overlay(T, &s->host.data.mission.strings, "z", &narrow_size);
	wide = text_overlay(T, &s->host.data.mission.strings, "\xd0\x96", &wide_size);
	kp_t_release(T, mark);
	kp_t_ok(T, wide_size > narrow_size, "more bytes");
	kp_t_ok(T, first_text(T, PATH, narrow, narrow_size, "z", &one), "a text shown");
	kp_t_ok(T, first_text(T, PATH, wide, wide_size, "\xd0\x96", &two), "a text shown");
	kp_t_eq(T, one.a, two.a, "the same string");
	kp_t_eq(T, one.b, two.b, "the same time");
}

static const KpTest TESTS[] = {
	{ "supplement_finds_measures_and_draws", supplement_finds_measures_and_draws, 0 },
	{ "supplement_tells_a_missing_code_once", supplement_tells_a_missing_code_once, 0 },
	{ "supplement_malformed_is_not_taken", supplement_malformed_is_not_taken, 0 },
	{ "fnt_supplement_from_a_truetype_font", fnt_supplement_from_a_truetype_font, 0 },
	{ "tables_past_u16_replace_the_base", tables_past_u16_replace_the_base, 0 },
	{ "other_tables_are_refused", other_tables_are_refused, 0 },
	{ "dialogs_take_the_languages_texts", dialogs_take_the_languages_texts, 0 },
	{ "scripts_are_read_from_the_language_first", scripts_are_read_from_the_language_first, 0 },
	{ "glyphs_join_every_font_and_replace_the_vector_fonts", glyphs_join_every_font_and_replace_the_vector_fonts, 0 },
	{ "the_languages_text_is_valid_and_has_glyphs", the_languages_text_is_valid_and_has_glyphs, 0 },
	{ "the_atlas_holds_the_supplement_at_scale_4", the_atlas_holds_the_supplement_at_scale_4, 0 },
	{ "mission_text_refusals_leave_the_missions_own", mission_text_refusals_leave_the_missions_own, 0 },
	{ "missions_with_the_languages_text_keep_their_state", missions_with_the_languages_text_keep_their_state, 6 },
	{ "a_texts_time_counts_characters", a_texts_time_counts_characters, 0 },
};

KP_SUITE(kp_suite_locale, "test_locale", TESTS);
