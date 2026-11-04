/* The credits screens (credits.cpp, atlas P05 0x4148b0..0x4150d0): rez\<name>.txt run in rez\credits.bin
 * (RunCreditsScreen 0x415010) - a mission's establishing text before its ready room (ShowMissionEstablishingText
 * 0x414f70), the finale after Zerg 9b (0x414fc0), the credits and the epilogs. The script's text goes to the current
 * position control (1-6), its tags set the background and its palette (shown at the next page with the fades, which
 * are the host's), the colour bank, the fade speed and the page's time; a page ends at </PAGE> and the next one comes
 * when its time ran out (the idle events), at a click, Space or Enter; Escape ends the screen. */
#include "kp_ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kp_game.h"

#define POSITIONS 6
#define FADE_DEFAULT 1
#define TIME_DEFAULT 5000
#define BANK_SIZE 192
#define KEY_SPACE 0x20
#define KEY_ENTER 0x0d
#define KEY_ESCAPE 0x1b
#define DRAW_FLAG 0x80000000u
#define CREDITS_MUSIC 5 /* RunGameCredits 0x414e70's track */
#define EPILOG_MUSIC 9  /* RunCampaignEpilog 0x414ef0's */

/* Show Credits' own first pages, in the format of the game's scripts and with its backgrounds and colour bank: the
 * remake's makers before the original's (crdt_exp, crdt_lst) */
static const char REMAKE_SCRIPT[] =
	"</BACKGROUND glue\\palmm\\backgnd.pcx>\n"
	"</FONTCOLOR glue\\palmm\\tfont.pcx>\n"
	"</SCREENCENTER>\n"
	"Koprulu Sector\n\nA StarCraft remake\n"
	"</PAGE>\n"
	"</SCREENCENTER>\n"
	"Remake created by\n\nn0madic\nAnthropic Claude\n"
	"</PAGE>\n"
	"</SCREENCENTER>\n"
	"Original game\n\nStarCraft\nStarCraft: Brood War\n"
	"</PAGE>\n";
/* 0x414e70: the expansion's credits, then the original's */
static const char *const GAME_SCRIPTS[] = { "rez\\crdt_exp.txt", "rez\\crdt_lst.txt" };
#define GAME_SCRIPT_COUNT (sizeof GAME_SCRIPTS / sizeof GAME_SCRIPTS[0])
/* 0x414ef0 (0x4ec074, 0x4ec07c): the original's epilog and the expansion's (by the run's expansion byte), and the
 * credits after each */
static const char *const EPILOGS[2] = { "epilog", "epilogX" };
static const char *const EPILOG_CREDITS[2] = { "rez\\crdt_lst.txt", "rez\\crdt_exp.txt" };

/* 0x4ebb9f + id: the text format of each position control */
static const uint8_t FORMATS[POSITIONS + 1] = { 0, 0x21, 0x24, 0x22, 0x22, 0x22, 0x41 };
/* 0x4ebb70: the position tags by control ID */
static const char *const POSITION_TAGS[POSITIONS + 1] = { NULL, "</SCREENLEFT>", "</SCREENRIGHT>", "</SCREENTOP>",
	"</SCREENBOTTOM>", "</SCREENCENTER>", "</SCREENLOWERLEFT>" };

static KpUiCredits *cr(KpUi *ui) { return &ui->credits; }

static int prefix(const char *s, size_t n, const char *p)
{
	size_t k = strlen(p);
	size_t i;
	if (n < k) return 0;
	for (i = 0; i < k; i++) {
		char a = s[i], b = p[i];
		if (a >= 'a' && a <= 'z') a = (char)(a - 32);
		if (b >= 'a' && b <= 'z') b = (char)(b - 32);
		if (a != b) return 0;
	}
	return 1;
}

