/* The JSON reader and writer of the oracle tools and the C suites (kp_json.h). */
#include "kp_json.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BLOCK_SIZE (1u << 20)
#define MAX_DEPTH 256

struct KpJsonBlock {
	KpJsonBlock *next;
	size_t used, size;
	/* the bytes follow, aligned for any value */
};

/* The bytes of a block after its header, aligned to 16. */
#define BLOCK_HEADER ((sizeof(KpJsonBlock) + 15) & ~(size_t)15)

static void out_of_memory(void)
{
	fprintf(stderr, "kp_json: out of memory\n");
	abort();
}

void kp_json_doc_init(KpJsonDoc *doc) { doc->blocks = NULL; }

void kp_json_doc_free(KpJsonDoc *doc)
{
	KpJsonBlock *b = doc->blocks;
	while (b) {
		KpJsonBlock *next = b->next;
		free(b);
		b = next;
	}
	doc->blocks = NULL;
}

void *kp_json_alloc(KpJsonDoc *doc, size_t size)
{
	KpJsonBlock *b = doc->blocks;
	size = (size + 15) & ~(size_t)15;
	if (!b || b->size - b->used < size) {
		size_t room = size > BLOCK_SIZE ? size : BLOCK_SIZE;
		b = (KpJsonBlock *)malloc(BLOCK_HEADER + room);
		if (!b) out_of_memory();
		b->size = room;
		b->used = 0;
		b->next = doc->blocks;
		doc->blocks = b;
	}
	{
		char *p = (char *)b + BLOCK_HEADER + b->used;
		b->used += size;
		memset(p, 0, size);
		return p;
	}
}

/* --- parsing --- */

typedef struct {
	KpJsonDoc *doc;
	const char *p, *end;
	char *err;
	size_t err_size;
	int failed;
	/* the children of the open containers (each container pops its own when it closes) */
	KpJson **items;
	const char **keys;
	size_t top, cap;
	char *chars; /* a string's bytes while it is read */
	size_t chars_cap;
} Parser;

static void parse_error(Parser *ps, const char *what)
{
	if (ps->failed) return;
	ps->failed = 1;
	if (ps->err && ps->err_size) snprintf(ps->err, ps->err_size, "json: %s", what);
}

static void skip_space(Parser *ps)
{
	while (ps->p < ps->end && (*ps->p == ' ' || *ps->p == '\t' || *ps->p == '\n' || *ps->p == '\r')) ps->p++;
}

static void push_child(Parser *ps, const char *key, KpJson *v)
{
	if (ps->top == ps->cap) {
		size_t cap = ps->cap ? ps->cap * 2 : 1024;
		KpJson **items = (KpJson **)realloc(ps->items, cap * sizeof *items);
		const char **keys = (const char **)realloc(ps->keys, cap * sizeof *keys);
		if (!items || !keys) out_of_memory();
		ps->items = items;
		ps->keys = keys;
		ps->cap = cap;
	}
	ps->items[ps->top] = v;
	ps->keys[ps->top] = key;
	ps->top++;
}

static void add_char(Parser *ps, size_t *n, char c)
{
	if (*n + 1 >= ps->chars_cap) {
		size_t cap = ps->chars_cap ? ps->chars_cap * 2 : 256;
		char *chars = (char *)realloc(ps->chars, cap);
		if (!chars) out_of_memory();
		ps->chars = chars;
		ps->chars_cap = cap;
	}
	ps->chars[(*n)++] = c;
}

static int hex4(const char *p, unsigned *out)
{
	unsigned v = 0;
	int k;
	for (k = 0; k < 4; k++) {
		char c = p[k];
		v <<= 4;
		if (c >= '0' && c <= '9') v |= (unsigned)(c - '0');
		else if (c >= 'a' && c <= 'f') v |= (unsigned)(c - 'a' + 10);
		else if (c >= 'A' && c <= 'F') v |= (unsigned)(c - 'A' + 10);
		else return -1;
	}
	*out = v;
	return 0;
}

