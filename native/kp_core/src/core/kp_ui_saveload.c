/* The in-game message boxes (okcancel.cpp, atlas P27 0x47acd0..0x47b0a0) and the save and load dialogs (saveload.cpp,
 * P31 0x49ae00..0x49b710): rez\ok.bin, rez\okcancel.bin and rez\wait.bin as modeless dialogs with a completion
 * callback; rez\savegame.bin lists the saved games (the host's: u:saves, a save the game made added first), takes a
 * name and sends the save command 0x06, with the overwrite and delete confirmations; rez\loadgame.bin sends the load
 * command 0x07. The files are the host's: the game's commands reach it as KP_EVENT_SAVE_GAME / KP_EVENT_LOAD_GAME, a
 * delete as a call (ui->delete_save). */
#include "kp_text.h"
#include "kp_ui.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#define ID_OK (-2)
#define ID_CANCEL (-3)
#define ID_TEXT 1
#define ID_LIST 1
#define ID_EDIT 2
#define ID_DELETE 3
#define FLAG_TILED 0x20000000u
#define FLAG_EDIT_FILTER 0x80000000u /* SaveNameEditProc 0x49b4d0: +0x18 bit 31 (the focus outline) */
#define SAVE_EDIT_MARGIN 3           /* and the text inset +0x38 */
#define TILED_OK 3
#define TILED_MENU 4
#define TILED_CANCEL 5
#define FORMAT_BOX 0x28              /* DrawMessageBoxText 0x47acb0 */
#define SAVES_MAX 128
#define SAVE_NAME_MAX 27             /* SaveNameEditKeyFilter 0x49b260: at most 27 characters */
#define CMD_SAVE 0x06                /* SendSaveGameCommand 0x4740e0: {0x06, time, name} */
#define CMD_LOAD 0x07                /* SendLoadGameCommand 0x473f30: {0x07, id, name} */
#define CMD_NAME KP_UI_SAVE_NAME /* the original's 28 bytes, in UTF-8 */
#define NET_DELETE_FAILED 1          /* network.tbl */
#define NET_REPLACE_FAILED 2
#define NET_REPLACE 3
#define NET_DELETE 4
#define NET_TOO_MANY 5
#define NET_RESERVED 6               /* "That file name is a reserved name..." */
#define SAVE_NAME_LIMIT 28           /* IsInvalidFileName 0x408300's limit in SaveDialogConfirm */
#define NET_FORBIDDEN 0x11
#define NET_SAVING 0x6d

typedef void (*BoxDone)(KpUi *ui, int32_t result);

static void open_save_dialog(KpUi *ui);

static void end_dialog(KpUi *ui, int32_t dialog)
{
	kp_ui_destroy_dialog(ui, dialog);
	ui->abort = 1;
}

static void back_to_game_menu(KpUi *ui, int32_t dialog)
{
	if (--ui->menu_depth == 0) {
		end_dialog(ui, dialog);
	} else {
		ui->menu_depth--;
		kp_ui_game_menu(ui, KP_UI_GAME_MENU);
	}
}

static void tiled(KpUi *ui, int32_t dialog, int16_t id, int index)
{
	int32_t c = kp_ui_find_control(ui, dialog, id);
	if (c >= 0) kp_ui_set_user_word(&ui->controls[c], index, FLAG_TILED);
}

/* --- the message boxes and the wait box --- */

static void draw_box_text(KpUi *ui, KpUiControl *c, int32_t x, int32_t y, const int16_t clip[4])
{
	/* DrawMessageBoxText 0x47acb0 */
	(void)x;
	(void)y;
	(void)clip;
	if (c->text != NULL) kp_ui_draw_control_text(ui, c, 0, 0, NULL, FORMAT_BOX);
}

