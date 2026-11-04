/*
 * A cinematic's subtitle as an image (kp_present.h; not the original's: Remastered's .srt, 2026-11-04): its UTF-8
 * lines wrapped at spaces, each centred, in a TrueType font (stb_truetype, third_party/README.md) at the window's
 * resolution, white in a dark outline so it reads over any picture.
 */
#include "kp_present.h"
#include "stb_truetype.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define OUTLINE_PER_PX (1.0f / 14.0f) /* the outline's radius over the line height */
#define OUTLINE_ALPHA 0.85f
#define MAX_CODES 4096

/* The next code point of s[*k..n) (*k moved past it), or -1 for a byte that does not begin one (skipped). */
static int32_t utf8_next(const unsigned char *s, size_t n, size_t *k)
{
	unsigned char c = s[*k];
	int32_t cp;
	int more, j;
	(*k)++;
	if (c < 0x80) return c;
	if (c >= 0xc2 && c <= 0xdf) more = 1, cp = c & 0x1f;
	else if (c >= 0xe0 && c <= 0xef) more = 2, cp = c & 0x0f;
	else if (c >= 0xf0 && c <= 0xf4) more = 3, cp = c & 0x07;
	else return -1;
	if (*k + (size_t)more > n) return -1;
	for (j = 0; j < more; j++) {
		if ((s[*k + (size_t)j] & 0xc0) != 0x80) return -1;
		cp = (cp << 6) | (s[*k + (size_t)j] & 0x3f);
	}
	*k += (size_t)more;
	if ((more == 2 && cp < 0x800) || (more == 3 && (cp < 0x10000 || cp > 0x10ffff)) || (cp >= 0xd800 && cp <= 0xdfff))
		return -1;
	return cp;
}

typedef struct {
	const stbtt_fontinfo *info;
	const int32_t *codes;
	const int *glyphs;
	float scale;
} Layout;

/* The width of codes[a..b) in pixels, with their kerning. */
static float width_of(const Layout *l, int a, int b)
{
	float w = 0;
	int k, adv, lsb;
	for (k = a; k < b; k++) {
		stbtt_GetGlyphHMetrics(l->info, l->glyphs[k], &adv, &lsb);
		w += (float)adv * l->scale;
		if (k + 1 < b) w += (float)stbtt_GetGlyphKernAdvance(l->info, l->glyphs[k], l->glyphs[k + 1]) * l->scale;
	}
	return w;
}

/* The lines of codes[0..n) (a '\n' ends one, a space where it passes limit wraps it): each [start, end); their count. */
static int wrap(const Layout *l, int n, float limit, int starts[], int ends[])
{
	int lines = 0, p = 0;
	while (p <= n && lines < KP_CAPTION_LINES) {
		int q = p, start = p;
		while (q < n && l->codes[q] != '\n') q++; /* the paragraph [p, q) */
		while (start < q && lines < KP_CAPTION_LINES) {
			int k, best = -1;
			while (start < q && l->codes[start] == ' ') start++;
			if (start == q) break;
			for (k = start + 1; k <= q; k++) {
				if (k < q && l->codes[k] != ' ') continue;
				if (width_of(l, start, k) > limit && best >= 0) break;
				best = k;
				if (width_of(l, start, k) > limit) break; /* a word longer than the line: on its own */
			}
			starts[lines] = start;
			ends[lines++] = best;
			start = best;
		}
		p = q + 1;
	}
	return lines;
}

/* ring[] = the largest of cover[] within radius of each pixel (a disk), in O(w * h * radius): for a row dy of the
 * disk the largest within its half-width floor(sqrt(r^2 - dy^2)) of the row y + dy, grown[] widened by a column a step
 * (row: w bytes). */
static void outline(const uint8_t *cover, int32_t w, int32_t h, int radius, uint8_t *ring, uint8_t *grown, uint8_t *row)
{
	int k, dy;
	int32_t x, y;
	memcpy(grown, cover, (size_t)w * (size_t)h);
	memset(ring, 0, (size_t)w * (size_t)h);
	for (k = 0; k <= radius; k++) {
		if (k > 0) {
			for (y = 0; y < h; y++) {
				uint8_t *g = grown + (size_t)y * (size_t)w;
				for (x = 0; x < w; x++) {
					uint8_t v = g[x];
					if (x > 0 && g[x - 1] > v) v = g[x - 1];
					if (x + 1 < w && g[x + 1] > v) v = g[x + 1];
					row[x] = v;
				}
				memcpy(g, row, (size_t)w);
			}
		}
		for (dy = -radius; dy <= radius; dy++) {
			int half = radius;
			while (half * half + dy * dy > radius * radius) half--;
			if (half != k) continue;
			for (y = 0; y < h; y++) {
				const uint8_t *g;
				uint8_t *o = ring + (size_t)y * (size_t)w;
				if (y + dy < 0 || y + dy >= h) continue;
				g = grown + (size_t)(y + dy) * (size_t)w;
				for (x = 0; x < w; x++)
					if (g[x] > o[x]) o[x] = g[x];
			}
		}
	}
}

void kp_caption_free(KpCaption *c)
{
	free(c->rgba);
	memset(c, 0, sizeof *c);
}

