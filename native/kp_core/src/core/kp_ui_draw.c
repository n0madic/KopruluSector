/* kp_ui's drawing: the blits and the stock draw procedures of the dialog controls (atlas P42 0x4c37d0..0x4c47a0,
 * P44 0x4ca110, P45 0x4ce720..0x4cfe60), into the target surface (ui->text.target, a dialog's bitmap). */
#include "kp_ui.h"

#include <string.h>

#define FORMAT_CENTRED 0x14
#define FORMAT_LEFT 0x11
#define FORMAT_RIGHT 0x12
#define FORMAT_BUTTON 0x22
#define FORMAT_BOX_RIGHT_OF 0x21 /* an option button's or check box's text after its box */
#define FORMAT_BOX_LEFT_OF 0x24  /* before it (flag 0x10000000) */
#define FLAG_BOX_RIGHT 0x10000000u /* an option button's or check box's box at the right */
#define MODE_OPAQUE 0
#define MODE_KEYED 1
#define MODE_COLOUR_A 4
#define MODE_COLOUR_B 5
#define FLAG_SELECT_B 0x2000000u
#define FLAG_FONT_MASK 0x4c00u
#define FLAG_HOTKEY_PREFIX 0x300u
#define FLAG_TILED 0x20000000u
#define COLOUR_DEFAULT_BUTTON 5 /* 0x6d236d */
#define COLOUR_FOCUS 16         /* 0x6d2378 */

/* A bitmap descriptor {w, h, pixels}. */
typedef struct {
	int32_t w, h;
	const uint8_t *pixels;
} Bitmap;

/* A blit's pixel rule for the glyph log (kp_ui_glyph_log_blit). */
typedef struct {
	const KpUi *ui;
	int mode;
} BlitRule;

static uint8_t blit_write(const void *ctx, uint8_t src, uint8_t dst)
{
	const BlitRule *r = (const BlitRule *)ctx;
	if (r->mode == MODE_OPAQUE) return src;
	if (r->mode == MODE_COLOUR_A || r->mode == MODE_COLOUR_B) return kp_ui_single_colour(r->ui, r->mode - MODE_COLOUR_A, src, dst);
	return src != 0 ? src : dst;
}

/* BlitWithEffect 0x4ca110 into the target: source rectangle s (l, t, r, b), destination d (l, t, r, b). With the
 * glyph log on (not the original's: the vector text, ADR 0011), the glyphs come along with the pixels. */
static void blit_effect(KpUi *ui, const Bitmap *b, const int32_t s[4], const int32_t d[4], int mode)
{
	KpUiSurface *t = ui->text.target, from;
	int32_t w = s[2] - s[0], h = s[3] - s[1], x, y, rect[4];
	BlitRule r;
	int glyphs = 0;
	r.ui = ui;
	r.mode = mode;
	if (d[2] - d[0] < w) w = d[2] - d[0];
	if (d[3] - d[1] < h) h = d[3] - d[1];
	if (t == NULL || w <= 0 || h <= 0) return;
	from.w = b->w;
	from.h = b->h;
	from.pixels = (uint8_t *)b->pixels; /* read only */
	rect[0] = d[0] > 0 ? d[0] : 0;
	rect[1] = d[1] > 0 ? d[1] : 0;
	rect[2] = d[0] + w < t->w ? d[0] + w : t->w;
	rect[3] = d[1] + h < t->h ? d[1] + h : t->h;
	if (ui->text.log != NULL) glyphs = kp_ui_glyph_log_blit_begin(ui->text.log, &from, t, rect);
	for (y = 0; y < h; y++) {
		const uint8_t *src = b->pixels + (int64_t)(s[1] + y) * b->w + s[0];
		int32_t dy = d[1] + y;
		if (dy < 0 || dy >= t->h) continue;
		for (x = 0; x < w; x++) {
			int32_t dx = d[0] + x;
			uint8_t v = src[x];
			if (dx < 0 || dx >= t->w) continue;
			uint8_t *px = &t->pixels[(int64_t)dy * t->w + dx];
			*px = blit_write(&r, v, *px);
			if (ui->image_log.open && (mode == MODE_OPAQUE || v != 0)) kp_ui_image_put(ui->image_log.open, dx, dy, *px);
		}
	}
	/* the HD interface's images (not the original's, ADR 0013 R4d) come along with the pixels; drawn as a part of an
	 * image, the pixels are that image's alone: the images under it lose them at its end, once it found what lay under
	 * them (KpUiImage.blitted) */
	if (ui->hd && !ui->image_log.open) {
		kp_ui_image_blit(ui, &from, t, s[0] + rect[0] - d[0], s[1] + rect[1] - d[1], rect, mode != MODE_OPAQUE);
	} else if (ui->hd) {
		ui->image_log.open->blitted = 1;
	}
	if (glyphs) {
		KpUiGlyphRule rule;
		rule.write = blit_write;
		rule.ctx = &r;
		rule.keyed = mode != MODE_OPAQUE;
		kp_ui_glyph_log_blit(ui->text.log, &from, t, s[0] + rect[0] - d[0], s[1] + rect[1] - d[1], rect, &rule);
	}
}

