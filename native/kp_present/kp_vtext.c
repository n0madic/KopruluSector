/*
 * The vector text of large scales (kp_present.h; ADR 0011, 2026-10-26): the TrueType font's atlas for the five FNT
 * fonts at the window's scale (stb_truetype, third_party/README.md) and the quads of the canvas's vector glyphs, laid
 * out on the original's pens, cells and baselines.
 */
#include "kp_present.h"
#include "stb_truetype.h"

#include <stdlib.h>
#include <string.h>

#define ATLAS_MIN 512
#define ATLAS_MAX 4096
#define PAD 1
#define OPEN 1.0e6f /* a clip side left open */
#define SPACING_MAX 0.2f /* the letter spacing a line may get, over its font's cap height */

/* Windows-1252's 0x80..0x9f (0: no character). */
static const uint16_t CP1252[32] = { 0x20ac, 0, 0x201a, 0x0192, 0x201e, 0x2026, 0x2020, 0x2021, 0x02c6, 0x2030, 0x0160,
	0x2039, 0x0152, 0, 0x017d, 0, 0, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014, 0x02dc, 0x2122, 0x0161,
	0x203a, 0x0153, 0, 0x017e, 0x0178 };

static int codepoint(int byte)
{
	if (byte >= 0x80 && byte < 0xa0) return CP1252[byte - 0x80];
	return byte == 0x7f ? 0 : byte;
}

/* The TrueType font of an FNT font: font16x (slot 4), the wide one, takes the second when there is one. */
static int ttf_of(const KpVText *v, int slot) { return slot == KP_VTEXT_FONTS - 1 && v->ttf_count > 1 ? 1 : 0; }

static const stbtt_fontinfo *info_of(const KpVText *v, int slot) { return (const stbtt_fontinfo *)v->info[ttf_of(v, slot)]; }

int kp_vtext_init(KpVText *v, const uint8_t *const ttf[], const size_t sizes[], int count)
{
	int k, t, x0, y0, x1, y1;
	memset(v, 0, sizeof *v);
	if (count < 1 || count > KP_VTEXT_TTFS) return -1;
	for (t = 0; t < count; t++) {
		stbtt_fontinfo *info;
		if (ttf[t] == NULL || sizes[t] < 12 || !(info = (stbtt_fontinfo *)calloc(1, sizeof *info))) {
			kp_vtext_free(v);
			return -1;
		}
		v->info[t] = info;
		v->ttf_count = t + 1;
		if (stbtt_InitFont(info, ttf[t], stbtt_GetFontOffsetForIndex(ttf[t], 0)) == 0
			|| stbtt_GetCodepointBox(info, 'H', &x0, &y0, &x1, &y1) == 0 || y1 <= 0) {
			kp_vtext_free(v);
			return -1;
		}
		v->cap_units[t] = y1;
	}
	v->size = 1.0;
	for (k = '!'; k < 256; k++) { /* a byte every font has */
		int cp = codepoint(k);
		v->codes[k] = (uint8_t)(cp != 0);
		for (t = 0; t < count; t++) {
			if (stbtt_FindGlyphIndex((const stbtt_fontinfo *)v->info[t], cp) == 0) v->codes[k] = 0;
		}
	}
	return 0;
}

void kp_vtext_free(KpVText *v)
{
	int t;
	for (t = 0; t < KP_VTEXT_TTFS; t++) free(v->info[t]);
	free(v->atlas);
	free(v->quads);
	memset(v, 0, sizeof *v);
}

int kp_vtext_active(int mode, double scale)
{
	return mode == KP_VTEXT_VECTOR || (mode == KP_VTEXT_AUTO && scale >= KP_VTEXT_AUTO_SCALE - 1e-9);
}

/* The glyphs' bitmap boxes at the fonts' pixel scales, packed in shelves w wide: the atlas height, or -1 when it
 * passes the largest atlas. */
