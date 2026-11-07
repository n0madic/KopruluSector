/*
 * A glyph supplement in FNT's form from a TrueType font (kp_present.h; ADR 0016): the characters an FNT font's 224 byte
 * slots lack, rasterized (stb_truetype) to the font's cap height and fitted to its widths, in the masks its own glyphs
 * use. Written by the converter's `locale` through tools/kp_fnt_make.c.
 */
#include "kp_present.h"
#include "kp_present_internal.h"
#include "stb_truetype.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MASKS 8
#define PACKET_SKIP_MAX 31
#define PACKET_SKIP_SHIFT 3

/* A font's colour rows (tFontGam.pcx, a theme's tFont.pcx): entry 1 is the body, 2, 3 and 4 darken towards the edge;
 * the shadow is entry 7, or 5 where a font's rows have black there. Font14's letters have none: its 5 is in the boxes of
 * 0x7f..0xa0 alone, which is why the masks are counted on the letters. */
#define BODY 1
#define EDGE_BRIGHT 2
#define EDGE_MIDDLE 3
#define EDGE_DARK 4
#define SHADOW_BLACK 5
#define SHADOW 7
#define STYLE_SHARE 10 /* a mask is the font's when at least 1 in this many of its glyphs' pixels has it */

/* Coverage (of 255) to a mask: a font with edges has a soft outline, the others a threshold low enough to keep a thin
 * stroke that falls between two rows (the crossbar of a small H). With edges, the body's threshold is the one at which
 * the font's own Latin letters have the FNT font's share of body pixels (fit_body): font10's glyphs are nearly all body,
 * font14's half edges, and one threshold for both drew font10's thin strokes in its edge colour, with no shadow, which
 * only the body casts. The edges split the coverage under it in thirds. */
#define COVER_THRESHOLD 80
#define COVER_DARK 30
#define COVER_BODY_LOW (COVER_DARK + 3) /* the body's threshold leaves the three edges a level each */
#define COVER_BODY_HIGH 255
#define EDGE_LEVELS 3

#define SHIFTS 4 /* the horizontal subpixel positions tried for a glyph, the sharpest kept: stems on whole pixels */
#define GLYPH_LEFT_MAX 1 /* the x of a glyph with a side bearing */
#define MASK_SIDE_MAX 255 /* FNT's bytes */
#define FIT_STEPS 16 /* the search for the widths' factor */
#define FIT_LOW 0.25
#define FIT_HIGH 2.0
#define LETTER_FIRST 'A'
#define LETTER_LAST 'z'

typedef struct {
	int edges;  /* the font's glyphs have edge masks */
	int shadow; /* its drop shadow's mask, -1 none */
	int top, height; /* kp_ui_font_cap: the cap's rows from the pen */
	int cell;   /* the rows a line of the font takes: a glyph stays in them */
	double fit;  /* the horizontal scale's factor, to the FNT font's widths */
	int body, bright, middle; /* the coverage of the body's mask and the two brighter edges' (fit_body) */
} Style;

static int fail(char *err, size_t n, const char *why)
{
	snprintf(err, n, "%s", why);
	return -1;
}

static int is_letter(int code) { return (code >= 'A' && code <= 'Z') || (code >= 'a' && code <= 'z'); }

/* How many of the pixels of the FNT font's Latin letters each mask has, as the blitter reads their packets. */
static void count_masks(const uint8_t *font, size_t size, uint32_t counts[MASKS])
{
	int low = font[4], code;
	memset(counts, 0, MASKS * sizeof counts[0]);
	for (code = LETTER_FIRST; code <= LETTER_LAST; code++) {
		uint32_t off = is_letter(code) ? kp_le_u32(font + KP_FNT_HEADER + 4 * (code - low)) : 0;
		size_t at;
		int total, pos = 0;
		if (off == 0 || (size_t)off + KP_FNT_GLYPH_HEAD > size) continue;
		total = font[off] * font[off + 1];
		at = (size_t)off + KP_FNT_GLYPH_HEAD;
		while (pos < total && at < size) {
			int skip = font[at] >> PACKET_SKIP_SHIFT, mask = font[at] & (MASKS - 1);
			at++;
			pos += skip;
			if (pos >= total) break;
			if (skip == 0 || mask != 0) {
				counts[mask]++;
				pos++;
			}
		}
	}
}

