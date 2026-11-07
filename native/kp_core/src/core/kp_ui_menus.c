/* The glue's loop and its menus (glues.cpp 0x457510; glumain.cpp, glulogin.cpp, glupopup.cpp, the campaign menus
 * gluchatslot.cpp..gluconn.cpp and glues.cpp, glujoin.cpp's load screen; atlas P18..P21, P41): a state's screen runs
 * modally (RunModal 0x4c35c0 as a dialog whose destruction runs its continuation), the popups nest in it (a glue dialog
 * composited into the screen's bitmap, flag 0x8000000), and the screen's end leaves the next state and the host's work
 * (a mission or cinematic to start, a saved game to load, the exit). Multiplayer, the Campaign Editor, Play Custom and
 * Load Replay are outside the product's scope: their buttons have no label and answer nothing. */
#include "kp_text.h"
#include "kp_ui.h"
#include "kp_rng.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define THEME_MAIN 0            /* PalMm */
#define THEME_CAMPAIGN 1        /* PalCs */
#define THEME_NONE 11           /* PalNl */
#define STATE_MAIN 0
#define STATE_LOGIN 5
#define STATE_CAMPAIGN 6
#define STATE_TERRAN_ROOM 7
#define STATE_ZERG_ROOM 8
#define STATE_PROTOSS_ROOM 9
#define STATE_LOAD 13
#define STATE_EXP_CAMPAIGN 22
#define DIALOG_TRANSLUCENT 0x40000u
#define TITLE_MUSIC 22
#define LIST_FLAG 0x20000u
/* the gluAll and network.tbl strings */
#define STR_VERSION_FORMAT 3    /* gluAll "%s" */
#define STR_GAME_TYPE 0xaa      /* gluAll "Select Game Type" */
#define STR_NEW_NAME 23         /* gluAll "Enter new character name:" */
#define STR_DELETE_PROFILE 24
#define STR_DELETE_SAVE 0x16     /* gluAll: the saved game's delete */
#define STR_NAME_EXISTS 25
#define NET_RESERVED 6           /* network.tbl: a reserved file name (IsInvalidFileName 0x408300) */
#define PROFILE_NAME_LIMIT 25     /* CreateNewProfile 0x45a320's limit */
#define STR_TOO_MANY 26
#define STR_FILE_ERROR 27
#define STR_DELETE_SAVE 0x16
#define NET_FORBIDDEN 0x11      /* network.tbl: the characters a name cannot have */
#define NET_RESERVED 6
#define NET_ANONYMOUS 0x48
#define NET_PLAY_ZERG 0x8f
#define NET_PLAY_PROTOSS 0x90
#define NET_ZERG_WARNING 141
#define NET_PROTOSS_WARNING 142
#define FORBIDDEN "*\\/:><|&+^?-"   /* 0x508278 */
#define NAME_MAX 25
#define HINT_TIMER 2
#define HINT_PERIOD 200
#define ANIMATION_TIMER 0x48
#define ANIMATION_PERIOD 30
#define LOGIN_TIMER 0x57
#define LOGIN_PERIOD 50
/* the screens' and popups' results */
#define R_OK 1
#define R_CANCEL 3
#define R_EXIT 2
#define R_SINGLE 3
#define ID_MAIN_MULTI 4         /* the buttons the remake lacks: Multiplayer, Campaign Editor */
#define ID_MAIN_EDITOR 5
#define ID_CAMPAIGN_CUSTOM 10   /* the campaign menus' buttons the remake lacks: Play Custom, Load Replay (results 10 and */
#define ID_CAMPAIGN_REPLAY 11   /* 11, glue states 11 and 12: RunCampaignMenu 0x453f00, 0x458b00) */
#define R_INTRO 8
/* The main menu's version label: the original's is "Version 1.17.0" (0x465960 from its executable's product version),
 * the remake's says what it is a remake of */
#define VERSION_LABEL "Remake based on the original version %s"
#define ORIGINAL_VERSION "1.17.0"
#define R_CREDITS 9
#define R_ORIGINAL 6
#define R_EXPANSION 7
#define R_LOGIN_OK 4
#define R_LOGIN_CANCEL 5
#define R_NEW_ID 6
#define R_DELETE 7
#define R_LOAD_OK 4
#define R_LOAD_CANCEL 5
#define R_LOAD_DELETE 7
#define ID_LOGIN_OK 4
#define ID_LOGIN_DELETE 7
#define ID_LOGIN_LIST 8
#define ID_POPUP_TEXT 2
#define ID_POPUP_TEXT2 5
#define ID_EDIT 4
#define ID_HISTORY_LIST 6
#define ID_LOAD_LIST 6
#define ID_LOAD_OK 4
#define ID_LOAD_DELETE 7
#define ID_MAIN_VERSION 10
#define ID_EPISODE_FIRST 6      /* 6..8: the episode buttons */

static KpUiMenus *mn(KpUi *ui) { return &ui->menus; }

/* --- the modal runs --- */

int32_t kp_ui_modal(KpUi *ui, const char *bin, KpUiInteract proc, KpUiModalDone done)
{
	/* RunModal 0x4c35c0: the dialog's procedure runs until the dialog ends; the continuation then */
	KpUiMenus *m = mn(ui);
	int32_t d;
	if (m->modal_count >= KP_UI_MODALS) return -1;
	d = kp_ui_load_dialog(ui, bin);
	if (d < 0) return -1;
	m->modal[m->modal_count].dialog = d;
	m->modal[m->modal_count].done = done;
	m->modal[m->modal_count].suspended = ui->timers.in_pass > 0; /* opened by a timer's work: its pass waits */
	if (ui->timers.in_pass > 0) ui->timers.suspend = 1;
	m->modal_count++;
	kp_ui_init_dialog(ui, d, proc);
	return d;
}

void kp_ui_modal_destroyed(KpUi *ui, int32_t dialog)
{
	KpUiMenus *m = mn(ui);
	int k;
	for (k = m->modal_count - 1; k >= 0; k--) {
		if (m->modal[k].dialog == dialog) {
			KpUiModalDone done = m->modal[k].done;
			int suspended = m->modal[k].suspended;
			memmove(&m->modal[k], &m->modal[k + 1], sizeof m->modal[0] * (size_t)(m->modal_count - k - 1));
			m->modal_count--;
			if (done != NULL) done(ui, ui->result);
			if (suspended) kp_ui_resume_pass(ui); /* the timer's work returns: its pass goes on */
			return;
		}
	}
}

/* EndDialog 0x4c2b50 of a dialog that ends without a slide: its rectangle is repainted (a popup's was composited into
 * the screen below it), its destruction now. */
static void end_dialog(KpUi *ui, int32_t dialog)
{
	kp_ui_close_dialog(ui, dialog);
	kp_ui_destroy_dialog(ui, dialog);
}

/* --- the popups (glupopup.cpp, P21) --- */

static const char *theme_dir(int theme)
{
	static const char *const DIRS[12] = { "PalMm", "PalCs", "PalRt", "PalRz", "PalRp", "PalPd", "PalPv", "PalZd",
		"PalZv", "PalTd", "PalTv", "PalNl" };
	return DIRS[theme >= 0 && theme < 12 ? theme : THEME_NONE];
}

static int state_theme(int state)
{
	/* the glue state table 0x4ed820's themes */
	static const uint8_t THEMES[26] = { 0, 11, 11, 11, 12, 11, 1, 2, 3, 4, 11, 11, 11, 11, 7, 8, 9, 10, 5, 6, 11, 11,
		1, 11, 11, 0 };
	return state >= 0 && state < 26 ? THEMES[state] : THEME_NONE;
}

