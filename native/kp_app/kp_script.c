/*
 * The script's parse and its run (kp_script.h; the language in docs/formats/script.md).
 */
#include "kp_script.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__GNUC__) || defined(__clang__)
#define KP_SCRIPT_PRINTF(a, b) __attribute__((format(printf, a, b)))
#else
#define KP_SCRIPT_PRINTF(a, b)
#endif

#define MAX_COORD 1000000              /* a point's or a camera's coordinate, a value's number argument */
#define MAX_K ((int64_t)1 << 40)       /* an expression's number */
#define MAX_SECONDS 86400              /* a time in seconds */
#define MAX_FRAMES 10000000
#define MAX_FINGER 255
#define MAX_RADIUS 4096
#define LOG_LINE 1024

/* --- the parse --- */

typedef struct {
	char buf[KP_SCRIPT_LINE + KP_SCRIPT_WORDS];
	const char *w[KP_SCRIPT_WORDS];
	int quoted[KP_SCRIPT_WORDS];
	int n, at;
	int amp; /* the line ended in " &" */
} Words;

typedef struct {
	KpScript *s;
	const KpScriptHost *h;
	KpError *err;
	int file, line, depth;
	int commands; /* the commands so far: the header is over */
	int amp;      /* the line before ended in " &" */
	int has_mission, has_clock, has_timeout, has_watchdog;
} Parse;

typedef int (*ParseFn)(Parse *P, Words *W, int at);

static const char *const KIND_NAMES[KP_SCRIPT_NAME_KINDS] = { "key", "value", "condition", "control", "check",
	"choice", "menu", "options dialog", "unit kind", "dialog", "screen" };
static const char *const OPS[] = { "==", "!=", "<", "<=", ">", ">=" };

static int input_command(const char *w);

KP_SCRIPT_PRINTF(2, 3) static int fail(Parse *P, const char *fmt, ...)
{
	char what[KP_ERROR_SIZE];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(what, sizeof what, fmt, ap);
	va_end(ap);
	snprintf(P->err->msg, sizeof P->err->msg, "%s:%d: %s", P->s->files[P->file], P->line, what);
	return -1;
}

static int is(const char *w, const char *s) { return w != NULL && strcmp(w, s) == 0; }
static int digit(char c) { return c >= '0' && c <= '9'; }
static char lower(char c) { return c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c; }

/* Whether two file names are one file where the file system ignores case (macOS's, Windows's). */
static int same_file(const char *a, const char *b)
{
	for (; *a && lower(*a) == lower(*b); a++, b++) {}
	return lower(*a) == lower(*b);
}

static int name_char(char c)
{
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || digit(c) || c == '_' || c == '-';
}

/* Whether w is [A-Za-z0-9_-]+ (and also '.' or '/' where allowed), with no "..". */
static int plain(const char *w, int dots, int slashes)
{
	const char *p;
	if (!w[0] || strstr(w, "..") != NULL || w[0] == '.' || w[0] == '/') return 0;
	for (p = w; *p; p++) {
		if (!name_char(*p) && !(dots && *p == '.') && !(slashes && *p == '/')) return 0;
	}
	return 1;
}

static int is_number(const char *w)
{
	const char *p = w;
	if (*p == '+' || *p == '-') p++;
	if (!digit(*p)) return 0;
	while (digit(*p)) p++;
	return *p == 0;
}

/* A decimal integer in [lo, hi]; 0, or -1. */
static int parse_int(const char *w, int64_t lo, int64_t hi, int64_t *out)
{
	const char *p = w;
	int64_t v = 0;
	int neg = 0;
	if (!w || !is_number(w)) return -1;
	if (*p == '+' || *p == '-') neg = *p++ == '-';
	for (; *p; p++) {
		if (v > MAX_K) return -1;
		v = v * 10 + (*p - '0');
	}
	if (neg) v = -v;
	if (v < lo || v > hi) return -1;
	*out = v;
	return 0;
}

/* Seconds with up to 3 decimals as ms; 0, or -1. */
static int parse_seconds(const char *w, int64_t *ms)
{
	const char *p = w;
	int64_t whole = 0, frac = 0;
	int places = 0;
	if (!w || !digit(*p)) return -1;
	for (; digit(*p); p++) {
		if (whole > MAX_SECONDS) return -1;
		whole = whole * 10 + (*p - '0');
	}
	if (*p == '.') {
		for (p++; digit(*p) && places < 3; p++, places++) frac = frac * 10 + (*p - '0');
		if (places == 0) return -1;
	}
	if (*p != 0 || whole > MAX_SECONDS) return -1;
	while (places++ < 3) frac *= 10;
	*ms = whole * 1000 + frac;
	return 0;
}

static int op_of(const char *w)
{
	int k;
	for (k = 0; k < (int)(sizeof OPS / sizeof OPS[0]); k++) {
		if (is(w, OPS[k])) return k;
	}
	return -1;
}

/* The line's words: unquoted ones split at spaces and tabs, quoted strings with \" and \\, a word starting with # the
 * comment's start; a last word "&" sets amp. */
static int split(Parse *P, const char *line, size_t len, Words *W)
{
	size_t i = 0, o = 0;
	W->n = W->at = W->amp = 0;
	while (i < len) {
		unsigned char c = (unsigned char)line[i];
		size_t start = o;
		if (c == ' ' || c == '\t') {
			i++;
			continue;
		}
		if (c == '#') break;
		if (W->n == KP_SCRIPT_WORDS) return fail(P, "more than %d words", KP_SCRIPT_WORDS);
		W->w[W->n] = W->buf + o;
		W->quoted[W->n] = c == '"';
		if (c == '"') {
			for (i++;; i++) {
				if (i >= len) return fail(P, "a string without its closing quote");
				c = (unsigned char)line[i];
				if (c == '"') break;
				if (c == '\\') {
					if (i + 1 >= len || (line[i + 1] != '"' && line[i + 1] != '\\')) return fail(P, "a string's \\ escapes only \" and \\");
					c = (unsigned char)line[++i];
				}
				if (c < 0x20 || c == 0x7f) return fail(P, "a control character");
				if (o - start >= KP_SCRIPT_STRING - 1) return fail(P, "a string longer than %d bytes", KP_SCRIPT_STRING - 1);
				W->buf[o++] = (char)c;
			}
			i++;
			if (i < len && line[i] != ' ' && line[i] != '\t') return fail(P, "a string runs into a word");
		} else {
			for (; i < len && line[i] != ' ' && line[i] != '\t'; i++) {
				c = (unsigned char)line[i];
				if (c < 0x20 || c == 0x7f) return fail(P, "a control character");
				if (c == '"') return fail(P, "a quote inside a word");
				if (o - start >= KP_SCRIPT_NAME - 1) return fail(P, "a word longer than %d bytes", KP_SCRIPT_NAME - 1);
				W->buf[o++] = (char)c;
			}
		}
		W->buf[o++] = 0;
		W->n++;
	}
	if (W->n > 0 && !W->quoted[W->n - 1] && is(W->w[W->n - 1], "&")) {
		if (W->n == 1) return fail(P, "& alone");
		W->amp = 1;
		W->n--;
	}
	return 0;
}

static const char *peek(const Words *W) { return W->at < W->n && !W->quoted[W->at] ? W->w[W->at] : NULL; }
static int peek_quoted(const Words *W) { return W->at < W->n && W->quoted[W->at]; }
static int at_end(const Words *W) { return W->at >= W->n; }

/* The next word, a name (not a string). */
static int take_word(Parse *P, Words *W, const char *what, const char **w)
{
	*w = NULL; /* GCC cannot see that fail's -1 is the only way out without a word: its -Wmaybe-uninitialized, with -Werror */
	if (at_end(W)) return fail(P, "%s expected", what);
	if (W->quoted[W->at]) return fail(P, "%s expected, not a string", what);
	*w = W->w[W->at++];
	return 0;
}

