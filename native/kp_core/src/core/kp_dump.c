/* The state lines of a game (kp_game.h) in the format of tools/native_state.py: one line per record,
 * `<category> <key> <field>=<value> ...` with the fields in sorted order, so the original's dumps and Koprulu's
 * state compare line by line and hash the same way. kp_dump_hash walks the same lines and hashes their values without
 * writing them. Allocates (the dump is not part of a step). */
#include "kp_game_internal.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RESOURCE_CONTAINER 0x2000
#define MAX_FIELDS 256
#define NAME_MAX_LENGTH 255 /* a field's " name=" (Shape.length) */
#define SHAPES 64           /* the field orders a dump keeps, by their names (direct-mapped) */
#define TEXT_SPARES 16      /* the list buffers a dump keeps for its next lists */
#define NUMBER_MAX 24       /* the bytes of a 64-bit number in decimal or hex */
#define SHORT_COPY 16       /* copy_short's bytes */
#define ARENA_SLACK SHORT_COPY /* the bytes past a reservation */
#define HASH_MUL 0x9e3779b97f4a7c15ull

typedef struct {
	const char *name;        /* a literal or a static table's: it outlives the line */
	const char *external;    /* the text dump's value: a list's own bytes, or NULL for ... */
	size_t value, value_len; /* ... the bytes at value in the arena */
} Field;

/* The field names of a line in their emitted order (the same pointers: a line of the same fields from the same
 * places), with a hash of their bytes (kp_dump_hash's) and, for the text, their sorted order with each one's " name="
 * at prefix[at[k]] (prefix_size bytes, padded for copy_short). */
typedef struct {
	int count;
	const char *names[MAX_FIELDS];
	uint64_t hash;
	uint16_t order[MAX_FIELDS];
	uint16_t at[MAX_FIELDS];
	uint8_t length[MAX_FIELDS]; /* of " name=" */
	char *prefix;
	size_t prefix_size;
} Shape;

/* The fields a drawn dump leaves out (dump_lines' `drawn`): none, the image's position, the unit's minimap notices. */
enum { FILTER_NONE, FILTER_IMAGE, FILTER_UNIT };

struct Dump;

/* A growable string for list values (a,b,c). kp_dump_hash's lists hash their pieces into h instead: len is then 0 until
 * the first piece (only whether it is 0 is read, as the text's comma), s a piece's scratch. */
typedef struct {
	struct Dump *d; /* the dump whose spare buffers it takes and gives back (NULL: its own) */
	int hash;
	char *s;
	size_t len, cap;
	uint64_t h;
	int failed;
} Text;

typedef struct Dump {
	KpDumpLine emit;
	void *ud;
	int drawn; /* leave out the fields the original's interface refreshes (image x, y; the minimap's unit notices) */
	int hash;  /* kp_dump_hash: the lines' values hashed into h, no text */
	uint64_t h;
	char *arena; /* the line's values (the text dump's) */
	size_t used, cap;
	Field fields[MAX_FIELDS];
	int count;
	const char *category;
	size_t category_len;
	uint64_t category_hash;
	Text key;
	int filter;
	char *line; /* end()'s line buffer, reused */
	size_t line_cap;
	Shape *shapes[SHAPES];
	char *spares[TEXT_SPARES]; /* list buffers to reuse, with their sizes */
	size_t spare_caps[TEXT_SPARES];
	int spare_count;
	char *owned[MAX_FIELDS]; /* the line's list buffers (its fields' external values), given back at its end */
	size_t owned_caps[MAX_FIELDS];
	int owned_count;
	int failed;
} Dump;

static uint64_t mix(uint64_t h, uint64_t v)
{
	h = (h ^ v) * HASH_MUL;
	return h ^ (h >> 29);
}

/* n bytes, and their count (the machine's byte order: a hash within one process). */
static uint64_t mix_bytes(uint64_t h, const char *s, size_t n)
{
	uint64_t w;
	h = mix(h, (uint64_t)n);
	for (; n >= 8; s += 8, n -= 8) {
		memcpy(&w, s, 8);
		h = mix(h, w);
	}
	if (n > 0) {
		w = 0;
		memcpy(&w, s, n);
		h = mix(h, w);
	}
	return h;
}

/* A block of n bytes in four lanes (the machine's byte order, as mix_bytes): kp_dump_hash's in place of a CRC. */
static uint64_t mix_block(uint64_t h, const uint8_t *s, size_t n)
{
	uint64_t a = h, b = h ^ 1, c = h ^ 2, e = h ^ 3, w[4];
	for (; n >= sizeof w; s += sizeof w, n -= sizeof w) {
		memcpy(w, s, sizeof w);
		a = mix(a, w[0]);
		b = mix(b, w[1]);
		c = mix(c, w[2]);
		e = mix(e, w[3]);
	}
	return mix_bytes(mix(mix(mix(mix(a, b), c), e), (uint64_t)n), (const char *)s, n);
}

/* u in decimal at s (room for NUMBER_MAX bytes); returns the length (as "%llu"). */
static size_t format_uint(char *s, uint64_t u)
{
	uint64_t rest = u;
	size_t n = 1, k;
	if (u < 10) {
		s[0] = (char)('0' + (int)u);
		return 1;
	}
	while (rest >= 100) {
		rest /= 100;
		n += 2;
	}
	if (rest >= 10) n++;
	for (k = n; k >= 2; k -= 2) { /* two digits a division */
		unsigned pair = (unsigned)(u % 100);
		u /= 100;
		s[k - 2] = (char)('0' + pair / 10);
		s[k - 1] = (char)('0' + pair % 10);
	}
	if (k == 1) s[0] = (char)('0' + (int)u);
	return n;
}

/* v in decimal at s (room for NUMBER_MAX bytes); returns the length (as "%lld"). */
static size_t format_int(char *s, int64_t v)
{
	if (v >= 0) return format_uint(s, (uint64_t)v);
	s[0] = '-';
	return 1 + format_uint(s + 1, (uint64_t)0 - (uint64_t)v);
}

#define HEX_WIDTH_MAX 16 /* the widest zero padding format_hex takes: a 64-bit value's digits */

/* u in lowercase hex at s, at least `width` (at most HEX_WIDTH_MAX) digits zero-padded (as "%0<width>llx"); returns
 * the length. */
static size_t format_hex(char *s, uint64_t u, int width)
{
	static const char DIGITS[] = "0123456789abcdef";
	int n = width > 1 ? width : 1, k;
	while (n < HEX_WIDTH_MAX && (u >> (4 * n)) != 0) n++;
	for (k = n; k >= 2; k -= 2) { /* a byte's two digits at a time */
		unsigned byte = (unsigned)(u & 0xff);
		u >>= 8;
		s[k - 2] = DIGITS[byte >> 4];
		s[k - 1] = DIGITS[byte & 0xf];
	}
	if (k == 1) s[0] = DIGITS[u & 0xf];
	return (size_t)n;
}

/* Makes room for n more bytes (and the NUL). */
static int text_reserve(Text *t, size_t n)
{
	if (t->failed) return 0;
	if (t->len + n + 1 > t->cap) {
		size_t cap = t->cap ? t->cap * 2 : 256;
		char *p;
		while (t->len + n + 1 > cap) cap *= 2;
		p = (char *)realloc(t->s, cap);
		if (!p) {
			t->failed = 1;
			return 0;
		}
		t->s = p;
		t->cap = cap;
	}
	return 1;
}

/* Appends fmt with the conversions the dump uses (%s, %d, %u, %x and their ll forms, a zero-padded width for hex);
 * -1 for any other format (the caller then drops what was appended). */
static int format_small(Text *t, const char *fmt, va_list ap)
{
	const char *f;
	for (f = fmt; *f; f++) {
		char num[NUMBER_MAX];
		size_t n;
		int width = 0, longlong = 0;
		if (*f != '%') {
			if (!text_reserve(t, 1)) return 0;
			t->s[t->len++] = *f;
			continue;
		}
		f++;
		if (*f == '0') {
			f++;
			while (*f >= '0' && *f <= '9') width = width * 10 + (*f++ - '0');
		} else if (*f >= '1' && *f <= '9') {
			return -1;
		}
		if (f[0] == 'l' && f[1] == 'l') {
			longlong = 1;
			f += 2;
		}
		switch (*f) {
		case 's': {
			const char *v = va_arg(ap, const char *);
			if (width || longlong) return -1;
			n = strlen(v);
			if (!text_reserve(t, n)) return 0;
			memcpy(t->s + t->len, v, n);
			t->len += n;
			continue;
		}
		case 'd':
			if (width) return -1;
			n = format_int(num, longlong ? (int64_t)va_arg(ap, long long) : (int64_t)va_arg(ap, int));
			break;
		case 'u':
			if (width) return -1;
			n = format_uint(num, longlong ? (uint64_t)va_arg(ap, unsigned long long) : (uint64_t)va_arg(ap, unsigned));
			break;
		case 'x':
			if (width > HEX_WIDTH_MAX) return -1;
			n = format_hex(num, longlong ? (uint64_t)va_arg(ap, unsigned long long) : (uint64_t)va_arg(ap, unsigned), width);
			break;
		default:
			return -1;
		}
		if (!text_reserve(t, n)) return 0;
		memcpy(t->s + t->len, num, n);
		t->len += n;
	}
	if (text_reserve(t, 0)) t->s[t->len] = '\0';
	return 0;
}

