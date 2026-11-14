/*
 * The interface's canvas and cursor (ADR 0011): kp_ui composes the console art and the panels of a layout
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

/* --- the console (ADR 0011, 2026-11-05; ADR 0013 R4d): composed from StarCraft: Remastered's pieces --- */

#define CENTRE_TRIM_ROWS 36 /* the centre piece's rows (from 352) over the right block's first panel (the menu button, 388) */

/* Whether the pack holds the console's HD art, and its frames. */
static int console_asset(const KpCanvas *c, const KpUi *ui, int32_t *count)
{
	int32_t first;
	int flags;
	return c->hd_ui && c->canvas.ids_next && ui->console_file
		&& kp_hd_ui_asset(c->hd_ui, ui->console_file->path, ui->console_file->path_len, &first, count, &flags) >= 0
		&& (flags & KP_HD_UI_CONSOLE);
}

/* A piece's item: frame k of the console's HD asset (its frame 0 for every group: a replay's console, one picture),
 * its group's top-left with the console PCX's offsets; 0 without. */
static uint16_t piece_item(KpCanvas *c, KpUi *ui, const KpLayoutGroup *g, int k)
{
	KpUiCanvasItem it;
	int32_t count;
	if (!console_asset(c, ui, &count) || (ui->console_whole ? k >= KP_LAYOUT_GROUPS : k >= count)) return 0;
	memset(&it, 0, sizeof it);
	it.path = ui->console_file->path;
	it.path_len = ui->console_file->path_len;
	it.frame = ui->console_whole ? 0 : k;
	it.x = g->x;
	it.y = g->y;
	it.w = ui->console.w;
	it.h = ui->console.h;
	if (k >= KP_UI_TILE_LEFT) { /* its image's own column 0 at the first copy's; repeating every period */
		const KpUiConsolePiece *p = &ui->console_pieces[k];
		it.x = g->x + p->x;
		it.w = p->period;
		it.tiled = 1;
	}
	return kp_ui_canvas_add_item(&c->canvas, &it);
}

/* Rows row0..row1 of piece k under an item, on the screen's columns from..to. */
static void draw_rows(KpCanvas *c, KpUi *ui, const KpLayoutGroup *g, int k, uint16_t item, int32_t row0, int32_t row1,
	int32_t from, int32_t to)
{
	KpUiConsolePiece rows = ui->console_pieces[k];
	int32_t x = g->x + rows.x, y = g->y + rows.y + row0;
	rows.cells += (size_t)row0 * (size_t)rows.w;
	rows.h = row1 - row0;
	kp_ui_canvas_item(&c->canvas, item);
	kp_ui_canvas_piece(ui, &c->canvas, &rows, x, y, from, to);
	kp_ui_canvas_item(&c->canvas, 0);
}

static void draw_piece(KpCanvas *c, KpUi *ui, const KpLayoutGroup *g, int k, int32_t from, int32_t to)
{
	draw_rows(c, ui, g, k, piece_item(c, ui, g, k), 0, ui->console_pieces[k].h, from, to);
}

/* The original's console, the screen's whole width: its groups in their places. */
static void compose_original(KpCanvas *c, KpUi *ui, const KpArrangement *a)
{
	int k;
	for (k = 0; k < KP_LAYOUT_GROUPS; k++) {
		const KpLayoutGroup *g = &a->groups[k];
		kp_ui_canvas_item(&c->canvas, piece_item(c, ui, g, k));
		kp_ui_canvas_console(ui, &c->canvas, g->x, g->y, g->x0, g->x1);
		kp_ui_canvas_item(&c->canvas, 0);
	}
}

/* The pieces whole, each over the last: the left and right ones with their wings, the wings' tiles where the screen is
 * wider than they reach, the centre piece over their ends. In the original's art (no HD art for the items) its own pixels
 * then go over them: the pieces show only where it has none. */
