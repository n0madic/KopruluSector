/*
 * A script's names over the product's flow (kp_script_flow.h): fixed tables, so a script's typo is a parse error before
 * any data loads; their meaning over the KpFlow of the frame.
 */
#include "kp_script_flow.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --- the values --- */

enum {
	V_TICK, V_CAMERA_X, V_CAMERA_Y, V_CAMERA_TILE_X, V_CAMERA_TILE_Y, V_MINIMAP_X, V_MINIMAP_Y, V_SELECTED, V_UNITS,
	V_CREEP_TILES, V_CREEP_RECEDING, V_TRIGGER_WAITING, V_PORTRAIT_MS, V_TARGETING, V_PLACING, V_GROUPS_HELD, V_PAD_OPEN,
	V_PAD_COUNT, V_PAD_SHOWN, V_GAMMA, V_PORTRAITS, V_VIDEO_GRAPHICS, V_VIDEO_FOG, V_VIDEO_WATER, V_VIDEO_FONT,
	V_VIDEO_LIGHTING, V_VIDEO_SERIAL, V_VIDEO_COMMITS, V_TEXT_VECTOR, V_SELECTION_HAS, V_SELECTION_FIRST, V_MOVED,
	V_GROUP_HELD, V_REPLAY_ENDED, V_REPLAY_FRAMES, V_SIM_CRC, V_COUNT
};
enum { NEED_UI, NEED_WORLD, NEED_GAME, NEED_CONSOLE }; /* what a value is read from */
static const struct {
	const char *name;
	KpScriptArg arg;
	int need;
} VALUES[V_COUNT] = {
	{ "tick", KP_SCRIPT_ARG_NONE, NEED_GAME },
	{ "camera.x", KP_SCRIPT_ARG_NONE, NEED_WORLD },        /* the view's centre in the world's pixels */
	{ "camera.y", KP_SCRIPT_ARG_NONE, NEED_WORLD },
	{ "camera.tile.x", KP_SCRIPT_ARG_NONE, NEED_WORLD },   /* the view's top-left tile */
	{ "camera.tile.y", KP_SCRIPT_ARG_NONE, NEED_WORLD },
	{ "minimap.x", KP_SCRIPT_ARG_NONE, NEED_CONSOLE },     /* the minimap's view: the tile it last drew */
	{ "minimap.y", KP_SCRIPT_ARG_NONE, NEED_CONSOLE },
	{ "selected", KP_SCRIPT_ARG_NONE, NEED_GAME },         /* the local player's selection */
	{ "units", KP_SCRIPT_ARG_INT, NEED_GAME },             /* units:N, player N's units (0..11) */
	{ "creep.tiles", KP_SCRIPT_ARG_NONE, NEED_GAME },
	{ "creep.receding", KP_SCRIPT_ARG_NONE, NEED_GAME },
	{ "trigger.waiting", KP_SCRIPT_ARG_NONE, NEED_GAME },  /* the local player's triggers wait (a transmission) */
	{ "portrait.ms", KP_SCRIPT_ARG_NONE, NEED_CONSOLE },   /* the trigger portrait's time left, 0 none */
	{ "targeting", KP_SCRIPT_ARG_NONE, NEED_CONSOLE },     /* an order's target is aimed */
	{ "placing", KP_SCRIPT_ARG_NONE, NEED_CONSOLE },       /* a building is placed */
	{ "groups.held", KP_SCRIPT_ARG_NONE, NEED_CONSOLE },   /* the groups that hold units, a bit each (group 0 the lowest) */
	{ "pad.open", KP_SCRIPT_ARG_NONE, NEED_CONSOLE },      /* the touch pad's groups opened */
	{ "pad.count", KP_SCRIPT_ARG_NONE, NEED_CONSOLE },     /* its buttons shown */
	{ "pad.shown", KP_SCRIPT_ARG_INT, NEED_CONSOLE },      /* pad.shown:N, its button N (KP_UI_PAD_*) shown */
	{ "gamma", KP_SCRIPT_ARG_NONE, NEED_UI },
	{ "portraits", KP_SCRIPT_ARG_NONE, NEED_UI },          /* the portraits' mode: 0 none, 1 still, 2 animated */
	{ "video.graphics", KP_SCRIPT_ARG_NONE, NEED_UI },     /* the video options (KP_VIDEO_AUTO -1, or the choice's value) */
	{ "video.fog", KP_SCRIPT_ARG_NONE, NEED_UI },
	{ "video.water", KP_SCRIPT_ARG_NONE, NEED_UI },
	{ "video.font", KP_SCRIPT_ARG_NONE, NEED_UI },
	{ "video.lighting", KP_SCRIPT_ARG_NONE, NEED_UI },
	{ "video.serial", KP_SCRIPT_ARG_NONE, NEED_UI },       /* the dialog's changes */
	{ "video.commits", KP_SCRIPT_ARG_NONE, NEED_UI },      /* the dialog's OKs kept */
	{ "text.vector", KP_SCRIPT_ARG_NONE, NEED_UI },        /* the interface's text in the vector font */
	{ "selection.has", KP_SCRIPT_ARG_PICK, NEED_GAME },    /* selection.has:PICK, 1 when the pick is selected */
	{ "selection.first", KP_SCRIPT_ARG_PICK, NEED_GAME },  /* selection.first:PICK, 1 when the selection's first */
	{ "moved", KP_SCRIPT_ARG_PICK, NEED_GAME },            /* moved:PICK, the world's pixels from where it was picked */
	{ "group.held", KP_SCRIPT_ARG_INT, NEED_CONSOLE },     /* group.held:N, 1 when group N (0..9) holds units */
	{ "replay.ended", KP_SCRIPT_ARG_NONE, NEED_GAME },     /* a replay's playback passed its record */
	{ "replay.frames", KP_SCRIPT_ARG_NONE, NEED_GAME },    /* the replay's frames (its header's), 0 outside one */
	{ "sim.crc", KP_SCRIPT_ARG_NONE, NEED_GAME },          /* the state's CRCs folded, the local interface's aside */
};
#define GROUPS 10
#define PLAYERS 12

/* --- the conditions --- */

enum { C_GAME, C_DIALOG, C_NODIALOG, C_PAUSED, C_GLUE, C_STILL, C_SCREEN, C_ROOM, C_MOVIE, C_NOMOVIE, C_EPISODE,
	C_BRIEFING, C_SCORE, C_REPLAY, C_COUNT };
/* episode: a campaign's episode chosen (its text's popup, or its ready room); briefing: the ready room open (its text
 * past); score: the score screen; replay: the game in play is a replay's playback */
static const char *const CONDS[C_COUNT] = { "game", "dialog", "nodialog", "paused", "glue", "still", "screen", "room",
	"movie", "nomovie", "episode", "briefing", "score", "replay" };

/* --- the names of the commands' arguments --- */