/* Appends fmt to t's text (format_small, else printf sized first). */
static void text_vformat(Text *t, const char *fmt, va_list ap)
{
	va_list measure, print;
	size_t start = t->len;
	int n;
	va_copy(measure, ap);
	va_copy(print, ap);
	if (format_small(t, fmt, ap) < 0) {
		t->len = start;
		n = vsnprintf(NULL, 0, fmt, measure);
		if (n >= 0 && text_reserve(t, (size_t)n)) {
			vsnprintf(t->s + t->len, (size_t)n + 1, fmt, print);
			t->len += (size_t)n;
		}
	}
	va_end(print);
	va_end(measure);
}

static void text_add(Text *t, const char *fmt, ...)
{
	va_list ap;
	size_t before = t->len;
	if (t->failed) return;
	va_start(ap, fmt);
	if (t->hash) { /* the piece written at the scratch's start, hashed */
		t->len = 0;
		text_vformat(t, fmt, ap);
		t->h = mix_bytes(t->h, t->s, t->len);
		t->len += before;
	} else {
		text_vformat(t, fmt, ap);
	}
	va_end(ap);
}

/* A list item: v in decimal after a comma when the list has items. */
static void text_item(Text *t, int64_t v)
{
	if (t->hash) {
		t->h = mix(t->h, (uint64_t)v);
		t->len++;
		return;
	}
	if (!text_reserve(t, 1 + NUMBER_MAX)) return;
	if (t->len > 0) t->s[t->len++] = ',';
	t->len += format_int(t->s + t->len, v);
}

/* A list item "a:b", after a comma when the list has items. */
static void text_pair(Text *t, int64_t a, int64_t b)
{
	if (t->hash) {
		t->h = mix(mix(t->h, (uint64_t)a), (uint64_t)b);
		t->len++;
		return;
	}
	if (!text_reserve(t, 2 + 2 * NUMBER_MAX)) return;
	if (t->len > 0) t->s[t->len++] = ',';
	t->len += format_int(t->s + t->len, a);
	t->s[t->len++] = ':';
	t->len += format_int(t->s + t->len, b);
}

/* A list item "v0:v1:...", after a comma when the list has items. */
static void text_record(Text *t, const int64_t *v, int n)
{
	int k;
	if (t->hash) {
		for (k = 0; k < n; k++) t->h = mix(t->h, (uint64_t)v[k]);
		t->len++;
		return;
	}
	if (!text_reserve(t, 1 + (size_t)n * (1 + NUMBER_MAX))) return;
	if (t->len > 0) t->s[t->len++] = ',';
	for (k = 0; k < n; k++) {
		if (k > 0) t->s[t->len++] = ':';
		t->len += format_int(t->s + t->len, v[k]);
	}
}

/* u in hex, at least `width` digits (as "%0<width>llx"), with no separator. */
static void text_hex(Text *t, uint64_t u, int width)
{
	if (t->hash) {
		t->h = mix(t->h, u);
		t->len++;
		return;
	}
	if (!text_reserve(t, HEX_WIDTH_MAX)) return;
	t->len += format_hex(t->s + t->len, u, width);
}

static Text text_new(Dump *d)
{
	Text t;
	memset(&t, 0, sizeof t);
	t.d = d;
	t.hash = d->hash;
	if (d->spare_count > 0) {
		d->spare_count--;
		t.s = d->spares[d->spare_count];
		t.cap = d->spare_caps[d->spare_count];
	}
	return t;
}

/* A buffer back to the dump's spares (or freed). */
static void spare(Dump *d, char *s, size_t cap)
{
	if (!s) return;
	if (d->spare_count < TEXT_SPARES) {
		d->spares[d->spare_count] = s;
		d->spare_caps[d->spare_count++] = cap;
	} else {
		free(s);
	}
}

static void text_free(Text *t)
{
	if (t->d) spare(t->d, t->s, t->cap);
	else free(t->s);
	memset(t, 0, sizeof *t);
}

/* Room for n more bytes in the arena. */
static int arena_reserve(Dump *d, size_t n)
{
	if (d->failed) return 0;
	if (d->used + n + ARENA_SLACK > d->cap) {
		size_t cap = d->cap ? d->cap : 4096;
		char *p;
		while (d->used + n + ARENA_SLACK > cap) cap *= 2;
		p = (char *)realloc(d->arena, cap);
		if (!p) {
			d->failed = 1;
			return 0;
		}
		d->arena = p;
		d->cap = cap;
	}
	return 1;
}

/* A line of the category starts (its key follows: begin_id, begin_pair, begin). */
static void begin_category(Dump *d, const char *category)
{
	if (category != d->category) { /* the lines of a category come together */
		d->category = category;
		d->category_len = strlen(category);
		d->category_hash = mix_bytes(0, category, d->category_len);
		d->filter = !d->drawn ? FILTER_NONE
			: strcmp(category, "image") == 0 ? FILTER_IMAGE
			: strcmp(category, "unit") == 0 ? FILTER_UNIT : FILTER_NONE;
	}
	d->used = 0;
	d->count = 0;
	d->key.len = 0;
	if (d->hash) d->h = mix(d->h, d->category_hash);
}

/* A line whose key is the number n. */
static void begin_id(Dump *d, const char *category, int64_t n)
{
	begin_category(d, category);
	if (d->hash) {
		d->h = mix(d->h, (uint64_t)n);
	} else if (text_reserve(&d->key, NUMBER_MAX)) {
		d->key.len = format_int(d->key.s, n);
	} else {
		d->failed = 1;
	}
}

/* A line whose key is "a.b". */
static void begin_pair(Dump *d, const char *category, int64_t a, int64_t b)
{
	begin_category(d, category);
	if (d->hash) {
		d->h = mix(mix(d->h, (uint64_t)a), (uint64_t)b);
	} else if (text_reserve(&d->key, 2 * NUMBER_MAX + 1)) {
		d->key.len = format_int(d->key.s, a);
		d->key.s[d->key.len++] = '.';
		d->key.len += format_int(d->key.s + d->key.len, b);
	} else {
		d->failed = 1;
	}
}

static void begin(Dump *d, const char *category, const char *fmt, ...)
{
	va_list ap;
	begin_category(d, category);
	va_start(ap, fmt);
	text_vformat(&d->key, fmt, ap);
	va_end(ap);
	if (d->key.failed) d->failed = 1;
	if (d->hash && !d->failed) d->h = mix_bytes(d->h, d->key.s, d->key.len);
}

/* Whether the line takes field `name` (its slot then holds the name). */
static int field_take(Dump *d, const char *name)
{
	if (d->failed) return 0;
	if (d->filter == FILTER_IMAGE && (name[0] == 'x' || name[0] == 'y') && name[1] == '\0') return 0;
	/* the minimap's blink counter, which the minimap's drawing counts down (0x46e490), its color and the silence of
	 * the under-attack notices (0x446f00), which the camera and the idle loop (0x446ff0) decide */
	if (d->filter == FILTER_UNIT && (name[0] == 'l' || name[0] == 'a')
		&& (strcmp(name, "last_event_timer") == 0 || strcmp(name, "last_event_color") == 0
			|| strcmp(name, "attack_notify_timer") == 0)) {
		return 0;
	}
	if (d->count >= MAX_FIELDS) {
		d->failed = 1;
		return 0;
	}
	d->fields[d->count].name = name;
	d->fields[d->count].external = NULL;
	return 1;
}

/* A field whose value is the string s (size bytes). */
static void field_str(Dump *d, const char *name, const char *s, size_t size)
{
	Field *f;
	if (!field_take(d, name)) return;
	f = &d->fields[d->count++];
	if (d->hash) {
		d->h = mix_bytes(d->h, s, size);
		return;
	}
	if (!arena_reserve(d, size)) return;
	memcpy(d->arena + d->used, s, size);
	f->value = d->used;
	f->value_len = size;
	d->used += size;
}

/* A number field in decimal; KP_NIL leaves the field out. */
static void field_int(Dump *d, const char *name, int64_t v)
{
	Field *f;
	if (v == KP_NIL || !field_take(d, name)) return;
	f = &d->fields[d->count++];
	if (d->hash) {
		d->h = mix(d->h, (uint64_t)v);
		return;
	}
	if (!arena_reserve(d, NUMBER_MAX)) return;
	f->value = d->used;
	f->value_len = format_int(d->arena + d->used, v);
	d->used += f->value_len;
}

/* A number field in hex, at least `width` digits (as "%0<width>llx"). */
static void field_hex(Dump *d, const char *name, uint64_t u, int width)
{
	Field *f;
	if (!field_take(d, name)) return;
	f = &d->fields[d->count++];
	if (d->hash) {
		d->h = mix(d->h, u);
		return;
	}
	if (!arena_reserve(d, NUMBER_MAX)) return;
	f->value = d->used;
	f->value_len = format_hex(d->arena + d->used, u, width);
	d->used += f->value_len;
}