/* A string at ps->p (after its quote) into the arena; NULL on an error. */
static const char *parse_string(Parser *ps, int *length)
{
	size_t n = 0;
	char *s;
	while (ps->p < ps->end && *ps->p != '"') {
		char c = *ps->p++;
		if (c == '\\') {
			char e;
			if (ps->p >= ps->end) break;
			e = *ps->p++;
			switch (e) {
			case '"': add_char(ps, &n, '"'); break;
			case '\\': add_char(ps, &n, '\\'); break;
			case '/': add_char(ps, &n, '/'); break;
			case 'b': add_char(ps, &n, '\b'); break;
			case 'f': add_char(ps, &n, '\f'); break;
			case 'n': add_char(ps, &n, '\n'); break;
			case 'r': add_char(ps, &n, '\r'); break;
			case 't': add_char(ps, &n, '\t'); break;
			case 'u': {
				unsigned cp;
				if (ps->end - ps->p < 4 || hex4(ps->p, &cp) != 0) {
					parse_error(ps, "bad \\u escape");
					return NULL;
				}
				ps->p += 4;
				if (cp >= 0xd800 && cp < 0xdc00 && ps->end - ps->p >= 6 && ps->p[0] == '\\' && ps->p[1] == 'u') {
					unsigned lo;
					if (hex4(ps->p + 2, &lo) == 0 && lo >= 0xdc00 && lo < 0xe000) {
						cp = 0x10000 + ((cp - 0xd800) << 10) + (lo - 0xdc00);
						ps->p += 6;
					}
				}
				if (cp < 0x100) {
					add_char(ps, &n, (char)cp);
				} else if (cp < 0x800) {
					add_char(ps, &n, (char)(0xc0 | (cp >> 6)));
					add_char(ps, &n, (char)(0x80 | (cp & 0x3f)));
				} else if (cp < 0x10000) {
					add_char(ps, &n, (char)(0xe0 | (cp >> 12)));
					add_char(ps, &n, (char)(0x80 | ((cp >> 6) & 0x3f)));
					add_char(ps, &n, (char)(0x80 | (cp & 0x3f)));
				} else {
					add_char(ps, &n, (char)(0xf0 | (cp >> 18)));
					add_char(ps, &n, (char)(0x80 | ((cp >> 12) & 0x3f)));
					add_char(ps, &n, (char)(0x80 | ((cp >> 6) & 0x3f)));
					add_char(ps, &n, (char)(0x80 | (cp & 0x3f)));
				}
				break;
			}
			default:
				parse_error(ps, "bad escape");
				return NULL;
			}
		} else {
			add_char(ps, &n, c);
		}
	}
	if (ps->p >= ps->end) {
		parse_error(ps, "unterminated string");
		return NULL;
	}
	ps->p++; /* the closing quote */
	s = (char *)kp_json_alloc(ps->doc, n + 1);
	if (n) memcpy(s, ps->chars, n);
	s[n] = '\0';
	*length = (int)n;
	return s;
}

static KpJson *parse_value(Parser *ps, int depth);

/* Closes the container v whose children are ps->items[base..top). */
static void close_container(Parser *ps, KpJson *v, size_t base)
{
	size_t n = ps->top - base;
	v->count = v->cap = (int)n;
	if (n) {
		v->items = (KpJson **)kp_json_alloc(ps->doc, n * sizeof *v->items);
		memcpy(v->items, ps->items + base, n * sizeof *v->items);
		if (v->type == KP_JSON_OBJECT) {
			v->keys = (const char **)kp_json_alloc(ps->doc, n * sizeof *v->keys);
			memcpy(v->keys, ps->keys + base, n * sizeof *v->keys);
		}
	}
	ps->top = base;
}

static KpJson *parse_container(Parser *ps, int depth, int object)
{
	KpJson *v = kp_json_new(ps->doc, object ? KP_JSON_OBJECT : KP_JSON_ARRAY);
	size_t base = ps->top;
	char close = object ? '}' : ']';
	if (depth > MAX_DEPTH) {
		parse_error(ps, "nested too deeply");
		return NULL;
	}
	skip_space(ps);
	if (ps->p < ps->end && *ps->p == close) {
		ps->p++;
		return v;
	}
	for (;;) {
		const char *key = NULL;
		KpJson *item;
		skip_space(ps);
		if (object) {
			int len;
			if (ps->p >= ps->end || *ps->p != '"') {
				parse_error(ps, "expected a key");
				return NULL;
			}
			ps->p++;
			if (!(key = parse_string(ps, &len))) return NULL;
			skip_space(ps);
			if (ps->p >= ps->end || *ps->p != ':') {
				parse_error(ps, "expected ':'");
				return NULL;
			}
			ps->p++;
		}
		if (!(item = parse_value(ps, depth + 1))) return NULL;
		push_child(ps, key, item);
		skip_space(ps);
		if (ps->p < ps->end && *ps->p == ',') {
			ps->p++;
			continue;
		}
		if (ps->p < ps->end && *ps->p == close) {
			ps->p++;
			break;
		}
		parse_error(ps, object ? "expected ',' or '}'" : "expected ',' or ']'");
		return NULL;
	}
	close_container(ps, v, base);
	return v;
}