static int32_t pack(KpVText *v, const float px[KP_VTEXT_FONTS], int32_t w)
{
	int32_t x = PAD, y = PAD, row = 0;
	int f, k;
	for (f = 0; f < KP_VTEXT_FONTS; f++) {
		const stbtt_fontinfo *info = info_of(v, f);
		for (k = 0; k < 256; k++) {
			KpVTextGlyph *g = &v->glyphs[f][k];
			int cp = codepoint(k), x0, y0, x1, y1, adv, lsb;
			memset(g, 0, sizeof *g);
			if (px[f] <= 0 || !v->codes[k]) continue;
			stbtt_GetCodepointHMetrics(info, cp, &adv, &lsb);
			stbtt_GetCodepointBitmapBox(info, cp, px[f], px[f], &x0, &y0, &x1, &y1);
			g->ok = 1;
			g->advance = (float)adv * px[f];
			g->left = (float)x0;
			g->top = (float)y0;
			if (x1 <= x0 || y1 <= y0) continue;
			if (x + (x1 - x0) + PAD > w) {
				x = PAD;
				y += row + PAD;
				row = 0;
			}
			if (x1 - x0 + 2 * PAD > w) return -1;
			g->x = (int16_t)x;
			g->y = (int16_t)y;
			g->w = (int16_t)(x1 - x0);
			g->h = (int16_t)(y1 - y0);
			x += g->w + PAD;
			if (g->h > row) row = g->h;
			if (y + row + PAD > ATLAS_MAX) return -1;
		}
	}
	return y + row + PAD;
}

int kp_vtext_atlas(KpVText *v, const uint8_t *const fonts[KP_VTEXT_FONTS], const size_t sizes[KP_VTEXT_FONTS],
	double scale)
{
	float px[KP_VTEXT_FONTS];
	int32_t w, h = -1;
	int f, k;
	if (v->ttf_count == 0 || scale <= 0) return -1;
	if (scale == v->scale && v->size == v->built_size && memcmp(fonts, v->fonts, sizeof v->fonts) == 0) {
		return v->atlas ? 0 : -1;
	}
	v->scale = scale;
	v->built_size = v->size;
	memcpy(v->fonts, fonts, sizeof v->fonts);
	free(v->atlas);
	v->atlas = NULL;
	for (f = 0; f < KP_VTEXT_FONTS; f++) {
		kp_ui_font_cap(fonts[f], sizes[f], &v->cap_top[f], &v->cap_height[f]);
		px[f] = v->cap_height[f] > 0 ? (float)(v->cap_height[f] * scale * v->size / v->cap_units[ttf_of(v, f)]) : 0.0f;
		v->px[f] = px[f];
	}
	for (w = ATLAS_MIN; w <= ATLAS_MAX; w *= 2) {
		h = pack(v, px, w);
		if (h >= 0 && h <= w) break;
	}
	if (h < 0 || w > ATLAS_MAX) return -1;
	v->atlas = (uint8_t *)calloc((size_t)w * (size_t)h, 1);
	if (v->atlas == NULL) return -1;
	v->atlas_w = w;
	v->atlas_h = h;
	for (f = 0; f < KP_VTEXT_FONTS; f++) {
		for (k = 0; k < 256; k++) {
			const KpVTextGlyph *g = &v->glyphs[f][k];
			if (g->w <= 0) continue;
			stbtt_MakeCodepointBitmap(info_of(v, f), v->atlas + (size_t)g->y * (size_t)w + g->x, g->w,
				g->h, w, px[f], px[f], codepoint(k));
		}
	}
	v->atlas_changed = 1;
	return 0;
}

static int font_slot(const KpVText *v, const uint8_t *font)
{
	int f;
	for (f = 0; f < KP_VTEXT_FONTS; f++) {
		if (v->fonts[f] == font) return f;
	}
	return -1;
}

