/* The edit controls (edit.cpp, atlas P43 0x4c77d0..0x4c8110): EditBoxProc 0x4c7880 with the caret's blink on timer
 * 2, the cursor keys, insert and overwrite, typing mode (the mouse cursor hidden until the mouse moves), the cursor
 * placed at a click, and DrawEditText 0x4c7c40 with its scroll. The texts are the interface's (KP_UI_EDITS of them).
 * Single-byte text only: the Korean lead and trail bytes are not told apart. */
#include "kp_ui.h"

#include <stdio.h>
#include <string.h>

#define CARET_TIMER 2
#define CARET_ON_MS 500
#define CARET_OFF_MS 250
#define CARET_SHOWN KP_UI_PRESSED   /* +0x18 0x40000000: the caret is visible */
#define CARET_GLYPH 0xa6
#define MODE_MODIFIED 0x01
#define MODE_INSERT 0x10
#define COLOUR_DEFAULT 2
#define COLOUR_FOCUS 0              /* 0x6d2368 */
#define COLOUR_OUTLINE 16           /* 0x6d2378 */
#define FORMAT_EDIT 0x11
#define VK_BACK 8
#define VK_END 0x23
#define VK_HOME 0x24
#define VK_LEFT 0x25
#define VK_RIGHT 0x27
#define VK_INSERT 0x2d
#define VK_DELETE 0x2e

static int slot_of(KpUi *ui, int32_t control)
{
	int k;
	for (k = 0; k < KP_UI_EDITS; k++) {
		if (ui->edits[k].control == control) return k;
	}
	for (k = 0; k < KP_UI_EDITS; k++) {
		if (ui->edits[k].control < 0) {
			memset(&ui->edits[k], 0, sizeof ui->edits[k]);
			ui->edits[k].control = control;
			ui->edits[k].mode = MODE_INSERT;
			ui->controls[control].text = ui->edits[k].text;
			return k;
		}
	}
	return -1;
}

uint8_t *kp_ui_edit_buffer(KpUi *ui, int32_t control)
{
	int k = slot_of(ui, control);
	return k >= 0 ? ui->edits[k].text : NULL;
}

void kp_ui_edit_set_text(KpUi *ui, int32_t control, const uint8_t *text)
{
	/* SetEditText 0x4c8110 */
	int k = slot_of(ui, control);
	if (k < 0) return;
	snprintf((char *)ui->edits[k].text, KP_UI_EDIT_SIZE, "%s", text != NULL ? (const char *)text : "");
	ui->edits[k].cursor = (uint8_t)strlen((const char *)ui->edits[k].text);
	ui->edits[k].mode |= MODE_MODIFIED;
	if (ui->controls[control].flags & KP_UI_VISIBLE) kp_ui_mark_dirty(ui, control);
}

static void caret_tick(KpUi *ui, int32_t owner);

static void caret_on(KpUi *ui, int32_t self)
{
	ui->controls[self].flags |= CARET_SHOWN;
	(void)kp_ui_set_timer(&ui->timers, self, CARET_TIMER, CARET_ON_MS, kp_ui_callback(ui, caret_tick), ui->now);
	kp_ui_mark_dirty(ui, self);
}

static void caret_tick(KpUi *ui, int32_t owner)
{
	/* EditCaretBlinkTimer 0x4c7840: shown 500 ms, hidden 250 ms */
	KpUiControl *c = &ui->controls[owner];
	int was = (c->flags & CARET_SHOWN) != 0;
	c->flags ^= CARET_SHOWN;
	(void)kp_ui_set_timer(&ui->timers, owner, CARET_TIMER, was ? CARET_OFF_MS : CARET_ON_MS,
		kp_ui_callback(ui, caret_tick), ui->now);
	kp_ui_mark_dirty(ui, owner);
}

static void typing(KpUi *ui, int32_t self, int k, int on)
{
	/* EditSetTypingMode 0x4c7e50 */
	if (on && !ui->edits[k].typing) {
		ui->cursor_hidden = 1;
		kp_ui_set_capture(ui, self, KP_UI_MOUSE_MOVE);
		kp_ui_set_capture(ui, self, KP_UI_LEFT_DOWN);
		kp_ui_set_capture(ui, self, KP_UI_RIGHT_DOWN);
		ui->edits[k].typing = 1;
	} else if (!on && ui->edits[k].typing) {
		ui->cursor_hidden = 0;
		kp_ui_release_refs(ui, self);
		ui->edits[k].typing = 0;
	}
}

