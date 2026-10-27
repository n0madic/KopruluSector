/*
 * The touch pad's drawing (phase 10, ADR 0014 point 4): not the original's, which has no touch input. Its buttons
 * borrow the tooltip box's look (ShowTooltipText 0x418f60: the UI colours 15 and 16 of 0x6d2368) and the game's font,
 * so they sit with the console's art; their state is the host's (kp_hud.c).
 */
#include "kp_ui.h"

#include <string.h>

#include "kp_game.h"

#define FILL 15        /* the tooltip's fill (0x6d2377) */
#define OUTLINE 16     /* and its outline (0x6d2378) */
#define FONT 2         /* the game's font16 */
#define LIT 3          /* SelectTextColor's codes: yellow, white, grey */
#define HELD 4
#define EMPTY 5

uint32_t kp_ui_groups_held(const KpUi *ui)
{
	const KpGame *g = ui->game;
	uint32_t held = 0;
	int n;
	if (g == NULL) return 0;
	for (n = 0; n < 10; n++) {
		if (g->groups[g->local_player][n][0] != 0) held |= 1u << n;
	}
	return held;
}

static void outline(KpUi *ui, int32_t x, int32_t y, int32_t w, int32_t h, uint8_t c)
{
	kp_ui_fill_rect(ui, x, y, w, 1, c);
	kp_ui_fill_rect(ui, x, y + h - 1, w, 1, c);
	kp_ui_fill_rect(ui, x, y, 1, h, c);
	kp_ui_fill_rect(ui, x + w - 1, y, 1, h, c);
}

void kp_ui_touch_pad(KpUi *ui, KpUiSurface *s, int32_t bw, int32_t bh, int32_t gap, int pressed, int shift)
{
	KpUiSurface *saved = ui->text.target;
	uint32_t held = kp_ui_groups_held(ui);
	int b;
	memset(s->pixels, 0, (size_t)s->w * (size_t)s->h);
	ui->text.target = s;
	for (b = 0; b < KP_UI_PAD_BUTTONS; b++) {
		int32_t x = b * (bw + gap), tw, ty;
		int lit = b == pressed || (b == KP_UI_PAD_SHIFT && shift);
		uint8_t label[8];
		if (x + bw > s->w || bh > s->h) break;
		kp_ui_fill_rect(ui, x, 0, bw, bh, ui->ui_colours[FILL]);
		outline(ui, x, 0, bw, bh, ui->ui_colours[OUTLINE]);
		if (lit) outline(ui, x + 1, 1, bw - 2, bh - 2, ui->ui_colours[OUTLINE]);
		if (b == KP_UI_PAD_SHIFT) {
			memcpy(label, "Shift", 6);
		} else {
			label[0] = (uint8_t)('0' + KP_UI_PAD_GROUP(b));
			label[1] = 0;
		}
		if (kp_ui_text_select_font(&ui->text, ui->fonts[FONT], ui->font_sizes[FONT]) != 0) continue;
		if (b == KP_UI_PAD_SHIFT && kp_ui_text_width(&ui->text, label) > bw - 4) {
			label[1] = 0; /* "S" where "Shift" does not fit */
		}
		kp_ui_select_color(&ui->text, lit ? LIT : b == KP_UI_PAD_SHIFT || (held >> KP_UI_PAD_GROUP(b) & 1) ? HELD : EMPTY);
		ui->text.flags = 0;
		ui->text.gap = 0;
		ui->text.clip_left = (int16_t)x;
		ui->text.clip_top = 0;
		ui->text.clip_right = (int16_t)(x + bw - 1);
		ui->text.clip_bottom = (int16_t)(bh - 1);
		tw = kp_ui_text_width(&ui->text, label);
		ty = (bh - ui->text.max_height) / 2;
		(void)kp_ui_draw_text(&ui->text, x + (bw - tw) / 2, ty > 0 ? ty : 0, label);
	}
	ui->text.font = NULL;
	ui->text.target = saved;
}