/* BlitBitmapClipped 0x4c3c70: the bitmap from (sx, sy) into the rectangle (inclusive), mode from the flags. */
static void blit_clipped(KpUi *ui, const Bitmap *b, int32_t sx, int32_t sy, const int16_t rect[4], uint32_t flags)
{
	int32_t l = rect[0], t = rect[1], r = l + b->w, bt = t + b->h, s[4], d[4];
	int mode;
	if (rect[2] + 1 < r) r = rect[2] + 1;
	if (rect[3] + 1 < bt) bt = rect[3] + 1;
	if (sx < 0) {
		l -= sx;
		sx = 0;
	}
	if (sy < 0) {
		t -= sy;
		sy = 0;
	}
	s[0] = sx;
	s[1] = sy;
	s[2] = r - l + sx + 1 < b->w ? r - l + sx + 1 : b->w;
	s[3] = bt - t + sy + 1 < b->h ? bt - t + sy + 1 : b->h;
	d[0] = l;
	d[1] = t;
	d[2] = r;
	d[3] = bt;
	if (flags & KP_UI_TRANSLUCENT) mode = (flags & FLAG_SELECT_B) ? MODE_COLOUR_B : MODE_COLOUR_A;
	else mode = (flags & KP_UI_TRANSPARENT) ? MODE_KEYED : MODE_OPAQUE;
	if (b->pixels != NULL) blit_effect(ui, b, s, d, mode);
}

void kp_ui_blit_clipped(KpUi *ui, const uint8_t *pixels, int32_t w, int32_t h, int32_t sx, int32_t sy,
	const int16_t rect[4], uint32_t flags)
{
	Bitmap b;
	b.w = w;
	b.h = h;
	b.pixels = pixels;
	blit_clipped(ui, &b, sx, sy, rect, flags);
}

/* GetGrpFrameInfo 0x4cf570: the frame's bitmap and origin, the GRP's size. */
static int grp_frame(const uint8_t *grp, size_t size, int frame, Bitmap *b, int32_t *fx, int32_t *fy, int32_t *gw,
	int32_t *gh)
{
	uint32_t count, off;
	const uint8_t *r;
	if (grp == NULL || size < 6) return -1;
	count = kp_le_u16(grp);
	frame &= 0xff;
	if ((uint32_t)frame >= count) frame = 0;
	if (6 + 8 * (size_t)(frame + 1) > size) return -1;
	r = grp + 6 + 8 * frame;
	off = kp_le_u32(r + 4);
	b->w = r[2] + ((off & 0x80000000u) ? 256 : 0);
	b->h = r[3];
	off &= 0x7fffffffu;
	if ((size_t)off + (size_t)b->w * (size_t)b->h > size) return -1;
	b->pixels = grp + off;
	*fx = r[0];
	*fy = r[1];
	*gw = (int32_t)kp_le_u16(grp + 2);
	*gh = (int32_t)kp_le_u16(grp + 4);
	return 0;
}