static int box_proc(KpUi *ui, KpUiControl *d, KpUiEvent *e)
{
	/* MessageBoxProc 0x47afa0 (the in-game style without the frame: the border colour) */
	int32_t self = (int32_t)(d - ui->controls), t;
	if (e->type == KP_UI_USER) {
		switch (e->code) {
		case KP_UI_CREATE: /* InitMessageBox 0x47aec0 */
			kp_ui_tile_frame(ui, self);
			kp_ui_border_colour(ui, self);
			tiled(ui, self, ID_OK, TILED_OK);
			tiled(ui, self, ID_CANCEL, TILED_CANCEL);
			if ((t = kp_ui_find_control(ui, self, ID_TEXT)) >= 0) {
				ui->controls[t].text = (const uint8_t *)ui->box.text;
				ui->controls[t].draw = draw_box_text;
			}
			break;
		case KP_UI_DESTROY: /* DestroyMessageBox 0x47af60 */
			if ((t = kp_ui_find_control(ui, self, ID_TEXT)) >= 0) ui->controls[t].text = NULL;
			ui->box.text[0] = 0;
			ui->box.open = 0;
			kp_ui_close_modeless(ui, 0);
			(void)kp_ui_modeless_default(ui, d, e);
			return 1;
		case KP_UI_CLOSE: {
			BoxDone done = (BoxDone)ui->box.done;
			end_dialog(ui, self);
			ui->box.done = NULL;
			if (done != NULL) done(ui, ui->result);
			return 1;
		}
		default: break;
		}
	}
	return kp_ui_modeless_default(ui, d, e);
}

static void message_box(KpUi *ui, const char *text, BoxDone done, const char *bin)
{
	/* ShowGameMessageBox 0x47ae20 */
	if (ui->box.open || ui->box.done != NULL) return;
	ui->box.done = (void (*)(struct KpUi *, int32_t))done;
	snprintf(ui->box.text, sizeof ui->box.text, "%s", text);
	ui->box.open = 1;
	(void)kp_ui_open_modeless(ui, bin, box_proc);
}

/* A box with network.tbl string `string` naming a save (the save dialogs' questions and errors). */
static void name_box(KpUi *ui, int string, const char *name, BoxDone done, const char *bin)
{
	char buf[KP_UI_POPUP_TEXT];
	kp_ui_format_name_xy(buf, sizeof buf, kp_ui_tbl(ui, KP_UI_NETWORK, string), (const uint8_t *)name, 0, 0);
	message_box(ui, buf, done, bin);
}

static int wait_proc(KpUi *ui, KpUiControl *d, KpUiEvent *e)
{
	/* WaitDialogProc 0x47b0a0 */
	int32_t self = (int32_t)(d - ui->controls), t;
	if (e->type == KP_UI_USER && e->code == KP_UI_CREATE) {
		kp_ui_tile_frame(ui, self);
		kp_ui_border_colour(ui, self);
		if ((t = kp_ui_find_control(ui, self, ID_TEXT)) >= 0) {
			ui->controls[t].text = (const uint8_t *)ui->box.wait;
			ui->controls[t].draw = draw_box_text;
		}
		kp_ui_cursor_set(ui, 1);
	} else if (e->type == KP_UI_USER && e->code == KP_UI_DESTROY) {
		if ((t = kp_ui_find_control(ui, self, ID_TEXT)) >= 0) ui->controls[t].text = NULL;
		ui->box.wait[0] = 0;
	}
	return kp_ui_modeless_default(ui, d, e);
}

/* --- the saved games (the host's list) --- */

static int save_index_of(const KpUi *ui, const char *name)
{
	int k;
	for (k = 0; k < ui->menus.save_count; k++) {
		const char *a = ui->menus.saves[k], *b = name;
		while (*a && *b && tolower((unsigned char)*a) == tolower((unsigned char)*b)) {
			a++;
			b++;
		}
		if (*a == 0 && *b == 0) return k; /* _mbsicmp */
	}
	return -1;
}

int kp_ui_delete_save(KpUi *ui, const char *name)
{
	/* DeleteSaveFile 0x498b30: the host deletes the file and the list loses the name (the original lists the files
	 * again: a file that stays keeps it) */
	int k;
	if (ui->delete_save != NULL && !ui->delete_save(ui->host, name)) return 0;
	if ((k = save_index_of(ui, name)) < 0) return 1;
	memmove(ui->menus.saves[k], ui->menus.saves[k + 1], sizeof ui->menus.saves[0] * (size_t)(ui->menus.save_count - k - 1));
	ui->menus.save_count--;
	return 1;
}

