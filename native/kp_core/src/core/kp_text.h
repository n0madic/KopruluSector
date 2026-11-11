/* The runtime's text is UTF-8 (ADR 0016). A byte that is no valid sequence reads as that byte's code point in WHATWG's
 * windows-1252, a bijection of the 256 bytes, so the original engine's byte strings decode to the same glyphs. */
#ifndef KP_TEXT_H
#define KP_TEXT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KP_TEXT_UTF8_MAX 4       /* the bytes of one code point */
#define KP_TEXT_ASCII_END 0x80   /* the code points whose byte is the same in UTF-8 and every slot of an FNT font */
#define KP_TEXT_NUL_TERMINATED SIZE_MAX /* a kp_text_next limit: the string's NUL ends every sequence */

/* The code point of a byte in windows-1252. */
int32_t kp_text_from_byte(uint8_t byte);
/* The byte of a code point in windows-1252 (an FNT font's slot), -1 for one it does not have. */
int kp_text_byte(int32_t cp);
/* The code point at s[*at] and *at past it; n bounds the sequence. */
int32_t kp_text_next(const uint8_t *s, size_t n, size_t *at);
/* The start of the character holding byte `at` of the NUL-terminated s. */
size_t kp_text_floor(const uint8_t *s, size_t at);
/* The start of the character before byte `at`, which starts one; 0 at the start. */
size_t kp_text_prev(const uint8_t *s, size_t at);
/* The characters of the NUL-terminated s. */
size_t kp_text_chars(const uint8_t *s);
/* s into out, cut on a character's boundary at size - 1 bytes: its length. */
size_t kp_text_copy(char *out, size_t size, const char *s);
/* s into out, at most `chars` characters, cut on a character's boundary at size - 1 bytes: its length. */
size_t kp_text_copy_chars(char *out, size_t size, const char *s, size_t chars);
/* A code point's bytes into out: their count, 0 for no code point. */
size_t kp_text_encode(int32_t cp, uint8_t out[KP_TEXT_UTF8_MAX]);
/* A windows-1252 string as UTF-8, cut on a character's boundary at size - 1 bytes: its length. */
size_t kp_text_from_legacy(const char *legacy, char *out, size_t size);
/* A UTF-8 string back to windows-1252 bytes, what has no byte left out: its length (never longer than utf8). */
size_t kp_text_to_legacy(const char *utf8, char *out, size_t size);
/* Whether n bytes are valid UTF-8 as Python's strict decoder takes it (no overlong forms, surrogates or code points
 * past U+10FFFF). */
int kp_text_valid_utf8(const uint8_t *s, size_t n);
/* Raw text in UTF-8 (converter/kpf/kpd.py text_utf8): valid UTF-8 kept, else each byte as windows-1252; a malloc'd
 * copy, NUL terminated past *out_size, or NULL when out of memory. */
uint8_t *kp_text_utf8_of(const uint8_t *raw, size_t n, size_t *out_size);
/* A TBL with its text in UTF-8 (kpd.table_utf8): each offset moves with the bytes the text before it grew by, one
 * past the end by the whole growth; a malloc'd copy, NUL terminated past *out_size, or NULL when the offset table runs
 * past the file, an offset outgrows 16 bits, or out of memory. */
uint8_t *kp_text_table_utf8(const uint8_t *data, size_t size, size_t *out_size);

#ifdef __cplusplus
}
#endif

#endif