/* choose NAME: the video dialog's option buttons (kp_ui.h's KP_VIDEO_CHOICE_ID, the portraits' modes) */
static const struct {
	const char *name;
	int16_t id;
} CHOICES[] = {
	{ "graphics.auto", KP_VIDEO_CHOICE_ID(KP_VIDEO_ID_GRAPHICS, KP_VIDEO_AUTO) },
	{ "graphics.original", KP_VIDEO_CHOICE_ID(KP_VIDEO_ID_GRAPHICS, KP_GRAPHICS_ORIGINAL) },
	{ "graphics.hd2", KP_VIDEO_CHOICE_ID(KP_VIDEO_ID_GRAPHICS, KP_GRAPHICS_HD2) },
	{ "graphics.hd", KP_VIDEO_CHOICE_ID(KP_VIDEO_ID_GRAPHICS, KP_GRAPHICS_HD) },
	{ "fog.auto", KP_VIDEO_CHOICE_ID(KP_VIDEO_ID_FOG, KP_VIDEO_AUTO) },
	{ "fog.original", KP_VIDEO_CHOICE_ID(KP_VIDEO_ID_FOG, KP_FOG_ORIGINAL) },
	{ "fog.smooth", KP_VIDEO_CHOICE_ID(KP_VIDEO_ID_FOG, KP_FOG_SMOOTH) },
	{ "water.auto", KP_VIDEO_CHOICE_ID(KP_VIDEO_ID_WATER, KP_VIDEO_AUTO) },
	{ "water.still", KP_VIDEO_CHOICE_ID(KP_VIDEO_ID_WATER, KP_WATER_STILL) },
	{ "water.animated", KP_VIDEO_CHOICE_ID(KP_VIDEO_ID_WATER, KP_WATER_ANIMATED) },
	{ "font.auto", KP_VIDEO_CHOICE_ID(KP_VIDEO_ID_FONT, KP_VIDEO_AUTO) },
	{ "font.original", KP_VIDEO_CHOICE_ID(KP_VIDEO_ID_FONT, KP_VTEXT_ORIGINAL) },
	{ "font.vector", KP_VIDEO_CHOICE_ID(KP_VIDEO_ID_FONT, KP_VTEXT_VECTOR) },
	{ "lighting.auto", KP_VIDEO_CHOICE_ID(KP_VIDEO_ID_LIGHTING, KP_VIDEO_AUTO) },
	{ "lighting.off", KP_VIDEO_CHOICE_ID(KP_VIDEO_ID_LIGHTING, KP_LIGHTING_OFF) },
	{ "lighting.on", KP_VIDEO_CHOICE_ID(KP_VIDEO_ID_LIGHTING, KP_LIGHTING_ON) },
	{ "portraits.none", KP_VIDEO_ID_PORTRAITS + KP_UI_PORTRAITS_NONE },
	{ "portraits.still", KP_VIDEO_ID_PORTRAITS + KP_UI_PORTRAITS_STILL },
	{ "portraits.animated", KP_VIDEO_ID_PORTRAITS + KP_UI_PORTRAITS_ANIMATED },
};
/* menu NAME (kp_ui_game_menu), options-dialog NAME (kp_ui_options_dialog) */
static const char *const MENUS[] = { "game", "help", "options", "quit-mission", "quit", "save", "load" };
static const char *const OPTIONS[] = { NULL, "speed", "sound", "video", "tips" };
/* select NAME: the local player's worker, its main building (by its race), the unit a box takes the most units
 * around */
enum { UNIT_WORKER, UNIT_MAIN, UNIT_GROUP, UNIT_COUNT };
static const char *const UNITS[UNIT_COUNT] = { "worker", "main", "group" };
#define N_OF(t) ((int)(sizeof t / sizeof t[0]))

/* --- the dialogs and their controls --- */

/* The dialogs by their BIN's name without its directory and .bin (any case): the glue's screens and popups, the game's
 * menus, ours (koprulu\); then, their indices from CONSOLE_FIRST, the console's own, which are no dialog to a script's
 * dialog, nodialog and screen (their controls are DIALOG.ID's, `ui` lists them). */
static const char *const DIALOGS[] = { "abrtmenu", "allyfltr", "credits", "gamemenu", "gluCmpgn", "gluCustm", "gluExpCmpgn", "gluGameMode",
	"gluHist", "gluLoad", "gluLogin", "gluMain", "gluPEdit", "gluPOk", "gluPOkCancel", "gluScore", "glurdyp", "glurdyt",
	"glurdyz", "help", "helpmenu", "language", "lmission", "loadgame", "objctdlg", "ok", "okcancel", "options", "quit",
	"quit2mnu", "quitrepl", "restart", "savegame", "snd_dlg", "spd_dlg", "tips_dlg", "titledlg", "video", "wait", "wmission" };
static const char *const CONSOLE_DIALOGS[] = { "minimap", "ptextbox", "ttextbox", "ztextbox", "stat_f10", "statbtnn",
	"statbtnp", "statbtnt", "statbtnz", "statdata", "statlb", "statport", "statres" };
#define CONSOLE_FIRST ((int)(sizeof DIALOGS / sizeof DIALOGS[0]))
#define DIALOG_COUNT (CONSOLE_FIRST + (int)(sizeof CONSOLE_DIALOGS / sizeof CONSOLE_DIALOGS[0]))
#define ANY_DIALOG (-1)
#define RIGHT_END_INSET 2 /* control NAME right: the point this far inside the control's right edge (a slider's end) */
#define ID_OK (-2) /* a dialog's OK and Cancel (kp_ui_gamemenu.c's ID_OK, ID_CANCEL) */
#define ID_CANCEL (-3)
#define ID_BITS 16 /* DIALOG.ID: the name's id past the table is the dialog's index above the control's 16-bit ID */

/* The controls: a dialog's (NULL: the topmost open dialog that has the ID) and its ID. */
static const struct {
	const char *name;
	const char *dialog;
	int16_t id;
} CONTROLS[] = {
	{ "ok", NULL, ID_OK },
	{ "cancel", NULL, ID_CANCEL },
	/* gamemenu.bin (GameMenuHandler 0x47b5d0) */
	{ "gamemenu.save", "gamemenu", 1 },
	{ "gamemenu.load", "gamemenu", 2 },
	{ "gamemenu.options", "gamemenu", 3 },
	{ "gamemenu.help", "gamemenu", 4 },
	{ "gamemenu.objectives", "gamemenu", 5 },
	{ "gamemenu.end-mission", "gamemenu", 6 },
	{ "gamemenu.return", "gamemenu", ID_CANCEL },
	/* the glue (kp_ui_menus.c: RunMainMenu 0x45a8b0, RunGameModePopup 0x45fff0, the login, the campaign menus, the load
	 * screen) */
	{ "glumain.single", "gluMain", 3 },
	{ "glugamemode.original", "gluGameMode", 6 },
	{ "glugamemode.expansion", "gluGameMode", 7 },
	{ "glulogin.ok", "gluLogin", 4 },
	{ "glulogin.cancel", "gluLogin", 5 },
	{ "glulogin.new", "gluLogin", 6 },
	{ "glulogin.delete", "gluLogin", 7 },
	{ "glucmpgn.load", "gluCmpgn", 5 },
	{ "glucmpgn.player", "gluCmpgn", 9 },
	{ "glucmpgn.replay", "gluCmpgn", 11 },
	{ "gluexpcmpgn.load", "gluExpCmpgn", 5 },
	{ "gluexpcmpgn.player", "gluExpCmpgn", 9 },
	{ "gluexpcmpgn.replay", "gluExpCmpgn", 11 },
	/* Load Replay (RunCreateGameScreen 0x455550 in its replay mode, glue state 12) */
	{ "glucustm.list", "gluCustm", 5 },
	{ "glucustm.ok", "gluCustm", 12 },
	{ "glucustm.cancel", "gluCustm", 13 },
	{ "gluload.ok", "gluLoad", 4 },
	{ "gluload.cancel", "gluLoad", 5 },
	{ "gluload.delete", "gluLoad", 7 },
};
#define CONTROL_COUNT ((int)(sizeof CONTROLS / sizeof CONTROLS[0]))