/* A field of n bytes by their CRC-32 in hex (8 digits); kp_dump_hash hashes the bytes themselves. */
static void field_crc(Dump *d, const char *name, const uint8_t *bytes, size_t n)
{
	if (!d->hash) {
		field_hex(d, name, kp_crc32(bytes, n), 8);
	} else if (field_take(d, name)) {
		d->count++;
		d->h = mix_block(d->h, bytes, n);
	}
}

/* A number field in decimal after a letter (as "%c%lld"). */
static void field_tagged(Dump *d, const char *name, char tag, int64_t v)
{
	char s[1 + NUMBER_MAX];
	s[0] = tag;
	field_str(d, name, s, 1 + format_int(s + 1, v));
}

/* A list field: its buffer becomes the line's (the text dump) or its hash is taken; t is emptied. */
static void field_text(Dump *d, const char *name, Text *t)
{
	Field *f;
	if (t->failed) d->failed = 1;
	if (!field_take(d, name)) {
		text_free(t);
		return;
	}
	f = &d->fields[d->count++];
	if (d->hash) {
		d->h = mix(mix(d->h, t->h), (uint64_t)t->len);
		text_free(t);
		return;
	}
	f->external = t->s ? t->s : "";
	f->value_len = t->len;
	if (t->s) {
		d->owned[d->owned_count] = t->s;
		d->owned_caps[d->owned_count++] = t->cap;
	}
	memset(t, 0, sizeof *t);
}

typedef struct {
	const char *name;
	int index;
} Named;

static int by_name(const void *a, const void *b) { return strcmp(((const Named *)a)->name, ((const Named *)b)->name); }

/* The shape of the line's fields (Shape), kept or made; NULL out of memory. */
static const Shape *shape_of(Dump *d)
{
	Named named[MAX_FIELDS];
	int n = d->count, k, slot;
	uint64_t key = (uint64_t)n;
	Shape *sh;
	if (n > 0) {
		key = mix(key, (uint64_t)(uintptr_t)d->fields[0].name);
		key = mix(key, (uint64_t)(uintptr_t)d->fields[n / 2].name);
		key = mix(key, (uint64_t)(uintptr_t)d->fields[n - 1].name);
	}
	slot = (int)(key % SHAPES);
	sh = d->shapes[slot];
	if (sh && sh->count == n) {
		for (k = 0; k < n && sh->names[k] == d->fields[k].name; k++) {
		}
		if (k == n) return sh;
	}
	if (!sh) {
		if (!(sh = (Shape *)calloc(1, sizeof *sh))) return NULL;
		d->shapes[slot] = sh;
	}
	sh->count = -1; /* until it is whole */
	sh->hash = mix(0, (uint64_t)n);
	sh->prefix_size = 0;
	for (k = 0; k < n; k++) {
		size_t len = strlen(d->fields[k].name);
		if (len > NAME_MAX_LENGTH - 2) return NULL;
		sh->names[k] = d->fields[k].name;
		sh->length[k] = (uint8_t)(len + 2);
		sh->prefix_size += len + 2;
		sh->hash = mix_bytes(sh->hash, d->fields[k].name, len);
		named[k].name = d->fields[k].name;
		named[k].index = k;
	}
	if (!d->hash) {
		char *p;
		size_t at = 0;
		free(sh->prefix);
		if (sh->prefix_size > UINT16_MAX || !(sh->prefix = (char *)malloc(sh->prefix_size + SHORT_COPY))) {
			sh->prefix = NULL;
			return NULL;
		}
		qsort(named, (size_t)n, sizeof named[0], by_name);
		for (k = 0; k < n; k++) {
			int i = named[k].index;
			sh->order[k] = (uint16_t)i;
			sh->at[k] = (uint16_t)at;
			p = sh->prefix + at;
			p[0] = ' ';
			memcpy(p + 1, d->fields[i].name, (size_t)sh->length[i] - 2);
			p[sh->length[i] - 1] = '=';
			at += sh->length[i];
		}
		memset(sh->prefix + at, 0, SHORT_COPY);
	}
	sh->count = n;
	return sh;
}

/* The line's list buffers back to the spares. */
static void give_back(Dump *d)
{
	int k;
	for (k = 0; k < d->owned_count; k++) spare(d, d->owned[k], d->owned_caps[k]);
	d->owned_count = 0;
}

/* Appends n bytes at p and returns the end. */
static char *append(char *p, const char *s, size_t n)
{
	memcpy(p, s, n);
	return p + n;
}

/* append() of a few bytes as one fixed copy: SHORT_COPY bytes readable at s and writable at p. */
static char *copy_short(char *p, const char *s, size_t n)
{
	if (n <= SHORT_COPY) memcpy(p, s, SHORT_COPY);
	else memcpy(p, s, n);
	return p + n;
}

static void end(Dump *d)
{
	const Shape *sh;
	char *p;
	size_t size;
	int k;
	if (d->failed) {
		give_back(d);
		return;
	}
	if (!(sh = shape_of(d))) {
		d->failed = 1;
		give_back(d);
		return;
	}
	if (d->hash) {
		d->h = mix(d->h, sh->hash);
		return;
	}
	size = d->category_len + 1 + d->key.len + sh->prefix_size;
	for (k = 0; k < d->count; k++) size += d->fields[k].value_len;
	if (size + 1 + SHORT_COPY > d->line_cap) {
		size_t cap = d->line_cap ? d->line_cap : 4096;
		char *line;
		while (size + 1 + SHORT_COPY > cap) cap *= 2;
		if (!(line = (char *)realloc(d->line, cap))) {
			d->failed = 1;
			give_back(d);
			return;
		}
		d->line = line;
		d->line_cap = cap;
	}
	p = append(d->line, d->category, d->category_len);
	*p++ = ' ';
	p = append(p, d->key.s, d->key.len);
	for (k = 0; k < d->count; k++) {
		const Field *f = &d->fields[sh->order[k]];
		p = copy_short(p, sh->prefix + sh->at[k], sh->length[sh->order[k]]);
		/* a list's own buffer has no slack past it; the arena's values do (ARENA_SLACK) */
		p = f->external ? append(p, f->external, f->value_len) : copy_short(p, d->arena + f->value, f->value_len);
	}
	*p = '\0';
	d->emit(d->ud, d->line, (size_t)(p - d->line));
	give_back(d);
}

/* An AI context (kp_game.h KP_AI_CONTEXT) as tools/native_state.py writes it: r<region>, g<guard>, t<town>, or 0. */
static const char *context_ref(char *buf, size_t size, int64_t context)
{
	static const char LETTERS[] = "?rgt";
	int64_t kind = context >> 16;
	if (context == 0) snprintf(buf, size, "0");
	else snprintf(buf, size, "%c%lld", kind >= 1 && kind <= 3 ? LETTERS[kind] : '?', (long long)((context & 0xffff) - 1));
	return buf;
}

/* The chain of records from head through next[] up to none. */
#define CHAIN(t, head, next, none, limit) \
	do { \
		int64_t c_ = (head); \
		int guard_ = 0; \
		while (c_ != (none) && c_ >= 0 && c_ < (limit) && guard_++ <= (limit)) { \
			text_item(&(t), c_); \
			c_ = (next)[c_]; \
		} \
	} while (0)