/* The next word, a string of 1 byte or more, into out (KP_SCRIPT_STRING). */
static int take_text(Parse *P, Words *W, const char *what, char *out)
{
	if (!peek_quoted(W)) return fail(P, "%s expected (a quoted string)", what);
	if (!W->w[W->at][0]) return fail(P, "%s is empty", what);
	snprintf(out, KP_SCRIPT_STRING, "%s", W->w[W->at++]);
	return 0;
}

static int take_int(Parse *P, Words *W, const char *what, int64_t lo, int64_t hi, int64_t *out)
{
	const char *w;
	if (take_word(P, W, what, &w) != 0) return -1;
	if (parse_int(w, lo, hi, out) != 0) return fail(P, "%s: \"%s\" is no number in %lld..%lld", what, w, (long long)lo, (long long)hi);
	return 0;
}

static int take_int32(Parse *P, Words *W, const char *what, int64_t lo, int64_t hi, int32_t *out)
{
	int64_t v;
	if (take_int(P, W, what, lo, hi, &v) != 0) return -1;
	*out = (int32_t)v;
	return 0;
}

static int take_seconds(Parse *P, Words *W, const char *what, int zero, int64_t *ms)
{
	const char *w;
	if (take_word(P, W, what, &w) != 0) return -1;
	if (parse_seconds(w, ms) != 0 || (!zero && *ms == 0)) {
		return fail(P, "%s: \"%s\" is no time in seconds (%s%d at most, 3 decimals)", what, w, zero ? "0 to " : "above 0 to ",
			MAX_SECONDS);
	}
	return 0;
}

static int resolve(Parse *P, KpScriptKind kind, const char *w, KpScriptName *out)
{
	memset(out, 0, sizeof *out);
	out->arg = KP_SCRIPT_ARG_NONE;
	return P->h && P->h->name && P->h->name(P->h->ud, kind, w, out) == 0 ? 0 : -1;
}

static int take_name(Parse *P, Words *W, KpScriptKind kind, int32_t *id)
{
	const char *w;
	KpScriptName n;
	if (take_word(P, W, KIND_NAMES[kind], &w) != 0) return -1;
	if (resolve(P, kind, w, &n) != 0) return fail(P, "unknown %s \"%s\"", KIND_NAMES[kind], w);
	*id = n.id;
	return 0;
}

static int find_mark(const KpScript *s, const char *name)
{
	int k;
	for (k = 0; k < s->mark_count; k++) {
		if (strcmp(s->marks[k].name, name) == 0) return k;
	}
	return -1;
}

static int find_pick(const KpScript *s, const char *name)
{
	int k;
	for (k = 0; k < s->pick_count; k++) {
		if (strcmp(s->picks[k], name) == 0) return k;
	}
	return -1;
}

/* A value: NAME, or NAME:ARG as the host's name takes it. */
static int parse_value(Parse *P, const char *w, KpScriptRef *ref)
{
	char name[KP_SCRIPT_NAME];
	const char *colon = strchr(w, ':');
	size_t len = colon ? (size_t)(colon - w) : strlen(w);
	KpScriptName n;
	memcpy(name, w, len);
	name[len] = 0;
	if (resolve(P, KP_SCRIPT_NAME_VALUE, name, &n) != 0) return fail(P, "unknown value \"%s\"", name);
	ref->id = n.id;
	ref->arg = -1;
	switch (n.arg) {
	case KP_SCRIPT_ARG_NONE:
		if (colon) return fail(P, "the value %s takes no argument", name);
		return 0;
	case KP_SCRIPT_ARG_INT: {
		int64_t max = n.arg_max > 0 ? n.arg_max : MAX_COORD;
		if (!colon || parse_int(colon + 1, 0, max, &ref->arg) != 0) {
			return fail(P, "the value %s takes a number 0..%lld: %s:N", name, (long long)max, name);
		}
		return 0;
	}
	case KP_SCRIPT_ARG_PICK: {
		int k = colon ? find_pick(P->s, colon + 1) : -1;
		if (k < 0) return fail(P, "the value %s takes a pick made before: %s:PICK", name, name);
		ref->arg = k;
		return 0;
	}
	default: return fail(P, "the value %s has a condition's or a check's argument", name);
	}
}

/* An expression: a number, a value or @mark.value, then + N or - N. */
static int parse_expr(Parse *P, Words *W, KpScriptExpr *e)
{
	const char *w;
	int64_t k;
	memset(e, 0, sizeof *e);
	e->mark = e->slot = -1;
	e->value.id = -1;
	e->value.arg = -1;
	if (take_word(P, W, "a value or a number", &w) != 0) return -1;
	if (w[0] == '@') {
		char mark[KP_SCRIPT_NAME];
		const char *dot = strchr(w + 1, '.');
		KpScriptMark *m;
		size_t len = dot ? (size_t)(dot - w - 1) : 0;
		int slot;
		if (!dot || len == 0) return fail(P, "\"%s\": a mark's value is @MARK.VALUE", w);
		memcpy(mark, w + 1, len);
		mark[len] = 0;
		e->mark = find_mark(P->s, mark);
		if (e->mark < 0) return fail(P, "no mark \"%s\" before this line", mark);
		if (parse_value(P, dot + 1, &e->value) != 0) return -1;
		m = &P->s->marks[e->mark];
		for (slot = 0; slot < m->count && (m->values[slot].id != e->value.id || m->values[slot].arg != e->value.arg); slot++) {}
		if (slot == m->count) {
			if (m->count == KP_SCRIPT_MARK_VALUES) return fail(P, "the mark %s keeps more than %d values", mark, KP_SCRIPT_MARK_VALUES);
			m->values[m->count++] = e->value;
		}
		e->kind = KP_SCRIPT_EXPR_MARK;
		e->slot = slot;
	} else if (is_number(w)) {
		if (parse_int(w, -MAX_K, MAX_K, &e->k) != 0) return fail(P, "\"%s\" is out of range", w);
		e->kind = KP_SCRIPT_EXPR_NUMBER;
	} else {
		if (parse_value(P, w, &e->value) != 0) return -1;
		e->kind = KP_SCRIPT_EXPR_VALUE;
	}
	w = peek(W);
	if (is(w, "+") || is(w, "-")) {
		int neg = w[0] == '-';
		W->at++;
		if (take_int(P, W, neg ? "a number after -" : "a number after +", 0, MAX_K, &k) != 0) return -1;
		e->k += neg ? -k : k;
	} else if (w && (w[0] == '+' || w[0] == '-') && is_number(w)) {
		if (parse_int(w, -MAX_K, MAX_K, &k) != 0) return fail(P, "\"%s\" is out of range", w);
		W->at++;
		e->k += k;
	}
	return 0;
}

/* A word a condition's optional argument must not take: the words that may come after a condition ("and", a wait's
 * options, until's input). A command that is no input is a name there (the dialogs quit and wait). */
static int reserved(const char *w) { return is(w, "and") || is(w, "within") || is(w, "every") || input_command(w); }

