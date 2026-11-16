/* The save-replay dialog (replay.cpp, atlas P28 0x480900..0x481450): RunSaveReplayDialog 0x4809d0 runs
 * rez\savegame.bin modal over the score screen in the frame of the local race and result (glue\pal<r><d|v>\tile.grp,
 * which kp_ui_glue_tile_grp makes from dlgs\tile.grp), lists the replays folder's .rep files (EnumerateReplayFiles
 * 0x4803d0 through the host), takes a name and copies LastReplay.rep to it (SaveReplayAs 0x4802c0 with the overwrite
 * question 0x480900), a failure in an OK box; a saved replay relabels the score's button. The original hides the
 * dialog's Delete (SaveReplayDialogActivate 0x480c80; its procedure 0x481160 would delete the name the dialog last
 * closed with). Koprulu shows it: the selected replay is deleted after the saves' question (network.tbl 4), a failure
 * in an OK box (network.tbl 1), and the dialog opens again. The files are the host's (ui->list_replays,
 * replay_exists, copy_last_replay, delete_replay). */
#include "kp_text.h"
#include "kp_ui.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#define ID_OK (-2)
#define ID_CANCEL (-3)
#define ID_LIST 1
#define ID_EDIT 2
#define ID_DELETE 3
#define ID_TITLE 4
#define PROCS 4                      /* 0x508e54: the list's, the edit's, Delete's and the end (16 bytes) */
#define GLUE_THEME 0                 /* InitGlueDialog's theme argument (a nested dialog keeps the screen's) */
#define FLAG_TILED 0x20000000u
#define FLAG_EDIT_OUTLINE 0x80000000u /* SaveReplayEditProc 0x481250: +0x18 bit 31 (the focus outline) */
#define EDIT_MARGIN 3                /* and the text inset +0x38 */
#define TILED_OK 3
#define TILED_DELETE 4               /* Koprulu's shown Delete: the in-game save dialog's art */
#define TILED_CANCEL 5
#define LISTED_MAX 28                /* EnumerateReplayFiles 0x4803d0: names of 28 characters or more stay unlisted */
#define LIST_OWNED 0x10              /* AddReplayNameToList 0x481300: the row's text copied */
#define NAME_BYTES 256               /* a listed file's name at most (longer ones are past LISTED_MAX anyway) */
#define VK_DELETE 0x2e
#define GLU_TITLE 0xb3               /* gluAll: the dialog's title */
#define NET_DELETE_FAILED 1          /* network.tbl */
#define NET_REPLACE 3
#define NET_DELETE 4
#define NET_SAVE_FAILED 0x65
#define NAME_FONT 2                  /* 0x6d302c: the font ConfirmReplayOverwrite 0x480900 measures the name in */
#define NAME_WIDTH 230               /* 0xe6: the name's width before its "..." */
#define ELLIPSIS "..."               /* 0x4ecdec */
#define LAST_REPLAY "LastReplay"
#define BACKGROUNDS 6
#define TILE_COLOURS 2
#define GRP_HEADER 6
#define GRP_FRAME 8
#define RLE_SKIP 0x80
#define RLE_RUN 0x40
#define SAVED 1                      /* SaveReplayAs's outcomes: copied (or LastReplay itself), not copied, declined */
#define NOT_SAVED 0

/* 1.17's glue\pal<r><d|v>\tile.grp is dlgs\tile.grp with its pixels of colours 42 and 41 in two colours of the score
 * palette (only the frames' pixel bytes; checked byte for byte), by 0x508e3c's order: Zd, Td, Pd, Zv, Tv, Pv. */
static const uint8_t TILE_FROM[TILE_COLOURS] = { 42, 41 };
static const uint8_t TILE_TO[BACKGROUNDS][TILE_COLOURS] = { { 160, 151 }, { 146, 134 }, { 134, 122 }, { 127, 112 },
	{ 123, 106 }, { 145, 134 } };

static void open_dialog(KpUi *ui);

