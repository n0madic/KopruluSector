/* A language's texts over the base (kp_locale.h): the Python converter's rules until stage C3, in C. */
#include "kp_locale.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kp_chk.h"
#include "kp_cine.h"
#include "kp_host.h"
#include "kp_present.h"
#include "kp_res_scr.h"
#include "kp_scr.h"
#include "kp_text.h"
#include "kp_ui_text.h"

#define REZ "locales/%s/Assets/rez/"
#define PLACEHOLDER "<<PLACEHOLDER>>"
#define CONTROL_END 0x20     /* text bytes under it are colours and layout, never a character to draw */
#define SPACE 0x20           /* the FNT fonts have no glyph for it: kp_ui_text gives it its width */
#define NO_BREAK_SPACE 0xa0  /* nor for U+00A0 */
#define C1_FIRST 0x80
#define C1_LAST 0x9f
/* windows-1252's 0x80..0xa0: the base's FNT fonts have no such character in those slots (a box, font10 other letters),
 * so the supplement holds their code points and the engine looks there before a slot (kp_ui_text.c's glyph_of) */
#define UNTRUE_FIRST 0x80
#define UNTRUE_LAST 0xa0
#define FNT_HEADER 8
#define NEXT_SCENARIO 41      /* KP_ACT_SET_NEXT_SCENARIO: its string is a map's name, looked up by its text */
#define STR_LIMIT 0xffffu
#define NO_STRING 0xffffu     /* a row no text is at */
#define NONE32 0xffffffffu
#define WIDGET_TYPE 2         /* a DLGS record's fields (kpd.UI_WIDGET) */
#define WIDGET_FLAGS 6
#define WIDGET_RECT 10        /* left, top, right, bottom: i16 */
#define WIDGET_SIZE 18        /* width, height: u16 */
#define WIDGET_TEXT_AT 22     /* u32 */
#define WIDGET_TEXT_LEN 26    /* u16 */
#define WIDGET_TEXT_RECT 40   /* a button's extra[4]: the rectangle its text is laid out in (DrawFlcButtonText 0x4c8650) */
#define SMK_SIZE 28           /* kpd.UI_SMK */
#define SMK_NAME_AT 2
#define SMK_NAME_LEN 6
#define TYPE_IMAGE 5
#define TYPE_BUTTON 14
#define HOTKEY_FLAG KP_UI_HOTKEY_PREFIX
#define CODE_POINTS 0x110000
#define WHERE 160             /* a problem's place */
#define LINE 512              /* a problem's line */
#define PATH 256
#define LAYOUT_RECT 1         /* LLAY's flags */
#define LAYOUT_TEXT 2

static const char *const SECTIONS[KP_UI_TBLS] = { "STAT", "NETW", "GLUA", "GHST", "TIPS", "HELP" };
static const char *const TABLES[KP_UI_TBLS] = { "rez/stat_txt.tbl", "rez/network.tbl", "rez/gluall.tbl",
	"rez/gluhist.tbl", "rez/tips.tbl", "rez/help_txt.tbl" }; /* STBL's keys */
const char *const KP_LOCALE_FONTS[KP_UI_VECTOR_FONTS] = { "font/EUROSTILE-REG.TTF", "font/EUROSTILEEXT-REG.TTF" };
static const char *const VFNT_NAMES[KP_UI_VECTOR_FONTS] = { "fonts\\Eurostile.ttf", "fonts\\EurostileExtended.ttf" };
#define WIDE_FONT 4 /* font16x: the wide TrueType font */

/* --- the profile's tables --- */

typedef struct {
	const uint8_t *p, *end;
	int bad;
} Cur;

typedef struct {
	const uint8_t *s;
	size_t n;
} Span;

static Cur defs(const KpDefsProfile *p, const char *fourcc)
{
	const KpDefsTable *t = p ? kp_defs_table(p, fourcc) : NULL;
	Cur c;
	c.p = t ? t->data : NULL;
	c.end = t ? t->data + t->size : NULL;
	c.bad = 0;
	return c;
}

static const uint8_t *take(Cur *c, size_t n)
{
	const uint8_t *at = c->p;
	if (c->bad || !c->p || (size_t)(c->end - c->p) < n) {
		c->bad = 1;
		return NULL;
	}
	c->p += n;
	return at;
}

static uint32_t u8_(Cur *c)
{
	const uint8_t *p = take(c, 1);
	return p ? p[0] : 0;
}

static uint32_t u16_(Cur *c)
{
	const uint8_t *p = take(c, 2);
	return p ? kp_le_u16(p) : 0;
}

static Span name8(Cur *c)
{
	Span s;
	s.n = u8_(c);
	s.s = take(c, s.n);
	if (!s.s) s.n = 0;
	return s;
}

static Span text16(Cur *c)
{
	Span s;
	s.n = u16_(c);
	s.s = take(c, s.n);
	if (!s.s) s.n = 0;
	return s;
}

/* A table's count: 0 when the profile has none (a test profile) */
static uint32_t count_of(Cur *c) { return c->p ? u16_(c) : 0; }

static int span_is(Span s, const char *z) { return s.s && strlen(z) == s.n && memcmp(s.s, z, s.n) == 0; }

static int span_eq(Span s, const uint8_t *p, size_t n) { return s.n == n && (n == 0 || memcmp(s.s, p, n) == 0); }

static char ascii_lower(char c) { return c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c; }

/* n bytes alike but for the case of their ASCII letters */
static int same_nocase(const char *a, const char *b, size_t n)
{
	size_t k;
	for (k = 0; k < n; k++)
		if (ascii_lower(a[k]) != ascii_lower(b[k])) return 0;
	return 1;
}

/* --- what was found --- */

static void push_line(char ***list, int *count, int *cap, const char *line)
{
	char *copy;
	if (*count == *cap) {
		int c = *cap ? 2 * *cap : 16;
		char **g = (char **)realloc(*list, (size_t)c * sizeof *g);
		if (!g) return;
		*list = g;
		*cap = c;
	}
	if ((copy = (char *)malloc(strlen(line) + 1)) != NULL) {
		memcpy(copy, line, strlen(line) + 1);
		(*list)[(*count)++] = copy;
	}
}

static void problem(KpLocaleFound *f, const char *fmt, ...)
{
	char line[LINE];
	va_list ap;
	if (!f) return;
	va_start(ap, fmt);
	vsnprintf(line, sizeof line, fmt, ap);
	va_end(ap);
	push_line(&f->problems, &f->problem_count, &f->problem_cap, line);
}

static void free_lines(char **list, int count)
{
	int k;
	for (k = 0; k < count; k++) free(list[k]);
	free(list);
}

void kp_locale_found_free(KpLocaleFound *f)
{
	free_lines(f->problems, f->problem_count);
	free_lines(f->code_where, f->code_count);
	free_lines(f->exceptions, f->exception_count);
	free_lines(f->mended, f->mended_count);
	free(f->codes);
	free(f->seen);
	memset(f, 0, sizeof *f);
}

/* The code points (from U+0020) of a valid UTF-8 text, each with where it was first seen. */
static void note_codes(KpLocaleFound *f, const char *where, const uint8_t *t, size_t n)
{
	size_t at = 0;
	if (!f) return;
	if (!f->seen && !(f->seen = (uint8_t *)calloc(CODE_POINTS / 8, 1))) return;
	while (at < n) {
		int32_t cp = kp_text_next(t, n, &at);
		if (cp < CONTROL_END || cp >= CODE_POINTS || (f->seen[cp >> 3] & (1u << (cp & 7)))) continue;
		if (f->code_count == f->code_cap) {
			int c = f->code_cap ? 2 * f->code_cap : 256;
			int32_t *g = (int32_t *)realloc(f->codes, (size_t)c * sizeof *g);
			char **w;
			if (!g) return;
			f->codes = g;
			if (!(w = (char **)realloc(f->code_where, (size_t)c * sizeof *w))) return;
			f->code_where = w;
			f->code_cap = c;
		}
		{
			int cap = f->code_count; /* code_where grows with codes */
			push_line(&f->code_where, &cap, &f->code_cap, where);
			if (cap == f->code_count) return; /* out of memory */
		}
		f->codes[f->code_count++] = cp;
		f->seen[cp >> 3] |= (uint8_t)(1u << (cp & 7));
	}
}

/* --- defs/l10n/locales.json's rules (tools/defs_c.py) --- */

typedef struct {
	Span table; /* a section ("STAT"), or a dialog */
	uint32_t row;
	Span key;   /* LEXC's row id */
	Span text;
	int flags;
	int16_t rect[4], text_rect[4];
	Span base;  /* LOWN's: the base's text */
	int used;
} Rule;

typedef struct {
	Rule *items;
	int count;
} Rules;

typedef struct {
	Rules rows, exceptions, templates, keep, texts, layout, strings, own;
} LocaleRules;

/* A rules table's entries of the locale (`fields` its layout after the locale's name; templates have none). */
enum { R_ROWS, R_EXCEPTIONS, R_TEMPLATES, R_KEEP, R_TEXTS, R_LAYOUT, R_STRINGS, R_OWN };

