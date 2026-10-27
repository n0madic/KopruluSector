/* The canvas of kp_ui (kp_ui.h, ADR 0011): the panels, the console art and the fillers composed for the logical
 * screen, two bytes per pixel (palette index, opacity), with the rectangle that changed. */
#include "kp_ui.h"
#include "kp_game.h"
#include "kp_int.h"

#include <stdlib.h>
#include <string.h>

#define OPAQUE 255
#define FLAG_BACKDROP 0x10000000u   /* a dialog composited onto the backdrop with its flags (0x4c5630) */
#define GLYPH_AGE 120               /* composes a glyph stays in the log without being found live */
#define OWNER_SEQ 0x7fffu           /* the owner plane's draw */
#define OWNER_TINT 0x8000u          /* the owner plane's flag: a translucent dialog blended over the pixel since */

/* Writes a pixel of the frame being composed. */
static void put(KpUiCanvas *c, int32_t x, int32_t y, uint8_t index, uint8_t alpha)
{
	uint8_t *p;
	size_t at;
	if (x < 0 || y < 0 || x >= c->w || y >= c->h) return;
	at = (size_t)y * (size_t)c->w + (size_t)x;
	p = c->next + 2 * at;
	p[0] = index;
	p[1] = alpha;
	if (c->text) c->owner[at] = c->seq;
	if (c->ids_next) c->ids_next[at] = c->item;
}

/* Writes a pixel a translucent dialog blended: a vector glyph under it stays, in the blended colours. */
static void put_tint(KpUiCanvas *c, int32_t x, int32_t y, uint8_t index, uint8_t alpha)
{
	uint8_t *p;
	size_t at;
	if (x < 0 || y < 0 || x >= c->w || y >= c->h) return;
	at = (size_t)y * (size_t)c->w + (size_t)x;
	p = c->next + 2 * at;
	p[0] = index;
	p[1] = alpha;
	if (c->text) c->owner[at] |= OWNER_TINT;
	if (c->ids_next) c->ids_next[at] = c->item;
}

/* --- the vector text (not the original's: the text of large scales, ADR 0011) --- */

void kp_ui_set_vector_text(KpUi *ui, int record, int on, const uint8_t *codes)
{
	int k, top, height;
	ui->text.log = record ? &ui->glyph_log : NULL;
	if (!record) kp_ui_glyph_log_free(&ui->glyph_log);
	ui->vector_text = record && on;
	if (codes != NULL) memcpy(ui->vector_codes, codes, sizeof ui->vector_codes);
	for (k = 0; k < KP_UI_FNT_FONTS; k++) { /* a font without a cap height has no vector glyphs */
		kp_ui_font_cap(ui->font_files[k], ui->font_file_sizes[k], &top, &height);
		ui->vector_font_ok[k] = (uint8_t)(height > 0);
	}
}

int kp_ui_vector_fonts(const KpUi *ui, const uint8_t *const **files, const size_t **sizes)
{
	*files = ui->vector_fonts;
	*sizes = ui->vector_font_sizes;
	return ui->vector_font_count;
}

static int vector_font(const KpUi *ui, const uint8_t *font)
{
	int k;
	for (k = 0; k < KP_UI_FNT_FONTS; k++) {
		if (ui->font_files[k] == font) return ui->vector_font_ok[k];
	}
	return 0;
}

/* Room for n listed glyphs, and for the twice as many kp_ui_canvas_text_end may make of them. */
static int text_room(KpUiCanvas *c, int32_t n)
{
	if (n > c->glyph_capacity) {
		int32_t cap = c->glyph_capacity ? 2 * c->glyph_capacity : 256;
		KpUiCanvasGlyph *grown = (KpUiCanvasGlyph *)realloc(c->glyphs, (size_t)cap * sizeof *grown);
		if (grown == NULL) return -1;
		c->glyphs = grown;
		c->glyph_capacity = cap;
	}
	if (2 * n > c->spare_capacity) {
		int32_t cap = 2 * c->glyph_capacity;
		KpUiCanvasGlyph *grown = (KpUiCanvasGlyph *)realloc(c->spare, (size_t)cap * sizeof *grown);
		if (grown == NULL) return -1;
		c->spare = grown;
		c->spare_capacity = cap;
	}
	return 0;
}