static int parse_term(Parse *P, Words *W, KpScriptTerm *t)
{
	const char *w = peek(W), *next = W->at + 1 < W->n && !W->quoted[W->at + 1] ? W->w[W->at + 1] : NULL;
	int compared = !w || w[0] == '@' || is_number(w) || op_of(next) >= 0 || is(next, "+") || is(next, "-")
		|| (next && (next[0] == '+' || next[0] == '-') && is_number(next));
	KpScriptName n;
	memset(t, 0, sizeof *t);
	if (!compared && resolve(P, KP_SCRIPT_NAME_COND, w, &n) != 0) {
		/* no condition: a value's comparison when w is a value (its operator missing is the error then) */
		char name[KP_SCRIPT_NAME];
		const char *colon = strchr(w, ':');
		size_t len = colon ? (size_t)(colon - w) : strlen(w);
		memcpy(name, w, len);
		name[len] = 0;
		if (resolve(P, KP_SCRIPT_NAME_VALUE, name, &n) != 0) return fail(P, "unknown condition \"%s\"", w);
		compared = 1;
	}
	if (!compared) {
		W->at++;
		t->named = 1;
		t->cond.id = n.id;
		t->cond.arg = -1;
		if (n.arg == KP_SCRIPT_ARG_NAME || n.arg == KP_SCRIPT_ARG_OPT_NAME) {
			const char *a = peek(W);
			KpScriptName an;
			if (a && !reserved(a) && resolve(P, n.arg_kind, a, &an) == 0) {
				W->at++;
				t->cond.arg = an.id;
			} else if (n.arg == KP_SCRIPT_ARG_NAME) {
				return a ? fail(P, "the condition %s needs a %s, not \"%s\"", w, KIND_NAMES[n.arg_kind], a)
						 : fail(P, "the condition %s needs a %s", w, KIND_NAMES[n.arg_kind]);
			}
		} else if (n.arg != KP_SCRIPT_ARG_NONE) {
			return fail(P, "the condition %s has a value's or a check's argument", w);
		}
		return 0;
	}
	if (!w) return fail(P, "a condition expected");
	if (parse_expr(P, W, &t->a) != 0) return -1;
	w = peek(W);
	t->op = op_of(w);
	if (t->op < 0) return fail(P, "a comparison (== != < <= > >=) expected%s%s", w ? ", not " : "", w ? w : "");
	W->at++;
	return parse_expr(P, W, &t->b);
}

static int parse_cond(Parse *P, Words *W, KpScriptCond *c)
{
	c->count = 0;
	for (;;) {
		if (c->count == KP_SCRIPT_TERMS) return fail(P, "a condition of more than %d terms", KP_SCRIPT_TERMS);
		if (parse_term(P, W, &c->terms[c->count]) != 0) return -1;
		c->count++;
		if (!is(peek(W), "and")) return 0;
		W->at++;
	}
}

/* A point: X Y, world X Y, control NAME [right], unit PICK [DX DY], pad N, pad toggle, center [DX DY], view X% Y%,
 * beside PICK D. */
static int parse_point(Parse *P, Words *W, KpScriptPoint *p)
{
	const char *w = peek(W);
	memset(p, 0, sizeof *p);
	p->name = -1;
	if (!w) return fail(P, "a point expected");
	if (is_number(w)) {
		p->kind = KP_SCRIPT_AT_XY;
		return take_int32(P, W, "a point's x", -MAX_COORD, MAX_COORD, &p->x) != 0
			|| take_int32(P, W, "a point's y", -MAX_COORD, MAX_COORD, &p->y) != 0 ? -1 : 0;
	}
	W->at++;
	if (is(w, "world")) {
		p->kind = KP_SCRIPT_AT_WORLD;
		return take_int32(P, W, "the world's x", -MAX_COORD, MAX_COORD, &p->x) != 0
			|| take_int32(P, W, "the world's y", -MAX_COORD, MAX_COORD, &p->y) != 0 ? -1 : 0;
	}
	if (is(w, "control")) {
		p->kind = KP_SCRIPT_AT_CONTROL;
		if (take_name(P, W, KP_SCRIPT_NAME_CONTROL, &p->name) != 0) return -1;
		if (is(peek(W), "right")) {
			W->at++;
			p->flag = 1;
		}
		return 0;
	}
	if (is(w, "unit") || is(w, "beside")) {
		const char *pick;
		p->kind = is(w, "unit") ? KP_SCRIPT_AT_UNIT : KP_SCRIPT_AT_BESIDE;
		if (take_word(P, W, "a pick's name", &pick) != 0) return -1;
		p->name = find_pick(P->s, pick);
		if (p->name < 0) return fail(P, "no pick \"%s\" before this line", pick);
		if (p->kind == KP_SCRIPT_AT_BESIDE) return take_int32(P, W, "the distance", 1, MAX_COORD, &p->x);
		if (peek(W) && is_number(peek(W))) {
			return take_int32(P, W, "the offset's x", -MAX_COORD, MAX_COORD, &p->x) != 0
				|| take_int32(P, W, "the offset's y", -MAX_COORD, MAX_COORD, &p->y) != 0 ? -1 : 0;
		}
		return 0;
	}
	if (is(w, "pad")) {
		if (is(peek(W), "toggle")) {
			W->at++;
			p->kind = KP_SCRIPT_AT_PAD_TOGGLE;
			return 0;
		}
		p->kind = KP_SCRIPT_AT_PAD;
		return take_int32(P, W, "the pad's button", 0, 9, &p->name);
	}
	if (is(w, "center")) {
		p->kind = KP_SCRIPT_AT_CENTER;
		if (peek(W) && is_number(peek(W))) {
			return take_int32(P, W, "the offset's x", -MAX_COORD, MAX_COORD, &p->x) != 0
				|| take_int32(P, W, "the offset's y", -MAX_COORD, MAX_COORD, &p->y) != 0 ? -1 : 0;
		}
		return 0;
	}
	if (is(w, "view")) {
		int32_t *xy[2];
		int k;
		xy[0] = &p->x;
		xy[1] = &p->y;
		p->kind = KP_SCRIPT_AT_VIEW;
		for (k = 0; k < 2; k++) {
			char num[KP_SCRIPT_NAME];
			const char *v;
			size_t len;
			int64_t pct;
			if (take_word(P, W, "a percent", &v) != 0) return -1;
			len = strlen(v);
			if (len < 2 || v[len - 1] != '%') return fail(P, "\"%s\": a view's point is X%% Y%%", v);
			memcpy(num, v, len - 1);
			num[len - 1] = 0;
			if (parse_int(num, 0, 100, &pct) != 0) return fail(P, "\"%s\" is no percent in 0..100", v);
			*xy[k] = (int32_t)pct;
		}
		return 0;
	}
	return fail(P, "\"%s\" is no point (X Y, world, control, unit, pad, center, view, beside)", w);
}

/* --- the commands --- */

#define STEP(at) (&P->s->steps[at])

static int p_none(Parse *P, Words *W, int at)
{
	(void)P;
	(void)W;
	(void)at;
	return 0;
}

static int mods(Parse *P, const char *w, int *out)
{
	char buf[KP_SCRIPT_NAME];
	char *part, *save = NULL;
	snprintf(buf, sizeof buf, "%s", w);
	for (part = buf; part; part = save) {
		int bit;
		save = strchr(part, '+');
		if (save) *save++ = 0;
		bit = is(part, "shift") ? KP_SCRIPT_MOD_SHIFT : is(part, "ctrl") ? KP_SCRIPT_MOD_CTRL
			: is(part, "alt") ? KP_SCRIPT_MOD_ALT : is(part, "gui") ? KP_SCRIPT_MOD_GUI : 0;
		if (!bit) return fail(P, "\"%s\" is no modifier (shift, ctrl, alt, gui, joined by +)", part);
		*out |= bit;
	}
	return 0;
}

static int p_key(Parse *P, Words *W, int at)
{
	KpScriptStep *st = STEP(at);
	const char *w;
	if (take_name(P, W, KP_SCRIPT_NAME_KEY, &st->id) != 0) return -1;
	st->flag = KP_SCRIPT_KEY_AUTO;
	while ((w = peek(W)) != NULL) {
		if (is(w, "mod")) {
			W->at++;
			if (st->mods) return fail(P, "a second mod");
			if (take_word(P, W, "a modifier", &w) != 0 || mods(P, w, &st->mods) != 0) return -1;
		} else if (is(w, "text") || is(w, "notext")) {
			W->at++;
			if (st->flag != KP_SCRIPT_KEY_AUTO) return fail(P, "a second text or notext");
			st->flag = is(w, "text") ? KP_SCRIPT_KEY_TEXT : KP_SCRIPT_KEY_NOTEXT;
			if (st->flag == KP_SCRIPT_KEY_TEXT && take_text(P, W, "the key's text", st->text) != 0) return -1;
		} else {
			break;
		}
	}
	return 0;
}

static int p_text(Parse *P, Words *W, int at) { return take_text(P, W, "the text", STEP(at)->text); }
static int p_point(Parse *P, Words *W, int at) { return parse_point(P, W, &STEP(at)->p); }

