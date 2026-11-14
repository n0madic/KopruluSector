/*
 * The interface's host (kp_hud.h): the console of a game and the glue's screens over kp_ui.
 *
 * The console: kp_ui runs the local player's console over the game (the resources, the status panel, the portrait,
 * the minimap, the menu button), repaints the invalid region as the original's PresentFrame does, the canvas composes
 * the open dialogs by the layout of the logical screen. A point on a panel goes to it in the original's coordinates
 * (ADR 0011: the panels keep their content, the layout moves them); a point on the console art elsewhere is over the
 * console; the rest is the game view. A button's press picks the panel (or the game view) the gesture keeps until the
 * buttons are up.
 *
 * The glue: one interface runs the original's menus, the ready room of each game and the score screen after it; the
 * console of the game is the same interface's. The screen's dialog, its popups composited, is drawn at the logical
 * screen's centre with the glue's palette.
 */
#include "kp_hud.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kp_text.h"
#include "kp_touch.h"
#include "kp_utf8.h"

#define CONSOLE_A 255         /* the canvas's opacity of the console art and the panels */
#define RESULT_START 101      /* the ready room's Start once the turn steps confirmed it (kp_ui_briefing.c) */
#define DEFAULT_NAME "Player"

static const char *const RACE_GRPS[3] = { "dlgs\\zerg.grp", "dlgs\\terran.grp", "dlgs\\protoss.grp" };

KpUi *kp_interface_new_data(const KpUiData *d, const KpUiLocale *locale, char *err, size_t err_size)
{
	KpUi *ui = kp_ui_new_data(d, err, err_size);
	const KpDefsTable *chea = kp_defs_table(d->profile, "CHEA"), *tabl = kp_defs_table(d->profile, "TABL");
	if (!ui) return NULL;
	if (locale != NULL) kp_ui_set_locale(ui, locale);
	if (!chea || !tabl || kp_ui_set_cheats(ui, chea->data, chea->size) != 0) {
		snprintf(err, err_size, "ui: no campaign CHEA or TABL table");
		kp_ui_free(ui);
		return NULL;
	}
	ui->menus.campaign = tabl->data;
	ui->menus.campaign_size = tabl->size;
	return ui;
}

int kp_hud_colours(KpUi *ui, const KpHudMap *map)
{
	const KpTileset *t = map->tileset;
	kp_ui_resolve_colours(ui, t->rgb, t->cycled);
	if (kp_ui_game_effects(ui, t->rgb, t->cycled) != 0) return -1;
	if (kp_ui_set_minimap(ui, t->mmni, t->mmni_size, t->rgb, t->cycled, map->palettes->tilesets[map->tileset_index & 7].dark,
		map->colors) != 0) {
		snprintf(ui->error, sizeof ui->error, "ui: no tminimap.pcx or tblink.pcx");
		return -1;
	}
	return 0;
}

void kp_hud_view_size(int32_t w, int32_t h, int32_t *vw, int32_t *vh)
{
	*vw = w;
	*vh = h - KP_HUD_CONSOLE_ROWS;
}

void kp_hud_keep_focus(int32_t ow, int32_t oh, int32_t nw, int32_t nh, int64_t *x, int64_t *y)
{
	int32_t ovw, ovh, nvw, nvh;
	kp_hud_view_size(ow, oh, &ovw, &ovh);
	kp_hud_view_size(nw, nh, &nvw, &nvh);
	*x += kp_floor_div(ovw - nvw, 2);
	*y += kp_floor_div(ovh - nvh, 2);
}

int kp_hud_take_camera(KpUi *ui, int64_t *x, int64_t *y)
{
	if (!ui->input.camera_moved) return 0;
	ui->input.camera_moved = 0;
	*x = ui->input.screen_x;
	*y = ui->input.screen_y;
	return 1;
}

int kp_hud_init(KpHud *h, KpUi *ui, KpCanvas *shared, KpGame *g, const KpHudMap *map, uint32_t now_ms, int32_t w,
	int32_t hh, int from_save)
{
	int64_t race;
	memset(h, 0, sizeof *h);
	h->ui = ui;
	h->gesture = -2;
	if (shared) {
		h->canvas = shared;
	} else {
		h->canvas = (KpCanvas *)calloc(1, sizeof *h->canvas);
		if (!h->canvas) {
			snprintf(ui->error, sizeof ui->error, "hud: out of memory");
			return -1;
		}
		kp_canvas_init(h->canvas);
		h->own_canvas = 1;
	}
	if (from_save) ui->from_save = 1;
	race = g->players.race[g->local_player];
	if (race < 0 || race > 2) race = 1;
	if (kp_hud_colours(ui, map) != 0) return -1;
	/* the placement ghost's colours, by owner as the world's images */
	if (map->palettes->tunit_size >= sizeof ui->player_colours) {
		memcpy(ui->player_colours, map->palettes->tunit, sizeof ui->player_colours);
	}
	kp_ui_font_set(ui, 1);
	if (kp_ui_apply_theme(ui, RACE_GRPS[race]) != 0) {
		snprintf(ui->error, sizeof ui->error, "ui: no GRP %s", RACE_GRPS[race]);
		return -1;
	}
	ui->now = now_ms;
	ui->addon_anchor = 1; /* an add-on's preview beside its parent (phase 10; a touch has no pointer over the view) */
	if (kp_ui_hud_create(ui, g, NULL) != 0) return -1;
	if (w > 0) {
		int32_t vw, vh;
		int64_t x, y;
		kp_hud_view_size(w, hh, &vw, &vh);
		kp_ui_set_view(ui, vw, vh);
		kp_ui_start_screen(ui); /* HandleStartLocationRecord 0x46a280, InitScreenPosition 0x445810 */
		if (kp_hud_take_camera(ui, &x, &y)) {
			h->has_start = 1;
			h->start_x = (int32_t)x;
			h->start_y = (int32_t)y;
		}
	}
	return 0;
}

