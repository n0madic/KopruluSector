/* The fatal error dialog (dlgfatal.cpp, atlas P13 0x43ec00..0x43f090): in a game ShowNetErrorMessage 0x474a70 shows a
 * network report with ShowFatalErrorDialog 0x43ec00, rez\dlgFatal.bin run modally over the stopped game with the
 * report's text (control -10) and its detail (control -20); its OK (-2) closes it. */
#include "kp_ui.h"

#include <stdio.h>

#define ID_OK (-2)
#define ID_TEXT (-10)
#define ID_DETAIL (-20)
#define COLOUR_WHITE 0          /* 0x6d2368 */
#define COLOUR_BLACK 15         /* 0x6d2377 */
#define COLOUR_OUTLINE 16       /* 0x6d2378 */
#define FORMAT_TEXT 0x22        /* the layout byte 0x6d300e */
#define FLAG_BACKDROP 0x10000000u /* a dialog composited onto the backdrop with its flags */

/* DrawRectOutline 0x4ce7d0 */
static void outline(KpUi *ui, int32_t x, int32_t y, int32_t w, int32_t h, uint8_t colour)
{
	kp_ui_fill_rect(ui, x, y, w, 1, colour);
	kp_ui_fill_rect(ui, x, y + h - 1, w, 1, colour);
	kp_ui_fill_rect(ui, x, y, 1, h, colour);
	kp_ui_fill_rect(ui, x + w - 1, y, 1, h, colour);
}

/* The two outlines of a rectangle (left, top, right, bottom inclusive, 16-bit sizes as the original's): around it
 * and one pixel inside. */
static void double_outline(KpUi *ui, int32_t l, int32_t t, int32_t r, int32_t b, uint8_t colour)
{
	outline(ui, l, t, (uint16_t)(r - l + 1), (uint16_t)(b - t + 1), colour);
	outline(ui, l + 1, t + 1, (uint16_t)(r - l - 1), (uint16_t)(b - t - 1), colour);
}

/* DrawFatalDialogBackground 0x43edb0: the clip (or the whole dialog) filled black, in the dialog's own coordinates,
 * then its outlines. */
static void draw_background(KpUi *ui, KpUiControl *d, int32_t x, int32_t y, const int16_t clip[4])
{
	int32_t w = d->right - d->left, h = d->bottom - d->top;
	(void)x;
	(void)y;
	if (clip != NULL) {
		kp_ui_fill_rect(ui, clip[0], clip[1], (uint16_t)(clip[2] - clip[0] + 1), (uint16_t)(clip[3] - clip[1] + 1),
			ui->ui_colours[COLOUR_BLACK]);
	} else {
		kp_ui_fill_rect(ui, 0, 0, (uint16_t)(w + 1), (uint16_t)(h + 1), ui->ui_colours[COLOUR_BLACK]);
	}
	double_outline(ui, 0, 0, w, h, ui->ui_colours[COLOUR_OUTLINE]);
}

/* DrawFatalDialogFrameControl 0x43ed10: OK's outlines, white while pressed, and its text. */
static void draw_ok(KpUi *ui, KpUiControl *c, int32_t x, int32_t y, const int16_t clip[4])
{
	(void)x;
	(void)y;
	(void)clip;
	double_outline(ui, c->left, c->top, c->right, c->bottom,
		ui->ui_colours[(c->flags & KP_UI_PRESSED) ? COLOUR_WHITE : COLOUR_OUTLINE]);
	if (c->text != NULL) kp_ui_draw_control_text(ui, c, 0, 0, NULL, FORMAT_TEXT);
}

/* DrawFatalDialogText 0x43ee70 */
static void draw_text(KpUi *ui, KpUiControl *c, int32_t x, int32_t y, const int16_t clip[4])
{
	(void)x;
	(void)y;
	(void)clip;
	if (c->text != NULL) kp_ui_draw_control_text(ui, c, 0, 0, NULL, FORMAT_TEXT);
}

static void set_text(KpUi *ui, int32_t dialog, int16_t id, const char *text)
{
	int32_t c = kp_ui_find_control(ui, dialog, id);
	if (c < 0) return;
	ui->controls[c].text = (const uint8_t *)text;
	ui->controls[c].draw = text != NULL ? draw_text : ui->controls[c].draw;
}