static void send_save(KpUi *ui, const char *name)
{
	/* SendSaveGameCommand 0x4740e0 (the time stamp: the host's clock in seconds) */
	uint8_t cmd[1 + 4 + CMD_NAME];
	uint32_t t = ui->menus.time_seed;
	memset(cmd, 0, sizeof cmd);
	cmd[0] = CMD_SAVE;
	cmd[1] = (uint8_t)t;
	cmd[2] = (uint8_t)(t >> 8);
	cmd[3] = (uint8_t)(t >> 16);
	cmd[4] = (uint8_t)(t >> 24);
	snprintf((char *)cmd + 5, CMD_NAME, "%s", name);
	kp_ui_queue_command(ui, cmd, 5 + strlen((const char *)cmd + 5) + 1);
}

static void reopen_save(KpUi *ui, int32_t result)
{
	/* ReopenSaveDialog 0x49b420 */
	(void)result;
	ui->menu_depth = 0;
	open_save_dialog(ui);
}

static void confirm_delete(KpUi *ui, int32_t result)
{
	/* ConfirmDeleteSaveCallback 0x49ae00 */
	if ((int16_t)result == ID_OK) {
		if (!kp_ui_delete_save(ui, ui->save.name)) {
			name_box(ui, NET_DELETE_FAILED, ui->save.name, reopen_save, "rez\\ok.bin"); /* the save dialog after it */
			return;
		}
		ui->save.index = 0;
		ui->save.name[0] = 0;
	}
	ui->menu_depth = 0;
	open_save_dialog(ui);
}

static void confirm_overwrite(KpUi *ui, int32_t result)
{
	/* ConfirmOverwriteSaveCallback 0x49b010 */
	if ((int16_t)result != ID_OK) {
		ui->menu_depth = 0;
		open_save_dialog(ui);
		return;
	}
	if (!kp_ui_delete_save(ui, ui->save.name)) {
		name_box(ui, NET_REPLACE_FAILED, ui->save.name, reopen_save, "rez\\ok.bin");
		return;
	}
	send_save(ui, ui->save.name);
}

static const char *list_text(KpUi *ui, int32_t dialog)
{
	int32_t l = kp_ui_find_control(ui, dialog, ID_LIST);
	int s = l >= 0 ? kp_ui_list_selection(ui, l) : KP_UI_LIST_NONE;
	return s != KP_UI_LIST_NONE ? (const char *)kp_ui_list_text(ui, l, s) : NULL;
}

static void update_delete(KpUi *ui, int32_t dialog, int32_t list)
{
	/* UpdateDeleteButton 0x49b360 */
	int32_t del = kp_ui_find_control(ui, dialog, ID_DELETE);
	if (del < 0) return;
	kp_ui_set_enabled(ui, del, kp_ui_list_count(ui, list) != 0 && kp_ui_list_selection(ui, list) != KP_UI_LIST_NONE);
}

static int save_list_proc(KpUi *ui, KpUiControl *c, KpUiEvent *e)
{
	/* SaveListProc 0x49b500 with FillSaveList 0x49b570 */
	int32_t self = (int32_t)(c - ui->controls), edit, ok;
	int k, s;
	if (e->type == KP_UI_USER && e->code == KP_UI_CREATE) {
		int r = kp_ui_list_proc(ui, c, e);
		kp_ui_list_redraw(ui, self, 0);
		for (k = 0; k < ui->menus.save_count; k++) (void)kp_ui_list_add(ui, self, (const uint8_t *)ui->menus.saves[k], 0);
		kp_ui_list_redraw(ui, self, 1);
		if (kp_ui_list_count(ui, self) == 0 && (ok = kp_ui_find_control(ui, c->parent, ID_OK)) >= 0) {
			kp_ui_set_enabled(ui, ok, 0);
		}
		update_delete(ui, c->parent, self);
		return r;
	}
	if (e->type == KP_UI_USER && e->code == KP_UI_VALUE_CHANGED) {
		(void)kp_ui_list_proc(ui, c, e);
		edit = kp_ui_find_control(ui, c->parent, ID_EDIT);
		s = kp_ui_list_selection(ui, self);
		if (edit >= 0 && s != KP_UI_LIST_NONE) kp_ui_edit_set_text(ui, edit, kp_ui_list_text(ui, self, s));
		update_delete(ui, c->parent, self);
		return 1;
	}
	return kp_ui_list_proc(ui, c, e);
}