/* SStrCmpI: equal with ASCII's case folded (SaveReplayAs's LastReplay test; the list's skip is case-sensitive). */
static int same_name(const char *a, const char *b)
{
	while (*a != 0 && tolower((unsigned char)*a) == tolower((unsigned char)*b)) {
		a++;
		b++;
	}
	return tolower((unsigned char)*a) == tolower((unsigned char)*b);
}

static uint8_t remap(int background, uint8_t colour)
{
	int k;
	for (k = 0; k < TILE_COLOURS; k++) {
		if (colour == TILE_FROM[k]) return TILE_TO[background][k];
	}
	return colour;
}

size_t kp_ui_glue_tile_grp(const uint8_t *grp, size_t size, int background, uint8_t *out, size_t out_size)
{
	size_t frames, f;
	if (grp == NULL || out == NULL || size < GRP_HEADER || size > out_size || background < 0 || background >= BACKGROUNDS) {
		return 0;
	}
	frames = kp_le_u16(grp);
	if (GRP_HEADER + GRP_FRAME * frames > size) return 0;
	memcpy(out, grp, size);
	for (f = 0; f < frames; f++) {
		const uint8_t *rec = grp + GRP_HEADER + GRP_FRAME * f;
		size_t off = kp_le_u32(rec + 4) & 0x7fffffffu, w = rec[2], h = rec[3], y;
		if (off + 2 * h > size) return 0;
		for (y = 0; y < h; y++) { /* the line's packets until its width: only their pixel bytes change */
			size_t at = off + kp_le_u16(grp + off + 2 * y), x = 0, n, k;
			while (x < w) {
				uint8_t b;
				if (at >= size) return 0;
				b = grp[at++];
				if (b & RLE_SKIP) {
					x += b & 0x7f;
					continue;
				}
				n = b & RLE_RUN ? 1 : b;
				if (at + n > size) return 0;
				for (k = 0; k < n; k++) out[at + k] = remap(background, grp[at + k]);
				x += b & RLE_RUN ? (size_t)(b & 0x3f) : b;
				at += n;
			}
		}
	}
	return size;
}

/* --- the dialog's controls --- */

static void copy_selection(KpUi *ui, int32_t list)
{
	/* CopyReplaySelectionToEdit 0x481450 */
	int32_t dialog = ui->controls[list].parent, edit = kp_ui_find_control(ui, dialog, ID_EDIT), ok;
	int s = kp_ui_list_selection(ui, list);
	if (edit < 0 || s == KP_UI_LIST_NONE) return;
	kp_ui_edit_set_text(ui, edit, kp_ui_list_text(ui, list, s));
	kp_ui_edit_scroll_home(ui, edit);
	if ((ok = kp_ui_find_control(ui, dialog, ID_OK)) >= 0) kp_ui_set_enabled(ui, ok, 1);
}

void kp_ui_replay_listed(KpUi *ui, const char *file)
{
	/* EnumerateReplayFiles 0x4803d0's rules, AddReplayNameToList 0x481300 */
	char name[NAME_BYTES];
	const char *dot = strrchr(file, '.');
	size_t n = dot != NULL ? (size_t)(dot - file) : strlen(file);
	if (ui->replay_save.list < 0 || n >= sizeof name) return;
	memcpy(name, file, n);
	name[n] = 0;
	if (kp_text_chars((const uint8_t *)name) >= LISTED_MAX || strcmp(name, LAST_REPLAY) == 0) return;
	(void)kp_ui_list_add(ui, ui->replay_save.list, (const uint8_t *)name, LIST_OWNED);
}

static void fill(KpUi *ui, int32_t list)
{
	/* FillReplayList 0x481370 */
	int32_t dialog = ui->controls[list].parent, ok;
	kp_ui_list_redraw(ui, list, 0);
	ui->replay_save.list = list;
	if (ui->list_replays != NULL) ui->list_replays(ui->host, ui);
	ui->replay_save.list = -1;
	kp_ui_list_redraw(ui, list, 1);
	if (kp_ui_list_count(ui, list) == 0 && (ok = kp_ui_find_control(ui, dialog, ID_OK)) >= 0) kp_ui_set_enabled(ui, ok, 0);
	kp_ui_update_delete_button(ui, dialog, list);
	copy_selection(ui, list);
}