void kp_hud_place_camera(KpHud *h, int64_t *x, int64_t *y)
{
	KpUi *ui = h->ui;
	kp_ui_set_screen_position(ui, (int32_t)*x, (int32_t)*y);
	ui->input.camera_moved = 0;
	h->has_start = 1;
	h->start_x = ui->input.screen_x;
	h->start_y = ui->input.screen_y;
	*x = h->start_x;
	*y = h->start_y;
}

void kp_hud_free(KpHud *h)
{
	free(h->pad.pixels);
	if (h->pad.shift != KP_UI_PAD_SHIFT_OFF && h->ui) h->ui->input.keys[0] = 0;
	if (h->canvas) h->canvas->overlay = NULL; /* a shared canvas goes back to the glue */
	if (h->own_canvas && h->canvas) {
		kp_canvas_free(h->canvas);
		free(h->canvas);
	}
	memset(h, 0, sizeof *h);
}

void kp_hud_event(KpHud *h, const KpEvent *e) { kp_ui_game_event(h->ui, e->kind, e->a, e->b, e->c, e->text); }

static void composition_key(KpUi *ui, int32_t w, int32_t h, KpHudKey *key)
{
	int32_t d;
	memset(key, 0, sizeof *key);
	key->w = w;
	key->h = h;
	for (d = ui->open; d >= 0; d = ui->controls[d].next) {
		if (key->count == KP_HUD_KEY_DIALOGS) {
			key->count = -1;
			break;
		}
		key->dialogs[key->count++] = d;
	}
}

/* The column's buttons: Shift, the toggle, the groups with units in them (folded) or all ten (opened) under it. */
static int pad_buttons(int open, uint32_t held, int *out)
{
	int n = 0, b;
	out[n++] = KP_UI_PAD_SHIFT;
	out[n++] = KP_UI_PAD_TOGGLE;
	for (b = 0; b < 10; b++) {
		if (open || (held >> KP_UI_PAD_GROUP(b) & 1)) out[n++] = b;
	}
	return n;
}

/* The column along the game view's left edge (vh rows high) under the resource line with the groups in held, its
 * buttons shorter where the view's height is short of them, the last groups left out where even the shortest do not
 * fit (the column stays above the console). */
static void pad_layout(KpHud *h, int32_t vh, uint32_t held)
{
	KpHudPad *p = &h->pad;
	KpUi *ui = h->ui;
	int32_t bw = (int32_t)ceil(KP_HUD_PAD_DP * p->px_per_dp), margin, gap, bh, top, room, total, rx, ry;
	int buttons[KP_UI_PAD_BUTTONS], count = pad_buttons(p->open, held, buttons);
	size_t need;
	margin = bw / 8 > 2 ? bw / 8 : 2;
	gap = margin;
	bh = bw * 4 / 5;
	top = margin;
	if (ui->hud.resources >= 0 && kp_canvas_place(h->canvas, ui->hud.resources, &rx, &ry)) {
		const KpUiControl *d = &ui->controls[ui->hud.resources];
		top = ry + (d->bottom - d->top + 1) + margin;
	}
	room = vh - top - margin;
	if (count * (bh + gap) - gap > room) bh = (room - gap * (count - 1)) / count;
	if (bh < KP_HUD_PAD_MIN_PX) {
		bh = KP_HUD_PAD_MIN_PX;
		count = (room + gap) / (bh + gap);
		if (count < 2) count = 2; /* Shift and the toggle at least */
	}
	total = count * (bh + gap) - gap;
	if (p->laid && p->bw == bw && p->bh == bh && p->y == top && p->count == count
		&& memcmp(p->buttons, buttons, sizeof buttons[0] * (size_t)count) == 0) {
		return;
	}
	need = (size_t)bw * (size_t)total;
	if (need > p->size) {
		uint8_t *px = (uint8_t *)realloc(p->pixels, need);
		if (px == NULL) return;
		p->pixels = px;
		p->size = need;
	}
	memcpy(p->buttons, buttons, sizeof buttons);
	p->count = count;
	p->bw = bw;
	p->bh = bh;
	p->gap = gap;
	p->h = total;
	p->x = margin;
	p->y = top;
	p->surface.w = bw;
	p->surface.h = total;
	p->surface.pixels = p->pixels;
	p->laid = 1;
	p->drawn = 0;
}

/* The pad shows in the touch mode with no modeless dialog over the game (the game menu, the options, the saves, the
 * result: the pad over them would hide their buttons and take their taps). */
static int pad_shown(const KpHud *h) { return h->pad.on && h->ui->modeless < 0; }

/* The pad laid out and drawn again when its state, the groups or the blend colour changed (a group's tap folding it by
 * now; the game menu's, the options', the saves' and the result's dialogs set their border's colour, so a fill drawn
 * with the colour before is no longer the blend's and went opaque); the canvas takes it as its overlay. */
