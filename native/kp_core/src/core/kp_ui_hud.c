/* The in-game console over a native game (plan phase 9B; status.cpp, statres.cpp, statlb.cpp; atlas P36, P37): the
 * console's dialogs, the selection's snapshot that the status panel shows and the refreshes the game asks for. The
 * console reads the game (KpUi.game); it writes only the minimap blink of the units (ADR 0010). */
#include "kp_ui.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "kp_game.h"
#include "kp_ids.h"

#define RESOURCE_TIMER 1     /* InitResourceDialog 0x4a7850: UpdateResourcePanel every 20 ms */
#define RESOURCE_PERIOD 20
#define RESOURCE_BAR 6       /* the bar control (an observer's), hidden in a game */
#define RESOURCE_COUNTDOWN -10 /* the trigger countdown's text, hidden until SetCountdownText 0x4a7960 */
#define RESOURCE_CONTROLS 6
#define RESOURCE_FLAGS 0x30002000u
#define RESOURCE_FORMAT 0x21
#define RESOURCE_COLOUR 7
#define SUPPLY_OVER_COLOUR 6 /* the maximum below the used supply */
#define BAR_FRAME_WUSER 10
#define ICON_MINERALS 0
#define ICON_GAS 1           /* + the local race */
#define ICON_SUPPLY 4        /* + the race */
#define ICON_BAR 11
#define FONT_SLOT_10 1       /* 0x6d3028 */
#define RACES 3
#define STATIC_TYPE 9        /* the static text controls */
#define LB_TIMER 1           /* LeaderboardDialogInteract 0x4a5bd0: the refresh every 1000 ms (0x4a62b0) */
#define LB_PERIOD 1000
#define LB_FLAGS 0x30002000u
#define LB_FORMAT 0x21
#define LB_COLOUR 7
#define LB_ROWS 8
#define LB_SWATCH_FIRST -17  /* the swatches -17..-10, the chain from -10 */
#define LB_SWATCHES -10
#define LB_VALUES -20        /* the values' (greed: minerals') chain */
#define LB_GAS -40
#define LB_MINERALS_TEXT -30
#define LB_LABEL -28
#define LB_MINERAL_ICON -48
#define LB_GAS_ICON -49
#define LB_GREED 0x28
#define LB_GAP 4
#define LB_LABEL_UNITS 0x330 /* stat_txt: "Units" ... "Score" from type 0x11, "Units to go" ... from 0x21 (0x4a7d70) */
#define LB_LABEL_TO_GO 0x325

static const char ICONS[] = "game\\icons.grp";

static const KpUiFile *icons(const KpUi *ui) { return kp_ui_file(ui->grps, ui->grp_count, ICONS); }

static int32_t next_control(const KpUi *ui, int32_t c) { return c >= 0 ? ui->controls[c].next : -1; }

/* --- the resource panel (statres.bin) --- */

/* DrawResourceBar 0x4a7590: icons.grp frame 11 filled to lUser / 256 of its width, the rest in colour 0. */
static void resource_bar(KpUi *ui, const KpUiControl *c, int32_t x, uint16_t height)
{
	int32_t w, h, y;
	const uint8_t *pixels;
	uint16_t filled, rest;
	const KpUiFile *f = icons(ui);
	if (f == NULL || kp_ui_grp_bitmap(f->data, f->size, ICON_BAR, &w, &h, &pixels) != 0) return;
	filled = (uint16_t)((uint32_t)((int32_t)(int16_t)w * (int32_t)c->user) >> 8);
	rest = (uint16_t)(w - filled);
	y = (uint16_t)((uint16_t)(((int32_t)height - (int16_t)h) / 2) + (uint16_t)c->top);
	if (filled != 0) {
		int16_t src[4];
		src[0] = 0;
		src[1] = 0;
		src[2] = (int16_t)(filled - 1);
		src[3] = (int16_t)(h - 1);
		kp_ui_blit_opaque(ui, x, y, pixels, w, h, src);
	}
	if (rest != 0) kp_ui_fill_rect(ui, x + filled, y, rest, h, 0);
}

/* DrawResourceControl 0x4a74c0: the icon wUser centred on the control's height, then its text (or the bar). */
static void resource_draw(KpUi *ui, KpUiControl *c, int32_t x, int32_t y, const int16_t clip[4])
{
	int32_t w, h;
	const uint8_t *pixels;
	uint16_t height = (uint16_t)(c->bottom - c->top + 1);
	int16_t src[4];
	const KpUiFile *f = icons(ui);
	(void)x;
	(void)y;
	(void)clip;
	if (f == NULL || kp_ui_grp_bitmap(f->data, f->size, (uint8_t)c->wuser, &w, &h, &pixels) != 0) return;
	src[0] = 0;
	src[1] = 0;
	src[2] = (int16_t)(w - 1);
	src[3] = (int16_t)(h - 1);
	kp_ui_blit_transparent(ui, c->left, (uint16_t)c->top + ((int32_t)height - (int16_t)h) / 2, pixels, w, h, src);
	if (c->id == RESOURCE_BAR) {
		resource_bar(ui, c, (uint16_t)(c->left + 2 + w), height);
		return;
	}
	if (c->text == NULL) return;
	(void)kp_ui_text_select_font(&ui->text, ui->fonts[FONT_SLOT_10], ui->font_sizes[FONT_SLOT_10]);
	ui->text.flags = RESOURCE_FORMAT;
	ui->text.clip_left = c->left;
	ui->text.clip_top = c->top;
	ui->text.clip_right = c->right;
	ui->text.clip_bottom = c->bottom;
	kp_ui_select_color(&ui->text, RESOURCE_COLOUR);
	(void)kp_ui_draw_text(&ui->text, (int16_t)w + 2 + c->left, c->top, c->text);
	ui->text.font = NULL;
}

static char *resource_text(KpUi *ui, const KpUiControl *c)
{
	int index = c->id;
	if (index < 0 || index >= (int)(sizeof ui->hud.resource_text / sizeof ui->hud.resource_text[0])) index = 0;
	return ui->hud.resource_text[index];
}