static int list_proc(KpUi *ui, KpUiControl *c, KpUiEvent *e)
{
	/* SaveReplayListProc 0x481320: filled before the list's own creation */
	int32_t self = (int32_t)(c - ui->controls);
	if (e->type == KP_UI_USER && e->code == KP_UI_CREATE) {
		fill(ui, self);
	} else if (e->type == KP_UI_USER && e->code == KP_UI_VALUE_CHANGED) {
		(void)kp_ui_list_proc(ui, c, e);
		copy_selection(ui, self);
		kp_ui_update_delete_button(ui, c->parent, self);
		return 1;
	}
	return kp_ui_list_proc(ui, c, e);
}

static int edit_proc(KpUi *ui, KpUiControl *c, KpUiEvent *e)
{
	/* SaveReplayEditProc 0x481250 */
	int32_t self = (int32_t)(c - ui->controls), list;
	if (e->type == KP_UI_USER && e->code == KP_UI_CREATE) {
		c->flags |= FLAG_EDIT_OUTLINE;
		kp_ui_edit_set_margin(ui, self, EDIT_MARGIN);
	} else if (e->type == KP_UI_CHAR) {
		return kp_ui_save_name_key(ui, c, e);
	} else if (e->type == KP_UI_KEY_DOWN && e->key == VK_DELETE && c->text != NULL
		&& kp_ui_edit_cursor(ui, self) < strlen((const char *)c->text)) { /* a character deleted: the name is typed */
		list = kp_ui_find_control(ui, c->parent, ID_LIST);
		if (list >= 0 && kp_ui_list_selection(ui, list) != KP_UI_LIST_NONE) {
			(void)kp_ui_send_user(ui, list, KP_UI_VALUE_CHANGED, KP_UI_LIST_NONE);
			kp_ui_mark_dirty(ui, list);
			kp_ui_update_delete_button(ui, c->parent, list);
		}
	}
	return kp_ui_edit_proc(ui, c, e);
}

/* --- the dialog --- */

static void tiled(KpUi *ui, int32_t dialog, int16_t id, int index)
{
	int32_t c = kp_ui_find_control(ui, dialog, id);
	if (c >= 0) kp_ui_set_user_word(&ui->controls[c], index, FLAG_TILED);
}

static void trim(char *name)
{
	/* 0x4814b0: the trailing spaces off */
	size_t n;
	for (n = strlen(name); n > 0 && name[n - 1] == ' '; n--) name[n - 1] = 0;
}

static void activate(KpUi *ui, int32_t dialog)
{
	/* SaveReplayDialogActivate 0x480c80 (Delete shown: Koprulu's) */
	int32_t c;
	kp_ui_glue_activate(ui, dialog, ui->glue.theme);
	tiled(ui, dialog, ID_CANCEL, TILED_CANCEL);
	tiled(ui, dialog, ID_OK, TILED_OK);
	tiled(ui, dialog, ID_DELETE, TILED_DELETE);
	if ((c = kp_ui_find_control(ui, dialog, ID_LIST)) >= 0) kp_ui_list_select(ui, c, ui->replay_save.index);
	if ((c = kp_ui_find_control(ui, dialog, ID_TITLE)) >= 0) kp_ui_set_control_text(ui, c, kp_ui_tbl(ui, KP_UI_GLUALL, GLU_TITLE));
}