/* The pixels to compose of surface s, its top-left cw x ch placed at canvas (x, y) by the draw c->seq. Its live
 * glyphs stay in the log (seen); with the vector text on, those of a vector font with a pixel in that crop are taken
 * out, in a copy where their pixels hold what lay under them, and listed for the render (`list`; else only the copy).
 * Else the surface's own pixels. */
static const uint8_t *text_source(KpUi *ui, KpUiCanvas *c, const KpUiSurface *s, int32_t x, int32_t y, int32_t cw,
	int32_t ch, int list)
{
	KpUiGlyphLog *log = &ui->glyph_log;
	const KpUiGlyphSurface *sf;
	size_t size = (size_t)s->w * (size_t)s->h;
	uint32_t k;
	int copied = 0;
	if (ui->text.log == NULL || s->pixels == NULL || s->w <= 0 || s->h <= 0
		|| (sf = kp_ui_glyph_surface(log, s->pixels, s->w)) == NULL) {
		return s->pixels;
	}
	for (k = sf->head; k != KP_UI_GLYPH_NONE; k = log->glyphs[k].next) {
		KpUiGlyph *g = &log->glyphs[k];
		KpUiCanvasGlyph *v;
		uint32_t j;
		int inside = 0;
		if (!kp_ui_glyph_live(log, g, s)) continue;
		g->seen = log->serial;
		if (!ui->vector_text || !c->text || g->wrapped || !ui->vector_codes[g->code] || !vector_font(ui, g->font)) {
			continue;
		}
		for (j = g->first; j < g->first + g->count && !inside; j++) {
			inside = log->offsets[j] % s->w < cw && log->offsets[j] / s->w < ch;
		}
		if (!inside) continue;
		if (list && text_room(c, c->glyph_count + 1) != 0) break; /* out of memory: the rest stay bitmaps */
		if (!copied) {
			if (c->source_size < size) {
				uint8_t *grown = (uint8_t *)realloc(c->source, size);
				if (grown == NULL) return s->pixels;
				c->source = grown;
				c->source_size = size;
			}
			memcpy(c->source, s->pixels, size);
			copied = 1;
		}
		for (j = g->first; j < g->first + g->count; j++) c->source[log->offsets[j]] = log->under[j];
		if (!list) continue;
		v = &c->glyphs[c->glyph_count++];
		memset(v, 0, sizeof *v);
		v->x = x + g->pen_x;
		v->y = y + g->pen_y;
		v->font = g->font;
		v->code = g->code;
		v->advance = g->advance;
		v->fill = g->fill;
		v->shadow = g->shadow;
		v->has_shadow = g->has_shadow;
		memcpy(v->box, g->box, sizeof v->box);
		v->seq = c->seq;
		v->glyph = k;
	}
	return copied ? c->source : s->pixels;
}

/* The glyphs the draw c->seq listed from index `first`, of a surface at canvas (x, y), own their pixels' places in its
 * top-left cw x ch (a transparent under kept no pixel). */
static void text_own(KpUi *ui, KpUiCanvas *c, int32_t first, int32_t x, int32_t y, int32_t cw, int32_t ch)
{
	const KpUiGlyphLog *log = &ui->glyph_log;
	int32_t k;
	for (k = first; k < c->glyph_count; k++) {
		const KpUiGlyph *g = &log->glyphs[c->glyphs[k].glyph];
		int32_t stride = log->surfaces[g->surface].stride;
		uint32_t j;
		for (j = g->first; j < g->first + g->count; j++) {
			int32_t sx = log->offsets[j] % stride, sy = log->offsets[j] / stride, cx = x + sx, cy = y + sy;
			if (sx < cw && sy < ch && cx >= 0 && cy >= 0 && cx < c->w && cy < c->h) {
				c->owner[(size_t)cy * (size_t)c->w + (size_t)cx] = c->seq;
			}
		}
	}
}