static void pad_frame(KpHud *h, int32_t vh, uint32_t now_ms)
{
	KpHudPad *p = &h->pad;
	uint32_t held;
	/* not under a finger pressing a button: the column laid out again would move the button away from it */
	if (p->fold_at != 0 && p->pressed < 0 && (int32_t)(now_ms - p->fold_at) >= 0) {
		p->fold_at = 0;
		if (p->open) {
			p->open = 0;
			p->laid = 0;
		}
	}
	if (!pad_shown(h)) {
		if (h->canvas->overlay != NULL) p->dirty = 1;
		h->canvas->overlay = NULL;
		return;
	}
	held = kp_ui_groups_held(h->ui);
	pad_layout(h, vh, held);
	if (p->pixels == NULL) return;
	if (!p->drawn || held != p->held || h->ui->effects.colour[0] != p->fill) {
		p->held = held;
		p->fill = h->ui->effects.colour[0];
		kp_ui_touch_pad(h->ui, &p->surface, p->bw, p->bh, p->gap, p->buttons, p->count, p->pressed, p->shift, p->open,
			held);
		p->drawn = 1;
		p->dirty = 1;
	}
	h->canvas->overlay = &p->surface;
	h->canvas->overlay_x = p->x;
	h->canvas->overlay_y = p->y;
}

/* The pad's Shift (KP_UI_PAD_SHIFT_*) and the interface's Shift byte with it. */
static void pad_shift(KpHud *h, int shift)
{
	h->pad.shift = shift;
	h->ui->input.keys[0] = shift != KP_UI_PAD_SHIFT_OFF;
	h->pad.drawn = 0;
}

void kp_hud_pad(KpHud *h, int on, double px_per_dp)
{
	KpHudPad *p = &h->pad;
	if (on && !p->on) {
		p->pressed = -1;
		p->open = 0;
		p->fold_at = 0;
		p->drawn = 0;
	}
	if (!on && p->shift != KP_UI_PAD_SHIFT_OFF) pad_shift(h, KP_UI_PAD_SHIFT_OFF);
	p->on = on;
	if (px_per_dp != p->px_per_dp) p->laid = 0;
	p->px_per_dp = px_per_dp > 0 ? px_per_dp : 1;
}

int kp_hud_pad_at(const KpHud *h, int32_t lx, int32_t ly)
{
	const KpHudPad *p = &h->pad;
	int32_t k;
	if (!pad_shown(h) || !p->laid || lx < p->x || lx >= p->x + p->bw || ly < p->y || ly >= p->y + p->h) return -1;
	k = (ly - p->y) / (p->bh + p->gap);
	return ly - p->y - k * (p->bh + p->gap) < p->bh && k < p->count ? p->buttons[k] : -1;
}

int kp_hud_pad_point(const KpHud *h, int button, int32_t *lx, int32_t *ly)
{
	const KpHudPad *p = &h->pad;
	int k;
	if (!pad_shown(h) || !p->laid) return 0;
	for (k = 0; k < p->count; k++) {
		if (p->buttons[k] != button) continue;
		*lx = p->x + p->bw / 2;
		*ly = p->y + k * (p->bh + p->gap) + p->bh / 2;
		return 1;
	}
	return 0;
}

/* The groups opened or folded: the column laid out again. */
static void pad_open(KpHud *h, int open)
{
	h->pad.fold_at = 0;
	if (h->pad.open == open) return;
	h->pad.open = open;
	h->pad.laid = 0;
}

static int ping_at(const KpHud *h, int32_t lx, int32_t ly, int32_t *map_x, int32_t *map_y);

/* The pad's Shift spent: a click with it on for the next click that queued a command (an order, a selection's change) turns it off, so
 * that it is the next click's Shift only (the Fold7: left on, it made the later taps and boxes add to the selection). */
static void pad_shift_spent(KpHud *h, int commands)
{
	if (h->pad.shift == KP_UI_PAD_SHIFT_ONCE && h->ui->input.command_count > commands) pad_shift(h, KP_UI_PAD_SHIFT_OFF);
}

void kp_hud_keys_cleared(KpHud *h)
{
	if (h->pad.shift != KP_UI_PAD_SHIFT_OFF) pad_shift(h, KP_UI_PAD_SHIFT_OFF);
}

static void pad_lit(KpHud *h, int pressed)
{
	if (h->pad.pressed == pressed) return;
	h->pad.pressed = pressed;
	h->pad.drawn = 0;
}

