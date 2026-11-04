/* UTF-8 decoding (kp_utf8.h). */
#include "kp_utf8.h"

int32_t kp_utf8_next(const uint8_t *s, size_t n, size_t *at)
{
	uint8_t c = s[(*at)++], lo = 0x80, hi = 0xbf; /* the second byte's range (table 3-7) */
	int32_t cp;
	int more;
	if (c < 0x80) return c;
	if (c >= 0xc2 && c <= 0xdf) more = 1, cp = c & 0x1f;
	else if (c >= 0xe0 && c <= 0xef) more = 2, cp = c & 0x0f;
	else if (c >= 0xf0 && c <= 0xf4) more = 3, cp = c & 0x07;
	else return KP_UTF8_BAD;
	if (c == 0xe0) lo = 0xa0;      /* no overlong three-byte form */
	else if (c == 0xed) hi = 0x9f; /* no surrogate */
	else if (c == 0xf0) lo = 0x90; /* no overlong four-byte form */
	else if (c == 0xf4) hi = 0x8f; /* nothing past U+10FFFF */
	for (; more > 0; more--, lo = 0x80, hi = 0xbf) {
		if (*at >= n || s[*at] < lo || s[*at] > hi) return KP_UTF8_BAD;
		cp = (cp << 6) | (s[(*at)++] & 0x3f);
	}
	return cp;
}
