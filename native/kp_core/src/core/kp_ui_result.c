/* The modeless dialogs over the game (modeless.cpp, atlas P24 0x46ec70..0x46eda0), the in-game dialogs' tile frame and
 * border colour (opt_dlgs.cpp, P27 0x47bb70, 0x47bd40) and the result dialogs (victory.cpp, P41 0x4bcf80..0x4bd400):
 * rez\lmission.bin ends the game with exit code 7 after 20 s or at its OK, rez\wmission.bin with 6 at its OK. */
#include "kp_ui.h"

#include <stdlib.h>
#include <string.h>

#define TILE_GRP "dlgs\\tile.grp"   /* 0x4ef370 */
#define TILE_FRAMES 3                /* a row of the 9-slice frame: the left, the middle, the right piece */
#define BORDER_PERCENT 60            /* SetDialogBorderColor 0x47bd40 */
#define BORDER_FRAME 4               /* the frame whose (0, 0) pixel is the colour that blends */
#define SFX_VICTORY 21
#define SFX_DEFEAT 22
#define DEFEAT_TIMER 0
#define DEFEAT_MS 20000
#define EXIT_VICTORY 6               /* RequestGameExit 0x444e00's codes */
#define EXIT_DEFEAT 7
#define RESULT_DRAW 5                /* KP_EVENT_GAME_RESULT's a */
#define RESULT_DEFEAT 2
#define ID_OK (-2)
#define ID_OPTION 1                  /* lmission: Become Observer; wmission: Continue Playing (multiplayer only) */
#define ID_TEXT (-20)
#define ID_DISCONNECTED (-21)
#define ID_DRAW (-23)
#define MENU_BUTTON_WUSER 0x70       /* 0x508ba0[4] (SetChildMinus2UserWord 0x46c840) and 0x508ba8 (MenuButtonProc) */
#define FLAG_TILED 0x20000000u       /* a button's three-part tiled art (DrawTiledButton 0x4c45a0) */
#define FLAG_BACKDROP 0x10000000u    /* a dialog composited onto the backdrop with its flags */
#define CONTINUE_STRING 0x10         /* network.tbl */
#define TIMED_MESSAGE_MS 7000
#define TIMED_MESSAGE_COLOUR 3
#define TIMED_MESSAGE_SOUND 23

static const KpUiFile *tile_grp(const KpUi *ui) { return kp_ui_file(ui->grps, ui->grp_count, TILE_GRP); }

/* GetGrpPixel 0x47c110: a pixel of an RLE frame, 0 outside it or in a transparent packet. */
static uint8_t grp_pixel(const uint8_t *grp, size_t size, int frame, int32_t x, int32_t y)
{
	const uint8_t *rec, *line;
	uint32_t off;
	if (grp == NULL || 6 + 8 * (size_t)(frame + 1) > size) return 0;
	rec = grp + 6 + 8 * (size_t)frame;
	x -= rec[0];
	y -= rec[1];
	if (x < 0 || y < 0 || x >= rec[2] || y >= rec[3]) return 0;
	off = kp_le_u32(rec + 4) & 0x7fffffffu;
	if ((size_t)off + 2 * (size_t)(y + 1) > size) return 0;
	line = grp + off + kp_le_u16(grp + off + 2 * (size_t)y);
	for (;;) {
		uint8_t b;
		int32_t count;
		if (line >= grp + size) return 0;
		b = *line++;
		count = b & 0x3f;
		if (x < count) {
			if (b & 0x80) return 0;
			if (b & 0x40) return *line;
			return line[x];
		}
		x -= count;
		if (b & 0x80) continue;
		line += (b & 0x40) ? 1 : (size_t)count;
	}
}

/* DrawTileFrameRow 0x47b3c0: frame f at (0, y), f + 1 every tile width while x < width - tile width, f + 2 last. */
static void tile_row(KpUi *ui, const KpUiFile *grp, int32_t width, int32_t y, int frame)
{
	int32_t gw = (int32_t)kp_le_u16(grp->data + 2), gh = (int32_t)kp_le_u16(grp->data + 4), x;
	int32_t crop[4];
	crop[0] = 0;
	crop[1] = 0;
	crop[2] = gw;
	crop[3] = gh;
	kp_ui_draw_grp_rle(ui, 0, y, grp->data, grp->size, frame, crop);
	for (x = gw; x < width - gw; x += gw) kp_ui_draw_grp_rle(ui, x, y, grp->data, grp->size, frame + 1, crop);
	kp_ui_draw_grp_rle(ui, x, y, grp->data, grp->size, frame + 2, crop);
}