/* The controls' kinds (the BIN's types; 9..11 the labels' alignments, 14 the glue's buttons with their flic clips). */
static const char *const KINDS[] = { "dialog", "default-button", "button", "option", "checkbox", "image", "slider",
	"scrollbar", "edit", "label", "label", "label", "list", "combo", "flic-button" };
#define KIND_COUNT ((int)(sizeof KINDS / sizeof KINDS[0]))
#define KIND_OPTION 3   /* their checked byte: words36[0]'s low byte (kp_ui_draw.c) */
#define KIND_CHECKBOX 4

static char lower(char c) { return c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c; }

/* Whether a (n bytes) and b name the same, in any case. */
static int same(const char *a, size_t n, const char *b)
{
	size_t k;
	for (k = 0; k < n; k++) {
		if (b[k] == '\0' || lower(a[k]) != lower(b[k])) return 0;
	}
	return b[n] == '\0';
}

static const char *dialog_name(int k) { return k < CONSOLE_FIRST ? DIALOGS[k] : CONSOLE_DIALOGS[k - CONSOLE_FIRST]; }

static int dialog_index_n(const char *name, size_t n)
{
	int k;
	for (k = 0; k < DIALOG_COUNT; k++) {
		if (same(name, n, dialog_name(k))) return k;
	}
	return -1;
}

static int dialog_index(const char *name) { return dialog_index_n(name, strlen(name)); }

/* DIALOG.ID: the dialog's index and the control's ID; 0, or -1 when word is not of that form. */
static int dialog_control(const char *word, int *dialog, int16_t *id)
{
	const char *dot = strrchr(word, '.');
	char *end;
	long n;
	if (!dot || dot == word || !dot[1]) return -1;
	n = strtol(dot + 1, &end, 10);
	if (*end || n < INT16_MIN || n > INT16_MAX || ((dot[1] < '0' || dot[1] > '9') && dot[1] != '-')) return -1;
	*dialog = dialog_index_n(word, (size_t)(dot - word));
	*id = (int16_t)n;
	return *dialog >= 0 ? 0 : -1;
}

/* A control name's dialog (ANY_DIALOG: the topmost that has it) and ID. */
static void control_of(int32_t name, int *dialog, int16_t *id)
{
	if (name < CONTROL_COUNT) {
		*dialog = CONTROLS[name].dialog ? dialog_index(CONTROLS[name].dialog) : ANY_DIALOG;
		*id = CONTROLS[name].id;
	} else {
		*dialog = (name - CONTROL_COUNT) >> ID_BITS;
		*id = (int16_t)(uint16_t)((name - CONTROL_COUNT) & ((1 << ID_BITS) - 1));
	}
}

int kp_script_flow_name(KpScriptKind kind, const char *word, KpScriptName *out)
{
	int k = -1, dialog;
	int16_t id;
	out->arg = KP_SCRIPT_ARG_NONE;
	out->arg_kind = KP_SCRIPT_NAME_KEY;
	out->arg_max = 0;
	switch (kind) {
	case KP_SCRIPT_NAME_VALUE:
		for (k = V_COUNT - 1; k >= 0 && !same(word, strlen(word), VALUES[k].name); k--) {}
		if (k < 0) break;
		out->arg = VALUES[k].arg;
		if (k == V_UNITS) out->arg_max = PLAYERS - 1;
		if (k == V_PAD_SHOWN) out->arg_max = KP_UI_PAD_BUTTONS - 1;
		if (k == V_GROUP_HELD) out->arg_max = GROUPS - 1;
		break;
	case KP_SCRIPT_NAME_COND:
		for (k = C_COUNT - 1; k >= 0 && !same(word, strlen(word), CONDS[k]); k--) {}
		if (k == C_DIALOG || k == C_SCREEN) {
			out->arg = KP_SCRIPT_ARG_OPT_NAME;
			out->arg_kind = KP_SCRIPT_NAME_DIALOG;
		}
		break;
	case KP_SCRIPT_NAME_DIALOG: /* a condition's: the console's dialogs are none */
		k = dialog_index(word);
		if (k >= CONSOLE_FIRST) k = -1;
		break;
	case KP_SCRIPT_NAME_CONTROL:
		for (k = CONTROL_COUNT - 1; k >= 0 && !same(word, strlen(word), CONTROLS[k].name); k--) {}
		if (k < 0 && dialog_control(word, &dialog, &id) == 0) k = CONTROL_COUNT + (dialog << ID_BITS) + (uint16_t)id;
		break;
	case KP_SCRIPT_NAME_CHOICE:
		for (k = N_OF(CHOICES) - 1; k >= 0 && !same(word, strlen(word), CHOICES[k].name); k--) {}
		if (k >= 0) k = CHOICES[k].id;
		break;
	case KP_SCRIPT_NAME_MENU:
		for (k = N_OF(MENUS) - 1; k >= 0 && !same(word, strlen(word), MENUS[k]); k--) {}
		break;
	case KP_SCRIPT_NAME_OPTIONS:
		for (k = N_OF(OPTIONS) - 1; k > 0 && !same(word, strlen(word), OPTIONS[k]); k--) {}
		if (k == 0) k = -1;
		break;
	case KP_SCRIPT_NAME_UNIT:
		for (k = UNIT_COUNT - 1; k >= 0 && !same(word, strlen(word), UNITS[k]); k--) {}
		break;
	default: break;
	}
	if (k < 0) return -1;
	out->id = k;
	return 0;
}

/* --- the frame's interface --- */

/* The dialog's BIN name without its directory and .bin: its start in *name, its length. */
static size_t bin_stem(const KpUi *ui, int32_t d, const char **name)
{
	const KpUiControl *c = &ui->controls[d];
	const KpUiBin *b;
	size_t start, end;
	*name = "";
	if (c->bin < 0 || c->bin >= ui->bin_count) return 0;
	b = &ui->bins[c->bin];
	for (start = b->name_len; start > 0 && b->name[start - 1] != '\\' && b->name[start - 1] != '/'; start--) {}
	end = b->name_len;
	if (end - start > 4 && same(b->name + end - 4, 4, ".bin")) end -= 4;
	*name = b->name + start;
	return end - start;
}

/* The dialog's index in DIALOGS, -1 none. */
static int dialog_of(const KpUi *ui, int32_t d)
{
	const char *name;
	size_t n = bin_stem(ui, d, &name);
	return dialog_index_n(name, n);
}

static int is_console(const KpUi *ui, int32_t d) { return dialog_of(ui, d) >= CONSOLE_FIRST; }

static int is_dialog(const KpUi *ui, int32_t d, int dialog)
{
	return dialog == ANY_DIALOG || (dialog >= 0 && dialog_of(ui, d) == dialog);
}

/* The dialogs a script sees, topmost first: the game's modeless dialog, then the open list. */
static int candidates(const KpUi *ui, int32_t *out, int cap)
{
	int n = 0;
	int32_t d;
	if (ui->modeless >= 0 && n < cap) out[n++] = ui->modeless;
	for (d = ui->open; d >= 0 && n < cap; d = ui->controls[d].next) {
		if (d != ui->modeless) out[n++] = d;
	}
	return n;
}

#define CANDIDATES 32

/* The open list's dialogs (the glue's screen and the popups over it). */
static int open_count(const KpUi *ui)
{
	int32_t d;
	int n = 0;
	for (d = ui->open; d >= 0; d = ui->controls[d].next) n++;
	return n;
}

/* The glue's screen: the open list's last (the popups come before it), -1 none. */
static int32_t glue_screen(const KpUi *ui)
{
	int32_t d, screen = -1;
	for (d = ui->open; d >= 0; d = ui->controls[d].next) screen = d;
	return screen;
}