/* DrawThemeFrame 0x4c3d60. */
static void theme_frame(KpUi *ui, int frame, int32_t x, int32_t y, const int16_t clip[4], uint32_t flags)
{
	Bitmap b;
	int32_t fx, fy, gw, gh, ox, oy;
	int16_t d[4];
	KpUiImage *im;
	if (grp_frame(ui->theme.grp, ui->theme.grp_size, frame, &b, &fx, &fy, &gw, &gh) != 0) return;
	ox = (uint16_t)(x + fx);
	oy = (uint16_t)(y + fy);
	d[0] = (int16_t)(clip[0] > ox ? clip[0] : ox);
	d[1] = (int16_t)(clip[1] > oy ? clip[1] : oy);
	d[2] = (int16_t)(ox + gw < clip[2] ? ox + gw : clip[2]);
	d[3] = (int16_t)(oy + gh < clip[3] ? oy + gh : clip[3]);
	im = kp_ui_image_begin(ui, ui->theme.grp, frame & 0xff, ox, oy, b.w, b.h);
	blit_clipped(ui, &b, d[0] - ox > 0 ? d[0] - ox : 0, d[1] - oy > 0 ? d[1] - oy : 0, d, flags);
	if (im) kp_ui_image_end(ui, im, 0);
}

/* SelectControlFont 0x4c3660. */
void kp_ui_select_control_font(KpUi *ui, const KpUiControl *c)
{
	int slot = 1;
	switch (c->flags & FLAG_FONT_MASK) {
	case 0x400: slot = 0; break;
	case 0x800: slot = 2; break;
	case 0x4000: slot = 3; break;
	default: break;
	}
	(void)kp_ui_text_select_font(&ui->text, ui->fonts[slot], ui->font_sizes[slot]);
}

/* DrawControlText 0x4c3e70 with the format byte already in the text state's flags. */
static void control_text(KpUi *ui, KpUiControl *c, int32_t x, int32_t y, const int16_t *clip)
{
	uint8_t format = ui->text.flags;
	int colour;
	const uint8_t *text = c->text;
	if (text == NULL) return;
	kp_ui_select_control_font(ui, c);
	ui->text.flags = format;
	if (c->flags & KP_UI_NO_SPACING) ui->text.spacing = 0;
	if (c->flags & KP_UI_DISABLED) colour = 5;
	else if (c->flags & KP_UI_HOVER) colour = 4;
	else if (c->type == 1 || c->type == 2 || c->type == 3 || c->type == 13) colour = 3;
	else if (c->type >= 9 && c->type <= 11) {
		if (c->flags & KP_UI_CONSUMED) colour = 4;
		else if (c->flags & KP_UI_PRESSED) colour = 6;
		else if (c->flags & 0x20000000u) colour = 3;
		else colour = 2;
	} else colour = 2;
	kp_ui_select_color(&ui->text, colour);
	if ((c->flags & FLAG_HOTKEY_PREFIX) && text[0] != 0) text++;
	if (clip != NULL) {
		ui->text.clip_left = clip[0];
		ui->text.clip_top = clip[1];
		ui->text.clip_right = clip[2];
		ui->text.clip_bottom = clip[3];
	} else {
		ui->text.clip_left = c->left;
		ui->text.clip_top = c->top;
		ui->text.clip_right = c->right;
		ui->text.clip_bottom = c->bottom;
	}
	(void)kp_ui_draw_text_told(ui, c, c->left + x, c->top + y, text);
	ui->text.font = NULL; /* the font is deselected */
}

size_t kp_ui_draw_text_told(KpUi *ui, const KpUiControl *c, int32_t x, int32_t y, const uint8_t *text)
{
	size_t at = kp_ui_draw_text(&ui->text, x, y, text);
	if (at != SIZE_MAX && ui->text_cut != NULL) ui->text_cut(ui->text_cut_ctx, ui, c ? (int32_t)(c - ui->controls) : -1, text, at);
	return at;
}

/* DrawDialogBackground 0x4c37f0: the dialog's image, else a fill. */
static void draw_dialog(KpUi *ui, KpUiControl *c, int32_t x, int32_t y, const int16_t clip[4])
{
	KpUiSurface *t = ui->text.target;
	if (c->image != NULL) {
		Bitmap b;
		b.w = c->image_w;
		b.h = c->image_h;
		b.pixels = c->image;
		blit_clipped(ui, &b, x, y, clip, 0);
		return;
	}
	if (t != NULL) {
		uint8_t colour = (c->flags & KP_UI_TRANSPARENT) ? 0 : ui->ui_colours[12]; /* 0x6d2374 */
		int32_t row, col, rect[4];
		for (row = clip[1]; row <= clip[3]; row++) {
			if (row < 0 || row >= t->h) continue;
			for (col = clip[0]; col <= clip[2]; col++) {
				if (col >= 0 && col < t->w) t->pixels[(int64_t)row * t->w + col] = colour;
			}
		}
		rect[0] = clip[0];
		rect[1] = clip[1];
		rect[2] = clip[2] + 1;
		rect[3] = clip[3] + 1;
		kp_ui_image_erase(ui, t, rect, NULL, 0, 0);
	}
}