/* The popup dialog's image (0x66b3f8) from a glue file. */
static void popup_image(KpUi *ui, const char *path)
{
	KpUiMenus *m = mn(ui);
	const KpUiFile *f;
	int32_t w, h;
	kp_ui_surface_free(ui, m->popup_image.pixels);
	m->popup_image.pixels = NULL;
	if ((f = kp_ui_glue_file(ui, path)) == NULL || kp_ui_pcx(f->data, f->size, &w, &h, NULL, 0) != 0) return;
	m->popup_image.pixels = (uint8_t *)malloc((size_t)w * (size_t)h);
	if (m->popup_image.pixels == NULL || kp_ui_pcx(f->data, f->size, &w, &h, m->popup_image.pixels,
		(size_t)w * (size_t)h) != 0) {
		free(m->popup_image.pixels);
		m->popup_image.pixels = NULL;
		return;
	}
	m->popup_image.w = w;
	kp_ui_picture(ui, m->popup_image.pixels, w, h, f); /* the HD interface's (ADR 0013 R4d) */
	m->popup_image.h = h;
}

/* The popup's background <theme dir>\<name>. */
static void popup_background(KpUi *ui, const char *name)
{
	char path[64];
	snprintf(path, sizeof path, "glue\\%s\\%s", theme_dir(state_theme(mn(ui)->state)), name);
	popup_image(ui, path);
}

static void close_popup(KpUi *ui)
{
	/* CloseGluePopup 0x45fd20 */
	KpUiMenus *m = mn(ui);
	if (m->popup < 0) return;
	ui->result = R_CANCEL;
	end_dialog(ui, m->popup);
}

/* FitPopupTextFont 0x4605e0. */
static void fit_font(KpUi *ui, KpUiControl *c)
{
	int32_t w, h10;
	if (c->text == NULL) return;
	w = c->right - c->left + 1;
	if (w == 0) w = 1;
	h10 = (c->bottom - c->top + 1) * 10;
	for (;;) {
		uint32_t f = c->flags & 0x4c00u;
		int32_t line;
		kp_ui_select_control_font(ui, c);
		line = ui->text.font != NULL ? ui->text.max_height : 1; /* 0x6d3037 */
		if (kp_ui_text_width(&ui->text, c->text) * 10 / w + 10 < h10 / (line + 1)) break;
		c->flags &= ~0x4c00u;
		if (f == 0x4000u) c->flags |= 0x800u;
		else if (f == 0x800u) continue;
		else if (f == 0 || f == 0x400u) {
			c->flags |= 0x400u;
			break;
		}
	}
	ui->text.font = NULL;
}

static void draw_popup_text(KpUi *ui, KpUiControl *c, int32_t x, int32_t y, const int16_t clip[4])
{
	/* DrawPopupText 0x4605c0 */
	(void)x;
	(void)y;
	(void)clip;
	if (c->text != NULL) kp_ui_draw_control_text(ui, c, 0, 0, NULL, 0x18);
}

static void popup_texts(KpUi *ui, int32_t dialog)
{
	/* SetPopupTexts 0x4606d0 */
	KpUiMenus *m = mn(ui);
	int32_t c = kp_ui_find_control(ui, dialog, ID_POPUP_TEXT);
	if (c >= 0) {
		ui->controls[c].text = (const uint8_t *)m->popup_text;
		fit_font(ui, &ui->controls[c]);
		ui->controls[c].draw = draw_popup_text;
		kp_ui_mark_dirty(ui, c);
	}
	if (m->popup_text2[0] != 0 && (c = kp_ui_find_control(ui, dialog, ID_POPUP_TEXT2)) >= 0) {
		ui->controls[c].text = (const uint8_t *)m->popup_text2;
		fit_font(ui, &ui->controls[c]);
		kp_ui_mark_dirty(ui, c);
	}
}

/* The popups' procedure arrays (0x508288, 0x5082b0, 0x5082a4, 0x5082b8, 0x5082dc). */
static KpUiInteract popup_procs[8];
static int popup_proc_count;

static void popup_post_load(KpUi *ui, int32_t dialog)
{
	/* InitGluePopupDialog 0x460580 */
	KpUiMenus *m = mn(ui);
	KpUiControl *d = &ui->controls[dialog];
	kp_ui_glue_init_dialog(ui, dialog, popup_procs, popup_proc_count, state_theme(m->state));
	d->flags |= DIALOG_TRANSLUCENT;
	if (m->popup_image.pixels != NULL) {
		kp_ui_drop_image(ui, d);
		d->image = m->popup_image.pixels; /* the popup's own now */
		d->image_w = (uint16_t)m->popup_image.w;
		d->image_h = (uint16_t)m->popup_image.h;
		d->owns_image = 1;
		m->popup_image.pixels = NULL;
	}
	kp_ui_mark_dirty(ui, dialog);
}

static void centre(KpUiControl *d)
{
	/* CenterGlueDialog 0x46c8a0 (the right and bottom still hold the size) */
	d->left = (int16_t)((KP_UI_SCREEN_W - d->right) / 2);
	d->top = (int16_t)((KP_UI_SCREEN_H - d->bottom) / 2);
}

static int popup_proc(KpUi *ui, KpUiControl *d, KpUiEvent *e)
{
	/* GluePopupProc 0x460740 (the dialog ends at a button's activation with its ID) */
	KpUiMenus *m = mn(ui);
	int32_t self = (int32_t)(d - ui->controls);
	KpUiInteract base = kp_ui_type_interact(0);
	if (e->type == KP_UI_USER) {
		switch (e->code) {
		case KP_UI_CREATE: popup_texts(ui, self); break;
		case KP_UI_DESTROY:
			kp_ui_glue_release_dialog(ui, self);
			m->popup = -1;
			break;
		case KP_UI_RECENTRE: centre(d); break;
		case KP_UI_POST_LOAD: popup_post_load(ui, self); break;
		case KP_UI_CLOSE:
			(void)base(ui, d, e);
			end_dialog(ui, self);
			return 1;
		default: break;
		}
	}
	return base(ui, d, e);
}

/* The popup's dialog run as a modal over the screen (its image loaded before). */
static int32_t run_popup(KpUi *ui, const char *bin, const KpUiInteract *procs, int count,
	KpUiInteract proc, KpUiModalDone done)
{
	KpUiMenus *m = mn(ui);
	int32_t d;
	memcpy(popup_procs, procs, sizeof procs[0] * (size_t)count);
	popup_proc_count = count;
	d = kp_ui_modal(ui, bin, proc, done);
	m->popup = d;
	return d;
}

static void ok_popup(KpUi *ui, const uint8_t *text, KpUiModalDone done)
{
	/* ShowOkPopup 0x460230 */
	KpUiMenus *m = mn(ui);
	static const KpUiInteract PROCS[2] = { kp_ui_text_button_proc, NULL };
	close_popup(ui);
	snprintf(m->popup_text, sizeof m->popup_text, "%s", text != NULL ? (const char *)text : "");
	m->popup_text2[0] = 0;
	popup_background(ui, "pOPopup.pcx");
	(void)run_popup(ui, "rez\\gluPOk.bin", PROCS, 2, popup_proc, done);
}

static void ok_cancel_popup(KpUi *ui, const uint8_t *text, KpUiModalDone done)
{
	/* ShowOkCancelPopup 0x460330 */
	KpUiMenus *m = mn(ui);
	static const KpUiInteract PROCS[3] = { kp_ui_text_button_proc, NULL, kp_ui_text_button_proc };
	close_popup(ui);
	snprintf(m->popup_text, sizeof m->popup_text, "%s", text != NULL ? (const char *)text : "");
	m->popup_text2[0] = 0;
	popup_background(ui, "pDPopup.pcx");
	(void)run_popup(ui, "rez\\gluPOkCancel.bin", PROCS, 3, popup_proc, done);
}

/* --- the edit popup (RunEditPopup 0x45fd50) --- */

static int forbidden(const KpUiMenus *m, uint16_t key)
{
	if (key == 0 || key >= KP_TEXT_ASCII_END) return 0; /* the lists are ASCII: strchr takes a char */
	return strchr(FORBIDDEN, (int)key) != NULL || (m->edit_extra != NULL && strchr((const char *)m->edit_extra, (int)key) != NULL);
}

