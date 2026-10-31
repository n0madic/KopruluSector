/* The game's console (status.cpp, statmisc.cpp; atlas P36, P37): the console art and the panels drawn over it. */
#include "kp_ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kp_game.h"

#define MENU_DIALOG_FLAG 0x20000000u
#define MENU_BUTTON_ID 1                 /* stat_f10.bin's MENU (the virtual-key hotkey F10) */
#define S_GAME_MENU 0x32e         /* "Game Menu (\x03F10\x01)" */

/* SetControlUserWord's table 0x508ba0 */
static const uint16_t BUTTON_FRAME_TABLE[8] = { 3, 0x53, 0, 0x67, 0x70, 0x79, 0x82, 0x8c };
static const char CONSOLE_LETTERS[4] = { 'z', 't', 'p', 'n' };

int kp_ui_load_console(KpUi *ui, int race)
{
	char path[32];
	const KpUiFile *f;
	int32_t w, h;
	if (race < 0 || race > 3) return -1;
	snprintf(path, sizeof path, "game\\%cconsole.pcx", CONSOLE_LETTERS[race]);
	f = kp_ui_file(ui->pcx, ui->pcx_count, path);
	if (f == NULL || kp_ui_pcx(f->data, f->size, &w, &h, NULL, 0) != 0) return -1;
	free(ui->console.pixels);
	ui->console.pixels = (uint8_t *)malloc((size_t)w * (size_t)h);
	if (ui->console.pixels == NULL) return -1;
	ui->console.w = w;
	ui->console.h = h;
	ui->console_file = f;
	return kp_ui_pcx(f->data, f->size, &w, &h, ui->console.pixels, (size_t)w * (size_t)h);
}

void kp_ui_capture_console(KpUi *ui, int32_t control)
{
	KpUiControl *c = &ui->controls[control];
	KpUiSurface target, *saved = ui->text.target;
	int16_t src[4];
	c->image_w = c->words36[0];
	c->image_h = c->words36[1];
	if (c->owns_image) {
		kp_ui_image_forget(ui, c->image);
		free(c->image);
	}
	c->image = (uint8_t *)calloc((size_t)c->image_w * (size_t)c->image_h + 1, 1);
	c->owns_image = 1;
	c->console_capture = 1;
	if (c->image == NULL || ui->console.pixels == NULL) return;
	target.w = c->image_w;
	target.h = c->image_h;
	target.pixels = c->image;
	ui->text.target = &target;
	src[0] = c->left;
	src[1] = c->top;
	src[2] = (int16_t)(c->left + c->image_w - 1);
	src[3] = (int16_t)(c->top + c->image_h - 1);
	kp_ui_blit_opaque(ui, 0, 0, ui->console.pixels, ui->console.w, ui->console.h, src);
	if (ui->hd && ui->console_file) { /* the HD interface's image (ADR 0013 R4d): a part of the console's group */
		int32_t item[4];
		item[0] = -c->left;
		item[1] = -c->top;
		item[2] = ui->console.w;
		item[3] = ui->console.h;
		kp_ui_image_whole(ui, &target, ui->console_file,
			c->left < KP_UI_CONSOLE_CUT_1 ? 0 : c->left < KP_UI_CONSOLE_CUT_2 ? 1 : 2, item, 0);
	}
	ui->text.target = saved;
}

/* SetControlUserWord 0x46c820. */
void kp_ui_set_user_word(KpUiControl *c, int index, uint32_t flags)
{
	c->wuser = BUTTON_FRAME_TABLE[index];
	c->flags |= flags;
}

/* MenuButtonDraw 0x4a62f0: the theme frame wUser (+1 enabled, +2 pressed), the text offset 3 (0 when pressed). */
static void menu_button_draw(KpUi *ui, KpUiControl *c, int32_t x, int32_t y, const int16_t clip[4])
{
	int frame = (uint8_t)c->wuser;
	int32_t offset;
	(void)x;
	(void)y;
	kp_ui_update_focus(ui, c);
	if (!(c->flags & KP_UI_DISABLED)) frame += (c->flags & KP_UI_PRESSED) ? 2 : 1;
	kp_ui_draw_theme_frame(ui, frame & 0xff, c->left, (uint16_t)c->top, clip, c->flags);
	if (c->text == NULL) return;
	offset = (c->flags & KP_UI_PRESSED) ? 0 : 3;
	kp_ui_draw_control_text(ui, c, offset, offset, NULL, 0x22);
}

