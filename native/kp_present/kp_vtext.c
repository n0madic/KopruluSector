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

/* Windows-1252's 0x80..0x9f (0: no character). */
static const uint16_t CP1252[32] = { 0x20ac, 0, 0x201a, 0x0192, 0x201e, 0x2026, 0x2020, 0x2021, 0x02c6, 0x2030, 0x0160,
	0x2039, 0x0152, 0, 0x017d, 0, 0, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014, 0x02dc, 0x2122, 0x0161,
	0x203a, 0x0153, 0, 0x017e, 0x0178 };

static int codepoint(int byte)
{
	if (byte >= 0x80 && byte < 0xa0) return CP1252[byte - 0x80];
	return byte == 0x7f ? 0 : byte;
}

int kp_vtext_init(KpVText *v, const uint8_t *ttf, size_t size)
{
	stbtt_fontinfo *info;
	int k, x0, y0, x1, y1;
	memset(v, 0, sizeof *v);
	if (ttf == NULL || size < 12) return -1;
	info = (stbtt_fontinfo *)calloc(1, sizeof *info);
	if (info == NULL) return -1;
	if (stbtt_InitFont(info, ttf, stbtt_GetFontOffsetForIndex(ttf, 0)) == 0
		|| stbtt_GetCodepointBox(info, 'H', &x0, &y0, &x1, &y1) == 0 || y1 <= 0) {
		free(info);
		return -1;
	}
	v->info = info;
	v->ttf = ttf;
	v->cap_units = y1;
	for (k = '!'; k < 256; k++) {
		int cp = codepoint(k);
		v->codes[k] = (uint8_t)(cp != 0 && stbtt_FindGlyphIndex(info, cp) != 0);
	}
	return 0;
}

void kp_vtext_free(KpVText *v)
{
	free(v->info);
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
	const stbtt_fontinfo *info = (const stbtt_fontinfo *)v->info;
	int32_t x = PAD, y = PAD, row = 0;
	int f, k;
	for (f = 0; f < KP_VTEXT_FONTS; f++) {
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
	if (v->info == NULL || scale <= 0) return -1;
	if (scale == v->scale && memcmp(fonts, v->fonts, sizeof v->fonts) == 0) return v->atlas ? 0 : -1;
	v->scale = scale;
	memcpy(v->fonts, fonts, sizeof v->fonts);
	free(v->atlas);
	v->atlas = NULL;
	for (f = 0; f < KP_VTEXT_FONTS; f++) {
		kp_ui_font_cap(fonts[f], sizes[f], &v->cap_top[f], &v->cap_height[f]);
		px[f] = v->cap_height[f] > 0 ? (float)(v->cap_height[f] * scale / v->cap_units) : 0.0f;
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
			stbtt_MakeCodepointBitmap((const stbtt_fontinfo *)v->info, v->atlas + (size_t)g->y * (size_t)w + g->x, g->w,
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

int32_t kp_vtext_quads(KpVText *v, const KpUiCanvas *c, double scale)
{
	int32_t k;
	float s = (float)scale;
	v->quad_count = 0;
	if (v->atlas == NULL || scale <= 0) return 0;
	for (k = 0; k < c->glyph_count; k++) {
		const KpUiCanvasGlyph *u = &c->glyphs[k];
		int f = font_slot(v, u->font);
		const KpVTextGlyph *g;
		float cell, narrow = 1.0f, offset, rect[4], clip[4], shadow;
		if (f < 0) continue;
		g = &v->glyphs[f][u->code];
		if (!g->ok || g->w <= 0) continue;
		cell = (float)u->advance * s;
		if (g->advance > cell && g->advance > 0) narrow = cell / g->advance;
		offset = (cell - g->advance * narrow) / 2.0f;
		rect[0] = ((float)u->x * s + offset + g->left * narrow) / s;
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
			float r[4];
			shadow = (s * 0.5f < 1.0f ? 1.0f : s * 0.5f) / s;
			r[0] = rect[0] + shadow;
			r[1] = rect[1] + shadow;
			r[2] = rect[2] + shadow;
			r[3] = rect[3] + shadow;
			if (quad(v, r, g, clip, u->shadow) != 0) break;
		}
		if (quad(v, rect, g, clip, u->fill) != 0) break;
	}
	return v->quad_count;
}