void kp_script_flow_dialogs(const KpFlow *f, char *out, size_t size)
{
	int32_t list[CANDIDATES];
	int n, k, shown = 0;
	size_t used = 0;
	out[0] = '\0';
	if (!f->ui) {
		snprintf(out, size, "no interface");
		return;
	}
	n = candidates(f->ui, list, CANDIDATES);
	for (k = 0; k < n && used + 1 < size; k++) {
		const char *name;
		size_t len;
		int w;
		if (is_console(f->ui, list[k])) continue;
		len = bin_stem(f->ui, list[k], &name);
		w = snprintf(out + used, size - used, "%s%.*s", shown++ ? ", " : "", (int)len, name);
		if (w < 0) break;
		used += (size_t)w < size - used ? (size_t)w : size - used - 1;
	}
	if (shown == 0) snprintf(out, size, "no dialog");
}

int kp_script_flow_still(const KpUi *ui)
{
	const KpUiGlue *g = &ui->glue;
	int k;
	for (k = 0; k < g->desc_count; k++) {
		if (g->slide_active[k] && g->slide_control[k] >= 0 && ui->controls[g->slide_control[k]].wuser != 0) return 0;
	}
	return g->hidden_count == 0 && g->slide_delay == 0;
}

/* Whether the game is in play with its console (its first view taken). */
static int in_game(const KpFlow *f)
{
	return f->world && f->playing && f->hud_on && f->session && f->session->g && !f->session->failed
		&& !kp_flow_glue_active(f) && !f->hud.has_start;
}

/* A dialog over the screen: in a game its modeless dialog, in the glue a popup over its screen. */
static int dialog_open(const KpFlow *f, int dialog)
{
	int32_t list[CANDIDATES];
	int n, k;
	if (!f->ui) return 0;
	if (dialog == ANY_DIALOG) return kp_flow_glue_active(f) ? open_count(f->ui) > 1 : f->ui->modeless >= 0;
	n = candidates(f->ui, list, CANDIDATES);
	for (k = 0; k < n; k++) {
		if (is_dialog(f->ui, list[k], dialog)) return 1;
	}
	return 0;
}

/* What the glue shows, for a failure's why. */
static void glue_why(const KpFlow *f, char *why, size_t size)
{
	char open[256];
	if (!kp_flow_glue_active(f) || !f->ui) {
		snprintf(why, size, "%s", f->world ? "a game" : "no glue");
		return;
	}
	kp_script_flow_dialogs(f, open, sizeof open);
	snprintf(why, size, "the glue: %s%s%s", open, f->glue.screen == KP_GLUE_ROOM ? " (a ready room)" : "",
		kp_script_flow_still(f->ui) ? "" : ", its panels moving");
}

/* The glue's screen standing still with no popup over it, and the dialog's when dialog is one. */
static int screen_up(const KpFlow *f, int dialog)
{
	return kp_flow_glue_active(f) && f->ui && kp_script_flow_still(f->ui) && open_count(f->ui) == 1
		&& is_dialog(f->ui, glue_screen(f->ui), dialog);
}

/* The view's size: the game view above the console in a game, else the logical screen. */
static void view_size(const KpFlow *f, int32_t *w, int32_t *h)
{
	if (f->hud_on) {
		kp_hud_view_size(f->world_w, f->world_h, w, h);
	} else {
		*w = f->world_w;
		*h = f->world_h;
	}
}

/* The game's values: its game, NULL with why when there is none. */
static KpGame *game_of(const KpFlow *f, char *why, size_t size)
{
	if (f->session && f->session->g) return f->session->g;
	snprintf(why, size, "no game");
	return NULL;
}

/* Whether what the value is read from is there; why says what is missing. */
static int value_there(const KpFlow *f, int need, char *why, size_t size)
{
	const char *missing = NULL;
	if (need == NEED_UI && !f->ui) missing = "no interface";
	else if (need == NEED_WORLD && !f->world) missing = "no game";
	else if (need >= NEED_GAME && !(f->session && f->session->g)) missing = "no game";
	else if (need == NEED_CONSOLE && (!f->ui || !f->hud_on)) missing = "no console";
	if (missing) snprintf(why, size, "%s", missing);
	return missing == NULL;
}

/* The pick's unit in g (run's slot: its reference, kp_unit_ref), 0 with why when there is none or it is gone (dead,
 * its record another unit's). */
static int64_t picked(const KpScriptRun *run, KpGame *g, int64_t slot, char *why, size_t size)
{
	int64_t n;
	if (!run || slot < 0 || slot >= KP_SCRIPT_PICKS || run->picks[slot] == 0) {
		snprintf(why, size, "no unit picked");
		return 0;
	}
	if ((n = kp_unit_from_ref(g, run->picks[slot])) == 0) snprintf(why, size, "the picked unit is gone");
	return n;
}

/* sim.crc: each category's CRC folded in dump order, but those of the local interface a replay's viewer does not share
 * with the recorded player: step's result dialog clock (TickResultDialogTimer 0x4be340 does not run in a replay),
 * the local selection, the sprites and images (its selection circles and the right click's ground marker). */
static void fold_crc(void *ud, const char *category, uint32_t crc)
{
	static const char *const LOCAL[] = { "step", "selection", "sprite", "image" };
	uint32_t *folded = (uint32_t *)ud;
	size_t k;
	for (k = 0; k < sizeof LOCAL / sizeof LOCAL[0]; k++) {
		if (strcmp(category, LOCAL[k]) == 0) return;
	}
	*folded = *folded * 31u + crc;
}