static KpJson *parse_number(Parser *ps)
{
	const char *start = ps->p;
	char buf[64];
	size_t n;
	int fraction = 0;
	KpJson *v;
	if (ps->p < ps->end && *ps->p == '-') ps->p++;
	while (ps->p < ps->end) {
		char c = *ps->p;
		if (c >= '0' && c <= '9') {
			ps->p++;
		} else if (c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-') {
			fraction = 1;
			ps->p++;
		} else {
			break;
		}
	}
	n = (size_t)(ps->p - start);
	if (n == 0 || n >= sizeof buf || (n == 1 && *start == '-')) {
		parse_error(ps, "bad number");
		return NULL;
	}
	memcpy(buf, start, n);
	buf[n] = '\0';
	v = kp_json_new(ps->doc, KP_JSON_NUMBER);
	v->d = strtod(buf, NULL);
	if (!fraction && n < 19 + (*start == '-')) {
		v->i = strtoll(buf, NULL, 10);
		v->integral = 1;
	} else if (!fraction) {
		v->i = strtoll(buf, NULL, 10); /* 19 digits or more: integral when it fits */
		v->integral = fabs(v->d) < 9.2e18;
	} else {
		v->i = (int64_t)v->d;
		v->integral = v->d == floor(v->d) && fabs(v->d) < 9.2e18;
	}
	return v;
}

static int match_word(Parser *ps, const char *word)
{
	size_t n = strlen(word);
	if ((size_t)(ps->end - ps->p) < n || memcmp(ps->p, word, n) != 0) return 0;
	ps->p += n;
	return 1;
}

static KpJson *parse_value(Parser *ps, int depth)
{
	skip_space(ps);
	if (ps->p >= ps->end) {
		parse_error(ps, "unexpected end");
		return NULL;
	}
	switch (*ps->p) {
	case '{': ps->p++; return parse_container(ps, depth, 1);
	case '[': ps->p++; return parse_container(ps, depth, 0);
	case '"': {
		KpJson *v = kp_json_new(ps->doc, KP_JSON_STRING);
		ps->p++;
		if (!(v->s = parse_string(ps, &v->count))) return NULL;
		return v;
	}
	case 't':
		if (match_word(ps, "true")) return kp_json_new(ps->doc, KP_JSON_TRUE);
		break;
	case 'f':
		if (match_word(ps, "false")) return kp_json_new(ps->doc, KP_JSON_FALSE);
		break;
	case 'n':
		if (match_word(ps, "null")) return kp_json_new(ps->doc, KP_JSON_NULL);
		break;
	default:
		if (*ps->p == '-' || (*ps->p >= '0' && *ps->p <= '9')) return parse_number(ps);
		break;
	}
	parse_error(ps, "unexpected character");
	return NULL;
}

KpJson *kp_json_parse(KpJsonDoc *doc, const char *text, size_t size, char *err, size_t err_size)
{
	Parser ps;
	KpJson *v;
	memset(&ps, 0, sizeof ps);
	ps.doc = doc;
	ps.p = text;
	ps.end = text + size;
	ps.err = err;
	ps.err_size = err_size;
	if (err && err_size) err[0] = '\0';
	v = parse_value(&ps, 0);
	if (v) {
		skip_space(&ps);
		if (ps.p != ps.end) {
			parse_error(&ps, "trailing characters");
			v = NULL;
		}
	}
	if (ps.failed && err && err_size) { /* the error's place: the byte offset from the start */
		size_t n = strlen(err);
		snprintf(err + n, err_size - n, " at byte %ld", (long)(ps.p - text));
	}
	free(ps.items);
	free(ps.keys);
	free(ps.chars);
	return ps.failed ? NULL : v;
}

/* --- lookups --- */

const KpJson *kp_json_get(const KpJson *object, const char *key)
{
	int k;
	if (!object || object->type != KP_JSON_OBJECT) return NULL;
	for (k = 0; k < object->count; k++) {
		if (strcmp(object->keys[k], key) == 0) return object->items[k];
	}
	return NULL;
}

const KpJson *kp_json_at(const KpJson *array, int index)
{
	if (!array || array->type != KP_JSON_ARRAY || index < 0 || index >= array->count) return NULL;
	return array->items[index];
}