void kp_ui_tile_frame(KpUi *ui, int32_t dialog)
{
	/* DrawDialogTileFrame 0x47bb70 (the default GRP): the dialog's size rounded up to whole tiles, its image cleared
	 * and the rows drawn into it */
	KpUiControl *d = &ui->controls[dialog];
	const KpUiFile *grp = tile_grp(ui);
	KpUiSurface image, *saved = ui->text.target;
	int32_t gw, gh, w = d->image_w, h = d->image_h, r, y;
	uint32_t serial;
	if (grp == NULL || grp->size < 6) return;
	gw = (int32_t)kp_le_u16(grp->data + 2);
	gh = (int32_t)kp_le_u16(grp->data + 4);
	if (gw <= 0 || gh <= 0) return;
	if ((r = (int16_t)w % gw) != 0) w = (uint16_t)(w + gw - r);
	if ((r = (int16_t)h % gh) != 0) h = (uint16_t)(h + gh - r);
	if (d->owns_image) {
		kp_ui_image_forget(ui, d->image);
		free(d->image);
	}
	d->image = (uint8_t *)calloc((size_t)w * (size_t)h, 1);
	d->owns_image = d->image != NULL;
	if (d->image == NULL) return;
	d->image_w = (uint16_t)w;
	d->image_h = (uint16_t)h;
	image.pixels = d->image;
	image.w = w;
	image.h = h;
	ui->text.target = &image;
	serial = ui->image_log.serial;
	tile_row(ui, grp, (int16_t)w, 0, 0);
	for (y = gh; y < (int16_t)h - gh; y += gh) tile_row(ui, grp, (int16_t)w, y, TILE_FRAMES);
	tile_row(ui, grp, (int16_t)w, y, 2 * TILE_FRAMES);
	kp_ui_image_tiles(ui, &image, serial, gw, gh); /* the HD interface's images: one per frame */
	ui->text.target = saved;
}

void kp_ui_border_colour(KpUi *ui, int32_t dialog)
{
	/* SetDialogBorderColor 0x47bd40: the dialog is composited translucent onto the backdrop, the tile GRP's centre
	 * colour blending at 60 % (SetSingleColourEffect 0x4ca820, set A) */
	const KpUiFile *grp = tile_grp(ui);
	ui->controls[dialog].flags |= FLAG_BACKDROP | KP_UI_TRANSLUCENT;
	ui->effects.level[0] = kp_ui_effect_level(BORDER_PERCENT);
	ui->effects.colour[0] = grp != NULL ? grp_pixel(grp->data, grp->size, BORDER_FRAME, 0, 0) : 0;
}

/* SetChildMinus2UserWord 0x46c840 (4, 0x20000000): the OK button's tiled art. */
static void tiled_ok(KpUi *ui, int32_t dialog)
{
	int32_t c = kp_ui_find_control(ui, dialog, ID_OK);
	if (c < 0) return;
	ui->controls[c].wuser = MENU_BUTTON_WUSER;
	ui->controls[c].flags |= FLAG_TILED;
}

/* MenuButtonActivateProc 0x46c7a0: the activation goes to the parent with the button's ID as the result. */
int kp_ui_menu_button_activate_proc(KpUi *ui, KpUiControl *c, KpUiEvent *e)
{
	if (e->type == KP_UI_USER && e->code == KP_UI_CLOSE) {
		KpUiControl *d = &ui->controls[c->parent];
		ui->result = c->id;
		if (d->interact) (void)d->interact(ui, d, e);
		return 1;
	}
	return kp_ui_type_interact(c->type)(ui, c, e);
}

/* MenuButtonProc 0x46c7d0: the in-game menus' buttons. */
int kp_ui_menu_button_proc(KpUi *ui, KpUiControl *c, KpUiEvent *e)
{
	if (e->type == KP_UI_USER && e->code == KP_UI_CREATE) {
		c->flags |= FLAG_TILED;
		c->wuser = MENU_BUTTON_WUSER;
		c->interact = kp_ui_menu_button_activate_proc;
	}
	return kp_ui_menu_button_activate_proc(ui, c, e);
}

static void set_visible(KpUi *ui, int32_t dialog, int16_t id, int show)
{
	int32_t c = kp_ui_find_control(ui, dialog, id);
	if (c >= 0) kp_ui_set_visible(ui, c, show);
}

/* --- the modeless dialog --- */