/* SetSupplyText 0x4a7640: "(used + 1) / 2" over "(max + 1) / 2", red when the maximum is the smaller. */
static void set_supply_text(KpUi *ui, int32_t k, uint16_t frame, uint32_t used, uint32_t max)
{
	KpUiControl *c = &ui->controls[k];
	uint32_t b = ((max & 0xffff) + 1) / 2, a = ((used & 0xffff) + 1) / 2, value = a << 16 | b;
	char *text;
	kp_ui_show_control(ui, k);
	if (c->text != NULL && c->user == value && c->wuser == frame) return;
	c->user = value;
	c->wuser = frame;
	text = resource_text(ui, c);
	snprintf(text, 32, "%c%d\x01/%d", (uint16_t)b < (uint16_t)a ? SUPPLY_OVER_COLOUR : RESOURCE_COLOUR, (int)a, (int)b);
	c->text = (const uint8_t *)text;
	kp_ui_mark_dirty(ui, k);
}

/* SetAnimatedResourceText 0x4a76e0: the shown value steps a sixteenth of the way (and one more) to the target. */
static void set_animated_text(KpUi *ui, int32_t k, uint16_t frame, int32_t *shown, int32_t target, int snap)
{
	KpUiControl *c = &ui->controls[k];
	int32_t old = *shown, d = (int32_t)((uint32_t)target - (uint32_t)old);
	char *text;
	kp_ui_show_control(ui, k);
	if (d == 0) {
		if (c->text != NULL && (int32_t)c->user == target && c->wuser == frame) return;
	} else if (snap) {
		*shown = target;
	} else {
		*shown = (int32_t)((uint32_t)(d / 16) + (d > 0 ? 1u : 0xffffffffu) + (uint32_t)old);
	}
	c->wuser = frame;
	c->user = (uint32_t)*shown;
	text = resource_text(ui, c);
	snprintf(text, 32, "%d", (int)*shown);
	c->text = (const uint8_t *)text;
	kp_ui_mark_dirty(ui, k);
}

/* UpdateCountdownText 0x4bde30 and HideCountdownText 0x4a79a0, which the game runs in its second's pass: the trigger
 * countdown as "m:ss" (or "h:mm:ss") in control -10 (SetCountdownText 0x4a7960), the control hidden without it. */
static void update_countdown(KpUi *ui, int32_t dialog)
{
	int32_t k = kp_ui_find_control(ui, dialog, RESOURCE_COUNTDOWN);
	int64_t s = ui->game->trig.countdown_text;
	char text[32];
	if (k < 0) return;
	if (s < 0) {
		kp_ui_hide_control(ui, k);
		return;
	}
	if (s / 3600 != 0) snprintf(text, sizeof text, "%d:%02d:%02d", (int)(s / 3600), (int)(s % 3600 / 60), (int)(s % 60));
	else snprintf(text, sizeof text, "%d:%02d", (int)(s / 60), (int)(s % 60));
	kp_ui_show_control(ui, k);
	if (ui->controls[k].text != NULL && strcmp((const char *)ui->controls[k].text, text) == 0) return;
	snprintf(ui->hud.resource_text[0], sizeof ui->hud.resource_text[0], "%s", text);
	ui->controls[k].text = (const uint8_t *)ui->hud.resource_text[0];
	kp_ui_mark_dirty(ui, k);
}

/* UpdateResourcePanel 0x4a79c0, the 20 ms timer: the supply of each race the player has, the gas, the minerals; the
 * controls left over are hidden. */
static void update_resources(KpUi *ui, int32_t dialog)
{
	KpGame *g = ui->game;
	int32_t c = kp_ui_find_control(ui, dialog, 1);
	int64_t viewer;
	int race;
	if (g == NULL || c < 0) return;
	update_countdown(ui, dialog);
	viewer = g->local_player; /* TODO: a replay's viewer (0x67cc90) is outside V1 */
	for (race = 0; race < RACES && c >= 0; race++) {
		uint32_t used = (uint32_t)kp_supply_used(g, race, viewer);
		uint32_t max = (uint32_t)kp_supply_provided_capped(g, race, viewer);
		if ((int16_t)max != 0) {
			set_supply_text(ui, c, (uint16_t)(ICON_SUPPLY + race), used, max);
			c = next_control(ui, c);
		}
	}
	if (c < 0) return; /* a panel with fewer controls than the original's statres.bin */
	set_animated_text(ui, c, (uint16_t)(g->players.race[g->local_player] + ICON_GAS), &ui->hud.shown_gas,
		(int32_t)g->players.gas[viewer], 0);
	if ((c = next_control(ui, c)) < 0) return;
	set_animated_text(ui, c, ICON_MINERALS, &ui->hud.shown_minerals, (int32_t)g->players.minerals[viewer], 0);
	for (c = next_control(ui, c); c >= 0 && ui->controls[c].id < RESOURCE_BAR; c = next_control(ui, c)) {
		kp_ui_hide_control(ui, c);
	}
}

/* ResourceChildInteract 0x4a7790. */
static int resource_child_interact(KpUi *ui, KpUiControl *c, KpUiEvent *e)
{
	KpUiInteract stat = kp_ui_type_interact(STATIC_TYPE); /* StaticControlInteract 0x4c1860 */
	if (e->type == KP_UI_USER && e->code == KP_UI_CREATE) {
		c->user = 0;
		c->draw = resource_draw;
		c->interact = stat;
		c->wuser = c->id == RESOURCE_BAR ? BAR_FRAME_WUSER : 0;
	}
	return stat(ui, c, e);
}

