/* The canvas of kp_ui (kp_ui.h, ADR 0011): the panels and the console art composed for the logical
 * screen, two bytes per pixel (palette index, opacity), with the rectangle that changed. */
#include "kp_text.h"
#include "kp_ui.h"
#include "kp_game.h"
#include "kp_int.h"
#include "kp_kpf.h"

#include <stdlib.h>
#include <string.h>

#define OPAQUE 255
#define FLAG_BACKDROP 0x10000000u   /* a dialog composited onto the backdrop with its flags (0x4c5630) */
#define GLYPH_AGE 120               /* composes a glyph stays in the log without being found live */
#define OWNER_SEQ 0x7fffu           /* the owner plane's draw */
#define OWNER_TINT 0x8000u          /* the owner plane's flag: a translucent dialog blended over the pixel since */
#define PART_PIECES 4               /* the rectangles a vector glyph's part is cut into at most (more: its pixels' box) */
#define GLYPH_PIECES (2 * PART_PIECES) /* a vector glyph's at most: its parts', plain and blended */
#define GLYPH_FIRST 256             /* the glyph list's first size */

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
	if (c->hd_plain) c->hd_plain[at] = c->plain;
	if (c->ids_next) {
		uint16_t was = c->ids_next[at];
		/* an item over another's pixel (a dialog's button over the console) lies over it where it has no item under
		 * it of its own surface: where its HD image does not reach, the one under it shows, not the SD pixel */
		if (c->item != 0 && was != 0 && was != c->item && c->items[c->item].under == 0) c->items[c->item].under = was;
		c->ids_next[at] = c->item;
		c->below_next[at] = c->below_value;
	}
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
	if (c->hd_plain) c->hd_plain[at] = c->plain;
	if (c->ids_next) {
		c->ids_next[at] = c->item;
		c->below_next[at] = c->below_value;
	}
}

/* --- the vector text (not the original's: the text of large scales, ADR 0011) --- */

/* Every open dialog with its controls painted again: the text drawn before the log was turned on (or off) is not (or
 * no longer) in it. */
static void repaint_open(KpUi *ui)
{
	int32_t d, c, count = 0;
	for (d = ui->open; d >= 0 && count < KP_UI_CONTROLS; d = ui->controls[d].next, count++) {
		kp_ui_mark_dirty(ui, d);
		for (c = ui->controls[d].child; c >= 0; c = ui->controls[c].next) kp_ui_mark_dirty(ui, c);
	}
}