static int read_rules(const KpDefsProfile *p, const char *fourcc, int kind, const char *locale, Rules *out)
{
	Cur c = defs(p, fourcc);
	uint32_t n = count_of(&c), k;
	memset(out, 0, sizeof *out);
	if (!(out->items = (Rule *)calloc(n ? n : 1, sizeof *out->items))) return -1;
	for (k = 0; k < n && !c.bad; k++) {
		Rule r;
		Span loc = { NULL, 0 };
		int j;
		memset(&r, 0, sizeof r);
		if (kind != R_TEMPLATES) loc = name8(&c);
		switch (kind) {
		case R_ROWS:
			r.table.s = take(&c, 4);
			r.table.n = r.table.s ? 4 : 0;
			r.row = u16_(&c);
			r.text = text16(&c);
			break;
		case R_EXCEPTIONS:
			r.table = name8(&c);
			r.key = name8(&c);
			break;
		case R_TEMPLATES:
			r.key = name8(&c);
			r.text = text16(&c);
			break;
		case R_KEEP:
			r.table = name8(&c);
			r.row = u16_(&c);
			break;
		case R_TEXTS:
			r.table = name8(&c);
			r.row = u16_(&c);
			r.text = text16(&c);
			break;
		case R_LAYOUT:
			r.table = name8(&c);
			r.row = u16_(&c);
			r.flags = (int)u8_(&c);
			for (j = 0; j < 4; j++) r.rect[j] = (int16_t)u16_(&c);
			for (j = 0; j < 4; j++) r.text_rect[j] = (int16_t)u16_(&c);
			break;
		case R_STRINGS:
			r.table = name8(&c);
			r.row = u16_(&c);
			r.text = text16(&c);
			break;
		default: /* R_OWN */
			r.table = name8(&c);
			r.base = text16(&c);
			r.text = text16(&c);
			break;
		}
		if (kind == R_TEMPLATES || span_is(loc, locale)) out->items[out->count++] = r;
	}
	return c.bad ? -1 : 0;
}

static void free_rules(LocaleRules *r)
{
	free(r->rows.items);
	free(r->exceptions.items);
	free(r->templates.items);
	free(r->keep.items);
	free(r->texts.items);
	free(r->layout.items);
	free(r->strings.items);
	free(r->own.items);
	memset(r, 0, sizeof *r);
}

static int load_rules(const KpDefsProfile *p, const char *locale, LocaleRules *r)
{
	memset(r, 0, sizeof *r);
	if (read_rules(p, "LROW", R_ROWS, locale, &r->rows) != 0 || read_rules(p, "LEXC", R_EXCEPTIONS, locale, &r->exceptions) != 0
		|| read_rules(p, "LTPL", R_TEMPLATES, locale, &r->templates) != 0 || read_rules(p, "LKEP", R_KEEP, locale, &r->keep) != 0
		|| read_rules(p, "LTXT", R_TEXTS, locale, &r->texts) != 0 || read_rules(p, "LLAY", R_LAYOUT, locale, &r->layout) != 0
		|| read_rules(p, "LSTR", R_STRINGS, locale, &r->strings) != 0 || read_rules(p, "LOWN", R_OWN, locale, &r->own) != 0) {
		free_rules(r);
		return -1;
	}
	return 0;
}

/* The rule of (table or dialog or mission, number), NULL none */
static Rule *rule_of(Rules *rs, const char *table, size_t table_len, uint32_t row)
{
	int k;
	for (k = 0; k < rs->count; k++)
		if (rs->items[k].row == row && span_eq(rs->items[k].table, (const uint8_t *)table, table_len)) return &rs->items[k];
	return NULL;
}

/* --- the texts' checks --- */

/* A buffer of bytes */
typedef struct {
	uint8_t *p;
	size_t n, cap;
	int bad;
} Buf;

static void put(Buf *b, const void *data, size_t n)
{
	if (b->bad) return;
	if (b->n + n + 1 > b->cap) {
		size_t c = b->cap ? b->cap : 256;
		uint8_t *g;
		while (c < b->n + n + 1) c *= 2;
		if (!(g = (uint8_t *)realloc(b->p, c))) {
			b->bad = 1;
			return;
		}
		b->p = g;
		b->cap = c;
	}
	if (n) memcpy(b->p + b->n, data, n);
	b->n += n;
	b->p[b->n] = 0;
}

static void put_u16(Buf *b, uint32_t v)
{
	uint8_t x[2];
	kp_le_put16(x, v);
	put(b, x, 2);
}

static void put_u32(Buf *b, uint32_t v)
{
	uint8_t x[4];
	kp_le_put32(x, v);
	put(b, x, 4);
}

/* The text with Remastered's templates filled in (locales.json's `templates`) and each C1 control (U+0080..U+009F)
 * turned into the windows-1252 character of its byte, one windows-1252 leaves undefined kept (a tool decoded a
 * windows-1252 text as Latin-1 on the way to UTF-8: deDE's zerg07 has an en dash as U+0096). A text that is not UTF-8
 * keeps its C1 bytes (note_text's problem). Malloc'd into *out; where is listed when it changed. */
static int mend(const Rules *templates, KpLocaleFound *f, const char *where, const uint8_t *text, size_t n, uint8_t **out,
	size_t *out_n)
{
	Buf a = { NULL, 0, 0, 0 }, b = { NULL, 0, 0, 0 };
	int k, c1 = 0;
	size_t at;
	put(&a, text, n);
	for (k = 0; k < templates->count && !a.bad; k++) {
		const Rule *t = &templates->items[k];
		Buf next = { NULL, 0, 0, 0 };
		for (at = 0; at < a.n;) {
			if (t->key.n && a.n - at >= t->key.n && memcmp(a.p + at, t->key.s, t->key.n) == 0) {
				put(&next, t->text.s, t->text.n);
				at += t->key.n;
			} else {
				put(&next, a.p + at, 1);
				at++;
			}
		}
		put(&next, "", 0);
		free(a.p);
		a = next;
	}
	if (a.bad) {
		free(a.p);
		return -1;
	}
	if (kp_text_valid_utf8(a.p, a.n)) {
		for (at = 0; at < a.n;) {
			int32_t cp = kp_text_next(a.p, a.n, &at);
			if (cp >= C1_FIRST && cp <= C1_LAST) c1 = 1;
		}
	}
	if (c1) {
		for (at = 0; at < a.n;) {
			int32_t cp = kp_text_next(a.p, a.n, &at);
			uint8_t enc[KP_TEXT_UTF8_MAX];
			/* Python's cp1252 leaves 0x81, 0x8d, 0x8f, 0x90 and 0x9d undefined */
			if (cp >= C1_FIRST && cp <= C1_LAST && cp != 0x81 && cp != 0x8d && cp != 0x8f && cp != 0x90 && cp != 0x9d)
				cp = kp_text_from_byte((uint8_t)cp);
			put(&b, enc, kp_text_encode(cp, enc));
		}
		put(&b, "", 0);
		free(a.p);
		if (b.bad) {
			free(b.p);
			return -1;
		}
		a = b;
	}
	if (f && (a.n != n || memcmp(a.p, text, n) != 0)) push_line(&f->mended, &f->mended_count, &f->mended_cap, where);
	*out = a.p;
	*out_n = a.n;
	return 0;
}

/* SPECIFIER's next match (%[-+ #0]*\d*(?:\.\d+)?[a-zA-Z%]?) at or after *at: 1 and its span, 0 none. */
static int next_specifier(const uint8_t *t, size_t n, size_t *at, size_t *from, size_t *len)
{
	size_t i = *at, j;
	while (i < n && t[i] != '%') i++;
	if (i >= n) return 0;
	j = i + 1;
	while (j < n && (t[j] == '-' || t[j] == '+' || t[j] == ' ' || t[j] == '#' || t[j] == '0')) j++;
	while (j < n && t[j] >= '0' && t[j] <= '9') j++;
	if (j + 1 < n && t[j] == '.' && t[j + 1] >= '0' && t[j + 1] <= '9') {
		j++;
		while (j < n && t[j] >= '0' && t[j] <= '9') j++;
	}
	if (j < n && ((t[j] >= 'a' && t[j] <= 'z') || (t[j] >= 'A' && t[j] <= 'Z') || t[j] == '%')) j++;
	*from = i;
	*len = j - i;
	*at = j;
	return 1;
}

/* Whether both texts have the same printf conversions, in order. */
static int same_specifiers(const uint8_t *a, size_t an, const uint8_t *b, size_t bn)
{
	size_t ia = 0, ib = 0;
	for (;;) {
		size_t fa, la, fb, lb;
		int ha = next_specifier(a, an, &ia, &fa, &la), hb = next_specifier(b, bn, &ib, &fb, &lb);
		if (ha != hb) return 0;
		if (!ha) return 1;
		if (la != lb || memcmp(a + fa, b + fb, la) != 0) return 0;
	}
}

static size_t count_byte(const uint8_t *t, size_t n, uint8_t c)
{
	size_t k, m = 0;
	for (k = 0; k < n; k++) m += t[k] == c;
	return m;
}

/* The control bytes a text uses, as a 32-bit set */
static uint32_t controls_of(const uint8_t *t, size_t n)
{
	uint32_t set = 0;
	size_t k;
	for (k = 0; k < n; k++)
		if (t[k] < CONTROL_END) set |= 1u << t[k];
	return set;
}

/* No control byte the base does not use, none of Remastered's substitutions (\{[A-Za-z_]+\}): 1, else 0 and a problem. */
static int marks_ok(KpLocaleFound *f, const char *where, const uint8_t *t, size_t n, uint32_t controls)
{
	uint32_t strange = controls_of(t, n) & ~controls;
	size_t k;
	int ok = 1;
	if (strange) {
		problem(f, "%s: control bytes the base does not use (mask 0x%08x)", where, (unsigned)strange);
		ok = 0;
	}
	for (k = 0; k < n; k++) {
		size_t j = k + 1;
		if (t[k] != '{') continue;
		while (j < n && ((t[j] >= 'A' && t[j] <= 'Z') || (t[j] >= 'a' && t[j] <= 'z') || t[j] == '_')) j++;
		if (j > k + 1 && j < n && t[j] == '}') {
			problem(f, "%s: a substitution the engine does not make: %.*s", where, (int)(j + 1 - k), (const char *)t + k);
			return 0;
		}
	}
	return ok;
}

