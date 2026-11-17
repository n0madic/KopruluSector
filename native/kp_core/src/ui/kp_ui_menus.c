/* The glue's loop and its menus (glues.cpp 0x457510; glumain.cpp, glulogin.cpp, glupopup.cpp, the campaign menus
 * gluchatslot.cpp..gluconn.cpp and glues.cpp, glujoin.cpp's load screen; atlas P18..P21, P41): a state's screen runs
 * modally (RunModal 0x4c35c0 as a dialog whose destruction runs its continuation), the popups nest in it (a glue dialog
 * composited into the screen's bitmap, flag 0x8000000), and the screen's end leaves the next state and the host's work
 * (a mission or cinematic to start, a saved game to load, a replay to play, a custom game to create, the exit).
 * Multiplayer and the Campaign Editor are outside the product's scope: their buttons have no label and answer nothing. */
#include "kp_gametype.h"
#include "kp_template.h"
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
#define STATE_PLAY_CUSTOM 11
#define STATE_LOAD_REPLAY 12
#define STATE_LOAD 13
#define STATE_EXP_CAMPAIGN 22
#define DIALOG_TRANSLUCENT 0x40000u
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
#define ID_CAMPAIGN_CUSTOM 10   /* the campaign menus' Play Custom and Load Replay (results 10 and */
#define ID_CAMPAIGN_REPLAY 11   /* 11, glue states 11 and 12: RunCampaignMenu 0x453f00, 0x458b00) */
#define R_INTRO 8
/* The main menu's version label: the original's is "Version 1.17.0" (0x465960 from its executable's product version),
 * the remake's says what it is a remake of: the label's own text (defs/ui/koprulu-dialogs.json's "labels", translated
 * like our other texts) is the format of this version */
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
#define R_LANGUAGE 11           /* the main menu's Language: our record (defs/ui/koprulu-dialogs.json "extend") */
#define ID_LANGUAGE_OK 1        /* koprulu\language.bin (ours): OK, Cancel, the text's button, the speech's label */
#define ID_LANGUAGE_CANCEL 3
#define ID_LANGUAGE_TEXT 5      /* and button */
#define ID_LANGUAGE_SPEECH_LABEL 6
#define ID_LANGUAGE_SPEECH 7
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
	(void)kp_ui_load_picture(ui, kp_ui_glue_file(ui, path), &mn(ui)->popup_image, NULL);
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
	kp_ui_end_dialog(ui, m->popup);
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

static void popup_post_load(KpUi *ui, int32_t dialog)
{
	/* InitGluePopupDialog 0x460580 */
	KpUiMenus *m = mn(ui);
	KpUiControl *d = &ui->controls[dialog];
	kp_ui_glue_init_dialog(ui, dialog, m->popup_procs, m->popup_proc_count, state_theme(m->state));
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

void kp_ui_centre_glue_dialog(KpUiControl *d)
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
		case KP_UI_RECENTRE: kp_ui_centre_glue_dialog(d); break;
		case KP_UI_POST_LOAD: popup_post_load(ui, self); break;
		case KP_UI_CLOSE:
			(void)base(ui, d, e);
			kp_ui_end_dialog(ui, self);
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
	memcpy(m->popup_procs, procs, sizeof procs[0] * (size_t)count);
	m->popup_proc_count = count;
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
	memcpy(m->popup_procs, PROCS, sizeof PROCS);
	m->popup_proc_count = 4;
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
	else if (r == R_LANGUAGE) screen_ended(ui, m->state, KP_UI_MENU_LANGUAGE);
	else screen_ended(ui, STATE_MAIN, KP_UI_MENU_STATE);
}

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
	ui->result = m->saved_result;
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
	m->saved_result = ui->result;
	close_popup(ui);
	snprintf(m->popup_text, sizeof m->popup_text, "%s", (const char *)kp_ui_tbl(ui, KP_UI_GLUALL, STR_GAME_TYPE));
	m->popup_text2[0] = 0;
	popup_background(ui, "retail_ex.pcx");
	(void)run_popup(ui, "rez\\gluGameMode.bin", procs, 7, popup_proc, game_mode_done);
}

/* --- the languages (ADR 0016): our koprulu\language.bin over the main menu --- */

/* The languages installed for the text (speech 0) or the speech. */
static int languages_for(const KpUiLanguages *l, int speech)
{
	int k, n = 0;
	for (k = 0; k < l->count; k++) n += speech ? l->list[k].speech : l->list[k].text;
	return n;
}

int kp_ui_languages_offered(const KpUi *ui)
{
	const KpUiLanguages *l = &ui->menus.languages;
	return languages_for(l, 0) > 1 || languages_for(l, 1) > 1;
}

/* The language after at installed for the text or the speech, round the list (at itself when it is the only one). */
static int next_language(const KpUiLanguages *l, int at, int speech)
{
	int k;
	for (k = 1; k <= l->count; k++) {
		int i = ((at < 0 ? -1 : at) + k) % l->count;
		if (speech ? l->list[i].speech : l->list[i].text) return i;
	}
	return at;
}

static void language_name(KpUi *ui, int32_t dialog, int16_t id, int at, int shown)
{
	const KpUiLanguages *l = &mn(ui)->languages;
	int32_t c = kp_ui_find_control(ui, dialog, id);
	if (c < 0) return;
	if (id != ID_LANGUAGE_SPEECH_LABEL) ui->controls[c].text = at >= 0 ? (const uint8_t *)l->list[at].name : NULL;
	kp_ui_set_visible(ui, c, shown);
	kp_ui_mark_dirty(ui, c);
}

/* The buttons' names of the popup's choice; the speech's row only when there is a speech to choose. */
static void language_names(KpUi *ui, int32_t dialog)
{
	const KpUiLanguages *l = &mn(ui)->languages;
	int speech = languages_for(l, 1) > 1;
	language_name(ui, dialog, ID_LANGUAGE_TEXT, l->chosen_text, 1);
	language_name(ui, dialog, ID_LANGUAGE_SPEECH_LABEL, -1, speech);
	language_name(ui, dialog, ID_LANGUAGE_SPEECH, l->chosen_speech, speech);
}

/* A language's button: its activation takes the next language and the popup stays. */
static int language_button(KpUi *ui, KpUiControl *c, KpUiEvent *e)
{
	KpUiLanguages *l = &mn(ui)->languages;
	if (e->type == KP_UI_USER && e->code == KP_UI_CLOSE) {
		int speech = c->id == ID_LANGUAGE_SPEECH;
		int *at = speech ? &l->chosen_speech : &l->chosen_text;
		*at = next_language(l, *at, speech);
		language_names(ui, c->parent);
		return 1;
	}
	return kp_ui_text_button_proc(ui, c, e);
}

static int language_proc(KpUi *ui, KpUiControl *d, KpUiEvent *e)
{
	int r = popup_proc(ui, d, e);
	if (e->type == KP_UI_USER && e->code == KP_UI_CREATE) language_names(ui, (int32_t)(d - ui->controls));
	return r;
}

/* OK with another choice ends the main menu for the host (KP_UI_MENU_LANGUAGE), as Exit's slide does. */
static void language_done(KpUi *ui, int32_t r)
{
	KpUiMenus *m = mn(ui);
	const KpUiLanguages *l = &m->languages;
	ui->result = m->saved_result;
	if (r == ID_LANGUAGE_OK && (l->chosen_text != l->text || l->chosen_speech != l->speech) && m->screen >= 0) {
		ui->result = R_LANGUAGE;
		kp_ui_glue_slide_out(ui, m->screen);
	}
}

static void language_popup(KpUi *ui)
{
	KpUiMenus *m = mn(ui);
	KpUiInteract procs[ID_LANGUAGE_SPEECH];
	memset(procs, 0, sizeof procs);
	procs[ID_LANGUAGE_OK - 1] = procs[ID_LANGUAGE_CANCEL - 1] = kp_ui_text_button_proc;
	procs[ID_LANGUAGE_TEXT - 1] = procs[ID_LANGUAGE_SPEECH - 1] = language_button;
	m->saved_result = ui->result;
	close_popup(ui);
	m->popup_text[0] = m->popup_text2[0] = 0;
	m->languages.chosen_text = m->languages.text;
	m->languages.chosen_speech = m->languages.speech;
	popup_background(ui, "pDPopup.pcx");
	(void)run_popup(ui, "koprulu\\language.bin", procs, ID_LANGUAGE_SPEECH, language_proc, language_done);
}