void kp_ui_canvas_text_end(KpUi *ui, KpUiCanvas *c)
{
	KpUiGlyphLog *log;
	KpUiCanvasGlyph *out;
	int32_t k, kept = 0, capacity;
	if (ui == NULL) return;
	log = &ui->glyph_log;
	/* each glyph's pixels no later draw covers, those a translucent dialog blended over apart: a glyph covered whole
	 * is dropped, one covered in part is clipped to its uncovered pixels, and the blended ones are a glyph of their own
	 * in the blended colours */
	for (k = 0; k < c->glyph_count; k++) {
		const KpUiCanvasGlyph *v = &c->glyphs[k];
		const KpUiGlyph *g = &log->glyphs[v->glyph];
		int32_t stride = log->surfaces[g->surface].stride, dx = v->x - g->pen_x, dy = v->y - g->pen_y;
		int32_t box[2][4], part;
		uint32_t j, seen[2] = { 0, 0 };
		for (part = 0; part < 2; part++) {
			box[part][0] = c->w;
			box[part][1] = c->h;
			box[part][2] = box[part][3] = 0;
		}
		for (j = g->first; j < g->first + g->count; j++) {
			int32_t cx = dx + log->offsets[j] % stride, cy = dy + log->offsets[j] / stride;
			uint16_t owner;
			if (cx < 0 || cy < 0 || cx >= c->w || cy >= c->h) continue;
			owner = c->owner[(size_t)cy * (size_t)c->w + (size_t)cx];
			if ((owner & OWNER_SEQ) != v->seq) continue;
			part = (owner & OWNER_TINT) != 0;
			seen[part]++;
			if (cx < box[part][0]) box[part][0] = cx;
			if (cy < box[part][1]) box[part][1] = cy;
			if (cx + 1 > box[part][2]) box[part][2] = cx + 1;
			if (cy + 1 > box[part][3]) box[part][3] = cy + 1;
		}
		for (part = 0; part < 2; part++) {
			KpUiCanvasGlyph *o;
			if (seen[part] == 0) continue;
			o = &c->spare[kept++];
			*o = *v;
			o->glyph = 0; /* the log's entry only while the frame is composed */
			o->whole = !g->part && seen[part] == g->count;
			if (g->part && seen[part] == g->count) {
				o->clip[0] = g->clip[0] + dx;
				o->clip[1] = g->clip[1] + dy;
				o->clip[2] = g->clip[2] + dx;
				o->clip[3] = g->clip[3] + dy;
			} else {
				memcpy(o->clip, box[part], sizeof o->clip);
				if (g->part) {
					if (g->clip[0] + dx > o->clip[0]) o->clip[0] = g->clip[0] + dx;
					if (g->clip[1] + dy > o->clip[1]) o->clip[1] = g->clip[1] + dy;
					if (g->clip[2] + dx < o->clip[2]) o->clip[2] = g->clip[2] + dx;
					if (g->clip[3] + dy < o->clip[3]) o->clip[3] = g->clip[3] + dy;
				}
			}
			if (part) {
				o->fill = c->tint_row[o->fill];
				o->shadow = c->tint_row[o->shadow];
			}
		}
	}
	out = c->spare;
	capacity = c->spare_capacity;
	c->spare = c->glyphs;
	c->spare_capacity = c->glyph_capacity;
	c->glyphs = out;
	c->glyph_capacity = capacity;
	c->glyph_count = kept;
	log->serial++;
	kp_ui_glyph_log_prune(log, GLYPH_AGE);
}