static void edit_ok_state(KpUi *ui, KpUiControl *edit)
{
	int32_t ok = kp_ui_find_control(ui, edit->parent, R_OK);
	if (ok < 0) return;
	if (edit->text == NULL || edit->text[0] == 0) ui->controls[ok].flags |= KP_UI_DISABLED;
	else ui->controls[ok].flags &= ~KP_UI_DISABLED;
	kp_ui_mark_dirty(ui, ok);
}

static int edit_interact(KpUi *ui, KpUiControl *c, KpUiEvent *e)
{
	/* EditPopupEditInteract 0x45fcc0 with FilterEditPopupChar 0x45fb00 */
	KpUiMenus *m = mn(ui);
	KpUiInteract base = kp_ui_type_interact(8);
	if (e->type == KP_UI_KEY_DOWN || e->type == KP_UI_KEY_REPEAT) {
		(void)base(ui, c, e);
		edit_ok_state(ui, c);
		return 1;
	}
	if (e->type == KP_UI_CHAR) {
		size_t n;
		if (forbidden(m, e->key)) return 1;
		if (c->wuser != 0) {
			kp_ui_edit_set_text(ui, (int32_t)(c - ui->controls), (const uint8_t *)"");
			c->wuser = 0;
		}
		n = c->text != NULL ? kp_text_chars(c->text) : 0;
		if ((int64_t)n < (int64_t)c->user - 1 || e->key == 8) {
			if (!(e->key == ' ' && n == 0)) (void)base(ui, c, e);
		}
		edit_ok_state(ui, c);
		return 1;
	}
	return base(ui, c, e);
}

static int edit_popup_proc(KpUi *ui, KpUiControl *d, KpUiEvent *e)
{
	/* EditPopupProc 0x45fc00 */
	KpUiMenus *m = mn(ui);
	int32_t self = (int32_t)(d - ui->controls), edit;
	if (e->type == KP_UI_USER) {
		switch (e->code) {
		case KP_UI_CREATE: /* InitEditPopup 0x45fa70: the prompt keeps its control's font and drawing */
			if ((edit = kp_ui_find_control(ui, self, ID_POPUP_TEXT)) >= 0) {
				ui->controls[edit].text = (const uint8_t *)m->popup_text;
				kp_ui_mark_dirty(ui, edit);
			}
			if ((edit = kp_ui_find_control(ui, self, ID_EDIT)) >= 0) {
				KpUiControl *c = &ui->controls[edit];
				c->flags |= LIST_FLAG;
				kp_ui_edit_set_text(ui, edit, (const uint8_t *)m->edit_text);
				if (m->edit_text[0] != 0) c->wuser = 1;
				edit_ok_state(ui, c);
				kp_ui_focus_control(ui, edit);
				kp_ui_mark_dirty(ui, edit);
			}
			return kp_ui_type_interact(0)(ui, d, e);
		case KP_UI_DESTROY: /* EditPopupDestroy 0x45fc52 */
			if ((edit = kp_ui_find_control(ui, self, ID_EDIT)) >= 0 && ui->controls[edit].text != NULL) {
				snprintf(m->edit_text, sizeof m->edit_text, "%s", (const char *)ui->controls[edit].text);
			}
			break;
		default: break;
		}
	}
	return popup_proc(ui, d, e);
}

static void edit_popup(KpUi *ui, const uint8_t *prompt, const char *text, int max, const uint8_t *extra,
	KpUiModalDone done)
{
	KpUiMenus *m = mn(ui);
	static const KpUiInteract PROCS[4] = { kp_ui_text_button_proc, NULL, kp_ui_text_button_proc, edit_interact };
	int32_t d, edit;
	close_popup(ui);
	snprintf(m->popup_text, sizeof m->popup_text, "%s", prompt != NULL ? (const char *)prompt : "");
	snprintf(m->edit_text, sizeof m->edit_text, "%s", text);
	m->edit_extra = extra;
	m->edit_max = max;
	popup_background(ui, "pEPopup.pcx");
	memcpy(popup_procs, PROCS, sizeof PROCS);
	popup_proc_count = 4;
	d = kp_ui_load_dialog(ui, "rez\\gluPEdit.bin");
	if (d < 0) return;
	if ((edit = kp_ui_find_control(ui, d, ID_EDIT)) >= 0) ui->controls[edit].user = (uint32_t)max;
	m->modal[m->modal_count].dialog = d;
	m->modal[m->modal_count].done = done;
	m->modal[m->modal_count].suspended = ui->timers.in_pass > 0;
	if (ui->timers.in_pass > 0) ui->timers.suspend = 1;
	m->modal_count++;
	m->popup = d;
	kp_ui_init_dialog(ui, d, edit_popup_proc);
}

/* --- the font resources' keys (grid.cpp, cimage.cpp; atlas P22, P04) --- */

#define FONT_KEY_SEED 0x150b    /* MakeFontGidKey 0x448f40 */
#define FONT_KEY_BYTES 19       /* n = 20: 19 nonzero bytes and the NUL */
#define RESOURCE_KEY_SEED 0x4fa7 /* PrepareResourceKey 0x411c40 */
#define RESOURCE_KEY_DRAWS 112

static void time_reseed(KpUi *ui, uint32_t seed)
{
	/* srand(time(NULL)) */
	kp_rng_crt_init(&ui->glue.crt, seed);
}

static void font_key_draws(KpUi *ui, int decodes)
{
	/* DecodeSpawnResources 0x4635d0's decodes: each builds the fixed key (srand(0x150b), the low bytes of rand() until
	 * 19 are nonzero, srand(time(NULL))) and prepares it for DecryptResource 0x4119f0 (srand(0x4fa7), 112 draws,
	 * srand(time(0))); the menus' CRT generator is reseeded with the clock */
	KpUiMenus *m = mn(ui);
	uint32_t seed = m->time_seed;
	int k, n;
	if (m->time_seeds != NULL && m->time_seed_next < m->time_seed_count) seed = m->time_seeds[m->time_seed_next++];
	for (k = 0; k < decodes; k++) {
		kp_rng_crt_init(&ui->glue.crt, FONT_KEY_SEED);
		for (n = 0; n < FONT_KEY_BYTES;) {
			if ((kp_ui_glue_rand(ui) & 0xff) != 0) n++;
		}
		time_reseed(ui, seed);
		kp_rng_crt_init(&ui->glue.crt, RESOURCE_KEY_SEED);
		for (n = 0; n < RESOURCE_KEY_DRAWS; n++) (void)kp_ui_glue_rand(ui);
		time_reseed(ui, seed);
	}
}

/* --- the screens' common end --- */

static void screen_ended(KpUi *ui, int next, int action)
{
	/* EndGlueScreen 0x457b50 after the state's handler */
	KpUiMenus *m = mn(ui);
	m->state = next;
	m->action = action;
	m->screen = -1;
	kp_ui_glue_end(ui, state_theme(next));
}

/* --- the main menu (glue state 0) --- */

static void main_done(KpUi *ui, int32_t r)
{
	/* RunMainMenu 0x45a8b0's dispatch */
	KpUiMenus *m = mn(ui);
	uint16_t u = (uint16_t)r;
	if (u > 0xfff0) screen_ended(ui, STATE_MAIN, KP_UI_MENU_STATE);
	else if (u == 0xfff0) screen_ended(ui, m->state, KP_UI_MENU_STATE);
	else if (r == R_EXIT) screen_ended(ui, m->state, KP_UI_MENU_EXIT);
	else if (r == R_SINGLE) {
		m->multiplayer = 0;
		screen_ended(ui, STATE_LOGIN, KP_UI_MENU_STATE);
	} else if (r == R_INTRO) screen_ended(ui, m->state, KP_UI_MENU_INTRO);
	else if (r == R_CREDITS) screen_ended(ui, m->state, KP_UI_MENU_CREDITS);
	else screen_ended(ui, STATE_MAIN, KP_UI_MENU_STATE);
}

static int32_t saved_result;