static void delete_char(KpUi *ui, int32_t self, int k, int forward)
{
	/* EditDeleteChar 0x4c8090 */
	uint8_t *t = ui->edits[k].text;
	size_t n = strlen((const char *)t);
	int at;
	if (n == 0) return;
	if (forward) {
		at = ui->edits[k].cursor;
	} else {
		if (ui->edits[k].cursor == 0) return;
		at = --ui->edits[k].cursor;
	}
	if ((size_t)at < n) memmove(t + at, t + at + 1, n - (size_t)at);
	if (ui->edits[k].scroll != 0) ui->edits[k].mode |= MODE_MODIFIED;
	kp_ui_mark_dirty(ui, self);
}

static void insert_char(KpUi *ui, int32_t self, int k, uint8_t ch)
{
	/* EditInsertChar 0x4c77d0 */
	uint8_t *t = ui->edits[k].text;
	size_t n = strlen((const char *)t), at = ui->edits[k].cursor;
	if (n + 1 >= KP_UI_EDIT_SIZE) return;
	if (at >= n) t[n + 1] = 0;
	else if (ui->edits[k].mode & MODE_INSERT) memmove(t + at + 1, t + at, n - at + 1);
	t[at] = ch;
	ui->edits[k].cursor++;
	ui->edits[k].mode |= MODE_MODIFIED;
	kp_ui_mark_dirty(ui, self);
}

/* The control font has a glyph for the character (0x4cde90). */
static int has_glyph(KpUi *ui, KpUiControl *c, uint8_t ch)
{
	int ok;
	kp_ui_select_control_font(ui, c);
	ok = ui->text.font != NULL && ch >= ui->text.font[4] && ch <= ui->text.font[5];
	ui->text.font = NULL;
	return ok;
}

static void place_cursor(KpUi *ui, KpUiControl *c, int k, const KpUiEvent *e)
{
	/* EditPlaceCursorAtClick 0x4c7da0 */
	const KpUiControl *d = &ui->controls[c->parent];
	int32_t x = e->x - d->left, y = e->y - d->top, dx, w;
	size_t n = strlen((const char *)ui->edits[k].text);
	if (x < c->left || x > c->right || y < c->top || y > c->bottom) return;
	dx = x - c->left;
	if (dx <= 0) {
		ui->edits[k].cursor = 0;
		return;
	}
	kp_ui_select_control_font(ui, c);
	w = kp_ui_text_width(&ui->text, ui->edits[k].text);
	ui->text.font = NULL;
	ui->edits[k].cursor = (uint8_t)(dx >= w ? n : (uint32_t)n * (uint32_t)dx / (uint32_t)w);
}

