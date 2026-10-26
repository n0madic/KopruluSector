/*
 * The interface's canvas and cursor (ADR 0011): kp_ui composes the console art, the fillers and the panels of a layout
 * into one canvas of the logical screen (KpUiCanvas: index and opacity per pixel, rows top first), which the canvas
 * pass writes into the index target; the cursor's frame lies over it at the mouse.
 */
#include "kp_present.h"

#include <stdio.h>
#include <string.h>

#define FLAG_BACKDROP 0x10000000 /* a dialog composited onto the backdrop (the in-game menus): under the console */

void kp_canvas_init(KpCanvas *c) { memset(c, 0, sizeof *c); }

void kp_canvas_free(KpCanvas *c)
{
	kp_ui_canvas_free(&c->canvas);
	memset(c, 0, sizeof *c);
}

static void bin_name(const KpUi *ui, const KpUiControl *k, char *out, size_t size)
{
	size_t n;
	out[0] = 0;
	if (k->bin < 0 || k->bin >= ui->bin_count) return;
	n = ui->bins[k->bin].name_len < size - 1 ? ui->bins[k->bin].name_len : size - 1;
	memcpy(out, ui->bins[k->bin].name, n);
	out[n] = 0;
}

int kp_canvas_hud_panels(KpUi *ui, KpPanel *out, int max)
{
	int32_t open[KP_CANVAS_PANELS], d;
	int n = 0, count = 0, i;
	if (max < 1) return 0;
	memset(&out[0], 0, sizeof out[0]);
	out[0].lines = 1;
	out[0].dialog = -1;
	snprintf(out[0].bin, sizeof out[0].bin, "lines");
	out[0].rect[2] = 639;
	out[0].rect[3] = 479;
	count = 1;
	for (d = ui->open; d >= 0 && n < KP_CANVAS_PANELS; d = ui->controls[d].next) open[n++] = d;
	for (i = n - 1; i >= 0 && count < max; i--) { /* bottom first */
		const KpUiControl *k = &ui->controls[open[i]];
		KpPanel *p = &out[count++];
		memset(p, 0, sizeof *p);
		p->dialog = open[i];
		bin_name(ui, k, p->bin, sizeof p->bin);
		p->rect[0] = k->left;
		p->rect[1] = k->top;
		p->rect[2] = k->right;
		p->rect[3] = k->bottom;
		p->backdrop = (k->flags & FLAG_BACKDROP) != 0;
	}
	return count;
}

static int resize(KpCanvas *c, int32_t w, int32_t h)
{
	if (w == c->w && h == c->h) return 0;
	if (kp_ui_canvas_resize(&c->canvas, w, h) != 0) return -1;
	c->w = w;
	c->h = h;
	c->resized = 1;
	return 0;
}

static void add_place(KpCanvas *c, int32_t dialog, int32_t x, int32_t y)
{
	int k;
	for (k = 0; k < c->place_count; k++) {
		if (c->places[k].dialog == dialog) {
			c->places[k].x = x;
			c->places[k].y = y;
			return;
		}
	}
	if (c->place_count < KP_CANVAS_PANELS) {
		c->places[c->place_count].dialog = dialog;
		c->places[c->place_count].x = x;
		c->places[c->place_count].y = y;
		c->place_count++;
	}
}

int kp_canvas_place(const KpCanvas *c, int32_t dialog, int32_t *x, int32_t *y)
{
	int k;
	for (k = 0; k < c->place_count; k++) {
		if (c->places[k].dialog == dialog) {
			*x = c->places[k].x;
			*y = c->places[k].y;
			return 1;
		}
	}
	return 0;
}

/* The frame's end: the changed rectangle (the whole canvas after a resize), the blend row for translucent texels. */
static int finish(KpCanvas *c, KpUi *ui)
{
	int changed;
	kp_ui_canvas_text_end(ui, &c->canvas);
	changed = kp_ui_canvas_end(&c->canvas);
	if (c->resized) {
		c->resized = 0;
		c->dirty[0] = 0;
		c->dirty[1] = 0;
		c->dirty[2] = c->w;
		c->dirty[3] = c->h;
		changed = 1;
	} else if (changed) {
		memcpy(c->dirty, c->canvas.dirty, sizeof c->dirty);
	} else {
		memset(c->dirty, 0, sizeof c->dirty);
	}
	c->blend_valid = c->canvas.blends > 0;
	if (c->blend_valid) kp_ui_canvas_blend_row(ui, c->blend_row);
	return changed;
}

static int32_t at_least_zero(int32_t v) { return v < 0 ? 0 : v; }