static void game_mode_done(KpUi *ui, int32_t r)
{
	/* ChooseGameMode 0x45a750 offline (the CDs are present) */
	KpUiMenus *m = mn(ui);
	int proceed = 0;
	if ((r & 0xff) == R_ORIGINAL) {
		m->expansion = 0;
		proceed = 1;
	} else if ((r & 0xff) == R_EXPANSION) {
		m->expansion = 1;
		proceed = 1;
	}
	ui->result = saved_result;
	if (proceed && m->screen >= 0) kp_ui_glue_slide_out(ui, m->screen);
}

static void game_mode_popup(KpUi *ui)
{
	/* RunGameModePopup 0x45fff0 */
	KpUiMenus *m = mn(ui);
	static const KpUiInteract PROCS[7] = { NULL, NULL, kp_ui_text_button_proc, NULL, NULL, kp_ui_text_button_proc,
		kp_ui_text_button_proc };
	KpUiInteract procs[7];
	memcpy(procs, PROCS, sizeof PROCS);
	procs[1] = procs[4] = kp_ui_type_interact(10);
	saved_result = ui->result;
	close_popup(ui);
	snprintf(m->popup_text, sizeof m->popup_text, "%s", (const char *)kp_ui_tbl(ui, KP_UI_GLUALL, STR_GAME_TYPE));
	m->popup_text2[0] = 0;
	popup_background(ui, "retail_ex.pcx");
	(void)run_popup(ui, "rez\\gluGameMode.bin", procs, 7, popup_proc, game_mode_done);
}

static void version_label(KpUi *ui, int32_t dialog)
{
	/* SetMainMenuVersionLabel 0x45ab00 (not spawned: gluAll 3 "%s" of the version) */
	KpUiMenus *m = mn(ui);
	int32_t c = kp_ui_find_control(ui, dialog, ID_MAIN_VERSION);
	if (c < 0) return;
	font_key_draws(ui, 1); /* the spawner's name from font\font.clh: none on a full installation */
	ui->controls[c].text = (const uint8_t *)m->version;
	kp_ui_mark_dirty(ui, c);
}

/* A button of a feature outside V1 (Multiplayer, Campaign Editor, Play Custom, Load Replay): a click, its activation
 * and the hover sound do nothing (with no label it has no hotkey either); an animated one keeps its animation and hover
 * fade. */
static int unsupported_event(const KpUiEvent *e)
{
	switch (e->type) {
	case KP_UI_LEFT_DOWN: case KP_UI_LEFT_DOUBLE: case KP_UI_LEFT_UP: return 1;
	case KP_UI_USER: return e->code == KP_UI_ACTIVATE || e->code == KP_UI_MOUSE_ENTER;
	default: return 0;
	}
}

static int unsupported_button_proc(KpUi *ui, KpUiControl *c, KpUiEvent *e)
{
	return unsupported_event(e) ? 1 : kp_ui_flc_button_proc(ui, c, e);
}

static int unsupported_text_button_proc(KpUi *ui, KpUiControl *c, KpUiEvent *e)
{
	return unsupported_event(e) ? 1 : kp_ui_text_button_proc(ui, c, e);
}

static void hide_unsupported_labels(KpUi *ui, int32_t dialog, const int16_t *ids, int count)
{
	int k;
	for (k = 0; k < count; k++) {
		int32_t c = kp_ui_find_control(ui, dialog, ids[k]);
		if (c < 0) continue;
		ui->controls[c].text = NULL;
		kp_ui_mark_dirty(ui, c);
	}
}

static int main_proc(KpUi *ui, KpUiControl *d, KpUiEvent *e)
{
	/* MainMenuDialogProc 0x45ac60 */
	KpUiMenus *m = mn(ui);
	int32_t self = (int32_t)(d - ui->controls);
	KpUiInteract base = kp_ui_type_interact(0);
	if (e->type == KP_UI_CHAR && e->key == ' ') return 1;
	if (e->type != KP_UI_USER) return base(ui, d, e);
	switch (e->code) {
	case KP_UI_CREATE: {
		static const int16_t UNSUPPORTED[2] = { ID_MAIN_MULTI, ID_MAIN_EDITOR };
		version_label(ui, self);
		hide_unsupported_labels(ui, self, UNSUPPORTED, 2);
		kp_ui_glue_activate(ui, self, THEME_MAIN);
		(void)base(ui, d, e);
		if (!ui->music.playing) kp_ui_music_play(ui, TITLE_MUSIC);
		return 1;
	}
	case KP_UI_POST_LOAD: {
		KpUiInteract procs[11];
		int k;
		memset(procs, 0, sizeof procs);
		for (k = 1; k <= 4; k++) procs[k] = kp_ui_flc_button_proc; /* 0x5081d0 */
		procs[ID_MAIN_MULTI - 1] = procs[ID_MAIN_EDITOR - 1] = unsupported_button_proc;
		procs[7] = procs[8] = kp_ui_text_button_proc;
		procs[9] = kp_ui_type_interact(10);
		procs[10] = kp_ui_flc_button_proc;
		kp_ui_glue_init_dialog(ui, self, procs, 11, THEME_MAIN);
		break;
	}
	case KP_UI_CLOSE:
		switch (ui->result) {
		case R_SINGLE: game_mode_popup(ui); return 1; /* the expansion's data are present */
		default: kp_ui_glue_slide_out(ui, self); return 1;
		}
	default: break;
	}
	(void)m;
	return base(ui, d, e);
}

/* --- the glue's loop --- */

static int32_t open_screen(KpUi *ui, const char *bin, KpUiInteract proc, KpUiModalDone done)
{
	KpUiMenus *m = mn(ui);
	kp_ui_glue_sounds(ui);
	m->screen = kp_ui_modal(ui, bin, proc, done);
	return m->screen;
}

int32_t kp_ui_login_open(KpUi *ui);
int32_t kp_ui_campaign_open(KpUi *ui, int expansion);
int32_t kp_ui_load_open(KpUi *ui);

static const uint8_t *campaign_table(KpUi *ui, int expansion, int race, int *count);
static uint16_t row16(const uint8_t *row, int at);
static void font_key_draws(KpUi *ui, int decodes);

int kp_ui_warp_mission(KpUi *ui, const char *text, int race, int base, int expansion, uint16_t *mission)
{
	/* ParseMissionWarpNumber 0x4bbb60: the n-th row (from the base) neither a cinematic nor hidden, then as many hidden
	 * rows after it as the letter after the number counts from 'a'; the original's 32-bit parse (0x4d0c27) takes a
	 * number past 32 bits or a negative one to no row */
	char *end = NULL;
	unsigned long long v = strtoull(text, &end, 10);
	int32_t n = (int32_t)((uint32_t)v - (uint32_t)base);
	int count = 0, k = 0;
	const uint8_t *table = campaign_table(ui, expansion, race, &count);
	if (v > UINT32_MAX || n < 0 || table == NULL) return 0;
	for (;; k++) {
		if (k >= count || row16(table + 8 * k, 2) == 0) return 0;
		if (row16(table + 8 * k, 4) == 0 && table[8 * k + 7] == 0 && n-- == 0) break;
	}
	if (end != NULL && *end != 0) {
		int letter = tolower((unsigned char)*end) - 'a';
		while (letter != 0) {
			letter--;
			if (row16(table + 8 * k, 2) == 0 || ++k >= count || table[8 * k + 7] == 0) return 0;
		}
		if (row16(table + 8 * k, 2) == 0) return 0;
	}
	*mission = row16(table + 8 * k, 2);
	return 1;
}

void kp_ui_set_pending_mission(KpUi *ui, uint16_t mission, int race, int expansion)
{
	/* SetPendingGlueMission 0x457e10 */
	KpUiMenus *m = mn(ui);
	m->warp.pending = 1;
	m->warp.mission = mission;
	m->warp.race = race;
	m->warp.expansion = expansion;
}