static int quad(KpVText *v, const float rect[4], const KpVTextGlyph *g, const float clip[4], uint8_t colour)
{
	float *q;
	if (v->quad_count == v->quad_capacity) {
		int32_t n = v->quad_capacity ? 2 * v->quad_capacity : 512;
		float *grown = (float *)realloc(v->quads, (size_t)n * KP_VTEXT_QUAD_FLOATS * sizeof *grown);
		if (grown == NULL) return -1;
		v->quads = grown;
		v->quad_capacity = n;
	}
	q = v->quads + (size_t)v->quad_count++ * KP_VTEXT_QUAD_FLOATS;
	memcpy(q, rect, 4 * sizeof *q);
	q[4] = (float)g->x / (float)v->atlas_w;
	q[5] = (float)g->y / (float)v->atlas_h;
	q[6] = (float)(g->x + g->w) / (float)v->atlas_w;
	q[7] = (float)(g->y + g->h) / (float)v->atlas_h;
	memcpy(q + 8, clip, 4 * sizeof *q);
	q[12] = (float)colour;
	q[13] = q[14] = q[15] = 0.0f;
	return 0;
}

/* A canvas glyph's place in its line: the draw that placed it, its font, its row and its pen. */
typedef struct {
	int32_t seq, font, y, x, index;
} LineKey;

static int by_line(const void *a, const void *b)
{
	const LineKey *p = (const LineKey *)a, *q = (const LineKey *)b;
	if (p->seq != q->seq) return p->seq < q->seq ? -1 : 1;
	if (p->font != q->font) return p->font < q->font ? -1 : 1;
	if (p->y != q->y) return p->y < q->y ? -1 : 1;
	if (p->x != q->x) return p->x < q->x ? -1 : 1;
	return p->index < q->index ? -1 : p->index > q->index;
}

/* The glyph u's quads (its shadow first) with its bitmap's left edge at window x and narrowed by `narrow`. */
static int glyph_quads(KpVText *v, const KpUiCanvasGlyph *u, const KpVTextGlyph *g, int f, float x, float narrow,
	float s)
{
	float rect[4], clip[4];
	rect[0] = x / s;
	rect[1] = ((float)(u->y + v->cap_top[f] + v->cap_height[f]) * s + g->top) / s;
	rect[2] = rect[0] + (float)g->w * narrow / s;
	rect[3] = rect[1] + (float)g->h / s;
	/* a covered glyph is cut where its uncovered pixels end short of its bitmap's edge */
	clip[0] = clip[1] = -OPEN;
	clip[2] = clip[3] = OPEN;
	if (!u->whole) {
		if (u->clip[0] > u->x + u->box[2]) clip[0] = (float)u->clip[0];
		if (u->clip[1] > u->y + u->box[3]) clip[1] = (float)u->clip[1];
		if (u->clip[2] < u->x + u->box[2] + u->box[0]) clip[2] = (float)u->clip[2];
		if (u->clip[3] < u->y + u->box[3] + u->box[1]) clip[3] = (float)u->clip[3];
	}
	if (u->has_shadow) {
		float r[4], shadow = (s * 0.5f < 1.0f ? 1.0f : s * 0.5f) / s;
		r[0] = rect[0] + shadow;
		r[1] = rect[1] + shadow;
		r[2] = rect[2] + shadow;
		r[3] = rect[3] + shadow;
		if (quad(v, r, g, clip, u->shadow) != 0) return -1;
	}
	return quad(v, rect, g, clip, u->fill);
}

/* The vector advance of the glyph at keys[k] in window pixels, with the kerning before the next glyph of its word. */
static float vector_advance(const KpVText *v, const KpUiCanvas *c, const LineKey *keys, int32_t k, int32_t end)
{
	const KpUiCanvasGlyph *u = &c->glyphs[keys[k].index];
	const KpVTextGlyph *g = &v->glyphs[keys[k].font][u->code];
	float a;
	if (!g->ok) return (float)u->advance * (float)v->scale;
	a = g->advance;
	if (k + 1 < end) {
		a += (float)stbtt_GetCodepointKernAdvance(info_of(v, keys[k].font), codepoint(u->code),
			codepoint(c->glyphs[keys[k + 1].index].code)) * v->px[keys[k].font];
	}
	return a;
}