void kp_hud_pad_button(KpHud *h, int moment, int32_t lx, int32_t ly)
{
	KpHudPad *p = &h->pad;
	KpUi *ui = h->ui;
	int b = kp_hud_pad_at(h, lx, ly), pressed = p->pressed, commands;
	uint16_t digit;
	switch (moment) {
	case KP_TOUCH_BUTTON_PRESS:
		pad_lit(h, b);
		return;
	case KP_TOUCH_BUTTON_END:
		pad_lit(h, -1);
		return;
	case KP_TOUCH_BUTTON_TAP:
		pad_lit(h, -1);
		if (b < 0 && pressed < 0) { /* a ping's: the view centred on its event (the Space's for the fingers) */
			int32_t mx, my;
			if (ping_at(h, lx, ly, &mx, &my)) kp_ui_set_screen_position(ui, mx - ui->view_w / 2, my - ui->view_h / 2);
			return;
		}
		if (b < 0 || b != pressed) return; /* let go off the button pressed */
		if (b == KP_UI_PAD_SHIFT) {
			pad_shift(h, p->shift != KP_UI_PAD_SHIFT_OFF ? KP_UI_PAD_SHIFT_OFF : KP_UI_PAD_SHIFT_ONCE);
			return;
		}
		if (b == KP_UI_PAD_TOGGLE) {
			pad_open(h, !p->open);
			return;
		}
		if (kp_ui_edit_focused(ui)) return; /* a message's text box would take the digit as a character */
		commands = ui->input.command_count;
		kp_ui_window_char(ui, (uint16_t)('0' + KP_UI_PAD_GROUP(b)), 0); /* the digit: its group recalled */
		pad_shift_spent(h, commands);
		if (p->open) p->fold_at = (ui->now + KP_HUD_PAD_FOLD_MS) | 1; /* for a second tap on the same button */
		return;
	default: /* the long press: Shift locked, a group's Ctrl and the digit, the group assigned (with no Shift: Ctrl, Shift
		 * and a digit is none) */
		if (pressed == KP_UI_PAD_SHIFT) {
			pad_shift(h, KP_UI_PAD_SHIFT_LOCKED);
			return;
		}
		if (pressed < 0 || pressed >= KP_UI_PAD_SHIFT || kp_ui_edit_focused(ui)) return;
		pad_open(h, 0);
		digit = (uint16_t)('0' + KP_UI_PAD_GROUP(pressed));
		ui->input.keys[0] = 0;
		kp_hud_key(ui, 1, 0x11, 0);
		kp_hud_key(ui, 1, digit, 0);
		kp_hud_key(ui, 0, digit, 0);
		kp_hud_key(ui, 0, 0x11, 0);
		ui->input.keys[0] = p->shift != KP_UI_PAD_SHIFT_OFF;
		return;
	}
}

/* The message pump's idle event (0x4401c0), each frame as the pump's loop has one when its queue is empty: a hotkey's
 * press completes, a slider's capture ends and the options' previews run (the volumes', the gamma's); in a game the
 * input procedure's (GameIdleThunk 0x448f10: the sounds' cooldowns), at the glue the credits' pages turn. */
static void idle(KpUi *ui)
{
	KpUiEvent e;
	memset(&e, 0, sizeof e);
	e.type = KP_UI_IDLE;
	e.x = ui->mouse_x;
	e.y = ui->mouse_y;
	kp_ui_input_event(ui, &e);
}

int kp_hud_frame(KpHud *h, int32_t w, int32_t hh, uint32_t now_ms, int steps, int cursor, int32_t cx, int32_t cy,
	int64_t *cam_x, int64_t *cam_y, int *moved, int *released)
{
	KpUi *ui = h->ui;
	KpHudKey key;
	int32_t vw, vh;
	int draws, lines, game_view, k, changed = 0;
	int64_t x, y;
	ui->now = now_ms;
	kp_ui_tick(ui, now_ms);
	idle(ui);
	kp_hud_view_size(w, hh, &vw, &vh);
	kp_ui_set_view(ui, vw, vh);
	/* UpdateGameUi 0x445020 runs on each pass of GameLoop 0x4451f0, a step or none: a selection's snapshot is the next
	 * pass's (a right click in the selection's tick, the group's key and the click at once, gives the order) */
	for (k = 0; k < (steps > 0 ? steps : 1); k++) kp_ui_hud_frame(ui);
	/* the game's first view replaces the host's camera once (the host's is still the previous game's or the view before
	 * a load) */
	if (h->has_start) {
		*cam_x = h->start_x;
		*cam_y = h->start_y;
	}
	kp_ui_set_screen(ui, (int32_t)*cam_x, (int32_t)*cam_y);
	if (cursor == KP_HUD_POINTER_AIM) kp_ui_pointer_update(ui); /* a finger aiming: its cursor, no edge scroll */
	if (cursor == KP_HUD_POINTER_MOUSE) {
		int32_t ox = kp_floor_div(cx * KP_LAYOUT_MIN_W, w), oy = kp_floor_div(cy * KP_LAYOUT_MIN_H, hh);
		kp_ui_cursor_update(ui, ox, oy);
		if (now_ms - h->scrolled_ms > KP_HUD_SCROLL_MS) {
			h->scrolled_ms = now_ms;
			kp_ui_update_scroll(ui, ox, oy);
		}
	} else if (now_ms - h->scrolled_ms > KP_HUD_SCROLL_MS) {
		/* no pointer (a finger's, none yet): the scroll keys and a trigger's camera pan still step (the pan holds the view
		 * and its trigger's wait until it arrives), with no edge scroll: the original's screen centre */
		h->scrolled_ms = now_ms;
		kp_ui_update_scroll(ui, KP_LAYOUT_MIN_W / 2, KP_LAYOUT_MIN_H / 2);
	}
	/* UpdateGameUi 0x445020's preview at the mouse, then the draw's end of a box with no button down */
	kp_ui_game_view_update(ui);
	/* the canvas is composed again only when something on it changed: a repaint, the text lines, the open dialogs, the
	 * screen's size (the tooltip's moves repaint the dialogs under it), the game view's drag box and marker while they
	 * show and once more when they went */
	draws = kp_ui_present(ui);
	kp_ui_lines_expire(ui);
	lines = ui->lines.changed;
	composition_key(ui, w, hh, &key);
	game_view = kp_ui_game_view_active(ui);
	pad_frame(h, vh, now_ms);
	if (draws > 0 || lines || !h->has_composed || key.count < 0 || memcmp(&key, &h->composed, sizeof key) != 0 || game_view
		|| h->game_view || h->pad.dirty) {
		KpPanel panels[KP_CANVAS_PANELS];
		int count = kp_canvas_hud_panels(ui, panels, KP_CANVAS_PANELS);
		h->game_view = game_view;
		h->has_composed = 1;
		h->pad.dirty = 0;
		h->composed = key;
		changed = kp_canvas_compose(h->canvas, ui, w, hh, panels, count, -1) > 0; /* the layout by the screen's size */
	}
	*moved = 0;
	if (kp_hud_take_camera(ui, &x, &y)) {
		*cam_x = x;
		*cam_y = y;
		*moved = 1;
	} else if (h->has_start) {
		*moved = 1;
	}
	h->has_start = 0;
	*released = kp_ui_take_release(ui);
	return changed;
}