static void version_label(KpUi *ui, int32_t dialog)
{
	/* SetMainMenuVersionLabel 0x45ab00 (not spawned: gluAll 3 "%s" of the version) */
	KpUiMenus *m = mn(ui);
	int32_t c = kp_ui_find_control(ui, dialog, ID_MAIN_VERSION);
	if (c < 0) return;
	font_key_draws(ui, 1); /* the spawner's name from font\font.clh: none on a full installation */
	if (ui->controls[c].text != (const uint8_t *)m->version) {
		const char *format = (const char *)ui->controls[c].text, *mark;
		/* the one %s the converter checked against the base's; a label with no text (data older than it) stays empty */
		m->version[0] = 0;
		if (format != NULL && (mark = strstr(format, "%s")) != NULL)
			snprintf(m->version, sizeof m->version, "%.*s%s%s", (int)(mark - format), format, ORIGINAL_VERSION, mark + 2);
	}
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
		if (!ui->music.playing) kp_ui_music_play(ui, KP_UI_TITLE_MUSIC);
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
		procs[R_LANGUAGE - 1] = kp_ui_text_button_proc; /* 0x5081d0's eleventh (no record of the original's has it) */
		/* no language to choose: the dialog is the original's, without the Language's FLC timer */
		if ((k = kp_ui_find_control(ui, self, R_LANGUAGE)) >= 0 && !kp_ui_languages_offered(ui)) kp_ui_drop_control(ui, self, k);
		kp_ui_glue_init_dialog(ui, self, procs, 11, THEME_MAIN);
		break;
	}
	case KP_UI_CLOSE:
		switch (ui->result) {
		case R_SINGLE: game_mode_popup(ui); return 1; /* the expansion's data are present */
		case R_LANGUAGE: language_popup(ui); return 1;
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
int32_t kp_ui_create_open(KpUi *ui);

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
	if (pending_mission(ui)) return -1;
	switch (state) {
	case STATE_MAIN:
		m->multiplayer = 0;
		return open_screen(ui, "rez\\gluMain.bin", main_proc, main_done);
	case STATE_LOGIN: return kp_ui_login_open(ui);
	case STATE_CAMPAIGN: return kp_ui_campaign_open(ui, 0);
	case STATE_EXP_CAMPAIGN: return kp_ui_campaign_open(ui, 1);
	case STATE_LOAD: return kp_ui_load_open(ui);
	case STATE_PLAY_CUSTOM:
	case STATE_LOAD_REPLAY: return kp_ui_create_open(ui);
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

static void login_buttons(KpUi *ui)
{
	/* UpdateLoginButtons 0x45a280 */
	int sel = kp_ui_list_selection(ui, mn(ui)->login_list);
	int32_t ok = kp_ui_find_control(ui, mn(ui)->login_dialog, ID_LOGIN_OK), del = kp_ui_find_control(ui, mn(ui)->login_dialog, ID_LOGIN_DELETE);
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
			int32_t ok = kp_ui_find_control(ui, m->login_dialog, ID_LOGIN_OK);
			int32_t del = kp_ui_find_control(ui, m->login_dialog, ID_LOGIN_DELETE);
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
			m->login_list = self;
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
	kp_ui_list_insert(ui, m->login_list, at, (const uint8_t *)m->profiles[at].name, 0);
	kp_ui_list_select(ui, m->login_list, at);
	{
		int32_t ok = kp_ui_find_control(ui, m->login_dialog, ID_LOGIN_OK);
		int32_t del = kp_ui_find_control(ui, m->login_dialog, ID_LOGIN_DELETE);
		if (del >= 0) kp_ui_set_enabled(ui, del, 1);
		if (ok >= 0) kp_ui_set_enabled(ui, ok, 1);
	}
}

static void new_profile(KpUi *ui)
{
	KpUiMenus *m = mn(ui);
	if (kp_ui_list_count(ui, m->login_list) >= KP_UI_PROFILES) {
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
	int s = kp_ui_list_selection(ui, m->login_list);
	if (r != R_OK || s == KP_UI_LIST_NONE || s >= m->profile_count) return;
	if (ui->delete_profile_saves) ui->delete_profile_saves(ui->host, m->profiles[s].name);
	kp_ui_list_delete(ui, m->login_list, s);
	memmove(&m->profiles[s], &m->profiles[s + 1], sizeof m->profiles[0] * (size_t)(m->profile_count - s - 1));
	m->profile_count--;
	m->profiles_changed = 1;
	if (m->profile_count == 0) kp_ui_list_select(ui, m->login_list, KP_UI_LIST_NONE);
	else kp_ui_list_select(ui, m->login_list, s < m->profile_count ? s : m->profile_count - 1);
	login_buttons(ui);
}

static void login_done(KpUi *ui, int32_t r)
{
	/* RunLoginScreen 0x459f10's dispatch */
	KpUiMenus *m = mn(ui);
	if (r == R_LOGIN_OK) screen_ended(ui, m->expansion ? STATE_EXP_CAMPAIGN : STATE_CAMPAIGN, KP_UI_MENU_STATE);
	else screen_ended(ui, STATE_MAIN, KP_UI_MENU_STATE);
}

/* A dialog's wheel events give its list's scroll bar a row up (type 17: code 15) or down (18: 16); the original has no
 * test for a list without a bar (LoginDialogProc 0x45a040, CreateGameDialogProc 0x4550e0). */
static int wheel_list(KpUi *ui, int32_t list, const KpUiEvent *e)
{
	const KpUiList *l = list >= 0 ? ui->controls[list].list : NULL;
	if (l != NULL && l->bar >= 0) {
		(void)kp_ui_send_user(ui, l->bar, e->type == KP_UI_WHEEL_UP ? KP_UI_SCROLL_UP : KP_UI_SCROLL_DOWN, 0);
	}
	return 1;
}

static int is_wheel(const KpUiEvent *e) { return e->type == KP_UI_WHEEL_UP || e->type == KP_UI_WHEEL_DOWN; }

static int login_proc(KpUi *ui, KpUiControl *d, KpUiEvent *e)
{
	/* LoginDialogProc 0x45a040 */
	KpUiMenus *m = mn(ui);
	int32_t self = (int32_t)(d - ui->controls);
	KpUiInteract base = kp_ui_type_interact(0);
	if (is_wheel(e)) return wheel_list(ui, m->login_list, e);
	if (e->type != KP_UI_USER) return base(ui, d, e);
	switch (e->code) {
	case KP_UI_CREATE: /* LoadLoginProfiles 0x45a000 */
		if (m->profile_count <= 0) (void)kp_ui_set_timer(&ui->timers, self, LOGIN_TIMER, LOGIN_PERIOD, 0, ui->now);
		kp_ui_glue_activate(ui, self, THEME_NONE);
		break;
	case KP_UI_CLOSE:
		switch (ui->result) {
		case R_LOGIN_OK: { /* SelectLoginProfile 0x45a240 */
			int s = kp_ui_list_selection(ui, m->login_list);
			if (s == KP_UI_LIST_NONE || s >= m->profile_count) return 1;
			snprintf(m->player, sizeof m->player, "%s", m->profiles[s].name);
			kp_ui_glue_slide_out(ui, self);
			return 1;
		}
		case R_NEW_ID: new_profile(ui); return 1;
		case R_DELETE:
			if (kp_ui_list_selection(ui, m->login_list) == KP_UI_LIST_NONE) return 1;
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
		m->login_dialog = self;
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
	mn(ui)->login_list = -1;
	return open_screen(ui, "rez\\gluLogin.bin", login_proc, login_done);
}

/* --- the campaign menus (glue states 6, 22) --- */

/* The profile's TABL table of race (0 Zerg, 1 Terran, 2 Protoss) and expansion: its rows (8 bytes: label, mission,
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

static void episode_draw(KpUi *ui, KpUiControl *c, int32_t x, int32_t y, const int16_t clip[4])
{
	/* DrawEpisodeButton 0x458d50: a disabled button draws without its hover */
	uint32_t flags = c->flags;
	if (flags & KP_UI_DISABLED) c->flags &= ~KP_UI_HOVER;
	kp_ui_flc_draw_button(ui, c, x, y, clip);
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
			c->draw = episode_draw;
			(void)kp_ui_set_timer(&ui->timers, self, ANIMATION_TIMER, ANIMATION_PERIOD,
				kp_ui_callback(ui, episode_animate), ui->now);
			return 1;
		default: break;
		}
	}
	return kp_ui_flc_button_proc(ui, c, e);
}

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
	case 10: next = STATE_PLAY_CUSTOM; break;
	case 11: next = STATE_LOAD_REPLAY; break;
	default: next = STATE_MAIN; break;
	}
	screen_ended(ui, next, mn(ui)->action == KP_UI_MENU_CINEMATIC ? KP_UI_MENU_CINEMATIC
		: next == STATE_PROTOSS_ROOM || next == STATE_TERRAN_ROOM || next == STATE_ZERG_ROOM ? KP_UI_MENU_MISSION
		: KP_UI_MENU_STATE);
}

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
	ui->result = m->episode_result;
	if (m->campaign_dialog >= 0) kp_ui_glue_slide_out(ui, m->campaign_dialog);
}

void kp_ui_menus_mission_draws(KpUi *ui) { font_key_draws(ui, 2); }

static void episode_confirmed(KpUi *ui, int32_t r)
{
	if (r != R_OK) return;
	ui->result = mn(ui)->episode_result;
	(void)start_episode(ui, episode_race((int16_t)mn(ui)->episode_result));
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
		kp_ui_glue_activate(ui, self, THEME_CAMPAIGN);
		if (!ui->music.playing) kp_ui_music_play(ui, KP_UI_TITLE_MUSIC);
		break;
	}
	case KP_UI_CLOSE: { /* OnCampaignMenuButton 0x453d90, the expansion's 0x458be0 */
		int race = episode_race((int16_t)ui->result);
		if (race < 0) {
			kp_ui_glue_slide_out(ui, self);
			return 1;
		}
		m->episode_result = ui->result;
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
		procs[ID_CAMPAIGN_CUSTOM - 1] = procs[ID_CAMPAIGN_REPLAY - 1] = kp_ui_text_button_proc;
		if (m->expansion) {
			procs[5] = kp_ui_flc_button_proc;
			procs[6] = procs[7] = episode_proc;
		} else {
			procs[5] = procs[7] = episode_proc;
			procs[6] = kp_ui_flc_button_proc;
		}
		m->campaign_dialog = self;
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
	m->campaign_dialog = -1;
	episode_hints(ui, expansion);
	return open_screen(ui, expansion ? "rez\\gluExpCmpgn.bin" : "rez\\gluCmpgn.bin", campaign_proc, campaign_done);
}

/* --- the load screen (glue state 13) --- */