/* A row of a table or a dialog against the base's (Findings.check_row): UTF-8, the same printf conversions (or an
 * exception of locales.json's), the same NULs, the marks; 1 taken (its code points noted), 0 with its problems. */
static int row_ok(KpLocaleFound *f, const Rules *exceptions, const char *table, const char *row, const uint8_t *base,
	size_t bn, const uint8_t *text, size_t tn, uint32_t controls)
{
	char where[WHERE];
	int ok = 1, k;
	snprintf(where, sizeof where, "%s %s", table, row);
	if (!kp_text_valid_utf8(text, tn)) {
		problem(f, "%s: not UTF-8", where);
		ok = 0;
	}
	if (!same_specifiers(base, bn, text, tn)) {
		for (k = 0; k < exceptions->count; k++)
			if (span_is(exceptions->items[k].table, table) && span_is(exceptions->items[k].key, row)) break;
		if (k < exceptions->count) {
			if (f) push_line(&f->exceptions, &f->exception_count, &f->exception_cap, where);
		} else {
			problem(f, "%s: its printf conversions are not the base's", where);
			ok = 0;
		}
	}
	if (count_byte(text, tn, 0) != count_byte(base, bn, 0)) {
		problem(f, "%s: %zu NULs became %zu", where, count_byte(base, bn, 0), count_byte(text, tn, 0));
		ok = 0;
	}
	if (!marks_ok(f, where, text, tn, controls)) ok = 0;
	if (ok) note_codes(f, where, text, tn);
	return ok;
}

/* --- the language's string XML --- */

typedef struct {
	char path[PATH];
	KpScrStrings strings; /* the values mended; a bad one (an escape past a byte) left out of every row */
} Xml;

typedef struct {
	KpLocale *l;
	const KpUiData *d;
	const KpFiles *raw;
	const char *locale;
	LocaleRules rules;
	KpResScr *scr;
	Xml *xml;
	int xml_count, xml_cap;
	int oom;
} Build;

/* A file of the language's source: its bytes, NULL with a problem when it is there but does not read (or, `needed`,
 * when it is not there either). */
static uint8_t *read_raw(Build *b, const char *path, size_t *size, int needed)
{
	char why[256] = "";
	uint8_t *data = b->raw->read(b->raw->ctx, path, size, why, sizeof why);
	if (!data && (why[0] || needed)) problem(&b->l->found, "%s: %s", path, why[0] ? why : "not in the data");
	return data;
}

/* The language's copy of a manifest's XML ("SD/rez/stat_txt.xml" as locales/<locale>/Assets/rez/stat_txt.xml), read
 * once: its strings, none when it does not read (a problem). */
static const KpScrStrings *xml_of(Build *b, const char *xml, size_t xml_len)
{
	const char *slash;
	char path[PATH], why[KP_SCR_ERR];
	size_t size = 0, base_len;
	uint8_t *raw;
	Xml *x;
	int k;
	uint32_t i;
	for (slash = xml + xml_len; slash > xml && slash[-1] != '/' && slash[-1] != '\\'; slash--) {}
	base_len = (size_t)(xml + xml_len - slash);
	snprintf(path, sizeof path, REZ "%.*s", b->locale, (int)base_len, slash);
	for (k = 0; k < b->xml_count; k++)
		if (strcmp(b->xml[k].path, path) == 0) return &b->xml[k].strings;
	if (b->xml_count == b->xml_cap) {
		int c = b->xml_cap ? 2 * b->xml_cap : 8;
		Xml *g = (Xml *)realloc(b->xml, (size_t)c * sizeof *g);
		if (!g) {
			b->oom = 1;
			return NULL;
		}
		b->xml = g;
		b->xml_cap = c;
	}
	x = &b->xml[b->xml_count++];
	memset(x, 0, sizeof *x);
	snprintf(x->path, sizeof x->path, "%s", path);
	if (!(raw = read_raw(b, path, &size, 1))) return &x->strings;
	if (kp_scr_xml_strings(raw, size, KP_SCR_XML_LANGUAGE, &x->strings, why, sizeof why) != 0) {
		problem(&b->l->found, "%s: %s", path, why);
		memset(&x->strings, 0, sizeof x->strings);
	}
	free(raw);
	for (i = 0; i < x->strings.count; i++) {
		KpScrString *s = &x->strings.items[i];
		char where[WHERE];
		uint8_t *m;
		size_t mn;
		snprintf(where, sizeof where, "%s %s", path, s->id);
		if (s->bad) {
			problem(&b->l->found, "%s: an escape is past a byte", where);
			continue;
		}
		if (mend(&b->rules.templates, &b->l->found, where, s->value, s->size, &m, &mn) != 0) {
			b->oom = 1;
			continue;
		}
		free(s->value);
		s->value = m;
		s->size = mn;
	}
	return &x->strings;
}

/* A string of the language's XML by its id, or by the id with its last "_N" as "-N" (the manifest numbers a run of rows
 * so); NULL none */
static const KpScrString *xml_string(const KpScrStrings *s, const char *id, size_t n)
{
	const KpScrString *v = kp_scr_string(s, id, n);
	char alt[PATH];
	size_t k = n;
	if (v || n >= sizeof alt) return v;
	while (k > 0 && id[k - 1] >= '0' && id[k - 1] <= '9') k--;
	if (k == n || k == 0 || id[k - 1] != '_') return NULL;
	memcpy(alt, id, n);
	alt[k - 1] = '-';
	return kp_scr_string(s, alt, n);
}

/* --- the tables --- */

/* The marks of a span that may be a NUL in a language's XML (a NUL, KP_SCR_TBL_NUL_IN_XML) */
static size_t nul_marks(const uint8_t *t, size_t n)
{
	return count_byte(t, n, 0) + count_byte(t, n, KP_SCR_TBL_NUL_IN_XML);
}

/* The language's text with the NULs its XML writes as 0x07 (a colour code as well), each of the base's marks saying
 * which it is (with_nuls), in place. */
static void with_nuls(const uint8_t *base, size_t bn, uint8_t *text, size_t tn)
{
	size_t k, b = 0, seen = 0;
	for (k = 0; k < tn; k++) {
		if (text[k] != 0 && text[k] != KP_SCR_TBL_NUL_IN_XML) continue;
		while (b < bn && base[b] != 0 && base[b] != KP_SCR_TBL_NUL_IN_XML) b++;
		if (b < bn) {
			if (base[b] == 0) text[k] = 0;
			b++;
		}
		seen++;
	}
	(void)seen;
}

/* A TBL's button text (a hotkey, then a mode byte) keeps its form: an ASCII hotkey, the base's mode. */
static int button_form(const uint8_t *base, size_t bn, const uint8_t *text, size_t tn)
{
	if (bn < 2 || base[0] < 0x21 || base[0] > 0x7e || base[1] >= CONTROL_END) return 1;
	return tn >= 2 && text[0] >= 0x21 && text[0] <= 0x7e && text[1] == base[1];
}

static int is_placeholder(const uint8_t *t, size_t n) { return n == strlen(PLACEHOLDER) && memcmp(t, PLACEHOLDER, n) == 0; }

/* A TBL of u32 offsets: u32 count, the offsets from its start, each text with its final NUL. */
static uint8_t *wide_table(const KpScrSpan *spans, uint32_t count, size_t *size)
{
	Buf b = { NULL, 0, 0, 0 };
	uint32_t k, at = 4 + 4 * count;
	put_u32(&b, count);
	for (k = 0; k < count; k++) {
		put_u32(&b, at);
		at += (uint32_t)spans[k].size + 1;
	}
	for (k = 0; k < count; k++) put(&b, spans[k].data, spans[k].size), put(&b, "", 1);
	if (b.bad) {
		free(b.p);
		return NULL;
	}
	*size = b.n;
	return b.p;
}

/* Table i of the base (convert_tables): each row the language's (the manifest's rows, locales.json's shorter ones) or
 * the base's. */