int kp_ui_canvas_resize(KpUiCanvas *c, int32_t w, int32_t h)
{
	uint8_t *pixels, *next;
	uint16_t *owner;
	if (w < 1 || h < 1) return -1;
	pixels = (uint8_t *)calloc((size_t)w * (size_t)h, 2);
	next = (uint8_t *)calloc((size_t)w * (size_t)h, 2);
	owner = (uint16_t *)calloc((size_t)w * (size_t)h, sizeof *owner);
	if (pixels == NULL || next == NULL || owner == NULL) {
		free(pixels);
		free(next);
		free(owner);
		return -1;
	}
	free(c->pixels);
	free(c->next);
	free(c->owner);
	c->pixels = pixels;
	c->next = next;
	c->owner = owner;
	c->w = w;
	c->h = h;
	if (c->ids && kp_ui_canvas_hd(c, 1) != 0) return -1; /* the item planes at the new size */
	c->dirty[0] = c->dirty[1] = 0;
	c->dirty[2] = w;
	c->dirty[3] = h;
	return 0;
}

void kp_ui_canvas_free(KpUiCanvas *c)
{
	free(c->pixels);
	free(c->next);
	free(c->owner);
	free(c->glyphs);
	free(c->spare);
	free(c->source);
	free(c->scratch);
	free(c->ids);
	free(c->ids_next);
	memset(c, 0, sizeof *c);
}

int kp_ui_canvas_hd(KpUiCanvas *c, int on)
{
	size_t n = (size_t)c->w * (size_t)c->h;
	free(c->ids);
	free(c->ids_next);
	c->ids = c->ids_next = NULL;
	c->item = 0;
	if (!on || n == 0) return 0;
	c->ids = (uint16_t *)calloc(n, sizeof *c->ids);
	c->ids_next = (uint16_t *)calloc(n, sizeof *c->ids_next);
	if (c->ids == NULL || c->ids_next == NULL) {
		free(c->ids);
		free(c->ids_next);
		c->ids = c->ids_next = NULL;
		return -1;
	}
	c->dirty[0] = c->dirty[1] = 0;
	c->dirty[2] = c->w;
	c->dirty[3] = c->h;
	return 0;
}

void kp_ui_canvas_item(KpUiCanvas *c, uint16_t item) { c->item = item; }

uint16_t kp_ui_canvas_add_item(KpUiCanvas *c, const KpUiCanvasItem *item)
{
	if (c->ids_next == NULL || c->item_count >= KP_UI_CANVAS_ITEMS) return 0;
	c->items[++c->item_count] = *item;
	return (uint16_t)c->item_count;
}

void kp_ui_canvas_begin(const KpUi *ui, KpUiCanvas *c)
{
	c->text = ui != NULL && ui->vector_text && c->owner != NULL;
	if (c->next != NULL) memset(c->next, 0, (size_t)c->w * (size_t)c->h * 2);
	if (c->text) memset(c->owner, 0, (size_t)c->w * (size_t)c->h * sizeof *c->owner);
	if (c->ids_next) memset(c->ids_next, 0, (size_t)c->w * (size_t)c->h * sizeof *c->ids_next);
	c->item = 0;
	c->item_count = 0;
	c->tint_valid = 0;
	c->blends = 0;
	c->seq = 0;
	c->glyph_count = 0;
}

int kp_ui_canvas_end(KpUiCanvas *c)
{
	size_t row = (size_t)c->w * 2;
	int32_t y, x0 = c->w, x1 = 0, y0 = c->h, y1 = 0;
	for (y = 0; y < c->h; y++) {
		const uint8_t *a = c->pixels + (size_t)y * row, *b = c->next + (size_t)y * row;
		uint16_t *ia = c->ids ? c->ids + (size_t)y * (size_t)c->w : NULL;
		const uint16_t *ib = c->ids ? c->ids_next + (size_t)y * (size_t)c->w : NULL;
		int32_t l, r;
		if (memcmp(a, b, row) == 0 && (!ia || memcmp(ia, ib, (size_t)c->w * sizeof *ia) == 0)) continue;
		for (l = 0; l < c->w && a[2 * l] == b[2 * l] && a[2 * l + 1] == b[2 * l + 1] && (!ia || ia[l] == ib[l]); l++) {}
		for (r = c->w - 1; r > l && a[2 * r] == b[2 * r] && a[2 * r + 1] == b[2 * r + 1] && (!ia || ia[r] == ib[r]); r--) {}
		if (ia) memcpy(ia, ib, (size_t)c->w * sizeof *ia);
		if (l < x0) x0 = l;
		if (r + 1 > x1) x1 = r + 1;
		if (y < y0) y0 = y;
		y1 = y + 1;
		memcpy(c->pixels + (size_t)y * row, b, row);
	}
	if (y0 >= y1) return 0;
	c->dirty[0] = x0;
	c->dirty[1] = y0;
	c->dirty[2] = x1;
	c->dirty[3] = y1;
	return 1;
}