static void compose_pieces(KpCanvas *c, KpUi *ui, const KpArrangement *a, int hd)
{
	const KpLayoutGroup *left = &a->groups[0], *status = &a->groups[1], *right = &a->groups[2];
	const KpUiConsolePiece *p = ui->console_pieces, *centre = &p[KP_UI_PIECE_CENTRE];
	int32_t middle = status->x + centre->x + centre->w / 2; /* where the wings' tiles end: under the centre piece, past its margins */
	int32_t trim = centre->h < CENTRE_TRIM_ROWS ? centre->h : CENTRE_TRIM_ROWS;
	uint16_t centre_item;
	int k;
	draw_piece(c, ui, right, KP_UI_PIECE_RIGHT, 0, a->w);
	if (left->above) { /* a narrow screen's minimap block stands alone: the centre piece's left margin at the screen's edge */
		draw_piece(c, ui, right, KP_UI_TILE_RIGHT, 0, middle);
	} else {
		draw_piece(c, ui, left, KP_UI_PIECE_LEFT, 0, a->w);
		if (left->x + p[KP_UI_PIECE_LEFT].x + p[KP_UI_PIECE_LEFT].w < middle) {
			draw_piece(c, ui, left, KP_UI_TILE_LEFT, left->x + p[KP_UI_TILE_LEFT].x, middle);
		}
		if (right->x + p[KP_UI_PIECE_RIGHT].x > middle) {
			draw_piece(c, ui, right, KP_UI_TILE_RIGHT, middle, right->x + p[KP_UI_TILE_RIGHT].x + p[KP_UI_TILE_RIGHT].period);
		}
	}
	/* its top whole (the trim runs on into the blocks'), below it the blocks' panels lie over its margins: an item names one
	 * item under it, the block's as on the original's screen */
	centre_item = piece_item(c, ui, status, KP_UI_PIECE_CENTRE);
	draw_rows(c, ui, status, KP_UI_PIECE_CENTRE, centre_item, 0, trim, 0, a->w);
	draw_rows(c, ui, status, KP_UI_PIECE_CENTRE, centre_item, trim, centre->h, left->above ? 0 : left->right, right->left);
	if (left->above) draw_piece(c, ui, left, KP_UI_PIECE_LEFT, 0, left->right);
	if (hd) return;
	for (k = 0; k < KP_LAYOUT_GROUPS; k++) {
		const KpLayoutGroup *g = &a->groups[k];
		kp_ui_canvas_console_owned(ui, &c->canvas, k, g->x, g->y, g->above ? g->x1 : ui->console.w);
	}
}

/* The fluff dialogs over the panels (CreateFluffDialogs 0x4a5a40): only a replay's list reaches into one, its
 * (138, 367, 358, 22) over the status panel's and the menu button's first row 388 (the emulator's frames); the races'
 * end above their panels. Each group's part at its place, as its console. TODO: the HD pieces' layouts (the HD art has
 * no original pixels to repeat; the row stays the panels'). */
static void compose_fluff(KpCanvas *c, KpUi *ui, const KpArrangement *a, int original, int hd)
{
	int k;
	if (!original && hd) return;
	for (k = 0; k < KP_LAYOUT_GROUPS; k++) {
		const KpLayoutGroup *g = &a->groups[k];
		if (original) {
			kp_ui_canvas_item(&c->canvas, piece_item(c, ui, g, k));
			kp_ui_canvas_fluff(ui, &c->canvas, -1, g->x, g->y, g->x0, g->x1);
			kp_ui_canvas_item(&c->canvas, 0);
		} else {
			kp_ui_canvas_fluff(ui, &c->canvas, k, g->x, g->y, 0, 0);
		}
	}
}

int kp_canvas_compose(KpCanvas *c, KpUi *ui, int32_t w, int32_t h, const KpPanel *panels, int count, int layout)
{
	KpArrangement *a = &c->arrangement;
	int32_t x, y, tx, ty, tw, th, frames;
	int k, hd, original, pieces = ui->console_pieces[KP_UI_PIECE_LEFT].cells != NULL;
	if (resize(c, w, h) != 0) return -1;
	hd = console_asset(c, ui, &frames);
	kp_layout_arrange(a, w, h, layout < 0 ? kp_layout_choose(w, pieces, ui->console_edges) : layout);
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
	/* the original's screen in the original's art: its console, pixel for pixel */
	original = !pieces || a->layout == KP_LAYOUT_CLASSIC || a->layout == KP_LAYOUT_NARROW_ORIGINAL
		|| (w == KP_LAYOUT_MIN_W && !hd);
	if (original) {
		compose_original(c, ui, a);
	} else {
		compose_pieces(c, ui, a, hd);
	}
	for (k = 0; k < count; k++) {
		if (panels[k].backdrop || panels[k].lines) continue; /* drawn above */
		if (kp_layout_place(a, panels[k].bin, panels[k].rect, &x, &y)) {
			kp_ui_canvas_dialog(ui, &c->canvas, panels[k].dialog, x, y);
			add_place(c, panels[k].dialog, x, y);
		}
	}
	compose_fluff(c, ui, a, original, hd);
	if (c->overlay != NULL) kp_ui_canvas_translucent(ui, &c->canvas, c->overlay, c->overlay_x, c->overlay_y);
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
		kp_ui_canvas_screen(ui, &c->canvas, dialog, x, y);
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