static int p_finger(Parse *P, Words *W, int at)
{
	KpScriptStep *st = STEP(at);
	const char *w;
	if (take_word(P, W, "down, move or up", &w) != 0) return -1;
	st->flag = is(w, "down") ? KP_SCRIPT_FINGER_DOWN : is(w, "move") ? KP_SCRIPT_FINGER_MOVE : is(w, "up") ? KP_SCRIPT_FINGER_UP : -1;
	if (st->flag < 0) return fail(P, "\"%s\": a finger goes down, move or up", w);
	return take_int32(P, W, "the finger", 0, MAX_FINGER, &st->id) != 0 ? -1 : parse_point(P, W, &st->p);
}

static int p_tap(Parse *P, Words *W, int at)
{
	KpScriptStep *st = STEP(at);
	return take_int32(P, W, "the finger", 0, MAX_FINGER, &st->id) != 0 ? -1 : parse_point(P, W, &st->p);
}

static int p_word(Parse *P, Words *W, const char *want)
{
	const char *w;
	if (take_word(P, W, want, &w) != 0) return -1;
	return is(w, want) ? 0 : fail(P, "\"%s\" expected, not \"%s\"", want, w);
}

static int p_touch(Parse *P, Words *W, int at)
{
	(void)at;
	return p_word(P, W, "on");
}

static int p_audit(Parse *P, Words *W, int at)
{
	(void)at;
	return p_word(P, W, "hd");
}

static int p_on_off(Parse *P, Words *W, int at)
{
	const char *w;
	if (take_word(P, W, "on or off", &w) != 0) return -1;
	if (!is(w, "on") && !is(w, "off")) return fail(P, "on or off expected, not \"%s\"", w);
	STEP(at)->flag = is(w, "on");
	return 0;
}

static int p_camera(Parse *P, Words *W, int at)
{
	KpScriptStep *st = STEP(at);
	const char *w = peek(W);
	st->p.name = -1;
	if (is(w, "max")) {
		W->at++;
		st->flag = KP_SCRIPT_CAMERA_MAX;
		return 0;
	}
	st->flag = KP_SCRIPT_CAMERA_AT;
	if (is(w, "by")) {
		W->at++;
		st->flag = KP_SCRIPT_CAMERA_BY;
	}
	return take_int32(P, W, "the camera's x", -MAX_COORD, MAX_COORD, &st->p.x) != 0
		|| take_int32(P, W, "the camera's y", -MAX_COORD, MAX_COORD, &st->p.y) != 0 ? -1 : 0;
}

static int p_save_name(Parse *P, Words *W, int at) { return take_text(P, W, "the save's name", STEP(at)->text); }

static int p_named(Parse *P, Words *W, int at)
{
	KpScriptStep *st = STEP(at);
	KpScriptKind kind = st->op == KP_SCRIPT_CHOOSE ? KP_SCRIPT_NAME_CHOICE : st->op == KP_SCRIPT_MENU ? KP_SCRIPT_NAME_MENU
		: st->op == KP_SCRIPT_OPTIONS_DIALOG ? KP_SCRIPT_NAME_OPTIONS : KP_SCRIPT_NAME_CONTROL;
	return take_name(P, W, kind, &st->id);
}

static int p_select(Parse *P, Words *W, int at)
{
	KpScriptStep *st = STEP(at);
	return take_name(P, W, KP_SCRIPT_NAME_UNIT, &st->id) != 0 || take_int(P, W, "the box's radius", 0, MAX_RADIUS, &st->n) != 0
		? -1 : 0;
}

static int p_hang(Parse *P, Words *W, int at) { return take_seconds(P, W, "the hang's seconds", 0, &STEP(at)->n); }

/* within EXPR (ms), every S: each once, in either order. */
static int wait_options(Parse *P, Words *W, int at, int every)
{
	for (;;) {
		const char *w = peek(W);
		if (is(w, "within")) {
			KpScriptExpr e;
			W->at++;
			if (STEP(at)->has_within) return fail(P, "a second within");
			if (parse_expr(P, W, &e) != 0) return -1;
			STEP(at)->has_within = 1;
			STEP(at)->within = e;
		} else if (every && is(w, "every")) {
			int64_t ms;
			W->at++;
			if (STEP(at)->n != KP_SCRIPT_EVERY_MS || every > 1) return fail(P, "a second every");
			if (take_seconds(P, W, "every's seconds", 1, &ms) != 0) return -1;
			STEP(at)->n = ms;
			every = 2;
		} else {
			return 0;
		}
	}
}

static int p_wait(Parse *P, Words *W, int at)
{
	KpScriptStep *st = STEP(at);
	const char *w = peek(W);
	if (is(w, "frames")) {
		W->at++;
		st->op = KP_SCRIPT_WAIT_FRAMES;
		return take_int(P, W, "the frames", 1, MAX_FRAMES, &st->n);
	}
	if (is(w, "seconds")) {
		W->at++;
		st->op = KP_SCRIPT_WAIT_TIME;
		return take_seconds(P, W, "the seconds", 0, &st->n);
	}
	if (parse_cond(P, W, &st->cond) != 0) return -1;
	return wait_options(P, W, at, 0);
}

static int parse_command(Parse *P, Words *W);

static int p_until(Parse *P, Words *W, int at)
{
	KpScriptCond c;
	int input;
	if (parse_cond(P, W, &c) != 0) return -1;
	STEP(at)->cond = c;
	STEP(at)->n = KP_SCRIPT_EVERY_MS;
	if (wait_options(P, W, at, 1) != 0) return -1;
	if (at_end(W)) return fail(P, "until needs an input after its condition");
	input = parse_command(P, W);
	if (input < 0) return -1;
	if (kp_script_class(STEP(input)->op) != KP_SCRIPT_INPUT) return fail(P, "until repeats an input, not a wait or an observation");
	return 0;
}

static int p_file(Parse *P, Words *W, int at)
{
	KpScriptStep *st = STEP(at);
	const char *w;
	int k;
	if (take_word(P, W, "a file's name", &w) != 0) return -1;
	if (!plain(w, 0, 0)) return fail(P, "\"%s\": a file's name is [A-Za-z0-9_-]", w);
	if (same_file(w, "failure")) return fail(P, "the name %s is the run's (its failure's files)", w);
	for (k = 0; k < at; k++) {
		if (P->s->steps[k].op == st->op && same_file(P->s->steps[k].text, w)) return fail(P, "a second file named %s", w);
	}
	snprintf(st->text, sizeof st->text, "%s", w);
	return 0;
}

static int p_log(Parse *P, Words *W, int at)
{
	KpScriptStep *st = STEP(at);
	const char *w;
	if (take_word(P, W, "a label", &w) != 0) return -1;
	if (!plain(w, 1, 0)) return fail(P, "\"%s\": a label is [A-Za-z0-9_.-]", w);
	snprintf(st->text, sizeof st->text, "%s", w);
	while (!at_end(W)) {
		if (st->value_count == KP_SCRIPT_LOG_VALUES) return fail(P, "more than %d values", KP_SCRIPT_LOG_VALUES);
		if (take_word(P, W, "a value", &w) != 0 || parse_value(P, w, &st->values[st->value_count]) != 0) return -1;
		st->value_count++;
	}
	return 0;
}

static int p_mark(Parse *P, Words *W, int at)
{
	KpScript *s = P->s;
	const char *w;
	if (take_word(P, W, "a mark's name", &w) != 0) return -1;
	if (!plain(w, 0, 0)) return fail(P, "\"%s\": a mark's name is [A-Za-z0-9_-]", w);
	if (find_mark(s, w) >= 0) return fail(P, "a second mark named %s", w);
	if (s->mark_count == KP_SCRIPT_MARKS) return fail(P, "more than %d marks", KP_SCRIPT_MARKS);
	snprintf(s->marks[s->mark_count].name, sizeof s->marks[0].name, "%s", w);
	STEP(at)->id = s->mark_count++;
	return 0;
}