int kp_ui_edit_proc(KpUi *ui, KpUiControl *c, KpUiEvent *e)
{
	/* EditBoxProc 0x4c7880 */
	int32_t self = (int32_t)(c - ui->controls);
	KpUiInteract base = kp_ui_type_interact(-1); /* ControlDefaultInteract 0x4c1e90 */
	int k = slot_of(ui, self);
	size_t n;
	if (k < 0) return base(ui, c, e);
	n = strlen((const char *)ui->edits[k].text);
	switch (e->type) {
	case KP_UI_KEY_DOWN:
		if ((e->key & 0xff) == VK_INSERT && (e->mods & 0xf) == 0) {
			ui->edits[k].mode ^= MODE_INSERT;
			return 1;
		}
		/* fall through */
	case KP_UI_KEY_REPEAT:
		switch (e->key & 0xff) {
		case VK_END: ui->edits[k].cursor = (uint8_t)n; kp_ui_mark_dirty(ui, self); return 1;
		case VK_HOME: ui->edits[k].cursor = 0; kp_ui_mark_dirty(ui, self); return 1;
		case VK_LEFT:
			if (ui->edits[k].cursor > 0) {
				ui->edits[k].cursor--;
				kp_ui_mark_dirty(ui, self);
			}
			return 1;
		case VK_RIGHT:
			if (ui->edits[k].cursor < n) {
				ui->edits[k].cursor++;
				kp_ui_mark_dirty(ui, self);
			}
			return 1;
		case VK_DELETE:
			if (ui->edits[k].cursor < n) delete_char(ui, self, k, 1);
			return 1;
		default: return 0;
		}
	case KP_UI_CHAR: {
		uint8_t ch = (uint8_t)(e->key & 0xff);
		caret_on(ui, self);
		if (ch == VK_BACK && ui->edits[k].cursor != 0) {
			typing(ui, self, k, 1);
			delete_char(ui, self, k, 0);
			return 1;
		}
		if (!has_glyph(ui, c, ch)) return 0;
		typing(ui, self, k, 1);
		insert_char(ui, self, k, ch);
		return 1;
	}
	case KP_UI_LEFT_DOWN: case KP_UI_LEFT_DOUBLE:
		caret_on(ui, self);
		if (ui->edits[k].typing) {
			ui->cursor_hidden = 0;
			kp_ui_release_refs(ui, self);
			ui->edits[k].typing = 0;
		}
		place_cursor(ui, c, k, e);
		return 1;
	case KP_UI_MOUSE_MOVE:
		typing(ui, self, k, 0);
		return 0;
	case KP_UI_USER:
		switch (e->code) {
		case KP_UI_DESTROY: /* DestroyEditBox 0x4c7c00 */
			kp_ui_delete_timer(&ui->timers, self, CARET_TIMER);
			typing(ui, self, k, 0);
			ui->edits[k].control = -1;
			break;
		case KP_UI_FOCUS: caret_on(ui, self); break;
		case KP_UI_FOCUS_LOST:
			typing(ui, self, k, 0);
			c->flags &= ~CARET_SHOWN;
			kp_ui_delete_timer(&ui->timers, self, CARET_TIMER);
			kp_ui_mark_dirty(ui, self);
			break;
		case KP_UI_POST_LOAD: /* InitEditBox 0x4c7d60 */
			ui->edits[k].text[0] = 0;
			ui->edits[k].typing = 0;
			ui->edits[k].cursor = ui->edits[k].scroll = ui->edits[k].margin_x = ui->edits[k].margin_y = 0;
			ui->edits[k].mode = MODE_INSERT;
			break;
		default: break;
		}
		return base(ui, c, e);
	default:
		return base(ui, c, e);
	}
}

int kp_ui_edit_focused(const KpUi *ui)
{
	/* the dialogs with an edit control take no focus (FLAG_NO_FOCUS: their keys go to the edit), so a shown one is the one
	 * typed into */
	int k;
	for (k = 0; k < KP_UI_EDITS; k++) {
		int32_t c = ui->edits[k].control, d;
		if (c < 0) continue;
		d = ui->controls[c].parent;
		if (d >= 0 && (ui->controls[c].flags & KP_UI_VISIBLE) && (ui->controls[d].flags & KP_UI_VISIBLE)) return 1;
	}
	return 0;
}

void kp_ui_edit_set_margin(KpUi *ui, int32_t control, uint8_t x)
{
	int k = slot_of(ui, control);
	if (k >= 0) ui->edits[k].margin_x = x;
}

/* EditRecalcScroll 0x4c7eb0 in font mode 1 (single-byte). */
static void recalc_scroll(KpUi *ui, KpUiControl *c, int k)
{
	uint8_t caret[2] = { CARET_GLYPH, 0 };
	int32_t avail = (c->right - 2 * ui->edits[k].margin_x - c->left + 1) - kp_ui_text_width(&ui->text, caret);
	int32_t sum = 0, i;
	ui->edits[k].avail = (uint16_t)avail;
	if (ui->edits[k].cursor < ui->edits[k].scroll) {
		ui->edits[k].scroll = ui->edits[k].cursor;
		return;
	}
	for (i = ui->edits[k].scroll; i < ui->edits[k].cursor; i++) {
		uint8_t one[2] = { ui->edits[k].text[i], 0 };
		sum += kp_ui_text_width(&ui->text, one);
	}
	if (sum < avail) return;
	ui->edits[k].scroll = ui->edits[k].cursor;
	sum = avail;
	while (ui->edits[k].scroll > 0) {
		uint8_t one[2] = { ui->edits[k].text[ui->edits[k].scroll - 1], 0 };
		int32_t w = kp_ui_text_width(&ui->text, one);
		if (w >= sum) break;
		sum -= w;
		ui->edits[k].scroll--;
	}
}