int kp_canvas_compose(KpCanvas *c, KpUi *ui, int32_t w, int32_t h, const KpPanel *panels, int count, int layout)
{
	KpArrangement *a = &c->arrangement;
	int32_t x, y, tx, ty, tw, th;
	int k;
	if (resize(c, w, h) != 0) return -1;
	kp_layout_arrange(a, w, h, layout);
	c->place_count = 0;
	kp_ui_canvas_begin(ui, &c->canvas);
	/* the game screen's own drawing after the sprites and the fog (its layer's callback 0x445ea0: the text lines, the
	 * drag box, the command marker) lies under everything else; the view's top-left is the logical screen's */
	for (k = 0; k < count; k++) {
		if (panels[k].lines && kp_layout_place(a, panels[k].bin, panels[k].rect, &x, &y)) {
			kp_ui_lines_expire(ui);
			kp_ui_canvas_layer(ui, &c->canvas, kp_ui_lines_layer(ui), x, y);
		}
	}
	kp_ui_canvas_game_view(ui, &c->canvas, 0, 0);
	/* a dialog composited onto the backdrop (BlitDialogToScreen 0x4c5630: the in-game menus) lies under the console */
	for (k = 0; k < count; k++) {
		if (panels[k].backdrop && kp_layout_place(a, panels[k].bin, panels[k].rect, &x, &y)) {
			kp_ui_canvas_dialog(ui, &c->canvas, panels[k].dialog, x, y);
			add_place(c, panels[k].dialog, x, y);
		}
	}
	for (k = 0; k < KP_LAYOUT_GROUPS; k++) {
		const KpLayoutGroup *g = &a->groups[k];
		kp_ui_canvas_console(ui, &c->canvas, g->x, g->y, g->x0, g->x1);
	}
	for (k = 0; k < a->filler_count; k++) {
		const KpLayoutFiller *f = &a->fillers[k];
		kp_ui_canvas_filler(ui, &c->canvas, f->x, f->y, f->w, f->column);
	}
	for (k = 0; k < count; k++) {
		if (panels[k].backdrop || panels[k].lines) continue; /* drawn above */
		if (kp_layout_place(a, panels[k].bin, panels[k].rect, &x, &y)) {
			kp_ui_canvas_dialog(ui, &c->canvas, panels[k].dialog, x, y);
			add_place(c, panels[k].dialog, x, y);
		}
	}
	/* the tooltip over the panels: moved with the open dialog its anchor (the box's bottom left) lies on */
	{
		const KpUiSurface *t = kp_ui_tooltip(ui, &tx, &ty, &tw, &th);
		if (t != NULL && tw > 0 && th > 0 && tw <= t->w && th <= t->h) {
			int32_t ax = tx, ay = ty + th - 1;
			for (k = count - 1; k >= 0; k--) { /* the topmost first */
				const KpPanel *p = &panels[k];
				int32_t px, py;
				if (!p->lines && kp_canvas_place(c, p->dialog, &px, &py) && ax >= p->rect[0] && ax <= p->rect[2]
					&& ay >= p->rect[1] && ay <= p->rect[3]) {
					tx += px - p->rect[0];
					ty += py - p->rect[1];
					break;
				}
			}
			kp_ui_canvas_tooltip(ui, &c->canvas, at_least_zero(tx < w - tw ? tx : w - tw),
				at_least_zero(ty < h - th ? ty : h - th));
		}
	}
	return finish(c, ui);
}

int kp_canvas_compose_screen(KpCanvas *c, KpUi *ui, int32_t w, int32_t h, int32_t dialog, int32_t x, int32_t y)
{
	if (resize(c, w, h) != 0) return -1;
	c->place_count = 0;
	kp_ui_canvas_begin(ui, &c->canvas);
	if (dialog >= 0) {
		kp_ui_canvas_margins(ui, &c->canvas, dialog, x, y);
		kp_ui_canvas_dialog(ui, &c->canvas, dialog, x, y);
		add_place(c, dialog, x, y);
	} else {
		kp_ui_canvas_fill(&c->canvas, 0, 0, w, h, 0);
	}
	return finish(c, ui);
}

void kp_canvas_pixel(const KpCanvas *c, int32_t x, int32_t y, uint8_t *index, uint8_t *alpha)
{
	const uint8_t *p;
	if (x < 0 || y < 0 || x >= c->w || y >= c->h || !c->canvas.pixels) {
		*index = 0;
		*alpha = 0;
		return;
	}
	p = c->canvas.pixels + 2 * ((size_t)y * (size_t)c->w + (size_t)x);
	*index = p[0];
	*alpha = p[1];
}

/* --- the cursor: the original's screen layer 0 (DrawCursor 0x43e490 at the mouse, over every other layer) --- */

void kp_cursor_init(KpCursor *c)
{
	memset(c, 0, sizeof *c);
	c->key = -1;
}

int kp_cursor_frame(KpCursor *c, KpUi *ui, int has_point)
{
	int64_t key = ui && has_point ? kp_ui_cursor_key(ui) : -1;
	if (key != c->key) {
		int32_t w, h, dx, dy;
		c->key = key;
		c->shown = 0; /* a frame refused below leaves the pixels unshown */
		if (key >= 0 && kp_ui_cursor_frame(ui, c->pixels, sizeof c->pixels, &w, &h, &dx, &dy) == 0 && w <= KP_CURSOR_SIZE
			&& h <= KP_CURSOR_SIZE && w > 0 && h > 0) {
			c->w = w;
			c->h = h;
			c->dx = dx;
			c->dy = dy;
			c->shown = 1;
			c->changed = 1;
		}
	}
	return c->shown;
}