void kp_ui_canvas_console(KpUi *ui, KpUiCanvas *c, int32_t x, int32_t y, int32_t x0, int32_t x1)
{
	const KpUiSurface *s = &ui->console;
	int32_t i, j;
	c->seq++;
	if (s->pixels == NULL) return;
	if (x0 < 0) x0 = 0;
	if (x1 > s->w) x1 = s->w;
	for (j = 0; j < s->h; j++) {
		for (i = x0; i < x1; i++) {
			uint8_t v = s->pixels[(size_t)j * (size_t)s->w + (size_t)i];
			if (v != 0) put(c, x + i, y + j, v, OPAQUE); /* the art's holes keep what lies under (a backdrop dialog) */
		}
	}
}

void kp_ui_canvas_filler(KpUi *ui, KpUiCanvas *c, int32_t x, int32_t y, int32_t w, int32_t column)
{
	const KpUiSurface *s = &ui->console;
	int32_t i, j;
	c->seq++;
	if (s->pixels == NULL || column < 0 || column >= s->w) return;
	for (j = 0; j < s->h; j++) {
		uint8_t v = s->pixels[(size_t)j * (size_t)s->w + (size_t)column];
		if (v == 0) continue;
		for (i = 0; i < w; i++) put(c, x + i, y + j, v, OPAQUE);
	}
}

void kp_ui_canvas_fill(KpUiCanvas *c, int32_t x, int32_t y, int32_t w, int32_t h, uint8_t index)
{
	int32_t i, j;
	c->seq++;
	for (j = 0; j < h; j++) {
		for (i = 0; i < w; i++) put(c, x + i, y + j, index, OPAQUE);
	}
}

#define SHADE_SHIFT 2 /* the margins' brightness: a quarter */

/* The glue palette's indices darkened to a quarter of their brightness (the nearest colour, the lowest index on a
 * tie). */
static void build_shade(const KpUi *ui, KpUiCanvas *c)
{
	const uint8_t *p = ui->glue.palette;
	int i, j;
	for (i = 0; i < 256; i++) {
		int32_t r = p[4 * i] >> SHADE_SHIFT, g = p[4 * i + 1] >> SHADE_SHIFT, b = p[4 * i + 2] >> SHADE_SHIFT, best = 0;
		int32_t best_d = -1;
		for (j = 0; j < 256; j++) {
			int32_t dr = p[4 * j] - r, dg = p[4 * j + 1] - g, db = p[4 * j + 2] - b, d = dr * dr + dg * dg + db * db;
			if (best_d < 0 || d < best_d) {
				best = j;
				best_d = d;
			}
		}
		c->shade[i] = (uint8_t)best;
	}
	c->shade_serial = ui->glue.palette_serial + 1;
}

void kp_ui_canvas_margins(KpUi *ui, KpUiCanvas *c, int32_t dialog, int32_t x, int32_t y)
{
	const KpUiControl *d = &ui->controls[dialog];
	const uint8_t *src;
	int32_t i, j, w = d->bitmap.w, h = d->bitmap.h;
	c->seq++;
	if (d->bitmap.pixels == NULL || w <= 0 || h <= 0) return;
	src = text_source(ui, c, &d->bitmap, x, y, w, h, 0); /* the edges without the vector glyphs' bitmaps */
	if (c->shade_serial != ui->glue.palette_serial + 1) build_shade(ui, c);
	for (j = 0; j < c->h; j++) {
		int32_t sy = j - y < 0 ? 0 : (j - y >= h ? h - 1 : j - y);
		int inside_row = j >= y && j < y + h;
		for (i = 0; i < c->w; i++) {
			int32_t sx;
			if (inside_row && i >= x && i < x + w) {
				i = x + w - 1; /* the screen itself is the dialog's */
				continue;
			}
			sx = i - x < 0 ? 0 : (i - x >= w ? w - 1 : i - x);
			put(c, i, j, c->shade[src[(size_t)sy * (size_t)w + (size_t)sx]], OPAQUE);
		}
	}
}