/* ShouldShowFocus 0x4c2390 into flag 0x1000 (UpdateFocusHighlight 0x4c2640). */
static void update_focus(KpUi *ui, KpUiControl *c)
{
	const KpUiControl *p = c->parent >= 0 ? &ui->controls[c->parent] : NULL;
	int32_t self = (int32_t)(c - ui->controls);
	int show = (c->flags & KP_UI_VISIBLE) && !(c->flags & KP_UI_DISABLED) && p != NULL
		&& (p->active == self || (c->type == 1 && p->active < 0 && !(p->flags & 0x20000000u)));
	if (show) c->flags |= KP_UI_FOCUS_LINE;
	else c->flags &= ~KP_UI_FOCUS_LINE;
}

/* DrawRectOutline 0x4ce7d0 of the control's rectangle in colour (the lines clipped to the target only). */
static void outline(KpUi *ui, const KpUiControl *c, uint8_t colour)
{
	int32_t w = (uint16_t)(c->right - c->left + 1), h = (uint16_t)(c->bottom - c->top + 1);
	kp_ui_fill_rect(ui, c->left, c->top, w, 1, colour);
	kp_ui_fill_rect(ui, c->left, c->top + h - 1, w, 1, colour);
	kp_ui_fill_rect(ui, c->left, c->top, 1, h, colour);
	kp_ui_fill_rect(ui, c->left + w - 1, c->top, 1, h, colour);
}

/* DrawFrameButton 0x4c4300. */
static void draw_frame_button(KpUi *ui, KpUiControl *c, int32_t x, int32_t y, const int16_t clip[4])
{
	int pressed = (c->flags & KP_UI_PRESSED) != 0;
	(void)x;
	(void)y;
	update_focus(ui, c);
	if (!(c->flags & KP_UI_PLAIN)) {
		int frame = (c->flags & KP_UI_DISABLED) ? c->wuser : pressed ? c->wuser + 2 : c->wuser + 1;
		theme_frame(ui, frame, c->left, c->top, clip, c->flags);
	}
	if (c->type == 1) outline(ui, c, ui->ui_colours[COLOUR_DEFAULT_BUTTON]);
	if (c->flags & KP_UI_FOCUS_LINE) outline(ui, c, ui->ui_colours[COLOUR_FOCUS]);
	ui->text.flags = FORMAT_BUTTON;
	control_text(ui, c, pressed ? 2 : 0, 0, NULL);
}

void kp_ui_three_part_bar(KpUi *ui, int32_t x, int32_t y, int32_t total, int32_t piece, int frame,
	const int16_t clip[4], uint32_t flags)
{
	int16_t c[4];
	int32_t n, k;
	c[0] = clip[0];
	c[1] = clip[1];
	c[2] = (int16_t)(clip[0] + total);
	c[3] = clip[3];
	theme_frame(ui, frame, x, y, c, flags);
	if (piece <= 0) return;
	n = (total - piece - 1) / piece;
	c[2] = (int16_t)(clip[0] + total - piece);
	for (k = 1; k <= n; k++) theme_frame(ui, frame + 1, x + k * piece, y, c, flags);
	if (total >= piece) {
		c[0] = (int16_t)(clip[0] > x + piece ? clip[0] : x + piece);
		c[2] = (int16_t)(clip[0] + total);
		theme_frame(ui, frame + 2, x + total - piece, y, c, flags);
	}
}