int kp_script_flow_value(const KpFlow *f, const KpScriptRun *run, const KpScriptRef *ref, int64_t *out, char *why,
	size_t size)
{
	const KpUi *ui = f->ui;
	KpGame *g;
	int32_t vw, vh, x, y;
	int64_t n, receding;
	uint32_t ms;
	int camera, k;
	if (ref->id < 0 || ref->id >= V_COUNT) {
		snprintf(why, size, "no value %d", (int)ref->id);
		return -1;
	}
	if (!value_there(f, VALUES[ref->id].need, why, size)) return -1;
	g = f->session ? f->session->g : NULL;
	view_size(f, &vw, &vh);
	switch (ref->id) {
	case V_TICK: *out = f->session->tick; break;
	case V_CAMERA_X: *out = f->cam_x + vw / 2; break;
	case V_CAMERA_Y: *out = f->cam_y + vh / 2; break;
	case V_CAMERA_TILE_X: *out = f->cam_x / KP_WORLD_TILE; break;
	case V_CAMERA_TILE_Y: *out = f->cam_y / KP_WORLD_TILE; break;
	case V_MINIMAP_X: *out = ui->minimap.view_x; break;
	case V_MINIMAP_Y: *out = ui->minimap.view_y; break;
	case V_SELECTED: *out = kp_local_selection_count(g); break;
	case V_UNITS:
		if (ref->arg < 0 || ref->arg >= PLAYERS) {
			snprintf(why, size, "no player %lld (0..%d)", (long long)ref->arg, PLAYERS - 1);
			return -1;
		}
		*out = 0;
		for (n = g->lists[KP_LIST_ACTIVE].head; n > 0; n = g->u.next[n]) *out += g->u.owner[n] == ref->arg;
		break;
	case V_CREEP_TILES:
	case V_CREEP_RECEDING:
		*out = kp_creep_tiles(g, &receding);
		if (ref->id == V_CREEP_RECEDING) *out = receding;
		break;
	case V_TRIGGER_WAITING: *out = g->trig.wait_active[g->local_player] != 0; break;
	case V_PORTRAIT_MS: *out = kp_ui_pending_releases(ui, &ms, &camera) ? (int64_t)ms : 0; break;
	case V_TARGETING: *out = ui->input.targeting != 0; break;
	case V_PLACING: *out = kp_hud_placing(ui) != 0; break;
	case V_GROUPS_HELD: *out = kp_ui_groups_held(ui); break;
	case V_PAD_OPEN: *out = f->hud.pad.open != 0; break;
	case V_PAD_COUNT: *out = f->hud.pad.count; break;
	case V_PAD_SHOWN: *out = kp_hud_pad_point(&f->hud, (int)ref->arg, &x, &y) != 0; break;
	case V_GAMMA: *out = (int64_t)ui->gamma; break;
	case V_PORTRAITS: *out = (int64_t)ui->portrait.mode; break;
	case V_VIDEO_GRAPHICS: *out = ui->video_options.graphics; break;
	case V_VIDEO_FOG: *out = ui->video_options.fog; break;
	case V_VIDEO_WATER: *out = ui->video_options.water; break;
	case V_VIDEO_FONT: *out = ui->video_options.font; break;
	case V_VIDEO_LIGHTING: *out = ui->video_options.lighting; break;
	case V_VIDEO_SERIAL: *out = ui->video_options.serial; break;
	case V_VIDEO_COMMITS: *out = f->video_commits; break;
	case V_TEXT_VECTOR: *out = ui->vector_text != 0; break;
	case V_SELECTION_HAS:
	case V_SELECTION_FIRST:
	case V_MOVED:
		if ((n = picked(run, g, ref->arg, why, size)) == 0) return -1;
		if (ref->id == V_MOVED) {
			double dx = (double)(g->u.x[n] - run->pick_x[ref->arg]), dy = (double)(g->u.y[n] - run->pick_y[ref->arg]);
			*out = (int64_t)sqrt(dx * dx + dy * dy);
		} else if (ref->id == V_SELECTION_FIRST) {
			*out = g->local_selection[0] == n;
		} else {
			for (*out = 0, k = 0; k < KP_SLOTS; k++) *out |= g->local_selection[k] == n;
		}
		break;
	case V_GROUP_HELD:
		if (ref->arg < 0 || ref->arg >= GROUPS) {
			snprintf(why, size, "no group %lld (0..%d)", (long long)ref->arg, GROUPS - 1);
			return -1;
		}
		*out = (kp_ui_groups_held(ui) >> ref->arg) & 1;
		break;
	case V_REPLAY_ENDED: *out = f->session->replay_ended != 0; break;
	case V_REPLAY_FRAMES: *out = f->session->playing ? (int64_t)f->session->replay_frames : 0; break;
	case V_SIM_CRC: {
		uint32_t folded = 0;
		if (kp_dump_crcs(g, 0, fold_crc, &folded) != 0) {
			snprintf(why, size, "no state's CRC (out of memory)");
			return -1;
		}
		*out = folded;
		break;
	}
	default:
		snprintf(why, size, "no value %d", (int)ref->id);
		return -1;
	}
	return 0;
}

int kp_script_flow_cond(const KpFlow *f, const KpScriptRef *ref, char *why, size_t size)
{
	char open[256];
	int holds;
	switch (ref->id) {
	case C_GAME:
		holds = in_game(f);
		if (!holds) {
			snprintf(why, size, "%s", f->session && f->session->failed ? f->session->error : kp_flow_glue_active(f) ? "the glue"
				: f->world && f->hud.has_start ? "the game's first view to come" : "no game in play");
		}
		return holds;
	case C_DIALOG:
	case C_NODIALOG:
		holds = dialog_open(f, ref->id == C_DIALOG ? (int)ref->arg : ANY_DIALOG);
		if (ref->id == C_NODIALOG) holds = !holds;
		if (!holds) {
			kp_script_flow_dialogs(f, open, sizeof open);
			snprintf(why, size, "open: %s", open);
		}
		return holds;
	case C_PAUSED:
		holds = f->session && f->session->g && f->session->clock.paused;
		if (!holds) snprintf(why, size, "%s", f->session && f->session->g ? "the game runs" : "no game");
		return holds;
	case C_GLUE:
	case C_STILL:
	case C_SCREEN:
		holds = ref->id == C_GLUE ? kp_flow_glue_active(f) && f->ui
			: ref->id == C_STILL ? kp_flow_glue_active(f) && f->ui && kp_script_flow_still(f->ui)
			: screen_up(f, ref->arg >= 0 ? (int)ref->arg : ANY_DIALOG);
		if (!holds) glue_why(f, why, size);
		return holds;
	case C_ROOM:
	case C_BRIEFING:
	case C_SCORE:
	case C_EPISODE:
		/* the conditions past room's as the HD check waited for them: the glue's panels standing still */
		holds = kp_flow_glue_active(f) && f->ui && (ref->id == C_ROOM || kp_script_flow_still(f->ui))
			&& (ref->id == C_ROOM ? f->glue.screen == KP_GLUE_ROOM
				: ref->id == C_BRIEFING ? f->glue.screen == KP_GLUE_ROOM && f->glue.room_opened
				: ref->id == C_SCORE ? f->glue.screen == KP_GLUE_SCORE
				: f->glue.screen == KP_GLUE_ROOM || open_count(f->ui) > 1);
		if (!holds) glue_why(f, why, size);
		return holds;
	case C_REPLAY:
		holds = in_game(f) && f->playing_replay;
		if (!holds) snprintf(why, size, "%s", in_game(f) ? "the game is no replay" : "no game in play");
		return holds;
	case C_MOVIE:
	case C_NOMOVIE:
		holds = f->host.cine_active && f->host.cine_active(f->host.ctx);
		if (ref->id == C_NOMOVIE) holds = !holds;
		if (!holds) snprintf(why, size, "%s", ref->id == C_MOVIE ? "no movie plays" : "a movie plays");
		return holds;
	default:
		snprintf(why, size, "no condition %d", (int)ref->id);
		return 0;
	}
}

/* The dialog's place on the screen: on its canvas, or a glue popup composited into its screen at its place within it;
 * 0 when it is not on the screen. */
static int dialog_place(const KpFlow *f, int32_t d, int32_t *x, int32_t *y)
{
	const KpUi *ui = f->ui;
	int glue = kp_flow_glue_active(f);
	const KpCanvas *canvas = glue ? &f->glue.canvas : f->hud.canvas;
	int32_t screen = glue_screen(ui);
	if (!canvas) return 0;
	if (kp_canvas_place(canvas, d, x, y)) return 1;
	if (!glue || screen < 0 || !kp_canvas_place(canvas, screen, x, y)) return 0;
	*x += ui->controls[d].left - ui->controls[screen].left;
	*y += ui->controls[d].top - ui->controls[screen].top;
	return 1;
}

/* The control of the ID in the topmost dialog a script sees that is dialog (ANY_DIALOG: any): its index, its dialog in
 * *at; -1 none. */
static int32_t find_control(KpUi *ui, int dialog, int16_t id, int32_t *at)
{
	int32_t list[CANDIDATES], k;
	int n = candidates(ui, list, CANDIDATES), j;
	for (j = 0; j < n; j++) {
		if (is_dialog(ui, list[j], dialog) && (k = kp_ui_find_control(ui, list[j], id)) >= 0) {
			*at = list[j];
			return k;
		}
	}
	return -1;
}