/* InitResourceDialog 0x4a7850. */
static void init_resources(KpUi *ui, int32_t dialog)
{
	static const KpUiInteract CHILDREN[RESOURCE_CONTROLS] = { resource_child_interact, resource_child_interact,
		resource_child_interact, resource_child_interact, resource_child_interact, resource_child_interact };
	ui->controls[dialog].flags |= RESOURCE_FLAGS;
	kp_ui_bind_interacts(ui, dialog, CHILDREN, RESOURCE_CONTROLS);
	(void)kp_ui_set_timer(&ui->timers, dialog, RESOURCE_TIMER, RESOURCE_PERIOD, kp_ui_callback(ui, update_resources),
		ui->now);
	if (ui->hud.resources >= 0) {
		int32_t bar = kp_ui_find_control(ui, ui->hud.resources, RESOURCE_BAR);
		if (bar >= 0) {
			kp_ui_set_visible(ui, bar, 0);
			ui->controls[bar].user = 0;
		}
	}
}

/* The fluff dialogs' map of the input that passes to the game (0x4a5c58), which the resource and leaderboard dialogs
 * share. */
static int fluff_input(const KpUiEvent *e)
{
	switch (e->type) {
	case KP_UI_KEY_DOWN: case KP_UI_KEY_REPEAT: case KP_UI_MOUSE_MOVE: case KP_UI_LEFT_DOWN: case KP_UI_LEFT_DOUBLE:
	case KP_UI_RIGHT_DOWN: case KP_UI_RIGHT_DOUBLE: case KP_UI_CHAR:
		return 1;
	default:
		return 0;
	}
}

/* ResourceDialogInteract 0x4a77d0: the user events, and the fluff dialog's map of the input that passes to the game. */
static int resource_dialog_interact(KpUi *ui, KpUiControl *d, KpUiEvent *e)
{
	int32_t self = (int32_t)(d - ui->controls);
	KpUiInteract stock = kp_ui_type_interact(0);
	if (e->type == KP_UI_USER) {
		if (e->code == KP_UI_CREATE) init_resources(ui, self);
		else if (e->code == KP_UI_DESTROY) kp_ui_delete_timer(&ui->timers, self, RESOURCE_TIMER);
		else if (e->code == KP_UI_HIT_TEST) return 0;
		return stock(ui, d, e);
	}
	return fluff_input(e) ? 0 : stock(ui, d, e);
}

/* CreateResourceDialog 0x4a78f0. */
static int create_resources(KpUi *ui)
{
	int32_t d;
	ui->hud.shown_minerals = 0;
	ui->hud.shown_gas = 0;
	d = kp_ui_load_dialog(ui, "rez\\statres.bin");
	if (d < 0) return -1;
	ui->hud.resources = d;
	kp_ui_init_dialog(ui, d, resource_dialog_interact);
	return 0;
}

/* --- the leaderboard (statlb.bin) --- */

/* DrawColorSwatch 0x4a5af0: the control filled with the player lUser's colour. */
static void lb_swatch_draw(KpUi *ui, KpUiControl *c, int32_t x, int32_t y, const int16_t clip[4])
{
	(void)x;
	(void)y;
	(void)clip;
	kp_ui_fill_rect(ui, c->left, c->top, c->right - c->left + 1, c->bottom - c->top + 1,
		ui->minimap.player_colours[c->user & 15]);
}

/* DrawLeaderboardIcon 0x4a5b30: icons.grp frame wUser centred on the control's height. */
static void lb_icon_draw(KpUi *ui, KpUiControl *c, int32_t x, int32_t y, const int16_t clip[4])
{
	int32_t w, h;
	const uint8_t *pixels;
	int16_t src[4];
	const KpUiFile *f = icons(ui);
	(void)x;
	(void)y;
	(void)clip;
	if (f == NULL || kp_ui_grp_bitmap(f->data, f->size, (uint8_t)c->wuser, &w, &h, &pixels) != 0) return;
	src[0] = 0;
	src[1] = 0;
	src[2] = (int16_t)(w - 1);
	src[3] = (int16_t)(h - 1);
	kp_ui_blit_transparent(ui, c->left, c->top + ((int32_t)(c->bottom - c->top + 1) - (int16_t)h) / 2, pixels, w, h, src);
}

/* DrawLeaderboardText 0x4a5b80. */
static void lb_text_draw(KpUi *ui, KpUiControl *c, int32_t x, int32_t y, const int16_t clip[4])
{
	(void)x;
	(void)y;
	(void)clip;
	if (c->text == NULL) return;
	(void)kp_ui_text_select_font(&ui->text, ui->fonts[FONT_SLOT_10], ui->font_sizes[FONT_SLOT_10]);
	ui->text.flags = LB_FORMAT;
	ui->text.clip_left = c->left;
	ui->text.clip_top = c->top;
	ui->text.clip_right = c->right;
	ui->text.clip_bottom = c->bottom;
	kp_ui_select_color(&ui->text, LB_COLOUR);
	(void)kp_ui_draw_text(&ui->text, c->left, c->top, c->text);
	ui->text.font = NULL;
}

/* InitLeaderboardChild 0x4a6200. */
static void lb_init_child(KpUi *ui, int32_t k)
{
	KpUiControl *c = &ui->controls[k];
	if (c->id >= LB_SWATCH_FIRST && c->id <= LB_SWATCHES) {
		c->draw = lb_swatch_draw;
		c->user = 0;
	} else if (c->id == LB_MINERAL_ICON) {
		c->draw = lb_icon_draw;
		c->wuser = ICON_MINERALS;
	} else if (c->id == LB_GAS_ICON) {
		c->draw = lb_icon_draw;
		c->wuser = (uint16_t)(ICON_GAS + ui->game->players.race[ui->game->local_player]);
	} else if (c->type == STATIC_TYPE) {
		c->draw = lb_text_draw;
	}
}

/* The children with ids in [low, high] hidden (0x4a5f00: -28..-20; 0x4a5e70: -49..-30; 0x4a5e60: all). */
static void lb_hide(KpUi *ui, int16_t low, int16_t high)
{
	int32_t c;
	for (c = ui->controls[ui->hud.leaderboard].child; c >= 0; c = ui->controls[c].next) {
		if (ui->controls[c].id >= low && ui->controls[c].id <= high) kp_ui_hide_control(ui, c);
	}
}

static int lb_row_shown(KpUi *ui, int row)
{
	return (row == 0 || ui->hud.lb_expanded) && ui->game->trig.lb_entries[row][0] != -1;
}

