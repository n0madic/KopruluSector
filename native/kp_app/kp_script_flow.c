/*
 * A script's names over the product's flow (kp_script_flow.h): fixed tables, so a script's typo is a parse error before
 * any data loads; their meaning over the KpFlow of the frame.
 */
#include "kp_script_flow.h"

#include <stdio.h>
#include <string.h>

enum { V_TICK, V_CAMERA_X, V_CAMERA_Y, V_COUNT };
static const char *const VALUES[V_COUNT] = { "tick", "camera.x", "camera.y" };

enum { C_GAME, C_DIALOG, C_NODIALOG, C_PAUSED, C_COUNT };
static const char *const CONDS[C_COUNT] = { "game", "dialog", "nodialog", "paused" };

/* The dialogs by their BIN's name without its directory and .bin (any case): the glue's screens and popups, the game's
 * menus, ours (koprulu\). */
static const char *const DIALOGS[] = { "abrtmenu", "credits", "gamemenu", "gluCmpgn", "gluExpCmpgn", "gluGameMode",
	"gluHist", "gluLoad", "gluLogin", "gluMain", "gluPEdit", "gluPOk", "gluPOkCancel", "gluScore", "glurdyp", "glurdyt",
	"glurdyz", "help", "helpmenu", "language", "lmission", "loadgame", "objctdlg", "ok", "okcancel", "options", "quit",
	"quit2mnu", "restart", "savegame", "snd_dlg", "spd_dlg", "tips_dlg", "video", "wait", "wmission" };
#define DIALOG_COUNT ((int)(sizeof DIALOGS / sizeof DIALOGS[0]))
#define ANY_DIALOG (-1)
#define RIGHT_END_INSET 2 /* control NAME right: the point this far inside the control's right edge (a slider's end) */
#define ID_OK (-2) /* a dialog's OK and Cancel (kp_ui_gamemenu.c's ID_OK, ID_CANCEL) */
#define ID_CANCEL (-3)

/* The controls: a dialog's (ANY_DIALOG: the topmost open dialog that has the ID) and its ID. */
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
};
#define CONTROL_COUNT ((int)(sizeof CONTROLS / sizeof CONTROLS[0]))

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

static int find(const char *const *names, int count, const char *word)
{
	int k;
	for (k = 0; k < count; k++) {
		if (same(word, strlen(word), names[k])) return k;
	}
	return -1;
}

static int dialog_index(const char *name) { return find(DIALOGS, DIALOG_COUNT, name); }

