/*
 * The touch pad's drawing (phase 10, ADR 0014 point 4): not the original's, which has no touch input. Its buttons
 * borrow the tooltip box's outline (ShowTooltipText 0x418f60: the UI colour 16 of 0x6d2368), the translucent in-game
 * dialogs' fill (the blend colour of BlitSingleColourBlend 0x4ca9b0; the tooltip's opaque fill where no glue set it)
 * and the game's font, so they sit with the console's art; their state is the host's (kp_hud.c).
 */
#include "kp_ui.h"

#include <string.h>

#define FILL 15        /* the tooltip's fill (0x6d2377): where no glue set the blend colour (a game started directly) */
#define OUTLINE 16     /* the tooltip's outline (0x6d2378) */
#define FONT 2         /* the game's font16 */
#define LIT 3          /* SelectTextColor's codes: yellow, white, grey */
#define HELD 4
#define EMPTY 5

uint32_t kp_ui_groups_held(const KpUi *ui)
{
	uint32_t held = 0;
	int n;
	for (n = 0; n < KP_UI_PAD_GROUPS; n++) {
		if (kp_ui_group_held(ui, n)) held |= 1u << n;
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

/* A button's label: its digit, "Shift" ("S" where the word does not fit) or the toggle's "1-0" (the groups' digits). */
static void label_of(KpUi *ui, int b, int32_t bw, uint8_t label[8])
{
	if (b == KP_UI_PAD_SHIFT) {
		memcpy(label, "Shift", 6);
		if (kp_ui_text_width(&ui->text, label) > bw - 4) label[1] = 0;
	} else if (b == KP_UI_PAD_TOGGLE) {
		memcpy(label, "1-0", 4);
	} else {
		label[0] = (uint8_t)('0' + KP_UI_PAD_GROUP(b));
		label[1] = 0;
	}
}

void kp_ui_touch_pad(KpUi *ui, KpUiSurface *s, int32_t bw, int32_t bh, int32_t gap, const int *buttons, int count,
	int pressed, int shift, int open, uint32_t held)
{
	KpUiSurface *saved = ui->text.target;
	uint8_t fill = ui->effects.colour[0] != 0 ? ui->effects.colour[0] : ui->ui_colours[FILL];
	int k, font = kp_ui_text_select_font(&ui->text, ui->fonts[FONT], ui->font_sizes[FONT]) == 0 && ui->text.font != NULL;
	memset(s->pixels, 0, (size_t)s->w * (size_t)s->h);
	ui->text.target = s;
	for (k = 0; k < count; k++) {
		int b = buttons[k];
		int32_t y = k * (bh + gap), tw, ty;
		int lit = b == pressed || (b == KP_UI_PAD_SHIFT && shift) || (b == KP_UI_PAD_TOGGLE && open);
		uint8_t label[8];
		if (bw > s->w || y + bh > s->h) break;
		kp_ui_fill_rect(ui, 0, y, bw, bh, fill); /* the blend colour: blended over what lies under it */
		outline(ui, 0, y, bw, bh, ui->ui_colours[OUTLINE]);
		if (lit) outline(ui, 1, y + 1, bw - 2, bh - 2, ui->ui_colours[OUTLINE]);
		if (b == KP_UI_PAD_SHIFT && shift == KP_UI_PAD_SHIFT_LOCKED) outline(ui, 2, y + 2, bw - 4, bh - 4, ui->ui_colours[OUTLINE]); /* locked */
		if (!font) continue;
		label_of(ui, b, bw, label);
		kp_ui_select_color(&ui->text, lit ? LIT : b >= KP_UI_PAD_SHIFT || (held >> KP_UI_PAD_GROUP(b) & 1) ? HELD : EMPTY);
		ui->text.flags = 0;
		ui->text.gap = 0;
		ui->text.clip_left = 0;
		ui->text.clip_top = (int16_t)y;
		ui->text.clip_right = (int16_t)(bw - 1);
		ui->text.clip_bottom = (int16_t)(y + bh - 1);
		tw = kp_ui_text_width(&ui->text, label);
		ty = (bh - ui->text.max_height) / 2;
		(void)kp_ui_draw_text(&ui->text, (bw - tw) / 2, y + (ty > 0 ? ty : 0), label);
	}
	ui->text.font = NULL;
	ui->text.target = saved;
}
