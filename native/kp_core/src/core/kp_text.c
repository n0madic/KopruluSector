/* UTF-8 text with the windows-1252 reading of stray bytes (kp_text.h; ADR 0016). */
#include "kp_text.h"
#include "kp_utf8.h"

#include <string.h>

#define C1_FIRST 0x80
#define C1_END 0xa0

/* WHATWG's windows-1252: 0x80..0x9f (a byte Windows leaves undefined stays its C1 control, so every byte has one). */
static const uint16_t C1[C1_END - C1_FIRST] = { 0x20ac, 0x0081, 0x201a, 0x0192, 0x201e, 0x2026, 0x2020, 0x2021, 0x02c6,
	0x2030, 0x0160, 0x2039, 0x0152, 0x008d, 0x017d, 0x008f, 0x0090, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013,
	0x2014, 0x02dc, 0x2122, 0x0161, 0x203a, 0x0153, 0x009d, 0x017e, 0x0178 };

int32_t kp_text_from_byte(uint8_t byte)
{
	return byte >= C1_FIRST && byte < C1_END ? C1[byte - C1_FIRST] : byte;
}

int kp_text_byte(int32_t cp)
{
	int k;
	if (cp < 0) return -1;
	if (cp < C1_FIRST || (cp >= C1_END && cp <= 0xff)) return cp;
	for (k = 0; k < C1_END - C1_FIRST; k++) {
		if (C1[k] == cp) return C1_FIRST + k;
	}
	return -1;
}

int32_t kp_text_next(const uint8_t *s, size_t n, size_t *at)
{
	size_t from = *at;
	int32_t cp = kp_utf8_next(s, n, at);
	if (cp != KP_UTF8_BAD) return cp;
	*at = from + 1;
	return kp_text_from_byte(s[from]);
}

size_t kp_text_floor(const uint8_t *s, size_t at)
{
	size_t p = 0;
	while (p < at && s[p] != 0) {
		size_t q = p;
		(void)kp_text_next(s, KP_TEXT_NUL_TERMINATED, &q);
		if (q > at) return p;
		p = q;
	}
	return p;
}

size_t kp_text_prev(const uint8_t *s, size_t at)
{
	size_t p = 0, last = 0;
	while (p < at && s[p] != 0) {
		last = p;
		(void)kp_text_next(s, KP_TEXT_NUL_TERMINATED, &p);
	}
	return last;
}

size_t kp_text_chars(const uint8_t *s)
{
	size_t p = 0, count = 0;
	while (s[p] != 0) {
		(void)kp_text_next(s, KP_TEXT_NUL_TERMINATED, &p);
		count++;
	}
	return count;
}

size_t kp_text_copy(char *out, size_t size, const char *s)
{
	size_t n;
	if (size == 0) return 0;
	n = strlen(s);
	if (n >= size) n = kp_text_floor((const uint8_t *)s, size - 1);
	memcpy(out, s, n);
	out[n] = 0;
	return n;
}

size_t kp_text_copy_chars(char *out, size_t size, const char *s, size_t chars)
{
	const uint8_t *t = (const uint8_t *)s;
	size_t at = 0, next = 0;
	if (size == 0) return 0;
	while (chars-- > 0 && t[at] != 0) {
		(void)kp_text_next(t, KP_TEXT_NUL_TERMINATED, &next);
		if (next >= size) break;
		at = next;
	}
	memcpy(out, s, at);
	out[at] = 0;
	return at;
}

size_t kp_text_encode(int32_t cp, uint8_t out[KP_TEXT_UTF8_MAX])
{
	if (cp < 0 || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) return 0;
	if (cp < 0x80) {
		out[0] = (uint8_t)cp;
		return 1;
	}
	if (cp < 0x800) {
		out[0] = (uint8_t)(0xc0 | cp >> 6);
		out[1] = (uint8_t)(0x80 | (cp & 0x3f));
		return 2;
	}
	if (cp < 0x10000) {
		out[0] = (uint8_t)(0xe0 | cp >> 12);
		out[1] = (uint8_t)(0x80 | (cp >> 6 & 0x3f));
		out[2] = (uint8_t)(0x80 | (cp & 0x3f));
		return 3;
	}
	out[0] = (uint8_t)(0xf0 | cp >> 18);
	out[1] = (uint8_t)(0x80 | (cp >> 12 & 0x3f));
	out[2] = (uint8_t)(0x80 | (cp >> 6 & 0x3f));
	out[3] = (uint8_t)(0x80 | (cp & 0x3f));
	return 4;
}

size_t kp_text_from_legacy(const char *legacy, char *out, size_t size)
{
	const uint8_t *s = (const uint8_t *)legacy;
	size_t n = 0;
	if (size == 0) return 0;
	for (; *s != 0; s++) {
		uint8_t bytes[KP_TEXT_UTF8_MAX];
		size_t m = kp_text_encode(kp_text_from_byte(*s), bytes), k;
		if (n + m + 1 > size) break;
		for (k = 0; k < m; k++) out[n++] = (char)bytes[k];
	}
	out[n] = 0;
	return n;
}

size_t kp_text_to_legacy(const char *utf8, char *out, size_t size)
{
	const uint8_t *s = (const uint8_t *)utf8;
	size_t at = 0, n = 0;
	if (size == 0) return 0;
	while (s[at] != 0 && n + 1 < size) {
		int byte = kp_text_byte(kp_text_next(s, KP_TEXT_NUL_TERMINATED, &at));
		if (byte >= 0) out[n++] = (char)byte;
	}
	out[n] = 0;
	return n;
}