void kp_ui_edit_draw(KpUi *ui, KpUiControl *c, int32_t x, int32_t y, const int16_t clip[4])
{
	/* DrawEditControl 0x4c3b40 with DrawEditText 0x4c7c40 */
	int32_t self = (int32_t)(c - ui->controls);
	int k = slot_of(ui, self), wrap = ui->text.wrap;
	uint8_t buffer[KP_UI_EDIT_SIZE + 4], highlight = ui->text.highlight;
	size_t n, at;
	(void)x;
	(void)y;
	(void)clip;
	if (k < 0) return;
	kp_ui_select_control_font(ui, c);
	kp_ui_update_focus(ui, c);
	if (!(c->flags & KP_UI_PLAIN) && (c->flags & KP_UI_FOCUS_LINE)) { /* OutlineIfFocused 0x4c4200 */
		int32_t w = c->right - c->left + 1, h = c->bottom - c->top + 1;
		uint8_t colour = ui->ui_colours[COLOUR_FOCUS];
		kp_ui_fill_rect(ui, c->left, c->top, w, 1, colour);
		kp_ui_fill_rect(ui, c->left, c->top + h - 1, w, 1, colour);
		kp_ui_fill_rect(ui, c->left, c->top, 1, h, colour);
		kp_ui_fill_rect(ui, c->left + w - 1, c->top, 1, h, colour);
	}
	if (ui->edits[k].mode & MODE_MODIFIED) recalc_scroll(ui, c, k);
	n = strlen((const char *)ui->edits[k].text + ui->edits[k].scroll);
	memcpy(buffer, ui->edits[k].text + ui->edits[k].scroll, n + 1);
	if (c->flags & CARET_SHOWN) { /* {4, 0xa6, 1}: the caret glyph drawn without advancing */
		at = (uint8_t)(ui->edits[k].cursor - ui->edits[k].scroll);
		if (at > n) at = n;
		if (n + 3 < sizeof buffer) {
			memmove(buffer + at + 3, buffer + at, n - at + 1);
			buffer[at] = 4;
			buffer[at + 1] = CARET_GLYPH;
			buffer[at + 2] = 1;
		}
	}
	kp_ui_select_color(&ui->text, (ui->edits[k].mode >> 4) ? (ui->edits[k].mode >> 4) : COLOUR_DEFAULT);
	ui->text.clip_left = c->left;
	ui->text.clip_top = c->top;
	ui->text.clip_right = c->right;
	ui->text.clip_bottom = c->bottom;
	ui->text.wrap = 0;
	ui->text.flags = FORMAT_EDIT;
	ui->text.highlight = CARET_GLYPH; /* InitEditBox's caret glyph 0x6d3114 */
	(void)kp_ui_draw_text(&ui->text, c->left + ui->edits[k].margin_x, c->top + ui->edits[k].margin_y, buffer);
	ui->text.highlight = highlight;
	ui->text.wrap = wrap ? wrap : 1;
	ui->text.font = NULL;
	if ((c->flags & KP_UI_CONSUMED) && (c->flags & KP_UI_FOCUS_LINE)) {
		int32_t w = c->right - c->left + 1, h = c->bottom - c->top + 1;
		uint8_t colour = ui->ui_colours[COLOUR_OUTLINE];
		kp_ui_fill_rect(ui, c->left, c->top, w, 1, colour);
		kp_ui_fill_rect(ui, c->left, c->top + h - 1, w, 1, colour);
		kp_ui_fill_rect(ui, c->left, c->top, 1, h, colour);
		kp_ui_fill_rect(ui, c->left + w - 1, c->top, 1, h, colour);
	}
}
