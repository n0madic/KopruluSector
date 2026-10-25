/* The canvas of kp_ui (kp_ui.h, ADR 0011): the panels, the console art and the fillers composed for the logical
 * screen, two bytes per pixel (palette index, opacity), with the rectangle that changed. */
#include "kp_ui.h"
#include "kp_game.h"
#include "kp_int.h"

#include <stdlib.h>
#include <string.h>

#define OPAQUE 255
#define FLAG_BACKDROP 0x10000000u   /* a dialog composited onto the backdrop with its flags (0x4c5630) */

/* Writes a pixel of the frame being composed. */
static void put(KpUiCanvas *c, int32_t x, int32_t y, uint8_t index, uint8_t alpha)
{
	uint8_t *p;
	if (x < 0 || y < 0 || x >= c->w || y >= c->h) return;
	p = c->next + 2 * ((size_t)y * (size_t)c->w + (size_t)x);
	p[0] = index;
	p[1] = alpha;
}

int kp_ui_canvas_resize(KpUiCanvas *c, int32_t w, int32_t h)
{
	uint8_t *pixels, *next;
	if (w < 1 || h < 1) return -1;
	pixels = (uint8_t *)calloc((size_t)w * (size_t)h, 2);
	next = (uint8_t *)calloc((size_t)w * (size_t)h, 2);
	if (pixels == NULL || next == NULL) {
		free(pixels);
		free(next);
		return -1;
	}
	free(c->pixels);
	free(c->next);
	c->pixels = pixels;
	c->next = next;
	c->w = w;
	c->h = h;
	c->dirty[0] = c->dirty[1] = 0;
	c->dirty[2] = w;
	c->dirty[3] = h;
	return 0;
}

void kp_ui_canvas_free(KpUiCanvas *c)
{
	free(c->pixels);
	free(c->next);
	free(c->scratch);
	memset(c, 0, sizeof *c);
}

void kp_ui_canvas_begin(KpUiCanvas *c)
{
	if (c->next != NULL) memset(c->next, 0, (size_t)c->w * (size_t)c->h * 2);
	c->blends = 0;
}

int kp_ui_canvas_end(KpUiCanvas *c)
{
	size_t row = (size_t)c->w * 2;
	int32_t y, x0 = c->w, x1 = 0, y0 = c->h, y1 = 0;
	for (y = 0; y < c->h; y++) {
		const uint8_t *a = c->pixels + (size_t)y * row, *b = c->next + (size_t)y * row;
		int32_t l, r;
		if (memcmp(a, b, row) == 0) continue;
		for (l = 0; a[2 * l] == b[2 * l] && a[2 * l + 1] == b[2 * l + 1]; l++) {}
		for (r = c->w - 1; a[2 * r] == b[2 * r] && a[2 * r + 1] == b[2 * r + 1]; r--) {}
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
	int32_t i, j, w = d->bitmap.w, h = d->bitmap.h;
	if (d->bitmap.pixels == NULL || w <= 0 || h <= 0) return;
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
			put(c, i, j, c->shade[d->bitmap.pixels[(size_t)sy * (size_t)w + (size_t)sx]], OPAQUE);
		}
	}
}

void kp_ui_canvas_layer(KpUiCanvas *c, const KpUiSurface *s, int32_t x, int32_t y)
{
	int32_t i, j;
	if (s == NULL || s->pixels == NULL) return;
	for (j = 0; j < s->h; j++) {
		for (i = 0; i < s->w; i++) {
			uint8_t v = s->pixels[(size_t)j * (size_t)s->w + (size_t)i];
			if (v != 0) put(c, x + i, y + j, v, KP_UI_CANVAS_OVERLAY_A);
		}
	}
}

void kp_ui_canvas_tooltip(KpUi *ui, KpUiCanvas *c, int32_t x, int32_t y)
{
	/* TooltipLayerDraw 0x419100: the box's w x h of the tooltip bitmap, opaque (0x4ce720) */
	int32_t tx, ty, w, h, i, j;
	const KpUiSurface *s = kp_ui_tooltip(ui, &tx, &ty, &w, &h);
	if (s == NULL) return;
	for (j = 0; j < h && j < s->h; j++) {
		for (i = 0; i < w && i < s->w; i++) {
			put(c, x + i, y + j, s->pixels[(size_t)j * (size_t)s->w + (size_t)i], KP_UI_CANVAS_OVERLAY_A);
		}
	}
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
	int32_t i, j;
	if (d->bitmap.pixels == NULL) return;
	if (translucent) ui->effects.level[0] = kp_ui_effect_level(ui->effects.dialog_percent); /* 0x6ce780 (0x4c5630) */
	for (j = 0; j < d->bitmap.h; j++) {
		for (i = 0; i < d->bitmap.w; i++) {
			uint8_t v = d->bitmap.pixels[(size_t)j * (size_t)d->bitmap.w + (size_t)i];
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
					put(c, cx, cy, kp_ui_single_colour(ui, 0, v, under[0]), OPAQUE);
				} else { /* over the world: the render pass blends */
					put(c, cx, cy, v, KP_UI_CANVAS_BLEND_A);
					c->blends++;
				}
				continue;
			}
			if (keyed && v == 0) continue;
			put(c, x + i, y + j, v, OPAQUE);
		}
	}
}

void kp_ui_canvas_blend_row(const KpUi *ui, uint8_t out[256]) { kp_ui_effect_row(ui, 0, out); }