static int save_edit_proc(KpUi *ui, KpUiControl *c, KpUiEvent *e)
{
	/* SaveNameEditProc 0x49b4d0 with SaveNameEditKeyFilter 0x49b260 */
	int32_t self = (int32_t)(c - ui->controls), ok, list;
	if (e->type == KP_UI_USER && e->code == KP_UI_CREATE) {
		c->flags |= FLAG_EDIT_FILTER; /* the focus outline */
		kp_ui_edit_set_margin(ui, self, SAVE_EDIT_MARGIN);
	} else if (e->type == KP_UI_CHAR) {
		const uint8_t *forbidden = kp_ui_tbl(ui, KP_UI_NETWORK, NET_FORBIDDEN);
		uint16_t ch = e->key;
		size_t n = c->text != NULL ? kp_text_chars(c->text) : 0;
		if (ch == 0 || (ch < KP_TEXT_ASCII_END && forbidden != NULL && strchr((const char *)forbidden, ch) != NULL)) return 1;
		if (ch == 8 || (n < SAVE_NAME_MAX && !(ch == ' ' && n == 0))) (void)kp_ui_edit_proc(ui, c, e);
		if ((ok = kp_ui_find_control(ui, c->parent, ID_OK)) >= 0) {
			kp_ui_set_enabled(ui, ok, c->text != NULL && c->text[0] != 0);
		}
		list = kp_ui_find_control(ui, c->parent, ID_LIST);
		if (list < 0 || kp_ui_list_selection(ui, list) == KP_UI_LIST_NONE) return 1;
		(void)kp_ui_send_user(ui, list, KP_UI_VALUE_CHANGED, KP_UI_LIST_NONE);
		kp_ui_mark_dirty(ui, list);
		update_delete(ui, c->parent, list);
		(void)self;
		return 1;
	}
	return kp_ui_edit_proc(ui, c, e);
}

static void dialog_init(KpUi *ui, KpUiControl *d, KpUiEvent *e)
{
	/* SaveLoadDialogInit 0x49b610 */
	static const KpUiInteract CHILDREN[4] = { save_list_proc, save_edit_proc, kp_ui_menu_button_activate_proc, NULL };
	int32_t self = (int32_t)(d - ui->controls), list, edit;
	kp_ui_tile_frame(ui, self);
	kp_ui_border_colour(ui, self);
	kp_ui_bind_interacts(ui, self, CHILDREN, 4);
	tiled(ui, self, ID_CANCEL, TILED_CANCEL);
	tiled(ui, self, ID_OK, TILED_OK);
	tiled(ui, self, ID_DELETE, TILED_MENU);
	(void)kp_ui_modeless_default(ui, d, e); /* the children's creation: the list fills */
	list = kp_ui_find_control(ui, self, ID_LIST);
	if (list >= 0) (void)kp_ui_send_user(ui, list, KP_UI_VALUE_CHANGED, ui->save.index);
	if ((edit = kp_ui_find_control(ui, self, ID_EDIT)) >= 0) {
		if (ui->save.name[0] != 0) {
			kp_ui_edit_set_text(ui, edit, (const uint8_t *)ui->save.name);
		} else if (list >= 0 && (int)ui->save.index < kp_ui_list_count(ui, list)) {
			kp_ui_edit_set_text(ui, edit, kp_ui_list_text(ui, list, ui->save.index));
		}
	}
	ui->menu_depth++;
}

static void dialog_destroy(KpUi *ui, KpUiControl *d, KpUiEvent *e)
{
	/* SaveLoadDialogDestroy 0x49b710 */
	int32_t self = (int32_t)(d - ui->controls), list = kp_ui_find_control(ui, self, ID_LIST), edit;
	size_t n;
	ui->save.index = (uint8_t)(list >= 0 ? kp_ui_list_selection(ui, list) : KP_UI_LIST_NONE);
	if ((edit = kp_ui_find_control(ui, self, ID_EDIT)) >= 0 && ui->controls[edit].text != NULL) {
		snprintf(ui->save.name, sizeof ui->save.name, "%s", (const char *)ui->controls[edit].text);
		for (n = strlen(ui->save.name); n > 0 && ui->save.name[n - 1] == ' '; n--) ui->save.name[n - 1] = 0;
	}
	kp_ui_close_modeless(ui, 0);
	(void)kp_ui_modeless_default(ui, d, e);
}