static int p_pick(Parse *P, Words *W, int at)
{
	KpScript *s = P->s;
	const char *w;
	if (take_word(P, W, "a pick's name", &w) != 0) return -1;
	if (!plain(w, 0, 0)) return fail(P, "\"%s\": a pick's name is [A-Za-z0-9_-]", w);
	if (find_pick(s, w) >= 0) return fail(P, "a second pick named %s", w);
	if (s->pick_count == KP_SCRIPT_PICKS) return fail(P, "more than %d picks", KP_SCRIPT_PICKS);
	snprintf(s->picks[s->pick_count], sizeof s->picks[0], "%s", w);
	STEP(at)->id = s->pick_count++;
	return 0;
}

static int p_expect(Parse *P, Words *W, int at) { return parse_cond(P, W, &STEP(at)->cond); }

static int p_check(Parse *P, Words *W, int at)
{
	KpScriptStep *st = STEP(at);
	const char *w;
	KpScriptName n;
	if (take_word(P, W, KIND_NAMES[KP_SCRIPT_NAME_CHECK], &w) != 0) return -1;
	if (resolve(P, KP_SCRIPT_NAME_CHECK, w, &n) != 0) return fail(P, "unknown check \"%s\"", w);
	st->id = n.id;
	if (n.arg == KP_SCRIPT_ARG_TEXT || (n.arg == KP_SCRIPT_ARG_OPT_TEXT && peek_quoted(W))) {
		return take_text(P, W, "the check's argument", st->text);
	}
	if (n.arg != KP_SCRIPT_ARG_NONE && n.arg != KP_SCRIPT_ARG_OPT_TEXT) return fail(P, "the check %s has a value's or a condition's argument", w);
	return 0;
}

static const struct {
	const char *name;
	KpScriptOp op;
	ParseFn fn;
} COMMANDS[] = {
	{ "key", KP_SCRIPT_KEY, p_key },
	{ "text", KP_SCRIPT_TEXT, p_text },
	{ "move", KP_SCRIPT_MOVE, p_point },
	{ "click", KP_SCRIPT_CLICK, p_point },
	{ "rclick", KP_SCRIPT_RCLICK, p_point },
	{ "press", KP_SCRIPT_PRESS, p_point },
	{ "release", KP_SCRIPT_RELEASE, p_point },
	{ "finger", KP_SCRIPT_FINGER, p_finger },
	{ "tap", KP_SCRIPT_TAP, p_tap },
	{ "touch", KP_SCRIPT_TOUCH, p_touch },
	{ "choose", KP_SCRIPT_CHOOSE, p_named },
	{ "pause", KP_SCRIPT_PAUSE, p_none },
	{ "resume", KP_SCRIPT_RESUME, p_none },
	{ "camera", KP_SCRIPT_CAMERA, p_camera },
	{ "world", KP_SCRIPT_WORLD, p_on_off },
	{ "fog", KP_SCRIPT_FOG, p_on_off },
	{ "save", KP_SCRIPT_SAVE, p_save_name },
	{ "load", KP_SCRIPT_LOAD, p_save_name },
	{ "load-file", KP_SCRIPT_LOAD_FILE, p_save_name },
	{ "replay", KP_SCRIPT_REPLAY, p_save_name },
	{ "menu", KP_SCRIPT_MENU, p_named },
	{ "options-dialog", KP_SCRIPT_OPTIONS_DIALOG, p_named },
	{ "activate", KP_SCRIPT_ACTIVATE, p_named },
	{ "select", KP_SCRIPT_SELECT, p_select },
	{ "end-placement", KP_SCRIPT_END_PLACEMENT, p_none },
	{ "cancel-targeting", KP_SCRIPT_CANCEL_TARGETING, p_none },
	{ "audit", KP_SCRIPT_AUDIT, p_audit },
	{ "hang", KP_SCRIPT_HANG, p_hang },
	{ "wait", KP_SCRIPT_WAIT, p_wait },
	{ "until", KP_SCRIPT_UNTIL, p_until },
	{ "shot", KP_SCRIPT_SHOT, p_file },
	{ "state", KP_SCRIPT_STATE, p_file },
	{ "ui", KP_SCRIPT_UI, p_file },
	{ "log", KP_SCRIPT_LOG, p_log },
	{ "mark", KP_SCRIPT_MARK, p_mark },
	{ "pick", KP_SCRIPT_PICK, p_pick },
	{ "expect", KP_SCRIPT_EXPECT, p_expect },
	{ "check", KP_SCRIPT_CHECK, p_check },
	{ "quit", KP_SCRIPT_QUIT, p_none },
};

static int command_index(const char *w)
{
	int k;
	for (k = 0; k < (int)(sizeof COMMANDS / sizeof COMMANDS[0]); k++) {
		if (is(w, COMMANDS[k].name)) return k;
	}
	return -1;
}

static int input_command(const char *w)
{
	int k = command_index(w);
	return k >= 0 && kp_script_class(COMMANDS[k].op) == KP_SCRIPT_INPUT;
}

static int add_step(Parse *P, KpScriptOp op)
{
	KpScript *s = P->s;
	KpScriptStep *st;
	if (s->count == KP_SCRIPT_STEPS) return fail(P, "more than %d steps", KP_SCRIPT_STEPS);
	if (s->count == s->cap) {
		int cap = s->cap ? s->cap * 2 : 64;
		KpScriptStep *grown = (KpScriptStep *)realloc(s->steps, (size_t)cap * sizeof *grown);
		if (!grown) return fail(P, "out of memory");
		s->steps = grown;
		s->cap = cap;
	}
	st = &s->steps[s->count];
	memset(st, 0, sizeof *st);
	st->op = op;
	st->file = P->file;
	st->line = P->line;
	st->id = -1;
	st->p.name = -1;
	return s->count++;
}

/* A command and its arguments (until's input too); the step's index, or -1. */
static int parse_command(Parse *P, Words *W)
{
	const char *w;
	int c, at;
	if (take_word(P, W, "a command", &w) != 0) return -1;
	c = command_index(w);
	if (c < 0) return fail(P, "unknown command \"%s\"", w);
	at = add_step(P, COMMANDS[c].op);
	if (at < 0 || COMMANDS[c].fn(P, W, at) != 0) return -1;
	return at;
}

static int header(Parse *P, Words *W)
{
	KpScript *s = P->s;
	const char *d = W->w[W->at++], *w;
	if (is(d, "mission")) {
		if (P->has_mission++) return fail(P, "a second mission");
		if (take_word(P, W, "a mission", &w) != 0) return -1;
		if (!plain(w, 0, 1)) return fail(P, "\"%s\": a mission is [A-Za-z0-9_-/]", w);
		snprintf(s->mission, sizeof s->mission, "%s", w);
	} else if (is(d, "options") || is(d, "video")) {
		if (p_word(P, W, "originals") != 0) return -1;
		if (is(d, "video")) s->video_originals = 1;
		s->options_originals = 1; /* the video's originals need the options' (else the profile's video options come back) */
	} else if (is(d, "clock")) {
		if (P->has_clock++) return fail(P, "a second clock");
		if (take_word(P, W, "fixed or real", &w) != 0) return -1;
		if (!is(w, "fixed") && !is(w, "real")) return fail(P, "the clock is fixed or real, not \"%s\"", w);
		s->clock_real = is(w, "real");
	} else if (is(d, "timeout")) {
		if (P->has_timeout++) return fail(P, "a second timeout");
		if (take_seconds(P, W, "the timeout's seconds", 0, &s->timeout_ms) != 0) return -1;
	} else {
		if (P->has_watchdog++) return fail(P, "a second watchdog");
		if (take_seconds(P, W, "the watchdog's seconds", 0, &s->watchdog_ms) != 0) return -1;
	}
	return 0;
}

static int is_header(const char *w)
{
	return is(w, "mission") || is(w, "options") || is(w, "video") || is(w, "clock") || is(w, "timeout") || is(w, "watchdog");
}