/* DrawTiledButton 0x4c45a0. */
static void draw_tiled_button(KpUi *ui, KpUiControl *c, const int16_t clip[4])
{
	int frame = (c->flags & KP_UI_DISABLED) ? c->wuser : (c->flags & KP_UI_PRESSED) ? c->wuser + 6 : c->wuser + 3;
	update_focus(ui, c);
	kp_ui_three_part_bar(ui, c->left, c->top, c->right - c->left + 1, ui->theme.tile_piece, frame & 0xff, clip,
		KP_UI_TRANSPARENT);
	if (c->flags & KP_UI_FOCUS_LINE) outline(ui, c, ui->ui_colours[COLOUR_FOCUS]);
	ui->text.flags = FORMAT_BUTTON;
	control_text(ui, c, 0, 0, NULL);
}

/* DrawButton 0x4c3860. */
static void draw_button(KpUi *ui, KpUiControl *c, int32_t x, int32_t y, const int16_t clip[4])
{
	/* A button with its own bitmap (DrawBitmapButton 0x4c3fb0) is not ported: no BIN record of type 1 or 2 has an image
	 * (converter/tests/test_ui.py), and the code gives none an image */
	if (c->flags & FLAG_TILED) draw_tiled_button(ui, c, clip);
	else draw_frame_button(ui, c, x, y, clip);
}

/* DrawImage 0x4c3b20. */
static void draw_image(KpUi *ui, KpUiControl *c, int32_t x, int32_t y, const int16_t clip[4])
{
	Bitmap b;
	if (c->image == NULL) return;
	b.w = c->image_w;
	b.h = c->image_h;
	b.pixels = c->image;
	blit_clipped(ui, &b, x, y, clip, c->flags);
}

static void draw_text_format(KpUi *ui, KpUiControl *c, uint8_t format)
{
	if (c->text == NULL) return;
	ui->text.flags = format;
	control_text(ui, c, 0, 0, NULL);
}

static void draw_left_text(KpUi *ui, KpUiControl *c, int32_t x, int32_t y, const int16_t clip[4])
{
	(void)x;
	(void)y;
	(void)clip;
	draw_text_format(ui, c, FORMAT_LEFT);
}

static void draw_right_text(KpUi *ui, KpUiControl *c, int32_t x, int32_t y, const int16_t clip[4])
{
	(void)x;
	(void)y;
	(void)clip;
	draw_text_format(ui, c, FORMAT_RIGHT);
}

static void draw_centred_text(KpUi *ui, KpUiControl *c, int32_t x, int32_t y, const int16_t clip[4])
{
	(void)x;
	(void)y;
	(void)clip;
	draw_text_format(ui, c, FORMAT_CENTRED);
}

/* The text's last extent outlined (0x6d3040.., DrawRectOutline 0x4ce7d0) in the focus colour. */
static void text_extent_outline(KpUi *ui)
{
	const KpUiText *t = &ui->text;
	int32_t w = (uint16_t)(t->ext_right - t->ext_left + 1), h = (uint16_t)(t->ext_bottom - t->ext_top + 1);
	uint8_t colour = ui->ui_colours[COLOUR_FOCUS];
	kp_ui_fill_rect(ui, t->ext_left, t->ext_top, w, 1, colour);
	kp_ui_fill_rect(ui, t->ext_left, t->ext_top + h - 1, w, 1, colour);
	kp_ui_fill_rect(ui, t->ext_left, t->ext_top, 1, h, colour);
	kp_ui_fill_rect(ui, t->ext_left + w - 1, t->ext_top, 1, h, colour);
}

/* The box of an option button or a check box (S pixels square): at the left (the right with flag 0x10000000), centred
 * vertically; then the text after it (format 0x21) or before it (0x24). */
static void box_place(const KpUiControl *c, int32_t size, int32_t *bx, int32_t *by)
{
	*bx = (c->flags & FLAG_BOX_RIGHT) ? c->right - size : c->left;
	*by = c->top + (c->bottom - size - c->top) / 2;
}

static void box_label(KpUi *ui, KpUiControl *c, int32_t size)
{
	if (c->text == NULL) return;
	if (c->flags & FLAG_BOX_RIGHT) {
		int16_t rect[4];
		rect[0] = c->left;
		rect[1] = c->top;
		rect[2] = (int16_t)(c->right - 4 - size);
		rect[3] = c->bottom;
		kp_ui_draw_control_text(ui, c, 0, 0, rect, FORMAT_BOX_LEFT_OF);
	} else {
		kp_ui_draw_control_text(ui, c, size + 4, 0, NULL, FORMAT_BOX_RIGHT_OF);
	}
	if (c->flags & KP_UI_FOCUS_LINE) text_extent_outline(ui);
}

