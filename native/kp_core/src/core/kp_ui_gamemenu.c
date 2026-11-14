/* The in-game menus (opt_dlgs.cpp, atlas P27 0x47b140..0x47c110; help.cpp P16 0x464720..0x464850): the menus are
 * modeless dialogs over the paused game opened one at a time (OpenModelessDialog 0x46ecc0); MenuDialogProc 0x47b8f0
 * hands a button's activation to the open menu's handler (0x675068), the depth byte 0x671a81 counts the chain's levels
 * so that Previous reopens the parent or closes the last. The game menu (F10, the console's MENU button, Alt+M), the
 * end-mission menu with its restart, quit-mission and exit confirmations, the mission objectives, the help menu and
 * the keystroke help. */
#include "kp_ui.h"

#include <string.h>

#include "kp_game.h"

#define ID_OK (-2)
#define ID_CANCEL (-3)
#define ID_OBJECTIVES_TEXT 2
#define ID_HELP_LIST 1
#define ID_OBSERVER 1                /* quit2mnu's Become Observer (0x4b8e80 returns 0: always hidden) */
#define ID_NETWORK 4                 /* options.bin's Network button (multiplayer only) */
#define ID_SAVE 1                    /* gamemenu.bin's Save Game and Load Game */
#define ID_LOAD 2
#define ID_RESTART 1                 /* abrtmenu.bin's Restart Mission and Quit Mission */
#define ID_QUIT 2
#define ID_EXIT_REPLAY_TEXT (-13)    /* abrtmenu.bin's hidden "Exit Replay" label */
#define MENU_BUTTONS 8               /* 0x508d70: MenuButtonProc for the controls 1..8 */
#define FLAG_TILED 0x20000000u
#define FLAG_LIST_HELP 0x10020000u   /* FillHelpTextList 0x464850 */
#define TILED_MENU 4                 /* 0x508ba0[4] = 0x70: the menus' buttons */
#define HELP_STRINGS 0x34            /* help_txt.tbl 1..51 */
#define CMD_RESTART 0x08             /* SendRestartCommand 0x474060 */
#define EXIT_PROGRAM 2               /* RequestGameExit 0x444e00's codes */
#define EXIT_QUIT_MISSION 4
#define GLUE_CAMPAIGN 6              /* 0x66a274 after a quit mission: the campaign menu (Brood War 0x16) */
#define GLUE_EXPANSION_CAMPAIGN 22

typedef void (*Handler)(KpUi *ui, int32_t dialog);

static void open_game_menu(KpUi *ui);
static void open_abort_menu(KpUi *ui);
static void open_help_menu(KpUi *ui);

/* --- the pause (0x47cbb0, 0x47cc10): a single player's game stands while a modeless dialog is open --- */

void kp_ui_pause_game(KpUi *ui)
{
	/* PauseGame 0x47cbb0 (the flag the trigger action Pause Game shares, kp_core's KP_PAUSED) */
	if (ui->game == NULL || ui->menus.multiplayer || kp_scalar(ui->game, KP_PAUSED) == 1) return;
	kp_scalar_set(ui->game, KP_PAUSED, 1);
	kp_ui_cancel_drag_box(ui);
	ui->pause_tick = ui->now; /* 0x675180 */
}

void kp_ui_game_paused(KpUi *ui)
{
	/* kp_core sets KP_PAUSED itself where the original calls PauseGame 0x47cbb0 (TrigActPauseGame 0x4b6020, the result
	 * dialog's OpenModelessDialog 0x46ecc0): the tick it takes then, which a later UnpauseGame discounts from */
	ui->pause_tick = ui->now;
}

void kp_ui_unpause_game(KpUi *ui, int discount)
{
	/* UnpauseGame 0x47cc10: the paused seconds leave the game's elapsed clock (0x47a620) */
	if (ui->game == NULL || ui->menus.multiplayer || kp_scalar(ui->game, KP_PAUSED) != 1) return;
	if (discount) ui->paused_seconds += (ui->now - ui->pause_tick) / 1000;
	kp_scalar_set(ui->game, KP_PAUSED, 0);
}

/* --- the menu framework (0x47b7f0..0x47b940) --- */

static Handler handler_of(KpUi *ui) { return (Handler)ui->menu_handler; }

static void set_handler(KpUi *ui, Handler h) { ui->menu_handler = (void (*)(KpUi *, int32_t))h; }

/* SetChildUserWord 0x46c840 / 0x46c870: the OK or Cancel button's tiled art. */
static void tiled_child(KpUi *ui, int32_t dialog, int16_t id)
{
	int32_t c = kp_ui_find_control(ui, dialog, id);
	if (c >= 0) kp_ui_set_user_word(&ui->controls[c], TILED_MENU, FLAG_TILED);
}