/* RunGlueLoop 0x457510's entry with a pending mission: its ready room by the race (CreateCampaignMissionGame 0x4bb6c0;
 * the host makes the game), or the state the host asked for when there is no campaign. */
static int pending_mission(KpUi *ui)
{
	static const int ROOMS[3] = { STATE_ZERG_ROOM, STATE_TERRAN_ROOM, STATE_PROTOSS_ROOM };
	KpUiMenus *m = mn(ui);
	if (!m->warp.pending || m->multiplayer) return 0;
	m->warp.pending = 0;
	m->expansion = m->warp.expansion != 0;
	if (m->warp.race < 0 || m->warp.race > 2 || m->campaign == NULL) {
		m->expansion = 0;
		return 0;
	}
	font_key_draws(ui, 2); /* CreateCampaignMissionGame 0x4bb6c0: FillProgramInfo 0x46f330's key and name */
	m->state = ROOMS[m->warp.race];
	m->mission = m->warp.mission;
	m->action = KP_UI_MENU_MISSION;
	return 1;
}

int32_t kp_ui_menus_enter(KpUi *ui, int state)
{
	/* GlueLoop 0x457510's states of the campaign path */
	KpUiMenus *m = mn(ui);
	m->state = state;
	m->action = KP_UI_MENU_NONE;
	m->screen = -1;
	if (m->version[0] == 0) snprintf(m->version, sizeof m->version, VERSION_LABEL, ORIGINAL_VERSION);
	if (pending_mission(ui)) return -1;
	switch (state) {
	case STATE_MAIN:
		m->multiplayer = 0;
		return open_screen(ui, "rez\\gluMain.bin", main_proc, main_done);
	case STATE_LOGIN: return kp_ui_login_open(ui);
	case STATE_CAMPAIGN: return kp_ui_campaign_open(ui, 0);
	case STATE_EXP_CAMPAIGN: return kp_ui_campaign_open(ui, 1);
	case STATE_LOAD: return kp_ui_load_open(ui);
	default: return -1;
	}
}

int kp_ui_menus_poll(KpUi *ui) { return mn(ui)->screen < 0 && mn(ui)->action != KP_UI_MENU_NONE; }

void kp_ui_menus_free(KpUi *ui)
{
	free(mn(ui)->popup_image.pixels);
	mn(ui)->popup_image.pixels = NULL;
}

/* --- the login screen (glue state 5) --- */

static int32_t login_dialog, login_list;

static void login_buttons(KpUi *ui)
{
	/* UpdateLoginButtons 0x45a280 */
	int sel = kp_ui_list_selection(ui, login_list);
	int32_t ok = kp_ui_find_control(ui, login_dialog, ID_LOGIN_OK), del = kp_ui_find_control(ui, login_dialog, ID_LOGIN_DELETE);
	int on = sel != KP_UI_LIST_NONE && mn(ui)->profile_count > 0;
	if (ok >= 0) kp_ui_set_enabled(ui, ok, on);
	if (del >= 0) kp_ui_set_enabled(ui, del, on);
}

static int login_list_proc(KpUi *ui, KpUiControl *c, KpUiEvent *e)
{
	/* LoginListProc 0x45a590 */
	KpUiMenus *m = mn(ui);
	int32_t self = (int32_t)(c - ui->controls);
	int k, r;
	if (e->type == KP_UI_USER) {
		switch (e->code) {
		case KP_UI_CREATE: { /* FillLoginList 0x45a620 */
			int32_t ok = kp_ui_find_control(ui, login_dialog, ID_LOGIN_OK);
			int32_t del = kp_ui_find_control(ui, login_dialog, ID_LOGIN_DELETE);
			if (del >= 0) kp_ui_set_enabled(ui, del, 0);
			if (ok >= 0) kp_ui_set_enabled(ui, ok, 0);
			kp_ui_list_redraw(ui, self, 0);
			for (k = 0; k < m->profile_count; k++) (void)kp_ui_list_add(ui, self, (const uint8_t *)m->profiles[k].name, 0);
			kp_ui_list_redraw(ui, self, 1);
			kp_ui_list_select(ui, self, 0);
			break;
		}
		case KP_UI_DESTROY: kp_ui_list_clear(ui, self); break;
		case KP_UI_POST_LOAD:
			c->flags |= LIST_FLAG;
			login_list = self;
			break;
		case KP_UI_VALUE_CHANGED:
			r = kp_ui_list_proc(ui, c, e);
			login_buttons(ui);
			(void)r;
			return 1;
		default: break;
		}
	}
	return kp_ui_list_proc(ui, c, e);
}

static int profile_index(KpUi *ui, const char *name)
{
	KpUiMenus *m = mn(ui);
	int k;
	for (k = 0; k < m->profile_count; k++) {
		const char *a = m->profiles[k].name, *b = name;
		while (*a && *b && (*a | 0x20) == (*b | 0x20)) a++, b++;
		if (*a == 0 && *b == 0) return k;
	}
	return -1;
}

static void new_profile_done(KpUi *ui, int32_t r)
{
	/* CreateNewProfile 0x45a320 after the edit popup */
	KpUiMenus *m = mn(ui);
	int k, at;
	uint32_t time = 0;
	if (r != R_OK || m->edit_text[0] == 0) return;
	if (kp_ui_invalid_file_name(ui, m->edit_text, PROFILE_NAME_LIMIT, 0)) {
		ok_popup(ui, kp_ui_tbl(ui, KP_UI_NETWORK, NET_RESERVED), NULL);
		return;
	}
	if (profile_index(ui, m->edit_text) >= 0) {
		ok_popup(ui, kp_ui_tbl(ui, KP_UI_GLUALL, STR_NAME_EXISTS), NULL);
		return;
	}
	for (k = 0; k < m->profile_count; k++) if (m->profiles[k].time >= time) time = m->profiles[k].time + 1;
	at = m->profile_count; /* InsertProfileNodeByTime 0x45a180: the newest last */
	memset(&m->profiles[at], 0, sizeof m->profiles[at]);
	snprintf(m->profiles[at].name, sizeof m->profiles[at].name, "%s", m->edit_text);
	m->profiles[at].time = time;
	m->profile_count++;
	m->profiles_changed = 1;
	kp_ui_list_insert(ui, login_list, at, (const uint8_t *)m->profiles[at].name, 0);
	kp_ui_list_select(ui, login_list, at);
	{
		int32_t ok = kp_ui_find_control(ui, login_dialog, ID_LOGIN_OK);
		int32_t del = kp_ui_find_control(ui, login_dialog, ID_LOGIN_DELETE);
		if (del >= 0) kp_ui_set_enabled(ui, del, 1);
		if (ok >= 0) kp_ui_set_enabled(ui, ok, 1);
	}
}

static void new_profile(KpUi *ui)
{
	KpUiMenus *m = mn(ui);
	if (kp_ui_list_count(ui, login_list) >= KP_UI_PROFILES) {
		ok_popup(ui, kp_ui_tbl(ui, KP_UI_GLUALL, STR_TOO_MANY), NULL);
		return;
	}
	edit_popup(ui, kp_ui_tbl(ui, KP_UI_GLUALL, STR_NEW_NAME), "", NAME_MAX, kp_ui_tbl(ui, KP_UI_NETWORK, NET_FORBIDDEN),
		new_profile_done);
	(void)m;
}

static void delete_profile_done(KpUi *ui, int32_t r)
{
	/* DeleteSelectedProfile 0x45a4b0 after its popup: DeleteCharacterAndSaves 0x4baf70 deletes the profile's saved
	 * games (the host's), then its record */
	KpUiMenus *m = mn(ui);
	int s = kp_ui_list_selection(ui, login_list);
	if (r != R_OK || s == KP_UI_LIST_NONE || s >= m->profile_count) return;
	if (ui->delete_profile_saves) ui->delete_profile_saves(ui->host, m->profiles[s].name);
	kp_ui_list_delete(ui, login_list, s);
	memmove(&m->profiles[s], &m->profiles[s + 1], sizeof m->profiles[0] * (size_t)(m->profile_count - s - 1));
	m->profile_count--;
	m->profiles_changed = 1;
	if (m->profile_count == 0) kp_ui_list_select(ui, login_list, KP_UI_LIST_NONE);
	else kp_ui_list_select(ui, login_list, s < m->profile_count ? s : m->profile_count - 1);
	login_buttons(ui);
}