int kp_ui_invalid_file_name(KpUi *ui, const char *name, size_t limit, int allow_path)
{
	static const char *const RESERVED[25] = { "nul", "con", "prn", "aux", "conin$", "conout$", "clock$", "com1", "com2",
		"com3", "com4", "com5", "com6", "com7", "com8", "com9", "lpt1", "lpt2", "lpt3", "lpt4", "lpt5", "lpt6", "lpt7",
		"lpt8", "lpt9" }; /* 0x5025b8 */
	const uint8_t *forbidden = kp_ui_tbl(ui, KP_UI_NETWORK, NET_FORBIDDEN);
	char set[64];
	const char *dot, *slash;
	size_t n = 0, k, len;
	for (k = 0; name[k] != 0; k++) {
		if ((uint8_t)name[k] >= 0x01 && (uint8_t)name[k] <= 0x1f) return 1;
	}
	if (kp_text_chars((const uint8_t *)name) >= limit) return 1; /* 0x476e20: not terminated within the limit */
	for (k = 0; forbidden != NULL && forbidden[k] != 0 && n < sizeof set - 1; k++) {
		if (allow_path && (forbidden[k] == '/' || forbidden[k] == '\\')) continue;
		set[n++] = (char)forbidden[k];
	}
	set[n] = 0;
	if (isalpha((unsigned char)name[0]) && name[1] == ':') name += 2;
	if (strpbrk(name, set) != NULL) return 1;
	if (allow_path) {
		slash = strrchr(name, '\\');
		dot = strrchr(name, '/');
		if (dot != NULL && (slash == NULL || dot > slash)) slash = dot;
		if (slash != NULL) name = slash + 1;
	}
	if (name[0] == 0) return 1;
	dot = strrchr(name, '.');
	len = dot != NULL ? (size_t)(dot - name) : strlen(name);
	for (k = 0; k < sizeof RESERVED / sizeof RESERVED[0]; k++) {
		size_t r = strlen(RESERVED[k]), m = len > r ? len : r, i;
		for (i = 0; i < m; i++) { /* _strnicmp */
			int a = tolower((unsigned char)name[i]), b = tolower((unsigned char)RESERVED[k][i]);
			if (a != b || a == 0) break;
		}
		if (i == m || (name[i] == 0 && RESERVED[k][i] == 0)) return 1;
	}
	return 0;
}

static void save_confirm(KpUi *ui, int32_t dialog)
{
	/* SaveDialogConfirm 0x49b120 */
	int32_t edit = kp_ui_find_control(ui, dialog, ID_EDIT);
	size_t n;
	snprintf(ui->save.name, sizeof ui->save.name, "%s",
		edit >= 0 && ui->controls[edit].text != NULL ? (const char *)ui->controls[edit].text : "");
	for (n = strlen(ui->save.name); n > 0 && ui->save.name[n - 1] == ' '; n--) ui->save.name[n - 1] = 0;
	if (save_index_of(ui, ui->save.name) >= 0) {
		name_box(ui, NET_REPLACE, ui->save.name, confirm_overwrite, "rez\\okcancel.bin");
		return;
	}
	if (ui->menus.save_count >= SAVES_MAX) {
		message_box(ui, (const char *)kp_ui_tbl(ui, KP_UI_NETWORK, NET_TOO_MANY), reopen_save, "rez\\ok.bin");
		return;
	}
	if (kp_ui_invalid_file_name(ui, ui->save.name, SAVE_NAME_LIMIT, 0)) {
		message_box(ui, (const char *)kp_ui_tbl(ui, KP_UI_NETWORK, NET_RESERVED), reopen_save, "rez\\ok.bin");
		return;
	}
	send_save(ui, ui->save.name);
	end_dialog(ui, dialog);
}

static void save_delete(KpUi *ui, int32_t dialog)
{
	/* SaveDialogDeleteButton 0x49ae90 */
	const char *s = list_text(ui, dialog);
	if (s == NULL) return;
	snprintf(ui->save.name, sizeof ui->save.name, "%s", s);
	name_box(ui, NET_DELETE, s, confirm_delete, "rez\\okcancel.bin");
}