static int dialog_proc(KpUi *ui, KpUiControl *d, KpUiEvent *e)
{
	/* SaveReplayDialogProc 0x4811a0 */
	static const KpUiInteract CHILDREN[PROCS] = { list_proc, edit_proc, kp_ui_menu_button_activate_proc, NULL };
	int32_t self = (int32_t)(d - ui->controls), c;
	KpUiInteract base = kp_ui_type_interact(0);
	if (e->type == KP_UI_USER) {
		switch (e->code) {
		case KP_UI_POST_LOAD: /* SaveReplayDialogInit 0x480db0 */
			kp_ui_glue_init_dialog(ui, self, CHILDREN, PROCS, GLUE_THEME);
			kp_ui_tile_frame_grp(ui, self, &ui->replay_save.tile_file);
			kp_ui_glue_border_colour(ui, self);
			break;
		case KP_UI_CREATE:
			activate(ui, self);
			break;
		case KP_UI_CLOSE: /* the typed name kept; Koprulu's Delete asks about the selected row */
			if ((c = kp_ui_find_control(ui, self, ID_EDIT)) >= 0 && ui->controls[c].text != NULL) {
				snprintf(ui->replay_save.name, sizeof ui->replay_save.name, "%s", (const char *)ui->controls[c].text);
				trim(ui->replay_save.name);
			}
			ui->replay_save.deleting[0] = 0;
			if (ui->result == ID_DELETE && (c = kp_ui_find_control(ui, self, ID_LIST)) >= 0
				&& kp_ui_list_selection(ui, c) != KP_UI_LIST_NONE) {
				snprintf(ui->replay_save.deleting, sizeof ui->replay_save.deleting, "%s",
					(const char *)kp_ui_list_text(ui, c, kp_ui_list_selection(ui, c)));
			}
			(void)base(ui, d, e);
			kp_ui_end_dialog(ui, self); /* RunModal returns the result */
			return 1;
		case KP_UI_DESTROY: /* SaveReplayDialogDestroy 0x480d00 */
			c = kp_ui_find_control(ui, self, ID_LIST);
			ui->replay_save.index = (uint8_t)(c >= 0 ? kp_ui_list_selection(ui, c) : KP_UI_LIST_NONE);
			if ((c = kp_ui_find_control(ui, self, ID_EDIT)) >= 0 && ui->controls[c].text != NULL) {
				snprintf(ui->replay_save.name, sizeof ui->replay_save.name, "%s", (const char *)ui->controls[c].text);
				trim(ui->replay_save.name);
			}
			kp_ui_glue_release_dialog(ui, self);
			break;
		default: break;
		}
	}
	return base(ui, d, e);
}

/* --- the results --- */

/* ConfirmReplayOverwrite 0x480900's text: network.tbl string with the name, cut to NAME_WIDTH pixels of the font with
 * "..." after it. */
static void name_text(KpUi *ui, char *out, size_t size, int string, const char *name)
{
	char cut[KP_UI_SAVE_NAME + sizeof ELLIPSIS];
	KpUiText measure = ui->text; /* the font selected on a copy: selecting resets the text state's metrics and clip */
	size_t n;
	n = kp_ui_text_select_font(&measure, ui->fonts[NAME_FONT], ui->font_sizes[NAME_FONT]) == 0
			&& ui->fonts[NAME_FONT] != NULL
		? kp_ui_text_fit_length(&measure, (const uint8_t *)name, NAME_WIDTH)
		: strlen(name);
	if (n > KP_UI_SAVE_NAME - 1) n = KP_UI_SAVE_NAME - 1;
	memcpy(cut, name, n);
	cut[n] = 0;
	if (n != strlen(name)) memcpy(cut + n, ELLIPSIS, sizeof ELLIPSIS);
	kp_ui_format_name_xy(out, size, kp_ui_tbl(ui, KP_UI_NETWORK, (uint8_t)string), (const uint8_t *)cut, 0, 0);
}

static void name_box(KpUi *ui, int string, const char *name, const char *bin, KpUiModalDone done)
{
	char text[KP_UI_POPUP_TEXT];
	name_text(ui, text, sizeof text, string, name);
	(void)kp_ui_glue_box(ui, text, bin, &ui->replay_save.tile_file, done);
}