/* The keys whose WM_KEYDOWN TranslateMessage follows with a WM_CHAR of their own code. */
static int char_key(uint16_t vk) { return vk == 0x08 || vk == 0x09 || vk == 0x0d || vk == 0x1b; }

void kp_hud_key(KpUi *ui, int down, uint16_t vk, int repeating)
{
	/* no character after an accelerator's key or a key a dialog closed on (the pump skips TranslateMessage) */
	if (kp_ui_window_key(ui, down, vk, repeating) && down && char_key(vk)) kp_ui_window_char(ui, vk, repeating);
}

#define KP_HUD_TEXT_END 0x10000 /* a character event holds the Basic Multilingual Plane */

void kp_hud_text_key(KpUi *ui, const char *utf8, uint16_t vk)
{
	size_t n = strlen(utf8), at = 0;
	while (at < n) {
		int32_t cp = kp_utf8_next((const uint8_t *)utf8, n, &at);
		if (cp < 32 || cp >= KP_HUD_TEXT_END) continue;
		if (cp >= KP_TEXT_ASCII_END && !kp_ui_edit_focused(ui)) {
			if (vk < 'A' || vk > 'Z') continue;
			cp = vk - 'A' + 'a';
			vk = 0; /* one key, one hotkey */
		}
		kp_ui_window_char(ui, (uint16_t)cp, 0);
	}
}

void kp_hud_text(KpUi *ui, const char *utf8) { kp_hud_text_key(ui, utf8, 0); }

uint16_t kp_hud_buttons(uint16_t buttons, int event)
{
	if (event == KP_UI_LEFT_DOWN || event == KP_UI_LEFT_DOUBLE) buttons |= 2; /* KP_UI_MOUSE_BUTTONS' left and right */
	if (event == KP_UI_RIGHT_DOWN || event == KP_UI_RIGHT_DOUBLE) buttons |= 8;
	if (event == KP_UI_LEFT_UP) buttons &= (uint16_t)~2u;
	if (event == KP_UI_RIGHT_UP) buttons &= (uint16_t)~8u;
	return buttons;
}

static int is_open(const KpUi *ui, int32_t dialog)
{
	int32_t d;
	for (d = ui->open; d >= 0; d = ui->controls[d].next) {
		if (d == dialog) return 1;
	}
	return 0;
}

/* The topmost open dialog the arrangement placed under the logical point, or -1. */
static int32_t panel_at(const KpUi *ui, const KpCanvas *c, int32_t lx, int32_t ly)
{
	int32_t d;
	for (d = ui->open; d >= 0; d = ui->controls[d].next) {
		const KpUiControl *k = &ui->controls[d];
		int32_t px, py;
		if (kp_canvas_place(c, d, &px, &py) && lx >= px && lx <= px + k->right - k->left && ly >= py
			&& ly <= py + k->bottom - k->top) {
			return d;
		}
	}
	return -1;
}

/* The event's point on the map of the minimap's ping near the logical point (KP_HUD_PING_DP), if any. */
static int ping_at(const KpHud *h, int32_t lx, int32_t ly, int32_t *map_x, int32_t *map_y)
{
	KpUi *ui = h->ui;
	int32_t d = ui->minimap.dialog, px, py;
	double per_dp = h->pad.px_per_dp > 0 ? h->pad.px_per_dp : 1;
	if (!h->has_composed || d < 0 || panel_at(ui, h->canvas, lx, ly) != d || !kp_canvas_place(h->canvas, d, &px, &py)) {
		return 0;
	}
	return kp_ui_minimap_ping_near(ui, ui->controls[d].left + lx - px, ui->controls[d].top + ly - py,
		(int32_t)ceil(KP_HUD_PING_DP * per_dp), map_x, map_y);
}

int kp_hud_touch_target(KpHud *h, int32_t lx, int32_t ly, int64_t cam_x, int64_t cam_y)
{
	KpUi *ui = h->ui;
	int32_t mx, my;
	if (kp_hud_pad_at(h, lx, ly) >= 0) return KP_TOUCH_BUTTON;
	if (ping_at(h, lx, ly, &mx, &my)) return KP_TOUCH_PANEL_BUTTON; /* a slide is the minimap's drag */
	if (h->has_composed) {
		uint8_t index, alpha;
		if (panel_at(ui, h->canvas, lx, ly) >= 0) return KP_TOUCH_PANEL;
		if (lx >= 0 && ly >= 0 && lx < h->canvas->w && ly < h->canvas->h) {
			kp_canvas_pixel(h->canvas, lx, ly, &index, &alpha);
			if (alpha == CONSOLE_A) return KP_TOUCH_PANEL;
		}
	}
	if (lx >= ui->view_w || ly >= ui->view_h) return KP_TOUCH_PANEL; /* below the game view */
	kp_ui_set_screen(ui, (int32_t)cam_x, (int32_t)cam_y);
	switch (kp_ui_touch_target(ui, lx, ly)) {
	case KP_UI_TOUCH_ORDER: return KP_TOUCH_ORDER;
	case KP_UI_TOUCH_AIM: return KP_TOUCH_AIM;
	case KP_UI_TOUCH_PLACE: return KP_TOUCH_PLACE;
	case KP_UI_TOUCH_GHOST: return KP_TOUCH_GHOST;
	default: return KP_TOUCH_SELECT;
	}
}