static int advance_of(const uint8_t *font, size_t size, int code)
{
	uint32_t off = kp_le_u32(font + KP_FNT_HEADER + 4 * (code - font[4]));
	return off != 0 && (size_t)off + KP_FNT_GLYPH_HEAD <= size ? font[off] + font[off + 2] + 1 : 0;
}

static uint8_t mask_of(const Style *st, int cover)
{
	if (!st->edges) return cover >= COVER_THRESHOLD ? BODY + 1 : 0;
	if (cover >= st->body) return BODY + 1;
	if (cover >= st->bright) return EDGE_BRIGHT + 1;
	if (cover >= st->middle) return EDGE_MIDDLE + 1;
	return cover >= COVER_DARK ? EDGE_DARK + 1 : 0;
}

typedef struct {
	uint8_t *data;
	size_t size, capacity;
} Out;

static int put(Out *o, const uint8_t *bytes, size_t n)
{
	if (o->size + n > o->capacity) {
		size_t cap = o->capacity ? 2 * o->capacity : 4096;
		uint8_t *grown;
		while (cap < o->size + n) cap *= 2;
		if (!(grown = (uint8_t *)realloc(o->data, cap))) return -1;
		o->data = grown;
		o->capacity = cap;
	}
	memcpy(o->data + o->size, bytes, n);
	o->size += n;
	return 0;
}

/* The glyph (mask k + 1 per pixel, 0 none) in FNT's packets: a skip of up to 31 pixels and a mask (a skip with mask 0
 * plots nothing, so a plotted mask here is never 0). */
static int put_glyph(Out *o, const uint8_t head[KP_FNT_GLYPH_HEAD], const uint8_t *masks, int total)
{
	int k, run = 0;
	if (put(o, head, KP_FNT_GLYPH_HEAD) != 0) return -1;
	for (k = 0; k <= total; k++) {
		uint8_t packet;
		if (k < total && masks[k] == 0) {
			run++;
			continue;
		}
		while (run > PACKET_SKIP_MAX || (k == total && run > 0)) {
			int step = run > PACKET_SKIP_MAX ? PACKET_SKIP_MAX : run;
			packet = (uint8_t)(step << PACKET_SKIP_SHIFT);
			if (put(o, &packet, 1) != 0) return -1;
			run -= step;
		}
		if (k == total) break;
		packet = (uint8_t)(run << PACKET_SKIP_SHIFT | (masks[k] - 1));
		if (put(o, &packet, 1) != 0) return -1;
		run = 0;
	}
	return 0;
}

/* The glyph's coverage at the horizontal subpixel position with the sharpest stems (the most squared coverage). */
static unsigned char *sharpest_bitmap(const stbtt_fontinfo *info, float sx, float px, int32_t cp, int *w, int *h, int *xoff,
	int *yoff)
{
	unsigned char *best = NULL;
	double best_score = -1.0;
	int shift;
	for (shift = 0; shift < SHIFTS; shift++) {
		int bw = 0, bh = 0, bx = 0, by = 0, k;
		double score = 0.0;
		unsigned char *b = stbtt_GetCodepointBitmapSubpixel(info, sx, px, (float)shift / SHIFTS, 0.0f, cp, &bw, &bh, &bx, &by);
		if (b == NULL) continue;
		for (k = 0; k < bw * bh; k++) score += (double)b[k] * b[k];
		if (score > best_score) {
			stbtt_FreeBitmap(best, NULL);
			best = b;
			best_score = score;
			*w = bw;
			*h = bh;
			*xoff = bx;
			*yoff = by;
		} else {
			stbtt_FreeBitmap(b, NULL);
		}
	}
	return best;
}