int kp_script_flow_name(KpScriptKind kind, const char *word, KpScriptName *out)
{
	int k = -1;
	out->arg = KP_SCRIPT_ARG_NONE;
	out->arg_kind = KP_SCRIPT_NAME_KEY;
	switch (kind) {
	case KP_SCRIPT_NAME_VALUE: k = find(VALUES, V_COUNT, word); break;
	case KP_SCRIPT_NAME_COND:
		k = find(CONDS, C_COUNT, word);
		if (k == C_DIALOG) {
			out->arg = KP_SCRIPT_ARG_OPT_NAME;
			out->arg_kind = KP_SCRIPT_NAME_DIALOG;
		}
		break;
	case KP_SCRIPT_NAME_DIALOG: k = dialog_index(word); break;
	case KP_SCRIPT_NAME_CONTROL:
		for (k = CONTROL_COUNT - 1; k >= 0 && !same(word, strlen(word), CONTROLS[k].name); k--) {}
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

static int is_dialog(const KpUi *ui, int32_t d, int dialog)
{
	const char *name;
	size_t n = bin_stem(ui, d, &name);
	return dialog == ANY_DIALOG || (dialog >= 0 && dialog < DIALOG_COUNT && same(name, n, DIALOGS[dialog]));
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

void kp_script_flow_dialogs(const KpFlow *f, char *out, size_t size)
{
	int32_t list[CANDIDATES];
	int n, k;
	size_t used = 0;
	out[0] = '\0';
	if (!f->ui) {
		snprintf(out, size, "no interface");
		return;
	}
	n = candidates(f->ui, list, CANDIDATES);
	for (k = 0; k < n && used + 1 < size; k++) {
		const char *name;
		size_t len = bin_stem(f->ui, list[k], &name);
		int w = snprintf(out + used, size - used, "%s%.*s", k ? ", " : "", (int)len, name);
		if (w < 0) break;
		used += (size_t)w < size - used ? (size_t)w : size - used - 1;
	}
	if (n == 0) snprintf(out, size, "no dialog");
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
	if (dialog == ANY_DIALOG) {
		int32_t d;
		if (!kp_flow_glue_active(f)) return f->ui->modeless >= 0;
		for (n = 0, d = f->ui->open; d >= 0; d = f->ui->controls[d].next) n++;
		return n > 1;
	}
	n = candidates(f->ui, list, CANDIDATES);
	for (k = 0; k < n; k++) {
		if (is_dialog(f->ui, list[k], dialog)) return 1;
	}
	return 0;
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

int kp_script_flow_value(const KpFlow *f, const KpScriptRef *ref, int64_t *out, char *why, size_t size)
{
	int32_t vw, vh;
	switch (ref->id) {
	case V_TICK:
		if (!f->session || !f->session->g) break;
		*out = f->session->tick;
		return 0;
	case V_CAMERA_X:
	case V_CAMERA_Y:
		if (!f->world) break;
		view_size(f, &vw, &vh);
		*out = ref->id == V_CAMERA_X ? f->cam_x + vw / 2 : f->cam_y + vh / 2;
		return 0;
	default:
		snprintf(why, size, "no value %d", (int)ref->id);
		return -1;
	}
	snprintf(why, size, "no game");
	return -1;
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
	default:
		snprintf(why, size, "no condition %d", (int)ref->id);
		return 0;
	}
}

/* The control's point: its centre, or its right end (a slider's), in the topmost dialog that has it. */
static int control_point(const KpFlow *f, const KpScriptPoint *p, int32_t *x, int32_t *y, char *why, size_t size)
{
	KpUi *ui = f->ui;
	int glue = kp_flow_glue_active(f);
	const KpCanvas *canvas = glue ? &f->glue.canvas : f->hud.canvas;
	int32_t list[CANDIDATES], d = -1, k = -1, px, py;
	int dialog = ANY_DIALOG, n, j;
	const KpUiControl *c;
	char open[256];
	if (p->name < 0 || p->name >= CONTROL_COUNT) {
		snprintf(why, size, "no control %d", (int)p->name);
		return -1;
	}
	if (!ui || !canvas) {
		snprintf(why, size, "no interface");
		return -1;
	}
	if (CONTROLS[p->name].dialog) dialog = dialog_index(CONTROLS[p->name].dialog);
	n = candidates(ui, list, CANDIDATES);
	for (j = 0; j < n && k < 0; j++) {
		d = list[j];
		if (is_dialog(ui, d, dialog)) k = kp_ui_find_control(ui, d, CONTROLS[p->name].id);
	}
	if (k < 0) {
		kp_script_flow_dialogs(f, open, sizeof open);
		snprintf(why, size, "no %s open (open: %s)", CONTROLS[p->name].name, open);
		return -1;
	}
	if (kp_canvas_place(canvas, d, &px, &py)) {
		/* the dialog has its place on the canvas */
	} else {
		/* a glue popup is composited into its screen, the open list's last */
		int32_t screen = -1, s;
		for (s = ui->open; s >= 0; s = ui->controls[s].next) screen = s;
		if (!glue || screen < 0 || !kp_canvas_place(canvas, screen, &px, &py)) {
			snprintf(why, size, "%s is not on the screen", CONTROLS[p->name].name);
			return -1;
		}
		px += ui->controls[d].left - ui->controls[screen].left;
		py += ui->controls[d].top - ui->controls[screen].top;
	}
	c = &ui->controls[k];
	*x = px + (p->flag ? c->right - RIGHT_END_INSET : (c->left + c->right) / 2);
	*y = py + (c->top + c->bottom) / 2;
	return 0;
}

int kp_script_flow_point(const KpFlow *f, const KpScriptPoint *p, int32_t *x, int32_t *y, char *why, size_t size)
{
	int32_t vw, vh;
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
	default:
		/* TODO: the picks' points and the touch pad's (stage SC5, docs/phase-script.md) */
		snprintf(why, size, "this point comes with stage SC5");
		return -1;
	}
	if (*x < 0 || *y < 0 || *x >= f->world_w || *y >= f->world_h) {
		snprintf(why, size, "%d,%d is off the screen (%dx%d)", (int)*x, (int)*y, (int)f->world_w, (int)f->world_h);
		return -1;
	}
	return 0;
}

int kp_script_flow_act(KpFlow *f, const KpScriptStep *step, char *why, size_t size)
{
	int32_t vw, vh;
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
	default:
		return KP_SCRIPT_FLOW_NONE;
	}
	snprintf(why, size, "no game");
	return -1;
}