/* A row's swatch shown with its player. */
static void lb_swatch(KpUi *ui, int32_t k, int64_t player)
{
	kp_ui_show_control(ui, k);
	if (ui->controls[k].user != (uint32_t)player) {
		ui->controls[k].user = (uint32_t)player;
		kp_ui_mark_dirty(ui, k);
	}
}

static void lb_text(KpUi *ui, int32_t k, char *text, int64_t value)
{
	snprintf(text, 16, "%d", (int)value);
	ui->controls[k].text = (const uint8_t *)text;
	kp_ui_show_control(ui, k);
	kp_ui_mark_dirty(ui, k);
}

/* PlaceLeaderboardLabel 0x4a5fa0: the label after the first row's value, its text the board's. */
static void lb_label(KpUi *ui)
{
	KpGame *g = ui->game;
	int32_t d = ui->hud.leaderboard, label = kp_ui_find_control(ui, d, LB_LABEL), row = kp_ui_find_control(ui, d, LB_VALUES);
	int64_t type = g->trig.lb_action;
	const uint8_t *text;
	KpUiControl *l;
	if (label < 0 || row < 0) return;
	l = &ui->controls[label];
	kp_ui_show_control(ui, label);
	if (ui->hud.lb_values[0][0] == 0) {
		l->left = (int16_t)(ui->controls[row].right + LB_GAP);
		kp_ui_hide_control(ui, label);
	} else {
		int32_t w, x, max;
		(void)kp_ui_text_select_font(&ui->text, ui->fonts[FONT_SLOT_10], ui->font_sizes[FONT_SLOT_10]);
		w = kp_ui_text_width(&ui->text, (const uint8_t *)ui->hud.lb_values[0]);
		ui->text.font = NULL;
		x = ui->controls[row].left + LB_GAP + w;
		max = ui->controls[row].right + LB_GAP;
		l->left = (int16_t)(max < x ? max : x);
	}
	text = (const uint8_t *)kp_map_string(g, g->trig.lb_label);
	if (text == NULL) {
		if (type >= 0x11 && type <= 0x15) text = kp_ui_tbl(ui, KP_UI_STAT_TXT, (uint32_t)(LB_LABEL_UNITS + type));
		else if (type >= 0x21 && type <= 0x25) text = kp_ui_tbl(ui, KP_UI_STAT_TXT, (uint32_t)(LB_LABEL_TO_GO + type));
	}
	l->text = text;
	kp_ui_mark_dirty(ui, label);
}

/* DrawValueLeaderboard 0x4a60b0. */
static void lb_values(KpUi *ui)
{
	int32_t d = ui->hud.leaderboard, v = kp_ui_find_control(ui, d, LB_VALUES), s = kp_ui_find_control(ui, d, LB_SWATCHES);
	int row;
	if (ui->hud.lb_state != 1) {
		ui->hud.lb_state = 1;
		lb_hide(ui, LB_GAS_ICON, LB_MINERALS_TEXT);
	}
	for (row = 0; row < LB_ROWS && v >= 0 && s >= 0; row++, v = next_control(ui, v), s = next_control(ui, s)) {
		if (lb_row_shown(ui, row)) {
			lb_swatch(ui, s, ui->game->trig.lb_entries[row][0]);
			lb_text(ui, v, ui->hud.lb_values[row], ui->game->trig.lb_entries[row][1]);
		} else {
			kp_ui_hide_control(ui, s);
			ui->hud.lb_values[row][0] = 0;
			kp_ui_hide_control(ui, v);
		}
	}
	lb_label(ui);
}

/* DrawGreedLeaderboard 0x4a5c90: each row's minerals and gas. */
static void lb_greed(KpUi *ui)
{
	KpGame *g = ui->game;
	int32_t d = ui->hud.leaderboard, m = kp_ui_find_control(ui, d, LB_MINERALS_TEXT);
	int32_t gas = kp_ui_find_control(ui, d, LB_GAS), s = kp_ui_find_control(ui, d, LB_SWATCHES);
	int row;
	if (ui->hud.lb_state != 2) {
		ui->hud.lb_state = 2;
		lb_hide(ui, LB_LABEL, LB_VALUES);
	}
	if (kp_ui_find_control(ui, d, LB_MINERAL_ICON) >= 0) kp_ui_show_control(ui, kp_ui_find_control(ui, d, LB_MINERAL_ICON));
	if (kp_ui_find_control(ui, d, LB_GAS_ICON) >= 0) kp_ui_show_control(ui, kp_ui_find_control(ui, d, LB_GAS_ICON));
	for (row = 0; row < LB_ROWS && m >= 0 && gas >= 0 && s >= 0;
		row++, m = next_control(ui, m), gas = next_control(ui, gas), s = next_control(ui, s)) {
		if (lb_row_shown(ui, row)) {
			int64_t p = g->trig.lb_entries[row][0];
			lb_swatch(ui, s, p);
			lb_text(ui, m, ui->hud.lb_values[row], g->players.minerals[p]);
			lb_text(ui, gas, ui->hud.lb_gas[row], g->players.gas[p]);
		} else {
			kp_ui_hide_control(ui, s);
			kp_ui_hide_control(ui, m);
			kp_ui_hide_control(ui, gas);
		}
	}
}

/* The timer's refresh 0x4a62b0. */
static void lb_refresh(KpUi *ui, int32_t dialog)
{
	(void)dialog;
	if (ui->game->trig.lb_action == 0) {
		if (ui->hud.lb_state != 0) {
			ui->hud.lb_state = 0;
			lb_hide(ui, INT16_MIN, INT16_MAX);
		}
	} else if (ui->game->trig.lb_action == LB_GREED) {
		lb_greed(ui);
	} else {
		lb_values(ui);
	}
}

void kp_ui_toggle_leaderboard(KpUi *ui)
{
	/* ToggleLeaderboardExpanded 0x4a6260: the other rows shown or hidden, at once */
	ui->hud.lb_expanded ^= 1;
	if (ui->game != NULL && ui->hud.leaderboard >= 0) lb_refresh(ui, ui->hud.leaderboard);
}