/* The menu's frame and translucency (DrawDialogTileFrame 0x47bb70, SetDialogBorderColor 0x47bd40). */
static void menu_frame(KpUi *ui, int32_t dialog)
{
	kp_ui_tile_frame(ui, dialog);
	kp_ui_border_colour(ui, dialog);
}

static void end_dialog(KpUi *ui, int32_t dialog)
{
	/* CloseDialog 0x4c2b50: the destroy event now (CloseModelessDialog 0x46ec70(0) in the menu's destroy) */
	kp_ui_destroy_dialog(ui, dialog);
	ui->abort = 1;
}

/* Previous: one level up (the reopened menu's init counts itself again), or back to the game from the first. */
static void back_to(KpUi *ui, int32_t dialog, void (*open)(KpUi *))
{
	if (--ui->menu_depth == 0) {
		end_dialog(ui, dialog);
	} else {
		ui->menu_depth--;
		open(ui);
	}
}

static void hide(KpUi *ui, int32_t dialog, int16_t id)
{
	int32_t c = kp_ui_find_control(ui, dialog, id);
	if (c >= 0) kp_ui_hide_control(ui, c);
}

static void game_menu_handler(KpUi *ui, int32_t dialog);
static void abort_menu_handler(KpUi *ui, int32_t dialog);
static void quit_mission_handler(KpUi *ui, int32_t dialog);
static void options_menu_handler(KpUi *ui, int32_t dialog);

static void init_menu(KpUi *ui, int32_t dialog)
{
	/* InitMenuDialog 0x47b7f0 (single player: the abort menu keeps its buttons, the game menu its Save and Load) */
	static KpUiInteract buttons[MENU_BUTTONS];
	int k;
	for (k = 0; k < MENU_BUTTONS; k++) buttons[k] = kp_ui_menu_button_proc;
	menu_frame(ui, dialog);
	kp_ui_bind_interacts(ui, dialog, buttons, MENU_BUTTONS);
	tiled_child(ui, dialog, ID_OK);
	tiled_child(ui, dialog, ID_CANCEL);
	/* InitAbortMenu 0x47b140 and InitNetworkGameMenu 0x47b4c0 of a replay (TODO: (outside V1) a multiplayer game's) */
	if (ui->game->w->replay && handler_of(ui) == abort_menu_handler) {
		int32_t label = kp_ui_find_control(ui, dialog, ID_EXIT_REPLAY_TEXT), quit = kp_ui_find_control(ui, dialog, ID_QUIT);
		hide(ui, dialog, ID_RESTART);
		if (label >= 0 && quit >= 0) kp_ui_set_control_text(ui, quit, ui->controls[label].text); /* 0x47b270 */
	}
	if (ui->game->w->replay && handler_of(ui) == game_menu_handler) {
		hide(ui, dialog, ID_LOAD);
		hide(ui, dialog, ID_SAVE); /* Pause and Resume stay hidden in a replay */
	}
	if (handler_of(ui) == quit_mission_handler) hide(ui, dialog, ID_OBSERVER);
	if (handler_of(ui) == options_menu_handler && !ui->menus.multiplayer) hide(ui, dialog, ID_NETWORK);
	ui->menu_depth++;
}

static int menu_proc(KpUi *ui, KpUiControl *d, KpUiEvent *e)
{
	/* MenuDialogProc 0x47b8f0 */
	int32_t self = (int32_t)(d - ui->controls);
	if (e->type == KP_UI_USER) {
		switch (e->code) {
		case KP_UI_CREATE: init_menu(ui, self); break;
		case KP_UI_DESTROY: /* MenuDialogDestroy 0x47b940 */
			kp_ui_close_modeless(ui, 0);
			(void)kp_ui_modeless_default(ui, d, e);
			return 1;
		case KP_UI_CLOSE:
			if (handler_of(ui) != NULL) handler_of(ui)(ui, self);
			return 1;
		default: break;
		}
	}
	return kp_ui_modeless_default(ui, d, e);
}

static void open_menu(KpUi *ui, const char *bin, Handler h)
{
	set_handler(ui, h);
	(void)kp_ui_open_modeless(ui, bin, menu_proc);
}

static void open_game_menu(KpUi *ui) { open_menu(ui, "rez\\gamemenu.bin", game_menu_handler); }  /* 0x47b300 */
static void open_abort_menu(KpUi *ui) { open_menu(ui, "rez\\abrtmenu.bin", abort_menu_handler); } /* 0x47b2e0 */
static void open_options_menu(KpUi *ui) { open_menu(ui, "rez\\options.bin", options_menu_handler); } /* 0x47b340 */

/* --- the objectives (0x47b960..0x47bab0) --- */