/* A glyph in FNT's terms: masks (mask k + 1 per pixel, 0 none) of its width and height, its x and y. */
typedef struct {
	uint8_t *masks;
	int w, h, left, top;
} Raster;

/* The FNT advance of a raster: x + width + 1, the blitter's. */
static int raster_advance(const Raster *r) { return r->left + r->w + 1; }

/* One code point at a horizontal scale: the columns of its ink and, for the font that has one, its shadow's. The side
 * bearings go, as in the FNT fonts' tight letters: the x says only whether the glyph has one. The rows past the font's
 * cell go too, shadow included, as its own descenders end there: text lines are a cell apart and a control's clip takes
 * whole cells. A character with no ink keeps the TrueType advance. 0, or -1 when out of memory or too large. */
static int raster_glyph(const stbtt_fontinfo *info, const Style *st, float sx, float px, int32_t cp, Raster *r)
{
	int w = 0, h = 0, xoff = 0, yoff = 0, first, last, x, y, skip, end, shadow = st->shadow >= 0;
	unsigned char *bitmap = sharpest_bitmap(info, sx, px, cp, &w, &h, &xoff, &yoff);
	int top = st->top + st->height + yoff;
	memset(r, 0, sizeof *r);
	if (bitmap == NULL) w = h = 0;
	first = w;
	last = -1;
	skip = top < 0 ? -top : 0;
	end = st->cell - shadow - top;
	if (end > h) end = h;
	for (y = skip; y < end; y++) {
		for (x = 0; x < w; x++) {
			if (mask_of(st, bitmap[y * w + x]) != 0) {
				if (x < first) first = x;
				if (x > last) last = x;
			}
		}
	}
	if (last < 0) { /* no ink: a space's width */
		int adv, lsb;
		stbtt_FreeBitmap(bitmap, NULL);
		stbtt_GetCodepointHMetrics(info, cp, &adv, &lsb);
		r->w = (int)((double)adv * sx + 0.5) - 1;
		if (r->w < 1) r->w = 1;
		r->h = 1;
		if (r->w > MASK_SIDE_MAX || !(r->masks = (uint8_t *)calloc((size_t)r->w, 1))) return -1;
		return 0;
	}
	r->w = last - first + 1 + shadow;
	r->h = end - skip + shadow;
	r->left = xoff + first > 0 ? GLYPH_LEFT_MAX : 0;
	r->top = top > 0 ? top : 0;
	if (r->w > MASK_SIDE_MAX || r->h > MASK_SIDE_MAX || !(r->masks = (uint8_t *)calloc((size_t)r->w * (size_t)r->h, 1))) {
		stbtt_FreeBitmap(bitmap, NULL);
		return -1;
	}
	for (y = skip; y < end; y++) {
		for (x = first; x <= last; x++) r->masks[(y - skip) * r->w + x - first] = mask_of(st, bitmap[y * w + x]);
	}
	stbtt_FreeBitmap(bitmap, NULL);
	if (shadow) { /* one pixel down and right of the body, under it */
		for (y = r->h - 2; y >= 0; y--) {
			for (x = r->w - 2; x >= 0; x--) {
				if (r->masks[y * r->w + x] == BODY + 1 && r->masks[(y + 1) * r->w + x + 1] == 0) {
					r->masks[(y + 1) * r->w + x + 1] = (uint8_t)(st->shadow + 1);
				}
			}
		}
	}
	return 0;
}

/* The factor of the horizontal scale at which the font's own Latin letters, made as raster_glyph does, take the FNT
 * font's widths: its other characters then keep the same proportion to them. */
static double fit_widths(const stbtt_fontinfo *info, const Style *st, const uint8_t *font, size_t size, float px)
{
	double low = FIT_LOW, high = FIT_HIGH, wanted = 0.0;
	int k, step;
	for (k = LETTER_FIRST; k <= LETTER_LAST; k++) {
		if (is_letter(k)) wanted += advance_of(font, size, k);
	}
	for (step = 0; step < FIT_STEPS; step++) {
		double fit = (low + high) / 2.0, got = 0.0;
		for (k = LETTER_FIRST; k <= LETTER_LAST; k++) {
			Raster r;
			if (!is_letter(k) || raster_glyph(info, st, (float)((double)px * fit), px, k, &r) != 0) continue;
			got += raster_advance(&r);
			free(r.masks);
		}
		if (got > wanted) high = fit;
		else low = fit;
	}
	return low;
}