void kp_ui_canvas_layer(KpUi *ui, KpUiCanvas *c, const KpUiSurface *s, int32_t x, int32_t y)
{
	const uint8_t *src;
	int32_t i, j, first = c->glyph_count;
	c->seq++;
	if (s == NULL || s->pixels == NULL) return;
	src = text_source(ui, c, s, x, y, s->w, s->h, 1);
	for (j = 0; j < s->h; j++) {
		for (i = 0; i < s->w; i++) {
			uint8_t v = src[(size_t)j * (size_t)s->w + (size_t)i];
			if (v != 0) put(c, x + i, y + j, v, KP_UI_CANVAS_OVERLAY_A);
		}
	}
	text_own(ui, c, first, x, y, s->w, s->h);
}

void kp_ui_canvas_tooltip(KpUi *ui, KpUiCanvas *c, int32_t x, int32_t y)
{
	/* TooltipLayerDraw 0x419100: the box's w x h of the tooltip bitmap, opaque (0x4ce720) */
	int32_t tx, ty, w, h, i, j, first = c->glyph_count;
	const KpUiSurface *s = kp_ui_tooltip(ui, &tx, &ty, &w, &h);
	const uint8_t *src;
	c->seq++;
	if (s == NULL) return;
	if (w > s->w) w = s->w;
	if (h > s->h) h = s->h;
	src = text_source(ui, c, s, x, y, w, h, 1); /* the box's glyphs: an earlier tooltip's may lie outside it */
	for (j = 0; j < h; j++) {
		for (i = 0; i < w; i++) {
			put(c, x + i, y + j, src[(size_t)j * (size_t)s->w + (size_t)i], KP_UI_CANVAS_OVERLAY_A);
		}
	}
	text_own(ui, c, first, x, y, w, h);
}

/* A pixel of the game view at canvas (x, y), kept inside its rectangle clip {left, top, right, bottom} exclusive. */
static void put_view(KpUiCanvas *c, const int32_t clip[4], int32_t x, int32_t y, uint8_t index)
{
	if (x >= clip[0] && y >= clip[1] && x < clip[2] && y < clip[3]) put(c, x, y, index, KP_UI_CANVAS_OVERLAY_A);
}

/* DrawRectOutline 0x4ce7d0 of the corners (l, t) and (r, b) inclusive. */
static void outline_view(KpUiCanvas *c, const int32_t clip[4], int32_t l, int32_t t, int32_t r, int32_t b, uint8_t index)
{
	int32_t i;
	for (i = l; i <= r; i++) {
		put_view(c, clip, i, t, index);
		put_view(c, clip, i, b, index);
	}
	for (i = t; i <= b; i++) {
		put_view(c, clip, l, i, index);
		put_view(c, clip, r, i, index);
	}
}

/* A frame of one of the interface's GRPs whose frame rectangle's top-left lies at canvas (x, y), through an identity
 * colour map, index 0 transparent. */