/* DrawOptionButton 0x4c3890: theme frame 6 disabled, else 7 + 2 checked (+1 pressed). */
static void draw_option(KpUi *ui, KpUiControl *c, int32_t x, int32_t y, const int16_t clip[4])
{
	int32_t size = ui->theme.option_size, bx, by;
	int checked = (uint8_t)c->words36[0], frame;
	(void)x;
	(void)y;
	box_place(c, size, &bx, &by);
	update_focus(ui, c);
	if (c->flags & KP_UI_DISABLED) frame = 6;
	else frame = (c->flags & KP_UI_PRESSED) ? (checked + 4) * 2 : checked * 2 + 7;
	theme_frame(ui, (uint8_t)frame, bx, by, clip, KP_UI_TRANSPARENT);
	box_label(ui, c, size);
}

/* DrawCheckBox 0x4c39b0: disabled 139 checked, else wUser; enabled wUser + 1 + 2 checked (+1 pressed); the box's
 * focus outline. */
static void draw_check(KpUi *ui, KpUiControl *c, int32_t x, int32_t y, const int16_t clip[4])
{
	int32_t size = ui->theme.check_size, bx, by;
	int checked = (uint8_t)c->words36[0], frame;
	(void)x;
	(void)y;
	box_place(c, size, &bx, &by);
	update_focus(ui, c);
	if (c->flags & KP_UI_DISABLED) frame = c->words36[0] ? 139 : (uint8_t)c->wuser;
	else frame = ((c->flags & KP_UI_PRESSED) ? (checked + 1) * 2 : checked * 2 + 1) + (uint8_t)c->wuser;
	theme_frame(ui, (uint8_t)frame, bx, by, clip, KP_UI_TRANSPARENT);
	if (c->flags & KP_UI_FOCUS_LINE) {
		uint8_t colour = ui->ui_colours[COLOUR_FOCUS];
		kp_ui_fill_rect(ui, bx, by, size, 1, colour);
		kp_ui_fill_rect(ui, bx, by + size - 1, size, 1, colour);
		kp_ui_fill_rect(ui, bx, by, 1, size, colour);
		kp_ui_fill_rect(ui, bx + size - 1, by, 1, size, colour);
	}
	box_label(ui, c, size);
}

KpUiDraw kp_ui_type_draw(int type)
{
	switch (type) {
	case 0: return draw_dialog;
	case 1: case 2: return draw_button;
	case 3: return draw_option;
	case 4: return draw_check;
	case 5: return draw_image;
	case 6: case 7: return kp_ui_scrollbar_draw;
	case 8: return kp_ui_edit_draw;
	case 9: return draw_left_text;
	case 10: return draw_right_text;
	case 11: return draw_centred_text;
	case 12: return kp_ui_list_draw;
	default: return NULL; /* type 13, a combo box: not ported, only the multiplayer and custom games' dialogs and the message filter have one (converter/tests/test_ui.py) */
	}
}

void kp_ui_draw_theme_frame(KpUi *ui, int frame, int32_t x, int32_t y, const int16_t clip[4], uint32_t flags)
{
	theme_frame(ui, frame, x, y, clip, flags);
}

void kp_ui_draw_control_text(KpUi *ui, KpUiControl *c, int32_t x, int32_t y, const int16_t *clip, uint8_t format)
{
	ui->text.flags = format;
	control_text(ui, c, x, y, clip);
}

void kp_ui_update_focus(KpUi *ui, KpUiControl *c)
{
	update_focus(ui, c);
}