/* LeaderboardDialogInteract 0x4a5bd0. */
static int lb_dialog_interact(KpUi *ui, KpUiControl *d, KpUiEvent *e)
{
	int32_t self = (int32_t)(d - ui->controls), c;
	KpUiInteract stock = kp_ui_type_interact(0);
	if (e->type == KP_UI_USER) {
		if (e->code == KP_UI_CREATE) {
			d->flags |= LB_FLAGS;
			for (c = d->child; c >= 0; c = ui->controls[c].next) lb_init_child(ui, c);
			(void)kp_ui_set_timer(&ui->timers, self, LB_TIMER, LB_PERIOD, kp_ui_callback(ui, lb_refresh), ui->now);
		} else if (e->code == KP_UI_DESTROY) {
			kp_ui_delete_timer(&ui->timers, self, LB_TIMER);
		} else if (e->code == KP_UI_HIT_TEST) {
			return 0;
		}
		return stock(ui, d, e);
	}
	return fluff_input(e) ? 0 : stock(ui, d, e);
}

/* CreateLeaderboardDialog 0x4a5f20 (a single-player game always has it). */
static int create_leaderboard(KpUi *ui)
{
	int32_t d;
	ui->hud.lb_state = 0;
	ui->hud.lb_expanded = 1;
	d = kp_ui_load_dialog(ui, "rez\\statlb.bin");
	if (d < 0) return -1;
	ui->hud.leaderboard = d;
	kp_ui_init_dialog(ui, d, lb_dialog_interact);
	return 0;
}

/* --- the text box (textbox.cpp) --- */

#define TB_FLAGS 0x30002000u
#define TB_LABELS_LAST 3
#define TB_WHISPER 4
#define TB_PROMPT 5
#define TB_EDIT 6
#define TB_MAX 78               /* TextboxEditCharFilter 0x4b3910 */
#define TB_SINGLE 1
#define CMD_CHEAT 0x12
#define EXIT_QUIT_MISSION 4     /* RequestGameExit 0x444e00's code: the glue's campaign menu or its pending mission */
#define WARP_REFUSED 63         /* HandleMissionWarpCheat 0x40e030: no warp to mission 63 */
#define CHEA_HEADER 6           /* u32 carried bits, u16 count */

int kp_ui_set_cheats(KpUi *ui, const uint8_t *chea, size_t size)
{
	size_t at = CHEA_HEADER;
	uint32_t n, k;
	if (chea == NULL || size < CHEA_HEADER) return -1;
	n = (uint32_t)chea[4] | (uint32_t)chea[5] << 8;
	for (k = 0; k < n; k++) {
		if (at + 5 > size || at + 5 + chea[at + 4] > size) return -1;
		at += 5 + chea[at + 4];
	}
	ui->cheats = chea;
	ui->cheats_size = size;
	return 0;
}

/* HandleMissionWarpCheat 0x40e030: under Ophelia's cheat a campaign's prefix and a mission ("terran 5", "xzerg 3b")
 * end the game for that mission's ready room (Quit Mission's exit, the glue's pending mission); 1 for a prefix. */
static int warp_cheat(KpUi *ui, const uint8_t *text)
{
	static const struct { const char *prefix; int race, base, expansion; } WARPS[] = { /* 0x4eba18 */
		{ "terran", 1, 0, 0 }, { "zerg", 0, 1, 0 }, { "protoss", 2, 1, 0 },
		{ "xterran", 1, 1, 1 }, { "xzerg", 0, 1, 1 }, { "xprotoss", 2, 1, 1 } };
	size_t k, n, i;
	uint16_t mission;
	if (!(kp_cheats(ui->game) & KP_CHEAT_OPHELIA)) return 0;
	for (k = 0; k < sizeof WARPS / sizeof WARPS[0]; k++) {
		n = strlen(WARPS[k].prefix);
		for (i = 0; i < n && text[i] != 0 && tolower(text[i]) == WARPS[k].prefix[i]; i++) {}
		if (i == n) break;
	}
	if (k == sizeof WARPS / sizeof WARPS[0]) return 0;
	if (kp_ui_warp_mission(ui, (const char *)text + n, WARPS[k].race, WARPS[k].base, WARPS[k].expansion, &mission)
		&& mission != WARP_REFUSED) {
		kp_ui_set_pending_mission(ui, mission, WARPS[k].race, WARPS[k].expansion);
		ui->game_exit = EXIT_QUIT_MISSION; /* RequestGameExit 0x444e00(4) */
		ui->menus.state = 0;
	}
	return 1;
}

/* ProcessCheatText 0x40dee0: a cheat's text toggles its bit (a single-player game; the CRT generator is reseeded either
 * way), else the mission warp's text. */
static int process_cheat(KpUi *ui, const uint8_t *text, int64_t *flags)
{
	char low[256];
	size_t n = strlen((const char *)text), at = CHEA_HEADER, k;
	uint32_t count, c;
	if (n > 255) n = 255;
	for (k = 0; k < n; k++) low[k] = (char)tolower(text[k]);
	low[n] = 0;
	ui->input.reseed = 1;
	if (ui->cheats == NULL) return 0;
	count = (uint32_t)ui->cheats[4] | (uint32_t)ui->cheats[5] << 8;
	for (c = 0; c < count; c++) {
		const uint8_t *e = ui->cheats + at;
		uint32_t bit = (uint32_t)e[0] | (uint32_t)e[1] << 8 | (uint32_t)e[2] << 16 | (uint32_t)e[3] << 24;
		size_t len = e[4];
		if (len == n && memcmp(e + 5, low, n) == 0) {
			*flags ^= bit;
			return 1;
		}
		at += 5 + len;
	}
	return warp_cheat(ui, text);
}