static void login_done(KpUi *ui, int32_t r)
{
	/* RunLoginScreen 0x459f10's dispatch */
	KpUiMenus *m = mn(ui);
	if (r == R_LOGIN_OK) screen_ended(ui, m->expansion ? STATE_EXP_CAMPAIGN : STATE_CAMPAIGN, KP_UI_MENU_STATE);
	else screen_ended(ui, STATE_MAIN, KP_UI_MENU_STATE);
}

static int login_proc(KpUi *ui, KpUiControl *d, KpUiEvent *e)
{
	/* LoginDialogProc 0x45a040 */
	KpUiMenus *m = mn(ui);
	int32_t self = (int32_t)(d - ui->controls);
	KpUiInteract base = kp_ui_type_interact(0);
	if (e->type != KP_UI_USER) return base(ui, d, e);
	switch (e->code) {
	case KP_UI_CREATE: /* LoadLoginProfiles 0x45a000 */
		if (m->profile_count <= 0) (void)kp_ui_set_timer(&ui->timers, self, LOGIN_TIMER, LOGIN_PERIOD, 0, ui->now);
		kp_ui_glue_activate(ui, self, THEME_NONE);
		break;
	case KP_UI_CLOSE:
		switch (ui->result) {
		case R_LOGIN_OK: { /* SelectLoginProfile 0x45a240 */
			int s = kp_ui_list_selection(ui, login_list);
			if (s == KP_UI_LIST_NONE || s >= m->profile_count) return 1;
			snprintf(m->player, sizeof m->player, "%s", m->profiles[s].name);
			kp_ui_glue_slide_out(ui, self);
			return 1;
		}
		case R_NEW_ID: new_profile(ui); return 1;
		case R_DELETE:
			if (kp_ui_list_selection(ui, login_list) == KP_UI_LIST_NONE) return 1;
			ok_cancel_popup(ui, kp_ui_tbl(ui, KP_UI_GLUALL, STR_DELETE_PROFILE), delete_profile_done);
			return 1;
		default: kp_ui_glue_slide_out(ui, self); return 1;
		}
	case KP_UI_TIMER:
		if (e->param == LOGIN_TIMER) {
			kp_ui_delete_timer(&ui->timers, self, LOGIN_TIMER);
			new_profile(ui);
			return 1;
		}
		break;
	case KP_UI_POST_LOAD: {
		static const uint16_t SLIDES[6] = { 1, 0, 2, 3, 3, 2 }; /* 0x508194 */
		KpUiInteract procs[9];
		int k;
		memset(procs, 0, sizeof procs);
		for (k = 3; k <= 6; k++) procs[k] = kp_ui_text_button_proc; /* 0x5081ac */
		procs[7] = login_list_proc;
		procs[8] = kp_ui_type_interact(10);
		login_dialog = self;
		kp_ui_glue_init_dialog(ui, self, procs, 9, THEME_NONE);
		kp_ui_glue_slide_in(ui, self, SLIDES, 3, 0);
		break;
	}
	default: break;
	}
	return base(ui, d, e);
}

int32_t kp_ui_login_open(KpUi *ui)
{
	login_list = -1;
	return open_screen(ui, "rez\\gluLogin.bin", login_proc, login_done);
}

/* --- the campaign menus (glue states 6, 22) --- */

/* The campaign.kpd TABL table of race (0 Zerg, 1 Terran, 2 Protoss) and expansion: its rows (8 bytes: label, mission,
 * cinematic, race, hidden) and their count, or NULL. */
static const uint8_t *campaign_table(KpUi *ui, int expansion, int race, int *count)
{
	KpUiMenus *m = mn(ui);
	const uint8_t *p = m->campaign, *end = m->campaign + m->campaign_size;
	int k, tables;
	if (p == NULL || m->campaign_size < 1) return NULL;
	tables = p[0];
	p++;
	for (k = 0; k < tables && p + 4 <= end; k++) {
		int n = p[2] | p[3] << 8;
		if (p[0] == expansion && p[1] == race) {
			*count = n;
			return p + 4;
		}
		p += 4 + 8 * (size_t)n;
	}
	return NULL;
}

static uint16_t row16(const uint8_t *row, int at) { return (uint16_t)(row[at] | row[at + 1] << 8); }

static KpUiProfile *player_profile(KpUi *ui)
{
	int k = profile_index(ui, mn(ui)->player);
	return k >= 0 ? &mn(ui)->profiles[k] : NULL;
}

static void episode_hints(KpUi *ui, int expansion)
{
	/* ComputeCampaignLockHints 0x454130, ComputeEpisodeHints 0x458f00 */
	KpUiMenus *m = mn(ui);
	KpUiProfile *p = player_profile(ui);
	memset(m->hints, 0, sizeof m->hints);
	if (p == NULL) return;
	if (!expansion) {
		uint32_t z = p->progress[0], t = p->progress[1], pr = p->progress[2];
		if (z == 0 && t <= 11) m->hints[0] = -10;
		if (pr == 0) {
			if (t <= 11) m->hints[2] = -11;
			else if (z <= 21) m->hints[2] = -12;
		}
	} else {
		uint32_t z = p->progress[3], t = p->progress[4], pr = p->progress[5];
		if (t == 0 && pr <= 39) m->hints[1] = -10;
		if (z == 0) {
			if (pr <= 39) m->hints[0] = -11;
			else if (t <= 48) m->hints[0] = -12;
		}
	}
}

/* The race of an episode button's ID: the original menu's Protoss 6, Terran 7, Zerg 8; the expansion's Protoss 6,
 * Terran 7, Zerg 8 too. */
static int episode_race(int16_t id) { return id == 6 ? 2 : id == 7 ? 1 : id == 8 ? 0 : -1; }

static void hint_tick(KpUi *ui, int32_t owner)
{
	/* CampaignLockHintTimer 0x4540e0, EpisodeHintTimer 0x458e80 */
	KpUiControl *c = &ui->controls[owner];
	int race = episode_race(c->id);
	int32_t label;
	if (race < 0 || mn(ui)->hints[race] == 0) return;
	label = kp_ui_find_control(ui, c->parent, (int16_t)mn(ui)->hints[race]);
	if (label >= 0) kp_ui_set_visible(ui, label, (c->flags & KP_UI_HOVER) != 0);
}

static KpUiDraw stock_flc_draw;

static void episode_draw(KpUi *ui, KpUiControl *c, int32_t x, int32_t y, const int16_t clip[4])
{
	/* DrawEpisodeButton 0x458d50: a disabled button draws without its hover */
	uint32_t flags = c->flags;
	if (flags & KP_UI_DISABLED) c->flags &= ~KP_UI_HOVER;
	if (stock_flc_draw != NULL) stock_flc_draw(ui, c, x, y, clip);
	c->flags = flags;
}

static void episode_animate(KpUi *ui, int32_t owner)
{
	/* EpisodeButtonAnimTimer 0x458ed0 */
	KpUiControl *c = &ui->controls[owner];
	uint32_t flags = c->flags;
	if (flags & KP_UI_DISABLED) c->flags &= ~KP_UI_HOVER;
	kp_ui_flc_advance(ui, owner);
	c->flags = (c->flags & ~KP_UI_HOVER) | (flags & KP_UI_HOVER);
}

