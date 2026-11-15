/* The tooltip box (plan phase 9C): ctxthelp.cpp's 160 x 92 bitmap 0x5c9994 on screen layer 1 (atlas P06
 * 0x418f60..0x4193e0) and PresentFrame 0x4c5360's invalidation of its rectangle (P43). The console's callers (the
 * command card, the status panel, the minimap's buttons, the menu button) are in their own files. */
#include "kp_ui.h"

#include <string.h>

#define TEXT_X 2               /* ShowTooltipText, RenderCommandTooltip: the text at (2, 0), clipped from x 2 */
#define TEXT_FORMAT 0x11
#define TEXT_ROW 2             /* SelectTextColor's row */
#define FILL 15                /* 0x6d2377 = 0x6d2368[15] */
#define OUTLINE 16             /* 0x6d2378 */
#define HIDDEN_X 640           /* HideTooltip: the layer off the screen */
#define HIDDEN_Y 400
#define SCREEN_LAST_X 639
#define SCREEN_LAST_Y 479

static KpUiTooltip *tt(KpUi *ui) { return &ui->tooltip; }

void kp_ui_tooltip_init(KpUi *ui)
{
	KpUiTooltip *t = tt(ui);
	/* InitTooltip 0x419220: the layer at (0, 0) with no size, enabled */
	memset(t, 0, sizeof *t);
	t->bitmap.w = KP_UI_TOOLTIP_W;
	t->bitmap.h = KP_UI_TOOLTIP_H;
	t->bitmap.pixels = t->pixels;
	t->enabled = 1;
}

/* PlaceTooltip 0x4193e0. The original also marks the game view's dirty tiles (0x4cac50, 0x4caec0) under the old and
 * the new rectangle; the renderer redraws the view whole (see the dirty grid in kp_ui_core.c) */
static void place(KpUi *ui, int16_t left, int16_t top, int16_t right, int16_t bottom)
{
	KpUiTooltip *t = tt(ui);
	t->x = left;
	t->y = top;
	t->w = (int16_t)(right - left + 1);
	t->h = (int16_t)(bottom - top + 1);
	t->visible = 1;
	t->changed = 1;
}

void kp_ui_tooltip_hide(KpUi *ui)
{
	KpUiTooltip *t = tt(ui);
	/* HideTooltip 0x4191d0 */
	t->x = HIDDEN_X;
	t->y = HIDDEN_Y;
	if (t->visible) t->changed = 1;
	t->visible = 0;
}

/* The bitmap cleared and the text drawn at (2, 0) in the tooltip's font (font slot 0, colour row 2, format 0x11, no
 * gap), clipped to the bitmap from x 2; the target restored, the font deselected by the caller. */
static KpUiSurface *begin_text(KpUi *ui, const uint8_t *text)
{
	KpUiTooltip *t = tt(ui);
	KpUiSurface *saved = ui->text.target;
	int32_t all[4];
	ui->text.target = &t->bitmap;
	memset(t->pixels, ui->ui_colours[FILL], sizeof t->pixels);
	all[0] = all[1] = 0;
	all[2] = t->bitmap.w;
	all[3] = t->bitmap.h;
	kp_ui_image_erase(ui, &t->bitmap, all, NULL, 0, 0); /* the HD interface's images there go (ADR 0013 R4d) */
	(void)kp_ui_text_select_font(&ui->text, ui->fonts[0], ui->font_sizes[0]);
	kp_ui_select_color(&ui->text, TEXT_ROW);
	ui->text.flags = TEXT_FORMAT;
	ui->text.clip_left = TEXT_X;
	ui->text.clip_top = 0;
	ui->text.clip_right = (int16_t)(t->bitmap.w - 1);
	ui->text.clip_bottom = (int16_t)(t->bitmap.h - 1);
	ui->text.gap = 0;
	(void)kp_ui_draw_text_told(ui, NULL, TEXT_X, 0, text);
	return saved;
}

/* DrawRectOutline 0x4ce7d0 at (0, 0) of the bitmap in 0x6d2378, the target restored, the box placed. */
static void finish(KpUi *ui, KpUiSurface *saved, int16_t left, int16_t top, int16_t right, int16_t bottom)
{
	int32_t w = (int16_t)(right - left + 1), h = (int16_t)(bottom - top + 1);
	uint8_t c = ui->ui_colours[OUTLINE];
	kp_ui_fill_rect(ui, 0, 0, w, 1, c);
	kp_ui_fill_rect(ui, 0, h - 1, w, 1, c);
	kp_ui_fill_rect(ui, 0, 0, 1, h, c);
	kp_ui_fill_rect(ui, w - 1, 0, 1, h, c);
	ui->text.target = saved;
	place(ui, left, top, right, bottom);
}

