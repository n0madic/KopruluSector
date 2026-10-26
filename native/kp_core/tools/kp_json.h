/*
 * A small JSON reader and writer for the oracle tools and the C suites (plan phase S, stage S1): the goldens of
 * tests/golden/oracle/ (the .json files of tools/oracle/oracle_common.py golden_json), the emulator's JSON lines (commands.jsonl,
 * ui.jsonl) and the values the suites build to compare with them. Not for the product: the game's data are .kpd/.kpm.
 *
 * A document owns every value parsed into it or built in it (an arena, released at once by kp_json_doc_free).
 * Strings are bytes: \u00XX escapes below 0x100 give the byte XX (the goldens keep byte strings that way),
 * higher code points their UTF-8. Numbers keep their integer value when integral (int64) and their double.
 */
#ifndef KP_JSON_H
#define KP_JSON_H

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	KP_JSON_NULL, KP_JSON_FALSE, KP_JSON_TRUE, KP_JSON_NUMBER, KP_JSON_STRING, KP_JSON_ARRAY, KP_JSON_OBJECT
} KpJsonType;

typedef struct KpJson {
	KpJsonType type;
	int count;              /* an array's elements, an object's members, a string's bytes */
	int cap;                /* the room of items/keys (built values grow) */
	int integral;           /* a number without a fraction or an exponent that fits int64 */
	int64_t i;              /* a number's integer value (truncated when not integral) */
	double d;               /* a number's value */
	const char *s;          /* a string's bytes, NUL terminated */
	const char **keys;      /* an object's keys, in document order */
	struct KpJson **items;  /* an array's elements, an object's values */
} KpJson;

typedef struct KpJsonBlock KpJsonBlock;
typedef struct {
	KpJsonBlock *blocks;
} KpJsonDoc;

void kp_json_doc_init(KpJsonDoc *doc);
void kp_json_doc_free(KpJsonDoc *doc);
/* Bytes of the document's arena (zeroed). Aborts when out of memory. */
void *kp_json_alloc(KpJsonDoc *doc, size_t size);

/* The value of text[0..size-1] (one JSON value, white space around it), or NULL with the error in err. */
KpJson *kp_json_parse(KpJsonDoc *doc, const char *text, size_t size, char *err, size_t err_size);

/* Lookups; NULL (or the fallback) for a missing member, an index out of range or another type. */
const KpJson *kp_json_get(const KpJson *object, const char *key);
const KpJson *kp_json_at(const KpJson *array, int index);
int kp_json_len(const KpJson *v); /* an array's or object's count, 0 otherwise */
int kp_json_is(const KpJson *v, KpJsonType type);
int64_t kp_json_int(const KpJson *v, int64_t fallback);
int kp_json_truthy(const KpJson *v); /* present and neither null nor false */
const char *kp_json_str(const KpJson *v, const char *fallback);
/* Shorthands over an object's member. */
int64_t kp_json_field_int(const KpJson *object, const char *key, int64_t fallback);
const char *kp_json_field_str(const KpJson *object, const char *key, const char *fallback);

/* Built values (in doc). */
KpJson *kp_json_new(KpJsonDoc *doc, KpJsonType type);
KpJson *kp_json_new_int(KpJsonDoc *doc, int64_t v);
KpJson *kp_json_new_bool(KpJsonDoc *doc, int v);
KpJson *kp_json_new_string(KpJsonDoc *doc, const char *s, size_t n);
/* Appends to an array; returns item. */
KpJson *kp_json_push(KpJsonDoc *doc, KpJson *array, KpJson *item);
KpJson *kp_json_push_int(KpJsonDoc *doc, KpJson *array, int64_t v);
/* Sets an object's member (replacing one of the same key); returns item. */
KpJson *kp_json_set(KpJsonDoc *doc, KpJson *object, const char *key, KpJson *item);
KpJson *kp_json_set_int(KpJsonDoc *doc, KpJson *object, const char *key, int64_t v);

/* Deep equality as the suites compare goldens: numbers by value, objects by their member sets, an empty array equal
 * to an empty object (the goldens do not tell them apart). */
int kp_json_equal(const KpJson *a, const KpJson *b);

/* A growable byte buffer. */
typedef struct {
	char *data;
	size_t size, cap;
} KpBuf;

void kp_buf_init(KpBuf *b);
void kp_buf_free(KpBuf *b);
void kp_buf_add(KpBuf *b, const void *bytes, size_t n);
void kp_buf_str(KpBuf *b, const char *s);
void kp_buf_char(KpBuf *b, char c);
#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 2, 3)))
#endif
void kp_buf_printf(KpBuf *b, const char *fmt, ...);
void kp_buf_vprintf(KpBuf *b, const char *fmt, va_list ap);
/* The buffer's text, NUL terminated (the terminator not counted in size). */
const char *kp_buf_text(KpBuf *b);

/* The compact JSON text of v (keys in their order). */
void kp_json_write(KpBuf *out, const KpJson *v);

#ifdef __cplusplus
}
#endif

#endif