/* LayoutTextbox 0x4b3db0 (a single player's mode shows label 1). */
static void textbox_layout(KpUi *ui)
{
	int32_t d = ui->hud.textbox, c, edit = -1, label = -1;
	uint8_t mode = ui->hud.textbox_mode;
	static const uint8_t SHOWN_IN[TB_WHISPER + 1] = { 0, 1, 3, 2, 4 }; /* labels 1..4 in modes 1, 3, 2, 4 */
	for (c = ui->controls[d].child; c >= 0; c = ui->controls[c].next) {
		KpUiControl *k = &ui->controls[c];
		if (k->id >= 1 && k->id <= TB_WHISPER) {
			/* TODO: a multiplayer's whisper label (FormatWhisperLabel 0x4b3eb0) */
			if (mode != 0 && SHOWN_IN[k->id] == mode) {
				kp_ui_show_control(ui, c);
				label = c;
			} else {
				kp_ui_hide_control(ui, c);
			}
		} else if (k->id == TB_EDIT) {
			edit = c;
		} else if (k->id == TB_PROMPT) {
			if (mode == 0 && k->text != NULL) kp_ui_show_control(ui, c);
			else kp_ui_hide_control(ui, c);
		}
	}
	if (edit < 0) return;
	if (mode == 0) {
		kp_ui_hide_control(ui, edit);
		return;
	}
	if (label >= 0) {
		kp_ui_select_control_font(ui, &ui->controls[d]);
		ui->controls[edit].left = (int16_t)(kp_ui_text_width(&ui->text, ui->controls[label].text) + ui->text.spacing);
		ui->text.font = NULL;
	}
	kp_ui_show_control(ui, edit);
}

void kp_ui_set_textbox_prompt(KpUi *ui, const uint8_t *text)
{
	/* SetTextboxPrompt 0x4b38c0: control 5's text, shown while the text box is closed */
	int32_t c;
	if (ui->hud.textbox < 0 || (c = kp_ui_find_control(ui, ui->hud.textbox, TB_PROMPT)) < 0) return;
	ui->controls[c].text = text;
	if (ui->hud.textbox_mode == 0 && text != NULL) kp_ui_show_control(ui, c);
	else kp_ui_hide_control(ui, c);
	kp_ui_mark_dirty(ui, c); /* 0x4c2840 */
}

/* ToggleTextbox 0x4b3bd0. */
static void textbox_toggle(KpUi *ui, int submit)
{
	int32_t edit = kp_ui_find_control(ui, ui->hud.textbox, TB_EDIT);
	if (ui->hud.textbox_mode == 0) {
		/* TODO: the IME and the player name (Storm 146) */
		kp_ui_set_accelerators(ui, KP_UI_ACCEL_TEXT); /* SetAccelerators102 0x43f120 */
		ui->hud.textbox_mode = TB_SINGLE;
	} else {
		uint8_t *text = edit >= 0 ? kp_ui_edit_buffer(ui, edit) : NULL;
		if (submit && text != NULL) {
			int64_t old = kp_cheats(ui->game), flags = old;
			if (process_cheat(ui, text, &flags) && flags != old) {
				uint8_t cmd[5];
				cmd[0] = CMD_CHEAT;
				cmd[1] = (uint8_t)flags;
				cmd[2] = (uint8_t)(flags >> 8);
				cmd[3] = (uint8_t)(flags >> 16);
				cmd[4] = (uint8_t)(flags >> 24);
				kp_ui_queue_command(ui, cmd, sizeof cmd); /* SendCheatCommand 0x473d40 */
			}
			/* a single player's other lines are discarded */
		}
		if (edit >= 0) kp_ui_edit_set_text(ui, edit, (const uint8_t *)""); /* SetEditText 0x4c8110 */
		kp_ui_set_accelerators(ui, KP_UI_ACCEL_GAME); /* SetGameAccelerators 0x43f140 */
		ui->hud.textbox_mode = 0;
	}
	textbox_layout(ui);
}

/* TextboxEditProc 0x4b3ad0. */
static int textbox_edit(KpUi *ui, KpUiControl *c, KpUiEvent *e)
{
	KpUiInteract stock = kp_ui_type_interact(c->type);
	if (e->type == KP_UI_USER && e->code == KP_UI_FOCUS_LOST && ui->hud.textbox_mode != 0) textbox_toggle(ui, 0);
	if ((e->type == KP_UI_KEY_DOWN || e->type == KP_UI_KEY_REPEAT) && (e->key >= 0x21 && e->key <= 0x28)) return 0;
	if (e->type == KP_UI_CHAR) {
		const uint8_t *t = kp_ui_edit_buffer(ui, (int32_t)(c - ui->controls));
		if (t != NULL && strlen((const char *)t) >= TB_MAX && (e->key & 0xff) != 8) return 1; /* 0x4b3910 */
		(void)stock(ui, c, e);
		return 1;
	}
	return stock(ui, c, e);
}

/* InitTextboxDialog 0x4b3a50. */
static void textbox_init(KpUi *ui, int32_t d)
{
	int32_t c;
	ui->hud.textbox_mode = 0;
	ui->controls[d].flags |= TB_FLAGS;
	kp_ui_select_control_font(ui, &ui->controls[d]);
	for (c = kp_ui_find_control(ui, d, 1); c >= 0 && ui->controls[c].id <= TB_LABELS_LAST; c = ui->controls[c].next) {
		KpUiControl *k = &ui->controls[c];
		k->right = (int16_t)(k->left + kp_ui_text_width(&ui->text, k->text));
	}
	ui->text.font = NULL;
	c = kp_ui_find_control(ui, d, TB_EDIT);
	if (c >= 0) ui->controls[c].interact = textbox_edit;
	/* TODO: a replay moves the dialog 24 pixels up */
}

/* TextboxDialogProc 0x4b3960: a paused single-player game's input stops here (IsSinglePlayerPaused 0x47cb70), where
 * its Escape skips the transmissions (SkipLocalTriggerWaits 0x4bda80; an unpaused game's Escape reaches the game's
 * character procedure 0x448780, which jumps there too). */