/* ClipBlit 0x4cfe60 (s16 arithmetic) and the copy of BlitOpaque 0x4ce720 or BlitTransparent 0x4cf5e0 (index 0 kept). */
static void clip_blit(KpUi *ui, int32_t x, int32_t y, const uint8_t *pixels, int32_t w, int32_t h, const int16_t src[4],
	int keyed)
{
	KpUiSurface *t = ui->text.target;
	int32_t sx0 = src[0], sy0 = src[1], sx1 = src[2], sy1 = src[3], dx = x, dy = y, row, col;
	if (t == NULL || pixels == NULL || sx0 >= w || sy0 >= h || sx1 < 0 || sy1 < 0) return;
	if (sx0 < 0) {
		dx -= sx0;
		sx0 = 0;
	}
	if (sx1 >= w) sx1 = w - 1;
	if (sy0 < 0) {
		dy -= sy0;
		sy0 = 0;
	}
	if (sy1 >= h) sy1 = h - 1;
	if (dx < 0) {
		sx0 -= dx;
		dx = 0;
	} else if (dx >= t->w) return;
	if (dy < 0) {
		sy0 -= dy;
		dy = 0;
	} else if (dy >= t->h) return;
	if (sx1 - sx0 + dx < 0 || sy1 - sy0 + dy < 0) return;
	if (sx1 - sx0 + dx - t->w + 1 > 0) sx1 -= sx1 - sx0 + dx - t->w + 1;
	if (sy1 - sy0 + dy - t->h + 1 > 0) sy1 -= sy1 - sy0 + dy - t->h + 1;
	for (row = 0; row <= sy1 - sy0; row++) {
		uint8_t *d = t->pixels + (int64_t)(dy + row) * t->w + dx;
		const uint8_t *p = pixels + (int64_t)(sy0 + row) * w + sx0;
		if (!keyed) {
			memcpy(d, p, (size_t)(sx1 - sx0 + 1));
			continue;
		}
		for (col = 0; col <= sx1 - sx0; col++) {
			if (p[col] != 0) d[col] = p[col];
		}
	}
	if (ui->hd) { /* the HD interface's images (not the original's, ADR 0013 R4d) come along with the pixels */
		KpUiSurface from;
		int32_t rect[4];
		from.w = w;
		from.h = h;
		from.pixels = (uint8_t *)pixels; /* read only */
		rect[0] = dx;
		rect[1] = dy;
		rect[2] = dx + sx1 - sx0 + 1;
		rect[3] = dy + sy1 - sy0 + 1;
		kp_ui_image_blit(ui, &from, t, sx0, sy0, rect, keyed);
	}
}

void kp_ui_blit_opaque(KpUi *ui, int32_t x, int32_t y, const uint8_t *pixels, int32_t w, int32_t h,
	const int16_t src[4])
{
	clip_blit(ui, x, y, pixels, w, h, src, 0);
}

void kp_ui_blit_transparent(KpUi *ui, int32_t x, int32_t y, const uint8_t *pixels, int32_t w, int32_t h,
	const int16_t src[4])
{
	clip_blit(ui, x, y, pixels, w, h, src, 1);
}

int kp_ui_grp_bitmap(const uint8_t *grp, size_t size, int frame, int32_t *w, int32_t *h, const uint8_t **pixels)
{
	Bitmap b;
	int32_t fx, fy, gw, gh;
	if (grp_frame(grp, size, frame, &b, &fx, &fy, &gw, &gh) != 0) return -1;
	*w = b.w;
	*h = b.h;
	*pixels = b.pixels;
	return 0;
}

void kp_ui_fill_rect(KpUi *ui, int32_t x, int32_t y, int32_t w, int32_t h, uint8_t colour)
{
	/* FillRect 0x4ce9c0: (h & 0xffff) lines of HLine 0x4ccb30 */
	KpUiSurface *t = ui->text.target;
	int32_t i, col, rect[4];
	if (t == NULL) return;
	for (i = 0; i < (int16_t)(h & 0xffff); i++) {
		int32_t row = y + i;
		if (row < 0 || row >= t->h) continue;
		for (col = x; col < x + w; col++) {
			if (col >= 0 && col < t->w) t->pixels[(int64_t)row * t->w + col] = colour;
		}
	}
	rect[0] = x;
	rect[1] = y;
	rect[2] = x + w;
	rect[3] = y + (int16_t)(h & 0xffff);
	kp_ui_image_erase(ui, t, rect, NULL, 0, 0);
}