static void grp_frame_view(KpUi *ui, KpUiCanvas *c, const int32_t clip[4], const KpUiFile *f, int frame, int32_t x,
	int32_t y)
{
	KpUiSurface target, *saved;
	uint8_t remap[256];
	int32_t w, h, crop[4], i, j;
	size_t need;
	if (kp_ui_grp_frame_size(f->data, f->size, frame, &w, &h) != 0 || w <= 0 || h <= 0) return;
	need = (size_t)w * (size_t)h;
	if (need > c->scratch_size) {
		uint8_t *p = (uint8_t *)realloc(c->scratch, need);
		if (p == NULL) return;
		c->scratch = p;
		c->scratch_size = need;
	}
	memset(c->scratch, 0, need);
	target.w = w;
	target.h = h;
	target.pixels = c->scratch;
	saved = ui->text.target;
	ui->text.target = &target;
	memcpy(remap, ui->remap, sizeof remap);
	for (i = 0; i < 256; i++) ui->remap[i] = (uint8_t)i;
	crop[0] = 0;
	crop[1] = 0;
	crop[2] = w;
	crop[3] = h;
	kp_ui_draw_grp_rle(ui, 0, 0, f->data, f->size, frame, crop);
	memcpy(ui->remap, remap, sizeof remap);
	ui->text.target = saved;
	for (j = 0; j < h; j++) {
		for (i = 0; i < w; i++) {
			uint8_t v = c->scratch[(size_t)j * (size_t)w + (size_t)i];
			if (v != 0) put_view(c, clip, x + i, y + j, v);
		}
	}
}

#define MARKER_GRP "unit\\thingy\\blackx.grp" /* images.dat 445, the marker sprite 318's image */
#define COLOUR_DRAG_BOX 18                     /* 0x6d237a */

int kp_ui_game_view_active(const KpUi *ui)
{
	const struct KpGame *g = ui->game;
	if (g == NULL) return 0;
	return (ui->input.box && ui->input.mouse_state != 0) || kp_scalar(g, KP_ORDER_MARKER) != 0
		|| ui->placement.shown[0] || ui->placement.shown[1];
}

/* DrawCommandMarker 0x418be0: the marker sprite's images, tail to head (DrawSprite 0x415a80), at the places
 * RefreshImageScreenPositions 0x415750 gives them; the marker's image has no player colours and is never mirrored. */
static void draw_marker(KpUi *ui, KpUiCanvas *c, const int32_t clip[4], int32_t x, int32_t y)
{
	const KpImages *w = ui->game->w;
	const KpUiFile *f = kp_ui_file(ui->grps, ui->grp_count, MARKER_GRP);
	int32_t s, i, frames;
	if (w->cursor < 0 || f == NULL || f->size < 6) return;
	s = w->thg.sprite[w->cursor];
	frames = f->data[0] | (f->data[1] & 0x7f) << 8;
	for (i = w->spr.images[s].tail; i != KP_NONE; i = w->img.prev[i]) {
		const uint8_t *rec;
		int32_t frame = w->img.frame[i], left, top;
		if ((w->img.flags[i] & KP_F_HIDDEN) != 0 || frame < 0 || frame >= frames) continue;
		rec = f->data + 6 + 8 * (size_t)frame;
		if ((size_t)(rec - f->data) + 8 > f->size) continue;
		left = kp_s16((uint32_t)w->spr.x[s]) + w->img.xoff[i] + rec[0] - (f->data[2] | f->data[3] << 8) / 2;
		top = kp_s16((uint32_t)w->spr.y[s]) + w->img.yoff[i] + rec[1] - (f->data[4] | f->data[5] << 8) / 2;
		grp_frame_view(ui, c, clip, f, frame, x + left - ui->input.screen_x, y + top - ui->input.screen_y);
	}
}

/* The placement's screen layers 4 then 3 (DrawPlacementLayer 0x47d200: BlitTransparent 0x4cf5e0 clipped to the game
 * view). */
static void draw_placement(const KpUi *ui, KpUiCanvas *c, const int32_t clip[4], int32_t x, int32_t y)
{
	int k;
	for (k = 1; k >= 0; k--) {
		const KpUiSurface *b = &ui->placement.bitmap[k];
		int32_t i, j;
		if (!ui->placement.shown[k] || b->pixels == NULL) continue;
		for (j = 0; j < b->h; j++) {
			for (i = 0; i < b->w; i++) {
				uint8_t v = b->pixels[(size_t)j * (size_t)b->w + (size_t)i];
				if (v != 0) put_view(c, clip, x + ui->placement.x[k] + i, y + ui->placement.y[k] + j, v);
			}
		}
	}
}