static int save_proc(KpUi *ui, KpUiControl *d, KpUiEvent *e)
{
	/* SaveDialogProc 0x49b490 */
	int32_t self = (int32_t)(d - ui->controls);
	if (e->type == KP_UI_USER) {
		switch (e->code) {
		case KP_UI_CREATE: dialog_init(ui, d, e); return 1;
		case KP_UI_DESTROY: dialog_destroy(ui, d, e); return 1;
		case KP_UI_CLOSE: /* SaveDialogActivate 0x49b440 */
			if (ui->result == ID_DELETE) save_delete(ui, self);
			else if (ui->result == ID_OK) {
				ui->menu_depth = 0;
				save_confirm(ui, self);
			} else if (ui->result == ID_CANCEL) back_to_game_menu(ui, self);
			return 1;
		default: break;
		}
	}
	return kp_ui_modeless_default(ui, d, e);
}

static int load_proc(KpUi *ui, KpUiControl *d, KpUiEvent *e)
{
	/* LoadDialogProc 0x49b3e0 */
	int32_t self = (int32_t)(d - ui->controls);
	if (e->type == KP_UI_USER) {
		switch (e->code) {
		case KP_UI_CREATE: dialog_init(ui, d, e); return 1;
		case KP_UI_DESTROY: dialog_destroy(ui, d, e); return 1;
		case KP_UI_CLOSE: /* LoadDialogActivate 0x49b3a0 */
			if (ui->result == ID_OK) { /* LoadDialogConfirm 0x49afa0 */
				const char *s = list_text(ui, self);
				int k = s != NULL ? save_index_of(ui, s) : -1;
				if (k >= 0) {
					uint8_t cmd[1 + 4 + CMD_NAME];
					memset(cmd, 0, sizeof cmd);
					cmd[0] = CMD_LOAD;
					cmd[1] = (uint8_t)k; /* the save's ID: its place in the host's list */
					snprintf((char *)cmd + 5, CMD_NAME, "%s", s);
					kp_ui_queue_command(ui, cmd, 5 + strlen((const char *)cmd + 5) + 1);
				}
				end_dialog(ui, self);
			} else if (ui->result == ID_CANCEL) {
				back_to_game_menu(ui, self);
			}
			return 1;
		default: break;
		}
	}
	return kp_ui_modeless_default(ui, d, e);
}

static void open_save_dialog(KpUi *ui) { (void)kp_ui_open_modeless(ui, "rez\\savegame.bin", save_proc); }

void kp_ui_save_dialog(KpUi *ui, int load)
{
	/* OpenSaveGameDialog 0x49b0b0 / OpenLoadGameDialog 0x49af50 (a single player's) */
	if (ui->menus.multiplayer) return;
	ui->save.index = 0;
	ui->save.name[0] = 0;
	if (load) (void)kp_ui_open_modeless(ui, "rez\\loadgame.bin", load_proc);
	else open_save_dialog(ui);
}

void kp_ui_saving(KpUi *ui, const char *name)
{
	/* SaveGameFile 0x498d20's wait box around the save (ShowWaitDialog 0x47b030, CloseWaitDialog 0x47b090): the host
	 * writes the file while it shows */
	const uint8_t *text = kp_ui_tbl(ui, KP_UI_NETWORK, NET_SAVING);
	snprintf(ui->box.wait, sizeof ui->box.wait, "%s\"%s\"", text != NULL ? (const char *)text : "", name);
	if (save_index_of(ui, name) < 0 && ui->menus.save_count < KP_UI_SAVES) { /* the next enumeration has it first */
		memmove(ui->menus.saves[1], ui->menus.saves[0], sizeof ui->menus.saves[0] * (size_t)ui->menus.save_count);
		snprintf(ui->menus.saves[0], sizeof ui->menus.saves[0], "%s", name);
		ui->menus.save_count++;
	}
	(void)kp_ui_open_modeless(ui, "rez\\wait.bin", wait_proc);
	kp_ui_present(ui);
	kp_ui_close_modeless(ui, 1);
	ui->menu_depth = 0;
}
