/*
 * UTF-8 decoding for every library (not the original's: its text is single bytes; the hosts' text events and
 * Remastered's subtitles are UTF-8). Strict, as the Unicode Standard's section 3.9, table 3-7: no overlong forms, no
 * surrogates, nothing past U+10FFFF. A malformed sequence is one error (KP_UTF8_BAD, nothing substituted) as the
 * Standard's "maximal subpart" practice: its longest valid prefix, and the byte after it starts the next character.
 */
#ifndef KP_UTF8_H
#define KP_UTF8_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KP_UTF8_BAD (-1)

/* The code point at s[*at..n) with *at moved past it, or KP_UTF8_BAD with *at moved past the malformed subpart (at
 * least a byte). *at < n; nothing at or past s[n] is read. */
int32_t kp_utf8_next(const uint8_t *s, size_t n, size_t *at);

#ifdef __cplusplus
}
#endif

#endif