static int objectives_proc(KpUi *ui, KpUiControl *d, KpUiEvent *e)
{
	/* ObjectivesDialogProc 0x47bab0 */
	int32_t self = (int32_t)(d - ui->controls), t;
	if (e->type == KP_UI_USER) {
		switch (e->code) {
		case KP_UI_CREATE: /* InitObjectivesDialog 0x47b960: the campaign's text is the map string (the other victory
		                    * conditions' network.tbl texts are the multiplayer's) */
			menu_frame(ui, self);
			tiled_child(ui, self, ID_CANCEL);
			if ((t = kp_ui_find_control(ui, self, ID_OBJECTIVES_TEXT)) >= 0 && ui->game != NULL) {
				ui->controls[t].text = (const uint8_t *)kp_map_string(ui->game,
					(uint16_t)ui->game->trig.objectives[ui->game->local_player]);
			}
			ui->menu_depth++;
			break;
		case KP_UI_DESTROY:
			kp_ui_close_modeless(ui, 0);
			(void)kp_ui_modeless_default(ui, d, e);
			return 1;
		case KP_UI_CLOSE: /* ObjectivesBack 0x47ba70 */
			back_to(ui, self, open_game_menu);
			return 1;
		default: break;
		}
	}
	return kp_ui_modeless_default(ui, d, e);
}

/* --- the keystroke help (0x464720..0x464850) --- */

static int help_list_proc(KpUi *ui, KpUiControl *c, KpUiEvent *e)
{
	/* HelpTextListProc 0x464810 with FillHelpTextList 0x464850 */
	int32_t self = (int32_t)(c - ui->controls);
	if (e->type == KP_UI_LEFT_DOUBLE) return 1;
	if (e->type == KP_UI_USER && e->code == KP_UI_CREATE) {
		int r = kp_ui_list_proc(ui, c, e);
		uint16_t k;
		kp_ui_list_redraw(ui, self, 0);
		for (k = 1; k < HELP_STRINGS; k++) (void)kp_ui_list_add(ui, self, kp_ui_tbl(ui, KP_UI_HELP, k), 0);
		kp_ui_list_redraw(ui, self, 1);
		c->flags |= FLAG_LIST_HELP;
		return r;
	}
	return kp_ui_list_proc(ui, c, e);
}

static int help_proc(KpUi *ui, KpUiControl *d, KpUiEvent *e)
{
	/* HelpDialogProc 0x464790 (help_txt.tbl is the interface's for good) */
	static const KpUiInteract CHILDREN[1] = { help_list_proc };
	int32_t self = (int32_t)(d - ui->controls);
	if (e->type == KP_UI_USER) {
		switch (e->code) {
		case KP_UI_CREATE: /* InitHelpDialog 0x464730 */
			menu_frame(ui, self);
			kp_ui_bind_interacts(ui, self, CHILDREN, 1);
			tiled_child(ui, self, ID_OK);
			ui->menu_depth++;
			break;
		case KP_UI_DESTROY:
			kp_ui_close_modeless(ui, 0);
			(void)kp_ui_modeless_default(ui, d, e);
			return 1;
		case KP_UI_CLOSE:
			back_to(ui, self, open_help_menu);
			return 1;
		default: break;
		}
	}
	return kp_ui_modeless_default(ui, d, e);
}

/* --- the handlers (0x47b5d0, 0x47b1b0, 0x47c0b0, 0x47bf70, 0x47bff0, 0x47b700, 0x47be40) --- */

static void game_menu_handler(KpUi *ui, int32_t dialog)
{
	/* GameMenuHandler 0x47b5d0 */
	set_handler(ui, NULL);
	switch (ui->result) {
	case ID_CANCEL: /* Return to Game */
		ui->menu_depth = 0;
		end_dialog(ui, dialog);
		break;
	case 1: kp_ui_save_dialog(ui, 0); break;
	case 2: kp_ui_save_dialog(ui, 1); break;
	case 3: open_options_menu(ui); break;
	case 4: open_help_menu(ui); break;
	case 5: (void)kp_ui_open_modeless(ui, "rez\\objctdlg.bin", objectives_proc); break;
	case 6: open_abort_menu(ui); break;
	default: break; /* 7, 8: the multiplayer's pause and resume commands */
	}
}

static void restart_handler(KpUi *ui, int32_t dialog)
{
	/* RestartMenuHandler 0x47c0b0: the command 0x08 ends a single player's game with the exit code 5 */
	static const uint8_t CMD[1] = { CMD_RESTART };
	set_handler(ui, NULL);
	if (ui->result == ID_CANCEL) {
		back_to(ui, dialog, open_abort_menu);
		return;
	}
	kp_ui_queue_command(ui, CMD, sizeof CMD);
	end_dialog(ui, dialog);
}