int kp_json_len(const KpJson *v)
{
	return v && (v->type == KP_JSON_ARRAY || v->type == KP_JSON_OBJECT) ? v->count : 0;
}

int kp_json_is(const KpJson *v, KpJsonType type) { return v && v->type == type; }

int64_t kp_json_int(const KpJson *v, int64_t fallback)
{
	if (!v) return fallback;
	if (v->type == KP_JSON_NUMBER) return v->i;
	if (v->type == KP_JSON_TRUE) return 1;
	if (v->type == KP_JSON_FALSE) return 0;
	return fallback;
}

int kp_json_truthy(const KpJson *v) { return v && v->type != KP_JSON_NULL && v->type != KP_JSON_FALSE; }

const char *kp_json_str(const KpJson *v, const char *fallback)
{
	return v && v->type == KP_JSON_STRING ? v->s : fallback;
}

int64_t kp_json_field_int(const KpJson *object, const char *key, int64_t fallback)
{
	return kp_json_int(kp_json_get(object, key), fallback);
}

const char *kp_json_field_str(const KpJson *object, const char *key, const char *fallback)
{
	return kp_json_str(kp_json_get(object, key), fallback);
}

/* --- building --- */

KpJson *kp_json_new(KpJsonDoc *doc, KpJsonType type)
{
	KpJson *v = (KpJson *)kp_json_alloc(doc, sizeof *v);
	v->type = type;
	return v;
}

KpJson *kp_json_new_int(KpJsonDoc *doc, int64_t value)
{
	KpJson *v = kp_json_new(doc, KP_JSON_NUMBER);
	v->i = value;
	v->d = (double)value;
	v->integral = 1;
	return v;
}

KpJson *kp_json_new_bool(KpJsonDoc *doc, int value) { return kp_json_new(doc, value ? KP_JSON_TRUE : KP_JSON_FALSE); }

KpJson *kp_json_new_string(KpJsonDoc *doc, const char *s, size_t n)
{
	KpJson *v = kp_json_new(doc, KP_JSON_STRING);
	char *copy = (char *)kp_json_alloc(doc, n + 1);
	if (n) memcpy(copy, s, n);
	copy[n] = '\0';
	v->s = copy;
	v->count = (int)n;
	return v;
}

static void grow(KpJsonDoc *doc, KpJson *v)
{
	int cap = v->cap ? v->cap * 2 : 8;
	KpJson **items = (KpJson **)kp_json_alloc(doc, (size_t)cap * sizeof *items);
	if (v->count) memcpy(items, v->items, (size_t)v->count * sizeof *items);
	v->items = items;
	if (v->type == KP_JSON_OBJECT) {
		const char **keys = (const char **)kp_json_alloc(doc, (size_t)cap * sizeof *keys);
		if (v->count) memcpy(keys, v->keys, (size_t)v->count * sizeof *keys);
		v->keys = keys;
	}
	v->cap = cap;
}

KpJson *kp_json_push(KpJsonDoc *doc, KpJson *array, KpJson *item)
{
	if (array->count == array->cap) grow(doc, array);
	array->items[array->count++] = item;
	return item;
}

KpJson *kp_json_push_int(KpJsonDoc *doc, KpJson *array, int64_t v) { return kp_json_push(doc, array, kp_json_new_int(doc, v)); }

KpJson *kp_json_set(KpJsonDoc *doc, KpJson *object, const char *key, KpJson *item)
{
	int k;
	size_t n;
	char *copy;
	for (k = 0; k < object->count; k++) {
		if (strcmp(object->keys[k], key) == 0) {
			object->items[k] = item;
			return item;
		}
	}
	if (object->count == object->cap) grow(doc, object);
	n = strlen(key);
	copy = (char *)kp_json_alloc(doc, n + 1);
	memcpy(copy, key, n + 1);
	object->keys[object->count] = copy;
	object->items[object->count++] = item;
	return item;
}

KpJson *kp_json_set_int(KpJsonDoc *doc, KpJson *object, const char *key, int64_t v)
{
	return kp_json_set(doc, object, key, kp_json_new_int(doc, v));
}