int kp_hud_placing(const KpUi *ui) { return ui->game != NULL && kp_scalar(ui->game, KP_PLACEMENT_ACTIVE) == 1; }

uint16_t kp_hud_back_key(const KpUi *ui)
{
	const KpGame *g = ui->game;
	if (g == NULL || ui->modeless >= 0 || kp_scalar(g, KP_PAUSED) != 0 || ui->input.targeting || kp_hud_placing(ui)
		|| kp_ui_edit_focused(ui)) {
		return KP_HUD_VK_ESCAPE;
	}
	return KP_HUD_VK_F10;
}

void kp_hud_input(KpHud *h, int event, int32_t lx, int32_t ly, int64_t cam_x, int64_t cam_y)
{
	KpUi *ui = h->ui;
	KpUiEvent e;
	int32_t x = lx, y = ly;
	int over = 0, commands = ui->input.command_count;
	if (h->has_composed) {
		int32_t d = h->gesture, px, py;
		if (d == -2 || (d >= 0 && !(kp_canvas_place(h->canvas, d, &px, &py) && is_open(ui, d)))) {
			d = panel_at(ui, h->canvas, lx, ly);
		}
		if (h->buttons == 0 && (event == KP_UI_LEFT_DOWN || event == KP_UI_LEFT_DOUBLE || event == KP_UI_RIGHT_DOWN
			|| event == KP_UI_RIGHT_DOUBLE)) {
			h->gesture = d;
		}
		if (d >= 0 && kp_canvas_place(h->canvas, d, &px, &py)) {
			const KpUiControl *k = &ui->controls[d];
			x = k->left + lx - px;
			y = k->top + ly - py;
			over = -1; /* the dialogs answer for themselves */
		} else if (lx >= 0 && ly >= 0 && lx < h->canvas->w && ly < h->canvas->h) {
			uint8_t index, alpha;
			kp_canvas_pixel(h->canvas, lx, ly, &index, &alpha);
			over = alpha == CONSOLE_A;
		}
	}
	h->buttons = kp_hud_buttons(h->buttons, event);
	if (h->buttons == 0) h->gesture = -2;
	kp_ui_set_over_console(ui, over);
	kp_ui_set_keys(ui, ui->input.keys[0], ui->input.keys[1], ui->input.keys[2], h->buttons);
	kp_ui_set_screen(ui, (int32_t)cam_x, (int32_t)cam_y);
	memset(&e, 0, sizeof e);
	e.type = (uint16_t)event;
	e.x = (int16_t)x;
	e.y = (int16_t)y;
	kp_ui_input_event(ui, &e);
	pad_shift_spent(h, commands);
}

void kp_hud_take_commands(KpUi *ui, KpSession *s)
{
	uint8_t buf[KP_UI_COMMAND_BYTES];
	size_t n;
	while ((n = kp_ui_take_command(ui, buf, sizeof buf)) != 0) {
		if (s) kp_session_command(s, buf, n, -1);
	}
}

void kp_hud_capture_lost(KpHud *h)
{
	h->buttons = 0;
	h->gesture = -2;
	kp_ui_capture_changed(h->ui);
}

/* --- the glue --- */

/* The profile's IDs as the interface's character list. */
static void characters(KpUi *ui, const KpProfile *p)
{
	ui->menus.profile_count = kp_profile_login_list(p, ui->menus.profiles, KP_UI_PROFILES);
	ui->menus.profiles_changed = 0;
}

void kp_glue_init(KpGlue *g, KpUi *ui, KpProfile *profile, uint32_t now_ms)
{
	memset(g, 0, sizeof *g);
	g->ui = ui;
	g->profile = profile;
	kp_canvas_init(&g->canvas);
	characters(ui, profile);
	ui->now = now_ms;
}

void kp_glue_free(KpGlue *g)
{
	kp_canvas_free(&g->canvas);
	memset(g, 0, sizeof *g);
}

void kp_glue_menus(KpGlue *g, int state)
{
	kp_ui_title_close(g->ui); /* RunGlueLoop 0x457510's DestroyTitleDialog 0x4b55b0 at each entry (none: nothing) */
	characters(g->ui, g->profile);
	kp_ui_menus_enter(g->ui, state);
	g->screen = KP_GLUE_MENUS;
	g->has_composed = 0; /* the shared canvas may hold a game's frame */
}

void kp_glue_title(KpGlue *g, uint32_t now_ms)
{
	g->ui->now = now_ms;
	if (kp_ui_title_open(g->ui) < 0) {
		kp_glue_menus(g, KP_GLUE_MAIN);
		return;
	}
	g->screen = KP_GLUE_TITLE;
	g->title_timed = 0; /* its Sleep counts from its first frame (kp_glue_frame) */
	g->has_composed = 0;
}