/* InitFatalDialog 0x43ee90 (the update callback 0x43f090 is UpdateCursor, which the host's frame runs anyway) */
static void init_dialog(KpUi *ui, int32_t dialog)
{
	KpUiControl *d = &ui->controls[dialog];
	int32_t ok = kp_ui_find_control(ui, dialog, ID_OK);
	if (ui->game != NULL) d->flags |= FLAG_BACKDROP;
	d->draw = draw_background;
	if (ok >= 0) ui->controls[ok].draw = draw_ok;
	set_text(ui, dialog, ID_TEXT, ui->fatal.has_text ? ui->fatal.text : NULL);
	set_text(ui, dialog, ID_DETAIL, ui->fatal.has_detail ? ui->fatal.detail : NULL);
	kp_ui_cursor_set(ui, 0);
	ui->cursor_locked = 1; /* LockCursor */
	ui->cursor_hidden = 0; /* ShowCursorLayer 0x43e600 */
	ui->keys.saved_active = ui->keys.active; /* SaveAccelerators 0x43f180, SetDefaultAccelerators 0x43f100 */
	ui->keys.saved_handler = ui->keys.handler;
	ui->keys.active = KP_UI_ACCEL_DEFAULT;
	ui->keys.handler = KP_UI_HANDLER_DEFAULT;
	kp_ui_tooltip_hide(ui);
}

/* DestroyFatalDialog 0x43ef40 (the cursor's clip 0x4ce850 is the host's, which owns the system cursor) */
static void destroy_dialog(KpUi *ui, int32_t dialog)
{
	set_text(ui, dialog, ID_TEXT, NULL);
	set_text(ui, dialog, ID_DETAIL, NULL);
	ui->fatal.has_text = ui->fatal.has_detail = 0;
	ui->cursor_locked = 0; /* UnlockCursor 0x43ea30 */
	ui->keys.active = ui->keys.saved_active; /* RestoreAccelerators 0x43f160 */
	ui->keys.handler = ui->keys.saved_handler;
}

/* FatalDialogEventHandler 0x43efd0: 0 InitFatalDialog, 1 DestroyFatalDialog, 2 the close (CloseDialog 0x4c2b50);
 * 10 clips the system cursor to the dialog (the host's). */
static int fatal_proc(KpUi *ui, KpUiControl *d, KpUiEvent *e)
{
	int32_t self = (int32_t)(d - ui->controls);
	if (e->type == KP_UI_USER) {
		switch (e->code) {
		case KP_UI_CREATE: init_dialog(ui, self); break;
		case KP_UI_DESTROY:
			destroy_dialog(ui, self);
			(void)kp_ui_type_interact(0)(ui, d, e);
			return 1;
		case KP_UI_CLOSE:
			kp_ui_end_dialog(ui, self);
			return 1;
		default: break;
		}
	}
	return kp_ui_type_interact(0)(ui, d, e);
}

/* The modal run's end: a fatal report's exits, as ReportNetError 0x474a00 asks for them once its message returned. */
static void fatal_done(KpUi *ui, int32_t result)
{
	(void)result;
	ui->fatal.dialog = -1;
	if (!ui->fatal.exit) return;
	ui->fatal.exit = 0;
	ui->game_exit = KP_UI_EXIT_QUIT_MISSION; /* RequestGameExit 0x444e00(4) */
	ui->menus.glue_exit = 1;                 /* RequestGlueExit 0x457cf0 */
}

int32_t kp_ui_fatal_open(KpUi *ui, const uint8_t *text, const char *detail, int exit)
{
	/* ShowFatalErrorDialog 0x43ec00 (its black-screen byte 0x50f0f0 is the original's DirectDraw fade, none here):
	 * the placement and the targeting cancelled, the modeless dialog closed, a fatal dialog open before destroyed,
	 * the strings copied */
	int32_t d;
	int pending = ui->fatal.exit; /* an open one's exits wait for this one's end */
	kp_ui_end_placement(ui);
	if (ui->game != NULL && ui->input.targeting) kp_ui_cancel_targeting(ui);
	kp_ui_close_modeless(ui, 1);
	ui->fatal.exit = 0;
	if (ui->fatal.dialog >= 0) kp_ui_end_dialog(ui, ui->fatal.dialog);
	ui->fatal.has_text = text != NULL;
	ui->fatal.has_detail = detail != NULL;
	snprintf(ui->fatal.text, sizeof ui->fatal.text, "%s", text != NULL ? (const char *)text : "");
	snprintf(ui->fatal.detail, sizeof ui->fatal.detail, "%s", detail != NULL ? detail : "");
	d = kp_ui_modal(ui, "rez\\dlgFatal.bin", fatal_proc, fatal_done);
	ui->fatal.dialog = d;
	ui->fatal.exit = d >= 0 && (exit || pending);
	if (d < 0 && pending) fatal_done(ui, 0);
	return d;
}

int kp_ui_fatal_shown(const KpUi *ui) { return ui->fatal.dialog >= 0; }