static int episode_proc(KpUi *ui, KpUiControl *c, KpUiEvent *e)
{
	/* CampaignEpisodeButtonInteract 0x453fe0, EpisodeButtonProc 0x458d80 */
	int32_t self = (int32_t)(c - ui->controls);
	if ((e->type == KP_UI_LEFT_DOWN || e->type == KP_UI_LEFT_DOUBLE) && (c->flags & KP_UI_DISABLED)) return 0;
	if (e->type == KP_UI_USER) {
		switch (e->code) {
		case KP_UI_CREATE:
			(void)kp_ui_set_timer(&ui->timers, self, HINT_TIMER, HINT_PERIOD, kp_ui_callback(ui, hint_tick), ui->now);
			break;
		case KP_UI_DESTROY:
			kp_ui_delete_timer(&ui->timers, self, HINT_TIMER);
			break;
		case KP_UI_HIT_TEST: {
			const KpUiControl *d = &ui->controls[c->parent];
			int32_t x = e->x - d->left, y = e->y - d->top;
			/* the rectangle at +0x36 (the BIN's responsive rectangle) */
			int inside = x >= (int16_t)c->words36[0] && x <= (int16_t)c->words36[2] && y >= (int16_t)c->words36[1]
				&& y <= (int16_t)c->words36[3];
			return inside && (c->flags & KP_UI_VISIBLE) ? (int)(c->flags & KP_UI_EVENTS) : 0;
		}
		case KP_UI_POST_LOAD:
			(void)kp_ui_flc_button_proc(ui, c, e);
			stock_flc_draw = c->draw;
			c->draw = episode_draw;
			(void)kp_ui_set_timer(&ui->timers, self, ANIMATION_TIMER, ANIMATION_PERIOD,
				kp_ui_callback(ui, episode_animate), ui->now);
			return 1;
		default: break;
		}
	}
	return kp_ui_flc_button_proc(ui, c, e);
}

static int32_t campaign_dialog;

static void campaign_done(KpUi *ui, int32_t r)
{
	/* RunCampaignMenu 0x453f00, RunExpansionCampaignScreen 0x458b00: the jump tables 0x453fb8, 0x458bb8 */
	int next;
	switch (r) {
	case 5: next = STATE_LOAD; break;
	case 6: next = STATE_PROTOSS_ROOM; break;
	case 7: next = STATE_TERRAN_ROOM; break;
	case 8: next = STATE_ZERG_ROOM; break;
	case 9: next = STATE_LOGIN; break;
	default: next = STATE_MAIN; break; /* Play Custom and Load Replay answer nothing (unsupported_text_button_proc) */
	}
	screen_ended(ui, next, mn(ui)->action == KP_UI_MENU_CINEMATIC ? KP_UI_MENU_CINEMATIC
		: next == STATE_PROTOSS_ROOM || next == STATE_TERRAN_ROOM || next == STATE_ZERG_ROOM ? KP_UI_MENU_MISSION
		: KP_UI_MENU_STATE);
}

static int32_t episode_result;

static void history_done(KpUi *ui, int32_t r);

static int history_proc(KpUi *ui, KpUiControl *d, KpUiEvent *e)
{
	/* MissionHistoryPopupProc 0x460520 */
	KpUiMenus *m = mn(ui);
	int32_t self = (int32_t)(d - ui->controls), list;
	const uint8_t *table;
	int count = 0, k, s;
	if (e->type == KP_UI_USER && e->code == KP_UI_CREATE) { /* InitMissionHistoryList 0x460450 */
		table = campaign_table(ui, m->expansion, m->history_table, &count);
		if ((list = kp_ui_find_control(ui, self, ID_HISTORY_LIST)) >= 0 && table != NULL) {
			ui->controls[list].flags |= LIST_FLAG;
			kp_ui_list_redraw(ui, list, 0);
			for (k = 0; k < count && row16(table + 8 * k, 2) != 0; k++) {
				if (row16(table + 8 * k, 2) > (uint32_t)m->history_progress || row16(table + 8 * k, 0) == 0) continue;
				s = kp_ui_list_add(ui, list, kp_ui_tbl(ui, KP_UI_GLUHIST, row16(table + 8 * k, 0)), 0);
				kp_ui_list_set_data(ui, list, s, (uint32_t)k);
			}
			kp_ui_list_redraw(ui, list, 1);
			kp_ui_list_select(ui, list, 0);
		}
	} else if (e->type == KP_UI_USER && e->code == KP_UI_CLOSE) { /* MissionHistoryPopupClose 0x4604e0 */
		if (ui->result == R_OK && (list = kp_ui_find_control(ui, self, ID_HISTORY_LIST)) >= 0
			&& (s = kp_ui_list_selection(ui, list)) != KP_UI_LIST_NONE) {
			m->history_row = (int32_t)kp_ui_list_data(ui, list, s);
		}
	}
	return popup_proc(ui, d, e);
}

static int start_episode(KpUi *ui, int race)
{
	/* StartCampaignEpisode 0x4bb5b0 (the profile is the host's: a missing one starts from nothing) */
	KpUiMenus *m = mn(ui);
	KpUiProfile *p = player_profile(ui);
	const uint8_t *table;
	int count = 0, k, listed = 0, field = (m->expansion ? 3 : 0) + race;
	uint32_t progress = p != NULL ? p->progress[field] : 0;
	/* the history's procedures 0x5082dc by the controls' IDs 1..7: Ok, Cancel, the list's index 7 */
	static const KpUiInteract PROCS[7] = { kp_ui_text_button_proc, NULL, kp_ui_text_button_proc, NULL, NULL, NULL,
		kp_ui_list_proc };
	table = campaign_table(ui, m->expansion, race, &count);
	if (table == NULL) return 0;
	m->race = race;
	m->history_table = race;
	for (k = 0; k < count && row16(table + 8 * k, 2) != 0; k++) {
		if (row16(table + 8 * k, 2) <= progress && row16(table + 8 * k, 0) != 0) listed++;
	}
	if (listed <= 1) { /* RunMissionHistoryPopup 0x45fef0 without its popup: the first row */
		m->history_row = 0;
		history_done(ui, R_OK);
		return 1;
	}
	/* RunMissionHistoryPopup 0x45fef0: the rows up to the progress; Cancel leaves the campaign menu as it is */
	m->history_progress = (int32_t)progress;
	m->history_row = -1;
	popup_image(ui, "glue\\campaign\\pHist.pcx");
	(void)run_popup(ui, "rez\\gluHist.bin", PROCS, 7, history_proc, history_done);
	return 1;
}

static void history_done(KpUi *ui, int32_t r)
{
	/* the rest of StartCampaignEpisode 0x4bb5b0: the progress, a cinematic row or the mission */
	KpUiMenus *m = mn(ui);
	KpUiProfile *p = player_profile(ui);
	const uint8_t *table, *row;
	int count = 0, field = (m->expansion ? 3 : 0) + m->race;
	uint16_t mission;
	if (r != R_OK) return;
	table = campaign_table(ui, m->expansion, m->race, &count);
	if (table == NULL || m->history_row < 0 || m->history_row >= count) return;
	row = table + 8 * (size_t)m->history_row;
	mission = row16(row, 2);
	if (p != NULL && p->progress[field] < mission) {
		p->progress[field] = mission;
		m->profiles_changed = 1;
	}
	/* CreateCampaignMissionGame 0x4bb6c0: FillProgramInfo 0x46f330's key and name; a cinematic row's follow its
	 * cinematic (AdvanceCampaignAfterGame 0x4bb790), which the host plays, when its row leads to a mission
	 * (kp_ui_menus_mission_draws: the host's campaign knows) */
	if (row16(row, 4) != 0) {
		m->action = KP_UI_MENU_CINEMATIC;
		m->cinematic = row16(row, 4);
	} else {
		font_key_draws(ui, 2);
	}
	m->mission = mission;
	ui->result = episode_result;
	if (campaign_dialog >= 0) kp_ui_glue_slide_out(ui, campaign_dialog);
}

void kp_ui_menus_mission_draws(KpUi *ui) { font_key_draws(ui, 2); }

static void episode_confirmed(KpUi *ui, int32_t r)
{
	if (r != R_OK) return;
	ui->result = episode_result;
	(void)start_episode(ui, episode_race((int16_t)episode_result));
}