void kp_ui_tooltip_text(KpUi *ui, int32_t x, int32_t y, const uint8_t *text)
{
	/* ShowTooltipText 0x418f60: the box at (x, y) moved left and up to stay on the screen */
	KpUiSurface *saved = begin_text(ui, text);
	int16_t left, top, right, bottom;
	ui->text.font = NULL;
	left = (int16_t)(x < 0 ? 0 : x);
	right = (int16_t)(ui->text.ext_right - ui->text.ext_left + left);
	if (SCREEN_LAST_X - right < 0) {
		left = (int16_t)(left + (SCREEN_LAST_X - right));
		right = SCREEN_LAST_X;
	}
	top = (int16_t)(y < 0 ? 0 : y);
	bottom = (int16_t)(ui->text.ext_bottom - ui->text.ext_top + top);
	if (SCREEN_LAST_Y - bottom < 0) {
		top = (int16_t)(top + (SCREEN_LAST_Y - bottom));
		bottom = SCREEN_LAST_Y;
	}
	finish(ui, saved, left, top, right, bottom);
}

void kp_ui_tooltip_over(KpUi *ui, int32_t x, int32_t bottom_y, const uint8_t *text,
	void (*extra)(KpUi *ui, int16_t rect[4]))
{
	/* RenderCommandTooltip 0x4a0a30: the text's extent, grown by the caller's cost line, over (x, bottom_y) */
	KpUiSurface *saved = begin_text(ui, text);
	int16_t r[4], left, top, right, bottom, w, h;
	r[0] = ui->text.ext_left;
	r[1] = ui->text.ext_top;
	r[2] = ui->text.ext_right;
	r[3] = ui->text.ext_bottom;
	if (extra) extra(ui, r);
	ui->text.font = NULL;
	w = (int16_t)(r[2] - r[0] + 1);
	h = (int16_t)(r[3] - r[1] + 1);
	left = (int16_t)x;
	right = (int16_t)(left - 1 + w);
	if ((int16_t)(SCREEN_LAST_X - right) < 0) {
		int16_t d = (int16_t)(SCREEN_LAST_X - right);
		left = (int16_t)(left + d);
		right = (int16_t)(right + d);
	}
	bottom = (int16_t)bottom_y;
	top = (int16_t)(bottom - h + 1);
	finish(ui, saved, left, top, right, bottom);
}

void kp_ui_tooltip_invalidate(KpUi *ui)
{
	/* PresentFrame 0x4c5360 step 3: the layer's rectangle (clipped to the screen) and the previous one */
	KpUiTooltip *t = tt(ui);
	int16_t *p = t->previous;
	if (!t->enabled) return;
	if (t->w != 0) {
		int16_t r[4];
		r[0] = t->x < 0 ? 0 : t->x;
		r[1] = t->y < 0 ? 0 : t->y;
		r[2] = (int16_t)(t->x + t->w - 1);
		r[3] = (int16_t)(t->y + t->h - 1);
		if (r[2] > SCREEN_LAST_X) r[2] = SCREEN_LAST_X;
		if (r[3] > SCREEN_LAST_Y) r[3] = SCREEN_LAST_Y;
		if (r[0] < r[2] && r[1] < r[3]) kp_ui_add_dirty_rect(ui, r[0], r[1], r[2], r[3]);
		if (p[0] < p[2] && p[1] < p[3]) kp_ui_add_dirty_rect(ui, p[0], p[1], p[2], p[3]);
		memcpy(p, r, sizeof r);
	} else if (p[0] != 0 || p[1] != 0 || p[2] != 0 || p[3] != 0) {
		if (p[0] < p[2] && p[1] < p[3]) kp_ui_add_dirty_rect(ui, p[0], p[1], p[2], p[3]);
		p[1] = (int16_t)(p[3] + 1);
	}
}

const KpUiSurface *kp_ui_tooltip(KpUi *ui, int32_t *x, int32_t *y, int32_t *w, int32_t *h)
{
	KpUiTooltip *t = tt(ui);
	if (!t->visible) return NULL;
	*x = t->x;
	*y = t->y;
	*w = t->w;
	*h = t->h;
	return &t->bitmap;
}