int kp_caption_render(KpCaption *out, const uint8_t *ttf, size_t ttf_size, const char *text, size_t len, float px,
	int32_t max_w)
{
	stbtt_fontinfo info;
	Layout l;
	int32_t *codes = NULL;
	int *glyphs = NULL, starts[KP_CAPTION_LINES], ends[KP_CAPTION_LINES];
	uint8_t *cover = NULL, *tmp = NULL, *ring = NULL, *grown = NULL, *row = NULL;
	int n = 0, lines, k, ascent, descent, gap, radius, pad;
	float line_h, widths[KP_CAPTION_LINES], widest = 0;
	int32_t w, h, x, y;
	size_t at = 0;
	kp_caption_free(out);
	if (!ttf || ttf_size < 12 || px < 1.0f || max_w < 1 || stbtt_InitFont(&info, ttf, stbtt_GetFontOffsetForIndex(ttf, 0)) == 0)
		return -1;
	codes = (int32_t *)malloc(sizeof *codes * MAX_CODES);
	glyphs = (int *)malloc(sizeof *glyphs * MAX_CODES);
	if (!codes || !glyphs) goto fail;
	while (at < len && n < MAX_CODES) {
		int32_t cp = utf8_next((const unsigned char *)text, len, &at);
		int g;
		if (cp == '\r' || cp == '\t') cp = ' ';
		if (cp < 0 || (cp < 0x20 && cp != '\n')) continue;
		g = cp == '\n' ? 0 : stbtt_FindGlyphIndex(&info, cp);
		if (cp != '\n' && g == 0) continue; /* the font lacks it */
		codes[n] = cp;
		glyphs[n++] = g;
	}
	l.info = &info;
	l.codes = codes;
	l.glyphs = glyphs;
	l.scale = stbtt_ScaleForPixelHeight(&info, px);
	stbtt_GetFontVMetrics(&info, &ascent, &descent, &gap);
	line_h = (float)(ascent - descent + gap) * l.scale;
	radius = (int)(px * OUTLINE_PER_PX + 0.5f);
	if (radius < 1) radius = 1;
	pad = radius + 1;
	lines = wrap(&l, n, (float)(max_w - 2 * pad), starts, ends);
	if (lines == 0) goto fail;
	for (k = 0; k < lines; k++) {
		widths[k] = width_of(&l, starts[k], ends[k]);
		if (widths[k] > widest) widest = widths[k];
	}
	w = (int32_t)ceilf(widest) + 2 * pad;
	h = (int32_t)ceilf(line_h * (float)lines) + 2 * pad;
	if (w > max_w) w = max_w;
	if (!(cover = (uint8_t *)calloc((size_t)w * (size_t)h, 1))) goto fail;
	for (k = 0; k < lines; k++) {
		float pen = (float)pad + ((float)(w - 2 * pad) - widths[k]) / 2.0f;
		int base = pad + (int)floorf(line_h * (float)k + (float)ascent * l.scale + 0.5f), j;
		if (pen < (float)pad) pen = (float)pad;
		for (j = starts[k]; j < ends[k]; j++) {
			int adv, lsb, x0, y0, x1, y1, gw, gh, gx, gy;
			float shift = pen - floorf(pen);
			stbtt_GetGlyphHMetrics(&info, glyphs[j], &adv, &lsb);
			stbtt_GetGlyphBitmapBoxSubpixel(&info, glyphs[j], l.scale, l.scale, shift, 0, &x0, &y0, &x1, &y1);
			gw = x1 - x0;
			gh = y1 - y0;
			if (gw > 0 && gh > 0) {
				uint8_t *more = (uint8_t *)realloc(tmp, (size_t)gw * (size_t)gh);
				if (!more) goto fail;
				tmp = more;
				stbtt_MakeGlyphBitmapSubpixel(&info, tmp, gw, gh, gw, l.scale, l.scale, shift, 0, glyphs[j]);
				for (gy = 0; gy < gh; gy++) {
					int32_t yy = base + y0 + gy;
					if (yy < 0 || yy >= h) continue;
					for (gx = 0; gx < gw; gx++) {
						int32_t xx = (int32_t)floorf(pen) + x0 + gx;
						uint8_t v = tmp[(size_t)gy * (size_t)gw + (size_t)gx];
						uint8_t *d;
						if (xx < 0 || xx >= w) continue;
						d = &cover[(size_t)yy * (size_t)w + (size_t)xx];
						if (v > *d) *d = v;
					}
				}
			}
			pen += (float)adv * l.scale;
			if (j + 1 < ends[k]) pen += (float)stbtt_GetGlyphKernAdvance(&info, glyphs[j], glyphs[j + 1]) * l.scale;
		}
	}
	ring = (uint8_t *)malloc((size_t)w * (size_t)h);
	grown = (uint8_t *)malloc((size_t)w * (size_t)h);
	row = (uint8_t *)malloc((size_t)w);
	if (!ring || !grown || !row || !(out->rgba = (uint8_t *)malloc((size_t)w * (size_t)h * 4))) goto fail;
	outline(cover, w, h, radius, ring, grown, row);
	for (y = 0; y < h; y++) { /* white over the outline */
		for (x = 0; x < w; x++) {
			size_t i = (size_t)y * (size_t)w + (size_t)x;
			float a = (float)cover[i] / 255.0f, o = (float)ring[i] / 255.0f * OUTLINE_ALPHA;
			uint8_t *p = out->rgba + i * 4;
			p[0] = p[1] = p[2] = (uint8_t)(a * 255.0f + 0.5f);
			p[3] = (uint8_t)((a + o * (1.0f - a)) * 255.0f + 0.5f);
		}
	}
	out->w = w;
	out->h = h;
	out->lines = lines;
	free(ring);
	free(grown);
	free(row);
	free(cover);
	free(tmp);
	free(codes);
	free(glyphs);
	return 0;
fail:
	free(ring);
	free(grown);
	free(row);
	free(cover);
	free(tmp);
	free(codes);
	free(glyphs);
	kp_caption_free(out);
	return -1;
}