static void build_table(Build *b, int i)
{
	KpLocale *l = b->l;
	KpLocaleFound *f = &l->found;
	KpResScrTable t;
	KpScrSpan *spans = NULL, *merged = NULL;
	uint8_t **owned = NULL;
	uint32_t count = 0, k, controls = 0;
	const KpScrStrings *xml;
	Cur ids;
	const uint8_t *base = b->d->tbl[i].data;
	if (!base || kp_scr_tbl_spans(base, b->d->tbl[i].size, &spans, &count) != 0) {
		problem(f, "%s: the base's table does not read", SECTIONS[i]);
		return;
	}
	if (kp_res_scr_table(b->scr, TABLES[i], &t) != 0) {
		problem(f, "%s: the profile has no manifest of it (STBL)", SECTIONS[i]);
		goto done;
	}
	if (t.ids != count) {
		problem(f, "%s: the manifest has %u rows for the base's %u", SECTIONS[i], (unsigned)t.ids, (unsigned)count);
		goto done;
	}
	if (!(xml = xml_of(b, t.xml, t.xml_len))) goto done;
	for (k = 0; k < count; k++) controls |= controls_of(spans[k].data, spans[k].size);
	merged = (KpScrSpan *)calloc(count ? count : 1, sizeof *merged);
	owned = (uint8_t **)calloc(count ? count : 1, sizeof *owned);
	if (!merged || !owned) {
		b->oom = 1;
		goto done;
	}
	ids.p = t.ids_at;
	ids.end = t.ids_end;
	ids.bad = 0;
	l->tables[i].rows = (int)count;
	for (k = 0; k < count; k++) {
		uint32_t n = u8_(&ids);
		const char *id = n == 0xff ? NULL : (const char *)take(&ids, n);
		const uint8_t *text = NULL;
		size_t tn = 0;
		char key[PATH];
		Rule *fix;
		key[0] = 0;
		merged[k] = spans[k];
		if (t.base_rows) { /* stat_txt: the XML holds Remastered's TBL's rows, then new ones */
			if (k < t.base_rows && k < xml->count && !xml->items[k].bad) {
				text = xml->items[k].value;
				tn = xml->items[k].size;
				snprintf(key, sizeof key, "%s", xml->items[k].id);
			}
		} else if (id && n) {
			const KpScrString *v = xml_string(xml, id, n);
			snprintf(key, sizeof key, "%.*s", (int)n, id);
			if (v && !v->bad) {
				text = v->value;
				tn = v->size;
			}
		}
		if (!key[0]) snprintf(key, sizeof key, "%u", (unsigned)k);
		if ((fix = rule_of(&b->rules.rows, SECTIONS[i], 4, k + 1)) != NULL) {
			fix->used = 1;
			text = fix->text.s;
			tn = fix->text.n;
			l->tables[i].shorter++;
		}
		if (!text || !tn || is_placeholder(text, tn) || !spans[k].size) continue; /* the base's */
		if (!(owned[k] = (uint8_t *)malloc(tn + 1))) {
			b->oom = 1;
			goto done;
		}
		memcpy(owned[k], text, tn);
		owned[k][tn] = 0;
		if (memchr(spans[k].data, 0, spans[k].size) && nul_marks(owned[k], tn) != nul_marks(spans[k].data, spans[k].size)) {
			problem(f, "%s %s: %zu marks that may be NULs became %zu", SECTIONS[i], key,
				nul_marks(spans[k].data, spans[k].size), nul_marks(owned[k], tn));
			continue;
		}
		with_nuls(spans[k].data, spans[k].size, owned[k], tn);
		if (!row_ok(f, &b->rules.exceptions, SECTIONS[i], key, spans[k].data, spans[k].size, owned[k], tn, controls))
			continue;
		if (!button_form(spans[k].data, spans[k].size, owned[k], tn)) {
			problem(f, "%s %s: the button form of the base's is lost", SECTIONS[i], key);
			continue;
		}
		merged[k].data = owned[k];
		merged[k].size = tn;
		l->tables[i].translated++;
	}
	if (ids.bad) {
		problem(f, "%s: the profile's STBL is malformed", SECTIONS[i]);
		goto done;
	}
	if (!(l->tbl[i] = wide_table(merged, count, &l->tbl_size[i]))) b->oom = 1;
	l->tables[i].bytes = l->tbl_size[i];
done:
	if (owned)
		for (k = 0; k < count; k++) free(owned[k]);
	free(owned);
	free(merged);
	free(spans);
}

/* --- the dialogs --- */

typedef struct {
	const uint8_t *name;
	size_t name_len;
	uint32_t widgets, smks;
	const uint8_t *records, *nodes;
} Dialog;

/* DLGS's dialogs (u16 count; per dialog u8 name length, name, u16 widgets, u16 SMK nodes, the records): malloc'd, NULL
 * when malformed or out of memory */
static Dialog *read_dialogs(const uint8_t *dlgs, size_t size, int *count)
{
	Cur c;
	Dialog *out;
	int n, k;
	c.p = dlgs;
	c.end = dlgs + size;
	c.bad = 0;
	n = (int)u16_(&c);
	if (c.bad || !(out = (Dialog *)calloc((size_t)n + 1, sizeof *out))) return NULL;
	for (k = 0; k < n && !c.bad; k++) {
		Span s = name8(&c);
		out[k].name = s.s;
		out[k].name_len = s.n;
		out[k].widgets = u16_(&c);
		out[k].smks = u16_(&c);
		out[k].records = take(&c, (size_t)out[k].widgets * KP_UI_WIDGET_SIZE);
		out[k].nodes = take(&c, (size_t)out[k].smks * SMK_SIZE);
	}
	if (c.bad) {
		free(out);
		return NULL;
	}
	*count = n;
	return out;
}

/* A record's or a node's text in a POOL: NULL for none (NONE32) or one past the pool */
static const uint8_t *pool_text(const uint8_t *pool, size_t pool_size, uint32_t at, uint32_t len)
{
	if (at == NONE32 || (size_t)at + len >= pool_size + (len == 0)) return NULL;
	return pool + at;
}

/* Two records alike but for where their text is (bare_record) */
static int same_record(const uint8_t *a, const uint8_t *b, size_t size, size_t text_at, size_t text_end)
{
	return memcmp(a, b, text_at) == 0 && memcmp(a + text_end, b + text_end, size - text_end) == 0;
}

typedef struct {
	Build *b;
} TextOf;

/* A dialog's text by its source "id:<xml>#<id>": the language's string, "" for a placeholder; NULL none. */
static const uint8_t *dialog_text(void *ctx, const char *xml, size_t xml_len, const char *id, size_t id_len, size_t *size)
{
	Build *b = (Build *)ctx;
	const KpScrStrings *s = xml_of(b, xml, xml_len);
	const KpScrString *v = s ? xml_string(s, id, id_len) : NULL;
	if (!v || v->bad) return NULL;
	if (!kp_text_valid_utf8(v->value, v->size)) { /* kp_ui_dialogs_build would read it as windows-1252 */
		problem(&b->l->found, "%.*s %.*s: not UTF-8", (int)xml_len, xml, (int)id_len, id);
		return NULL;
	}
	if (is_placeholder(v->value, v->size)) {
		*size = 0;
		return (const uint8_t *)"";
	}
	*size = v->size;
	return v->value;
}

/* The records our extensions append to dialog d (KEXT's, its root container left out) */
static uint32_t appended(const KpUiData *d, const Dialog *dl)
{
	const KpDefsTable *kext = kp_defs_table(d->profile, "KEXT");
	KpUiFiles ext = { NULL, 0 };
	uint32_t n = 0;
	int k;
	if (!kext || kp_ui_files_section(kext->data, kext->size, &ext) != 0) {
		free(ext.files);
		return 0;
	}
	for (k = 0; k < ext.count; k++) {
		KpUiDialogSource src;
		KpUiDialogs out;
		char err[KP_SCR_ERR], name[PATH];
		if (ext.files[k].path_len != dl->name_len || !same_nocase(ext.files[k].path, (const char *)dl->name, dl->name_len))
			continue;
		snprintf(name, sizeof name, "%.*s", (int)dl->name_len, (const char *)dl->name);
		memset(&src, 0, sizeof src);
		src.name = name;
		src.bin = ext.files[k].data;
		src.bin_size = ext.files[k].size;
		if (kp_ui_dialogs_build(&src, 1, &out, err, sizeof err) == 0) {
			n = kp_le_u16(out.dlgs + 3 + dl->name_len) - 1;
			kp_ui_dialogs_free(&out);
		}
		break;
	}
	free(ext.files);
	return n;
}

/* A text for a record whose flags say its first byte is the hotkey must start with an ASCII one */
static int hotkey_ok(KpLocaleFound *f, const char *where, uint32_t flags, const uint8_t *t, size_t n)
{
	if (!(flags & HOTKEY_FLAG) || (n && t[0] < KP_TEXT_ASCII_END)) return 1;
	problem(f, "%s: the hotkey of the text is not ASCII", where);
	return 0;
}

/* Set a record's rectangle and its button's text rectangle from a layout entry (apply_layout): 1, else 0 and a problem. */
static int apply_layout(KpLocaleFound *f, const char *where, uint8_t *rec, const Rule *fix)
{
	int k;
	if (!(fix->flags & (LAYOUT_RECT | LAYOUT_TEXT))) {
		problem(f, "%s: the layout gives neither rect nor text", where);
		return 0;
	}
	for (k = 0; k < 2; k++) {
		const int16_t *r = k == 0 ? fix->rect : fix->text_rect;
		if ((fix->flags & (k == 0 ? LAYOUT_RECT : LAYOUT_TEXT)) && !(r[0] <= r[2] && r[1] <= r[3])) {
			problem(f, "%s: the layout's %s [%d, %d, %d, %d] is empty", where, k == 0 ? "rect" : "text", r[0], r[1], r[2], r[3]);
			return 0;
		}
	}
	if ((fix->flags & LAYOUT_TEXT) && kp_le_u16(rec + WIDGET_TYPE) != TYPE_BUTTON) {
		problem(f, "%s: the layout's text rectangle is for a button (type %d)", where, TYPE_BUTTON);
		return 0;
	}
	if (fix->flags & LAYOUT_RECT) {
		for (k = 0; k < 4; k++) kp_le_put16(rec + WIDGET_RECT + 2 * k, (uint16_t)fix->rect[k]);
		kp_le_put16(rec + WIDGET_SIZE, (uint16_t)(fix->rect[2] - fix->rect[0] + 1));
		kp_le_put16(rec + WIDGET_SIZE + 2, (uint16_t)(fix->rect[3] - fix->rect[1] + 1));
	}
	if (fix->flags & LAYOUT_TEXT)
		for (k = 0; k < 4; k++) kp_le_put16(rec + WIDGET_TEXT_RECT + 2 * k, (uint16_t)fix->text_rect[k]);
	return 1;
}

/* The language's dialog of base dialog dl, rebuilt from the profile's SBIN (convert_dialogs' widgets): their DLGS and
 * POOL, 1; 0 when the profile has none of it or it does not fit (a problem). */