/* NextCreditsToken 0x4150d0: a text or a tag (with its < and >), 0 at the end or on an error. */
static size_t token(KpUiCredits *c, const char **out, int *tag)
{
	size_t len = 0;
	int blank = 1, inside = 0;
	*tag = 0;
	*out = c->text + c->at;
	while (c->at < c->size) {
		char ch = c->text[c->at];
		if (ch == '<') {
			if (len != 0 && !blank) return len;
			len = 0;
			*out = c->text + c->at;
			if (inside) return 0;
			inside = 1;
			*tag = 1;
		} else if (ch == '>') {
			if (!inside) return 0;
			c->at++;
			return len + 1;
		} else if (ch != '\n' && ch != '\r' && ch != '\t' && ch != ' ' && ch != 0x1a) {
			blank = 0;
		}
		c->at++;
		len++;
	}
	return inside || blank ? 0 : len;
}

static void set_text(KpUi *ui, const char *text, size_t len)
{
	/* SetCreditsControlText 0x414910 */
	KpUiCredits *c = cr(ui);
	KpUiControl *k;
	int id;
	if (c->current < 0) return;
	k = &ui->controls[c->current];
	id = k->id;
	if (id >= 1 && id <= POSITIONS && k->text != NULL) {
		k->text = NULL;
		kp_ui_hide_control(ui, c->current);
	}
	if (len == 0 || id < 1 || id > POSITIONS) return;
	if (len > KP_UI_CREDITS_TEXT - 1) len = KP_UI_CREDITS_TEXT - 1;
	memcpy(c->texts[id], text, len);
	c->texts[id][len] = 0;
	k->text = (const uint8_t *)c->texts[id];
	kp_ui_mark_dirty(ui, c->current);
	kp_ui_show_control(ui, c->current);
}

static void load_background(KpUi *ui, const char *name, size_t len)
{
	/* CreditsTagBackground 0x414a90 */
	KpUiCredits *c = cr(ui);
	KpUiControl *d = &ui->controls[c->dialog];
	char path[KP_UI_CREDITS_TEXT];
	const KpUiFile *f;
	int32_t w, h;
	size_t k;
	if (len >= sizeof path) len = sizeof path - 1;
	memcpy(path, name, len);
	path[len] = 0;
	kp_ui_surface_free(ui, c->background.pixels);
	c->background.pixels = NULL;
	d->image = NULL;
	f = kp_ui_glue_file(ui, path);
	if (f == NULL || f->size < 128 + 769 || kp_ui_pcx(f->data, f->size, &w, &h, NULL, 0) != 0) return;
	c->background.pixels = (uint8_t *)malloc((size_t)w * (size_t)h);
	if (c->background.pixels == NULL) return;
	(void)kp_ui_pcx(f->data, f->size, &w, &h, c->background.pixels, (size_t)w * (size_t)h);
	c->background.w = w;
	c->background.h = h;
	kp_ui_picture(ui, c->background.pixels, w, h, f); /* the HD interface's (ADR 0013 R4d) */
	d->image = c->background.pixels;
	d->image_w = (uint16_t)w;
	d->image_h = (uint16_t)h;
	d->owns_image = 0;
	for (k = 0; k < 256; k++) {
		const uint8_t *p = f->data + f->size - 768 + 3 * k;
		c->palette[4 * k] = p[0];
		c->palette[4 * k + 1] = p[1];
		c->palette[4 * k + 2] = p[2];
		c->palette[4 * k + 3] = 0;
	}
	c->pending = 1;
}