/* The control's point: its centre, or its right end (a slider's), in the topmost dialog that has it. */
static int control_point(const KpFlow *f, const KpScriptPoint *p, int32_t *x, int32_t *y, char *why, size_t size)
{
	KpUi *ui = f->ui;
	int32_t d = -1, k, px, py;
	int dialog;
	int16_t id;
	const KpUiControl *c;
	char open[256];
	if (p->name < 0) {
		snprintf(why, size, "no control %d", (int)p->name);
		return -1;
	}
	if (!ui) {
		snprintf(why, size, "no interface");
		return -1;
	}
	control_of(p->name, &dialog, &id);
	k = find_control(ui, dialog, id, &d);
	if (k < 0) {
		kp_script_flow_dialogs(f, open, sizeof open);
		if (p->name < CONTROL_COUNT) snprintf(why, size, "no %s open (open: %s)", CONTROLS[p->name].name, open);
		else snprintf(why, size, "no %s.%d open (open: %s)", dialog >= 0 && dialog < DIALOG_COUNT ? dialog_name(dialog) : "?",
			(int)id, open);
		return -1;
	}
	if (!dialog_place(f, d, &px, &py)) {
		snprintf(why, size, "the control's dialog is not on the screen");
		return -1;
	}
	c = &ui->controls[k];
	*x = px + (p->flag ? c->right - RIGHT_END_INSET : (c->left + c->right) / 2);
	*y = py + (c->top + c->bottom) / 2;
	return 0;
}

#define BESIDE_EDGE 8 /* beside PICK D: the point goes left of the unit when right of it is this near the view's edge */

int kp_script_flow_point(const KpFlow *f, const KpScriptRun *run, const KpScriptPoint *p, int32_t *x, int32_t *y,
	char *why, size_t size)
{
	int32_t vw, vh;
	int64_t n;
	switch (p->kind) {
	case KP_SCRIPT_AT_XY:
		*x = p->x;
		*y = p->y;
		break;
	case KP_SCRIPT_AT_WORLD:
		if (!f->world) {
			snprintf(why, size, "no game");
			return -1;
		}
		*x = (int32_t)(p->x - f->cam_x);
		*y = (int32_t)(p->y - f->cam_y);
		break;
	case KP_SCRIPT_AT_CONTROL:
		if (control_point(f, p, x, y, why, size) != 0) return -1;
		break;
	case KP_SCRIPT_AT_CENTER:
		view_size(f, &vw, &vh);
		*x = vw / 2 + p->x;
		*y = vh / 2 + p->y;
		break;
	case KP_SCRIPT_AT_VIEW: /* 100% is the last pixel */
		*x = (int32_t)((int64_t)f->world_w * p->x / 100);
		*y = (int32_t)((int64_t)f->world_h * p->y / 100);
		if (*x >= f->world_w) *x = f->world_w - 1;
		if (*y >= f->world_h) *y = f->world_h - 1;
		break;
	case KP_SCRIPT_AT_UNIT:
	case KP_SCRIPT_AT_BESIDE:
		if (!f->world || !f->session || !f->session->g) {
			snprintf(why, size, "no game");
			return -1;
		}
		if ((n = picked(run, f->session->g, p->name, why, size)) == 0) return -1;
		*x = (int32_t)(f->session->g->u.x[n] - f->cam_x);
		*y = (int32_t)(f->session->g->u.y[n] - f->cam_y);
		if (p->kind == KP_SCRIPT_AT_UNIT) {
			*x += p->x;
			*y += p->y;
		} else {
			view_size(f, &vw, &vh);
			*x = *x + p->x >= vw - BESIDE_EDGE ? *x - p->x : *x + p->x;
		}
		break;
	case KP_SCRIPT_AT_PAD:
	case KP_SCRIPT_AT_PAD_TOGGLE:
		if (!f->hud_on || !kp_hud_pad_point(&f->hud, p->kind == KP_SCRIPT_AT_PAD ? (int)p->name : KP_UI_PAD_TOGGLE, x, y)) {
			snprintf(why, size, "the touch pad shows no %s (%d buttons)", p->kind == KP_SCRIPT_AT_PAD ? "such group" : "toggle",
				f->hud_on ? f->hud.pad.count : 0);
			return -1;
		}
		break;
	default:
		snprintf(why, size, "no point of kind %d", p->kind);
		return -1;
	}
	if (*x < 0 || *y < 0 || *x >= f->world_w || *y >= f->world_h) {
		snprintf(why, size, "%d,%d is off the screen (%dx%d)", (int)*x, (int)*y, (int)f->world_w, (int)f->world_h);
		return -1;
	}
	return 0;
}

/* --- the units a script picks or boxes --- */

#define PICK_MARGIN 16  /* pick: a unit this far inside the view at least */
enum { RACES = 3 };
/* Per race (Zerg, Terran, Protoss, as the worker found tells): its worker, main building and the key of a building the
 * basic build menu places (units.dat ids). */
static const struct {
	int worker, main;
	char place;
} RACE[RACES] = { { 41, 131, 'H' }, { 7, 106, 'S' }, { 64, 154, 'P' } };

/* The local player's race (RACE's index) by its first worker; -1 none. */
static int race_of(const KpGame *g)
{
	int64_t n;
	int r;
	for (n = g->lists[KP_LIST_ACTIVE].head; n > 0; n = g->u.next[n]) {
		if (g->u.owner[n] != g->local_player) continue;
		for (r = 0; r < RACES; r++) {
			if (g->u.type[n] == RACE[r].worker) return r;
		}
	}
	return -1;
}

char kp_script_flow_race_key(const KpFlow *f)
{
	int race = f->session && f->session->g ? race_of(f->session->g) : -1;
	return race >= 0 ? RACE[race].place : 0;
}

/* The local player's first unit of the race's worker or main building; 0 none. */
static int64_t role_unit(const KpGame *g, int role)
{
	int64_t n;
	int race = race_of(g), type;
	if (race < 0) return 0;
	type = role == UNIT_WORKER ? RACE[race].worker : RACE[race].main;
	for (n = g->lists[KP_LIST_ACTIVE].head; n > 0 && (g->u.type[n] != type || g->u.owner[n] != g->local_player);
		n = g->u.next[n]) {}
	return n > 0 ? n : 0;
}

/* Whether a box selects the local player's unit along with others. */
static int groups(KpGame *g, int64_t n) { return g->u.owner[n] == g->local_player && kp_multi_selectable(g, n); }

/* The unit whose box of the radius takes the most units around it, whichever types the mission gave the player
 * (zerg02's Zerg have no Zerglings); 0 none. */
static int64_t group_unit(KpGame *g, int32_t radius)
{
	int64_t n, m, best = 0;
	int most = 0;
	for (n = g->lists[KP_LIST_ACTIVE].head; n > 0; n = g->u.next[n]) {
		int near = 0;
		if (!groups(g, n)) continue;
		for (m = g->lists[KP_LIST_ACTIVE].head; m > 0; m = g->u.next[m]) { /* the distance first: it is the cheap test */
			near += llabs((long long)(g->u.x[m] - g->u.x[n])) <= radius && llabs((long long)(g->u.y[m] - g->u.y[n])) <= radius
				&& groups(g, m);
		}
		if (near > most) {
			most = near;
			best = n;
		}
	}
	return best;
}