static void set_body(Style *st, int body)
{
	st->body = body;
	st->middle = COVER_DARK + (body - COVER_DARK) / EDGE_LEVELS;
	st->bright = COVER_DARK + 2 * (body - COVER_DARK) / EDGE_LEVELS;
}

/* The share (of 1) of body pixels among the ink of the font's Latin letters made as raster_glyph does: the FNT font's
 * own from its packets' counts, ours at the style's thresholds; -1 with no ink. */
static double body_share(const uint32_t counts[MASKS])
{
	uint32_t ink = counts[BODY] + counts[EDGE_BRIGHT] + counts[EDGE_MIDDLE] + counts[EDGE_DARK];
	return ink ? (double)counts[BODY] / ink : -1.0;
}

static double our_body_share(const stbtt_fontinfo *info, const Style *st, float px)
{
	uint32_t counts[MASKS] = { 0 };
	int k, j;
	for (k = LETTER_FIRST; k <= LETTER_LAST; k++) {
		Raster r;
		if (!is_letter(k) || raster_glyph(info, st, (float)((double)px * st->fit), px, k, &r) != 0) continue;
		for (j = 0; j < r.w * r.h; j++) {
			if (r.masks[j] != 0) counts[r.masks[j] - 1]++;
		}
		free(r.masks);
	}
	return body_share(counts);
}

/* The body's threshold at which our Latin letters have the FNT font's share of body pixels (a higher threshold, a
 * smaller share), of the two around it the nearer. Only the edges' split depends on it: the ink and the widths not. */
static void fit_body(Style *st, const stbtt_fontinfo *info, float px, double wanted)
{
	int low = COVER_BODY_LOW, high = COVER_BODY_HIGH; /* the share at low is at least the wanted, at high under it */
	set_body(st, low);
	if (our_body_share(info, st, px) <= wanted) return;
	set_body(st, high);
	if (our_body_share(info, st, px) >= wanted) return;
	while (high - low > 1) {
		int mid = low + (high - low) / 2;
		set_body(st, mid);
		if (our_body_share(info, st, px) >= wanted) low = mid;
		else high = mid;
	}
	set_body(st, low);
	{
		double at_low = our_body_share(info, st, px), at_high;
		set_body(st, high);
		at_high = our_body_share(info, st, px);
		set_body(st, at_low - wanted <= wanted - at_high ? low : high);
	}
}

static int style_of(Style *st, const stbtt_fontinfo *info, const uint8_t *font, size_t size, float *px, char *err, size_t n)
{
	uint32_t counts[MASKS], total = 0;
	int k, x0, y0, x1, y1;
	if (size < KP_FNT_HEADER || memcmp(font, "FONT", 4) != 0 || font[5] < font[4] || font[4] > 'A' || font[5] < 'z'
		|| size < KP_FNT_HEADER + 4 * (size_t)(font[5] - font[4] + 1)) {
		return fail(err, n, "not an FNT font with the Latin letters");
	}
	kp_ui_font_cap(font, size, &st->top, &st->height);
	if (st->height <= 0) return fail(err, n, "the FNT font has no cap height");
	st->cell = font[KP_FNT_MAX_HEIGHT];
	if (stbtt_GetCodepointBox(info, 'H', &x0, &y0, &x1, &y1) == 0 || y1 <= 0) return fail(err, n, "the TrueType font has no 'H'");
	*px = (float)st->height / (float)y1;
	count_masks(font, size, counts);
	for (k = 0; k < MASKS; k++) total += counts[k];
	st->edges = counts[EDGE_BRIGHT] + counts[EDGE_MIDDLE] + counts[EDGE_DARK] >= total / STYLE_SHARE;
	st->shadow = counts[SHADOW] >= total / STYLE_SHARE ? SHADOW : counts[SHADOW_BLACK] >= total / STYLE_SHARE ? SHADOW_BLACK : -1;
	set_body(st, COVER_BODY_HIGH); /* the ink is what any body's threshold gives */
	st->fit = fit_widths(info, st, font, size, *px);
	if (st->edges) fit_body(st, info, *px, body_share(counts));
	return 0;
}