static int textbox_proc(KpUi *ui, KpUiControl *d, KpUiEvent *e)
{
	int32_t self = (int32_t)(d - ui->controls);
	KpUiInteract stock = kp_ui_type_interact(0);
	int paused = ui->game != NULL && kp_scalar(ui->game, KP_PAUSED) != 0, r;
	switch (e->type) {
	case KP_UI_KEY_DOWN: case KP_UI_KEY_REPEAT: case KP_UI_MOUSE_MOVE: case KP_UI_LEFT_DOWN: case KP_UI_LEFT_DOUBLE:
	case KP_UI_RIGHT_DOWN: case KP_UI_RIGHT_DOUBLE:
		return paused;
	case KP_UI_USER:
		if (e->code == KP_UI_CREATE) textbox_init(ui, self);
		else if (e->code == KP_UI_HIT_TEST) return 0;
		break;
	case KP_UI_CHAR:
		if (paused) {
			if ((e->key & 0xff) == 27) kp_trigger_skip(ui->game);
			return 1;
		}
		if ((e->key & 0xff) == 13 || (e->key & 0xff) == 10) {
			textbox_toggle(ui, 1);
			return 1;
		}
		if ((e->key & 0xff) == 27 && ui->hud.textbox_mode != 0) {
			textbox_toggle(ui, 0);
			return 1;
		}
		break;
	default:
		break;
	}
	r = stock(ui, d, e);
	return paused ? 1 : r;
}

/* CreateTextbox 0x4b3b70. */
static int create_textbox(KpUi *ui)
{
	char path[24];
	int race = (int)ui->game->players.race[ui->game->local_player];
	static const char LETTERS[RACES] = { 'z', 't', 'p' };
	int32_t d;
	snprintf(path, sizeof path, "rez\\%ctextbox.bin", LETTERS[race >= 0 && race < RACES ? race : 1]);
	d = kp_ui_load_dialog(ui, path);
	if (d < 0) return -1;
	ui->hud.textbox = d;
	kp_ui_init_dialog(ui, d, textbox_proc);
	return 0;
}

/* --- the selection's snapshot and the refreshes (status.cpp) --- */

/* 0x49c140: whether entry should lead the status panel instead of leader. */
static int prefer_leader(KpGame *g, int64_t leader, int64_t entry)
{
	const KpUnits *u = &g->u;
	int disabled_leader, disabled_entry;
	if (leader == 0) return 1;
	disabled_leader = (u->status[leader] & 0x400) != 0 || u->lockdown_timer[leader] != 0 || u->stasis_timer[leader] != 0
		|| u->maelstrom_timer[leader] != 0;
	disabled_entry = (u->status[entry] & 0x400) != 0 || u->lockdown_timer[entry] != 0 || u->stasis_timer[entry] != 0
		|| u->maelstrom_timer[entry] != 0;
	if (disabled_leader != disabled_entry) return disabled_leader;
	return (uint8_t)(KP_DAT(g, units, sublabel, u->type[entry]) + u->rank[entry])
		> (uint8_t)(KP_DAT(g, units, sublabel, u->type[leader]) + u->rank[leader]);
}

/* RebuildStatusSelection 0x4a7cd0. */
static void rebuild_selection(KpUi *ui)
{
	KpGame *g = ui->game;
	int64_t leader = 0;
	uint8_t count = 0;
	int k;
	for (k = 0; k < KP_UI_SLOTS; k++) ui->hud.selection[k] = g->local_selection[k];
	for (k = 0; k < KP_UI_SLOTS; k++) {
		int64_t n = ui->hud.selection[k];
		if (n == 0) continue;
		if (prefer_leader(g, leader, n)) leader = n;
		count++;
	}
	ui->hud.selection_count = count;
	ui->hud.status_unit = leader;
	if (count == 1) {
		ui->hud.selection[0] = leader;
		for (k = 1; k < KP_UI_SLOTS; k++) ui->hud.selection[k] = 0;
	}
	kp_ui_card_modes(ui); /* ComputeCommandCardModes 0x4a18d0 */
}

/* RefreshStatusUi 0x4a7ed0: the command card's, the portrait's and the status panel's refreshes. */
static void refresh(KpUi *ui)
{
	ui->hud.card_refresh = 1;
	ui->hud.portrait_refresh = 1;
	ui->hud.status_refresh = 1;
	ui->status.hovered = -1; /* 0x4a1f80: 0x6aab38 = 0, 0x6aab3c = 0 */
	ui->status.hovered_user = 0;
}

static void at_tick(KpUi *ui, int dialog)
{
	if (ui->hud.create_tick[dialog] != 0) ui->now = ui->hud.create_tick[dialog];
}

/* GameDialogCallback 0x443f20: a control fired (code 2) plays the UI click (PlayUiClickSound 0x49e460). */
static void game_dialog_sound(KpUi *ui, int code)
{
	if (code == 2) kp_ui_play_click(ui);
}