static void quit_handler(KpUi *ui, int32_t dialog)
{
	/* QuitMenuHandler 0x47bf70 (rez\quit.bin): Exit Program (no Battle.net report or league logout offline) */
	set_handler(ui, NULL);
	if (ui->result != ID_OK) {
		back_to(ui, dialog, open_abort_menu);
		return;
	}
	ui->game_exit = EXIT_PROGRAM;
	end_dialog(ui, dialog);
}

static void quit_mission_handler(KpUi *ui, int32_t dialog)
{
	/* QuitMissionMenuHandler 0x47bff0 (rez\quit2mnu.bin) with its single player's tail: the exit code 4 and the
	 * campaign menu next */
	set_handler(ui, NULL);
	if (ui->result == ID_OK) {
		ui->game_exit = EXIT_QUIT_MISSION;
		ui->menus.state = ui->menus.expansion ? GLUE_EXPANSION_CAMPAIGN : GLUE_CAMPAIGN;
		end_dialog(ui, dialog);
	} else if (ui->result == ID_OBSERVER) {
		end_dialog(ui, dialog);
		ui->menu_depth = 0;
	} else {
		back_to(ui, dialog, open_abort_menu);
	}
}

static void quit_replay_handler(KpUi *ui, int32_t dialog)
{
	/* QuitReplayMenuHandler 0x47bed0 (rez\\quitrepl.bin): a single player's exit code 4 and the campaign menu next */
	set_handler(ui, NULL);
	if (ui->result == ID_OK) {
		ui->game_exit = EXIT_QUIT_MISSION;
		ui->menus.state = ui->menus.expansion ? GLUE_EXPANSION_CAMPAIGN : GLUE_CAMPAIGN;
		end_dialog(ui, dialog);
	} else {
		back_to(ui, dialog, open_abort_menu);
	}
}

/* OpenQuitMissionMenu 0x47b380 */
static void open_quit_mission(KpUi *ui)
{
	if (ui->game->w->replay) open_menu(ui, "rez\\quitrepl.bin", quit_replay_handler);
	else open_menu(ui, "rez\\quit2mnu.bin", quit_mission_handler);
}

static void abort_menu_handler(KpUi *ui, int32_t dialog)
{
	/* AbortMenuHandler 0x47b1b0 */
	set_handler(ui, NULL);
	kp_ui_delete_timer(&ui->timers, dialog, 10);
	switch (ui->result) {
	case ID_CANCEL: back_to(ui, dialog, open_game_menu); break;
	case 1: open_menu(ui, "rez\\restart.bin", restart_handler); break;
	case 2: open_quit_mission(ui); break;
	case 3: open_menu(ui, "rez\\quit.bin", quit_handler); break;
	default: break;
	}
}

static void help_menu_handler(KpUi *ui, int32_t dialog)
{
	/* HelpMenuHandler 0x47b700 */
	set_handler(ui, NULL);
	switch (ui->result) {
	case ID_CANCEL: back_to(ui, dialog, open_game_menu); break;
	case 1: (void)kp_ui_open_modeless(ui, "rez\\help.bin", help_proc); break;
	case 2: kp_ui_options_dialog(ui, KP_UI_OPTIONS_TIPS); break;
	default: break;
	}
}

static void open_help_menu(KpUi *ui) { open_menu(ui, "rez\\helpmenu.bin", help_menu_handler); } /* 0x47b320 */

static void options_menu_handler(KpUi *ui, int32_t dialog)
{
	/* OptionsMenuHandler 0x47be40 (4: the multiplayer's network dialog) */
	set_handler(ui, NULL);
	switch (ui->result) {
	case ID_CANCEL: back_to(ui, dialog, open_game_menu); break;
	case 1: kp_ui_options_dialog(ui, KP_UI_OPTIONS_SPEED); break;
	case 2: kp_ui_options_dialog(ui, KP_UI_OPTIONS_SOUND); break;
	case 3: kp_ui_options_dialog(ui, KP_UI_OPTIONS_VIDEO); break;
	default: break;
	}
}

/* --- the entries --- */

void kp_ui_game_menu(KpUi *ui, int which)
{
	switch (which) {
	case KP_UI_GAME_MENU: open_game_menu(ui); break;
	case KP_UI_GAME_MENU_HELP: open_help_menu(ui); break;
	case KP_UI_GAME_MENU_OPTIONS: open_options_menu(ui); break;
	case KP_UI_GAME_MENU_QUIT_MISSION: open_quit_mission(ui); break;
	case KP_UI_GAME_MENU_QUIT: open_menu(ui, "rez\\quit.bin", quit_handler); break;
	case KP_UI_GAME_MENU_SAVE: kp_ui_save_dialog(ui, 0); break;
	case KP_UI_GAME_MENU_LOAD: kp_ui_save_dialog(ui, 1); break;
	default: break;
	}
}