static int their_dialog(Build *b, const Dialog *dl, KpUiDialogs *out, Dialog *their)
{
	char key[PATH], name[PATH], err[KP_SCR_ERR];
	size_t k, size = 0;
	uint8_t *bin;
	KpUiDialogSource src;
	Dialog *parsed;
	int n = 0;
	uint32_t own = dl->widgets - appended(b->d, dl), r;
	snprintf(name, sizeof name, "%.*s", (int)dl->name_len, (const char *)dl->name);
	for (k = 0; name[k] && k + 1 < sizeof key; k++) key[k] = name[k] == '\\' ? '/' : ascii_lower(name[k]);
	key[k] = 0;
	if (!(bin = kp_res_scr_dialog(b->scr, key, dialog_text, b, &size))) return 0; /* none: ours */
	memset(&src, 0, sizeof src);
	src.name = name;
	src.bin = bin;
	src.bin_size = size;
	if (kp_ui_dialogs_build(&src, 1, out, err, sizeof err) != 0) {
		free(bin);
		problem(&b->l->found, "%s: the language's dialog does not read: %s", name, err);
		return 0;
	}
	free(bin);
	if (!(parsed = read_dialogs(out->dlgs, out->dlgs_size, &n)) || n != 1) {
		free(parsed);
		kp_ui_dialogs_free(out);
		return 0;
	}
	*their = parsed[0];
	free(parsed);
	if (their->widgets != own || their->smks != dl->smks) {
		problem(&b->l->found, "%s: %u records and %u movies became %u and %u", name, (unsigned)own, (unsigned)dl->smks,
			(unsigned)their->widgets, (unsigned)their->smks);
		kp_ui_dialogs_free(out);
		return 0;
	}
	for (r = 0; r < own; r++) {
		if (!same_record(dl->records + (size_t)r * KP_UI_WIDGET_SIZE, their->records + (size_t)r * KP_UI_WIDGET_SIZE,
				KP_UI_WIDGET_SIZE, WIDGET_TEXT_AT, WIDGET_TEXT_LEN + 2)) {
			problem(&b->l->found, "%s record %u: not the base's", name, (unsigned)r);
			kp_ui_dialogs_free(out);
			return 0;
		}
	}
	for (r = 0; r < dl->smks; r++) {
		if (!same_record(dl->nodes + (size_t)r * SMK_SIZE, their->nodes + (size_t)r * SMK_SIZE, SMK_SIZE, SMK_NAME_AT,
				SMK_NAME_LEN + 2)) {
			problem(&b->l->found, "%s movie %u: not the base's", name, (unsigned)r);
			kp_ui_dialogs_free(out);
			return 0;
		}
	}
	return 1;
}

/* A text into the new POOL: its offset and length, (NONE32, 0) for none */
static void place(Buf *pool, const uint8_t *text, size_t n, uint32_t *at, uint32_t *len)
{
	if (!text) {
		*at = NONE32;
		*len = 0;
		return;
	}
	*at = (uint32_t)pool->n;
	*len = (uint32_t)n;
	put(pool, text, n);
	put(pool, "", 1);
}

static void build_dialogs(Build *b)
{
	KpLocale *l = b->l;
	KpLocaleFound *f = &l->found;
	const KpUiData *d = b->d;
	Dialog *ours;
	int n = 0, k, j;
	uint32_t controls;
	Buf out = { NULL, 0, 0, 0 }, pool = { NULL, 0, 0, 0 };
	if (!d->dlgs.data || !(ours = read_dialogs(d->dlgs.data, d->dlgs.size, &n))) {
		problem(f, "the base's dialogs do not read");
		return;
	}
	controls = controls_of(d->pool.data, d->pool.size);
	for (j = 0; j < b->rules.own.count; j++) { /* our texts name dialogs that are there */
		Rule *o = &b->rules.own.items[j];
		for (k = 0; k < n && !span_eq(o->table, ours[k].name, ours[k].name_len); k++) {}
		if (k == n) {
			problem(f, "koprulu-%s.json: no dialog %.*s", b->locale, (int)o->table.n, (const char *)o->table.s);
			o->used = 1;
		}
	}
	put_u16(&out, (uint32_t)n);
	for (k = 0; k < n; k++) {
		const Dialog *dl = &ours[k];
		KpUiDialogs theirs_out;
		Dialog theirs;
		char name[PATH];
		uint32_t r, own_count = 0;
		int have = their_dialog(b, dl, &theirs_out, &theirs);
		if (have) own_count = theirs.widgets;
		snprintf(name, sizeof name, "%.*s", (int)dl->name_len, (const char *)dl->name);
		put(&out, (const uint8_t[]){ (uint8_t)dl->name_len }, 1);
		put(&out, dl->name, dl->name_len);
		put_u16(&out, dl->widgets);
		put_u16(&out, dl->smks);
		for (r = 0; r < dl->widgets; r++) {
			uint8_t rec[KP_UI_WIDGET_SIZE];
			uint32_t at, len, flags, type;
			const uint8_t *text, *use;
			size_t text_n, use_n;
			char where[WHERE], row[16];
			int name_like;
			Rule *rule;
			memcpy(rec, dl->records + (size_t)r * KP_UI_WIDGET_SIZE, sizeof rec);
			flags = kp_le_u32(rec + WIDGET_FLAGS);
			type = kp_le_u16(rec + WIDGET_TYPE);
			text = pool_text(d->pool.data, d->pool.size, kp_le_u32(rec + WIDGET_TEXT_AT), kp_le_u16(rec + WIDGET_TEXT_LEN));
			text_n = text ? kp_le_u16(rec + WIDGET_TEXT_LEN) : 0;
			use = text;
			use_n = text_n;
			name_like = r == 0 || type == TYPE_IMAGE;
			snprintf(where, sizeof where, "%s record %u", name, (unsigned)r);
			snprintf(row, sizeof row, "%u", (unsigned)r);
			rule = NULL;
			if (text && text_n && !name_like) { /* ours: a record we add or label */
				for (j = 0; j < b->rules.own.count; j++) {
					Rule *o = &b->rules.own.items[j];
					if (span_eq(o->table, dl->name, dl->name_len) && span_eq(o->base, text, text_n)) {
						rule = o;
						break;
					}
				}
			}
			if (rule) {
				rule->used = 1;
				if (hotkey_ok(f, where, flags, rule->text.s, rule->text.n)
					&& row_ok(f, &b->rules.exceptions, name, row, text, text_n, rule->text.s, rule->text.n, controls)) {
					use = rule->text.s;
					use_n = rule->text.n;
					l->dialogs_translated++;
				}
			} else if ((rule = rule_of(&b->rules.keep, name, strlen(name), r)) != NULL) {
				rule->used = 1;
				l->dialogs_kept += text != NULL && text_n > 0;
			} else if ((rule = rule_of(&b->rules.texts, name, strlen(name), r)) != NULL) {
				rule->used = 1;
				if (hotkey_ok(f, where, flags, rule->text.s, rule->text.n)
					&& row_ok(f, &b->rules.exceptions, name, row, text ? text : (const uint8_t *)"", text_n, rule->text.s,
						rule->text.n, controls)) {
					use = rule->text.s;
					use_n = rule->text.n;
					l->dialogs_translated++;
				}
			} else if (have && r < own_count && !name_like && text && text_n) { /* a name stays: a file's or the dialog's */
				const uint8_t *t = theirs.records + (size_t)r * KP_UI_WIDGET_SIZE;
				uint32_t tlen = kp_le_u16(t + WIDGET_TEXT_LEN);
				const uint8_t *tt = pool_text(theirs_out.pool, theirs_out.pool_size, kp_le_u32(t + WIDGET_TEXT_AT), tlen);
				if (tt && tlen) {
					if (hotkey_ok(f, where, flags, tt, tlen)
						&& row_ok(f, &b->rules.exceptions, name, row, text, text_n, tt, tlen, controls)) {
						use = tt;
						use_n = tlen;
						l->dialogs_translated++;
					}
				} else {
					l->dialogs_kept++;
				}
			} else if (text && text_n && !name_like) {
				l->dialogs_kept++;
			}
			place(&pool, use, use_n, &at, &len);
			kp_le_put32(rec + WIDGET_TEXT_AT, at);
			kp_le_put16(rec + WIDGET_TEXT_LEN, len);
			if ((rule = rule_of(&b->rules.layout, name, strlen(name), r)) != NULL) {
				rule->used = 1;
				if (apply_layout(f, where, rec, rule)) l->dialogs_laid_out++;
			}
			put(&out, rec, sizeof rec);
		}
		for (r = 0; r < dl->smks; r++) {
			uint8_t node[SMK_SIZE];
			uint32_t at, len;
			const uint8_t *t;
			memcpy(node, dl->nodes + (size_t)r * SMK_SIZE, sizeof node);
			len = kp_le_u16(node + SMK_NAME_LEN);
			t = pool_text(d->pool.data, d->pool.size, kp_le_u32(node + SMK_NAME_AT), len);
			place(&pool, t, t ? len : 0, &at, &len);
			kp_le_put32(node + SMK_NAME_AT, at);
			kp_le_put16(node + SMK_NAME_LEN, len);
			put(&out, node, sizeof node);
		}
		if (have) {
			/* the text of a record the language has but whose base has none is not taken: the base decides */
			kp_ui_dialogs_free(&theirs_out);
		}
	}
	for (j = 0; j < b->rules.layout.count; j++)
		if (!b->rules.layout.items[j].used)
			problem(f, "%.*s record %u: the layout names no such record", (int)b->rules.layout.items[j].table.n,
				(const char *)b->rules.layout.items[j].table.s, (unsigned)b->rules.layout.items[j].row);
	for (j = 0; j < b->rules.keep.count; j++)
		if (!b->rules.keep.items[j].used)
			problem(f, "%.*s record %u: locales.json keeps the base's text of no such record", (int)b->rules.keep.items[j].table.n,
				(const char *)b->rules.keep.items[j].table.s, (unsigned)b->rules.keep.items[j].row);
	for (j = 0; j < b->rules.texts.count; j++)
		if (!b->rules.texts.items[j].used)
			problem(f, "%.*s record %u: locales.json gives a text to no such record", (int)b->rules.texts.items[j].table.n,
				(const char *)b->rules.texts.items[j].table.s, (unsigned)b->rules.texts.items[j].row);
	for (j = 0; j < b->rules.own.count; j++)
		if (!b->rules.own.items[j].used)
			problem(f, "koprulu-%s.json %.*s: no record of ours has the text %.*s", b->locale, (int)b->rules.own.items[j].table.n,
				(const char *)b->rules.own.items[j].table.s, (int)b->rules.own.items[j].base.n,
				(const char *)b->rules.own.items[j].base.s);
	free(ours);
	if (out.bad || pool.bad) {
		free(out.p);
		free(pool.p);
		b->oom = 1;
		return;
	}
	l->dlgs = out.p;
	l->dlgs_size = out.n;
	l->pool = pool.p;
	l->pool_size = pool.n;
}