int kp_script_flow_select(KpFlow *f, const KpScriptStep *step, int32_t box[4], char *why, size_t size)
{
	KpGame *g = f->world && f->session ? f->session->g : NULL;
	int64_t n;
	int32_t x, y, r = (int32_t)step->n;
	if (!g) {
		snprintf(why, size, "no game");
		return -1;
	}
	n = step->id == UNIT_GROUP ? group_unit(g, r) : role_unit(g, step->id);
	if (n <= 0) {
		snprintf(why, size, "the player has no %s", step->id >= 0 && step->id < UNIT_COUNT ? UNITS[step->id] : "such unit");
		return -1;
	}
	/* the camera on it as the HD check placed it: the logical screen's centre */
	f->cam_x = g->u.x[n] - f->world_w / 2;
	f->cam_y = g->u.y[n] - f->world_h / 2;
	kp_flow_clamp_camera(f);
	x = (int32_t)(g->u.x[n] - f->cam_x);
	y = (int32_t)(g->u.y[n] - f->cam_y);
	box[0] = x - r;
	box[1] = y - r;
	box[2] = x + r;
	box[3] = y + r;
	return 0;
}

/* pick: the local player's unit nearest the view's centre, inside it, not under the touch pad, the one a tap there
 * takes (kp_ui_pick_unit); 0 none. */
static int64_t own_unit(KpFlow *f)
{
	const KpGame *g = f->session->g;
	int32_t vw, vh;
	int64_t n, best = 0, best_d = INT64_MAX;
	view_size(f, &vw, &vh);
	for (n = g->lists[KP_LIST_ACTIVE].head; n > 0; n = g->u.next[n]) {
		int64_t x = g->u.x[n] - f->cam_x, y = g->u.y[n] - f->cam_y, dd;
		if (g->u.owner[n] != g->local_player || x < PICK_MARGIN || y < PICK_MARGIN || x >= vw - PICK_MARGIN
			|| y >= vh - PICK_MARGIN) {
			continue;
		}
		if (f->hud_on && kp_hud_pad_at(&f->hud, (int32_t)x, (int32_t)y) >= 0) continue;
		if (kp_ui_pick_unit(f->ui, (int32_t)g->u.x[n], (int32_t)g->u.y[n]) != n) continue;
		dd = (x - vw / 2) * (x - vw / 2) + (y - vh / 2) * (y - vh / 2);
		if (dd < best_d) {
			best_d = dd;
			best = n;
		}
	}
	return best;
}

/* Whether a save's name fits the profile's (the flow keeps KP_PROFILE_SAVE_NAME bytes: a longer one would be cut,
 * then saved under another name than a check of it looks for); why says when not. */
static int save_name_fits(const char *name, char *why, size_t size)
{
	if (strlen(name) < KP_PROFILE_SAVE_NAME) return 1;
	snprintf(why, size, "a save's name is %d bytes at most", KP_PROFILE_SAVE_NAME - 1);
	return 0;
}

int32_t kp_script_flow_control(const char *dialog, int16_t id)
{
	int d = dialog_index(dialog);
	return d < 0 ? -1 : CONTROL_COUNT + (d << ID_BITS) + (uint16_t)id;
}

int kp_script_flow_act(KpFlow *f, KpScriptRun *run, const KpScriptStep *step, char *why, size_t size)
{
	int32_t vw, vh, at = -1, k;
	int dialog;
	int16_t id;
	int64_t n;
	switch (step->op) {
	case KP_SCRIPT_CAMERA:
		if (!f->world) break;
		view_size(f, &vw, &vh);
		if (step->flag == KP_SCRIPT_CAMERA_AT) {
			f->cam_x = step->p.x - vw / 2;
			f->cam_y = step->p.y - vh / 2;
		} else if (step->flag == KP_SCRIPT_CAMERA_BY) {
			f->cam_x += step->p.x;
			f->cam_y += step->p.y;
		} else {
			f->cam_x = f->cam_y = INT32_MAX; /* the clamp's bottom-right */
		}
		kp_flow_clamp_camera(f);
		return 0;
	case KP_SCRIPT_PAUSE:
	case KP_SCRIPT_RESUME:
		if (!f->session || !f->session->g) break;
		if (step->op == KP_SCRIPT_PAUSE) kp_session_pause(f->session);
		else kp_session_resume(f->session);
		return 0;
	case KP_SCRIPT_SAVE:
	case KP_SCRIPT_LOAD: /* the flow's next frame saves or loads (as the save and load dialogs ask it) */
		if (!in_game(f)) break;
		if (!save_name_fits(step->text, why, size)) return -1;
		snprintf(step->op == KP_SCRIPT_SAVE ? f->pending_save : f->pending_load, KP_PROFILE_SAVE_NAME, "%s", step->text);
		return 0;
	case KP_SCRIPT_LOAD_FILE:
		if (!save_name_fits(step->text, why, size) || kp_flow_load_saved(f, step->text, why, size) != 0) return -1;
		return 0;
	case KP_SCRIPT_REPLAY:
		return kp_flow_play_replay(f, step->text, why, size);
	case KP_SCRIPT_MENU:
	case KP_SCRIPT_OPTIONS_DIALOG:
		if (!in_game(f)) break;
		if (step->op == KP_SCRIPT_MENU) kp_ui_game_menu(f->ui, (int)step->id);
		else kp_ui_options_dialog(f->ui, (int)step->id);
		return 0;
	case KP_SCRIPT_ACTIVATE: /* as its click does: the result, the menu's handler */
		if (!f->ui) {
			snprintf(why, size, "no interface");
			return -1;
		}
		control_of(step->id, &dialog, &id);
		if ((k = find_control(f->ui, dialog, id, &at)) < 0) {
			char open[256];
			kp_script_flow_dialogs(f, open, sizeof open);
			snprintf(why, size, "no such control open (open: %s)", open);
			return -1;
		}
		(void)kp_ui_send_user(f->ui, k, KP_UI_CLOSE, 0);
		return 0;
	case KP_SCRIPT_END_PLACEMENT:
	case KP_SCRIPT_CANCEL_TARGETING:
		if (!in_game(f)) break;
		if (step->op == KP_SCRIPT_END_PLACEMENT) kp_ui_end_placement(f->ui);
		else kp_ui_cancel_targeting(f->ui);
		return 0;
	case KP_SCRIPT_PICK:
		if (!in_game(f) || !run) break;
		if ((n = own_unit(f)) == 0) {
			snprintf(why, size, "no unit of the player's on the screen to pick");
			return -1;
		}
		run->picks[step->id] = kp_unit_ref(f->session->g, n);
		run->pick_x[step->id] = f->session->g->u.x[n];
		run->pick_y[step->id] = f->session->g->u.y[n];
		return 0;
	default:
		return KP_SCRIPT_FLOW_NONE;
	}
	snprintf(why, size, "%s", f->session && f->session->failed ? f->session->error : "no game in play");
	return -1;
}

/* --- the observations --- */

#define UI_LINE 768
#define UI_TEXT 160 /* a control's text quoted at most */

typedef struct {
	char s[UI_LINE];
	size_t n;
} Line;

static void put(Line *l, const char *fmt, ...)
{
	va_list ap;
	int w;
	if (l->n + 1 >= sizeof l->s) return;
	va_start(ap, fmt);
	w = vsnprintf(l->s + l->n, sizeof l->s - l->n, fmt, ap);
	va_end(ap);
	if (w > 0) l->n += (size_t)w < sizeof l->s - l->n ? (size_t)w : sizeof l->s - l->n - 1;
}

static void put_byte(Line *l, char c)
{
	if (l->n + 1 >= sizeof l->s) return;
	l->s[l->n++] = c;
	l->s[l->n] = '\0';
}

/* A text quoted without the font's colour and format codes (the bytes below a space; a line break as \n), its quote and
 * backslash escaped, the rest (UTF-8) as it is; cut at UI_TEXT bytes, before a character the cut would split. */