static void load_colours(KpUi *ui, const char *name, size_t len)
{
	/* CreditsTagFontColor 0x414b80: SBmpLoadImage into a 192-byte stack buffer; the credits' tfont.pcx files are 48x1,
	 * so the rows past the sixth are the stack's leftovers there and 0 here (the scripts' texts use the first rows) */
	char path[KP_UI_CREDITS_TEXT];
	const KpUiFile *f;
	uint8_t bank[BANK_SIZE];
	int32_t w, h;
	if (len >= sizeof path) len = sizeof path - 1;
	memcpy(path, name, len);
	path[len] = 0;
	f = kp_ui_glue_file(ui, path);
	memset(bank, 0, sizeof bank);
	if (f != NULL && kp_ui_pcx(f->data, f->size, &w, &h, NULL, 0) == 0) {
		size_t n = (size_t)w * (size_t)h;
		uint8_t *all = (uint8_t *)malloc(n);
		if (all != NULL && kp_ui_pcx(f->data, f->size, &w, &h, all, n) == 0) {
			memcpy(bank, all, n < BANK_SIZE ? n : BANK_SIZE);
		}
		free(all);
	}
	kp_ui_text_set_rows(&ui->text, bank, BANK_SIZE);
}

static long number(const char *s, size_t n)
{
	char buf[32];
	if (n >= sizeof buf) n = sizeof buf - 1;
	memcpy(buf, s, n);
	buf[n] = 0;
	return strtol(buf, NULL, 10);
}

/* ExecuteCreditsTag 0x414ce0: 1 for </PAGE>. */
static int tag(KpUi *ui, const char *t, size_t len)
{
	KpUiCredits *c = cr(ui);
	int id;
	if (len > 14 && prefix(t, len, "</BACKGROUND ")) {
		load_background(ui, t + 13, len - 14);
	} else if (len > 13 && prefix(t, len, "</FONTCOLOR ")) {
		load_colours(ui, t + 12, len - 13);
	} else if (len > 13 && prefix(t, len, "</FADESPEED ")) {
		long v = number(t + 12, len - 13);
		if (v != 0) c->fade_speed = (uint8_t)v;
	} else if (len > 15 && prefix(t, len, "</DISPLAYTIME ")) {
		long v = number(t + 14, len - 15);
		if (v != 0) c->display_time = (uint32_t)v;
	} else if (len == 7 && prefix(t, len, "</PAGE>")) {
		if (c->pending) { /* CreditsTagPage 0x414bd0: the fades are the host's */
			c->pending = 0;
			memcpy(ui->glue.palette, c->palette, sizeof c->palette);
			ui->glue.palette_serial++;
			kp_ui_mark_dirty(ui, c->dialog);
		}
		return 1;
	} else {
		for (id = POSITIONS; id >= 1; id--) {
			if (len == strlen(POSITION_TAGS[id]) && prefix(t, len, POSITION_TAGS[id])) {
				if (c->current >= 0) kp_ui_hide_control(ui, c->current);
				c->current = kp_ui_find_control(ui, c->dialog, (int16_t)id);
				break;
			}
		}
	}
	return 0;
}

/* ShowNextCreditsPage 0x414c60: 1 with a page shown, 0 when the script ended (the dialog closed). */
static int next_page(KpUi *ui)
{
	KpUiCredits *c = cr(ui);
	if (ui->video_trace && ui->page_log_count < KP_UI_VIDEO_TRACE) ui->page_log[ui->page_log_count++] = ui->now;
	for (;;) {
		const char *t;
		int is_tag;
		size_t len;
		if (c->at >= c->size) break;
		len = token(c, &t, &is_tag);
		if (len == 0) break;
		if (!is_tag) {
			set_text(ui, t, len);
		} else if (tag(ui, t, len)) {
			c->deadline = ui->now + c->display_time; /* the original's GetTickCount is after the page's fades */
			if (ui->page_deadlines != NULL && ui->page_deadline_next < ui->page_deadline_count) {
				c->deadline = ui->page_deadlines[ui->page_deadline_next++];
			}
			return 1;
		}
	}
	kp_ui_close_dialog(ui, c->dialog);
	c->done = 1;
	return 0;
}

static void draw_text(KpUi *ui, KpUiControl *k, int32_t x, int32_t y, const int16_t clip[4])
{
	/* the position controls' draw 0x414c20 */
	(void)x;
	(void)y;
	(void)clip;
	k->flags |= DRAW_FLAG;
	kp_ui_draw_control_text(ui, k, 0, 0, NULL, k->id >= 0 && k->id <= POSITIONS ? FORMATS[k->id] : 0x11);
}