void kp_glue_epilog(KpGlue *g, int expansion)
{
	if (kp_ui_credits_epilog_open(g->ui, expansion) < 0) {
		kp_glue_menus(g, KP_GLUE_MAIN);
		return;
	}
	g->screen = KP_GLUE_EPILOG;
	g->has_composed = 0; /* the shared canvas may hold a game's frame */
}

/* A ready room's glue state -> its race (Terran 7, Zerg 8, Protoss 9). */
static int room_race(int state) { return state == 7 ? 1 : state == 8 ? 0 : 2; }

int kp_glue_room(KpGlue *g, KpGame *game, int state)
{
	g->ui->game = game;
	if (kp_ui_ready_room_open(g->ui, room_race(state)) < 0) return -1;
	kp_ui_ready_room_continue(g->ui);
	g->screen = KP_GLUE_ROOM;
	g->room_opened = 0;
	g->has_composed = 0;
	return 0;
}

void kp_glue_score(KpGlue *g, KpGame *game, int result, uint32_t elapsed)
{
	KpUiScoreData d;
	kp_ui_score_data(g->ui, game, result, g->profile->name[0] ? g->profile->name : DEFAULT_NAME, elapsed, &d);
	kp_ui_score_open(g->ui, &d);
	g->screen = KP_GLUE_SCORE;
	g->has_composed = 0;
}

/* The interface's character list back into the host's profile: the IDs the login created or deleted, the progress the
 * menus raised (an episode's start); the login's name. */
static void take_profile(KpGlue *g, const char *player)
{
	KpUi *ui = g->ui;
	char owner[KP_PROFILE_NAME];
	snprintf(owner, sizeof owner, "%s", kp_profile_save_owner(g->profile));
	ui->menus.profiles_changed = 0; /* the host reads the list here */
	kp_profile_from_login_list(g->profile, ui->menus.profiles, ui->menus.profile_count, player);
	/* another ID logged in (or the logged-in one deleted): its saves, the original's save\<name>\ (the list stays
	 * otherwise: the load screen's choice is a row of it) */
	if (strcmp(owner, kp_profile_save_owner(g->profile)) != 0) kp_glue_saves(ui, g->profile);
}

static KpGlueEvent poll(KpGlue *g)
{
	KpUi *ui = g->ui;
	KpGlueEvent ev;
	memset(&ev, 0, sizeof ev);
	if (g->screen == KP_GLUE_MENUS) {
		KpUiMenus *m = &ui->menus;
		int action;
		if (!kp_ui_menus_poll(ui)) {
			/* an ID created or deleted on the login screen is written at once, as the original's file is
			 * (CreateNewProfile 0x45a320, DeleteSelectedProfile 0x45a4b0), not when the screen ends */
			if (m->profiles_changed) {
				take_profile(g, NULL);
				ev.profile_changed = 1;
			}
			return ev;
		}
		action = m->action;
		m->action = KP_UI_MENU_NONE;
		take_profile(g, m->player);
		ev.profile_changed = 1;
		if ((action == KP_UI_MENU_MISSION || action == KP_UI_MENU_CINEMATIC) && m->state >= 7 && m->state <= 9) {
			/* a mission history's cinematic row plays its cinematic, then goes on to the mission (StartCampaignEpisode
			 * 0x4bb5b0: the main loop plays it and AdvanceCampaignAfterGame goes on to the playable row) */
			g->screen = KP_GLUE_NONE;
			ev.kind = KP_GLUE_MISSION;
			ev.mission = m->mission;
			ev.state = m->state;
			ev.expansion = m->expansion;
			ev.cinematic = action == KP_UI_MENU_CINEMATIC ? m->cinematic : 0;
		} else if (action == KP_UI_MENU_INTRO && g->cinematics) { /* View Intro: the host plays it, then the main menu */
			g->screen = KP_GLUE_NONE;
			ev.kind = KP_GLUE_INTRO;
			ev.profile_changed = 0;
		} else if (action == KP_UI_MENU_CREDITS && kp_ui_credits_game_open(ui) >= 0) { /* Show Credits */
			g->screen = KP_GLUE_CREDITS;
			g->has_composed = 0;
			ev.profile_changed = 0;
		} else if (action == KP_UI_MENU_EXIT) {
			g->screen = KP_GLUE_NONE;
			ev.kind = KP_GLUE_EXIT;
			ev.profile_changed = 0;
		} else if (action == KP_UI_MENU_LANGUAGE) { /* the languages' popup chose others: the host starts over in them */
			g->screen = KP_GLUE_NONE;
			ev.kind = KP_GLUE_LANGUAGE;
		} else if (action == KP_UI_MENU_LOAD_SAVE) { /* the load screen's saved game (RunLoadGameScreen 0x459970) */
			g->screen = KP_GLUE_NONE;
			ev.kind = KP_GLUE_LOAD;
			ev.save = m->save_choice;
		} else if (action == KP_UI_MENU_REPLAY) { /* Load Replay's Ok: the replay's game at once (0x6662b8 = 1) */
			g->screen = KP_GLUE_NONE;
			ev.kind = KP_GLUE_REPLAY;
			ev.save = m->replay_choice;
		} else {
			/* the next state's screen (View Intro does nothing without a host that plays cinematics) */
			kp_ui_menus_enter(ui, m->state);
			ev.kind = KP_GLUE_PROFILE;
		}
	} else if (g->screen == KP_GLUE_TITLE) {
		if ((int32_t)(ui->now - g->title_until) >= 0) kp_glue_menus(g, KP_GLUE_MAIN); /* the Sleep over: state 0 */
	} else if (g->screen == KP_GLUE_CREDITS) {
		if (kp_ui_credits_game_close(ui)) kp_glue_menus(g, ui->menus.state); /* the main menu again */
	} else if (g->screen == KP_GLUE_EPILOG) {
		if (kp_ui_credits_epilog_close(ui)) kp_glue_menus(g, KP_GLUE_MAIN);
	} else if (g->screen == KP_GLUE_ROOM) {
		kp_ui_ready_room_continue(ui);
		kp_ui_ready_room_idle(ui);
		if (is_open(ui, ui->briefing.dialog)) {
			g->room_opened = 1;
		} else if (g->room_opened) {
			g->screen = KP_GLUE_NONE;
			kp_ui_ready_room_end(ui);
			ev.kind = ui->result == RESULT_START ? KP_GLUE_ROOM_START : KP_GLUE_ROOM_CANCEL;
		}
	} else if (g->screen == KP_GLUE_SCORE) {
		if (ui->score.answered && !is_open(ui, ui->score.dialog)) {
			g->screen = KP_GLUE_NONE;
			ev.kind = KP_GLUE_SCORE_DONE;
		}
	}
	return ev;
}