static int parse_text(Parse *P, int file, const char *text, size_t size);

static int include(Parse *P, Words *W)
{
	KpScript *s = P->s;
	const char *w;
	char *text;
	size_t size = 0;
	int file = P->file, line = P->line, rc;
	W->at = 1;
	if (take_word(P, W, "a file's name", &w) != 0) return -1;
	if (!at_end(W)) return fail(P, "unexpected \"%s\"", W->w[W->at]);
	if (W->amp) return fail(P, "& ends only an input's line");
	if (!plain(w, 1, 0)) return fail(P, "\"%s\": an include is a file beside the script ([A-Za-z0-9_.-], no ..)", w);
	if (P->depth + 1 > KP_SCRIPT_INCLUDES) return fail(P, "includes nest deeper than %d", KP_SCRIPT_INCLUDES);
	if (s->file_count == KP_SCRIPT_FILES) return fail(P, "more than %d files", KP_SCRIPT_FILES);
	text = P->h && P->h->read ? P->h->read(P->h->ud, w, &size) : NULL;
	if (!text) return fail(P, "cannot read %s", w);
	snprintf(s->files[s->file_count], sizeof s->files[0], "%s", w);
	P->depth++;
	rc = parse_text(P, s->file_count++, text, size);
	P->depth--;
	free(text);
	if (rc != 0) return -1;
	P->file = file;
	P->line = line;
	return 0;
}

static int parse_line(Parse *P, Words *W)
{
	KpScriptClass k;
	int at;
	if (W->quoted[0]) return fail(P, "a command expected, not a string");
	if (is_header(W->w[0])) {
		if (P->depth > 0) return fail(P, "%s belongs in the main script's header", W->w[0]);
		if (P->commands > 0) return fail(P, "%s belongs in the header, before the first command", W->w[0]);
		if (W->amp) return fail(P, "& ends only an input's line");
		if (header(P, W) != 0) return -1;
		return at_end(W) ? 0 : fail(P, "unexpected \"%s\"", W->w[W->at]);
	}
	if (P->amp && is(W->w[0], "include")) return fail(P, "the line before ends in &: this one must be an input");
	P->commands++;
	if (is(W->w[0], "include")) return include(P, W);
	at = parse_command(P, W);
	if (at < 0) return -1;
	if (!at_end(W)) return fail(P, "unexpected \"%s\"", W->quoted[W->at] ? "a string" : W->w[W->at]);
	k = kp_script_class(STEP(at)->op);
	if (P->amp && k != KP_SCRIPT_INPUT) return fail(P, "the line before ends in &: this one must be an input");
	if (W->amp && k != KP_SCRIPT_INPUT) return fail(P, "& ends only an input's line");
	STEP(at)->same_frame = W->amp;
	P->amp = W->amp;
	return 0;
}

static int parse_text(Parse *P, int file, const char *text, size_t size)
{
	size_t i = 0;
	char *copy = (char *)malloc(size + 1);
	Words W;
	P->file = file;
	P->line = 0;
	if (!copy) return fail(P, "out of memory");
	memcpy(copy, text, size);
	copy[size] = 0;
	P->s->texts[file] = copy;
	P->s->sizes[file] = size;
	if (size >= 3 && memcmp(copy, "\xef\xbb\xbf", 3) == 0) i = 3; /* a UTF-8 byte order mark */
	while (i < size) {
		const char *nl = (const char *)memchr(copy + i, '\n', size - i);
		size_t end = nl ? (size_t)(nl - copy) : size, len = end - i;
		P->line++;
		if (len > 0 && copy[i + len - 1] == '\r') len--;
		if (memchr(copy + i, 0, len)) return fail(P, "a NUL byte");
		if (len >= KP_SCRIPT_LINE) return fail(P, "a line longer than %d bytes", KP_SCRIPT_LINE - 1);
		if (split(P, copy + i, len, &W) != 0) return -1;
		if (W.n > 0 && parse_line(P, &W) != 0) return -1;
		i = end + 1;
	}
	if (P->amp) return fail(P, "the last line ends in &: an input must follow it");
	return 0;
}

int kp_script_parse(KpScript *s, const char *name, const char *text, size_t size, const KpScriptHost *h, KpError *err)
{
	Parse P;
	const char *base = name, *p;
	size_t len;
	memset(s, 0, sizeof *s);
	memset(&P, 0, sizeof P);
	P.s = s;
	P.h = h;
	P.err = err;
	err->msg[0] = 0;
	for (p = name; *p; p++) {
		if (*p == '/' || *p == '\\') base = p + 1;
	}
	snprintf(s->files[0], sizeof s->files[0], "%s", base);
	len = strlen(s->files[0]);
	snprintf(s->name, sizeof s->name, "%.*s", (int)(len > 4 && strcmp(s->files[0] + len - 4, ".kps") == 0 ? len - 4 : len),
		s->files[0]);
	s->file_count = 1;
	s->timeout_ms = KP_SCRIPT_TIMEOUT_MS;
	s->watchdog_ms = KP_SCRIPT_WATCHDOG_MS;
	if (parse_text(&P, 0, text, size) != 0) {
		kp_script_free(s);
		return -1;
	}
	s->runnable = s->count;
	return 0;
}

int kp_script_live_begin(KpScript *s, KpError *err)
{
	if (s->file_count == KP_SCRIPT_FILES) {
		snprintf(err->msg, sizeof err->msg, "%s: no file left for the live lines (%d at most)", s->files[0], KP_SCRIPT_FILES);
		return -1;
	}
	s->live = 1;
	s->live_file = s->file_count++;
	snprintf(s->files[s->live_file], sizeof s->files[0], "%s", KP_SCRIPT_LIVE_NAME);
	return 0;
}

KP_SCRIPT_PRINTF(2, 3) static void say(const KpScriptHost *h, const char *fmt, ...);
static void line_text(const KpScript *s, int file, int line, char *out, size_t size);

/* A live line's answer once its steps ran (or at once, an include of none). */
static void answer(const KpScript *s, const KpScriptHost *h, int line)
{
	char text[KP_SCRIPT_LINE];
	line_text(s, s->live_file, line, text, sizeof text);
	say(h, "script %s:%d: %s: done", s->files[s->live_file], line, text);
}

/* The live line kept for the log's quotes (kp_script_line counts the lines): 0, or -1 out of memory. */
static int keep_live_line(KpScript *s, const char *line, size_t len)
{
	size_t at = s->sizes[s->live_file];
	char *text = (char *)realloc(s->texts[s->live_file], at + len + 2);
	if (!text) return -1;
	memcpy(text + at, line, len);
	text[at + len] = '\n';
	text[at + len + 1] = 0;
	s->texts[s->live_file] = text;
	s->sizes[s->live_file] = at + len + 1;
	return 0;
}

int kp_script_live_line(KpScript *s, const char *line, size_t len, const KpScriptHost *h, KpError *err)
{
	Parse P;
	Words W;
	int count = s->count, marks = s->mark_count, picks = s->pick_count, files = s->file_count, k, rc = 0;
	int mark_values[KP_SCRIPT_MARKS];
	memset(&P, 0, sizeof P);
	P.s = s;
	P.h = h;
	P.err = err;
	P.file = s->live_file;
	P.line = ++s->live_lines;
	P.commands = 1; /* the header is the file's */
	P.amp = s->live_amp;
	err->msg[0] = 0;
	if (len > 0 && line[len - 1] == '\r') len--;
	/* a line refused for its bytes is kept empty: the lines after it keep their numbers */
	if (memchr(line, 0, len)) rc = fail(&P, "a NUL byte");
	else if (len >= KP_SCRIPT_LINE) rc = fail(&P, "a line longer than %d bytes", KP_SCRIPT_LINE - 1);
	if (keep_live_line(s, line, rc ? 0 : len) != 0) {
		s->live_lines--; /* not kept: the next line takes its number, the quotes stay the lines' */
		return fail(&P, "out of memory");
	}
	if (rc != 0) return -1;
	for (k = 0; k < marks; k++) mark_values[k] = s->marks[k].count;
	if (split(&P, line, len, &W) != 0 || (W.n > 0 && parse_line(&P, &W) != 0)) {
		/* the line's steps, marks, picks and includes undone (a mark's values asked for by its expressions too) */
		s->count = count;
		s->mark_count = marks;
		s->pick_count = picks;
		for (k = 0; k < marks; k++) s->marks[k].count = mark_values[k];
		for (k = files; k < s->file_count; k++) {
			free(s->texts[k]);
			s->texts[k] = NULL;
			s->sizes[k] = 0;
		}
		s->file_count = files;
		return -1;
	}
	if (W.n == 0) return 0;
	for (k = count; k < s->count; k++) s->steps[k].live_line = P.line;
	if (s->count == count && h) answer(s, h, P.line); /* an include of no steps */
	if (P.amp && !s->live_amp) s->chain_start = count;
	s->live_amp = P.amp;
	s->runnable = P.amp ? s->chain_start : s->count;
	return s->count - count;
}