static void saved(KpUi *ui, int outcome)
{
	/* RunSaveReplayDialog's caller, SaveReplayButtonProc 0x4620c0: a copied replay relabels its button */
	if (outcome == SAVED) kp_ui_score_replay_saved(ui);
}

static void copy_last(KpUi *ui)
{
	/* CopyLastReplayTo 0x47fe60 (the host's), then RunSaveReplayDialog's failure box (network.tbl 0x65) */
	if (ui->copy_last_replay != NULL && ui->copy_last_replay(ui->host, ui->replay_save.name)) {
		saved(ui, SAVED);
		return;
	}
	(void)kp_ui_glue_box(ui, (const char *)kp_ui_tbl(ui, KP_UI_NETWORK, NET_SAVE_FAILED), "rez\\ok.bin",
		&ui->replay_save.tile_file, NULL);
}

static void overwrite_answered(KpUi *ui, int32_t result)
{
	/* ConfirmReplayOverwrite 0x480900's answer: "no" leaves the replay unsaved */
	if ((int16_t)result == ID_OK) copy_last(ui);
}

static void save_as(KpUi *ui)
{
	/* SaveReplayAs 0x4802c0 (the folder's creation is the host's copy) */
	const char *name = ui->replay_save.name;
	if (same_name(name, LAST_REPLAY)) { /* LastReplay is already there */
		saved(ui, SAVED);
		return;
	}
	if (ui->replay_exists != NULL && ui->replay_exists(ui->host, name)) {
		name_box(ui, NET_REPLACE, name, "rez\\okcancel.bin", overwrite_answered);
		return;
	}
	copy_last(ui);
}

static void reopen(KpUi *ui, int32_t result)
{
	(void)result;
	ui->replay_save.index = 0;
	open_dialog(ui);
}

static void delete_answered(KpUi *ui, int32_t result)
{
	/* Koprulu's: as the saves' ConfirmDeleteSaveCallback 0x49ae00, the dialog again after it */
	if ((int16_t)result == ID_OK
		&& (ui->delete_replay == NULL || !ui->delete_replay(ui->host, ui->replay_save.deleting))) {
		name_box(ui, NET_DELETE_FAILED, ui->replay_save.deleting, "rez\\ok.bin", reopen);
		return;
	}
	reopen(ui, result);
}

static void dialog_done(KpUi *ui, int32_t result)
{
	/* RunSaveReplayDialog 0x4809d0's results: OK saves, Koprulu's Delete asks, Cancel ends */
	switch ((int16_t)result) {
	case ID_OK: save_as(ui); break;
	case ID_DELETE:
		if (ui->replay_save.deleting[0] != 0) {
			name_box(ui, NET_DELETE, ui->replay_save.deleting, "rez\\okcancel.bin", delete_answered);
		}
		break;
	default: break;
	}
}

static void open_dialog(KpUi *ui) { (void)kp_ui_modal(ui, "rez\\savegame.bin", dialog_proc, dialog_done); }

int32_t kp_ui_replay_save_open(KpUi *ui, int race, int victory)
{
	/* RunSaveReplayDialog 0x4809d0: the background 0x508e3c[race + (victory ? 3 : 0)] */
	const KpUiFile *tile = kp_ui_file(ui->grps, ui->grp_count, "dlgs\\tile.grp");
	int background = (race >= 0 && race < 3 ? race : 0) + (victory ? 3 : 0);
	size_t n = tile != NULL ? kp_ui_glue_tile_grp(tile->data, tile->size, background, ui->replay_save.tile,
		sizeof ui->replay_save.tile) : 0;
	ui->replay_save.race = race;
	ui->replay_save.victory = victory;
	ui->replay_save.index = 0;
	memset(&ui->replay_save.tile_file, 0, sizeof ui->replay_save.tile_file);
	ui->replay_save.tile_file.data = ui->replay_save.tile;
	ui->replay_save.tile_file.size = n;
	return kp_ui_modal(ui, "rez\\savegame.bin", dialog_proc, dialog_done);
}