/* One code point's glyph at the end of o. */
static int make_glyph(Out *o, const stbtt_fontinfo *info, const Style *st, float px, int32_t cp, char *err, size_t n)
{
	Raster r;
	uint8_t head[KP_FNT_GLYPH_HEAD];
	int rc;
	if (raster_glyph(info, st, (float)((double)px * st->fit), px, cp, &r) != 0) return fail(err, n, "a glyph past FNT's 255 pixels");
	head[0] = (uint8_t)r.w;
	head[1] = (uint8_t)r.h;
	head[2] = (uint8_t)r.left;
	head[3] = (uint8_t)r.top;
	rc = put_glyph(o, head, r.masks, r.w * r.h);
	free(r.masks);
	return rc != 0 ? fail(err, n, "out of memory") : 0;
}

int kp_fnt_supplement(const uint8_t *ttf, size_t ttf_size, const uint8_t *font, size_t font_size, const int32_t *codes,
	int32_t count, uint8_t **out, size_t *out_size, int32_t *missing, int32_t *missing_count, char *err, size_t err_size)
{
	stbtt_fontinfo info;
	Style st;
	float px = 0.0f;
	Out o = { NULL, 0, 0 };
	int32_t k, kept = 0;
	uint8_t blank[KP_FNT_SUPPLEMENT_HEAD + KP_FNT_SUPPLEMENT_ENTRY] = { 0 };
	*out = NULL;
	*out_size = 0;
	*missing_count = 0;
	if (kp_ttf_open(&info, ttf, ttf_size) != 0) {
		return fail(err, err_size, "not a TrueType font");
	}
	if (style_of(&st, &info, font, font_size, &px, err, err_size) != 0) return -1;
	for (k = 0; k < count; k++) {
		if (k > 0 && codes[k] <= codes[k - 1]) return fail(err, err_size, "the code points are not ascending");
		if (stbtt_FindGlyphIndex(&info, codes[k]) != 0) kept++;
		else missing[(*missing_count)++] = codes[k];
	}
	if (put(&o, blank, KP_FNT_SUPPLEMENT_HEAD) != 0) return fail(err, err_size, "out of memory");
	for (k = 0; k < kept; k++) {
		if (put(&o, blank + KP_FNT_SUPPLEMENT_HEAD, KP_FNT_SUPPLEMENT_ENTRY) != 0) {
			free(o.data);
			return fail(err, err_size, "out of memory");
		}
	}
	o.data[0] = (uint8_t)kept;
	o.data[1] = (uint8_t)((uint32_t)kept >> 8);
	o.data[2] = (uint8_t)((uint32_t)kept >> 16);
	o.data[3] = (uint8_t)((uint32_t)kept >> 24);
	kept = 0;
	for (k = 0; k < count; k++) {
		size_t at = o.size;
		size_t entry = KP_FNT_SUPPLEMENT_HEAD + (size_t)kept * KP_FNT_SUPPLEMENT_ENTRY;
		int b;
		if (stbtt_FindGlyphIndex(&info, codes[k]) == 0) continue;
		if (make_glyph(&o, &info, &st, px, codes[k], err, err_size) != 0) {
			free(o.data);
			return -1;
		}
		for (b = 0; b < 4; b++) {
			o.data[entry + (size_t)b] = (uint8_t)((uint32_t)codes[k] >> (8 * b));
			o.data[entry + 4 + (size_t)b] = (uint8_t)((uint32_t)at >> (8 * b));
		}
		kept++;
	}
	*out = o.data;
	*out_size = o.size;
	return 0;
}