int kp_ui_hud_create(KpUi *ui, KpGame *g, const uint32_t *create_ticks)
{
	/* WasLoadedFromSave 0x499870, then ClearLoadedFromSave 0x499880: the mark is this game's console's, made or not */
	int from_save = ui->from_save;
	memset(&ui->hud, 0, sizeof ui->hud);
	if (create_ticks != NULL) memcpy(ui->hud.create_tick, create_ticks, sizeof ui->hud.create_tick);
	ui->hud.resources = -1;
	ui->hud.leaderboard = -1;
	ui->hud.textbox = -1;
	ui->status.panel = -1;
	ui->portrait.control = -1;
	ui->portrait.dialog = -1;
	ui->minimap.control = -1;
	ui->minimap.dialog = -1;
	ui->card.dialog = -1;
	ui->game = g;
	if (g == NULL) return 0;
	ui->from_save = 0;
	/* a new game on the same interface (the glue's): RunGame 0x445310 loops until RequestGameExit 0x444e00 sets the
	 * code again, InitTriggerState 0x4be240 forgets the result dialog's answer (0x4bcf30) and StartElapsedClock
	 * 0x47a7c0 the paused seconds */
	ui->game_exit = 0;
	ui->result_answered = 0;
	ui->paused_seconds = 0;
	if (kp_ui_lines_reset(ui) != 0) return -1; /* InitGameWorld 0x443790: ResetTextLines 0x46ae30 */
	memset(&ui->input, 0, sizeof ui->input);
	ui->input.double_group = 0xff; /* ResetControlGroups 0x463ea0 -> ResetGroupDoubleTap 0x4721b0 */
	ui->input.over_console = -1;
	ui->input.scroll.cursor_x = 320; /* RunGame 0x445310: the cursor at the screen's centre */
	ui->input.scroll.cursor_y = 240;
	ui->cursor_hidden = 0; /* and its layer shown (ShowCursorLayer 0x43e600; the room's slide-out hid it) */
	ui->cursor_theme = 0; /* the game's arrow: the room's indices are its theme's palette's, not the tileset's */
	ui->ui_sound = game_dialog_sound; /* StartGame 0x443f40 */
	kp_ui_install_game_input(ui);
	kp_ui_tooltip_init(ui); /* the game's interface setup 0x465aa0 */
	kp_ui_sound_start_game(ui); /* InitGameWorld 0x443680 */
	if (kp_ui_load_console(ui, (int)g->players.race[g->local_player]) != 0) return -1;
	/* the fluff dialogs only repeat the console art (docs/phase9.md) */
	at_tick(ui, KP_UI_HUD_PORTRAIT);
	if (ui->portrait_files != NULL && kp_ui_portrait_create(ui) != 0) return -1;
	at_tick(ui, KP_UI_HUD_LEADERBOARD);
	if (create_leaderboard(ui) != 0) return -1;
	at_tick(ui, KP_UI_HUD_RESOURCES);
	if (create_resources(ui) != 0) return -1;
	at_tick(ui, KP_UI_HUD_STATUS);
	if (kp_ui_status_create(ui) != 0) return -1;
	at_tick(ui, KP_UI_HUD_CARD);
	if (kp_ui_card_create(ui) != 0) return -1;
	at_tick(ui, KP_UI_HUD_MENU);
	if (kp_ui_create_menu_button(ui) < 0) return -1;
	at_tick(ui, KP_UI_HUD_TEXTBOX);
	if (create_textbox(ui) != 0) return -1;
	at_tick(ui, KP_UI_HUD_MINIMAP);
	if (ui->minimap.mmni != NULL && kp_ui_minimap_create(ui) != 0) return -1;
	ui->hud.selection_serial = g->local_selection_serial;
	ui->hud.refresh_serial = g->interface_refresh;
	refresh(ui);
	/* RunGame 0x445310: a single player's tips at the start with "Show Tips at Startup", not for a loaded game */
	if (!ui->menus.multiplayer && !from_save && (ui->options & KP_UI_OPTION_TIPS)) {
		kp_ui_options_dialog(ui, KP_UI_OPTIONS_TIPS_AUTOMATIC);
	}
	return 0;
}

/* FreeConsoleUi 0x4a7bc0's dialogs: the minimap 0x46e1a0, the text box 0x4b3b50, the portrait 0x4a6570, the status
 * panel 0x4a8240, the command card 0x4a1e60, the resources 0x4a78b0, the leaderboard 0x4a5c70, the menu button. */
#define CONSOLE_DIALOGS 8
static void console_dialogs(KpUi *ui, int32_t *out[CONSOLE_DIALOGS])
{
	out[0] = &ui->minimap.dialog;
	out[1] = &ui->hud.textbox;
	out[2] = &ui->portrait.dialog;
	out[3] = &ui->status.panel;
	out[4] = &ui->card.dialog;
	out[5] = &ui->hud.resources;
	out[6] = &ui->hud.leaderboard;
	out[7] = &ui->menu_button;
}

int kp_ui_is_console_dialog(KpUi *ui, int32_t d)
{
	int32_t *dialogs[CONSOLE_DIALOGS];
	int k;
	console_dialogs(ui, dialogs);
	for (k = 0; k < CONSOLE_DIALOGS; k++) {
		if (*dialogs[k] == d) return 1;
	}
	return 0;
}

void kp_ui_hud_free(KpUi *ui)
{
	/* FreeConsoleUi 0x4a7bc0: the console's dialogs destroyed, the tooltip hidden 0x4191d0; the portrait's movie ends
	 * with its dialog's */
	int32_t *dialogs[CONSOLE_DIALOGS];
	int k;
	console_dialogs(ui, dialogs);
	for (k = 0; k < CONSOLE_DIALOGS; k++) {
		if (*dialogs[k] >= 0) kp_ui_destroy_dialog(ui, *dialogs[k]);
		*dialogs[k] = -1;
	}
	kp_ui_video_end(&ui->portrait.video);
	ui->portrait.control = -1;
	ui->minimap.control = -1;
	kp_ui_tooltip_hide(ui);
	kp_ui_placement_free(ui); /* EndGame 0x443db0: FreePlacementBitmaps 0x47ce20 */
	ui->game = NULL;
}

void kp_ui_hud_frame(KpUi *ui)
{
	KpGame *g = ui->game;
	if (g == NULL) return;
	if (g->load_serial != ui->hud.load_serial) {
		/* a loaded save: the network's mirror is the selection the game's row holds now (the commands go by it) */
		int k;
		ui->hud.load_serial = g->load_serial;
		for (k = 0; k < KP_UI_SLOTS; k++) ui->input.mirror[k] = g->selections[g->local_player][k];
	}
	if (g->local_selection_serial != ui->hud.selection_serial) {
		ui->hud.selection_serial = g->local_selection_serial;
		ui->hud.rebuild = 1; /* OnSelectionChanged 0x4a7b60 */
		refresh(ui);
	}
	if (g->interface_refresh != ui->hud.refresh_serial) {
		ui->hud.refresh_serial = g->interface_refresh;
		refresh(ui);
	}
	if (ui->hud.rebuild) {
		rebuild_selection(ui);
		ui->hud.rebuild = 0;
	}
	if (ui->portrait.control >= 0) kp_ui_portrait_frame(ui);
	kp_ui_card_refresh(ui);
	kp_ui_status_frame(ui);
}