int kp_json_equal(const KpJson *a, const KpJson *b)
{
	int k;
	if (!a || !b) return a == b;
	if ((a->type == KP_JSON_ARRAY || a->type == KP_JSON_OBJECT) && (b->type == KP_JSON_ARRAY || b->type == KP_JSON_OBJECT)
		&& a->count == 0 && b->count == 0) {
		return 1;
	}
	if (a->type != b->type) return 0;
	switch (a->type) {
	case KP_JSON_NUMBER: return a->integral && b->integral ? a->i == b->i : a->d == b->d;
	case KP_JSON_STRING: return a->count == b->count && memcmp(a->s, b->s, (size_t)a->count) == 0;
	case KP_JSON_ARRAY:
		if (a->count != b->count) return 0;
		for (k = 0; k < a->count; k++) {
			if (!kp_json_equal(a->items[k], b->items[k])) return 0;
		}
		return 1;
	case KP_JSON_OBJECT:
		if (a->count != b->count) return 0;
		for (k = 0; k < a->count; k++) {
			if (!kp_json_equal(a->items[k], kp_json_get(b, a->keys[k]))) return 0;
		}
		return 1;
	default: return 1;
	}
}

/* --- buffers and writing --- */

void kp_buf_init(KpBuf *b)
{
	b->data = NULL;
	b->size = b->cap = 0;
}

void kp_buf_free(KpBuf *b)
{
	free(b->data);
	kp_buf_init(b);
}

static void reserve(KpBuf *b, size_t n)
{
	if (b->size + n + 1 > b->cap) {
		size_t cap = b->cap ? b->cap : 256;
		char *data;
		while (cap < b->size + n + 1) cap *= 2;
		data = (char *)realloc(b->data, cap);
		if (!data) out_of_memory();
		b->data = data;
		b->cap = cap;
	}
}

void kp_buf_add(KpBuf *b, const void *bytes, size_t n)
{
	reserve(b, n);
	if (n) memcpy(b->data + b->size, bytes, n);
	b->size += n;
	b->data[b->size] = '\0';
}

void kp_buf_str(KpBuf *b, const char *s) { kp_buf_add(b, s, strlen(s)); }

void kp_buf_char(KpBuf *b, char c) { kp_buf_add(b, &c, 1); }

void kp_buf_vprintf(KpBuf *b, const char *fmt, va_list ap)
{
	va_list copy;
	int n;
	va_copy(copy, ap);
	n = vsnprintf(NULL, 0, fmt, copy);
	va_end(copy);
	if (n < 0) return;
	reserve(b, (size_t)n);
	vsnprintf(b->data + b->size, (size_t)n + 1, fmt, ap);
	b->size += (size_t)n;
}

void kp_buf_printf(KpBuf *b, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	kp_buf_vprintf(b, fmt, ap);
	va_end(ap);
}

const char *kp_buf_text(KpBuf *b)
{
	reserve(b, 0);
	b->data[b->size] = '\0';
	return b->data;
}

static void write_string(KpBuf *out, const char *s, int n)
{
	int k;
	kp_buf_char(out, '"');
	for (k = 0; k < n; k++) {
		unsigned char c = (unsigned char)s[k];
		if (c == '"' || c == '\\') {
			kp_buf_char(out, '\\');
			kp_buf_char(out, (char)c);
		} else if (c < 0x20 || c >= 0x7f) {
			kp_buf_printf(out, "\\u%04x", c);
		} else {
			kp_buf_char(out, (char)c);
		}
	}
	kp_buf_char(out, '"');
}

void kp_json_write(KpBuf *out, const KpJson *v)
{
	int k;
	if (!v) {
		kp_buf_str(out, "null");
		return;
	}
	switch (v->type) {
	case KP_JSON_NULL: kp_buf_str(out, "null"); break;
	case KP_JSON_FALSE: kp_buf_str(out, "false"); break;
	case KP_JSON_TRUE: kp_buf_str(out, "true"); break;
	case KP_JSON_NUMBER:
		if (v->integral) kp_buf_printf(out, "%lld", (long long)v->i);
		else kp_buf_printf(out, "%.17g", v->d);
		break;
	case KP_JSON_STRING: write_string(out, v->s, v->count); break;
	case KP_JSON_ARRAY:
		kp_buf_char(out, '[');
		for (k = 0; k < v->count; k++) {
			if (k) kp_buf_char(out, ',');
			kp_json_write(out, v->items[k]);
		}
		kp_buf_char(out, ']');
		break;
	case KP_JSON_OBJECT:
		kp_buf_char(out, '{');
		for (k = 0; k < v->count; k++) {
			if (k) kp_buf_char(out, ',');
			write_string(out, v->keys[k], (int)strlen(v->keys[k]));
			kp_buf_char(out, ':');
			kp_json_write(out, v->items[k]);
		}
		kp_buf_char(out, '}');
		break;
	}
}