static void put_text(Line *l, const uint8_t *text)
{
	size_t k, end = 0;
	int back;
	while (text[end] && end < UI_TEXT) end++;
	for (back = 0; text[end] && back < 3 && end > 0 && (text[end] & 0xc0) == 0x80; back++) end--; /* a continuation */
	put(l, " \"");
	for (k = 0; k < end; k++) {
		if (text[k] == '\n') {
			put_byte(l, '\\');
			put_byte(l, 'n');
		} else if (text[k] >= 0x20 && text[k] != 0x7f) {
			if (text[k] == '"' || text[k] == '\\') put_byte(l, '\\');
			put_byte(l, (char)text[k]);
		}
	}
	put(l, text[end] ? "...\"" : "\"");
}

/* The hotkeys `key` names by a word (sdl/kp_app_script.c's KEYS), beside the letters and the digits. */
static const struct {
	uint8_t c;
	const char *name;
} HOTKEYS[] = { { 0x1b, "Escape" }, { '\r', "Return" }, { ' ', "Space" }, { '\t', "Tab" }, { '\b', "Backspace" },
	{ '-', "Minus" }, { '=', "Equals" }, { ',', "Comma" }, { '.', "Period" }, { '/', "Slash" }, { ';', "Semicolon" },
	{ '`', "Grave" } };
#define HOTKEY_COUNT ((int)(sizeof HOTKEYS / sizeof HOTKEYS[0]))

/* A control's text, then its hotkey (the text's first byte with KP_UI_HOTKEY_PREFIX) as `key` names it; one no key
 * name gives is `hotkey \xNN`. */
static void put_label(Line *l, const KpUiControl *c)
{
	const uint8_t *t = c->text;
	uint8_t hotkey = 0;
	int k;
	if (!t || !t[0]) return;
	if (c->flags & KP_UI_HOTKEY_PREFIX) hotkey = *t++;
	if (t[0]) put_text(l, t);
	if (!hotkey) return;
	if (hotkey >= 'a' && hotkey <= 'z') hotkey = (uint8_t)(hotkey - 'a' + 'A');
	if ((hotkey >= 'A' && hotkey <= 'Z') || (hotkey >= '0' && hotkey <= '9')) {
		put(l, " key %c", (char)hotkey);
		return;
	}
	for (k = 0; k < HOTKEY_COUNT; k++) {
		if (HOTKEYS[k].c == hotkey) {
			put(l, " key %s", HOTKEYS[k].name);
			return;
		}
	}
	put(l, " hotkey \\x%02x", hotkey);
}

static void emit_line(KpDumpLine emit, void *ud, const Line *l) { emit(ud, l->s, l->n); }

/* The table's names for control c of dialog d (its BIN's stem, n bytes): the dialog's own, and ok or cancel where
 * `control` takes them (the topmost dialog that has the ID). */
static void put_names(Line *l, KpUi *ui, int32_t d, const char *stem, size_t n, const KpUiControl *c)
{
	int32_t at = -1;
	int k, named = 0;
	for (k = 0; k < CONTROL_COUNT; k++) {
		if (CONTROLS[k].id != c->id) continue;
		if (CONTROLS[k].dialog ? !same(stem, n, CONTROLS[k].dialog)
			: find_control(ui, ANY_DIALOG, c->id, &at) < 0 || at != d) {
			continue;
		}
		put(l, "%s%s", named++ ? ", " : " = ", CONTROLS[k].name);
	}
}

/* A list box's items, its selected one marked. */
static void ui_list(const KpUiList *list, KpDumpLine emit, void *ud)
{
	Line l;
	int k;
	for (k = 0; k < list->count; k++) {
		l.n = 0;
		put(&l, "    item %d", k);
		if (list->text[k]) put_text(&l, list->text[k]);
		if (k == list->selected) put(&l, " selected");
		emit_line(emit, ud, &l);
	}
}

static void ui_dialog(const KpFlow *f, int32_t d, KpDumpLine emit, void *ud)
{
	KpUi *ui = f->ui;
	const KpUiControl *dc = &ui->controls[d];
	const char *name;
	size_t len = bin_stem(ui, d, &name);
	int32_t px = 0, py = 0, c;
	int placed = dialog_place(f, d, &px, &py);
	const char *role = d == ui->modeless ? "modeless" : is_console(ui, d) ? "console"
		: kp_flow_glue_active(f) ? d == glue_screen(ui) ? "screen" : "popup" : "open";
	Line l;
	l.n = 0;
	put(&l, "dialog %.*s", (int)len, len ? name : "?");
	if (placed) put(&l, " box %d,%d %d,%d", (int)px, (int)py, (int)(px + dc->right - dc->left), (int)(py + dc->bottom - dc->top));
	else put(&l, " not on the screen");
	put(&l, " (%s)", role);
	put_label(&l, dc);
	emit_line(emit, ud, &l);
	for (c = dc->child; c >= 0; c = ui->controls[c].next) {
		const KpUiControl *k = &ui->controls[c];
		l.n = 0;
		put(&l, "  %.*s.%d ", (int)len, len ? name : "?", (int)k->id);
		if (k->type < KIND_COUNT) put(&l, "%s", KINDS[k->type]);
		else put(&l, "type%d", (int)k->type);
		if (placed) {
			put(&l, " at %d,%d box %d,%d %d,%d", (int)(px + (k->left + k->right) / 2), (int)(py + (k->top + k->bottom) / 2),
				(int)(px + k->left), (int)(py + k->top), (int)(px + k->right), (int)(py + k->bottom));
		}
		put_label(&l, k);
		if (k->flags & KP_UI_DISABLED) put(&l, " disabled");
		if (!(k->flags & KP_UI_VISIBLE)) put(&l, " hidden");
		if ((k->type == KIND_OPTION || k->type == KIND_CHECKBOX) && (uint8_t)k->words36[0]) put(&l, " checked"); /* as drawn */
		put_names(&l, ui, d, name, len, k);
		emit_line(emit, ud, &l);
		if (k->list) ui_list(k->list, emit, ud);
	}
}

int kp_script_flow_ui(const KpFlow *f, KpDumpLine emit, void *ud, char *why, size_t size)
{
	int32_t list[CANDIDATES], vw, vh;
	int n, k;
	Line l;
	if (!f->ui) {
		snprintf(why, size, "no interface");
		return -1;
	}
	n = candidates(f->ui, list, CANDIDATES);
	l.n = 0;
	put(&l, "# ui: %s, the logical screen %dx%d", kp_flow_glue_active(f) ? "the glue" : in_game(f) ? "a game" : "no game in play",
		(int)f->world_w, (int)f->world_h);
	if (f->world && !kp_flow_glue_active(f)) {
		view_size(f, &vw, &vh);
		put(&l, ", the view %dx%d centred on %lld,%lld", (int)vw, (int)vh, (long long)(f->cam_x + vw / 2),
			(long long)(f->cam_y + vh / 2));
	}
	put(&l, "; %d %s, topmost first; a control is DIALOG.ID to `control`", n, n == 1 ? "dialog" : "dialogs");
	emit_line(emit, ud, &l);
	for (k = 0; k < n; k++) ui_dialog(f, list[k], emit, ud);
	return 0;
}

int kp_script_flow_state(const KpFlow *f, KpDumpLine emit, void *ud, char *why, size_t size)
{
	KpGame *g = game_of(f, why, size);
	if (!g) return -1;
	if (kp_dump(g, 1, emit, ud) != 0) {
		snprintf(why, size, "out of memory");
		return -1;
	}
	return 0;
}