void kp_script_free(KpScript *s)
{
	int k;
	for (k = 0; k < KP_SCRIPT_FILES; k++) free(s->texts[k]);
	free(s->steps);
	memset(s, 0, sizeof *s);
}

KpScriptClass kp_script_class(KpScriptOp op)
{
	/* KpScriptOp lists the inputs, then the waits, then the observations */
	if (op <= KP_SCRIPT_HANG) return KP_SCRIPT_INPUT;
	if (op <= KP_SCRIPT_UNTIL) return KP_SCRIPT_WAITS;
	if (op <= KP_SCRIPT_CHECK) return KP_SCRIPT_OBSERVES;
	return KP_SCRIPT_ENDS;
}

void kp_script_line(const KpScript *s, const KpScriptStep *step, char *out, size_t size)
{
	line_text(s, step->file, step->line, out, size);
}

static void line_text(const KpScript *s, int file, int at, char *out, size_t size)
{
	const char *t = file >= 0 && file < s->file_count ? s->texts[file] : NULL;
	size_t n = t ? s->sizes[file] : 0, i = 0, j;
	int line = 1;
	if (size == 0) return;
	out[0] = 0;
	if (!t) return;
	if (n >= 3 && memcmp(t, "\xef\xbb\xbf", 3) == 0) i = 3;
	while (line < at && i < n) {
		if (t[i++] == '\n') line++;
	}
	{ /* the line's end, or its comment's start: a word's first # outside a string (split's rules: \" and \\ escape) */
		int quoted = 0;
		for (j = i; j < n && t[j] != '\n'; j++) {
			if (quoted && t[j] == '\\' && j + 1 < n && t[j + 1] != '\n') j++;
			else if (t[j] == '"') quoted = !quoted;
			else if (!quoted && t[j] == '#' && (j == i || t[j - 1] == ' ' || t[j - 1] == '\t')) break;
		}
	}
	while (i < j && (t[i] == ' ' || t[i] == '\t')) i++;
	while (j > i && (t[j - 1] == ' ' || t[j - 1] == '\t' || t[j - 1] == '\r')) j--;
	if (j - i >= size) j = i + size - 1;
	memcpy(out, t + i, j - i);
	out[j - i] = 0;
}

/* --- the run --- */

KP_SCRIPT_PRINTF(2, 3) static void say(const KpScriptHost *h, const char *fmt, ...)
{
	char line[LOG_LINE];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(line, sizeof line, fmt, ap);
	va_end(ap);
	if (h->log) h->log(h->ud, line);
}

static void step_failed(const KpScriptRun *run, const KpScriptHost *h, const KpScriptStep *st, const char *why)
{
	char line[KP_SCRIPT_LINE];
	kp_script_line(run->script, st, line, sizeof line);
	say(h, "script %s:%d: %s: %s FAILED", run->script->files[st->file], st->line, line, why[0] ? why : "failed");
}

static void finish(KpScriptRun *run, const KpScriptHost *h)
{
	if (run->done) return;
	run->done = 1;
	say(h, "script %s: %d steps, %d failures", run->script->name, run->steps, run->failures);
}

/* The step after st's line (until's input is on its line). */
static int line_end(const KpScript *s, int at)
{
	const KpScriptStep *st = &s->steps[at];
	while (at < s->count && s->steps[at].file == st->file && s->steps[at].line == st->line) at++;
	return at;
}

static int abort_at(KpScriptRun *run, const KpScriptHost *h, const KpScriptStep *st, const char *why)
{
	const KpScript *s = run->script;
	step_failed(run, h, st, why);
	run->failures++;
	if (h->aborted) h->aborted(h->ud, run);
	if (s->live) { /* counted; the steps received before it skipped, the lines after it run */
		int skipped = s->count - line_end(s, (int)(st - s->steps));
		if (skipped > 0) say(h, "script %s: %d steps after it skipped", s->name, skipped);
		run->step = s->count;
		run->entered = 0;
		return 0;
	}
	run->aborted = 1;
	finish(run, h);
	return 0;
}

/* The step at run->step past, n steps with until's input: a live line's last step (its include's too) answers it,
 * unless a check of it failed. */
static void next_step(KpScriptRun *run, const KpScriptHost *h, int n)
{
	const KpScript *s = run->script;
	int line;
	run->step += n;
	line = s->steps[run->step - 1].live_line;
	if (line > 0 && (run->step >= s->count || s->steps[run->step].live_line != line) && run->failed_live_line != line) {
		answer(s, h, line);
	}
}

static int expr_value(const KpScriptRun *run, const KpScriptHost *h, const KpScriptExpr *e, int64_t *v, char *why, size_t size)
{
	switch (e->kind) {
	case KP_SCRIPT_EXPR_NUMBER: *v = e->k; return 0;
	case KP_SCRIPT_EXPR_MARK:
		if (!run->marked[e->mark]) {
			snprintf(why, size, "the mark %s was not set", run->script->marks[e->mark].name);
			return -1;
		}
		if (e->slot >= run->mark_taken[e->mark]) { /* a live line's: the mark ran before the line asked it for the value */
			snprintf(why, size, "the mark %s did not keep this value when it ran (a new mark keeps it)",
				run->script->marks[e->mark].name);
			return -1;
		}
		*v = run->marks[e->mark][e->slot] + e->k;
		return 0;
	default:
		why[0] = 0;
		if (!h->value || h->value(h->ud, run, &e->value, v, why, size) != 0) {
			if (!why[0]) snprintf(why, size, "no value");
			return -1;
		}
		*v += e->k;
		return 0;
	}
}

static int compare(int64_t a, int op, int64_t b)
{
	switch (op) {
	case KP_SCRIPT_EQ: return a == b;
	case KP_SCRIPT_NE: return a != b;
	case KP_SCRIPT_LT: return a < b;
	case KP_SCRIPT_LE: return a <= b;
	case KP_SCRIPT_GT: return a > b;
	default: return a >= b;
	}
}

/* 1 holds; 0 not, why saying which term; -1 a value cannot be had (why). */
static int holds(const KpScriptRun *run, const KpScriptHost *h, const KpScriptCond *c, char *why, size_t size)
{
	char term[16];
	int k;
	for (k = 0; k < c->count; k++) {
		const KpScriptTerm *t = &c->terms[k];
		term[0] = 0;
		if (c->count > 1) snprintf(term, sizeof term, "term %d: ", k + 1);
		why[0] = 0;
		if (t->named) {
			char detail[KP_SCRIPT_WHY];
			detail[0] = 0;
			if (!h->cond || h->cond(h->ud, run, &t->cond, detail, sizeof detail) != 1) {
				snprintf(why, size, "%s%s", term, detail[0] ? detail : "does not hold");
				return 0;
			}
		} else {
			int64_t a, b;
			if (expr_value(run, h, &t->a, &a, why, size) != 0 || expr_value(run, h, &t->b, &b, why, size) != 0) return -1;
			if (!compare(a, t->op, b)) {
				snprintf(why, size, "%s%lld %s %lld does not hold", term, (long long)a, OPS[t->op], (long long)b);
				return 0;
			}
		}
	}
	why[0] = 0;
	return 1;
}