int kp_ui_modeless_default(KpUi *ui, KpUiControl *d, KpUiEvent *e)
{
	/* ModelessDialogDefaultProc 0x46eda0: the keys, the mouse's moves and buttons and the characters stop here */
	int r = kp_ui_type_interact(0)(ui, d, e);
	switch (e->type) {
	case KP_UI_KEY_DOWN: case KP_UI_KEY_REPEAT: case KP_UI_MOUSE_MOVE: case KP_UI_LEFT_DOWN: case KP_UI_LEFT_UP:
	case KP_UI_LEFT_DOUBLE: case KP_UI_RIGHT_DOWN: case KP_UI_RIGHT_UP: case KP_UI_RIGHT_DOUBLE: case KP_UI_CHAR:
		return 1;
	default:
		return r;
	}
}

int32_t kp_ui_open_modeless(KpUi *ui, const char *bin, KpUiInteract proc)
{
	/* OpenModelessDialog 0x46ecc0: a single player's game paused under it, the placement (IsPlacementActive
	 * 0x47d070, EndPlacement 0x47d300) and the targeting (0x4b2900) cancelled */
	int32_t d;
	int k;
	kp_ui_unpause_game(ui, 1);
	kp_ui_close_modeless(ui, 1);
	for (k = 0; k < KP_UI_EVENT_TYPES; k++) {
		if (ui->capture[k] >= 0) kp_ui_set_capture(ui, -1, k); /* ReleaseAllCaptures 0x4c2c40 */
	}
	kp_ui_cancel_drag_box(ui);
	kp_ui_cursor_set(ui, 0);
	kp_ui_pause_game(ui);
	kp_ui_end_placement(ui);
	if (ui->game != NULL && ui->input.targeting) kp_ui_cancel_targeting(ui);
	ui->cursor_locked = 1; /* LockCursor */
	if (ui->game != NULL) { /* SaveAccelerators 0x43f180, SetDefaultAccelerators 0x43f100 */
		ui->keys.saved_active = ui->keys.active;
		ui->keys.saved_handler = ui->keys.handler;
		ui->keys.active = KP_UI_ACCEL_DEFAULT;
		ui->keys.handler = KP_UI_HANDLER_DEFAULT;
	}
	kp_ui_tooltip_hide(ui);
	d = kp_ui_load_dialog(ui, bin);
	if (d < 0) return -1;
	ui->modeless = d;
	kp_ui_init_dialog(ui, d, proc);
	return d;
}

void kp_ui_close_modeless(KpUi *ui, int destroy)
{
	/* CloseModelessDialog 0x46ec70 */
	int32_t d = ui->modeless;
	kp_ui_unpause_game(ui, 1);
	if (d < 0) return;
	ui->modeless = -1;
	if (destroy) {
		kp_ui_destroy_dialog(ui, d);
		ui->abort = 1;
	}
	if (ui->game != NULL) { /* RestoreAccelerators 0x43f160 */
		ui->keys.active = ui->keys.saved_active;
		ui->keys.handler = ui->keys.saved_handler;
	}
	ui->cursor_locked = 0; /* UnlockCursor 0x43ea30 */
}

/* --- the result dialogs --- */

static void end_dialog(KpUi *ui, int32_t dialog)
{
	/* CloseDialog 0x4c2b50: the destroy event, then the controls are freed */
	kp_ui_destroy_dialog(ui, dialog);
	ui->abort = 1;
}

static void init_frame(KpUi *ui, int32_t dialog, uint16_t sfx)
{
	kp_ui_stream_sfx(ui, sfx); /* PlayStreamedSfx 0x49e7c0 */
	kp_ui_tile_frame(ui, dialog);
	kp_ui_border_colour(ui, dialog);
}

static int defeat_proc(KpUi *ui, KpUiControl *d, KpUiEvent *e)
{
	/* DefeatDialogProc 0x4bd170 */
	int32_t self = (int32_t)(d - ui->controls), c;
	if (e->type == KP_UI_USER) {
		switch (e->code) {
		case KP_UI_CREATE: /* InitDefeatDialog 0x4bd060 (single player: the observer's button and the disconnection
		                    * hidden, the defeat's text shown) */
			init_frame(ui, self, SFX_DEFEAT);
			tiled_ok(ui, self);
			if ((c = kp_ui_find_control(ui, self, ID_OPTION)) >= 0) {
				ui->controls[c].interact = kp_ui_menu_button_proc;
				kp_ui_set_visible(ui, c, 0);
			}
			set_visible(ui, self, ID_TEXT, 1);
			set_visible(ui, self, ID_DISCONNECTED, 0);
			(void)kp_ui_set_timer(&ui->timers, self, DEFEAT_TIMER, DEFEAT_MS, 0, ui->now);
			ui->menu_depth = 0;
			break;
		case KP_UI_DESTROY:
			kp_ui_delete_timer(&ui->timers, self, DEFEAT_TIMER);
			kp_ui_close_modeless(ui, 0);
			(void)kp_ui_modeless_default(ui, d, e);
			ui->result_open = 0;
			return 1;
		case KP_UI_CLOSE: /* OnDefeatDialogButton 0x4bd130 */
			if (ui->result == ID_OK) ui->game_exit = EXIT_DEFEAT;
			ui->result_answered = 1;
			break;
		case KP_UI_TIMER:
			if (e->param == DEFEAT_TIMER) { /* the 20 s without an answer */
				ui->game_exit = EXIT_DEFEAT;
				end_dialog(ui, self);
				return 1;
			}
			break;
		default: break;
		}
	}
	return kp_ui_modeless_default(ui, d, e);
}