/* The end of the word starting at keys[i] inside its line ending at keys[end]: the glyphs whose cells follow. */
static int32_t word_end(const KpUiCanvas *c, const LineKey *keys, int32_t i, int32_t end)
{
	int32_t j = i + 1;
	while (j < end) {
		const KpUiCanvasGlyph *prev = &c->glyphs[keys[j - 1].index];
		if (keys[j].x != prev->x + prev->advance) break;
		j++;
	}
	return j;
}

int32_t kp_vtext_quads(KpVText *v, const KpUiCanvas *c, double scale)
{
	LineKey *keys;
	int32_t n = 0, line, end, i, j, k;
	float s = (float)scale;
	v->quad_count = 0;
	if (v->atlas == NULL || scale <= 0 || c->glyph_count <= 0) return 0;
	keys = (LineKey *)malloc((size_t)c->glyph_count * sizeof *keys);
	if (keys == NULL) return 0;
	for (k = 0; k < c->glyph_count; k++) {
		const KpUiCanvasGlyph *u = &c->glyphs[k];
		int f = font_slot(v, u->font);
		if (f < 0) continue;
		keys[n].seq = u->seq;
		keys[n].font = f;
		keys[n].y = u->y;
		keys[n].x = u->x;
		keys[n++].index = k;
	}
	qsort(keys, (size_t)n, sizeof *keys, by_line);
	/* a line: the glyphs of one draw, font and row; a word: those of its glyphs whose cells follow each other. Each
	 * word stays centred in its cells; the line's words share one letter spacing that fills their cells as the FNT
	 * font's glyphs do (up to SPACING_MAX), and a word wider than its cells is narrowed */
	for (line = 0; line < n; line = end) {
		float natural = 0.0f, cells = 0.0f, spacing = 0.0f;
		int32_t gaps = 0;
		for (end = line + 1; end < n && keys[end].seq == keys[line].seq && keys[end].font == keys[line].font
			&& keys[end].y == keys[line].y; end++) {
		}
		for (i = line; i < end; i = j) {
			const KpUiCanvasGlyph *last;
			j = word_end(c, keys, i, end);
			last = &c->glyphs[keys[j - 1].index];
			cells += (float)(last->x + last->advance - keys[i].x) * s;
			for (k = i; k < j; k++) natural += vector_advance(v, c, keys, k, j);
			gaps += j - i - 1;
		}
		if (gaps > 0 && cells > natural) spacing = (cells - natural) / (float)gaps;
		if (spacing > SPACING_MAX * (float)v->cap_height[keys[line].font] * s * (float)v->size) {
			spacing = SPACING_MAX * (float)v->cap_height[keys[line].font] * s * (float)v->size;
		}
		for (i = line; i < end; i = j) {
			const KpUiCanvasGlyph *last;
			float width = 0.0f, word, narrow = 1.0f, pen;
			j = word_end(c, keys, i, end);
			last = &c->glyphs[keys[j - 1].index];
			word = (float)(last->x + last->advance - keys[i].x) * s;
			for (k = i; k < j; k++) width += vector_advance(v, c, keys, k, j) + (k + 1 < j ? spacing : 0.0f);
			if (width > word && width > 0) narrow = word / width;
			pen = (float)keys[i].x * s + (word - width * narrow) / 2.0f;
			for (k = i; k < j; k++) {
				const KpUiCanvasGlyph *u = &c->glyphs[keys[k].index];
				const KpVTextGlyph *g = &v->glyphs[keys[k].font][u->code];
				if (g->ok && g->w > 0 && glyph_quads(v, u, g, keys[k].font, pen + g->left * narrow, narrow, s) != 0) {
					free(keys);
					return v->quad_count;
				}
				pen += (vector_advance(v, c, keys, k, j) + spacing) * narrow;
			}
		}
	}
	free(keys);
	return v->quad_count;
}