/* --- the scripts of the credits and the epilogues --- */

/* "</BACKGROUND x>" and "</FONTCOLOR x>", any case: each x the base's files must have */
static void check_tags(Build *b, const char *name, const uint8_t *t, size_t n)
{
	static const char *const TAGS[2] = { "</background ", "</fontcolor " };
	size_t k, j;
	int i;
	for (k = 0; k < n; k++) {
		for (i = 0; i < 2; i++) {
			size_t len = strlen(TAGS[i]);
			if (n - k < len || !same_nocase((const char *)t + k, TAGS[i], len)) continue;
			for (j = k + len; j < n && t[j] != '>'; j++) {}
			if (j < n) {
				const uint8_t *path = t + k + len;
				size_t pn = j - (k + len);
				int f;
				for (f = 0; f < b->d->glue.count; f++)
					if (b->d->glue.files[f].path_len == pn && same_nocase(b->d->glue.files[f].path, (const char *)path, pn))
						break;
				if (f == b->d->glue.count) problem(&b->l->found, "%s: names %.*s, which the base's files lack", name, (int)pn, (const char *)path);
			}
		}
	}
}

static void build_scripts(Build *b)
{
	KpLocale *l = b->l;
	const KpUiFiles *g = &b->d->glue;
	int k;
	if (!(l->scripts = (KpUiFile *)calloc((size_t)g->count + 1, sizeof *l->scripts))) {
		b->oom = 1;
		return;
	}
	for (k = 0; k < g->count; k++) {
		const KpUiFile *f = &g->files[k];
		char path[PATH], name[PATH];
		uint8_t *data;
		size_t size = 0;
		if (f->path_len < 8 || !same_nocase(f->path, "rez\\", 4)
			|| !same_nocase(f->path + f->path_len - 4, ".txt", 4))
			continue;
		snprintf(name, sizeof name, "%.*s", (int)f->path_len, f->path);
		snprintf(path, sizeof path, REZ "%s", b->locale, name + 4);
		if (!(data = read_raw(b, path, &size, 0))) {
			l->scripts_left++;
			continue;
		}
		if (!kp_text_valid_utf8(data, size)) {
			problem(&l->found, "%s: not UTF-8", name);
			free(data);
			continue;
		}
		note_codes(&l->found, name, data, size);
		check_tags(b, name, data, size);
		l->scripts[l->script_count].path = f->path;
		l->scripts[l->script_count].path_len = f->path_len;
		l->scripts[l->script_count].data = data;
		l->scripts[l->script_count].size = size;
		l->script_count++;
	}
}

/* --- the movies' subtitles --- */

/* SUBS (subtitles_section): the locale in 4 bytes, u32 cues, u32 text bytes; per movie of the catalog u32 its first
 * cue and u32 its cues; per cue u32 start, end (ms), at, size in the text; the text. None when no movie has any. */
static void build_subs(Build *b)
{
	KpLocale *l = b->l;
	const KpDefsTable *cinm = kp_defs_table(b->d->profile, "CINM");
	KpCineCatalog *cat = (KpCineCatalog *)malloc(sizeof *cat);
	KpCineSubs *s = (KpCineSubs *)malloc(sizeof *s);
	Buf table = { NULL, 0, 0, 0 }, records = { NULL, 0, 0, 0 }, text = { NULL, 0, 0, 0 }, out = { NULL, 0, 0, 0 };
	uint32_t total = 0;
	int k, c, movies = 0;
	char id[4] = { 0, 0, 0, 0 };
	if (!cat || !s) {
		b->oom = 1;
		goto done;
	}
	if (!cinm || kp_cine_catalog(cat, cinm->data, cinm->size) != 0) {
		problem(&l->found, "the profile's CINM does not read: no subtitles");
		goto done;
	}
	for (k = 0; k < cat->count; k++) {
		char path[PATH], lower[KP_CINE_NAME], where[WHERE], why[256];
		size_t size = 0, i;
		uint8_t *srt;
		KpError err;
		for (i = 0; cat->movies[k].name[i] && i + 1 < sizeof lower; i++) lower[i] = ascii_lower(cat->movies[k].name[i]);
		lower[i] = 0;
		snprintf(path, sizeof path, KP_LOCALE_SUBTITLES, b->locale, lower);
		put_u32(&table, total);
		s->count = 0;
		why[0] = 0;
		if ((srt = b->raw->read(b->raw->ctx, path, &size, why, sizeof why)) != NULL) {
			if (kp_cine_subs_srt(srt, size, s, &err) != 0) {
				problem(&l->found, "%s: %s", path, err.msg);
				free(srt);
				goto done; /* the converter's: a language's subtitles whole or none */
			}
			free(srt);
			movies++;
		} else if (why[0]) { /* there but unread */
			problem(&l->found, "%s: %s", path, why);
			goto done;
		}
		put_u32(&table, (uint32_t)s->count);
		for (c = 0; c < s->count; c++) {
			const KpCineCue *q = &s->cues[c];
			snprintf(where, sizeof where, "%s.srt cue %d", cat->movies[k].name, c);
			if (!kp_text_valid_utf8((const uint8_t *)s->text + q->at, q->size)) {
				problem(&l->found, "%s: not UTF-8", where);
				goto done;
			}
			note_codes(&l->found, where, (const uint8_t *)s->text + q->at, q->size);
			put_u32(&records, q->start_ms);
			put_u32(&records, q->end_ms);
			put_u32(&records, (uint32_t)text.n);
			put_u32(&records, q->size);
			put(&text, s->text + q->at, q->size);
		}
		total += (uint32_t)s->count;
	}
	if (!movies) goto done;
	memcpy(id, b->locale, strlen(b->locale) < 4 ? strlen(b->locale) : 4);
	put(&out, id, 4);
	put_u32(&out, total);
	put_u32(&out, (uint32_t)text.n);
	put(&out, table.p, table.n);
	put(&out, records.p, records.n);
	put(&out, text.p, text.n);
	if (table.bad || records.bad || text.bad || out.bad) {
		b->oom = 1;
		free(out.p);
		goto done;
	}
	l->subs = out.p;
	l->subs_size = out.n;
	l->subtitles = movies;
	out.p = NULL;
done:
	free(table.p);
	free(records.p);
	free(text.p);
	free(cat);
	free(s);
}

/* --- the language --- */

int kp_locale_held(const KpFiles *raw, const char *locale)
{
	char path[PATH], why[256] = "";
	size_t got = 0;
	uint64_t total = 0;
	uint8_t *head;
	KpError e;
	(void)why;
	snprintf(path, sizeof path, KP_LOCALE_PROBE, locale);
	free(head = kp_files_head(raw, path, 4, &got, &total, &e));
	return head != NULL;
}

/* Whether the profile's languages (LANG) list the locale, other than the base (the first) */
static int a_language(const KpDefsProfile *p, const char *locale)
{
	Cur c = defs(p, "LANG");
	uint32_t n = c.p ? u8_(&c) : 0, k;
	for (k = 0; k < n && !c.bad; k++) {
		Span id = name8(&c);
		name8(&c);
		if (k > 0 && span_is(id, locale)) return 1;
	}
	return 0;
}

int kp_locale_build(KpLocale *l, const KpUiData *d, const KpFiles *raw, const char *locale, KpError *err)
{
	Build b;
	int k;
	memset(l, 0, sizeof *l);
	memset(&b, 0, sizeof b);
	if (strlen(locale) >= KP_LOCALE_ID || !a_language(d->profile, locale)) {
		snprintf(err->msg, sizeof err->msg, "language %s: none of the profile's other than the base", locale);
		return -1;
	}
	snprintf(l->locale, sizeof l->locale, "%s", locale);
	b.l = l;
	b.d = d;
	b.raw = raw;
	b.locale = l->locale;
	if (load_rules(d->profile, locale, &b.rules) != 0 || !(b.scr = kp_res_scr_new(d->profile))) {
		free_rules(&b.rules);
		snprintf(err->msg, sizeof err->msg, "language %s: the profile's rules are malformed or out of memory", locale);
		return -1;
	}
	for (k = 0; k < KP_UI_TBLS && !b.oom; k++) build_table(&b, k);
	for (k = 0; k < b.rules.rows.count; k++)
		if (!b.rules.rows.items[k].used)
			problem(&l->found, "%.4s %u: the shorter text names no such row", (const char *)b.rules.rows.items[k].table.s,
				(unsigned)b.rules.rows.items[k].row);
	if (!b.oom) build_dialogs(&b);
	if (!b.oom) build_scripts(&b);
	if (!b.oom) build_subs(&b);
	if (!b.oom) { /* the language dialog shows every language by its own name */
		Cur c = defs(d->profile, "LANG");
		uint32_t n = c.p ? u8_(&c) : 0;
		for (k = 0; k < (int)n && !c.bad; k++) {
			Span id = name8(&c), name = name8(&c);
			char where[WHERE];
			snprintf(where, sizeof where, "locales.json %.*s", (int)id.n, (const char *)id.s);
			note_codes(&l->found, where, name.s, name.n);
		}
	}
	for (k = 0; k < b.xml_count; k++) kp_scr_strings_free(&b.xml[k].strings);
	free(b.xml);
	free_rules(&b.rules);
	kp_res_scr_free(b.scr);
	if (b.oom) {
		kp_locale_free(l);
		snprintf(err->msg, sizeof err->msg, "language %s: out of memory", locale);
		return -1;
	}
	return 0;
}