static int campaign_proc(KpUi *ui, KpUiControl *d, KpUiEvent *e)
{
	/* CampaignMenuDialogProc 0x453e80, the expansion's 0x458cd0 */
	KpUiMenus *m = mn(ui);
	int32_t self = (int32_t)(d - ui->controls);
	KpUiInteract base = kp_ui_type_interact(0);
	if (e->type != KP_UI_USER) return base(ui, d, e);
	switch (e->code) {
	case KP_UI_CREATE: {
		static const int16_t UNSUPPORTED[2] = { ID_CAMPAIGN_CUSTOM, ID_CAMPAIGN_REPLAY };
		hide_unsupported_labels(ui, self, UNSUPPORTED, 2);
		kp_ui_glue_activate(ui, self, THEME_CAMPAIGN);
		if (!ui->music.playing) kp_ui_music_play(ui, TITLE_MUSIC);
		break;
	}
	case KP_UI_CLOSE: { /* OnCampaignMenuButton 0x453d90, the expansion's 0x458be0 */
		int race = episode_race((int16_t)ui->result);
		if (race < 0) {
			kp_ui_glue_slide_out(ui, self);
			return 1;
		}
		episode_result = ui->result;
		if (m->hints[race] != 0) {
			uint32_t str = m->expansion ? (race == 1 ? NET_PLAY_ZERG : NET_PLAY_PROTOSS)
				: (race == 2 ? NET_PROTOSS_WARNING : NET_ZERG_WARNING);
			ok_cancel_popup(ui, kp_ui_tbl(ui, KP_UI_NETWORK, str), episode_confirmed);
			return 1;
		}
		(void)start_episode(ui, race);
		return 1;
	}
	case KP_UI_POST_LOAD: {
		static const uint16_t SLIDES[4] = { 1, 0, 3, 2 }; /* 0x503d14, 0x5080cc */
		KpUiInteract procs[11];
		memset(procs, 0, sizeof procs);
		procs[4] = procs[8] = kp_ui_text_button_proc; /* 0x503d1c, 0x5080d4 */
		procs[ID_CAMPAIGN_CUSTOM - 1] = procs[ID_CAMPAIGN_REPLAY - 1] = unsupported_text_button_proc;
		if (m->expansion) {
			procs[5] = kp_ui_flc_button_proc;
			procs[6] = procs[7] = episode_proc;
		} else {
			procs[5] = procs[7] = episode_proc;
			procs[6] = kp_ui_flc_button_proc;
		}
		campaign_dialog = self;
		kp_ui_glue_init_dialog(ui, self, procs, 11, THEME_CAMPAIGN);
		kp_ui_glue_slide_in(ui, self, SLIDES, 2, 0);
		break;
	}
	default: break;
	}
	return base(ui, d, e);
}

int32_t kp_ui_campaign_open(KpUi *ui, int expansion)
{
	KpUiMenus *m = mn(ui);
	m->multiplayer = 0;
	m->expansion = expansion;
	campaign_dialog = -1;
	episode_hints(ui, expansion);
	return open_screen(ui, expansion ? "rez\\gluExpCmpgn.bin" : "rez\\gluCmpgn.bin", campaign_proc, campaign_done);
}

/* --- the load screen (glue state 13) --- */

static int32_t load_dialog, load_list;

static void load_buttons(KpUi *ui)
{
	/* 0x459af0: Ok and Delete with a selection */
	int on = kp_ui_list_selection(ui, load_list) != KP_UI_LIST_NONE;
	int32_t ok = kp_ui_find_control(ui, load_dialog, ID_LOAD_OK), del = kp_ui_find_control(ui, load_dialog, ID_LOAD_DELETE);
	if (ok >= 0) kp_ui_set_enabled(ui, ok, on);
	if (del >= 0) kp_ui_set_enabled(ui, del, on);
}

static int save_list_proc(KpUi *ui, KpUiControl *c, KpUiEvent *e)
{
	/* SaveListProc 0x459d40 */
	KpUiMenus *m = mn(ui);
	int32_t self = (int32_t)(c - ui->controls);
	int k;
	if (e->type == KP_UI_USER) {
		switch (e->code) {
		case KP_UI_CREATE: /* 0x459dd0: the player's saved games */
			for (k = 0; k < m->save_count; k++) (void)kp_ui_list_add(ui, self, (const uint8_t *)m->saves[k], 0);
			load_list = self;
			load_buttons(ui);
			break;
		case KP_UI_POST_LOAD:
			c->flags |= LIST_FLAG;
			load_list = self;
			break;
		case KP_UI_VALUE_CHANGED:
			(void)kp_ui_list_proc(ui, c, e);
			load_buttons(ui);
			return 1;
		default: break;
		}
	}
	return kp_ui_list_proc(ui, c, e);
}

static void delete_save_done(KpUi *ui, int32_t r)
{
	/* DeleteSelectedSave 0x4599e0 after its popup: the save's file (0x498b30, its result not looked at: a file that
	 * stays is listed again), the list again (0x4991b0 with 0x459bc0) and the buttons (0x459af0) */
	KpUiMenus *m = mn(ui);
	int s = kp_ui_list_selection(ui, load_list), k;
	char name[sizeof m->saves[0]];
	if (r != R_OK || s == KP_UI_LIST_NONE || s >= m->save_count) return;
	snprintf(name, sizeof name, "%s", m->saves[s]);
	(void)kp_ui_delete_save(ui, name);
	kp_ui_list_clear(ui, load_list);
	for (k = 0; k < m->save_count; k++) (void)kp_ui_list_add(ui, load_list, (const uint8_t *)m->saves[k], 0);
	load_buttons(ui);
}

static void load_done(KpUi *ui, int32_t r)
{
	/* RunLoadGameScreen 0x459970 */
	KpUiMenus *m = mn(ui);
	if (r == R_LOAD_OK) screen_ended(ui, m->state, KP_UI_MENU_LOAD_SAVE);
	else if (r == R_LOAD_CANCEL) screen_ended(ui, m->expansion ? STATE_EXP_CAMPAIGN : STATE_CAMPAIGN, KP_UI_MENU_STATE);
	else screen_ended(ui, STATE_MAIN, KP_UI_MENU_STATE);
}

static int load_proc(KpUi *ui, KpUiControl *d, KpUiEvent *e)
{
	/* 0x459a70 */
	KpUiMenus *m = mn(ui);
	int32_t self = (int32_t)(d - ui->controls);
	KpUiInteract base = kp_ui_type_interact(0);
	if (e->type != KP_UI_USER) return base(ui, d, e);
	switch (e->code) {
	case KP_UI_CREATE: kp_ui_glue_activate(ui, self, THEME_NONE); break;
	case KP_UI_CLOSE:
		if (ui->result == R_LOAD_DELETE) { /* DeleteSelectedSave 0x4599e0 */
			ok_cancel_popup(ui, kp_ui_tbl(ui, KP_UI_GLUALL, STR_DELETE_SAVE), delete_save_done);
			return 1;
		}
		if (ui->result == R_LOAD_OK) m->save_choice = kp_ui_list_selection(ui, load_list);
		kp_ui_glue_slide_out(ui, self);
		return 1;
	case KP_UI_POST_LOAD: {
		static const uint16_t SLIDES[6] = { 1, 0, 2, 3, 3, 2 }; /* 0x508160 */
		KpUiInteract procs[7];
		memset(procs, 0, sizeof procs);
		procs[3] = procs[4] = procs[6] = kp_ui_text_button_proc; /* 0x508178 */
		procs[5] = save_list_proc;
		load_dialog = self;
		kp_ui_glue_slide_in(ui, self, SLIDES, 3, 0);
		kp_ui_glue_init_dialog(ui, self, procs, 7, THEME_NONE);
		break;
	}
	default: break;
	}
	return base(ui, d, e);
}

int32_t kp_ui_load_open(KpUi *ui)
{
	load_list = -1;
	return open_screen(ui, "rez\\gluLoad.bin", load_proc, load_done);
}