static void unit_line(Dump *d, KpGame *g, int64_t n, const char *list)
{
	static const char *const UNION_WORDS[7] = { "union_0xc0", "union_0xc4", "union_0xc8", "union_0xcc", "union_0xd0",
		"union_0xd4", "union_0xd8" };
	KpUnits *u = &g->u;
	int64_t unions = u->union_units[n];
	int k;
	begin_id(d, "unit", n);
#define UNIT_FIELD(name) \
	if (strcmp(#name, "prev") != 0 && strcmp(#name, "next") != 0 && strcmp(#name, "union_units") != 0 \
		&& strncmp(#name, "union_0x", 8) != 0 && strcmp(#name, "ai") != 0) { \
		field_int(d, #name, u->name[n]); \
	}
	KP_UNIT_FIELDS(UNIT_FIELD)
#undef UNIT_FIELD
	{
		int64_t words[7];
		words[0] = u->union_0xc0[n];
		words[1] = u->union_0xc4[n];
		words[2] = u->union_0xc8[n];
		words[3] = u->union_0xcc[n];
		words[4] = u->union_0xd0[n];
		words[5] = u->union_0xd4[n];
		words[6] = u->union_0xd8[n];
		for (k = 0; k < 7; k++) {
			/* a resource's +0xd8 is in the category "ai_resource", so the goldens of the units stay */
			if (k == 6 && (KP_DAT(g, units, special_ability_flags, u->type[n]) & RESOURCE_CONTAINER) != 0) continue;
			if ((unions & ((int64_t)1 << k)) != 0) field_tagged(d, UNION_WORDS[k], 'u', words[k]);
			/* a Pylon's word 4 is its Psi field sprite (+0xd0) */
			else if (k == 4 && u->type[n] == KP_TYPE_PYLON && words[k] != 0) field_tagged(d, UNION_WORDS[k], 's', words[k] - 1);
			/* a Ghost's +0xd0 holds the lone thingy of its nuke dot (kp_nuke.c), index + 1 */
			else if (k == 4 && kp_ghost_type(u->type[n]) && words[k] != 0) field_tagged(d, UNION_WORDS[k], 't', words[k] - 1);
			else field_int(d, UNION_WORDS[k], words[k]);
		}
	}
	if (u->ai[n] != 0) { /* the record's pool letter and index (tools/native_state.py AI_POOLS) */
		static const char AI_POOL_LETTERS[] = "?gwbm";
		int64_t kind = kp_ai_ref_kind(u->ai[n]);
		field_tagged(d, "ai", kind >= 1 && kind <= 4 ? AI_POOL_LETTERS[kind] : '?', kp_ai_ref_index(u->ai[n]));
	} else {
		field_int(d, "ai", 0);
	}
	field_str(d, "list", list, strlen(list));
	end(d);
}

/* Bullet b (tools/native_state.py BULLET): its flingy part from the unit fields, the rest from KpBullets. */
static void bullet_line(Dump *d, KpGame *g, int64_t b)
{
	KpUnits *u = &g->u;
	KpBullets *bl = &g->bullets;
	int64_t f = KP_UNITS + b;
	begin_id(d, "bullet", b);
#define BULLET_FLINGY(name) field_int(d, #name, u->name[f]);
	BULLET_FLINGY(move_x) BULLET_FLINGY(move_y) BULLET_FLINGY(move_unit) BULLET_FLINGY(waypoint_x)
	BULLET_FLINGY(waypoint_y) BULLET_FLINGY(target_waypoint_x) BULLET_FLINGY(target_waypoint_y)
	BULLET_FLINGY(movement_flags) BULLET_FLINGY(heading) BULLET_FLINGY(turn_speed) BULLET_FLINGY(velocity_direction)
	BULLET_FLINGY(flingy) BULLET_FLINGY(flingy_0x26) BULLET_FLINGY(movement_type) BULLET_FLINGY(x) BULLET_FLINGY(y)
	BULLET_FLINGY(halt_x) BULLET_FLINGY(halt_y) BULLET_FLINGY(top_speed) BULLET_FLINGY(speed1) BULLET_FLINGY(speed2)
	BULLET_FLINGY(speed_x) BULLET_FLINGY(speed_y) BULLET_FLINGY(acceleration) BULLET_FLINGY(heading2)
	BULLET_FLINGY(velocity_direction2) BULLET_FLINGY(sprite) BULLET_FLINGY(owner) BULLET_FLINGY(order_state)
	BULLET_FLINGY(order_signal) BULLET_FLINGY(order_type) BULLET_FLINGY(order_timer) BULLET_FLINGY(ground_cooldown)
	BULLET_FLINGY(air_cooldown) BULLET_FLINGY(spell_cooldown) BULLET_FLINGY(order_x) BULLET_FLINGY(order_y)
	BULLET_FLINGY(order_unit)
#undef BULLET_FLINGY
	field_int(d, "state", u->order[f]);
#define BULLET_FIELD(name) \
	if (strcmp(#name, "prev") != 0 && strcmp(#name, "next") != 0) field_int(d, #name, bl->name[b]);
	KP_BULLET_FIELDS(BULLET_FIELD)
#undef BULLET_FIELD
	end(d);
}

/* The header, points and region ids of path record k (tools/native_state.py PATH). */
static void path_line(Dump *d, KpGame *g, int64_t n, int64_t k)
{
	Text pts = text_new(d), regions = text_new(d);
	int64_t points = kp_path_s8(g, k, KP_PATH_POINTS), count = kp_path_s8(g, k, KP_PATH_REGIONS), i, x, y, at;
	begin_id(d, "path", n);
	field_int(d, "record", k);
	field_int(d, "frame", kp_path_u32(g, k, KP_PATH_FRAME));
	field_int(d, "blocker", kp_path_u32(g, k, KP_PATH_BLOCKER));
	field_int(d, "speed", kp_path_u32(g, k, KP_PATH_SPEED));
	field_int(d, "flags", kp_path_u8(g, k, KP_PATH_FLAGS));
	field_int(d, "retry", kp_path_u8(g, k, KP_PATH_RETRY));
	field_int(d, "slide", kp_path_u8(g, k, KP_PATH_SLIDE));
	field_int(d, "full_regions", kp_path_u8(g, k, KP_PATH_FULL_REGIONS));
	field_int(d, "regions", count);
	field_int(d, "region_index", kp_path_s8(g, k, KP_PATH_REGION_INDEX));
	field_int(d, "points", points);
	field_int(d, "point_index", kp_path_s8(g, k, KP_PATH_POINT_INDEX));
	kp_path_point(g, k, KP_PATH_SRC, &x, &y);
	field_int(d, "src_x", x);
	field_int(d, "src_y", y);
	kp_path_point(g, k, KP_PATH_NEXT, &x, &y);
	field_int(d, "next_x", x);
	field_int(d, "next_y", y);
	kp_path_point(g, k, KP_PATH_DEST, &x, &y);
	field_int(d, "dest_x", x);
	field_int(d, "dest_y", y);
	for (i = 0; i <= points - 1; i++) {
		kp_path_point(g, k, KP_PATH_PAYLOAD + 4 * i, &x, &y);
		text_pair(&pts, x, y);
	}
	at = KP_PATH_PAYLOAD + 4 * (points > 0 ? points : 0);
	for (i = 0; i <= count - 1; i++) text_item(&regions, kp_path_u16(g, k, at + 2 * i));
	field_text(d, "point_list", &pts);
	field_text(d, "region_list", &regions);
	end(d);
}

/* The field names of a player's control groups (a static table: a field's name outlives its line). */
static const char *const GROUP_NAMES[] = { "group0", "group1", "group2", "group3", "group4", "group5", "group6", "group7",
	"group8", "group9", "group10", "group11", "group12", "group13", "group14", "group15", "group16", "group17" };
typedef char group_names_check[sizeof GROUP_NAMES / sizeof GROUP_NAMES[0] == KP_GROUPS ? 1 : -1];

static void list12(Text *t, const int64_t *v)
{
	int k;
	for (k = 0; k < 12; k++) text_item(t, v[k]);
}

/* The CRC-32 of values as the original's u32 array (little-endian). */
static uint32_t crc_u32s(const int64_t *v, int n)
{
	uint8_t b[4096];
	uint32_t crc = 0;
	int k = 0;
	while (k < n) {
		int m = 0;
		for (; k < n && m + 4 <= (int)sizeof b; k++, m += 4) {
			uint32_t x = (uint32_t)v[k];
			b[m] = (uint8_t)x;
			b[m + 1] = (uint8_t)(x >> 8);
			b[m + 2] = (uint8_t)(x >> 16);
			b[m + 3] = (uint8_t)(x >> 24);
		}
		crc = kp_crc32_update(crc, b, (size_t)m);
	}
	return crc;
}

static void dump_lines(Dump *d, KpGame *g)
{
	static const char *const LIST_NAMES[KP_LIST_COUNT] = { "active", "pending", "free", "hidden", "special" };
	static const int UNIT_LISTS[4] = { KP_LIST_ACTIVE, KP_LIST_HIDDEN, KP_LIST_SPECIAL, KP_LIST_PENDING };
	KpUnits *u = &g->u;
	KpImages *w = g->w;
	KpOrders *o = &g->orders;
	KpTerrain *tr = &g->terrain;
	int k, p, r, l;
	int64_t n, i;
	begin(d, "global", "0");
	field_int(d, "rng", g->rng->state);
	field_int(d, "units_allocated", kp_scalar(g, KP_UNITS_ALLOCATED));
	field_int(d, "orders_allocated", o->allocated);
	field_int(d, "spatial_count", g->spatial.count);
	field_int(d, "active_tail", g->lists[KP_LIST_ACTIVE].tail);
	field_int(d, "pending_tail", g->lists[KP_LIST_PENDING].tail);
	field_int(d, "free_tail", g->lists[KP_LIST_FREE].tail);
	field_int(d, "hidden_tail", g->lists[KP_LIST_HIDDEN].tail);
	field_int(d, "special_tail", g->lists[KP_LIST_SPECIAL].tail);
	field_int(d, "cloaked_head", kp_scalar(g, KP_CLOAKED_HEAD));
	end(d);
	{ /* the RNG's draw counters by source (0x673940) and its seed variable (0x673934), a category of their own so
	   * the goldens of the others stay */
		Text sources = text_new(d);
		for (k = 0; k < KP_RNG_SOURCES; k++) {
			if (g->rng->counters[k] != 0) text_pair(&sources, k, (int64_t)g->rng->counters[k]);
		}
		begin(d, "rng_sources", "0");
		field_text(d, "counts", &sources);
		field_int(d, "seed", g->rng->initial_seed);
		end(d);
	}
	for (k = 0; k < KP_LIST_COUNT; k++) {
		Text t = text_new(d);
		CHAIN(t, g->lists[k].head, u->next, 0, KP_UNITS + 1);
		begin(d, "list", "%s", LIST_NAMES[k]);
		field_text(d, "units", &t);
		end(d);
	}
	for (p = 0; p < KP_PLAYERS; p++) {
		Text t = text_new(d);
		CHAIN(t, g->player_units[p], u->player_next, 0, KP_UNITS + 1);
		begin(d, "list", "player%d", p);
		field_text(d, "units", &t);
		end(d);
	}
	{
		KpBullets *bl = &g->bullets;
		Text active = text_new(d), free_list = text_new(d);
		CHAIN(active, bl->active.head, bl->next, 0, KP_BULLETS + 1);
		CHAIN(free_list, bl->free.head, bl->next, 0, KP_BULLETS + 1);
		begin(d, "bullets", "0");
		field_int(d, "allocated", bl->allocated);
		field_text(d, "active", &active);
		field_int(d, "active_tail", bl->active.tail);
		field_text(d, "free", &free_list);
		field_int(d, "free_tail", bl->free.tail);
		end(d);
	}
	{
		/* the provider list of psi power (0x665b54, next +0xfc) in a category of its own */
		Text providers = text_new(d);
		CHAIN(providers, g->power_head, u->rally_unit, 0, KP_UNITS + 1);
		begin(d, "power", "0");
		field_text(d, "providers", &providers);
		end(d);
	}
	/* units, then their order queues */
	for (l = 0; l < 4; l++) {
		for (n = g->lists[UNIT_LISTS[l]].head; n > 0 && n <= KP_UNITS; n = u->next[n]) unit_line(d, g, n, LIST_NAMES[UNIT_LISTS[l]]);
	}
	for (n = g->bullets.active.head; n > 0 && n <= KP_BULLETS; n = g->bullets.next[n]) bullet_line(d, g, n);
	for (l = 0; l < 4; l++) {
		for (n = g->lists[UNIT_LISTS[l]].head; n > 0 && n <= KP_UNITS; n = u->next[n]) {
			int64_t node = u->queue_head[n], q = 0;
			while (node != KP_NONE && node >= 0 && node < KP_ORDER_NODES) {
				begin_pair(d, "order", n, q++);
				field_int(d, "node", node);
				field_int(d, "order", o->order[node]);
				field_int(d, "type", o->type[node]);
				field_int(d, "x", o->x[node]);
				field_int(d, "y", o->y[node]);
				field_int(d, "unit", o->unit[node]);
				end(d);
				node = o->next[node];
			}
		}
	}
	{
		Text t = text_new(d);
		CHAIN(t, o->free.head, o->next, KP_NONE, KP_ORDER_NODES);
		begin(d, "order_free", "0");
		field_text(d, "nodes", &t);
		end(d);
	}
	for (l = 0; l < 4; l++) {
		for (n = g->lists[UNIT_LISTS[l]].head; n > 0 && n <= KP_UNITS; n = u->next[n]) {
			if (u->path[n] != 0) path_line(d, g, n, u->path[n]);
		}
	}
	{
		Text t = text_new(d);
		int64_t pk = kp_scalar(g, KP_PATH_FREE_HEAD);
		int guard = 0;
		while (pk != 0 && pk != KP_NIL && guard++ <= KP_PATH_RECORDS) {
			text_item(&t, pk);
			pk = kp_path_u32(g, pk, 0);
		}
		begin(d, "path_free", "0");
		field_text(d, "records", &t);
		end(d);
	}
	{
		Text t = text_new(d);
		for (i = 0; i < KP_DENSITY_CELLS; i++) {
			if (g->collision.density[i] == 0) continue;
			text_pair(&t, i, g->collision.density[i]);
		}
		begin(d, "density", "0");
		field_text(d, "cells", &t);
		end(d);
	}
	{
		/* sprites by row, then the images of all of them */
		int64_t *images = (int64_t *)malloc(sizeof *images * KP_IMAGES);
		int64_t count = 0;
		if (!images) {
			d->failed = 1;
			return;
		}
		for (r = 0; r < KP_ROWS; r++) {
			int32_t s = w->rows[r].head;
			int guard = 0;
			while (s != KP_NONE && s >= 0 && s < KP_SPRITES && guard++ <= KP_SPRITES) {
				Text ims = text_new(d);
				int32_t im = w->spr.images[s].head;
				int iguard = 0;
				while (im != KP_NONE && im >= 0 && im < KP_IMAGES && iguard++ <= KP_IMAGES) {
					if (im < KP_IMG_GHOSTS_FIRST) { /* the placement preview's are left out (tools/native_state.py) */
						text_item(&ims, im);
						if (count < KP_IMAGES) images[count++] = im;
					}
					im = w->img.next[im];
				}
				begin_id(d, "sprite", s);
				field_int(d, "id", w->spr.id[s]);
				field_int(d, "owner", w->spr.owner[s]);
				field_int(d, "selection", w->spr.selection[s]);
				field_int(d, "visibility", w->spr.visibility[s]);
				field_int(d, "elevation", w->spr.elevation[s]);
				field_int(d, "flags", w->spr.flags[s]);
				field_int(d, "sel_timer", w->spr.sel_timer[s]);
				field_int(d, "index", w->spr.index[s]);
				field_int(d, "width", w->spr.width[s]);
				field_int(d, "height", w->spr.height[s]);
				field_int(d, "x", kp_s16v(w->spr.x[s]));
				field_int(d, "y", kp_s16v(w->spr.y[s]));
				field_int(d, "row", r);
				field_int(d, "main", w->spr.main[s]);
				field_text(d, "images", &ims);
				end(d);
				s = w->spr.next[s];
			}
		}
		for (i = 0; i < count; i++) {
			int32_t im = (int32_t)images[i];
			begin_id(d, "image", im);
			field_int(d, "id", w->img.id[im]);
			field_int(d, "method", w->img.method[im]);
			field_int(d, "dir", w->img.dir[im]);
			field_int(d, "flags", w->img.flags[im]);
			field_int(d, "xoff", kp_s8((uint32_t)w->img.xoff[im]));
			field_int(d, "yoff", kp_s8((uint32_t)w->img.yoff[im]));
			field_int(d, "header", w->img.header[im]);
			field_int(d, "pc", w->img.pc[im]);
			field_int(d, "ret", w->img.ret[im]);
			field_int(d, "anim", w->img.anim[im]);
			field_int(d, "wait", w->img.wait[im]);
			field_int(d, "frameset", w->img.frameset[im]);
			field_int(d, "frame", w->img.frame[im]);
			field_int(d, "x", kp_s16v(w->img.x[im]));
			field_int(d, "y", kp_s16v(w->img.y[im]));
			field_int(d, "sprite", w->img.sprite[im]);
			end(d);
		}
		free(images);
	}
	{
		static const char *const NAMES[4] = { "all", "completed", "kills", "deaths" };
		const int64_t *counts[4];
		counts[0] = g->count_all;
		counts[1] = g->count_completed;
		counts[2] = g->count_kills;
		counts[3] = g->count_deaths;
		for (k = 0; k < 4; k++) {
			int t;
			for (t = 0; t < KP_UNIT_TYPES; t++) {
				int any = 0;
				for (p = 0; p < KP_PLAYERS; p++) {
					if (counts[k][t * 12 + p] != 0) any = 1;
				}
				if (any) {
					Text row = text_new(d);
					for (p = 0; p < KP_PLAYERS; p++) text_item(&row, counts[k][t * 12 + p]);
					begin(d, "counter", "%s.%d", NAMES[k], t);
					field_text(d, "players", &row);
					end(d);
				}
			}
		}
	}
	for (k = 0; k < KP_PSTAT_ARRAYS; k++) {
		Text row = text_new(d);
		for (p = 0; p < KP_PLAYERS; p++) text_item(&row, kp_u32v(g->pstat[k][p]));
		begin(d, "pstat", "0x%x", 0x5290c4 + 0x30 * k);
		field_text(d, "players", &row);
		end(d);
	}
	for (k = 0; k < 2; k++) {
		const int64_t *unit = k == 0 ? g->spatial.x_unit : g->spatial.y_unit, *at = k == 0 ? g->spatial.x_at : g->spatial.y_at;
		Text e = text_new(d);
		for (i = 0; i < g->spatial.count && i < KP_SPATIAL_CAPACITY; i++) {
			text_pair(&e, unit[i], at[i]);
		}
		begin(d, "spatial", "%s", k == 0 ? "x" : "y");
		field_text(d, "entries", &e);
		end(d);
	}
	for (p = 0; p < KP_PLAYERS; p++) {
		Text a = text_new(d);
		int q;
		for (q = 0; q < KP_PLAYERS; q++) text_hex(&a, (uint64_t)(g->players.alliance[p][q]), 2);
		begin_id(d, "player", p);
		field_int(d, "controller", g->players.controller[p]);
		field_int(d, "force", g->players.force[p]);
		field_text(d, "alliance", &a);
		field_int(d, "shared_vision", g->players.shared_vision[p]);
		end(d);
	}
	/* a category of its own, so the goldens of the earlier ones stay */
	for (p = 0; p < KP_PLAYERS; p++) {
		begin_id(d, "resources", p);
		field_int(d, "minerals", g->players.minerals[p]);
		field_int(d, "gas", g->players.gas[p]);
		field_int(d, "cumulative_minerals", g->players.cumulative_minerals[p]);
		field_int(d, "cumulative_gas", g->players.cumulative_gas[p]);
		end(d);
	}
	/* the techs, upgrades and the AI budget, a category of their own (plan phase 6) */
	for (p = 0; p < KP_PLAYERS; p++) {
		const KpPlayers *pl = &g->players;
		Text units = text_new(d), avail = text_new(d), done = text_new(d), level = text_new(d), max = text_new(d), rp = text_new(d);
		Text up = text_new(d);
		for (k = 0; k < KP_UNIT_TYPES; k++) text_hex(&units, (uint64_t)(pl->unit_available[p][k]), 2);
		for (k = 0; k < KP_TECHS; k++) {
			text_hex(&avail, (uint64_t)(pl->tech_available[p][k]), 2);
			text_hex(&done, (uint64_t)(pl->tech_researched[p][k]), 2);
		}
		for (k = 0; k < KP_UPGRADES; k++) {
			text_hex(&level, (uint64_t)(pl->upgrades[p][k]), 2);
			text_hex(&max, (uint64_t)(pl->upgrade_max[p][k]), 2);
		}
		for (k = 0; k < KP_TECH_PROGRESS_BYTES; k++) text_hex(&rp, (uint64_t)(pl->research_progress[p][k]), 2);
		for (k = 0; k < KP_UPGRADE_PROGRESS_BYTES; k++) text_hex(&up, (uint64_t)(pl->upgrade_progress[p][k]), 2);
		begin_id(d, "economy", p);
		field_text(d, "unit_available", &units);
		field_text(d, "tech_available", &avail);
		field_text(d, "tech_researched", &done);
		field_text(d, "upgrade_level", &level);
		field_text(d, "upgrade_max", &max);
		field_text(d, "research_progress", &rp);
		field_text(d, "upgrade_progress", &up);
		if (p < KP_AI_PLAYERS) {
			field_int(d, "ai_minerals", kp_u32v(g->pai.minerals[p]));
			field_int(d, "ai_gas", kp_u32v(g->pai.gas[p]));
		}
		end(d);
	}
	{
		Text buckets = text_new(d);
		int64_t v;
		for (k = 0; k < KP_RECEDE_BUCKETS; k++) text_hex(&buckets, (uint64_t)(tr->recede_buckets[k]), 2);
		begin(d, "step", "0");
		v = kp_scalar(g, KP_TILE_COUNTDOWN);
		field_int(d, "tile_countdown", v == KP_NIL ? KP_NIL : kp_u32v(v));
		field_int(d, "order_spread", kp_scalar(g, KP_ORDER_SPREAD));
		field_int(d, "secondary_spread", kp_scalar(g, KP_SECONDARY_SPREAD));
		field_int(d, "recede_timer", tr->recede_timer);
		field_int(d, "recede_free", tr->recede_free);
		field_int(d, "recede_retry", tr->recede_retry);
		field_text(d, "recede_buckets", &buckets);
		field_int(d, "ai_terrain_changed", g->ai.terrain_changed);
		field_int(d, "ai_period", kp_u32v(g->ai.period));
		field_int(d, "ai_cycle", g->ai.cycle);
		field_int(d, "ai_rounds", g->ai.rounds);
		field_int(d, "ai_region_player", g->ai.region_player);
		field_int(d, "ai_region_cursor", g->ai.region_cursor);
		field_int(d, "ai_spending_player", g->ai.spending_player);
		field_int(d, "ai_town_player", g->ai.town_player);
		field_int(d, "hit_slot", kp_scalar(g, KP_HIT_SLOT));
		v = kp_scalar(g, KP_POWER_DIRTY);
		field_int(d, "power_dirty", v != KP_NIL && v != 0 ? 1 : 0);
		field_int(d, "elapsed", kp_scalar(g, KP_ELAPSED));
		field_int(d, "clock_second", g->trig.second);
		field_int(d, "clock_pass", g->trig.pass);
		field_int(d, "clock_result", g->trig.result);
		field_int(d, "clock_leaderboard", g->trig.leaderboard);
		field_int(d, "turn", kp_scalar(g, KP_TURN));
		end(d);
	}
	/* The trigger engine (tools/native_state.py "trigger", "triggers", "location"). */
	for (p = 0; p < KP_TRIGGER_PLAYERS; p++) {
		int64_t copy;
		int k2 = 0;
		for (copy = g->trig.head[p]; copy >= 0; copy = g->trig.next[copy], k2++) {
			const uint8_t *rec = g->trig.records + (size_t)copy * KP_TRIGGER_RECORD;
			begin_pair(d, "trigger", p, k2);
			field_int(d, "flags", kp_le_u32(rec + KP_TRIG_EXECUTION_FLAGS));
			field_int(d, "continuation", rec[KP_TRIG_CONTINUATION]);
			field_crc(d, "record", rec, KP_TRIGGER_RECORD);
			end(d);
		}
	}
	{
		const KpTriggers *tg = &g->trig;
		Text waits = text_new(d), remaining = text_new(d), pending = text_new(d), participants = text_new(d);
		Text objectives = text_new(d), switches = text_new(d), results = text_new(d), entries = text_new(d);
		Text next = text_new(d);
		for (p = 0; p < KP_TRIGGER_PLAYERS; p++) {
			text_item(&waits, tg->wait_active[p]);
			text_item(&remaining, tg->wait_remaining[p]);
			text_item(&pending, tg->pending[p]);
			text_item(&participants, tg->participants[p]);
			text_item(&objectives, tg->objectives[p]);
			text_item(&results, g->results[p]);
			text_item(&entries, kp_u32v(tg->lb_entries[p][0]));
			text_item(&entries, kp_u32v(tg->lb_entries[p][1]));
		}
		for (k = 0; k < KP_SWITCH_WORDS; k++) text_item(&switches, tg->switches[k]);
		for (k = 0; k < KP_NEXT_SCENARIO; k++) text_hex(&next, (unsigned)(tg->next_scenario[k]), 2);
		begin(d, "triggers", "0");
		field_int(d, "player", tg->player);
		field_int(d, "paused", tg->paused);
		field_int(d, "result_dialog", tg->result_dialog);
		field_int(d, "game_paused", kp_scalar(g, KP_PAUSED));
		field_int(d, "speed", kp_scalar(g, KP_SPEED));
		field_text(d, "waits", &waits);
		field_text(d, "remaining", &remaining);
		field_text(d, "pending", &pending);
		field_text(d, "participants", &participants);
		field_text(d, "objectives", &objectives);
		field_text(d, "switches", &switches);
		field_text(d, "results", &results);
		field_int(d, "countdown", tg->countdown);
		field_int(d, "countdown_paused", tg->countdown_paused);
		field_int(d, "quiet", tg->quiet);
		field_text(d, "next_scenario", &next);
		field_int(d, "lb_computers", tg->lb_computers);
		field_int(d, "lb_action", tg->lb_action);
		field_int(d, "lb_location", tg->lb_location);
		field_int(d, "lb_type", tg->lb_type);
		field_int(d, "lb_goal", tg->lb_goal);
		field_int(d, "lb_label", tg->lb_label);
		field_text(d, "lb_entries", &entries);
		end(d);
		begin(d, "trigger_counts", "0");
		field_int(d, "key", tg->count_key);
		field_int(d, "location", tg->count_location);
		field_hex(d, "all", crc_u32s(tg->count_all, KP_UNIT_TYPES * KP_PLAYERS), 8);
		field_hex(d, "completed", crc_u32s(tg->count_completed, KP_UNIT_TYPES * KP_PLAYERS), 8);
		field_hex(d, "pstat", crc_u32s(tg->count_pstat[0], KP_PSTAT_ARRAYS * KP_PLAYERS), 8);
		end(d);
		for (k = 0; k < KP_LOCATIONS; k++) {
			const int64_t *loc = tg->locations[k];
			int f, any = 0;
			for (f = 0; f < KP_LOC_FIELDS; f++) {
				if (loc[f] != 0) any = 1;
			}
			if (!any) continue;
			begin_id(d, "location", k + 1);
			field_int(d, "left", loc[KP_LOC_LEFT]);
			field_int(d, "top", loc[KP_LOC_TOP]);
			field_int(d, "right", loc[KP_LOC_RIGHT]);
			field_int(d, "bottom", loc[KP_LOC_BOTTOM]);
			field_int(d, "name", loc[KP_LOC_NAME]);
			field_int(d, "elevation", loc[KP_LOC_ELEVATION]);
			end(d);
		}
	}
	{
		Text t = text_new(d);
		list12(&t, g->local_selection);
		begin(d, "selection", "local");
		field_text(d, "units", &t);
		end(d);
	}
	for (p = 0; p < KP_SELECTION_PLAYERS; p++) {
		Text t = text_new(d), stamps = text_new(d);
		begin_id(d, "selection", p);
		list12(&t, g->selections[p]);
		field_text(d, "units", &t);
		for (k = 0; k < KP_RECENT; k++) text_item(&stamps, g->recent_stamps[p][k]);
		field_text(d, "recent", &stamps);
		for (k = 0; k < KP_GROUPS; k++) {
			int any = 0, j;
			for (j = 0; j < 12; j++) {
				if (g->groups[p][k][j] != 0) any = 1;
			}
			if (any) {
				Text group = text_new(d);
				list12(&group, g->groups[p][k]);
				field_text(d, GROUP_NAMES[k], &group);
			}
		}
		end(d);
	}
	for (p = 0; p < 8; p++) {
		KpGuards *gd = &g->guards;
		Text recs = text_new(d);
		int64_t c = gd->heads[p];
		int guard = 0;
		while (c != -1 && c >= 0 && c < KP_GUARDS && guard++ <= KP_GUARDS) {
			int64_t record[9];
			record[0] = c;
			record[1] = gd->kind[c];
			record[2] = gd->unit[c];
			record[3] = gd->type[c];
			record[4] = gd->x1[c];
			record[5] = gd->y1[c];
			record[6] = gd->x2[c];
			record[7] = gd->y2[c];
			record[8] = gd->deaths[c];
			text_record(&recs, record, 9);
			c = gd->next[c];
		}
		begin_id(d, "guard", p);
		field_text(d, "records", &recs);
		end(d);
	}
	begin(d, "guard", "free");
	field_int(d, "head", g->guards.free);
	end(d);
	/* The guards' replacement request times that are set (tools/native_state.py guard_time): "record:time". */
	for (p = 0; p < 8; p++) {
		Text times = text_new(d);
		int64_t c = g->guards.heads[p], seen = 0;
		while (c >= 0 && c < KP_GUARDS && seen++ <= KP_GUARDS) { /* bounded as CHAIN: a broken list still ends */
			if (g->guards.time[c] != 0) text_pair(&times, c, g->guards.time[c]);
			c = g->guards.next[c];
		}
		begin_id(d, "guard_time", p);
		field_text(d, "times", &times);
		end(d);
	}
	/* The AI region records (tools/native_state.py AI_REGION): the ones that differ from their start (ID r, owner p,
	 * the rest 0). */
	for (p = 0; p < KP_AI_PLAYERS; p++) {
		for (i = 0; i < g->regions.count; i++) {
			const KpAiRegions *a = &g->air;
			int changed = 0;
#define AI_CHANGED(name) if (strcmp(#name, "military") != 0 && a->name[p][i] != 0) changed = 1;
			KP_AI_REGION_FIELDS(AI_CHANGED)
#undef AI_CHANGED
			if (!changed) continue;
			begin_pair(d, "ai_region", p, i);
#define AI_FIELD(name) if (strcmp(#name, "military") != 0) field_int(d, #name, a->name[p][i]);
			KP_AI_REGION_FIELDS(AI_FIELD)
#undef AI_FIELD
			end(d);
		}
	}
	for (p = 0; p < KP_AI_PLAYERS; p++) {
		Text marks = text_new(d);
		for (i = 0; i < g->regions.count; i++) text_hex(&marks, (unsigned)(g->ai_reach[p][i]), 2);
		begin_id(d, "ai_reach", p);
		field_int(d, "time", g->ai_reach_time[p]);
		field_int(d, "source", g->ai_reach_source[p]);
		field_text(d, "marks", &marks);
		end(d);
	}
	for (p = 0; p < KP_AI_PLAYERS; p++) {
		begin_id(d, "ai_player", p);
#define PLAYER_AI_FIELD(name) field_int(d, #name, g->pai.name[p]);
		KP_PLAYER_AI_FIELDS(PLAYER_AI_FIELD)
#undef PLAYER_AI_FIELD
		end(d);
	}
	/* The PlayerAi lists (tools/native_state.py AI_LISTS): the demand and supply, the spending heap's entries
	 * "priority:kind:id:context", the attack roster, the defense lists and the build limits as hex WORDs (bytes). */
	for (p = 0; p < KP_AI_PLAYERS; p++) {
		const KpPlayerAi *pai = &g->pai;
		Text heap = text_new(d), roster = text_new(d), defense = text_new(d), limits = text_new(d), area = text_new(d);
		int64_t e, li;
		for (e = 0; e < pai->heap_count[p] && e < KP_AI_HEAP; e++) {
			char ref[24];
			text_add(&heap, "%s%lld:%lld:%lld:%s", e ? "," : "", (long long)pai->heap_priority[p][e],
				(long long)pai->heap_kind[p][e], (long long)pai->heap_id[p][e],
				context_ref(ref, sizeof ref, pai->heap_context[p][e]));
		}
		for (e = 0; e < KP_AI_ROSTER; e++) text_hex(&roster, (uint64_t)(pai->roster[p][e]), 4);
		for (li = 0; li < KP_AI_DEFENSE_LISTS; li++) {
			for (e = 0; e < KP_AI_DEFENSE; e++) text_hex(&defense, (uint64_t)(pai->defense[p][li][e]), 4);
		}
		for (e = 0; e < KP_UNIT_TYPES; e++) text_hex(&limits, (uint64_t)(pai->limits[p][e]), 2);
		for (e = 0; e < 4; e++) text_item(&area, pai->area[p][e]);
		begin_id(d, "ai_lists", p);
		field_int(d, "demand_minerals", pai->demand_minerals[p]);
		field_int(d, "demand_gas", pai->demand_gas[p]);
		field_int(d, "demand_supply", pai->demand_supply[p]);
		field_int(d, "supply", pai->supply[p]);
		field_text(d, "heap", &heap);
		field_text(d, "roster", &roster);
		field_text(d, "defense", &defense);
		field_text(d, "limits", &limits);
		field_text(d, "area", &area);
		field_int(d, "medic", pai->medic[p]);
		end(d);
	}
	/* The towns and their workers and buildings (tools/native_state.py ai_towns): the records that differ from their
	 * start (free, linked to their neighbours, the rest 0, references -1), links and references as indexes. */
	{
		const KpTowns *t = &g->towns;
		const KpWorkerAis *wk = &g->workers;
		const KpBuildingAis *b = &g->buildings;
		for (i = 0; i < KP_TOWNS; i++) {
			Text gas, requests;
			int64_t e, last = -1;
			int initial = t->next[i] == (i + 1 < KP_TOWNS ? i + 1 : -1) && t->prev[i] == (i > 0 ? i - 1 : -1)
				&& t->workers[i] == -1 && t->buildings[i] == -1;
#define TOWN_ZERO(name) if (t->name[i] != 0) initial = 0;
			KP_TOWN_DATA_FIELDS(TOWN_ZERO)
#undef TOWN_ZERO
			for (e = 0; e < KP_TOWN_GAS; e++) {
				if (t->gas[i][e] != 0) initial = 0;
			}
			for (e = 0; e < KP_TOWN_REQUESTS; e++) {
				if (t->requests[i][e] != 0) last = e;
			}
			if (initial && last < 0) continue;
			gas = text_new(d);
			requests = text_new(d);
			for (e = 0; e < KP_TOWN_GAS; e++) text_item(&gas, t->gas[i][e]);
			for (e = 0; e <= last; e++) text_hex(&requests, (uint64_t)(t->requests[i][e]), 8);
			begin_id(d, "ai_town", i);
#define TOWN_FIELD(name) field_int(d, #name, t->name[i]);
			KP_TOWN_FIELDS(TOWN_FIELD)
#undef TOWN_FIELD
			field_text(d, "gas", &gas);
			field_text(d, "requests", &requests);
			end(d);
		}
		{
			Text heads = text_new(d);
			for (p = 0; p < KP_AI_PLAYERS; p++) text_item(&heads, t->heads[p]);
			begin(d, "ai_town", "lists");
			field_text(d, "heads", &heads);
			field_int(d, "free", t->free);
			end(d);
		}
		for (i = 0; i < KP_WORKER_AIS; i++) {
			if (wk->next[i] == (i + 1 < KP_WORKER_AIS ? i + 1 : -1) && wk->prev[i] == (i > 0 ? i - 1 : -1)
				&& wk->type[i] == 0 && wk->assignment[i] == 0 && wk->reassign[i] == 0 && wk->wait[i] == 0
				&& wk->time[i] == 0 && wk->unit[i] == 0 && wk->town[i] == -1) {
				continue;
			}
			begin_id(d, "ai_worker", i);
#define WORKER_FIELD(name) field_int(d, #name, wk->name[i]);
			KP_WORKER_AI_FIELDS(WORKER_FIELD)
#undef WORKER_FIELD
			end(d);
		}
		begin(d, "ai_worker", "lists");
		field_int(d, "free", wk->free);
		end(d);
		for (i = 0; i < KP_BUILDING_AIS; i++) {
			Text tags;
			int64_t e;
			int initial = b->next[i] == (i + 1 < KP_BUILDING_AIS ? i + 1 : -1) && b->prev[i] == (i > 0 ? i - 1 : -1)
				&& b->type[i] == 0 && b->unit[i] == 0 && b->town[i] == -1;
			for (e = 0; e < KP_TRAINING_TAGS; e++) {
				if (b->tags[i][e] != 0 || b->contexts[i][e] != 0) initial = 0;
			}
			if (initial) continue;
			tags = text_new(d);
			for (e = 0; e < KP_TRAINING_TAGS; e++) {
				char ref[24];
				text_add(&tags, "%s%lld:%s", e ? "," : "", (long long)b->tags[i][e],
					context_ref(ref, sizeof ref, b->contexts[i][e]));
			}
			begin_id(d, "ai_building", i);
#define BUILDING_FIELD(name) field_int(d, #name, b->name[i]);
			KP_BUILDING_AI_FIELDS(BUILDING_FIELD)
#undef BUILDING_FIELD
			field_text(d, "tags", &tags);
			end(d);
		}
		begin(d, "ai_building", "lists");
		field_int(d, "free", b->free);
		end(d);
	}
	/* The resource areas (tools/native_state.py ai_areas): the used records with the count and the countdown, and each
	 * resource's area +0xd8, AI claim +0xd9 and the union's high WORD. The bytes +0x10..+0x2f of a record are none of
	 * Koprulu's ("extra" is empty: the original's are shown when they are not 0). */
	{
		const KpAreas *a = &g->areas;
		for (i = 1; i <= a->used && i < KP_AREAS; i++) {
			begin_id(d, "ai_area", i);
#define AREA_FIELD(name) field_int(d, #name, a->name[i]);
			KP_AREA_FIELDS(AREA_FIELD)
#undef AREA_FIELD
			field_str(d, "extra", "", 0);
			end(d);
		}
		begin(d, "ai_area", "lists");
		field_int(d, "used", a->used);
		field_int(d, "countdown", a->countdown);
		{ /* the defense anchors with the stale ids past their count */
			Text anchors = text_new(d);
			for (l = 0; l < KP_AI_ANCHORS; l++) text_item(&anchors, g->ai.anchors[l]);
			field_int(d, "anchor_count", g->ai.anchor_count);
			field_text(d, "anchors", &anchors);
		}
		end(d);
		for (l = 0; l < 4; l++) {
			for (n = g->lists[UNIT_LISTS[l]].head; n > 0 && n <= KP_UNITS; n = g->u.next[n]) {
				int64_t word = g->u.union_0xd8[n];
				if ((KP_DAT(g, units, special_ability_flags, g->u.type[n]) & RESOURCE_CONTAINER) == 0) continue;
				begin_id(d, "ai_resource", n);
				field_int(d, "area", kp_band(word, 0xff));
				field_int(d, "claim", kp_band(kp_shrv(word, 8), 0xff));
				field_int(d, "rest", kp_shrv(word, 16));
				end(d);
			}
		}
	}
	/* The AIScript pool (tools/native_state.py ai_scripts): the slots that differ from their start (free, linked to
	 * their neighbours, the rest 0) with their links, payload and town as slot indexes (-1 none), and the lists' heads
	 * and the shared return PC. */
	for (i = 0; i < KP_AI_SCRIPTS; i++) {
		const KpScripts *sc = &g->scripts;
		if (sc->next[i] == (i + 1 < KP_AI_SCRIPTS ? i + 1 : -1) && sc->prev[i] == (i > 0 ? i - 1 : -1)
			&& sc->pc[i] == 0 && sc->wait[i] == 0 && sc->player[i] == 0 && sc->left[i] == 0 && sc->top[i] == 0
			&& sc->right[i] == 0 && sc->bottom[i] == 0 && sc->x[i] == 0 && sc->y[i] == 0 && sc->town[i] == -1
			&& sc->flags[i] == 0) {
			continue;
		}
		begin_id(d, "ai_script", i);
#define AI_SCRIPT_FIELD(name) field_int(d, #name, sc->name[i]);
		KP_AI_SCRIPT_FIELDS(AI_SCRIPT_FIELD)
#undef AI_SCRIPT_FIELD
		end(d);
	}
	begin(d, "ai_script", "lists");
	field_int(d, "head", g->scripts.head);
	field_int(d, "free", g->scripts.free);
	field_int(d, "ret", g->scripts.ret);
	end(d);
	for (i = 0; i < g->map_h; i++) {
		Text row = text_new(d);
		int64_t x;
		for (x = 0; x < g->map_w; x++) text_hex(&row, (uint64_t)(kp_u32v(tr->tiles[i * g->map_w + x])), 8);
		begin_id(d, "tiles", i);
		field_text(d, "flags", &row);
		end(d);
	}
	for (i = 0; i < g->map_h; i++) {
		Text mtxm = text_new(d), backup = text_new(d), border = text_new(d), display = text_new(d);
		int64_t x;
		for (x = 0; x < g->map_w; x++) {
			int64_t at = i * g->map_w + x;
			text_hex(&mtxm, (uint64_t)(tr->raw_tiles[at]), 4);
			text_hex(&backup, (uint64_t)(tr->creep_backup[at]), 4);
			text_hex(&border, (uint64_t)(tr->creep_border[at]), 2);
			text_hex(&display, (uint64_t)(tr->megatiles[at]), 4);
		}
		begin_id(d, "terrain", i);
		field_text(d, "mtxm", &mtxm);
		field_text(d, "backup", &backup);
		field_text(d, "border", &border);
		field_text(d, "display", &display);
		end(d);
	}
}


/* A dump of g's lines: emit (the text) or the hash into *hash; -1 out of memory. */
static int run_dump(KpGame *g, int drawn, KpDumpLine emit, void *ud, uint64_t *hash)
{
	Dump *d = (Dump *)calloc(1, sizeof *d);
	int failed, k;
	if (!d) return -1;
	d->emit = emit;
	d->ud = ud;
	d->drawn = drawn;
	d->hash = hash != NULL;
	dump_lines(d, g);
	give_back(d);
	failed = d->failed;
	if (hash) *hash = mix(d->h, (uint64_t)drawn);
	free(d->arena);
	free(d->line);
	free(d->key.s);
	for (k = 0; k < SHAPES; k++) {
		if (d->shapes[k]) free(d->shapes[k]->prefix);
		free(d->shapes[k]);
	}
	for (k = 0; k < d->spare_count; k++) free(d->spares[k]);
	free(d);
	return failed ? -1 : 0;
}

int kp_dump(KpGame *g, int drawn, KpDumpLine emit, void *ud) { return run_dump(g, drawn, emit, ud, NULL); }

int kp_dump_hash(KpGame *g, int drawn, uint64_t *out) { return run_dump(g, drawn, NULL, NULL, out); }

/* The CRC-32 of each category's lines joined by newlines, computed as the lines come. */
typedef struct {
	KpCategoryCrc each;
	void *ud;
	char category[32];
	char done[64][32]; /* categories already reported */
	int done_count;
	uint32_t crc;
	int lines; /* of the current category */
	int failed;
} Crcs;

static void crcs_flush(Crcs *o)
{
	if (o->category[0] == '\0') return;
	o->each(o->ud, o->category, o->crc);
	if (o->done_count < 64) snprintf(o->done[o->done_count++], sizeof o->done[0], "%s", o->category);
	o->category[0] = '\0';
	o->crc = 0;
	o->lines = 0;
}

static void crcs_line(void *ud, const char *line, size_t length)
{
	Crcs *o = (Crcs *)ud;
	size_t c = 0;
	int k;
	if (o->failed) return;
	while (c < length && line[c] != ' ') c++;
	if (c >= sizeof o->category) {
		o->failed = 1;
		return;
	}
	if (strncmp(o->category, line, c) != 0 || o->category[c] != '\0') {
		crcs_flush(o);
		memcpy(o->category, line, c);
		o->category[c] = '\0';
		for (k = 0; k < o->done_count; k++) {
			if (strcmp(o->done[k], o->category) == 0) o->failed = 1;
		}
	}
	if (o->lines++ > 0) o->crc = kp_crc32_update(o->crc, (const uint8_t *)"\n", 1);
	o->crc = kp_crc32_update(o->crc, (const uint8_t *)line, length);
}

int kp_dump_crcs(KpGame *g, int drawn, KpCategoryCrc each, void *ud)
{
	Crcs *o = (Crcs *)calloc(1, sizeof *o);
	int r;
	if (!o) return -1;
	o->each = each;
	o->ud = ud;
	r = kp_dump(g, drawn, crcs_line, o);
	if (r == 0 && !o->failed) crcs_flush(o);
	r = r != 0 || o->failed ? -1 : 0;
	free(o);
	return r;
}