void kp_locale_free(KpLocale *l)
{
	int k;
	for (k = 0; k < KP_UI_TBLS; k++) free(l->tbl[k]);
	free(l->dlgs);
	free(l->pool);
	for (k = 0; k < l->script_count; k++) free((void *)l->scripts[k].data);
	free(l->scripts);
	free(l->subs);
	kp_locale_found_free(&l->found);
	memset(l, 0, sizeof *l);
}

/* --- a mission's strings --- */

/* A CHK's STR rows from row 1 (str_rows): each (offset, length), offset NO_STRING where no text is. */
typedef struct {
	uint32_t at, n;
	int has;
} StrRow;

static StrRow *str_rows(const uint8_t *strs, size_t size, uint32_t *count)
{
	uint32_t n = size >= 2 ? kp_le_u16(strs) : 0, k;
	StrRow *rows = (StrRow *)calloc(n ? n : 1, sizeof *rows);
	if (!rows) return NULL;
	for (k = 1; k <= n; k++) {
		uint32_t at;
		const uint8_t *end;
		if (2 * (size_t)k + 2 > size) continue;
		at = kp_le_u16(strs + 2 * k);
		if (at >= size || !(end = (const uint8_t *)memchr(strs + at, 0, size - at))) continue;
		rows[k - 1].at = at;
		rows[k - 1].n = (uint32_t)(end - (strs + at));
		rows[k - 1].has = 1;
	}
	*count = n;
	return rows;
}

static int looked_up_count(const KpMission *m, uint32_t rows)
{
	uint32_t k;
	int n = 0;
	for (k = 1; k <= rows && k <= 0xffff; k++) n += kp_mission_string_looked_up(m, k);
	return n;
}

int kp_locale_mission(const KpFiles *raw, const char *locale, const char *mission, const KpMission *m, KpLocaleFound *found,
	uint8_t **strs, size_t *size, int counts[3], KpError *err)
{
	char path[PATH], why[256] = "";
	uint8_t *chk_bytes;
	size_t chk_size = 0;
	KpChk chk;
	const KpChkSection *theirs;
	StrRow *ours = NULL, *rows = NULL;
	uint32_t our_count = 0, their_count = 0, k, controls = 0, at;
	LocaleRules rules;
	Buf out = { NULL, 0, 0, 0 }, body = { NULL, 0, 0, 0 };
	int rc = -1, j, translated = 0, kept = 0;
	uint8_t **mended = NULL;
	*strs = NULL;
	*size = 0;
	snprintf(path, sizeof path, KP_HOST_SPEECH_MAP, locale, mission);
	if (!(chk_bytes = raw->read(raw->ctx, path, &chk_size, why, sizeof why))) {
		if (!why[0]) return 0;
		snprintf(err->msg, sizeof err->msg, "%s: %s", path, why);
		return -1;
	}
	if (kp_chk_read(&chk, chk_bytes, chk_size, raw->profile, 1, why, sizeof why) != 0) {
		snprintf(err->msg, sizeof err->msg, "%s: %s", path, why);
		free(chk_bytes);
		return -1;
	}
	memset(&rules, 0, sizeof rules);
	if (!(theirs = kp_chk_section(&chk, "STR "))) {
		snprintf(err->msg, sizeof err->msg, "%s: no STR", path);
		goto done;
	}
	if (load_rules(raw->profile, locale, &rules) != 0 || !(ours = str_rows(m->strings.data, m->strings.size, &our_count))
		|| !(rows = str_rows(theirs->data, theirs->size, &their_count))
		|| !(mended = (uint8_t **)calloc(our_count ? our_count : 1, sizeof *mended))) {
		snprintf(err->msg, sizeof err->msg, "%s: out of memory", path);
		goto done;
	}
	if (our_count != their_count) {
		snprintf(err->msg, sizeof err->msg, "%s: %u map strings became %u", mission, (unsigned)our_count, (unsigned)their_count);
		goto done;
	}
	for (k = 0; k < our_count; k++)
		if (ours[k].has) controls |= controls_of(m->strings.data + ours[k].at, ours[k].n);
	at = 2 + 2 * our_count;
	put_u16(&out, our_count);
	for (k = 1; k <= our_count; k++) {
		const StrRow *mine = &ours[k - 1], *their = &rows[k - 1];
		const uint8_t *use = mine->has ? m->strings.data + mine->at : NULL;
		size_t use_n = mine->has ? mine->n : 0;
		const uint8_t *text = their->has ? theirs->data + their->at : NULL;
		char where[WHERE];
		Rule *fix;
		snprintf(where, sizeof where, "%s string %u", mission, (unsigned)k);
		fix = rule_of(&rules.strings, mission, strlen(mission), k);
		if (fix) fix->used = 1;
		if (kp_mission_string_looked_up(m, k) || !mine->has || !mine->n || !text || !their->n || is_placeholder(text, their->n)) {
			kept += mine->has && mine->n;
		} else {
			uint8_t *t;
			size_t tn;
			if (mend(&rules.templates, found, where, text, their->n, &mended[k - 1], &tn) != 0) {
				snprintf(err->msg, sizeof err->msg, "%s: out of memory", path);
				goto done;
			}
			t = mended[k - 1];
			if (fix) {
				t = (uint8_t *)fix->text.s;
				tn = fix->text.n;
			}
			if (!kp_text_valid_utf8(t, tn)) {
				problem(found, "%s: not UTF-8", where);
			} else if (marks_ok(found, where, t, tn, controls)) {
				note_codes(found, where, t, tn);
				use = t;
				use_n = tn;
				translated++;
			}
		}
		if (!use) {
			put_u16(&out, NO_STRING);
			continue;
		}
		put_u16(&out, at + (uint32_t)body.n);
		put(&body, use, use_n);
		put(&body, "", 1);
	}
	for (j = 0; j < rules.strings.count; j++)
		if (!rules.strings.items[j].used && span_is(rules.strings.items[j].table, mission))
			problem(found, "%s string %u: locales.json gives a text to no such string", mission, (unsigned)rules.strings.items[j].row);
	if (out.bad || body.bad) {
		snprintf(err->msg, sizeof err->msg, "%s: out of memory", path);
		goto done;
	}
	if (at + body.n >= STR_LIMIT) {
		snprintf(err->msg, sizeof err->msg, "%s: the map's strings outgrew STR's 16-bit offsets", mission);
		goto done;
	}
	put(&out, body.p, body.n);
	if (out.bad) {
		snprintf(err->msg, sizeof err->msg, "%s: out of memory", path);
		goto done;
	}
	*strs = out.p;
	*size = out.n;
	out.p = NULL;
	if (counts) {
		counts[0] = translated;
		counts[1] = kept;
		counts[2] = looked_up_count(m, our_count);
	}
	rc = 1;
done:
	if (mended)
		for (k = 0; k < our_count; k++) free(mended[k]);
	free(mended);
	free(out.p);
	free(body.p);
	free(ours);
	free(rows);
	free_rules(&rules);
	free(chk_bytes);
	return rc;
}

/* --- the glyphs --- */

/* An FNT font's slot of a code point (kp_text_byte; ASCII and windows-1252's upper half), -1 none */
static int slot_of(int32_t cp) { return cp < 0x100 || kp_text_byte(cp) >= 0 ? kp_text_byte(cp) : -1; }

static int untrue(int slot) { return slot >= UNTRUE_FIRST && slot <= UNTRUE_LAST; }

static int slot_has_glyph(const uint8_t *font, size_t size, int slot)
{
	uint32_t low, high;
	if (size < FNT_HEADER) return 0;
	low = font[4];
	high = font[5];
	if ((uint32_t)slot < low || (uint32_t)slot > high || FNT_HEADER + 4 * ((size_t)slot - low) + 4 > size) return 0;
	return kp_le_u32(font + FNT_HEADER + 4 * ((size_t)slot - low)) != 0;
}

static int cmp_code(const void *a, const void *b)
{
	int32_t x = *(const int32_t *)a, y = *(const int32_t *)b;
	return x < y ? -1 : x > y;
}

/* The set: the code pages' (LCPG), the languages' names' (LANG), l's; none with a true slot, a control, U+00A0. */
static int32_t *glyph_set(const KpUiData *d, const KpLocale *l, int *count)
{
	Cur c = defs(d->profile, "LCPG");
	uint32_t n = count_of(&c), k, langs;
	int32_t *codes;
	int m = 0, cap = (int)n + (l ? l->found.code_count : 0) + 256;
	KpLocaleFound names;
	memset(&names, 0, sizeof names);
	{
		Cur lc = defs(d->profile, "LANG");
		langs = lc.p ? u8_(&lc) : 0;
		for (k = 0; k < langs && !lc.bad; k++) {
			Span name;
			name8(&lc);
			name = name8(&lc);
			if (kp_text_valid_utf8(name.s, name.n)) note_codes(&names, "", name.s, name.n);
		}
	}
	cap += names.code_count;
	if (!(codes = (int32_t *)malloc((size_t)cap * sizeof *codes))) {
		kp_locale_found_free(&names);
		return NULL;
	}
	for (k = 0; k < n && !c.bad; k++) codes[m++] = (int32_t)u16_(&c);
	for (k = 0; k < (uint32_t)names.code_count; k++) codes[m++] = names.codes[k];
	for (k = 0; l && k < (uint32_t)l->found.code_count; k++) codes[m++] = l->found.codes[k];
	kp_locale_found_free(&names);
	qsort(codes, (size_t)m, sizeof *codes, cmp_code);
	n = 0;
	for (k = 0; k < (uint32_t)m; k++) {
		int32_t cp = codes[k];
		int slot = slot_of(cp);
		if ((n && codes[n - 1] == cp) || cp < CONTROL_END || cp == NO_BREAK_SPACE || (slot >= 0 && !untrue(slot))) continue;
		codes[n++] = cp;
	}
	*count = (int)n;
	return codes;
}