static void load_buttons(KpUi *ui)
{
	/* 0x459af0: Ok and Delete with a selection */
	int on = kp_ui_list_selection(ui, mn(ui)->load_list) != KP_UI_LIST_NONE;
	int32_t ok = kp_ui_find_control(ui, mn(ui)->load_dialog, ID_LOAD_OK), del = kp_ui_find_control(ui, mn(ui)->load_dialog, ID_LOAD_DELETE);
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
			m->load_list = self;
			load_buttons(ui);
			break;
		case KP_UI_POST_LOAD:
			c->flags |= LIST_FLAG;
			m->load_list = self;
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
	int s = kp_ui_list_selection(ui, m->load_list), k;
	char name[sizeof m->saves[0]];
	if (r != R_OK || s == KP_UI_LIST_NONE || s >= m->save_count) return;
	snprintf(name, sizeof name, "%s", m->saves[s]);
	(void)kp_ui_delete_save(ui, name);
	kp_ui_list_clear(ui, m->load_list);
	for (k = 0; k < m->save_count; k++) (void)kp_ui_list_add(ui, m->load_list, (const uint8_t *)m->saves[k], 0);
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

#define SLIDE_DONE 0x405 /* the dialog's slides ended (kp_ui_glue.c) */

void kp_ui_menus_refused(KpUi *ui, uint16_t gluall) { mn(ui)->refusal = gluall; }

void kp_ui_menus_net_error(KpUi *ui, uint16_t network) { mn(ui)->net_error = network; }

void kp_ui_menus_flush_net_error(KpUi *ui)
{
	uint16_t index = mn(ui)->net_error;
	if (index == 0) return;
	mn(ui)->net_error = 0;
	ok_popup(ui, kp_ui_tbl(ui, KP_UI_NETWORK, index), NULL); /* ShowGlueOkBox 0x4570d0 over the active dialog */
}

/* The refusal of the game the screen chose before, once the screen stood (kp_ui_menus_refused). */
static void refused_popup(KpUi *ui)
{
	KpUiMenus *m = mn(ui);
	uint16_t refusal = m->refusal;
	if (refusal == 0) return;
	m->refusal = 0;
	ok_popup(ui, kp_ui_tbl(ui, KP_UI_GLUALL, refusal), NULL);
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
		if (ui->result == R_LOAD_OK) {
			int s = kp_ui_list_selection(ui, m->load_list);
			/* Koprulu's: a save that does not load here keeps the screen, with why (gluAll 66 or 67) */
			uint16_t refusal = s != KP_UI_LIST_NONE && ui->check_save != NULL ? ui->check_save(ui->host, s) : 0;
			if (refusal != 0) {
				ok_popup(ui, kp_ui_tbl(ui, KP_UI_GLUALL, refusal), NULL); /* the screen stays */
				return 1;
			}
			m->save_choice = s;
		}
		kp_ui_glue_slide_out(ui, self);
		return 1;
	case SLIDE_DONE:
		refused_popup(ui);
		break;
	case KP_UI_POST_LOAD: {
		static const uint16_t SLIDES[6] = { 1, 0, 2, 3, 3, 2 }; /* 0x508160 */
		KpUiInteract procs[7];
		memset(procs, 0, sizeof procs);
		procs[3] = procs[4] = procs[6] = kp_ui_text_button_proc; /* 0x508178 */
		procs[5] = save_list_proc;
		m->load_dialog = self;
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
	mn(ui)->load_list = -1;
	return open_screen(ui, "rez\\gluLoad.bin", load_proc, load_done);
}

/* --- the gluCustm screen: Play Custom (glue state 11) and Load Replay (12) (RunCreateGameScreen 0x455550 with ECX =
 * the replay mode, rez\gluCustm.bin; atlas P18, P19, P20) ---
 * One screen for both. The list is the listed folder's (menus.map_dir under the mode's root: Load Replay's
 * maps\replays, Play Custom's maps): "[Up One Level]" below the root, its folders as "[name]", then its files
 * (BuildMapList 0x45ba40, ScanMapDirectory 0x45c9a0 with flags 0x28, InsertMapEntrySorted 0x45b3f0). An entry's
 * information shows 100 ms after its selection (PreviewMapEntry 0x45b970); the game type and subtype lists offer the
 * templates; the slots' computers and races follow the selected map (RefreshComputerSlots 0x456420). Ok on a folder
 * enters it (OnCreateGameOk 0x455240's 0x8000000b), on a replay plays it, on a map creates its game with the screen's
 * template and races. */

#define ID_CREATE_LIST 5
#define ID_CREATE_FOLDER 6
#define R_CREATE_OK 12
#define R_CREATE_CANCEL 13
#define ID_SUBTYPE_LABEL 14     /* the subtype's label (0x669730) */
#define ID_GAME_TYPE 17
#define ID_SUBTYPE 18
#define ID_LOCAL_PLAYER 20      /* RefreshComputerSlots' first: the player's own slot */
#define ID_FIRST_SLOT 21        /* the computers' slot types 21..27, each with its race list RACE_LIST_OFFSET on */
#define ID_LAST_SLOT 27
#define ID_LOCAL_RACE 28        /* its label: the player's race; the siblings below it are the slots */
#define ID_GAME_TYPE_LABEL 38
#define RACE_LIST_OFFSET 8
#define COMBO_EVENTS 0x20000u   /* the combo boxes' post-load flag (LocalPlayerListProc 0x456120 and the others) */
#define SLOT_TIMER 5            /* RefreshComputerSlots 0x456420's own timer while the map is unread */
#define SLOT_PERIOD 100
#define PREVIEW_TIMER 12        /* PreviewMapEntry 0x45b970's SetTimer(12, 100) */
#define PREVIEW_PERIOD 100
#define CREATE_GRP "glue\\create\\iCreate.grp"
#define ICON_FOLDER 2           /* DrawFolderLabel 0x455fe0 */
#define ICON_FILE 3             /* DrawMapListItem 0x455850: 3 + highlighted */
#define CREATE_FORMAT 0x11      /* the layout flags 0x6d300e of the list's and the label's text */
#define STR_UP_ONE_LEVEL 28     /* gluAll: "Up One Level", in brackets (0x66adf0) */
#define REPLAY_ROOT_LABEL "replays" /* SetFolderLabel 0x4560d0 of maps\replays: its last name */
#define CUSTOM_ROOT_LABEL "maps"
#define CUSTOM_EXPANSION_DIR "BroodWar" /* InitMapList 0x455aa0's first visit with Brood War */
#define STR_INVALID_SCENARIO 61
#define STR_MISSING 66
#define STR_MAP_NAME 48
#define STR_REPLAY 178
#define STR_COUNT 30            /* "%u" */
#define STR_DIMENSIONS 32       /* "%ux%u" */
#define STR_COMPUTER_COUNT 35
#define STR_HUMAN_COUNT 37
#define STR_TILESET_FIRST 39    /* Badlands .. Twilight */
#define TILESETS 8
#define TILESET_INSTALLATION 2
/* OnCreateGameOk 0x455240's messages of CreateGameFromEntry 0x45bfc0's results (gluAll) */
#define STR_CREATE_EXPANSION 60 /* 0x80000001 */
#define STR_CREATE_UMS_ONLY 59  /* 0x80000002 */
#define STR_CREATE_INVALID 51   /* 0x80000007 */
#define STR_CREATE_TEMPLATE 54  /* 0x80000008 */
#define STR_CREATE_NO_HUMAN 58  /* 0x8000000c */
#define STR_CREATE_NO_COMPUTER 57 /* 0x8000000d; FinishSinglePlayerCreate 0x4554a0's too */
#define STR_CREATE_FAILED 52    /* the other results offline */
#define TEMPLATE_TOP_VS_BOTTOM 0x0f
#define TEMPLATE_SUBTYPE_TEXTS 9 /* FormatTemplateSubtype 0x447bb0: glue string 0xa4 + value */
#define STR_SUBTYPE_FIRST 0xa4
#define STR_TOP_VS_BOTTOM 0xac  /* "%u vs %u" */
#define SUBTYPE_CHARS 15        /* AddSubtypeItem 0x456720's 16-byte buffer */
#define TEMPLATE_SERVER 0x80    /* a Battle.net server template's type (0x447cc0, with a sub-byte) */
#define TEMPLATE_LADDER_FIRST 0x11 /* 0x447ed0: the types [0x11, 0x16] need a map of class 6 */
#define TEMPLATE_LADDER_LAST 0x16
#define MAP_CLASS_TOURNAMENT 2
#define MAP_CLASS_SERVER 5
#define MAP_CLASS_LADDER 6
#define SLOT_COMPUTER 1         /* the slot types 0x503d9c: {0 Closed, 1 Computer} */
#define RACE_RANDOM 6
#define RACE_DEFAULT_ITEM 3     /* FillRaceList 0x456170 selects Random */

static const struct { uint8_t value; uint16_t text; } SLOT_TYPES[2] = { { 0, 0x86 }, { 1, 0x84 } }; /* 0x503d9c */
static const struct { uint8_t race; uint8_t text; } RACES[4] = { { 0, 7 }, { 1, 8 }, { 2, 9 }, { 6, 10 } }; /* 0x503da4 */

/* The information fields and their controls (RegisterMapInfoField 0x45c6a0 from InitMapList 0x455aa0): the title,
 * the description, then players, size, computers, humans and tileset with SetMapInfoText 0x455d10's labels. Fields 4,
 * 6 and 7 are shown by the selected template (the players outside Use Map Settings, the computers and humans in it);
 * 5 and 8 keep the dialog's. */
enum { FIELD_PLAYERS, FIELD_SIZE, FIELD_COMPUTERS, FIELD_HUMANS, FIELD_TILESET, FIELD_LINES };
enum { SHOWN_KEPT, SHOWN_OUTSIDE_UMS, SHOWN_IN_UMS };
static const struct { int16_t control; uint16_t label; uint8_t shown; } INFO_LINES[FIELD_LINES] = {
	{ 9, 0x1f, SHOWN_OUTSIDE_UMS }, /* field 4 */
	{ 10, 0x21, SHOWN_KEPT },       /* field 5 */
	{ 0x24, 0x24, SHOWN_IN_UMS },   /* field 6 */
	{ 0x25, 0x26, SHOWN_IN_UMS },   /* field 7 */
	{ 0xb, 0x22, SHOWN_KEPT },      /* field 8 */
};
#define ID_INFO_TITLE 7
#define ID_INFO_DESCRIPTION 8
#define ID_INFO_UNVERIFIED 39   /* Koprulu's labels (defs/ui/koprulu-dialogs.json's extend), 39 + reason - 1: */
#define INFO_UNVERIFIED_LABELS 3 /* loads, another revision, other data (KP_UI_REPLAY_LOADS..KP_UI_REPLAY_DATA) */
#define R_REPLAY_IMPORT 42      /* Koprulu's buttons (the same extend), shown when the host has a file picker: Load */
#define R_REPLAY_EXPORT 43      /* Replay's under the list, */
#define R_CUSTOM_IMPORT 44      /* Play Custom's above Ok (the slots fill the space under its list) */
#define CREATE_CONTROLS R_CUSTOM_IMPORT

static KpUiCreate *cr(KpUi *ui) { return &ui->menus.create; }

static unsigned char ascii_upper(unsigned char ch) { return ch >= 'a' && ch <= 'z' ? (unsigned char)(ch - 32) : ch; }

static uint32_t record16(const uint8_t *r, int at) { return (uint32_t)r[at] | (uint32_t)r[at + 1] << 8; }

static uint32_t record32(const uint8_t *r, int at) { return record16(r, at) | record16(r, at + 2) << 16; }

/* FindTemplateRecord 0x447b50 of a list item's type (the type and its sub-byte << 8) and a subtype (NULL none). */
static const KpTemplate *find_template(const KpUi *ui, uint32_t type, uint32_t subtype)
{
	return ui->templates != NULL
		? kp_templates_find(ui->templates, (int)(type & 0xff), (int)(type >> 8 & 0xff), (int)(subtype & 0xffff)) : NULL;
}

/* IsTemplateMelee 0x447d30: the victory byte 1 */
static int template_melee(const KpUi *ui, uint32_t type, uint32_t subtype)
{
	const KpTemplate *t = find_template(ui, type, subtype);
	return t != NULL && t->record[KP_TEMPLATE_VICTORY] == KP_VICTORY_MELEE;
}

/* IsTemplateUms 0x447de0: no victory, no starting units, no tournament */
static int template_ums(const KpUi *ui, uint32_t type, uint32_t subtype)
{
	const KpTemplate *t = find_template(ui, type, subtype);
	return t != NULL && kp_template_ums(t->record);
}

static int selected_ums(KpUi *ui) { return template_ums(ui, cr(ui)->game_type, cr(ui)->subtype); }

/* IsTemplateSelectable 0x4479e0 off Battle.net: no tournament, no server template, offline a restriction other than 0
 * and 1 */
static int template_selectable(const uint8_t *r, int multiplayer)
{
	if (r[KP_TEMPLATE_TOURNAMENT] != 0) return 0;
	if (r[0] == TEMPLATE_SERVER && r[KP_TEMPLATE_SUB] != 0) return 0;
	return multiplayer || (r[KP_TEMPLATE_RESTRICTION] != 0 && r[KP_TEMPLATE_RESTRICTION] != 1);
}

/* The entry of the map list's selected item (GetMapEntryInfo 0x45beb0 finds it), or -1. */
static int selected_entry(KpUi *ui)
{
	KpUiMenus *m = mn(ui);
	int s = m->map_list >= 0 ? kp_ui_list_selection(ui, m->map_list) : KP_UI_LIST_NONE, k;
	if (s == KP_UI_LIST_NONE) return -1;
	k = (int)kp_ui_list_data(ui, m->map_list, s);
	return k >= 0 && k < m->entry_count ? k : -1;
}

/* SelectListItemByText 0x4c7090: the first item whose text the given one begins, the ASCII letters folded, selected
 * (its index); else no item selected (KP_UI_LIST_NONE). An empty text matches none. */
static int select_text(KpUi *ui, int32_t list, const char *text)
{
	int n = kp_ui_list_count(ui, list), k;
	for (k = 0; k < n && text[0] != 0; k++) {
		const uint8_t *t = kp_ui_list_text(ui, list, k);
		size_t i;
		if (t[0] == 0) continue;
		for (i = 0; text[i] != 0 && ascii_upper(t[i]) == ascii_upper((unsigned char)text[i]); i++) {}
		if (text[i] == 0) {
			kp_ui_list_select(ui, list, k);
			return k;
		}
	}
	kp_ui_list_select(ui, list, KP_UI_LIST_NONE);
	return KP_UI_LIST_NONE;
}

/* SaveGameTypeSelection 0x454e10, SubtypeListProc 0x456780's code 1: the selected item's text as the registry's value */
static void save_choice(KpUi *ui, int32_t list, char value[KP_UI_TEMPLATE_TEXT])
{
	int s = kp_ui_list_selection(ui, list);
	if (s == KP_UI_LIST_NONE) return;
	(void)kp_text_copy(value, KP_UI_TEMPLATE_TEXT, (const char *)kp_ui_list_text(ui, list, s));
	cr(ui)->registry_changed = 1;
}

static void info_text(KpUi *ui, int16_t id, const uint8_t *text)
{
	int32_t c = kp_ui_find_control(ui, mn(ui)->create_dialog, id);
	if (c >= 0) kp_ui_set_control_text(ui, c, text);
}

static void fill_subtypes(KpUi *ui);

/* ApplyEntryFixedSettings 0x454e50: a read replay's game type is its own (offline its list and label hidden, the
 * subtype list showing the replay's and locked); any other entry leaves them to the player. */
static void fixed_settings(KpUi *ui)
{
	KpUiMenus *m = mn(ui);
	int k = selected_entry(ui), at, n;
	int32_t type, subtype, label;
	const KpUiMapEntry *e;
	if (k < 0) return;
	e = &m->entries[k];
	type = kp_ui_find_control(ui, m->create_dialog, ID_GAME_TYPE);
	subtype = kp_ui_find_control(ui, m->create_dialog, ID_SUBTYPE);
	label = kp_ui_find_control(ui, m->create_dialog, ID_GAME_TYPE_LABEL);
	if (e->loaded && e->kind == KP_UI_ENTRY_REPLAY) {
		if (type >= 0) kp_ui_hide_control(ui, type);
		if (label >= 0) kp_ui_hide_control(ui, label);
		if (subtype >= 0) {
			n = kp_ui_list_count(ui, subtype);
			for (at = 0; at < n && kp_ui_list_data(ui, subtype, at) != e->game_subtype; at++) {}
			if (at < n && at != kp_ui_list_selection(ui, subtype)) {
				(void)kp_ui_send_user(ui, subtype, KP_UI_VALUE_CHANGED, (uint32_t)at);
			}
			kp_ui_set_enabled(ui, subtype, 0);
		}
	} else {
		if (type >= 0) {
			kp_ui_show_control(ui, type);
			kp_ui_set_enabled(ui, type, 1);
		}
		if (label >= 0) kp_ui_show_control(ui, label);
		if (subtype >= 0) kp_ui_set_enabled(ui, subtype, 1);
	}
}

/* OnMapSlotCountChanged 0x455710 (PublishMapInfo's tail, while the screen runs): Top vs Bottom's subtypes follow the
 * slots */
static void map_slot_count(KpUi *ui, uint8_t slots)
{
	KpUiCreate *c = cr(ui);
	if (!c->active) return;
	c->slots = slots;
	if ((c->game_type & 0xff) == TEMPLATE_TOP_VS_BOTTOM) fill_subtypes(ui);
	fixed_settings(ui);
}

/* ClearMapInfoFields 0x45b5e0 (k = -1) and PublishMapInfo 0x45dfc0 of entry k (a folder's fields are empty): the
 * title, the description and the lines through SetMapInfoText 0x455d10, the entry's User Select slots, then
 * OnMapSlotCountChanged 0x455710 with the slots of the replay read last, else the entry's. */
static void publish_info(KpUi *ui, int k)
{
	KpUiMenus *m = mn(ui);
	KpUiCreate *c = &m->create;
	const KpUiMapEntry *r = k >= 0 && k < m->entry_count ? &m->entries[k] : NULL;
	char value[FIELD_LINES][64];
	int j, u, ums;
	memset(value, 0, sizeof value);
	m->map_title[0] = m->map_description[0] = 0;
	if (r != NULL && (r->kind == KP_UI_ENTRY_REPLAY || r->kind == KP_UI_ENTRY_MAP)) {
		int replay = r->kind == KP_UI_ENTRY_REPLAY;
		/* LoadReplayEntryInfo 0x45d250, LoadMapEntryInfo 0x45d870 */
		if (r->status != 0) {
			uint16_t title = !replay || r->description == STR_MISSING ? STR_INVALID_SCENARIO : STR_REPLAY;
			(void)kp_text_copy_chars((char *)m->map_title, sizeof m->map_title,
				(const char *)kp_ui_tbl(ui, KP_UI_GLUALL, title), KP_UI_MAP_TITLE_CHARS);
			(void)kp_text_copy((char *)m->map_description, sizeof m->map_description,
				(const char *)kp_ui_tbl(ui, KP_UI_GLUALL, r->description));
		} else {
			if (replay) {
				(void)kp_text_copy_chars((char *)m->map_title, sizeof m->map_title,
					(const char *)kp_ui_tbl(ui, KP_UI_GLUALL, STR_REPLAY), KP_UI_MAP_TITLE_CHARS);
				snprintf((char *)m->map_description, sizeof m->map_description,
					(const char *)kp_ui_tbl(ui, KP_UI_GLUALL, STR_MAP_NAME), r->map);
				strncat((char *)m->map_description, "\n\n",
					sizeof m->map_description - strlen((char *)m->map_description) - 1);
			} else {
				(void)kp_text_copy_chars((char *)m->map_title, sizeof m->map_title, r->map, KP_UI_MAP_TITLE_CHARS);
				(void)kp_text_copy((char *)m->map_description, sizeof m->map_description, r->text);
			}
			snprintf(value[FIELD_PLAYERS], sizeof value[0], (const char *)kp_ui_tbl(ui, KP_UI_GLUALL, STR_COUNT),
				replay ? 1u : (unsigned)r->players);
			snprintf(value[FIELD_SIZE], sizeof value[0], (const char *)kp_ui_tbl(ui, KP_UI_GLUALL, STR_DIMENSIONS),
				(unsigned)r->width, (unsigned)r->height);
			snprintf(value[FIELD_COMPUTERS], sizeof value[0], (const char *)kp_ui_tbl(ui, KP_UI_GLUALL, STR_COMPUTER_COUNT),
				(unsigned)r->computers);
			snprintf(value[FIELD_HUMANS], sizeof value[0], (const char *)kp_ui_tbl(ui, KP_UI_GLUALL, STR_HUMAN_COUNT),
				(unsigned)r->humans);
			if (r->tileset < TILESETS) {
				snprintf(value[FIELD_TILESET], sizeof value[0], "%s",
					(const char *)kp_ui_tbl(ui, KP_UI_GLUALL, (uint32_t)(STR_TILESET_FIRST + r->tileset)));
			}
		}
	}
	info_text(ui, ID_INFO_TITLE, m->map_title);
	info_text(ui, ID_INFO_DESCRIPTION, m->map_description);
	for (u = 0; u < INFO_UNVERIFIED_LABELS; u++) { /* the reason's label alone */
		j = kp_ui_find_control(ui, m->create_dialog, (int16_t)(ID_INFO_UNVERIFIED + u));
		if (j >= 0) {
			kp_ui_set_visible(ui, j, r != NULL && r->kind == KP_UI_ENTRY_REPLAY && r->status == 0
				&& r->unverified == KP_UI_REPLAY_LOADS + u);
		}
	}
	ums = selected_ums(ui);
	for (j = 0; j < FIELD_LINES; j++) { /* SetMapInfoText 0x455d10: the label is the format, 0x12 aligns the value */
		const char *label = (const char *)kp_ui_tbl(ui, KP_UI_GLUALL, INFO_LINES[j].label);
		int32_t ctl = kp_ui_find_control(ui, m->create_dialog, INFO_LINES[j].control);
		if (strlen(label) + strlen(value[j]) < sizeof m->map_lines[0]) {
			snprintf((char *)m->map_lines[j], sizeof m->map_lines[j], label, 0x12, value[j]);
		} /* else the line keeps its old text: 0x455d51 skips SetControlText */
		if (ctl < 0) continue;
		kp_ui_set_control_text(ui, ctl, m->map_lines[j]);
		if (INFO_LINES[j].shown != SHOWN_KEPT) kp_ui_set_visible(ui, ctl, (INFO_LINES[j].shown == SHOWN_IN_UMS) == ums);
	}
	if (r == NULL) {
		memset(c->user_select, 0, sizeof c->user_select); /* ClearUserSelectFlags 0x479c80 */
		return;
	}
	memcpy(c->user_select, r->user_select, sizeof c->user_select); /* LoadUserSelectFlags 0x479cc0 */
	map_slot_count(ui, c->replay_read ? c->replay_slots : r->players);
}

/* LoadReplayEntryInfo 0x45d250 (the replay read flag 0x67cc90 set before its loaded test) and LoadMapEntryInfo
 * 0x45d870 (the map's header read: LoadMapHeader 0x46b5c0's players) of entry k, once; the host read a replay's with
 * the list, a map's in load_entry. */
static void read_entry(KpUi *ui, int k)
{
	KpUiCreate *c = cr(ui);
	KpUiMapEntry *e = &mn(ui)->entries[k];
	if (e->kind == KP_UI_ENTRY_REPLAY) c->replay_read = 1;
	if (e->loaded || (e->kind != KP_UI_ENTRY_REPLAY && e->kind != KP_UI_ENTRY_MAP)) return;
	if (e->kind == KP_UI_ENTRY_REPLAY && e->status == 0) c->replay_slots = e->slots; /* its header read (0x480690) */
	if (e->kind == KP_UI_ENTRY_MAP) {
		if (ui->load_entry != NULL) {
			ui->load_entry(ui->host, ui, k);
		} else { /* no host to read it: as a missing file */
			e->status = 1;
			e->description = STR_MISSING;
		}
		c->header_human = e->status == 0 ? e->first_human : -1;
	}
	e->loaded = 1;
}

/* DeferredPreviewTimer 0x45d1e0: the entry's information 100 ms after its selection. */
static void preview_tick(KpUi *ui, int32_t owner)
{
	KpUiMenus *m = mn(ui);
	int k = m->map_preview;
	kp_ui_delete_timer(&ui->timers, owner, PREVIEW_TIMER);
	m->create.replay_read = 0;
	m->map_preview = -1;
	if (k < 0 || k >= m->entry_count) return; /* the list was built again under it */
	read_entry(ui, k);
	publish_info(ui, k);
}

/* PreviewMapEntry 0x45b970: an unread map's or replay's fields cleared and its information 100 ms later; any other
 * entry's at once. */
static void preview_entry(KpUi *ui, int k)
{
	KpUiMenus *m = mn(ui);
	const KpUiMapEntry *e;
	kp_ui_delete_timer(&ui->timers, m->create_dialog, PREVIEW_TIMER);
	if (k < 0 || k >= m->entry_count) return;
	e = &m->entries[k];
	if ((e->kind == KP_UI_ENTRY_MAP || e->kind == KP_UI_ENTRY_REPLAY) && !e->loaded) {
		publish_info(ui, -1);
		m->map_preview = k;
		(void)kp_ui_set_timer(&ui->timers, m->create_dialog, PREVIEW_TIMER, PREVIEW_PERIOD,
			kp_ui_callback(ui, preview_tick), ui->now);
		return;
	}
	publish_info(ui, k);
}

/* ConfigureSlotControls 0x456350: a slot's type usable (a melee template's), else locked to a computer; its race list
 * shown for a usable computer. */
static void configure_slot(KpUi *ui, int32_t list, int configurable)
{
	int visible = configurable && (ui->controls[list].flags & KP_UI_VISIBLE);
	int s = kp_ui_list_selection(ui, list), n, k;
	int32_t race;
	if (!configurable && s != KP_UI_LIST_NONE && kp_ui_list_data(ui, list, s) != SLOT_COMPUTER) {
		n = kp_ui_list_count(ui, list);
		for (k = 0; k < n && kp_ui_list_data(ui, list, k) != SLOT_COMPUTER; k++) {}
		if (k < n) kp_ui_list_select(ui, list, k);
	}
	kp_ui_set_enabled(ui, list, configurable);
	s = kp_ui_list_selection(ui, list);
	race = kp_ui_find_control(ui, ui->controls[list].parent, (int16_t)(ui->controls[list].id + RACE_LIST_OFFSET));
	if (race >= 0) kp_ui_set_visible(ui, race, visible && s != KP_UI_LIST_NONE && kp_ui_list_data(ui, list, s) != 0);
}

static void slot_tick(KpUi *ui, int32_t owner);

/* RefreshComputerSlots 0x456420 (offline): the player's slot and as many computer slots as a melee template's map
 * has other slots, or another template's has computers (locked); the rest hidden. The player's race list is the label:
 * shown for a melee template, or while the list leads for a map whose first human slot is User Select, at Ok for the
 * player's slot being so. While the selected map is unread the refresh waits for it (100 ms). */
static void refresh_slots(KpUi *ui, int32_t dialog)
{
	KpUiMenus *m = mn(ui);
	KpUiCreate *c = &m->create;
	int k = selected_entry(ui), melee = 0, s, n, user;
	uint8_t total = 0, avail = 0;
	int32_t first, label, x;
	if (m->multiplayer) return;
	kp_ui_delete_timer(&ui->timers, dialog, SLOT_TIMER);
	if (k >= 0 && m->entries[k].kind != KP_UI_ENTRY_REPLAY) { /* 0xff each while unread (0x45beb0) */
		avail = m->entries[k].loaded ? m->entries[k].computers : 0xff;
		total = m->entries[k].loaded ? m->entries[k].players : 0xff;
	}
	if (c->loading && total == 0xff) {
		(void)kp_ui_set_timer(&ui->timers, dialog, SLOT_TIMER, SLOT_PERIOD, kp_ui_callback(ui, slot_tick), ui->now);
		return;
	}
	first = kp_ui_find_control(ui, dialog, ID_LOCAL_PLAYER);
	label = kp_ui_find_control(ui, dialog, ID_LOCAL_RACE);
	if (first < 0 || label < 0) return;
	s = c->type_list >= 0 ? kp_ui_list_selection(ui, c->type_list) : KP_UI_LIST_NONE;
	x = first;
	if (total != 0 && s != KP_UI_LIST_NONE) {
		melee = template_melee(ui, kp_ui_list_data(ui, c->type_list, s), 1);
		kp_ui_show_control(ui, first);
		if (c->loading) user = c->header_human >= 0 && c->header_human < KP_UI_SLOT_FLAGS && c->user_select[c->header_human];
		else user = c->local_slot >= 0 && c->local_slot < KP_UI_SLOT_FLAGS && c->user_select[c->local_slot];
		kp_ui_set_visible(ui, label, melee || user);
		n = melee ? (uint8_t)(total - 1) : avail;
		x = ui->controls[first].next;
		for (; n > 0; n--) {
			if (x < 0) return;
			if (ui->controls[x].id >= ID_LOCAL_RACE) break;
			kp_ui_show_control(ui, x);
			configure_slot(ui, x, melee);
			x = ui->controls[x].next;
		}
	}
	for (; x >= 0 && ui->controls[x].id < ID_LOCAL_RACE; x = ui->controls[x].next) {
		kp_ui_hide_control(ui, x);
		configure_slot(ui, x, melee);
	}
}

static void slot_tick(KpUi *ui, int32_t owner) { refresh_slots(ui, owner); }

/* AddSubtypeItem 0x456720: a template of the selected type with a value (Top vs Bottom's below the map's slots), its
 * text by FormatTemplateSubtype 0x447bb0, its data the subtype */
static void add_subtype_item(KpUi *ui, int32_t list, const uint8_t *r)
{
	KpUiCreate *c = cr(ui);
	uint32_t value = record32(r, KP_TEMPLATE_VALUE);
	char full[128], text[64];
	int at;
	if (value == 0) return;
	if (r[0] == TEMPLATE_TOP_VS_BOTTOM && (c->slots < 2 || value >= c->slots)) return;
	if (r[0] == TEMPLATE_SUBTYPE_TEXTS) {
		snprintf(full, sizeof full, "%s", (const char *)kp_ui_tbl(ui, KP_UI_GLUALL, STR_SUBTYPE_FIRST + value));
	} else if (r[0] == TEMPLATE_TOP_VS_BOTTOM) {
		snprintf(full, sizeof full, (const char *)kp_ui_tbl(ui, KP_UI_GLUALL, STR_TOP_VS_BOTTOM), (unsigned)value,
			(unsigned)(c->slots - value));
	} else {
		snprintf(full, sizeof full, "%u", (unsigned)value);
	}
	(void)kp_text_copy_chars(text, sizeof text, full, SUBTYPE_CHARS);
	at = kp_ui_list_add(ui, list, (const uint8_t *)text, 0x10);
	if (at != KP_UI_LIST_NONE) kp_ui_list_set_data(ui, list, at, record16(r, KP_TEMPLATE_SUBTYPE));
}

/* FillSubtypeList 0x456830: the selected type's subtypes, the registry's "Game Subtype" chosen (else the first; Top vs
 * Bottom's middle one), the list shown with them, hidden without (the subtype 1). */
static void fill_subtypes(KpUi *ui)
{
	KpUiCreate *c = cr(ui);
	int32_t list = c->subtype_list;
	int k, n;
	if (list < 0) return;
	kp_ui_list_redraw(ui, list, 0);
	kp_ui_list_clear(ui, list);
	for (k = 0; ui->templates != NULL && k < ui->templates->count; k++) { /* ForEachTemplateOfType 0x447ee0 */
		const uint8_t *r = ui->templates->list[k].record;
		if (r[0] == (c->game_type & 0xff) && r[KP_TEMPLATE_SUB] == (c->game_type >> 8 & 0xff)) add_subtype_item(ui, list, r);
	}
	kp_ui_list_redraw(ui, list, 1);
	n = kp_ui_list_count(ui, list);
	if (n == 0) {
		kp_ui_hide_control(ui, list);
		c->subtype = 1;
		return;
	}
	if (c->game_subtype[0] == 0 || select_text(ui, list, c->game_subtype) == KP_UI_LIST_NONE) kp_ui_list_select(ui, list, 0);
	if ((c->game_type & 0xff) == TEMPLATE_TOP_VS_BOTTOM) kp_ui_list_select(ui, list, n / 2);
	kp_ui_show_control(ui, list);
	c->subtype = (uint16_t)kp_ui_list_data(ui, list, kp_ui_list_selection(ui, list));
}

/* OnGameTypeSelected 0x456a40: the type, its subtype label (FindTemplateLabel 0x447ac0 and ':'), its subtypes, the
 * template's record (0x67293d), the slots again, and the selected entry's information again. */
static void on_type_selected(KpUi *ui, int32_t list)
{
	KpUiMenus *m = mn(ui);
	KpUiCreate *c = &m->create;
	int32_t parent = ui->controls[list].parent, label, entries;
	const KpTemplate *t;
	char text[KP_UI_TEMPLATE_TEXT * 3 + 1];
	int s, k;
	size_t chars;
	if (kp_ui_list_count(ui, list) == 0 || (s = kp_ui_list_selection(ui, list)) == KP_UI_LIST_NONE) return;
	c->game_type = kp_ui_list_data(ui, list, s);
	text[0] = 0;
	for (k = 0; ui->templates != NULL && k < ui->templates->count; k++) {
		const uint8_t *r = ui->templates->list[k].record;
		if (r[0] == (c->game_type & 0xff) && r[KP_TEMPLATE_SUB] == (c->game_type >> 8 & 0xff)) {
			(void)kp_text_from_legacy(ui->templates->list[k].label, text, sizeof text);
			break;
		}
	}
	/* SStrCopy of 32 bytes, then SStrNCat of ':' within them */
	chars = kp_text_copy_chars((char *)c->type_label, sizeof c->type_label - 1, text, KP_UI_TEMPLATE_TEXT - 1);
	if (c->type_label[0] != 0 && chars < KP_UI_TEMPLATE_TEXT - 1) strcat((char *)c->type_label, ":");
	if ((label = kp_ui_find_control(ui, parent, ID_SUBTYPE_LABEL)) >= 0) kp_ui_set_control_text(ui, label, c->type_label);
	fill_subtypes(ui);
	if ((t = find_template(ui, c->game_type, c->subtype)) != NULL) memcpy(c->record, t->record, sizeof c->record);
	if (!m->multiplayer) {
		c->loading = 1;
		refresh_slots(ui, parent);
	}
	entries = kp_ui_find_control(ui, parent, ID_CREATE_LIST);
	if (entries >= 0 && (s = kp_ui_list_selection(ui, entries)) != KP_UI_LIST_NONE) {
		preview_entry(ui, (int)kp_ui_list_data(ui, entries, s));
	}
}

/* FillGameTypeList 0x456910: the templates of subtype 1 the player may choose (AddGameTypeItem 0x456990, the name
 * and the type), the registry's "Custom Type" chosen (RestoreGameTypeSelection 0x454d40), else offline the last melee
 * template from the bottom; an empty list disabled. */
static void fill_types(KpUi *ui, int32_t list)
{
	KpUiMenus *m = mn(ui);
	KpUiCreate *c = &m->create;
	int k, n, restored = 0;
	for (k = 0; ui->templates != NULL && k < ui->templates->count; k++) {
		const KpTemplate *t = &ui->templates->list[k];
		char name[KP_UI_TEMPLATE_TEXT * 3 + 1];
		int at;
		if (record16(t->record, KP_TEMPLATE_SUBTYPE) != 1 || !template_selectable(t->record, m->multiplayer)) continue;
		(void)kp_text_from_legacy(t->name, name, sizeof name); /* the template's name: the list's own copy */
		at = kp_ui_list_add(ui, list, (const uint8_t *)name, 0x10);
		if (at != KP_UI_LIST_NONE) kp_ui_list_set_data(ui, list, at, t->record[0]);
	}
	n = kp_ui_list_count(ui, list);
	if (n == 0) {
		kp_ui_set_enabled(ui, list, 0);
		return;
	}
	if (c->custom_type[0] != 0 && select_text(ui, list, c->custom_type) != KP_UI_LIST_NONE) restored = 1;
	else kp_ui_list_select(ui, list, 0);
	if (!m->multiplayer && !restored) {
		for (k = n - 1; k >= 0; k--) {
			if (template_melee(ui, kp_ui_list_data(ui, list, k), 1)) {
				kp_ui_list_select(ui, list, k);
				break;
			}
		}
	}
	on_type_selected(ui, list);
}

static int type_list_proc(KpUi *ui, KpUiControl *c, KpUiEvent *e)
{
	/* GameTypeListProc 0x4569d0 */
	int32_t self = (int32_t)(c - ui->controls);
	if (e->type == KP_UI_USER) {
		switch (e->code) {
		case KP_UI_DESTROY: save_choice(ui, self, cr(ui)->custom_type); break;
		case KP_UI_POST_LOAD: c->flags |= COMBO_EVENTS; break;
		case KP_UI_VALUE_CHANGED:
			(void)kp_ui_combo_proc(ui, c, e);
			on_type_selected(ui, self);
			return 1;
		case SLIDE_DONE: fill_types(ui, self); break;
		default: break;
		}
	}
	return kp_ui_combo_proc(ui, c, e);
}

static int subtype_list_proc(KpUi *ui, KpUiControl *c, KpUiEvent *e)
{
	/* SubtypeListProc 0x456780 */
	int32_t self = (int32_t)(c - ui->controls);
	int s;
	if (e->type == KP_UI_USER) {
		switch (e->code) {
		case KP_UI_DESTROY: save_choice(ui, self, cr(ui)->game_subtype); break;
		case KP_UI_POST_LOAD:
			c->flags |= COMBO_EVENTS;
			kp_ui_hide_control(ui, self);
			cr(ui)->subtype = 1;
			break;
		case KP_UI_VALUE_CHANGED:
			(void)kp_ui_combo_proc(ui, c, e);
			if (kp_ui_list_count(ui, self) != 0 && (s = kp_ui_list_selection(ui, self)) != KP_UI_LIST_NONE) {
				cr(ui)->subtype = (uint16_t)kp_ui_list_data(ui, self, s);
			}
			return 1;
		default: break;
		}
	}
	return kp_ui_combo_proc(ui, c, e);
}

static int local_player_proc(KpUi *ui, KpUiControl *c, KpUiEvent *e)
{
	/* LocalPlayerListProc 0x456120: the player's name, its one item */
	int32_t self = (int32_t)(c - ui->controls);
	if (e->type == KP_UI_USER && e->code == KP_UI_CREATE) {
		kp_ui_list_redraw(ui, self, 0);
		(void)kp_ui_list_add(ui, self, (const uint8_t *)mn(ui)->player, 0);
		kp_ui_list_redraw(ui, self, 1);
	} else if (e->type == KP_UI_USER && e->code == KP_UI_POST_LOAD) {
		c->flags |= COMBO_EVENTS;
	}
	return kp_ui_combo_proc(ui, c, e);
}

static int race_list_proc(KpUi *ui, KpUiControl *c, KpUiEvent *e)
{
	/* RaceListProc 0x4561d0, FillRaceList 0x456170: Zerg, Terran, Protoss, Random (selected) */
	int32_t self = (int32_t)(c - ui->controls);
	int k, at;
	if (e->type == KP_UI_USER && e->code == KP_UI_CREATE) {
		kp_ui_list_redraw(ui, self, 0);
		for (k = 0; k < 4; k++) {
			if ((at = kp_ui_list_add(ui, self, kp_ui_tbl(ui, KP_UI_NETWORK, RACES[k].text), 0)) == KP_UI_LIST_NONE) break;
			kp_ui_list_set_data(ui, self, at, RACES[k].race);
		}
		kp_ui_list_redraw(ui, self, 1);
		kp_ui_list_select(ui, self, RACE_DEFAULT_ITEM);
	} else if (e->type == KP_UI_USER && e->code == KP_UI_POST_LOAD) {
		c->flags |= COMBO_EVENTS;
	}
	return kp_ui_combo_proc(ui, c, e);
}

static int slot_type_proc(KpUi *ui, KpUiControl *c, KpUiEvent *e)
{
	/* SlotTypeListProc 0x456290 */
	int32_t self = (int32_t)(c - ui->controls), race;
	int k, at, n, visible, s;
	if (e->type == KP_UI_USER) {
		switch (e->code) {
		case KP_UI_CREATE: /* FillSlotTypeList 0x456210: Closed, Computer (selected) */
			kp_ui_list_redraw(ui, self, 0);
			for (k = 0; k < 2; k++) {
				if ((at = kp_ui_list_add(ui, self, kp_ui_tbl(ui, KP_UI_GLUALL, SLOT_TYPES[k].text), 0)) == KP_UI_LIST_NONE) break;
				kp_ui_list_set_data(ui, self, at, SLOT_TYPES[k].value);
			}
			kp_ui_list_redraw(ui, self, 1);
			n = kp_ui_list_count(ui, self);
			for (k = 0; k < n && kp_ui_list_data(ui, self, k) != SLOT_COMPUTER; k++) {}
			kp_ui_list_select(ui, self, k < n ? k : 0);
			break;
		case KP_UI_POST_LOAD: c->flags |= COMBO_EVENTS; break;
		case KP_UI_VALUE_CHANGED: /* OnSlotTypeChanged 0x4562e0: the list box's default, enabled, the race list by it */
			(void)kp_ui_list_proc(ui, c, e);
			visible = (c->flags & KP_UI_VISIBLE) != 0;
			kp_ui_set_enabled(ui, self, 1);
			s = kp_ui_list_selection(ui, self);
			race = kp_ui_find_control(ui, c->parent, (int16_t)(c->id + RACE_LIST_OFFSET));
			if (race >= 0) kp_ui_set_visible(ui, race, visible && s != KP_UI_LIST_NONE && kp_ui_list_data(ui, self, s) != 0);
			return 1;
		default: break;
		}
	}
	return kp_ui_combo_proc(ui, c, e);
}

/* An icon of iCreate.grp at x, centred in the rows top..bottom (DrawMapListItem 0x455850, DrawFolderLabel 0x455fe0);
 * its width, 0 without the GRP. */
static int32_t create_icon(KpUi *ui, int frame, int32_t x, int32_t top, int32_t bottom, const int16_t clip[4])
{
	const KpUiFile *f = kp_ui_file(ui->grps, ui->grp_count, CREATE_GRP);
	const uint8_t *px;
	int32_t w, h;
	int16_t rect[4];
	if (f == NULL || kp_ui_dialog_grp_frame(f->data, f->size, frame, &px, &w, &h) != 0) return 0;
	rect[0] = (int16_t)x;
	rect[1] = (int16_t)(top + ((bottom - top + 1) / 2 - h / 2));
	rect[2] = clip[2];
	rect[3] = clip[3];
	kp_ui_blit_clipped(ui, px, w, h, 0, 0, rect, KP_UI_TRANSPARENT);
	return w;
}

static void map_item_draw(KpUi *ui, KpUiControl *c, int index, const int16_t row[4], int32_t dx, int32_t dy)
{
	/* DrawMapListItem 0x455850: the entry's icon (a folder's, else a file's), then its name cut to the row */
	KpUiList *l = c->list;
	int hot = index == l->hover;
	int32_t w, x;
	uint8_t text[KP_UI_MAP_FILE];
	size_t n;
	uint32_t k;
	int frame = 0; /* an item without its entry */
	if (index >= l->count || l->text[index] == NULL) return;
	k = kp_ui_list_data(ui, (int32_t)(c - ui->controls), index);
	if (k < (uint32_t)mn(ui)->entry_count) {
		uint8_t kind = mn(ui)->entries[k].kind;
		frame = kind == KP_UI_ENTRY_FOLDER || kind == KP_UI_ENTRY_UP ? hot : ICON_FILE + hot;
	}
	w = create_icon(ui, frame, row[0] + dx, row[1] + dy, row[3] + dy, row);
	x = dx + w + 2; /* the text x, from the row's left: past the icon drawn at the x offset */
	kp_ui_select_color(&ui->text, 3 + hot);
	ui->text.flags = CREATE_FORMAT;
	ui->text.clip_left = (int16_t)(row[0] + x);
	ui->text.clip_top = (int16_t)(row[1] + dy);
	ui->text.clip_right = (int16_t)(row[2] + x);
	ui->text.clip_bottom = (int16_t)(row[3] + dy);
	n = kp_ui_text_fit_length(&ui->text, l->text[index], (row[2] - row[0]) - x + 1);
	if (n >= sizeof text) n = sizeof text - 1;
	memcpy(text, l->text[index], n);
	text[n] = 0;
	(void)kp_ui_draw_text_told(ui, c, row[0] + x, row[1] + dy, text);
}

static void folder_label_draw(KpUi *ui, KpUiControl *c, int32_t x, int32_t y, const int16_t clip[4])
{
	/* DrawFolderLabel 0x455fe0: the folder's icon, then the folder's name */
	int32_t w = create_icon(ui, ICON_FOLDER, c->left + x, c->top + y, c->bottom + y, clip);
	if (c->text != NULL) kp_ui_draw_control_text(ui, c, x + w + 2, y, clip, CREATE_FORMAT);
}

/* The extension after a name's last '.' is scx or scm (BuildMapList 0x45ba40's map to select). */
static int map_extension(const char *name)
{
	const char *dot = strrchr(name, '.');
	return dot != NULL && ascii_upper((unsigned char)dot[1]) == 'S' && ascii_upper((unsigned char)dot[2]) == 'C'
		&& (ascii_upper((unsigned char)dot[3]) == 'X' || ascii_upper((unsigned char)dot[3]) == 'M') && dot[4] == 0;
}

/* RefreshMapList 0x455f50 (the dialog's code 0x405, its slides ended): the list filled (AddMapListItem 0x455810), the
 * entry to select previewed, then selected. BuildMapList 0x45ba40 selects the first entry whose list text begins the
 * remembered map's (0x668c78, Play Custom's last map; Load Replay keeps what it is), else the first .scx or .scm, else
 * the first entry; Koprulu's import selects the imported file (select). */
static void refresh_list_at(KpUi *ui, const char *select)
{
	KpUiMenus *m = mn(ui);
	KpUiCreate *c = &m->create;
	int chosen = -1, first_map = -1;
	int32_t folder = m->create_dialog >= 0 ? kp_ui_find_control(ui, m->create_dialog, ID_CREATE_FOLDER) : -1;
	const char *last = strrchr(m->map_dir, '/');
	int k;
	if (m->map_list < 0) return;
	if (folder >= 0) { /* SetFolderLabel 0x4560d0: the listed folder's last name */
		const char *root = m->state == STATE_PLAY_CUSTOM ? CUSTOM_ROOT_LABEL : REPLAY_ROOT_LABEL;
		kp_ui_set_control_text(ui, folder, (const uint8_t *)(m->map_dir[0] == 0 ? root : last ? last + 1 : m->map_dir));
	}
	m->entry_count = 0;
	kp_ui_list_redraw(ui, m->map_list, 0);
	kp_ui_list_clear(ui, m->map_list);
	if (ui->list_entries != NULL) (void)ui->list_entries(ui->host, ui);
	for (k = 0; k < m->entry_count; k++) {
		KpUiMapEntry *e = &m->entries[k];
		int at;
		if (e->kind == KP_UI_ENTRY_UP) {
			snprintf(e->label, sizeof e->label, "[%s]", (const char *)kp_ui_tbl(ui, KP_UI_GLUALL, STR_UP_ONE_LEVEL));
		} else {
			/* "[folder]" or the file: lengths and memmove, not snprintf, as both buffers are the entry's, which
			 * GCC's -Wrestrict takes for an overlap under the sanitizers and _FORTIFY_SOURCE (GCC 13 to 15) */
			size_t b = e->kind == KP_UI_ENTRY_FOLDER, f = strlen(e->file);
			if (f + 2 * b >= sizeof e->label) f = sizeof e->label - 1 - 2 * b;
			if (b) e->label[0] = '[';
			memmove(e->label + b, e->file, f);
			if (b) e->label[b + f] = ']';
			e->label[2 * b + f] = 0;
		}
		at = kp_ui_list_add(ui, m->map_list, (const uint8_t *)e->label, 3);
		if (at == KP_UI_LIST_NONE) continue;
		kp_ui_list_set_data(ui, m->map_list, at, (uint32_t)k);
		if (select != NULL) {
			if (chosen < 0 && e->kind == KP_UI_ENTRY_REPLAY && strcmp(e->file, select) == 0) chosen = at;
		} else if (chosen < 0 && c->preselect[0] != 0) {
			size_t n = strlen(e->label), i;
			for (i = 0; i < n && ascii_upper((unsigned char)c->preselect[i]) == ascii_upper((unsigned char)e->label[i]); i++) {}
			if (i == n) chosen = at; /* _strnicmp(preselect, name, strlen(name)) */
		}
		if (first_map < 0 && e->kind != KP_UI_ENTRY_FOLDER && e->kind != KP_UI_ENTRY_UP && map_extension(e->file)) first_map = at;
	}
	kp_ui_list_redraw(ui, m->map_list, 1);
	if (m->entry_count == 0) return;
	if (chosen < 0) chosen = first_map >= 0 ? first_map : 0;
	preview_entry(ui, (int)kp_ui_list_data(ui, m->map_list, chosen));
	kp_ui_list_select(ui, m->map_list, chosen);
}

static void refresh_list(KpUi *ui) { refresh_list_at(ui, NULL); }

static int map_list_proc(KpUi *ui, KpUiControl *c, KpUiEvent *e)
{
	/* MapListProc 0x4559d0 */
	int32_t self = (int32_t)(c - ui->controls);
	int k;
	if (e->type == KP_UI_USER) {
		switch (e->code) {
		case KP_UI_CREATE: /* InitMapList 0x455aa0; the list is filled when the slides end (refresh_list) */
			mn(ui)->map_list = self;
			c->list->draw_item = map_item_draw;
			break;
		case KP_UI_DESTROY: kp_ui_list_clear(ui, self); break;
		case KP_UI_POST_LOAD:
			c->flags |= 0x30000u;
			mn(ui)->map_list = self;
			break;
		case KP_UI_VALUE_CHANGED:
			(void)kp_ui_list_proc(ui, c, e);
			k = kp_ui_list_selection(ui, self);
			if (k != KP_UI_LIST_NONE) preview_entry(ui, (int)kp_ui_list_data(ui, self, k));
			cr(ui)->loading = 1;
			refresh_slots(ui, c->parent);
			return 1;
		default: break;
		}
	}
	return kp_ui_list_proc(ui, c, e);
}

/* AddFolderEntry 0x45b080's path: ".." the listed folder cut at its last separator, a folder's name appended; a
 * path past the buffer leaves the folder as it is. */
static void enter_folder(KpUi *ui, const KpUiMapEntry *e)
{
	KpUiMenus *m = mn(ui);
	if (e->kind == KP_UI_ENTRY_UP) {
		char *cut = strrchr(m->map_dir, '/');
		if (cut != NULL) *cut = 0;
		else m->map_dir[0] = 0;
	} else {
		/* lengths and memmove, not strcat: both buffers are KpUiMenus', which GCC's -Wrestrict takes for an overlap */
		size_t n = strlen(m->map_dir), sep = n != 0, f = strlen(e->file);
		if (n + 1 + f < sizeof m->map_dir) {
			if (sep) m->map_dir[n] = '/';
			memmove(m->map_dir + n + sep, e->file, f + 1);
		}
	}
}

static void create_refused(KpUi *ui, int32_t r) { (void)ui; (void)r; } /* the screen stays (every message case) */

/* Map entry k's class (+0x260), which LoadMapEntryInfo 0x45d870 reads with the header: the host's, asked only here
 * when the template reads it (a signed map's check reads the whole file); 0 without the host's map_class. */
static uint32_t entry_class(KpUi *ui, int k)
{
	KpUiMapEntry *e = &mn(ui)->entries[k];
	e->map_class = ui->map_class != NULL ? ui->map_class(ui->host, ui, k) : 0;
	return e->map_class;
}

/* ValidateGameTemplate 0x45b700 offline: the template's own needs of map entry k (OnCreateGameOk 0x455240's message) */
static uint16_t validate_template(KpUi *ui, int k)
{
	KpUiMenus *m = mn(ui);
	KpUiCreate *c = &m->create;
	const KpUiMapEntry *e = &m->entries[k];
	const KpTemplate *t = find_template(ui, c->game_type, c->subtype);
	if (t == NULL) return STR_CREATE_TEMPLATE;
	if (c->record[0] >= TEMPLATE_LADDER_FIRST && c->record[0] <= TEMPLATE_LADDER_LAST
		&& entry_class(ui, k) != MAP_CLASS_LADDER) {
		return STR_CREATE_FAILED; /* 0x80000012 */
	}
	if (t->record[KP_TEMPLATE_TOURNAMENT] == 1) {
		if (entry_class(ui, k) != MAP_CLASS_TOURNAMENT) return STR_CREATE_FAILED; /* 0x80000003 */
	} else if (t->record[0] == TEMPLATE_SERVER && t->record[KP_TEMPLATE_SUB] != 0) {
		if (!m->expansion || entry_class(ui, k) != MAP_CLASS_SERVER) return STR_CREATE_FAILED; /* 0x80000011, 0x8000000e */
	}
	if (e->tileset == TILESET_INSTALLATION && !template_ums(ui, c->game_type, c->subtype)) return STR_CREATE_UMS_ONLY;
	/* IsTemplateAllowedForMap 0x4480a0: the template's teams fit the map's slots */
	if (t->record[KP_TEMPLATE_TEAMS] != 0 && t->record[KP_TEMPLATE_TEAMS] > e->players) return STR_CREATE_TEMPLATE;
	return 0;
}

/* CreateGameFromEntry 0x45bfc0 offline for a replay or a map: 0 with its game chosen, else the gluAll message. */
static uint16_t create_game(KpUi *ui, int k)
{
	KpUiMenus *m = mn(ui);
	KpUiCreate *c = &m->create;
	KpUiMapEntry *e = &m->entries[k];
	uint16_t refusal;
	int local = -1;
	m->replay_choice = -1;
	c->choice = -1;
	if (e->kind == KP_UI_ENTRY_REPLAY) { /* read again (a status 1 is gluAll 51, 2 gluAll 60) */
		read_entry(ui, k);
		refusal = ui->check_replay != NULL ? ui->check_replay(ui->host, k) : 0;
		if (refusal == 0) m->replay_choice = k;
		return refusal;
	}
	if (!e->loaded) read_entry(ui, k);
	if (e->status == 1) return STR_CREATE_INVALID;
	if (e->status == 2) return STR_CREATE_EXPANSION;
	if (template_ums(ui, c->game_type, c->subtype)) {
		if (e->humans == 0) return STR_CREATE_NO_HUMAN;
		if (e->computers == 0) return STR_CREATE_NO_COMPUTER;
	} else if (e->players < 2) {
		return STR_CREATE_NO_COMPUTER;
	}
	if ((refusal = validate_template(ui, k)) != 0) return refusal;
	/* the header read again (LoadMapHeader 0x46b5c0), the player in the first open slot (PlaceLocalHumanForCustomGame
	 * 0x4bb550), the local game (CreateLocalGame 0x475220): the host's */
	if (ui->check_map != NULL && (refusal = ui->check_map(ui->host, k, &local)) != 0) return refusal;
	c->local_slot = (int8_t)(local >= 0 && local < KP_UI_SLOT_FLAGS ? local : -1);
	c->header_human = e->first_human;
	memcpy(c->user_select, e->user_select, sizeof c->user_select);
	c->choice = k;
	return 0;
}

/* CanStartCustomGame 0x455740: a Use Map Settings map with computers, else a shown slot with a computer */
static int can_start(KpUi *ui)
{
	KpUiMenus *m = mn(ui);
	int32_t x;
	int s, k;
	if (selected_ums(ui)) {
		k = selected_entry(ui);
		return k >= 0 && m->entries[k].kind == KP_UI_ENTRY_MAP && m->entries[k].loaded && m->entries[k].computers != 0
			&& m->entries[k].computers != 0xff;
	}
	for (x = kp_ui_find_control(ui, m->create_dialog, ID_FIRST_SLOT); x >= 0 && ui->controls[x].id <= ID_LAST_SLOT;
		x = ui->controls[x].next) {
		if ((ui->controls[x].flags & KP_UI_VISIBLE) && (s = kp_ui_list_selection(ui, x)) != KP_UI_LIST_NONE
			&& kp_ui_list_data(ui, x, s) == SLOT_COMPUTER) {
			return 1;
		}
	}
	return 0;
}

/* ApplyCustomGameRaces 0x455020: a melee template's races (the player's, then each shown race list's below it in the
 * list's order, KP_UI_RACE_NONE for the rest), another template's player race when its list is shown (Random without
 * a choice). */
static void custom_races(KpUi *ui)
{
	KpUiMenus *m = mn(ui);
	KpUiCreate *c = &m->create;
	int32_t local = kp_ui_find_control(ui, m->create_dialog, ID_LOCAL_RACE), x;
	int s, n = 0, k;
	c->local_race = KP_UI_RACE_NONE;
	for (k = 0; k < KP_UI_CUSTOM_RACES; k++) c->races[k] = KP_UI_RACE_NONE;
	c->melee = (uint8_t)template_melee(ui, c->game_type, c->subtype);
	if (local < 0) return;
	s = kp_ui_list_selection(ui, local);
	if (!c->melee) {
		if (ui->controls[local].flags & KP_UI_VISIBLE) c->local_race = s != KP_UI_LIST_NONE ? kp_ui_list_data(ui, local, s) : RACE_RANDOM;
		return;
	}
	if (s != KP_UI_LIST_NONE) c->local_race = kp_ui_list_data(ui, local, s);
	for (x = ui->controls[local].next; x >= 0 && n < KP_UI_CUSTOM_RACES; x = ui->controls[x].next) {
		if ((ui->controls[x].flags & KP_UI_VISIBLE) && ui->controls[x].list != NULL
			&& (s = kp_ui_list_selection(ui, x)) != KP_UI_LIST_NONE) {
			c->races[n++] = kp_ui_list_data(ui, x, s);
		}
	}
}

/* FinishSinglePlayerCreate 0x4554a0: the slots as Ok sees them, a computer opponent needed unless a replay was read,
 * then the races. 0 keeps the screen open. */
static int finish_create(KpUi *ui)
{
	KpUiMenus *m = mn(ui);
	KpUiCreate *c = &m->create;
	int k;
	kp_ui_delete_timer(&ui->timers, m->create_dialog, SLOT_TIMER);
	k = selected_entry(ui);
	if (k < 0 || m->entries[k].kind == KP_UI_ENTRY_FOLDER || m->entries[k].kind == KP_UI_ENTRY_UP) return 1;
	c->loading = 0;
	refresh_slots(ui, m->create_dialog);
	if (!can_start(ui) && !c->replay_read) {
		ok_popup(ui, kp_ui_tbl(ui, KP_UI_GLUALL, STR_CREATE_NO_COMPUTER), create_refused);
		return 0;
	}
	custom_races(ui);
	return 1;
}

/* OnCreateGameOk 0x455240: 0 keeps the screen open. */
static int create_ok(KpUi *ui)
{
	KpUiMenus *m = mn(ui);
	KpUiCreate *c = &m->create;
	int s = m->map_list >= 0 ? kp_ui_list_selection(ui, m->map_list) : KP_UI_LIST_NONE, k;
	uint16_t refusal;
	if (s == KP_UI_LIST_NONE) return 0;
	k = (int)kp_ui_list_data(ui, m->map_list, s);
	if (k < 0 || k >= m->entry_count) return 0;
	if (m->entries[k].kind == KP_UI_ENTRY_FOLDER || m->entries[k].kind == KP_UI_ENTRY_UP) { /* 0x8000000b */
		enter_folder(ui, &m->entries[k]);
		c->preselect[0] = 0;
		refresh_list(ui);
		return 0;
	}
	if ((refusal = create_game(ui, k)) != 0) {
		ok_popup(ui, kp_ui_tbl(ui, KP_UI_GLUALL, refusal), create_refused);
		return 0;
	}
	if (m->state == STATE_PLAY_CUSTOM) { /* the folder and the map remembered (Load Replay skips it) */
		memcpy(c->dir, m->map_dir, sizeof c->dir);
		(void)kp_text_copy(c->last, sizeof c->last, (const char *)kp_ui_list_text(ui, m->map_list, s));
	}
	return finish_create(ui);
}

/* Koprulu's Import (the host's picker; kp_ui_file_imported answers) and Export (the selected file's, a folder's
 * none) */
static void file_button(KpUi *ui, int32_t r)
{
	KpUiMenus *m = mn(ui);
	int k = selected_entry(ui);
	if ((r == R_REPLAY_IMPORT || r == R_CUSTOM_IMPORT) && ui->import_file != NULL) ui->import_file(ui->host);
	else if (r == R_REPLAY_EXPORT && ui->export_replay != NULL && k >= 0 && m->entries[k].kind == KP_UI_ENTRY_REPLAY) {
		ui->export_replay(ui->host, k);
	}
}

void kp_ui_file_imported(KpUi *ui, const char *file, uint16_t refusal)
{
	KpUiMenus *m = mn(ui);
	if ((m->state != STATE_LOAD_REPLAY && m->state != STATE_PLAY_CUSTOM) || m->screen < 0 || m->map_list < 0) return;
	if (refusal != 0) ok_popup(ui, kp_ui_tbl(ui, KP_UI_GLUALL, refusal), create_refused);
	else refresh_list_at(ui, file);
}

static void create_done(KpUi *ui, int32_t r)
{
	/* RunCreateGameScreen 0x455550: Ok starts the replay's game, or the map's (at once outside Use Map Settings or
	 * after a replay's read, else from the ready room of the player's race: the host's lobby decides it), Cancel the
	 * campaign menu */
	KpUiMenus *m = mn(ui);
	KpUiCreate *c = &m->create;
	c->active = 0;
	if (r == R_CREATE_OK) {
		if (m->replay_choice >= 0) {
			screen_ended(ui, m->state, KP_UI_MENU_REPLAY);
		} else {
			c->at_once = (uint8_t)(!selected_ums(ui) || c->replay_read);
			screen_ended(ui, m->state, KP_UI_MENU_CUSTOM);
		}
	} else if (r == R_CREATE_CANCEL || r == -3) {
		c->replay_read = 0;
		screen_ended(ui, m->expansion ? STATE_EXP_CAMPAIGN : STATE_CAMPAIGN, KP_UI_MENU_STATE);
	} else {
		screen_ended(ui, STATE_MAIN, KP_UI_MENU_STATE);
	}
}

static int create_proc(KpUi *ui, KpUiControl *d, KpUiEvent *e)
{
	/* CreateGameDialogProc 0x4550e0 */
	int32_t self = (int32_t)(d - ui->controls);
	KpUiInteract base = kp_ui_type_interact(0);
	if (is_wheel(e)) return wheel_list(ui, mn(ui)->map_list, e);
	if (e->type != KP_UI_USER) return base(ui, d, e);
	switch (e->code) {
	case KP_UI_CREATE: {
		static const uint16_t SLIDES[10] = { 1, 0, 2, 2, 3, 3, 4, 2, 19, 0 }; /* 0x503e48 */
		int32_t folder = kp_ui_find_control(ui, self, ID_CREATE_FOLDER);
		kp_ui_glue_slide_in(ui, self, SLIDES, 5, 0);
		if (folder >= 0) { /* InitMapList 0x455aa0: the listed folder's last name */
			const char *root = mn(ui)->state == STATE_PLAY_CUSTOM ? CUSTOM_ROOT_LABEL : REPLAY_ROOT_LABEL;
			const char *last = strrchr(mn(ui)->map_dir, '/');
			ui->controls[folder].draw = folder_label_draw;
			kp_ui_set_control_text(ui, folder,
				(const uint8_t *)(mn(ui)->map_dir[0] == 0 ? root : last ? last + 1 : mn(ui)->map_dir));
		}
		kp_ui_glue_activate(ui, self, THEME_NONE);
		break;
	}
	case KP_UI_CLOSE:
		if (ui->result == R_CREATE_OK && !create_ok(ui)) return 1;
		if (ui->result == R_REPLAY_IMPORT || ui->result == R_REPLAY_EXPORT || ui->result == R_CUSTOM_IMPORT) {
			file_button(ui, ui->result); /* the screen stays */
			return 1;
		}
		kp_ui_delete_timer(&ui->timers, self, SLOT_TIMER);
		kp_ui_glue_slide_out(ui, self);
		return 1;
	case SLIDE_DONE:
		refresh_list(ui);
		refused_popup(ui);
		break;
	case KP_UI_POST_LOAD: { /* InitCreateGameDialog 0x4551d0 with 0x503db0's procedures */
		KpUiInteract procs[CREATE_CONTROLS];
		int k, replay = mn(ui)->state != STATE_PLAY_CUSTOM;
		memset(procs, 0, sizeof procs);
		procs[ID_CREATE_LIST - 1] = map_list_proc;
		for (k = 6; k <= 11; k++) procs[k - 1] = kp_ui_type_interact(9);
		procs[R_CREATE_OK - 1] = procs[R_CREATE_CANCEL - 1] = kp_ui_text_button_proc;
		procs[14 - 1] = procs[15 - 1] = kp_ui_type_interact(9);
		procs[ID_GAME_TYPE - 1] = type_list_proc;
		procs[ID_SUBTYPE - 1] = subtype_list_proc;
		procs[ID_LOCAL_PLAYER - 1] = local_player_proc;
		for (k = ID_FIRST_SLOT; k <= ID_LAST_SLOT; k++) procs[k - 1] = slot_type_proc;
		for (k = ID_LOCAL_RACE; k <= ID_LAST_SLOT + RACE_LIST_OFFSET; k++) procs[k - 1] = race_list_proc;
		for (k = 36; k <= 38; k++) procs[k - 1] = kp_ui_type_interact(9);
		for (k = 0; k < INFO_UNVERIFIED_LABELS; k++) procs[ID_INFO_UNVERIFIED + k - 1] = kp_ui_type_interact(9);
		procs[R_REPLAY_IMPORT - 1] = procs[R_REPLAY_EXPORT - 1] = procs[R_CUSTOM_IMPORT - 1] = kp_ui_text_button_proc;
		/* without the host's file picker the screen is the original's; each mode keeps its own buttons */
		if ((k = kp_ui_find_control(ui, self, R_REPLAY_IMPORT)) >= 0 && (ui->import_file == NULL || !replay)) {
			kp_ui_drop_control(ui, self, k);
		}
		if ((k = kp_ui_find_control(ui, self, R_REPLAY_EXPORT)) >= 0 && (ui->export_replay == NULL || !replay)) {
			kp_ui_drop_control(ui, self, k);
		}
		if ((k = kp_ui_find_control(ui, self, R_CUSTOM_IMPORT)) >= 0 && (ui->import_file == NULL || replay)) {
			kp_ui_drop_control(ui, self, k);
		}
		mn(ui)->create_dialog = self;
		cr(ui)->subtype_list = kp_ui_find_control(ui, self, ID_SUBTYPE);
		cr(ui)->type_list = kp_ui_find_control(ui, self, ID_GAME_TYPE);
		kp_ui_glue_init_dialog(ui, self, procs, CREATE_CONTROLS, THEME_NONE);
		publish_info(ui, -1);
		break;
	}
	default: break;
	}
	return base(ui, d, e);
}

/* RunCreateGameScreen 0x455550's entry: the screen, its list's folder (InitMapList 0x455aa0: Load Replay's root each
 * time; Play Custom's first visit at maps, Brood War's at maps\BroodWar, a later visit at the remembered folder with
 * the remembered map to select) */
int32_t kp_ui_create_open(KpUi *ui)
{
	KpUiMenus *m = mn(ui);
	KpUiCreate *c = &m->create;
	m->create_dialog = m->map_list = -1;
	c->type_list = c->subtype_list = -1;
	m->map_preview = -1;
	m->replay_choice = -1;
	c->choice = -1;
	c->local_slot = -1;
	if (m->state != STATE_PLAY_CUSTOM) {
		m->map_dir[0] = 0;
	} else if (!c->visited) {
		c->visited = 1;
		snprintf(m->map_dir, sizeof m->map_dir, "%s", m->expansion ? CUSTOM_EXPANSION_DIR : "");
		memcpy(c->dir, m->map_dir, sizeof c->dir);
	} else {
		memcpy(m->map_dir, c->dir, sizeof m->map_dir);
		memcpy(c->preselect, c->last, sizeof c->preselect);
	}
	c->active = 1;
	return open_screen(ui, "rez\\gluCustm.bin", create_proc, create_done);
}