/* log's line: the label, then each value's name (the line's words) and its value. */
static void log_values(const KpScriptRun *run, const KpScriptHost *h, const KpScriptStep *st)
{
	char line[KP_SCRIPT_LINE], out[LOG_LINE], why[KP_SCRIPT_WHY];
	const char *words[KP_SCRIPT_WORDS];
	char *p;
	int n = 0, k;
	size_t used;
	kp_script_line(run->script, st, line, sizeof line);
	for (p = line; *p && n < KP_SCRIPT_WORDS;) {
		while (*p == ' ' || *p == '\t') *p++ = 0;
		if (!*p || *p == '#') break;
		words[n++] = p;
		while (*p && *p != ' ' && *p != '\t') p++;
	}
	*p = 0;
	used = (size_t)snprintf(out, sizeof out, "script %s:%d: %s", run->script->files[st->file], st->line, st->text);
	for (k = 0; k < st->value_count && used < sizeof out; k++) {
		int64_t v;
		const char *name = k + 2 < n ? words[k + 2] : "?";
		why[0] = 0;
		if (h->value && h->value(h->ud, run, &st->values[k], &v, why, sizeof why) == 0) {
			used += (size_t)snprintf(out + used, sizeof out - used, "%s %s %lld", k ? "," : ":", name, (long long)v);
		} else {
			used += (size_t)snprintf(out + used, sizeof out - used, "%s %s n/a", k ? "," : ":", name);
		}
	}
	say(h, "%s", out);
}

/* An observation: 0 the run goes on (a failed check counted), -1 it aborted. */
static int observe(KpScriptRun *run, const KpScriptHost *h, const KpScriptStep *st)
{
	char why[KP_SCRIPT_WHY];
	int k, r;
	why[0] = 0;
	switch (st->op) {
	case KP_SCRIPT_MARK: {
		const KpScriptMark *m = &run->script->marks[st->id];
		for (k = 0; k < m->count; k++) {
			why[0] = 0;
			if (!h->value || h->value(h->ud, run, &m->values[k], &run->marks[st->id][k], why, sizeof why) != 0) {
				abort_at(run, h, st, why[0] ? why : "a value of the mark's has none");
				return -1;
			}
		}
		run->marked[st->id] = 1;
		run->mark_taken[st->id] = m->count;
		return 0;
	}
	case KP_SCRIPT_LOG: log_values(run, h, st); return 0;
	case KP_SCRIPT_EXPECT:
		if (holds(run, h, &st->cond, why, sizeof why) != 1) {
			abort_at(run, h, st, why);
			return -1;
		}
		return 0;
	default:
		r = h->exec ? h->exec(h->ud, run, st, why, sizeof why) : -1;
		if (st->op == KP_SCRIPT_CHECK) run->checks++;
		if (r == KP_SCRIPT_CHECK_FAILED && st->op == KP_SCRIPT_CHECK) {
			step_failed(run, h, st, why);
			run->failures++;
			run->failed_live_line = st->live_line;
			return 0;
		} else if (r != 0) {
			abort_at(run, h, st, why[0] ? why : "the host cannot run it");
			return -1;
		}
		return 0;
	}
}

static int input(KpScriptRun *run, const KpScriptHost *h, const KpScriptStep *st)
{
	char why[KP_SCRIPT_WHY];
	why[0] = 0;
	if (!h->exec || h->exec(h->ud, run, st, why, sizeof why) != 0) {
		abort_at(run, h, st, why[0] ? why : "the host cannot run it");
		return -1;
	}
	return 0;
}

/* A wait's frame: 1 it goes on (the frame's steps end), 0 it is over (the next step runs now), -1 the run aborted. */
static int wait_frame(KpScriptRun *run, const KpScriptHost *h, const KpScriptStep *st, int64_t now)
{
	char why[KP_SCRIPT_WHY];
	int r;
	if (!run->entered) {
		run->entered = 1;
		run->why[0] = 0;
		run->since_ms = now;
		if (st->op == KP_SCRIPT_WAIT_FRAMES) {
			run->frames_left = st->n;
			return 1;
		}
		if (st->op == KP_SCRIPT_WAIT_TIME) {
			run->deadline_ms = now + st->n;
		} else {
			int64_t ms = run->script->timeout_ms;
			if (st->has_within && expr_value(run, h, &st->within, &ms, why, sizeof why) != 0) {
				run->entered = 0;
				return abort_at(run, h, st, why) - 1;
			}
			run->deadline_ms = now + ms;
			run->next_input_ms = now;
			run->inputs = 0;
		}
	} else if (st->op == KP_SCRIPT_WAIT_FRAMES && --run->frames_left > 0) {
		return 1;
	}
	switch (st->op) {
	case KP_SCRIPT_WAIT_FRAMES: break;
	case KP_SCRIPT_WAIT_TIME:
		if (now < run->deadline_ms) return 1;
		break;
	default:
		r = holds(run, h, &st->cond, why, sizeof why);
		if (r != 1) {
			if (why[0]) snprintf(run->why, sizeof run->why, "%s", why);
			if (now >= run->deadline_ms) {
				char msg[KP_SCRIPT_WHY + 64];
				snprintf(msg, sizeof msg, "no change in %lld ms%s%s", (long long)(now - run->since_ms), run->why[0] ? ": " : "",
					run->why);
				run->entered = 0;
				return abort_at(run, h, st, msg) - 1;
			}
			if (st->op == KP_SCRIPT_UNTIL && now >= run->next_input_ms) {
				if (input(run, h, st + 1) != 0) return -1;
				run->next_input_ms = now + st->n;
				run->inputs++;
			}
			return 1;
		}
		break;
	}
	run->entered = 0;
	/* until's input is a step of its own: run once at least, or never when the condition held at once */
	run->steps += 1 + (st->op == KP_SCRIPT_UNTIL && run->inputs > 0);
	next_step(run, h, st->op == KP_SCRIPT_UNTIL ? 2 : 1);
	return 0;
}

void kp_script_start(KpScriptRun *run, const KpScript *s)
{
	memset(run, 0, sizeof *run);
	run->script = s;
}

int kp_script_frame(KpScriptRun *run, const KpScriptHost *h, int64_t now_ms)
{
	const KpScript *s = run->script;
	if (run->done) return 0;
	run->frame++;
	for (;;) {
		const KpScriptStep *st;
		int r;
		if (run->step >= s->runnable) {
			if (s->live) return 1; /* its next line to come */
			finish(run, h);
			return 0;
		}
		st = &s->steps[run->step];
		switch (kp_script_class(st->op)) {
		case KP_SCRIPT_INPUT:
			/* an abort ends a file's run; a live run goes on with its next line */
			if (input(run, h, st) != 0) return !run->done;
			run->steps++;
			next_step(run, h, 1);
			if (!st->same_frame) return 1;
			break;
		case KP_SCRIPT_WAITS:
			r = wait_frame(run, h, st, now_ms);
			if (r != 0) return !run->done;
			break;
		case KP_SCRIPT_OBSERVES:
			if (observe(run, h, st) != 0) return !run->done;
			run->steps++;
			next_step(run, h, 1);
			break;
		default:
			run->steps++;
			finish(run, h);
			return 0;
		}
	}
}

void kp_script_abort(KpScriptRun *run, const KpScriptHost *h, const char *why)
{
	if (run->done) return;
	if (run->step < run->script->count) step_failed(run, h, &run->script->steps[run->step], why);
	else say(h, "script %s: %s FAILED", run->script->name, why);
	run->failures++;
	run->aborted = 1;
	finish(run, h);
}

int kp_script_idle(const KpScriptRun *run)
{
	return run->script->live && !run->done && run->step >= run->script->runnable;
}