int kp_locale_glyphs(KpLocaleGlyphs *g, const KpUiData *d, const KpFiles *raw, const KpLocale *l, KpLocaleFound *found,
	KpError *err)
{
	char why[256];
	int32_t *codes = NULL, *missing = NULL;
	int count = 0, k, i, rc = -1;
	memset(g, 0, sizeof *g);
	for (k = 0; k < KP_UI_VECTOR_FONTS; k++) {
		why[0] = 0;
		if (!(g->ttf[k] = raw->read(raw->ctx, KP_LOCALE_FONTS[k], &g->ttf_size[k], why, sizeof why))) {
			kp_locale_glyphs_free(g);
			if (!why[0]) return 0;
			snprintf(err->msg, sizeof err->msg, "glyphs: %s: %s", KP_LOCALE_FONTS[k], why);
			return -1;
		}
		g->vector_fonts[k].path = VFNT_NAMES[k];
		g->vector_fonts[k].path_len = strlen(VFNT_NAMES[k]);
		g->vector_fonts[k].data = g->ttf[k];
		g->vector_fonts[k].size = g->ttf_size[k];
	}
	if (!(codes = glyph_set(d, l, &count)) || !(missing = (int32_t *)malloc(((size_t)count + 1) * sizeof *missing))) {
		snprintf(err->msg, sizeof err->msg, "glyphs: out of memory");
		goto done;
	}
	for (;;) { /* a code point one TrueType font lacks leaves all five */
		int dropped = 0;
		for (i = 0; i < KP_UI_FNT_FONTS; i++) {
			int ttf = i == WIDE_FONT, mc = 0;
			char e[KP_ERROR_SIZE];
			free(g->supplements[i]);
			g->supplements[i] = NULL;
			if (kp_fnt_supplement(g->ttf[ttf], g->ttf_size[ttf], d->fonts[i].data, d->fonts[i].size, codes, count,
					&g->supplements[i], &g->supplement_sizes[i], missing, &mc, e, sizeof e) != 0) {
				snprintf(err->msg, sizeof err->msg, "glyphs: font %d: %s", i, e);
				goto done;
			}
			if (mc) {
				int32_t *grown = (int32_t *)realloc(g->dropped, ((size_t)g->dropped_count + (size_t)mc) * sizeof *grown);
				int a, m = 0;
				if (!grown) {
					snprintf(err->msg, sizeof err->msg, "glyphs: out of memory");
					goto done;
				}
				g->dropped = grown;
				for (a = 0; a < mc; a++) g->dropped[g->dropped_count++] = missing[a];
				for (a = 0; a < count; a++) {
					int b, gone = 0;
					for (b = 0; b < mc && !gone; b++) gone = codes[a] == missing[b];
					if (!gone) codes[m++] = codes[a];
				}
				count = m;
				dropped = 1;
				break;
			}
		}
		if (!dropped) break;
	}
	qsort(g->dropped, (size_t)g->dropped_count, sizeof *g->dropped, cmp_code);
	g->code_count = count;
	if (l) kp_locale_glyphs_cover(g, d, &l->found, found);
	rc = 1;
done:
	free(codes);
	free(missing);
	if (rc < 0) kp_locale_glyphs_free(g);
	return rc;
}

void kp_locale_glyphs_cover(const KpLocaleGlyphs *g, const KpUiData *d, const KpLocaleFound *used, KpLocaleFound *found)
{
	const uint8_t *s = g->supplements[0];
	int32_t n = s ? kp_ui_supplement_count(s, g->supplement_sizes[0]) : 0;
	int k, i;
	for (k = 0; k < used->code_count; k++) {
		int32_t cp = used->codes[k], lo = 0, hi = n - 1;
		int slot = slot_of(cp);
		if (cp == SPACE || cp == NO_BREAK_SPACE) continue; /* their width, no glyph */
		if (slot >= 0 && !untrue(slot)) { /* a character with a slot of its own needs its glyph there */
			for (i = 0; i < KP_UI_FNT_FONTS; i++)
				if (!slot_has_glyph(d->fonts[i].data, d->fonts[i].size, slot))
					problem(found, "%s: U+%04X has no glyph in font %d's slot", used->code_where[k], (unsigned)cp, i);
			continue;
		}
		while (lo <= hi) { /* the supplement's code points are in order */
			int32_t mid = lo + (hi - lo) / 2, at = kp_ui_supplement_code(s, mid);
			if (at == cp) break;
			if (at < cp) lo = mid + 1;
			else hi = mid - 1;
		}
		if (lo > hi) problem(found, "%s: U+%04X is in neither a slot nor the glyphs", used->code_where[k], (unsigned)cp);
	}
}

void kp_locale_glyphs_free(KpLocaleGlyphs *g)
{
	int k;
	for (k = 0; k < KP_UI_FNT_FONTS; k++) free(g->supplements[k]);
	for (k = 0; k < KP_UI_VECTOR_FONTS; k++) free(g->ttf[k]);
	free(g->dropped);
	memset(g, 0, sizeof *g);
}

void kp_locale_to_ui(const KpLocale *l, const KpLocaleGlyphs *g, KpUiLocale *out)
{
	const uint8_t *speech = out->speech;
	size_t speech_size = out->speech_size;
	int k;
	memset(out, 0, sizeof *out);
	out->speech = speech;
	out->speech_size = speech_size;
	if (l) {
		for (k = 0; k < KP_UI_TBLS; k++) {
			out->tbl[k] = l->tbl[k];
			out->tbl_size[k] = l->tbl_size[k];
		}
		out->dlgs = l->dlgs;
		out->dlgs_size = l->dlgs_size;
		out->pool = l->pool;
		out->pool_size = l->pool_size;
		out->files = l->scripts;
		out->file_count = l->script_count;
	}
	if (g && g->ttf[0]) {
		for (k = 0; k < KP_UI_FNT_FONTS; k++) {
			out->supplements[k] = g->supplements[k];
			out->supplement_sizes[k] = g->supplement_sizes[k];
		}
		out->vector_fonts = g->vector_fonts;
		out->vector_font_count = KP_UI_VECTOR_FONTS;
	}
}

/* --- the files a language is built from --- */

/* raw, each file it gives told to add */
typedef struct {
	const KpFiles *raw;
	KpLocaleAdd add;
	void *ctx;
	int stopped;
} Recorder;

static uint8_t *recorded_read(void *ctx, const char *name, size_t *size, char *err, size_t err_size)
{
	Recorder *r = (Recorder *)ctx;
	uint8_t *data = r->raw->read(r->raw->ctx, name, size, err, err_size);
	if (data && r->add(r->ctx, name) != 0) r->stopped = 1;
	return data;
}

static uint8_t *recorded_head(void *ctx, const char *name, size_t want, size_t *got, uint64_t *total, char *err,
	size_t err_size)
{
	Recorder *r = (Recorder *)ctx;
	uint8_t *data = r->raw->head ? r->raw->head(r->raw->ctx, name, want, got, total, err, err_size)
		: r->raw->read(r->raw->ctx, name, got, err, err_size);
	if (!r->raw->head && data) *total = *got;
	if (data && r->add(r->ctx, name) != 0) r->stopped = 1;
	return data;
}

int kp_locale_names(const KpData *d, const char *locale, KpLocaleAdd add, void *ctx, KpError *err)
{
	static const char MISSIONS[] = "missions/";
	Recorder r;
	KpFiles files = d->raw;
	KpLocale l;
	int k;
	r.raw = &d->raw;
	r.add = add;
	r.ctx = ctx;
	r.stopped = 0;
	files.read = recorded_read;
	files.head = recorded_head;
	files.ctx = &r;
	for (k = 0; k < KP_UI_VECTOR_FONTS; k++) {
		size_t got = 0;
		uint64_t total = 0;
		free(kp_files_head(&files, KP_LOCALE_FONTS[k], 4, &got, &total, err));
	}
	if (strcmp(locale, KP_LOCALE_BASE) != 0 && kp_locale_held(&files, locale)) {
		if (kp_locale_build(&l, &d->ui, &files, locale, err) != 0) return -1;
		kp_locale_free(&l);
		for (k = 0; k < d->mission_count && !r.stopped; k++) {
			const KpDataMission *entry = &d->missions[k];
			KpLocaleFound found;
			KpMission m;
			uint8_t *strs = NULL;
			size_t size = 0;
			if (!entry->catalog || !entry->directory[0] || strncmp(entry->key, MISSIONS, sizeof MISSIONS - 1) != 0) continue;
			if (kp_data_mission(d, entry, NULL, 0, &m, err) != 0) return -1;
			memset(&found, 0, sizeof found);
			kp_locale_mission(&files, locale, entry->key + sizeof MISSIONS - 1, &m, &found, &strs, &size, NULL, err);
			free(strs);
			kp_locale_found_free(&found);
			kp_mission_free(&m);
		}
	}
	if (r.stopped) {
		snprintf(err->msg, sizeof err->msg, "language %s: its files: out of memory", locale);
		return -1;
	}
	err->msg[0] = 0;
	return 0;
}