static int victory_proc(KpUi *ui, KpUiControl *d, KpUiEvent *e)
{
	/* VictoryDialogProc 0x4bd400 */
	int32_t self = (int32_t)(d - ui->controls), c;
	if (e->type == KP_UI_USER) {
		switch (e->code) {
		case KP_UI_CREATE: { /* InitVictoryDialog 0x4bd220 (single player, no replay) */
			int draw = ui->result_kind == RESULT_DRAW;
			init_frame(ui, self, SFX_VICTORY);
			if ((c = kp_ui_find_control(ui, self, ID_OPTION)) >= 0) ui->controls[c].interact = kp_ui_menu_button_proc; /* 0x50e0d4 */
			tiled_ok(ui, self);
			set_visible(ui, self, ID_OPTION, 0);
			set_visible(ui, self, ID_TEXT, !draw);
			set_visible(ui, self, ID_DISCONNECTED, 0);
			set_visible(ui, self, ID_DRAW, draw);
			break;
		}
		case KP_UI_DESTROY:
			kp_ui_close_modeless(ui, 0);
			(void)kp_ui_modeless_default(ui, d, e);
			ui->result_open = 0;
			return 1;
		case KP_UI_CLOSE: /* OnVictoryDialogButton 0x4bd3c0 */
			if (ui->result == ID_OK) {
				ui->game_exit = EXIT_VICTORY;
			} else if (ui->result == ID_OPTION) { /* DisplayTimedMessage 0x46ac90 */
				const uint8_t *text = kp_ui_tbl(ui, KP_UI_NETWORK, CONTINUE_STRING);
				if (text != NULL) {
					kp_ui_lines_add(ui, text, TIMED_MESSAGE_COLOUR, ui->now + TIMED_MESSAGE_MS, 0);
					(void)kp_ui_play_sound(ui, TIMED_MESSAGE_SOUND);
				}
			}
			ui->result_answered = 1;
			break;
		default: break;
		}
	}
	return kp_ui_modeless_default(ui, d, e);
}

int32_t kp_ui_show_result(KpUi *ui, int result)
{
	/* ShowDefeatDialog 0x4bcf80, ShowVictoryDialog 0x4bcff0: once, until it was answered (the game's CancelCursorModes
	 * 0x448c20 already ran in kp_core) */
	if (ui->result_open || ui->result_answered) return -1;
	ui->result_open = 1;
	ui->result = 0;
	ui->result_kind = (uint8_t)result;
	if (ui->input.targeting) kp_ui_cancel_targeting(ui);
	return kp_ui_open_modeless(ui, result == RESULT_DEFEAT ? "rez\\lmission.bin" : "rez\\wmission.bin",
		result == RESULT_DEFEAT ? defeat_proc : victory_proc);
}

void kp_ui_end_game(KpUi *ui)
{
	/* EndGame 0x443db0, the interface's part: the modeless dialog closed (0x46ec70(1)), the streamed sounds stopped
	 * (0x49db50), the console's teardown (FreeConsoleUi 0x4a7bc0), the game's input procedures gone (ResetInputHandlers
	 * 0x43f330: the glue's idle events no longer reach the game's), between them the sounds' and the music's stop
	 * (StopAllSoundsAndMusic 0x49db40, then StopAllSounds 0x49eca0 again): the score screen's track starts anew */
	kp_ui_close_modeless(ui, 1);
	kp_ui_stop_streams(ui);
	kp_ui_hud_free(ui);
	kp_ui_stop_sounds(ui);
	kp_ui_music_stop(ui);
	kp_ui_stop_sounds(ui);
	kp_ui_reset_input(ui);
}