static int credits_proc(KpUi *ui, KpUiControl *d, KpUiEvent *e)
{
	/* the credits dialog's handler 0x4149f0 */
	KpUiCredits *c = cr(ui);
	KpUiInteract base = kp_ui_type_interact(0);
	switch (e->type) {
	case KP_UI_KEY_DOWN:
		if ((e->key & 0xff) == KEY_SPACE || (e->key & 0xff) == KEY_ENTER) {
			if (!next_page(ui)) return 0;
		} else if ((e->key & 0xff) == KEY_ESCAPE) {
			c->aborted = 1;
			kp_ui_close_dialog(ui, c->dialog);
			c->done = 1;
			return 0;
		}
		break;
	case KP_UI_LEFT_DOWN: case KP_UI_RIGHT_DOWN:
		if (!next_page(ui)) return 0;
		break;
	case KP_UI_IDLE: /* CreditsDialogTick 0x414e20 */
		if ((int32_t)(ui->now - c->deadline) > 0 && !next_page(ui)) return 0;
		break;
	case KP_UI_USER:
		if (e->code == KP_UI_CREATE) { /* CreditsDialogInit 0x4148b0 */
			int id;
			for (id = POSITIONS; id >= 1; id--) {
				int32_t k = kp_ui_find_control(ui, c->dialog, (int16_t)id);
				if (k < 0) continue;
				ui->controls[k].text = NULL;
				ui->controls[k].draw = draw_text;
			}
			c->fade_speed = FADE_DEFAULT;
			c->display_time = TIME_DEFAULT;
			if (!next_page(ui)) return 0;
		} else if (e->code == KP_UI_DESTROY) { /* CreditsDialogDestroy 0x414990: the fade out is the host's */
			ui->glue.palette_serial++;
		}
		break;
	default: break;
	}
	return base(ui, d, e);
}

static int32_t open_script(KpUi *ui, const char *text, size_t size)
{
	KpUiCredits *c = cr(ui);
	int32_t d;
	memset(c->texts, 0, sizeof c->texts);
	c->text = text;
	c->size = size;
	c->at = 0;
	c->aborted = c->done = c->pending = 0;
	c->current = -1;
	d = kp_ui_load_dialog(ui, "rez\\credits.bin");
	if (d < 0) return -1;
	c->dialog = d;
	kp_ui_init_dialog(ui, d, credits_proc);
	return d;
}

int32_t kp_ui_credits_open(KpUi *ui, const char *name)
{
	/* RunCreditsScreen 0x415010 */
	char path[KP_UI_CREDITS_TEXT];
	const KpUiFile *f;
	snprintf(path, sizeof path, "rez\\%s.txt", name);
	f = kp_ui_glue_file(ui, path);
	if (f == NULL) return -1;
	return open_script(ui, (const char *)f->data, f->size);
}

/* A credits screen still standing goes first: its dialog, its script. */
static void close_standing(KpUi *ui)
{
	KpUiCredits *c = cr(ui);
	if (c->dialog >= 0 && ui->controls[c->dialog].used && ui->controls[c->dialog].interact == credits_proc) {
		kp_ui_destroy_dialog(ui, c->dialog);
	}
	c->dialog = -1;
	free(c->owned);
	c->owned = NULL;
	c->text = NULL;
	c->size = 0;
	c->then = NULL;
}

/* One screen of the remake's pages, then the game's scripts[0..n) that are there: the dialog's number or -1 (the
 * script made here is released then). */
