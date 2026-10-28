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

#include "kp_touch.h"

#define CONSOLE_A 255         /* the canvas's opacity of the console art and the panels */
#define RESULT_START 101      /* the ready room's Start once the turn steps confirmed it (kp_ui_briefing.c) */
#define DEFAULT_NAME "Player"

const char *const KP_UI_FILE_NAMES[KP_UI_FILES] = { "ui.kpd", "fonts.kpd", "strings.kpd", "portraits.kpd", "sounds.kpd",
	"campaign.kpd" };

static const char *const RACE_GRPS[3] = { "dlgs\\zerg.grp", "dlgs\\terran.grp", "dlgs\\protoss.grp" };

/* campaign.kpd's CHEA and TABL payloads (in its bytes); -1 when one is missing. */
static int campaign_sections(const uint8_t *data, size_t size, const uint8_t **chea, size_t *cn, const uint8_t **tabl,
	size_t *tn)
{
	*chea = kp_kpf_section(data, size, "CHEA", cn);
	*tabl = kp_kpf_section(data, size, "TABL", tn);
	return *chea && *tabl ? 0 : -1;
}

KpUi *kp_interface_new(uint8_t *const files[KP_UI_FILES], const size_t sizes[KP_UI_FILES], char *err, size_t err_size)
{
	KpUi *ui = kp_ui_new(files[KP_UI_FILE_UI], sizes[KP_UI_FILE_UI], files[KP_UI_FILE_FONTS], sizes[KP_UI_FILE_FONTS], err,
		err_size);
	const uint8_t *chea = NULL, *tabl = NULL;
	size_t cn = 0, tn = 0;
	if (!ui) return NULL;
	if (kp_ui_set_strings(ui, files[KP_UI_FILE_STRINGS], sizes[KP_UI_FILE_STRINGS]) != 0
		|| kp_ui_set_portraits(ui, files[KP_UI_FILE_PORTRAITS], sizes[KP_UI_FILE_PORTRAITS]) != 0
		|| kp_ui_set_sounds(ui, files[KP_UI_FILE_SOUNDS], sizes[KP_UI_FILE_SOUNDS]) != 0) {
		snprintf(err, err_size, "ui: %s", ui->error);
		kp_ui_free(ui);
		return NULL;
	}
	if (campaign_sections(files[KP_UI_FILE_CAMPAIGN], sizes[KP_UI_FILE_CAMPAIGN], &chea, &cn, &tabl, &tn) != 0
		|| kp_ui_set_cheats(ui, chea, cn) != 0) {
		snprintf(err, err_size, "ui: campaign.kpd has no CHEA or TABL section");
		kp_ui_free(ui);
		return NULL;
	}
	ui->menus.campaign = tabl;
	ui->menus.campaign_size = tn;
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
	if (h->pad.shift && h->ui) h->ui->input.keys[0] = 0;
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

/* The column's buttons: the toggle, Shift, the groups with units in them (folded) or all ten (opened). */
static int pad_buttons(int open, uint32_t held, int *out)
{
	int n = 0, b;
	out[n++] = KP_UI_PAD_TOGGLE;
	out[n++] = KP_UI_PAD_SHIFT;
	for (b = 0; b < 10; b++) {
		if (open || (held >> KP_UI_PAD_GROUP(b) & 1)) out[n++] = b;
	}
	return n;
}

/* The column along the game view's left edge (vh rows high) under the resource line, its buttons shorter where the
 * view's height is short of them. */
static void pad_layout(KpHud *h, int32_t vh)
{
	KpHudPad *p = &h->pad;
	KpUi *ui = h->ui;
	int32_t bw = (int32_t)ceil(KP_HUD_PAD_DP * p->px_per_dp), margin, gap, bh, top, room, total, rx, ry;
	uint32_t held = kp_ui_groups_held(ui);
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
	if (bh < 12) bh = 12;
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

/* The pad laid out and drawn again when its state or the groups changed; the canvas takes it as its overlay. */
static void pad_frame(KpHud *h, int32_t vh)
{
	KpHudPad *p = &h->pad;
	if (!p->on) {
		if (h->canvas->overlay != NULL) p->dirty = 1;
		h->canvas->overlay = NULL;
		return;
	}
	pad_layout(h, vh);
	if (p->pixels == NULL) return;
	if (!p->drawn || kp_ui_groups_held(h->ui) != p->held) {
		p->held = kp_ui_groups_held(h->ui);
		kp_ui_touch_pad(h->ui, &p->surface, p->bw, p->bh, p->gap, p->buttons, p->count, p->pressed, p->shift, p->open);
		p->drawn = 1;
		p->dirty = 1;
	}
	h->canvas->overlay = &p->surface;
	h->canvas->overlay_x = p->x;
	h->canvas->overlay_y = p->y;
}

void kp_hud_pad(KpHud *h, int on, double px_per_dp)
{
	KpHudPad *p = &h->pad;
	if (on && !p->on) {
		p->pressed = -1;
		p->open = 0;
		p->drawn = 0;
	}
	if (!on && p->shift) h->ui->input.keys[0] = 0;
	if (!on) p->shift = 0;
	p->on = on;
	if (px_per_dp != p->px_per_dp) p->laid = 0;
	p->px_per_dp = px_per_dp > 0 ? px_per_dp : 1;
}

int kp_hud_pad_at(const KpHud *h, int32_t lx, int32_t ly)
{
	const KpHudPad *p = &h->pad;
	int32_t k;
	if (!p->on || !p->laid || lx < p->x || lx >= p->x + p->bw || ly < p->y || ly >= p->y + p->h) return -1;
	k = (ly - p->y) / (p->bh + p->gap);
	return ly - p->y - k * (p->bh + p->gap) < p->bh && k < p->count ? p->buttons[k] : -1;
}

int kp_hud_pad_point(const KpHud *h, int button, int32_t *lx, int32_t *ly)
{
	const KpHudPad *p = &h->pad;
	int k;
	if (!p->on || !p->laid) return 0;
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
	if (h->pad.open == open) return;
	h->pad.open = open;
	h->pad.laid = 0;
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
	int b = kp_hud_pad_at(h, lx, ly), pressed = p->pressed;
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
		if (b < 0 || b != pressed) return; /* let go off the button pressed */
		if (b == KP_UI_PAD_SHIFT) {
			p->shift = !p->shift;
			ui->input.keys[0] = (uint8_t)p->shift;
			p->drawn = 0;
			return;
		}
		if (b == KP_UI_PAD_TOGGLE) {
			pad_open(h, !p->open);
			return;
		}
		kp_ui_window_char(ui, (uint16_t)('0' + KP_UI_PAD_GROUP(b)), 0); /* the digit: its group recalled */
		pad_open(h, 0);
		return;
	default: /* the long press: Ctrl and the digit, the group assigned */
		if (pressed < 0 || pressed >= KP_UI_PAD_SHIFT) return;
		pad_open(h, 0);
		digit = (uint16_t)('0' + KP_UI_PAD_GROUP(pressed));
		kp_hud_key(ui, 1, 0x11, 0);
		kp_hud_key(ui, 1, digit, 0);
		kp_hud_key(ui, 0, digit, 0);
		kp_hud_key(ui, 0, 0x11, 0);
		ui->input.keys[0] = (uint8_t)p->shift;
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
	for (k = 0; k < steps; k++) kp_ui_hud_frame(ui);
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
	pad_frame(h, vh);
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

/* The window's characters are single bytes (Latin-1's code points): a character past 255 has none and is left out,
 * and so is a control character (Enter's carriage return comes from the key itself). */
void kp_hud_text(KpUi *ui, const char *utf8)
{
	const unsigned char *s = (const unsigned char *)utf8;
	while (*s) {
		uint32_t b = *s, cp = 0x110000;
		int len = 1;
		if (b < 0x80) {
			cp = b;
		} else if (b >= 0xc0 && b < 0xe0 && s[1]) {
			cp = (b - 0xc0) * 64 + (s[1] % 64);
			len = 2;
		} else {
			len = b >= 0xf0 ? 4 : b >= 0xe0 ? 3 : 1;
		}
		if (cp >= 32 && cp < 256) kp_ui_window_char(ui, (uint16_t)cp, 0);
		while (len-- > 0 && *s) s++;
	}
}

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

int kp_hud_touch_target(KpHud *h, int32_t lx, int32_t ly, int64_t cam_x, int64_t cam_y)
{
	KpUi *ui = h->ui;
	if (kp_hud_pad_at(h, lx, ly) >= 0) return KP_TOUCH_BUTTON;
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

uint16_t kp_hud_back_key(const KpUi *ui)
{
	const KpGame *g = ui->game;
	if (g == NULL || ui->modeless >= 0 || kp_scalar(g, KP_PAUSED) != 0 || ui->input.targeting || kp_scalar(g, KP_PLACEMENT_ACTIVE) == 1
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
	int over = 0;
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

/* The profile (its login's name and progress) as the interface's character list. */
static void characters(KpUi *ui, const KpProfile *p)
{
	KpUiProfile *c = &ui->menus.profiles[0];
	int r;
	ui->menus.profile_count = 0;
	if (!p->name[0]) return;
	memset(c, 0, sizeof *c);
	snprintf(c->name, sizeof c->name, "%s", p->name);
	for (r = 0; r < KP_PROFILE_WORD_RACES; r++) {
		c->progress[r] = (uint32_t)p->progress.original[r];
		c->progress[r + KP_PROFILE_WORD_RACES] = (uint32_t)p->progress.expansion[r];
	}
	ui->menus.profile_count = 1;
}

void kp_glue_init(KpGlue *g, KpUi *ui, KpProfile *profile, uint32_t now_ms)
{
	memset(g, 0, sizeof *g);
	g->ui = ui;
	g->profile = profile;
	kp_canvas_init(&g->canvas);
	characters(ui, profile);
	ui->now = now_ms;
	kp_glue_menus(g, KP_GLUE_MAIN);
}

void kp_glue_free(KpGlue *g)
{
	kp_canvas_free(&g->canvas);
	memset(g, 0, sizeof *g);
}

void kp_glue_menus(KpGlue *g, int state)
{
	characters(g->ui, g->profile);
	kp_ui_menus_enter(g->ui, state);
	g->screen = KP_GLUE_MENUS;
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

/* The profile's progress the menus raised (an episode's start), merged into the host's profile; the login's name. */
static void take_profile(KpGlue *g, const char *player)
{
	KpUi *ui = g->ui;
	int k, r;
	if (player && player[0]) snprintf(g->profile->name, sizeof g->profile->name, "%s", player);
	ui->menus.profiles_changed = 0; /* the host reads the list here */
	for (k = 0; k < ui->menus.profile_count; k++) {
		const KpUiProfile *c = &ui->menus.profiles[k];
		KpCampaignProfile raised;
		if (strcmp(c->name, g->profile->name) != 0) continue;
		kp_campaign_new_profile(&raised);
		for (r = 0; r < KP_PROFILE_WORD_RACES; r++) {
			raised.original[r] = c->progress[r];
			raised.expansion[r] = c->progress[r + KP_PROFILE_WORD_RACES];
		}
		kp_profile_merge(&g->profile->progress, &raised);
	}
}

static KpGlueEvent poll(KpGlue *g)
{
	KpUi *ui = g->ui;
	KpGlueEvent ev;
	memset(&ev, 0, sizeof ev);
	if (g->screen == KP_GLUE_MENUS) {
		KpUiMenus *m = &ui->menus;
		int action;
		if (!kp_ui_menus_poll(ui)) return ev;
		action = m->action;
		m->action = KP_UI_MENU_NONE;
		take_profile(g, m->player);
		ev.profile_changed = 1;
		if (action == KP_UI_MENU_MISSION && m->state >= 7 && m->state <= 9) {
			g->screen = KP_GLUE_NONE;
			ev.kind = KP_GLUE_MISSION;
			ev.mission = m->mission;
			ev.state = m->state;
			ev.expansion = m->expansion;
		} else if (action == KP_UI_MENU_EXIT) {
			g->screen = KP_GLUE_NONE;
			ev.kind = KP_GLUE_EXIT;
			ev.profile_changed = 0;
		} else if (action == KP_UI_MENU_LOAD_SAVE) { /* the load screen's saved game (RunLoadGameScreen 0x459970) */
			g->screen = KP_GLUE_NONE;
			ev.kind = KP_GLUE_LOAD;
			ev.save = m->save_choice;
		} else {
			/* the next state's screen (a cinematic row is skipped: no SMK cinematics in V1, plan phase 9) */
			kp_ui_menus_enter(ui, m->state);
			ev.kind = KP_GLUE_PROFILE;
		}
	} else if (g->screen == KP_GLUE_ROOM) {
		kp_ui_ready_room_continue(ui);
		kp_ui_ready_room_idle(ui);
		if (is_open(ui, ui->briefing.dialog)) {
			g->room_opened = 1;
		} else if (g->room_opened) {
			g->screen = KP_GLUE_NONE;
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
	kp_ui_tick(ui, now_ms);
	idle(ui);
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
	kp_ui_input_event(ui, &e);
}

void kp_glue_saves(KpUi *ui, const KpProfile *profile)
{
	int k;
	ui->menus.save_count = 0;
	for (k = 0; k < profile->save_count && k < KP_UI_SAVES; k++) {
		snprintf(ui->menus.saves[k], sizeof ui->menus.saves[k], "%s", profile->saves[k]);
		ui->menus.save_count++;
	}
}

void kp_glue_capture_lost(KpGlue *g)
{
	g->buttons = 0;
	kp_ui_capture_changed(g->ui);
}