static int32_t half_floor(int32_t v) { return v >= 0 ? v / 2 : -((1 - v) / 2); }

KpGlueEvent kp_glue_frame(KpGlue *g, int32_t w, int32_t h, uint32_t now_ms, int *changed, uint8_t rgb[768],
	int *palette)
{
	KpUi *ui = g->ui;
	KpGlueEvent ev;
	int32_t bottom = -1, d;
	int draws, k;
	ui->now = now_ms;
	if (g->screen == KP_GLUE_TITLE) {
		if (!g->title_timed) {
			g->title_timed = 1;
			g->title_until = now_ms + KP_UI_GLUE_SLEEP_MS;
		}
		kp_ui_music_tick(ui); /* RunGlueLoop's Sleep: no timer, no idle work, but the music's fade in (kp_glue_title) */
	} else {
		kp_ui_tick(ui, now_ms);
		idle(ui);
	}
	ev = poll(g);
	draws = kp_ui_present(ui);
	for (d = ui->open; d >= 0; d = ui->controls[d].next) bottom = d; /* the screen: the open list's last */
	g->place_x = half_floor(w - KP_LAYOUT_MIN_W);
	g->place_y = half_floor(h - KP_LAYOUT_MIN_H);
	/* composed again only when the screen changed: a repaint, another dialog, the size, the palette (the margins) */
	*changed = 0;
	if (draws > 0 || !g->has_composed || w != g->composed_w || h != g->composed_h || bottom != g->composed_bottom
		|| ui->glue.palette_serial != g->composed_serial) {
		g->has_composed = 1;
		g->composed_w = w;
		g->composed_h = h;
		g->composed_bottom = bottom;
		g->composed_serial = ui->glue.palette_serial;
		*changed = kp_canvas_compose_screen(&g->canvas, ui, w, h, bottom, g->place_x, g->place_y) > 0;
	}
	*palette = 0;
	if (!g->has_palette || ui->glue.palette_serial != g->palette_serial) {
		g->has_palette = 1;
		g->palette_serial = ui->glue.palette_serial;
		for (k = 0; k < 256; k++) {
			rgb[3 * k] = ui->glue.palette[4 * k];
			rgb[3 * k + 1] = ui->glue.palette[4 * k + 1];
			rgb[3 * k + 2] = ui->glue.palette[4 * k + 2];
		}
		*palette = 1;
	}
	return ev;
}


void kp_glue_input(KpGlue *g, int event, int32_t lx, int32_t ly)
{
	KpUi *ui = g->ui;
	KpUiEvent e;
	g->buttons = kp_hud_buttons(g->buttons, event);
	kp_ui_set_keys(ui, ui->input.keys[0], ui->input.keys[1], ui->input.keys[2], g->buttons);
	memset(&e, 0, sizeof e);
	e.type = (uint16_t)event;
	e.x = (int16_t)(lx - g->place_x);
	e.y = (int16_t)(ly - g->place_y);
	if (g->screen == KP_GLUE_TITLE) {
		/* the title takes no input (RunGlueLoop's Sleep); the main menu's idle events carry the pointer where it is */
		ui->mouse_x = e.x;
		ui->mouse_y = e.y;
		return;
	}
	kp_ui_input_event(ui, &e);
}

void kp_glue_key(KpGlue *g, int down, uint16_t vk, int repeating)
{
	if (g->screen != KP_GLUE_TITLE) kp_hud_key(g->ui, down, vk, repeating);
}

void kp_glue_text(KpGlue *g, const char *utf8, uint16_t vk)
{
	if (g->screen != KP_GLUE_TITLE) kp_hud_text_key(g->ui, utf8, vk);
}

void kp_glue_saves(KpUi *ui, const KpProfile *profile)
{
	char names[KP_UI_SAVES][KP_PROFILE_SAVE_NAME];
	int k, n = kp_profile_save_list(profile, names, KP_UI_SAVES);
	for (k = 0; k < n; k++) snprintf(ui->menus.saves[k], sizeof ui->menus.saves[k], "%s", names[k]);
	ui->menus.save_count = n;
}

void kp_glue_capture_lost(KpGlue *g)
{
	g->buttons = 0;
	kp_ui_capture_changed(g->ui);
}