void kp_ui_draw_grp_rle(KpUi *ui, int32_t x, int32_t y, const uint8_t *grp, size_t size, int frame, const int32_t crop[4])
{
	/* DrawGrpFrameRemapped 0x405e1e: rows from crop top, the left skip consumed packet-wise, the right clip truncating
	 * the crossing packet, each opaque pixel through the colour map 0x501021 */
	KpUiSurface *t = ui->text.target;
	const uint8_t *rec, *base;
	int32_t rows = crop[3], row = 0, pitch;
	uint32_t off;
	KpUiImage *im = NULL;
	int k, tinted = 0;
	if (t == NULL || grp == NULL || size < 6) return;
	if ((uint32_t)(frame & 0xffff) >= (kp_le_u16(grp) & 0x7fff)) frame = 0;
	rec = grp + 6 + 8 * (size_t)frame;
	if ((size_t)(rec - grp) + 8 > size) return;
	off = kp_le_u32(rec + 4);
	if (off >= size) return;
	base = grp + off;
	pitch = t->w;
	if (ui->hd) { /* the HD interface's image (not the original's, ADR 0013 R4d): the frame where it lies uncropped */
		im = kp_ui_image_begin(ui, grp, frame & 0xffff, x - crop[0], y - crop[1], rec[2], rec[3]);
		for (k = 0; k < 256 && !tinted; k++) tinted = ui->remap[k] != (uint8_t)k;
		if (im) { /* the whole GRP's rectangle and the wireframe parts' colours (HD art drawn whole or in parts) */
			im->canvas[0] = x - crop[0] - rec[0];
			im->canvas[1] = y - crop[1] - rec[1];
			im->canvas[2] = (int32_t)kp_le_u16(grp + 2);
			im->canvas[3] = (int32_t)kp_le_u16(grp + 4);
			for (k = 0; k < KP_UI_WIRE_COLOURS; k++) im->parts[k] = ui->remap[KP_UI_WIRE_INDEXES[k]];
		}
	}
	while (--rows >= 0) {
		const uint8_t *src = base + kp_le_u16(base + 2 * (crop[1] + row));
		int32_t skip = crop[0], width = crop[2], dx = x, dy = y + row, n;
		uint8_t packet;
		row++;
/* v (*src++ in a literal run) is read once, and only for a pixel inside the target, as before the image log */
#define PUT(v) do { if (dx >= 0 && dx < t->w && dy >= 0 && dy < t->h) { uint8_t pv_ = ui->remap[(v)]; \
	t->pixels[(int64_t)dy * pitch + dx] = pv_; if (im) kp_ui_image_put(im, dx, dy, pv_); } dx++; } while (0)
		/* the left skip */
		while (skip > 0) {
			packet = *src++;
			if (packet & 0x80) {
				n = packet & 0x7f;
				skip -= n;
				if (skip < 0) {
					dx -= skip;
					width += skip;
				}
			} else if (packet & 0x40) {
				n = packet & 0x3f;
				src++;
				skip -= n;
				if (skip < 0) {
					n = -skip;
					src--;
					goto repeat;
				}
			} else {
				n = packet;
				src += n;
				skip -= n;
				if (skip < 0) {
					n = -skip;
					src -= n;
					goto literal;
				}
			}
		}
		if (width <= 0) continue;
		while (width > 0) {
			packet = *src++;
			if (packet & 0x80) {
				n = packet & 0x7f;
				width -= n;
				if (width < 0) n += width;
				dx += n;
				continue;
			} else if (packet & 0x40) {
				n = packet & 0x3f;
			repeat:
				width -= n;
				if (width < 0) n += width;
				while (n-- > 0) PUT(*src);
				src++;
				continue;
			} else {
				n = packet;
			literal:
				width -= n;
				if (width < 0) n += width;
				while (n-- > 0) PUT(*src++);
			}
		}
#undef PUT
	}
	if (im) kp_ui_image_end(ui, im, tinted);
}

int kp_ui_grp_frame_size(const uint8_t *grp, size_t size, int frame, int32_t *w, int32_t *h)
{
	const uint8_t *rec;
	if (grp == NULL || size < 6) return -1;
	if ((uint32_t)(frame & 0xffff) >= (kp_le_u16(grp) & 0x7fff)) frame = 0;
	rec = grp + 6 + 8 * (size_t)frame;
	if ((size_t)(rec - grp) + 8 > size) return -1;
	*w = rec[2];
	*h = rec[3];
	return 0;
}