void kp_ui_set_vector_text(KpUi *ui, int record, int on, const uint8_t *codes)
{
	int k, top, height, was = ui->text.log != NULL;
	ui->text.log = record ? &ui->glyph_log : NULL;
	if (!record) kp_ui_glyph_log_free(&ui->glyph_log);
	if (was != (record != 0)) repaint_open(ui);
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

void kp_ui_set_vector_extra(KpUi *ui, const int32_t *codes, int32_t count)
{
	ui->vector_extra = codes;
	ui->vector_extra_count = count;
}

/* Whether the vector font draws a code point: its byte slot's verdict, else one of the supplement's. */
static int vector_code(const KpUi *ui, int32_t cp)
{
	int slot = kp_text_byte(cp);
	int32_t lo = 0, hi = ui->vector_extra_count;
	if (slot >= 0) return ui->vector_codes[slot];
	while (lo < hi) {
		int32_t mid = lo + (hi - lo) / 2;
		if (ui->vector_extra[mid] == cp) return 1;
		if (ui->vector_extra[mid] < cp) lo = mid + 1;
		else hi = mid;
	}
	return 0;
}

static int vector_font(const KpUi *ui, const uint8_t *font)
{
	int k;
	for (k = 0; k < KP_UI_FNT_FONTS; k++) {
		if (ui->font_files[k] == font) return ui->vector_font_ok[k];
	}
	return 0;
}

/* Room for n listed glyphs, and for the GLYPH_PIECES times as many kp_ui_canvas_text_end may make of them. */
static int text_room(KpUiCanvas *c, int32_t n)
{
	if (n > c->glyph_capacity) {
		int32_t cap = c->glyph_capacity ? 2 * c->glyph_capacity : GLYPH_FIRST;
		KpUiCanvasGlyph *grown = (KpUiCanvasGlyph *)realloc(c->glyphs, (size_t)cap * sizeof *grown);
		if (grown == NULL) return -1;
		c->glyphs = grown;
		c->glyph_capacity = cap;
	}
	if (GLYPH_PIECES * n > c->spare_capacity) {
		/* doubled from its own size, not from the list's: the two swap at every text_end, and a list grown from the
		 * other's size took that factor again at every swap */
		int32_t cap = c->spare_capacity ? c->spare_capacity : GLYPH_PIECES * GLYPH_FIRST;
		while (cap < GLYPH_PIECES * n) cap *= 2;
		KpUiCanvasGlyph *grown = (KpUiCanvasGlyph *)realloc(c->spare, (size_t)cap * sizeof *grown);
		if (grown == NULL) return -1;
		c->spare = grown;
		c->spare_capacity = cap;
	}
	return 0;
}

/* The pixels to compose of surface s, its top-left cw x ch placed at canvas (x, y) by the draw c->seq. Its live
 * glyphs stay in the log (seen); with the vector text on, those of a vector font with a pixel in that crop are taken
 * out, in a copy where their pixels hold what lay under them (1 there in *vector, else NULL), and listed for the
 * render (`list`; else only the copy). Else the surface's own pixels. */
static const uint8_t *text_source(KpUi *ui, KpUiCanvas *c, const KpUiSurface *s, int32_t x, int32_t y, int32_t cw,
	int32_t ch, int list, const uint8_t **vector)
{
	KpUiGlyphLog *log = &ui->glyph_log;
	const KpUiGlyphSurface *sf;
	size_t size = (size_t)s->w * (size_t)s->h;
	uint32_t k;
	int copied = 0;
	if (vector != NULL) *vector = NULL;
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
		if (!ui->vector_text || !c->text || g->wrapped || !vector_code(ui, g->code) || !vector_font(ui, g->font)) {
			continue;
		}
		for (j = g->first; j < g->first + g->count && !inside; j++) {
			inside = log->offsets[j] % s->w < cw && log->offsets[j] / s->w < ch;
		}
		if (!inside) continue;
		if (list && text_room(c, c->glyph_count + 1) != 0) break; /* out of memory: the rest stay bitmaps */
		if (!copied) {
			if (c->source_size < size) {
				uint8_t *grown = (uint8_t *)realloc(c->source, size), *marks;
				if (grown == NULL) return s->pixels;
				c->source = grown;
				if ((marks = (uint8_t *)realloc(c->source_vector, size)) == NULL) return s->pixels;
				c->source_vector = marks;
				c->source_size = size;
			}
			memcpy(c->source, s->pixels, size);
			memset(c->source_vector, 0, size);
			copied = 1;
		}
		for (j = g->first; j < g->first + g->count; j++) {
			c->source[log->offsets[j]] = log->under[j];
			c->source_vector[log->offsets[j]] = 1;
		}
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
	if (copied && vector != NULL) *vector = c->source_vector;
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

/* The live glyphs of the briefing's transcript stay in the log as a composed surface's do. The canvas never composes
 * that bitmap: its rows are blitted into the room's dialog as they scroll, and the scroll starts 4 s (240 composes) after
 * the text is drawn, past GLYPH_AGE, so the rows that came into view had lost their glyphs. */
static void keep_transcript(KpUi *ui)
{
	KpUiGlyphLog *log = &ui->glyph_log;
	const KpUiSurface *s = &ui->briefing.text;
	const KpUiGlyphSurface *sf;
	uint32_t k;
	if (s->pixels == NULL || (sf = kp_ui_glyph_surface(log, s->pixels, s->w)) == NULL) return;
	for (k = sf->head; k != KP_UI_GLYPH_NONE; k = log->glyphs[k].next) {
		if (kp_ui_glyph_live(log, &log->glyphs[k], s)) log->glyphs[k].seen = log->serial;
	}
}

/* A glyph's bitmap box on the canvas, within the canvas and its part's rectangle (x0, y0, x1, y1 exclusive). */
static void glyph_area(const KpUiCanvas *c, const KpUiCanvasGlyph *v, const KpUiGlyph *g, int32_t dx, int32_t dy,
	int32_t area[4])
{
	area[0] = v->x + v->box[2];
	area[1] = v->y + v->box[3];
	area[2] = area[0] + v->box[0];
	area[3] = area[1] + v->box[1];
	if (g->part) {
		if (g->clip[0] + dx > area[0]) area[0] = g->clip[0] + dx;
		if (g->clip[1] + dy > area[1]) area[1] = g->clip[1] + dy;
		if (g->clip[2] + dx < area[2]) area[2] = g->clip[2] + dx;
		if (g->clip[3] + dy < area[3]) area[3] = g->clip[3] + dy;
	}
	if (area[0] < 0) area[0] = 0;
	if (area[1] < 0) area[1] = 0;
	if (area[2] > c->w) area[2] = c->w;
	if (area[3] > c->h) area[3] = c->h;
}

/* Whether the canvas pixel of `owner` shows the draw `seq` (no later draw wrote it) in the part `tinted` (a translucent
 * dialog blended over it, or not). */
static int open_as(uint16_t owner, uint16_t seq, int tinted)
{
	return (owner & OWNER_SEQ) <= seq && ((owner & OWNER_TINT) != 0) == tinted;
}

/* The pixels of area that show the draw seq in the part `tinted`, as rectangles: each row's runs, a run under one of
 * the same columns in the row above lengthening its rectangle. Returns their count, -1 for more than PART_PIECES. */
static int32_t open_pieces(const KpUiCanvas *c, const int32_t area[4], uint16_t seq, int tinted,
	int32_t pieces[PART_PIECES][4])
{
	int32_t n = 0, x, y, i;
	for (y = area[1]; y < area[3]; y++) {
		const uint16_t *row = c->owner + (size_t)y * (size_t)c->w;
		for (x = area[0]; x < area[2];) {
			int32_t x0;
			if (!open_as(row[x], seq, tinted)) {
				x++;
				continue;
			}
			for (x0 = x; x < area[2] && open_as(row[x], seq, tinted); x++) {}
			for (i = 0; i < n && !(pieces[i][0] == x0 && pieces[i][2] == x && pieces[i][3] == y); i++) {}
			if (i < n) {
				pieces[i][3] = y + 1;
				continue;
			}
			if (n == PART_PIECES) return -1;
			pieces[n][0] = x0;
			pieces[n][1] = y;
			pieces[n][2] = x;
			pieces[n++][3] = y + 1;
		}
	}
	return n;
}

/* Whether a rectangle is the glyph's whole bitmap box. */
static int whole_box(const KpUiCanvasGlyph *v, const int32_t r[4])
{
	return r[0] == v->x + v->box[2] && r[1] == v->y + v->box[3] && r[2] == r[0] + v->box[0]
		&& r[3] == r[1] + v->box[1];
}

void kp_ui_canvas_text_end(KpUi *ui, KpUiCanvas *c)
{
	KpUiGlyphLog *log;
	KpUiCanvasGlyph *out;
	int32_t k, kept = 0, capacity;
	if (ui == NULL) return;
	log = &ui->glyph_log;
	/* each glyph's pixels no later draw covers, those a translucent dialog blended over apart: a glyph covered whole
	 * is dropped, one covered in part is cut to the rectangles of its bitmap's box no later draw covers (a dialog's frame
	 * line across it stays over it), and the blended ones are a glyph of their own in the blended colours */
	for (k = 0; k < c->glyph_count; k++) {
		const KpUiCanvasGlyph *v = &c->glyphs[k];
		const KpUiGlyph *g = &log->glyphs[v->glyph];
		int32_t stride = log->surfaces[g->surface].stride, dx = v->x - g->pen_x, dy = v->y - g->pen_y;
		int32_t box[2][4], part, area[4], pieces[PART_PIECES][4], n, i;
		uint32_t j, seen[2] = { 0, 0 };
		int whole;
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
		glyph_area(c, v, g, dx, dy, area);
		for (part = 0; part < 2; part++) {
			if (seen[part] == 0) continue;
			n = open_pieces(c, area, v->seq, part, pieces);
			whole = !g->part && n == 1 && whole_box(v, pieces[0]);
			if (n <= 0) { /* more pieces than kept: its pixels' box within its area (the render opens a side at its
			               * bitmap's edge, so the area's sides there cut nothing) */
				pieces[0][0] = box[part][0] > area[0] ? box[part][0] : area[0];
				pieces[0][1] = box[part][1] > area[1] ? box[part][1] : area[1];
				pieces[0][2] = box[part][2] < area[2] ? box[part][2] : area[2];
				pieces[0][3] = box[part][3] < area[3] ? box[part][3] : area[3];
				n = 1;
			}
			for (i = 0; i < n && kept < c->spare_capacity; i++) {
				KpUiCanvasGlyph *o = &c->spare[kept++];
				*o = *v;
				o->glyph = 0; /* the log's entry only while the frame is composed */
				o->whole = whole;
				memcpy(o->clip, pieces[i], sizeof o->clip);
				if (part) {
					o->fill = c->tint_row[o->fill];
					o->shadow = c->tint_row[o->shadow];
				}
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
	keep_transcript(ui);
	log->serial++;
	kp_ui_glyph_log_prune(log, GLYPH_AGE);
}

/* The audit's plane gone (KpUiCanvas.hd_plain): it is made at the canvas's size with the item planes (begin). */
static void free_audit_plane(KpUiCanvas *c)
{
	free(c->hd_plain);
	c->hd_plain = NULL;
	c->hd_plain_size = 0;
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
	free_audit_plane(c);
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
	free(c->source_vector);
	free(c->scratch);
	free(c->ids);
	free(c->ids_next);
	free(c->below);
	free(c->below_next);
	free(c->hd_plain);
	memset(c, 0, sizeof *c);
}

int kp_ui_canvas_hd(KpUiCanvas *c, int on)
{
	size_t n = (size_t)c->w * (size_t)c->h;
	free(c->ids);
	free(c->ids_next);
	free(c->below);
	free(c->below_next);
	c->ids = c->ids_next = c->below = c->below_next = NULL;
	c->item = c->below_value = 0;
	free_audit_plane(c);
	if (!on || n == 0) return 0;
	c->ids = (uint16_t *)calloc(n, sizeof *c->ids);
	c->ids_next = (uint16_t *)calloc(n, sizeof *c->ids_next);
	c->below = (uint16_t *)calloc(n, sizeof *c->below);
	c->below_next = (uint16_t *)calloc(n, sizeof *c->below_next);
	if (c->ids == NULL || c->ids_next == NULL || c->below == NULL || c->below_next == NULL) {
		free(c->ids);
		free(c->ids_next);
		free(c->below);
		free(c->below_next);
		c->ids = c->ids_next = c->below = c->below_next = NULL;
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
	if (c->below_next) memset(c->below_next, 0, (size_t)c->w * (size_t)c->h * sizeof *c->below_next);
	c->item = c->below_value = 0;
	c->item_count = 0;
	c->tint_valid = 0;
	c->blends = 0;
	c->hd_lost = 0;
	c->hd_unlogged = 0;
	c->hd_unlogged_paths[0] = 0;
	c->plain = 0;
	if (!c->hd_audit || !c->ids_next) { /* the audit's plane, with the item planes (freed with them and by a resize) */
		free_audit_plane(c);
	} else if (c->hd_plain == NULL && (c->hd_plain = (uint8_t *)malloc((size_t)c->w * (size_t)c->h)) != NULL) {
		c->hd_plain_size = (size_t)c->w * (size_t)c->h;
	}
	if (c->hd_plain) memset(c->hd_plain, 0, c->hd_plain_size);
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
		uint16_t *ua = c->ids ? c->below + (size_t)y * (size_t)c->w : NULL;
		const uint16_t *ub = c->ids ? c->below_next + (size_t)y * (size_t)c->w : NULL;
		int32_t l, r;
		if (memcmp(a, b, row) == 0 && (!ia || (memcmp(ia, ib, (size_t)c->w * sizeof *ia) == 0
			&& memcmp(ua, ub, (size_t)c->w * sizeof *ua) == 0))) {
			continue;
		}
		for (l = 0; l < c->w && a[2 * l] == b[2 * l] && a[2 * l + 1] == b[2 * l + 1] && (!ia || (ia[l] == ib[l]
			&& ua[l] == ub[l])); l++) {}
		for (r = c->w - 1; r > l && a[2 * r] == b[2 * r] && a[2 * r + 1] == b[2 * r + 1] && (!ia || (ia[r] == ib[r]
			&& ua[r] == ub[r])); r--) {}
		if (ia) {
			memcpy(ia, ib, (size_t)c->w * sizeof *ia);
			memcpy(ua, ub, (size_t)c->w * sizeof *ua);
		}
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

/* A console pixel: under an HD console piece (the item set) a pixel of the game view's layer drawn first (the text
 * lines, the drag box, the placement) stays, the piece's item over it: the HD piece, lower than the SD console, draws
 * over it where it is opaque and leaves it over the world where it is not. */
static void put_console(KpUiCanvas *c, int32_t x, int32_t y, uint8_t index)
{
	size_t at;
	if (x < 0 || y < 0 || x >= c->w || y >= c->h) return;
	at = (size_t)y * (size_t)c->w + (size_t)x;
	if (c->item != 0 && c->ids_next != NULL && c->next[2 * at + 1] == KP_UI_CANVAS_OVERLAY_A) {
		c->ids_next[at] = c->item;
		c->below_next[at] = c->below_value;
		return;
	}
	put(c, x, y, index, OPAQUE);
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
			if (v != 0) put_console(c, x + i, y + j, v); /* the art's holes keep what lies under (a backdrop dialog) */
		}
	}
}

static int32_t floor_mod(int32_t a, int32_t b)
{
	int32_t m = a % b;
	return m < 0 ? m + b : m;
}

#define PIECE_UNDERS 8 /* the items a piece may lie over in one draw (the other pieces, a wing's tile) */

/* The item of a piece lying over item `was`: a copy of it naming that one, drawn under its translucent texels. */
static uint16_t item_over(KpUiCanvas *c, uint16_t item, uint16_t was, uint16_t unders[PIECE_UNDERS][2], int *count)
{
	KpUiCanvasItem it;
	uint16_t copy;
	int k;
	if (was == 0 || was == item) return item;
	for (k = 0; k < *count; k++) {
		if (unders[k][0] == was) return unders[k][1];
	}
	if (*count == PIECE_UNDERS) return item;
	it = c->items[item];
	it.under = was;
	copy = kp_ui_canvas_add_item(c, &it);
	if (copy == 0) return item;
	unders[*count][0] = was;
	unders[*count][1] = copy;
	++*count;
	return copy;
}

void kp_ui_canvas_piece(KpUi *ui, KpUiCanvas *c, const KpUiConsolePiece *p, int32_t x, int32_t y, int32_t from, int32_t to)
{
	const uint8_t *colours = ui->console_colours;
	uint16_t unders[PIECE_UNDERS][2], item = c->item;
	int count = 0;
	int32_t i, j, first;
	c->seq++;
	if (p->cells == NULL || colours == NULL) return;
	if (from < 0) from = 0;
	if (to > c->w) to = c->w;
	if (!p->period) { /* its own columns */
		if (from < x) from = x;
		if (to > x + p->w) to = x + p->w;
	}
	first = p->period ? floor_mod(from - x, p->period) : from - x;
	for (j = 0; j < p->h; j++) {
		const uint8_t *row = p->cells + (size_t)j * (size_t)p->w;
		int32_t cy = y + j, cell = first;
		if (cy < 0 || cy >= c->h) continue;
		for (i = from; i < to; i++, cell = cell + 1 == p->period ? 0 : cell + 1) {
			int32_t place;
			if (cell >= p->w) continue;
			place = row[cell] & ~KP_UI_CONSOLE_FRINGE;
			if (place == 0 || ((row[cell] & KP_UI_CONSOLE_FRINGE) && item == 0)) continue;
			if (item != 0 && c->ids_next != NULL) {
				c->item = item_over(c, item, c->ids_next[(size_t)cy * (size_t)c->w + (size_t)i], unders, &count);
			}
			put_console(c, i, cy, colours[place - 1]);
		}
	}
	c->item = item;
}

void kp_ui_canvas_console_owned(KpUi *ui, KpUiCanvas *c, int group, int32_t x, int32_t y, int32_t x1)
{
	const KpUiSurface *s = &ui->console;
	int32_t i, j;
	c->seq++;
	if (s->pixels == NULL || ui->console_owners == NULL) return;
	if (x1 > s->w) x1 = s->w;
	for (j = ui->console_owned_top; j < s->h; j++) {
		for (i = 0; i < x1; i++) {
			size_t at = (size_t)j * (size_t)s->w + (size_t)i;
			if (ui->console_owners[at] == group + 1) put_console(c, x + i, y + j, s->pixels[at]);
		}
	}
}

/* The rectangle k of the game's fluff list (x, y, w, h); 0 past its end or without the table. */
static int fluff_rect(const KpUi *ui, int k, int32_t r[4])
{
	const uint8_t *t = ui->fluff;
	size_t at = 1, n = ui->fluff_size;
	int list, j;
	if (t == NULL || n < 1 || ui->fluff_list < 0 || ui->fluff_list >= t[0]) return 0;
	for (list = 0; list < ui->fluff_list; list++) {
		if (at >= n) return 0;
		at += 1 + 8 * (size_t)t[at];
	}
	if (at >= n || k >= t[at] || at + 1 + 8 * (size_t)(k + 1) > n) return 0;
	at += 1 + 8 * (size_t)k;
	for (j = 0; j < 4; j++) r[j] = kp_le_u16(t + at + 2 * (size_t)j);
	return 1;
}

void kp_ui_canvas_fluff(KpUi *ui, KpUiCanvas *c, int group, int32_t x, int32_t y, int32_t x0, int32_t x1)
{
	const KpUiSurface *s = &ui->console;
	int32_t r[4], i, j;
	int k;
	c->seq++;
	if (s->pixels == NULL || (group >= 0 && ui->console_owners == NULL)) return;
	for (k = 0; fluff_rect(ui, k, r); k++) {
		for (j = r[1]; j < r[1] + r[3] && j < s->h; j++) {
			for (i = r[0]; i < r[0] + r[2] && i < s->w; i++) {
				size_t at = (size_t)j * (size_t)s->w + (size_t)i;
				if (s->pixels[at] == 0) continue; /* the art's holes keep what lies under */
				if (group < 0 ? (i >= x0 && i < x1) : ui->console_owners[at] == group + 1) put_console(c, x + i, y + j, s->pixels[at]);
			}
		}
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

/* A fixed screen's margins around its dialog d at (x, y): the dialog's pixels as composed (src, the vector glyphs taken
 * out) and their owners (NULL none), from the dialog's compose (one owners' pass per surface: the items once, the
 * development checks' counts once). */
static void draw_margins(KpUi *ui, KpUiCanvas *c, const KpUiControl *d, int32_t x, int32_t y, const uint8_t *src,
	const uint16_t *owners)
{
	int32_t i, j, w = d->bitmap.w, h = d->bitmap.h;
	c->seq++;
	if (src == NULL || w <= 0 || h <= 0) return;
	/* the HD interface (not the original's, ADR 0013 R4d): a margin pixel is its edge pixel's item, whose art (a
	 * wide glue background) may reach there */
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
			if (owners) {
				uint16_t below = ui->image_log.below[(size_t)sy * (size_t)w + (size_t)sx];
				c->item = owners[(size_t)sy * (size_t)w + (size_t)sx];
				c->plain = c->hd_audit && ui->image_log.plain[(size_t)sy * (size_t)w + (size_t)sx];
				c->below_value = (uint16_t)((below & KP_UI_BELOW_UNDER)
					| (below & KP_UI_BELOW_KNOWN ? KP_UI_BELOW_KNOWN | c->shade[below & 0xff] : 0));
			}
			put(c, i, j, c->shade[src[(size_t)sy * (size_t)w + (size_t)sx]], OPAQUE);
		}
	}
	c->item = c->below_value = 0;
	c->plain = 0;
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
	src = text_source(ui, c, s, x, y, w, h, 1, NULL); /* the box's glyphs: an earlier tooltip's may lie outside it */
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

/* A translucent texel's index over the world: the index-space pass maps the world's index through the blend row and
 * does not read it, the HD picture mixes its palette colour into the world. A glue theme's blend colour, which a game
 * keeps until a dialog of its own sets one, may lie in the palette's cycles (PalCs's 254, the load screen's): its
 * nearest colour outside them, so the HD mix does not cycle with the palette (the game's tables leave the cycles out
 * too). Not the original's (ADR 0011 approximation). */
static uint8_t blend_stand_in(const KpUi *ui, uint8_t v)
{
	uint8_t s = ui->effects.steady[v];
	return s != 0 ? s : v;
}

/* A pixel of a translucent layer: its blend colour blended over what lies under it, another index as it is (alpha). */
static void put_blended(KpUi *ui, KpUiCanvas *c, int32_t cx, int32_t cy, uint8_t v, uint8_t alpha)
{
	const uint8_t *under;
	if (v != ui->effects.colour[0] || cx < 0 || cy < 0 || cx >= c->w || cy >= c->h) {
		put(c, cx, cy, v, alpha);
		return;
	}
	under = c->next + 2 * ((size_t)cy * (size_t)c->w + (size_t)cx);
	/* over the canvas's own pixels (the console's, or the text lines and the game view's layers drawn first) the
	 * blend is here */
	if (under[1] == OPAQUE || under[1] == KP_UI_CANVAS_OVERLAY_A) {
		put_tint(c, cx, cy, kp_ui_single_colour(ui, 0, v, under[0]), alpha);
	} else { /* over the world: the render pass blends */
		put_tint(c, cx, cy, blend_stand_in(ui, v), KP_UI_CANVAS_BLEND_A);
		c->blends++;
	}
}

/* The blend's level and the vector glyphs' tint for a translucent layer. */
static void translucent_begin(KpUi *ui, KpUiCanvas *c)
{
	ui->effects.level[0] = kp_ui_effect_level(ui->effects.dialog_percent); /* 0x6ce780 (0x4c5630) */
	if (c->text && !c->tint_valid) { /* the blend of the vector glyphs under it */
		kp_ui_effect_row(ui, 0, c->tint_row);
		c->tint_valid = 1;
	}
}

/* A layer surface at canvas (x, y), index 0 transparent, KP_UI_CANVAS_OVERLAY_A; translucent: its pixels of the blend
 * colour blended over what lies under them. */
static void draw_layer(KpUi *ui, KpUiCanvas *c, const KpUiSurface *s, int32_t x, int32_t y, int translucent)
{
	const uint8_t *src;
	int32_t i, j, first = c->glyph_count;
	c->seq++;
	if (s == NULL || s->pixels == NULL) return;
	if (translucent) translucent_begin(ui, c);
	src = text_source(ui, c, s, x, y, s->w, s->h, 1, NULL);
	for (j = 0; j < s->h; j++) {
		for (i = 0; i < s->w; i++) {
			uint8_t v = src[(size_t)j * (size_t)s->w + (size_t)i];
			if (v == 0) continue;
			if (translucent) put_blended(ui, c, x + i, y + j, v, KP_UI_CANVAS_OVERLAY_A);
			else put(c, x + i, y + j, v, KP_UI_CANVAS_OVERLAY_A);
		}
	}
	text_own(ui, c, first, x, y, s->w, s->h);
}

void kp_ui_canvas_layer(KpUi *ui, KpUiCanvas *c, const KpUiSurface *s, int32_t x, int32_t y)
{
	draw_layer(ui, c, s, x, y, 0);
}

void kp_ui_canvas_translucent(KpUi *ui, KpUiCanvas *c, const KpUiSurface *s, int32_t x, int32_t y)
{
	draw_layer(ui, c, s, x, y, 1);
}

/* kp_ui_canvas_dialog: the dialog's pixels as composed (NULL without a bitmap) and their owners (*owners, NULL none). */
/* Whether a console dialog's pixel (i, j) lies where the console bitmap has none: its captured background
 * (CaptureConsoleBackground 0x4a7c40) is 0 there, and the original shows the game under it, as on the replay's
 * nconsole.pcx beside the minimap (0..137 x 315..479: 130..137 x 315..323) and zconsole.pcx's 133..137 x 315 (the
 * emulator's frames of a replay and of a Zerg game). */
static int console_hole(const KpUi *ui, const KpUiControl *d, int32_t i, int32_t j)
{
	int32_t cx = d->left + i, cy = d->top + j;
	const KpUiSurface *s = &ui->console;
	return d->console_capture && s->pixels != NULL && cx >= 0 && cy >= 0 && cx < s->w && cy < s->h
		&& s->pixels[(size_t)cy * (size_t)s->w + (size_t)cx] == 0;
}

static const uint8_t *compose_dialog(KpUi *ui, KpUiCanvas *c, int32_t dialog, int32_t x, int32_t y,
	const uint16_t **owners_out)
{
	const KpUiControl *d = &ui->controls[dialog];
	int keyed = (d->flags & KP_UI_TRANSPARENT) != 0;
	int translucent = (d->flags & (FLAG_BACKDROP | KP_UI_TRANSLUCENT)) == (FLAG_BACKDROP | KP_UI_TRANSLUCENT);
	int32_t i, j, first = c->glyph_count;
	const uint8_t *src, *vector;
	const uint16_t *owners;
	*owners_out = NULL;
	c->seq++;
	if (d->bitmap.pixels == NULL) return NULL;
	if (translucent) translucent_begin(ui, c);
	src = text_source(ui, c, &d->bitmap, x, y, d->bitmap.w, d->bitmap.h, 1, &vector);
	/* the HD interface's images' pixels (not the original's, ADR 0013 R4d), the vector glyphs taken out */
	owners = *owners_out = kp_ui_image_owners(ui, c, &d->bitmap, src, vector, x, y);
	for (j = 0; j < d->bitmap.h; j++) {
		for (i = 0; i < d->bitmap.w; i++) {
			uint8_t v = src[(size_t)j * (size_t)d->bitmap.w + (size_t)i];
			if (owners) {
				c->item = owners[(size_t)j * (size_t)d->bitmap.w + (size_t)i];
				c->plain = c->hd_audit && ui->image_log.plain[(size_t)j * (size_t)d->bitmap.w + (size_t)i];
				c->below_value = ui->image_log.below[(size_t)j * (size_t)d->bitmap.w + (size_t)i];
			}
			if (translucent) { /* BlitSingleColourBlend 0x4ca9b0 onto what lies under the dialog */
				if (v != 0) put_blended(ui, c, x + i, y + j, v, OPAQUE);
				continue;
			}
			if (v == 0 && (keyed || console_hole(ui, d, i, j))) continue;
			put(c, x + i, y + j, v, OPAQUE);
		}
	}
	c->item = c->below_value = 0;
	c->plain = 0;
	text_own(ui, c, first, x, y, d->bitmap.w, d->bitmap.h);
	return src;
}

void kp_ui_canvas_dialog(KpUi *ui, KpUiCanvas *c, int32_t dialog, int32_t x, int32_t y)
{
	const uint16_t *owners;
	(void)compose_dialog(ui, c, dialog, x, y, &owners);
}

void kp_ui_canvas_screen(KpUi *ui, KpUiCanvas *c, int32_t dialog, int32_t x, int32_t y)
{
	const uint16_t *owners;
	const uint8_t *src = compose_dialog(ui, c, dialog, x, y, &owners);
	draw_margins(ui, c, &ui->controls[dialog], x, y, src, owners);
}

void kp_ui_canvas_blend_row(const KpUi *ui, uint8_t out[256]) { kp_ui_effect_row(ui, 0, out); }