/* The menu button's tooltip "Game Menu (F10)" at its right top, once per hover (0x4a6360, 0x4a6480). */
static void menu_tooltip(KpUi *ui, int32_t b)
{
	const KpUiControl *c = &ui->controls[b], *d = &ui->controls[c->parent];
	if (ui->menu_hovered == b && ui->tooltip.visible) return;
	ui->menu_hovered = b;
	kp_ui_tooltip_text(ui, d->left + c->right, d->top + c->top, kp_ui_tbl(ui, KP_UI_STAT_TXT, S_GAME_MENU));
}

/* MenuButtonHoverTooltip 0x4a6360 and MenuDialogHover 0x4a6480: the control under the cursor (none while paused). */
static void menu_hover(KpUi *ui, int32_t dialog, int32_t self, const KpUiEvent *e)
{
	int32_t hit = kp_scalar(ui->game, KP_PAUSED) != 0 ? -1 : kp_ui_hit_test(ui, dialog, e);
	if (self >= 0 ? hit == self : hit >= 0 && hit != dialog) {
		menu_tooltip(ui, hit);
	} else if (self >= 0 || hit != ui->menu_hovered) {
		ui->menu_hovered = -1;
		kp_ui_tooltip_hide(ui);
	}
}

/* MenuButtonInteract 0x4a6500: user event 0 takes wUser = table[2] and the draw procedure 0x4a62f0. */
static int menu_button_interact(KpUi *ui, KpUiControl *c, KpUiEvent *e)
{
	KpUiInteract stock = kp_ui_type_interact(c->type);
	if (e->type == KP_UI_USER && e->code == KP_UI_CREATE) {
		kp_ui_set_user_word(c, 2, 0);
		c->draw = menu_button_draw;
	}
	if (e->type == KP_UI_MOUSE_MOVE) menu_hover(ui, c->parent, (int32_t)(c - ui->controls), e);
	if (e->type == KP_UI_USER && e->code == KP_UI_CLOSE && c->id == MENU_BUTTON_ID) kp_ui_game_menu(ui, KP_UI_GAME_MENU);
	return stock(ui, c, e);
}

/* MenuDialogInteract 0x4a63e0. */
static int menu_dialog_interact(KpUi *ui, KpUiControl *d, KpUiEvent *e)
{
	static const KpUiInteract CHILDREN[1] = { menu_button_interact };
	int32_t self = (int32_t)(d - ui->controls);
	if (e->type == KP_UI_KEY_REPEAT) return 0;
	if (e->type == KP_UI_USER && e->code == KP_UI_CREATE) {
		d->flags |= MENU_DIALOG_FLAG;
		kp_ui_bind_interacts(ui, self, CHILDREN, 1);
		kp_ui_capture_console(ui, self);
	}
	if (e->type == KP_UI_USER && e->code == KP_UI_HIT_TEST) return 1;
	if (e->type == KP_UI_MOUSE_MOVE) menu_hover(ui, self, -1, e);
	/* IsGamePaused 0x47cb60 ? ModelessDialogDefaultProc 0x46eda0 : DialogDefaultInteract: a paused game's keys,
	 * characters and mouse stop here */
	if (ui->game != NULL && kp_scalar(ui->game, KP_PAUSED) != 0) {
		switch (e->type) {
		case KP_UI_KEY_DOWN: case KP_UI_KEY_REPEAT: case KP_UI_MOUSE_MOVE: case KP_UI_LEFT_DOWN: case KP_UI_LEFT_UP:
		case KP_UI_LEFT_DOUBLE: case KP_UI_RIGHT_DOWN: case KP_UI_RIGHT_UP: case KP_UI_RIGHT_DOUBLE: case KP_UI_CHAR:
			(void)kp_ui_type_interact(0)(ui, d, e);
			return 1;
		default:
			break;
		}
	}
	return kp_ui_type_interact(0)(ui, d, e);
}

int32_t kp_ui_create_menu_button(KpUi *ui)
{
	int32_t d = kp_ui_load_dialog(ui, "rez\\stat_f10.bin");
	if (d < 0) return -1;
	kp_ui_init_dialog(ui, d, menu_dialog_interact);
	ui->menu_button = d;
	ui->menu_hovered = -1;
	return d;
}