void kp_ui_canvas_game_view(KpUi *ui, KpUiCanvas *c, int32_t x, int32_t y)
{
	const KpUiInput *p = &ui->input;
	int32_t clip[4];
	c->seq++;
	if (ui->game == NULL) return;
	clip[0] = x;
	clip[1] = y;
	clip[2] = x + ui->view_w;
	clip[3] = y + ui->view_h;
	/* DrawDragBox 0x4bcc40: an active box while a mouse button is down (with none it ended, kp_ui_game_view_update) */
	if (p->box && p->mouse_state != 0) {
		outline_view(c, clip, x + p->box_left, y + p->box_top, x + p->box_right, y + p->box_bottom,
			ui->ui_colours[COLOUR_DRAG_BOX]);
	}
	if (kp_scalar(ui->game, KP_ORDER_MARKER) != 0) draw_marker(ui, c, clip, x, y);
	draw_placement(ui, c, clip, x, y);
}

void kp_ui_canvas_dialog(KpUi *ui, KpUiCanvas *c, int32_t dialog, int32_t x, int32_t y)
{
	const KpUiControl *d = &ui->controls[dialog];
	int keyed = (d->flags & KP_UI_TRANSPARENT) != 0;
	int translucent = (d->flags & (FLAG_BACKDROP | KP_UI_TRANSLUCENT)) == (FLAG_BACKDROP | KP_UI_TRANSLUCENT);
	int32_t i, j, first = c->glyph_count;
	const uint8_t *src;
	const uint16_t *owners;
	c->seq++;
	if (d->bitmap.pixels == NULL) return;
	if (translucent) ui->effects.level[0] = kp_ui_effect_level(ui->effects.dialog_percent); /* 0x6ce780 (0x4c5630) */
	src = text_source(ui, c, &d->bitmap, x, y, d->bitmap.w, d->bitmap.h, 1);
	/* the HD interface's images' pixels (not the original's, ADR 0013 R4d), the vector glyphs taken out */
	owners = kp_ui_image_owners(ui, c, &d->bitmap, src, x, y);
	if (translucent && c->text && !c->tint_valid) { /* the blend of the vector glyphs under it */
		kp_ui_effect_row(ui, 0, c->tint_row);
		c->tint_valid = 1;
	}
	for (j = 0; j < d->bitmap.h; j++) {
		for (i = 0; i < d->bitmap.w; i++) {
			uint8_t v = src[(size_t)j * (size_t)d->bitmap.w + (size_t)i];
			if (owners) c->item = owners[(size_t)j * (size_t)d->bitmap.w + (size_t)i];
			if (translucent) { /* BlitSingleColourBlend 0x4ca9b0 onto what lies under the dialog */
				const uint8_t *under;
				int32_t cx = x + i, cy = y + j;
				if (v == 0) continue;
				if (v != ui->effects.colour[0] || cx < 0 || cy < 0 || cx >= c->w || cy >= c->h) {
					put(c, cx, cy, v, OPAQUE);
					continue;
				}
				under = c->next + 2 * ((size_t)cy * (size_t)c->w + (size_t)cx);
				/* over the canvas's own pixels (the console's, or the text lines and the game view's layers drawn
				 * first) the blend is here */
				if (under[1] == OPAQUE || under[1] == KP_UI_CANVAS_OVERLAY_A) {
					put_tint(c, cx, cy, kp_ui_single_colour(ui, 0, v, under[0]), OPAQUE);
				} else { /* over the world: the render pass blends */
					put_tint(c, cx, cy, v, KP_UI_CANVAS_BLEND_A);
					c->blends++;
				}
				continue;
			}
			if (keyed && v == 0) continue;
			put(c, x + i, y + j, v, OPAQUE);
		}
	}
	c->item = 0;
	text_own(ui, c, first, x, y, d->bitmap.w, d->bitmap.h);
}

void kp_ui_canvas_blend_row(const KpUi *ui, uint8_t out[256]) { kp_ui_effect_row(ui, 0, out); }