static int32_t open_with_remake(KpUi *ui, const char *const *scripts, size_t n)
{
	KpUiCredits *c = cr(ui);
	const KpUiFile *files[GAME_SCRIPT_COUNT];
	size_t size = sizeof REMAKE_SCRIPT - 1, at, k;
	int32_t d;
	close_standing(ui);
	if (n > GAME_SCRIPT_COUNT) n = GAME_SCRIPT_COUNT;
	for (k = 0; k < n; k++) {
		files[k] = kp_ui_glue_file(ui, scripts[k]);
		if (files[k] != NULL) size += files[k]->size;
	}
	c->owned = (char *)malloc(size);
	if (c->owned == NULL) return -1;
	memcpy(c->owned, REMAKE_SCRIPT, sizeof REMAKE_SCRIPT - 1);
	at = sizeof REMAKE_SCRIPT - 1;
	for (k = 0; k < n; k++) {
		if (files[k] == NULL) continue;
		memcpy(c->owned + at, files[k]->data, files[k]->size);
		at += files[k]->size;
	}
	if ((d = open_script(ui, c->owned, size)) < 0) close_standing(ui);
	return d;
}

/* The end of a credits screen that ended: its dialog and script, the music (0x46f040). */
static int finish(KpUi *ui)
{
	close_standing(ui);
	kp_ui_music_stop(ui);
	return 1;
}

int32_t kp_ui_credits_game_open(KpUi *ui)
{
	/* RunGameCredits 0x414e70: one screen for both scripts (an Escape in the first ends the second too, as the
	 * original's aborted flag does) */
	int32_t d = open_with_remake(ui, GAME_SCRIPTS, GAME_SCRIPT_COUNT);
	if (d < 0) return -1; /* the menu's music goes on */
	kp_ui_music_stop(ui);
	kp_ui_music_play(ui, CREDITS_MUSIC);
	return d;
}

int kp_ui_credits_game_close(KpUi *ui) { return cr(ui)->done ? finish(ui) : 0; }

int32_t kp_ui_credits_epilog_open(KpUi *ui, int expansion)
{
	/* RunCampaignEpilog 0x414ef0 */
	KpUiCredits *c = cr(ui);
	const char *credits = EPILOG_CREDITS[expansion != 0];
	int32_t d;
	close_standing(ui);
	d = kp_ui_credits_open(ui, EPILOGS[expansion != 0]);
	if (d >= 0) c->then = credits;
	else d = open_with_remake(ui, &credits, 1);
	if (d < 0) return -1;
	kp_ui_music_stop(ui);
	kp_ui_music_play(ui, EPILOG_MUSIC);
	return d;
}

int kp_ui_credits_epilog_close(KpUi *ui)
{
	KpUiCredits *c = cr(ui);
	const char *then = c->then;
	if (!c->done) return 0;
	/* the text ended: its credits, the music going on (the original's 0x46f040 is after both) */
	if (then != NULL && open_with_remake(ui, &then, 1) >= 0) return 0;
	return finish(ui);
}

const char *kp_ui_establishing_text(KpUi *ui, uint16_t mission, int finale)
{
	/* ShowMissionEstablishingText 0x414f70 / ShowMissionFinaleText 0x414fc0: the table's entry of the mission key */
	const uint8_t *p = ui->establishing;
	size_t at = 1;
	int n, k;
	static char name[64];
	if (p == NULL || mission == 0) return NULL;
	n = p[0];
	for (k = 0; k < n && at < ui->establishing_size; k++) {
		size_t len = p[at];
		uint16_t key = (uint16_t)(p[at + 1 + len] | p[at + 2 + len] << 8);
		int is_finale = k == n - 1;
		if (is_finale == (finale != 0) && key == mission && len < sizeof name) {
			memcpy(name, p + at + 1, len);
			name[len] = 0;
			return name;
		}
		at += 3 + len;
	}
	return NULL;
}

void kp_ui_credits_free(KpUi *ui)
{
	kp_ui_surface_free(ui, cr(ui)->background.pixels);
	cr(ui)->background.pixels = NULL;
	free(cr(ui)->owned);
	cr(ui)->owned = NULL;
}
