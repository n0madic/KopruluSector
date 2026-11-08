/* The dialog framework of kp_ui (kp_ui.h; dlg.cpp, ctrl.cpp: atlas P42 0x4c1470..0x4c3770, P43): the containers,
 * the control pool, loading, initializing and destroying dialogs, the default procedures' user events, and painting
 * a dialog into its off-screen bitmap. */
#include "kp_text.h"
#include "kp_ui.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WIDGET_SIZE KP_UI_WIDGET_SIZE
#define SMK_SIZE 28
#define NONE16 0xffffu
#define NONE32 0xffffffffu
#define SHOW_TIMER 12        /* InitDialog's timer: the show delays */
#define SHOW_PERIOD 100
#define DIALOG_FLAGS 0x44000000u
#define CHILD_ID_MASK 0xffff

static void fail(char *err, size_t n, const char *msg)
{
	if (err != NULL && n > 0) snprintf(err, n, "ui: %s", msg);
}

/* A files section (DGRP, PCXS): u16 count; per file u8 path length, path, u32 size, bytes. */
static int parse_files(const KpKpfSection *s, KpUiFile **out, int *count)
{
	size_t pos = 2;
	int i, n;
	if (s == NULL || s->size < 2) return -1;
	n = (int)kp_le_u16(s->data);
	*out = (KpUiFile *)calloc((size_t)(n > 0 ? n : 1), sizeof(KpUiFile));
	if (*out == NULL) return -1;
	for (i = 0; i < n; i++) {
		size_t len, size;
		if (pos + 1 > s->size) return -1;
		len = s->data[pos];
		if (pos + 1 + len + 4 > s->size) return -1;
		size = kp_le_u32(s->data + pos + 1 + len);
		if (pos + 5 + len + size > s->size) return -1;
		(*out)[i].path = (const char *)s->data + pos + 1;
		(*out)[i].path_len = len;
		(*out)[i].data = s->data + pos + 5 + len;
		(*out)[i].size = size;
		pos += 5 + len + size;
	}
	*count = n;
	return 0;
}

static int parse_dialogs(KpUi *ui, const KpKpfSection *s)
{
	size_t pos = 2;
	int i, n;
	if (s == NULL || s->size < 2) return -1;
	n = (int)kp_le_u16(s->data);
	ui->bins = (KpUiBin *)calloc((size_t)(n > 0 ? n : 1), sizeof(KpUiBin));
	if (ui->bins == NULL) return -1;
	for (i = 0; i < n; i++) {
		KpUiBin *b = &ui->bins[i];
		size_t len;
		if (pos + 1 > s->size) return -1;
		len = s->data[pos];
		if (pos + 1 + len + 4 > s->size) return -1;
		b->name = (const char *)s->data + pos + 1;
		b->name_len = len;
		pos += 1 + len;
		b->widget_count = (int)kp_le_u16(s->data + pos);
		b->smk_count = (int)kp_le_u16(s->data + pos + 2);
		pos += 4;
		b->widgets = s->data + pos;
		pos += (size_t)b->widget_count * WIDGET_SIZE;
		b->smks = s->data + pos;
		pos += (size_t)b->smk_count * SMK_SIZE;
		if (pos > s->size) return -1;
	}
	ui->bin_count = n;
	return 0;
}

static int path_equal(const char *a, size_t n, const char *b)
{
	size_t i;
	if (strlen(b) != n) return 0;
	for (i = 0; i < n; i++) {
		char x = a[i], y = b[i];
		if (x >= 'A' && x <= 'Z') x = (char)(x + 32);
		if (y >= 'A' && y <= 'Z') y = (char)(y + 32);
		if (x != y) return 0;
	}
	return 1;
}

const KpUiFile *kp_ui_file(const KpUiFile *files, int count, const char *path)
{
	int i;
	for (i = 0; i < count; i++) {
		if (path_equal(files[i].path, files[i].path_len, path)) return &files[i];
	}
	return NULL;
}

const KpUiFile *kp_ui_glue_file(KpUi *ui, const char *path)
{
	const KpUiFile *f = kp_ui_file(ui->locale_files, ui->locale_file_count, path);
	return f != NULL ? f : kp_ui_file(ui->glue_files, ui->glue_file_count, path);
}

/* The bitmap of an image control (the loader's fixup 0x4c33e0: a type-5 control's text names its PCX), decoded into
 * the control's own pixels. A file the glue lacks leaves the control without an image. */
static void load_image(KpUi *ui, KpUiControl *k)
{
	const KpUiFile *f;
	int32_t w, h;
	if (k->type != 5 || k->text == NULL || (f = kp_ui_glue_file(ui, (const char *)k->text)) == NULL) return;
	if (kp_ui_pcx(f->data, f->size, &w, &h, NULL, 0) != 0 || w <= 0 || h <= 0) return;
	k->image = (uint8_t *)malloc((size_t)w * (size_t)h);
	if (k->image == NULL) return;
	if (kp_ui_pcx(f->data, f->size, &w, &h, k->image, (size_t)w * (size_t)h) != 0) {
		free(k->image);
		k->image = NULL;
		return;
	}
	k->image_w = (uint16_t)w;
	k->image_h = (uint16_t)h;
	k->owns_image = 1;
	kp_ui_picture(ui, k->image, w, h, f); /* the HD interface's (ADR 0013 R4d) */
}

static void reset_pool(KpUi *ui)
{
	int i;
	for (i = 0; i < KP_UI_CONTROLS; i++) {
		ui->controls[i].used = 0;
		ui->controls[i].next = i + 1 < KP_UI_CONTROLS ? i + 1 : -1;
	}
	ui->free_control = 0;
}

KpUi *kp_ui_new(const uint8_t *ui_kpd, size_t ui_size, const uint8_t *fonts_kpd, size_t fonts_size, char *err,
	size_t err_size)
{
	static const char *const FONT_SECTIONS[KP_UI_FNT_FONTS] = { "F8  ", "F10 ", "F14 ", "F16 ", "F16X" };
	KpUi *ui = (KpUi *)calloc(1, sizeof(KpUi));
	KpError e;
	const KpKpfSection *pool, *rows;
	int i;
	if (ui == NULL) {
		fail(err, err_size, "out of memory");
		return NULL;
	}
	for (i = 0; i < 256; i++) ui->remap[i] = (uint8_t)i;
	for (i = 0; i < 12 * 8; i++) ui->player_colours[i / 8][i % 8] = (uint8_t)(8 + i % 8); /* until the host's */
	kp_rng_portrait_init(&ui->portrait.rng); /* the console portrait's RNG lives as long as the process */
	ui->portrait.mode = KP_UI_PORTRAITS_ANIMATED;
	kp_ui_glue_init(ui);
	ui->briefing.dialog = -1;
	ui->briefing.current = -1;
	ui->credits.dialog = ui->credits.current = -1;
	ui->portrait.control = ui->portrait.dialog = -1;
	/* no console before kp_ui_hud_create (kp_ui_dispatch tells its dialogs apart) */
	ui->minimap.control = ui->minimap.dialog = ui->hud.textbox = ui->status.panel = ui->card.dialog = -1;
	ui->hud.resources = ui->hud.leaderboard = -1;
	ui->input.over_console = -1;
	ui->mouse_scroll_speed = ui->key_scroll_speed = KP_UI_SCROLL_SPEED;
	ui->options = KP_UI_OPTIONS_DEFAULT;
	ui->gamma = KP_UI_GAMMA_DEFAULT;
	{
		KpUiVideoOptions *v = &ui->video_options;
		v->graphics = v->fog = v->water = v->font = v->lighting = KP_VIDEO_AUTO;
	}
	ui->view_w = KP_UI_VIEW_W;
	ui->view_h = KP_UI_VIEW_H;
	if (kp_kpf_parse(&ui->ui_kpf, ui_kpd, ui_size, "ui.kpd", 0, &e) != 0
		|| kp_kpf_parse(&ui->fonts_kpf, fonts_kpd, fonts_size, "fonts.kpd", 0, &e) != 0) {
		fail(err, err_size, e.msg);
		kp_ui_free(ui);
		return NULL;
	}
	ui->ui_data = ui_kpd;
	ui->ui_size = ui_size;
	pool = kp_kpf_find(&ui->ui_kpf, "POOL");
	rows = kp_kpf_find(&ui->fonts_kpf, "TFGM");
	if (pool == NULL || rows == NULL || parse_dialogs(ui, kp_kpf_find(&ui->ui_kpf, "DLGS")) != 0
		|| parse_files(kp_kpf_find(&ui->ui_kpf, "DGRP"), &ui->grps, &ui->grp_count) != 0
		|| parse_files(kp_kpf_find(&ui->ui_kpf, "PCXS"), &ui->pcx, &ui->pcx_count) != 0
		|| parse_files(kp_kpf_find(&ui->ui_kpf, "GFIL"), &ui->glue_files, &ui->glue_file_count) != 0) {
		fail(err, err_size, "ui.kpd or fonts.kpd lacks a section or is malformed");
		kp_ui_free(ui);
		return NULL;
	}
	ui->pool = pool->data;
	ui->pool_size = pool->size;
	{
		/* the vector font (absent: the text stays the original's bitmaps) */
		KpUiFile *files = NULL;
		int count = 0;
		if (kp_kpf_find(&ui->ui_kpf, "VFNT") != NULL && parse_files(kp_kpf_find(&ui->ui_kpf, "VFNT"), &files, &count) == 0) {
			int k;
			for (k = 0; k < count && k < KP_UI_VECTOR_FONTS; k++) {
				ui->vector_fonts[k] = files[k].data;
				ui->vector_font_sizes[k] = files[k].size;
			}
			ui->vector_font_count = k;
		}
		free(files);
	}
	if (kp_kpf_find(&ui->ui_kpf, "GHST") != NULL /* the placement ghosts (absent: none drawn) */
		&& parse_files(kp_kpf_find(&ui->ui_kpf, "GHST"), &ui->ghosts, &ui->ghost_count) != 0) {
		fail(err, err_size, "ui.kpd's GHST is malformed");
		kp_ui_free(ui);
		return NULL;
	}
	{
		const KpKpfSection *stly = kp_kpf_find(&ui->ui_kpf, "STLY");
		if (stly != NULL && stly->size >= 2 && 2 + 8 * (size_t)kp_le_u16(stly->data) <= stly->size) {
			ui->status_layout_count = (int)kp_le_u16(stly->data);
			ui->status_layouts = stly->data + 2;
		}
	}
	{
		const KpKpfSection *tfnt = kp_kpf_find(&ui->ui_kpf, "TFNT");
		if (tfnt == NULL || tfnt->size < 48 * 12) {
			fail(err, err_size, "ui.kpd lacks the themes' colour banks (TFNT)");
			kp_ui_free(ui);
			return NULL;
		}
		ui->theme_fonts = tfnt->data;
	}
	{
		const KpKpfSection *estb = kp_kpf_find(&ui->ui_kpf, "ESTB");
		if (estb != NULL && estb->size >= 1) {
			ui->establishing = estb->data;
			ui->establishing_size = estb->size;
		}
	}
	{
		const KpKpfSection *teff = kp_kpf_find(&ui->ui_kpf, "TEFF");
		if (teff != NULL && teff->size >= 2 && 2 + 12 * (size_t)kp_le_u16(teff->data) <= teff->size) {
			ui->theme_effect_size = kp_le_u16(teff->data);
			ui->theme_effects = teff->data + 2;
		}
	}
	{
		const KpKpfSection *volt = kp_kpf_find(&ui->ui_kpf, "VOLT");
		if (volt != NULL && volt->size >= 1 && 1 + 4 * (size_t)volt->data[0] <= volt->size) {
			ui->attenuation = volt->data + 1;
			ui->attenuation_count = volt->data[0];
		}
	}
	kp_ui_sound_init(ui, 1);
	{
		const KpKpfSection *musi = kp_kpf_find(&ui->ui_kpf, "MUSI");
		if (musi != NULL) kp_ui_music_tables(ui, musi->data, musi->size);
		ui->music.rotation = -1;
		/* InitDefaultVolumes 0x49dfa0's ApplyVolumeOptions (ResumeMusic's track 0 plays nothing) */
		ui->music.resumed = ui->sound.device && ui->sound.music != 0;
	}
	{
		const KpKpfSection *accl = kp_kpf_find(&ui->ui_kpf, "ACCL");
		kp_ui_load_accelerators(ui, accl ? accl->data : NULL, accl ? accl->size : 0);
	}
	{
		const KpKpfSection *btns = kp_kpf_find(&ui->ui_kpf, "BTNS");
		if (btns != NULL && btns->size >= 2 && kp_le_u16(btns->data) <= KP_UI_BUTTON_SETS) {
			size_t at = 2;
			int n = (int)kp_le_u16(btns->data), k;
			for (k = 0; k < n && at + 4 <= btns->size; k++) {
				ui->button_set_at[k] = (uint32_t)at;
				at += 4 + 20 * (size_t)kp_le_u16(btns->data + at + 2);
			}
			if (k == n && at <= btns->size) {
				ui->button_sets = btns->data;
				ui->button_set_count = n;
			}
		}
	}
	ui->game_rows = rows->data;
	ui->game_rows_size = rows->size;
	for (i = 0; i < KP_UI_FNT_FONTS; i++) {
		const KpKpfSection *f = kp_kpf_find(&ui->fonts_kpf, FONT_SECTIONS[i]);
		if (f == NULL) {
			fail(err, err_size, "fonts.kpd lacks a font");
			kp_ui_free(ui);
			return NULL;
		}
		ui->font_files[i] = f->data;
		ui->font_file_sizes[i] = f->size;
	}
	kp_ui_text_init(&ui->text);
	kp_ui_timers_init(&ui->timers);
	reset_pool(ui);
	ui->open = ui->pending = ui->menu_button = -1;
	for (i = 0; i < KP_UI_EVENT_TYPES; i++) ui->capture[i] = -1;
	ui->modeless = -1;
	ui->menus.popup = ui->menus.screen = -1;
	ui->score.dialog = -1;
	for (i = 0; i < 4; i++) ui->press[i].control = -1;
	for (i = 0; i < KP_UI_EDITS; i++) ui->edits[i].control = -1;
	return ui;
}

void kp_ui_free(KpUi *ui)
{
	int i;
	if (ui == NULL) return;
	for (i = 0; i < KP_UI_CONTROLS; i++) {
		if (!ui->controls[i].used) continue;
		kp_ui_list_free(&ui->controls[i]);
		kp_ui_scrollbar_free(&ui->controls[i]);
		if (ui->controls[i].owns_image) free(ui->controls[i].image);
		free(ui->controls[i].bitmap.pixels);
	}
	for (i = 0; i < KP_UI_FLC_CLIPS; i++) {
		if (!ui->flc[i].used) continue;
		kp_ui_video_end(&ui->flc[i].video);
		free(ui->flc[i].pixels);
	}
	kp_ui_ready_room_free(ui);
	kp_ui_score_free(ui);
	kp_ui_menus_free(ui);
	kp_ui_credits_free(ui);
	kp_ui_glue_free(ui);
	free(ui->timers.trace);
	kp_ui_glyph_log_free(&ui->glyph_log);
	kp_ui_image_log_free(&ui->image_log);
	kp_ui_effects_free(ui);
	free(ui->console.pixels);
	free(ui->status.bar_empty.pixels);
	kp_ui_video_end(&ui->portrait.video);
	free(ui->portrait.bitmap.pixels);
	free(ui->portrait.overlay.pixels);
	free(ui->portrait_files);
	free(ui->minimap.terrain);
	free(ui->minimap.image);
	free(ui->lines.layer.pixels);
	free(ui->status.bar_full.pixels);
	free(ui->bins);
	kp_ui_placement_free(ui);
	free(ui->grps);
	free(ui->ghosts);
	free(ui->pcx);
	free(ui->console_owners);
	free(ui->glue_files);
	free(ui->locale_files);
	free(ui->supplement_codes);
	kp_kpf_free(&ui->ui_kpf);
	kp_kpf_free(&ui->fonts_kpf);
	kp_kpf_free(&ui->strings_kpf);
	kp_kpf_free(&ui->portraits_kpf);
	kp_kpf_free(&ui->sounds_kpf);
	free(ui);
}

void kp_ui_font_set(KpUi *ui, int game)
{
	static const int GAME[KP_UI_FONTS] = { 0, 1, 3, 4 }, GLUE[KP_UI_FONTS] = { 1, 2, 3, 4 };
	const int *set = game ? GAME : GLUE;
	int i;
	for (i = 0; i < KP_UI_FONTS; i++) {
		ui->fonts[i] = ui->font_files[set[i]];
		ui->font_sizes[i] = ui->font_file_sizes[set[i]];
	}
	if (game) kp_ui_text_set_rows(&ui->text, ui->game_rows, ui->game_rows_size);
}

/* The width and height of a frame of a dialog GRP (GetGrpFrameInfo 0x4cf570). */
static void frame_size(const uint8_t *grp, size_t size, int frame, uint16_t *w, uint16_t *h)
{
	uint32_t count = kp_le_u16(grp), off;
	const uint8_t *r;
	*w = *h = 0;
	if ((uint32_t)frame >= count) frame = 0;
	if (6 + 8 * (size_t)(frame + 1) > size) return;
	r = grp + 6 + 8 * frame;
	off = kp_le_u32(r + 4);
	*w = (uint16_t)(r[2] + ((off & 0x80000000u) ? 256 : 0));
	*h = r[3];
}

static uint16_t larger(uint16_t a, uint16_t b) { return a > b ? a : b; }

int kp_ui_apply_theme(KpUi *ui, const char *grp_path)
{
	const KpUiFile *f = kp_ui_file(ui->grps, ui->grp_count, grp_path);
	uint16_t w, h;
	if (f == NULL || f->size < 6) return -1;
	ui->theme.grp = f->data;
	ui->theme.grp_size = f->size;
	frame_size(f->data, f->size, 10, &w, &h);
	ui->theme.option_size = larger(w, h);
	frame_size(f->data, f->size, 15, &w, &h);
	ui->theme.check_size = larger(w, h);
	frame_size(f->data, f->size, 16, &w, &h);
	ui->theme.up_w = w;
	ui->theme.up_h = h;
	frame_size(f->data, f->size, 29, &w, &h);
	ui->theme.track_h = h;
	frame_size(f->data, f->size, 53, &w, &h);
	ui->theme.row_w = w;
	ui->theme.row_h = h;
	frame_size(f->data, f->size, 22, &w, &h);
	ui->theme.left_w = w;
	ui->theme.left_h = h;
	frame_size(f->data, f->size, 32, &w, &h);
	ui->theme.piece_w = w;
	frame_size(f->data, f->size, 28, &w, &h);
	ui->theme.thumb_w = w;
	ui->theme.thumb_h = h;
	frame_size(f->data, f->size, 92, &w, &h);
	ui->theme.slider_piece = w;
	frame_size(f->data, f->size, 99, &w, &h);
	ui->theme.slider_thumb_w = w;
	ui->theme.slider_thumb_h = h;
	frame_size(f->data, f->size, 97, &w, &h);
	ui->theme.tick_w = w;
	frame_size(f->data, f->size, 77, &w, &h);
	ui->theme.wide_row_w = w;
	frame_size(f->data, f->size, 103, &w, &h);
	ui->theme.tile_piece = w;
	frame_size(f->data, f->size, 133, &w, &h);
	ui->theme.bar_piece = w;
	return 0;
}

static int32_t alloc_control(KpUi *ui)
{
	int32_t i = ui->free_control;
	if (i < 0) return -1;
	ui->free_control = ui->controls[i].next;
	memset(&ui->controls[i], 0, sizeof ui->controls[i]);
	ui->controls[i].used = 1;
	ui->controls[i].next = ui->controls[i].parent = ui->controls[i].child = -1;
	ui->controls[i].clips = -1;
	ui->controls[i].active = ui->controls[i].hover = -1;
	return i;
}

int32_t kp_ui_create_scroll_bar(KpUi *ui, int32_t list)
{
	/* CreateScrollBar 0x4c9580 (type 7, flags 0x20004: plain, freed with its dialog) as InitListBox 0x4c68c0 links it
	 * after the list; its change callback is the list's (kp_ui_list.c) */
	KpUiControl *l = &ui->controls[list];
	int32_t b = alloc_control(ui), length, track = ui->theme.track_h, up = ui->theme.up_h, height;
	KpUiControl *c;
	if (b < 0) return -1;
	c = &ui->controls[b];
	c->type = 7;
	c->flags = 0x20004u;
	c->parent = l->parent;
	c->interact = kp_ui_scrollbar_proc;
	c->draw = kp_ui_scrollbar_draw;
	c->bin = c->record = -1;
	length = l->bottom - l->top;
	height = track != 0 ? (int16_t)(((track / 2 - (2 * up + 4) + length) / track) * track + 2 * up + 4) : length;
	c->user = (uint32_t)list;
	l->bottom = (int16_t)(l->top + height);
	c->left = (int16_t)(l->right - ui->theme.up_w + 1);
	c->top = l->top;
	c->right = l->right;
	c->bottom = l->bottom;
	c->id = (int16_t)(uint16_t)((uint16_t)l->id - 0x8000u);
	c->next = l->next;
	l->next = b;
	return b;
}

void kp_ui_drop_image(KpUi *ui, KpUiControl *c)
{
	if (!c->owns_image) return;
	kp_ui_surface_free(ui, c->image);
	c->image = NULL;
	c->owns_image = 0;
}

static void free_control(KpUi *ui, int32_t i)
{
	KpUiControl *c = &ui->controls[i];
	kp_ui_list_free(c);
	kp_ui_scrollbar_free(c);
	kp_ui_drop_image(ui, c);
	kp_ui_surface_free(ui, c->bitmap.pixels);
	memset(c, 0, sizeof *c);
	c->next = ui->free_control;
	ui->free_control = i;
}

static int find_bin(const KpUi *ui, const char *name)
{
	int i;
	for (i = 0; i < ui->bin_count; i++) {
		if (path_equal(ui->bins[i].name, ui->bins[i].name_len, name)) return i;
	}
	return -1;
}

int32_t kp_ui_load_dialog(KpUi *ui, const char *name)
{
	int b = find_bin(ui, name), i, n;
	int32_t first = -1, *index, *last_child;
	KpUiBin *bin;
	if (b < 0) {
		snprintf(ui->error, sizeof ui->error, "ui: no dialog %s", name);
		return -1;
	}
	bin = &ui->bins[b];
	n = bin->widget_count;
	index = (int32_t *)malloc(sizeof(int32_t) * 2 * (size_t)(n > 0 ? n : 1));
	if (index == NULL) return -1;
	last_child = index + n;
	for (i = 0; i < n; i++) {
		const uint8_t *w = bin->widgets + (size_t)i * WIDGET_SIZE;
		int32_t c = alloc_control(ui);
		KpUiControl *k;
		uint32_t parent = kp_le_u16(w), text_off = kp_le_u32(w + 22);
		if (c < 0) {
			int j;
			for (j = 0; j < i; j++) free_control(ui, index[j]);
			free(index);
			snprintf(ui->error, sizeof ui->error, "ui: the control pool is full");
			return -1;
		}
		index[i] = c;
		last_child[i] = -1;
		k = &ui->controls[c];
		k->type = (uint16_t)kp_le_u16(w + 2);
		k->id = (int16_t)kp_le_u16(w + 4);
		k->flags = kp_le_u32(w + 6);
		k->left = (int16_t)kp_le_u16(w + 10);
		k->top = (int16_t)kp_le_u16(w + 12);
		k->right = (int16_t)kp_le_u16(w + 14);
		k->bottom = (int16_t)kp_le_u16(w + 16);
		k->image_w = (uint16_t)kp_le_u16(w + 18);
		k->image_h = (uint16_t)kp_le_u16(w + 20);
		k->text = text_off != NONE32 && text_off < ui->pool_size ? ui->pool + text_off : NULL;
		k->words36[0] = (uint16_t)kp_le_u16(w + 32);
		k->words36[1] = (uint16_t)kp_le_u16(w + 34);
		k->words36[2] = (uint16_t)kp_le_u16(w + 36);
		k->words36[3] = (uint16_t)kp_le_u16(w + 38);
		k->extra[0] = (uint16_t)kp_le_u16(w + 40);
		k->extra[1] = (uint16_t)kp_le_u16(w + 42);
		k->extra[2] = (uint16_t)kp_le_u16(w + 44);
		k->extra[3] = (uint16_t)kp_le_u16(w + 46);
		k->word1c = (uint16_t)kp_le_u16(w + 52);
		k->delay = (uint16_t)kp_le_u16(w + 54);
		k->user = kp_le_u32(w + 56);
		k->interact = kp_ui_type_interact(k->type);
		k->draw = kp_ui_type_draw(k->type);
		load_image(ui, k);
		k->bin = b;
		k->record = i;
		k->load = bin->loads;
		if (first < 0) { /* the dialog: the BIN's first record */
			first = c;
			continue;
		}
		/* a record whose parent is not before it hangs under the dialog, which frees it (none in the stock BINs) */
		if (parent == NONE16 || (int)parent >= i) parent = 0;
		k->parent = index[parent];
		if (last_child[parent] < 0) ui->controls[index[parent]].child = c;
		else ui->controls[last_child[parent]].next = c;
		last_child[parent] = c;
	}
	free(index);
	if (first >= 0) {
		ui->controls[first].flags |= KP_UI_OWNS_BIN;
		bin->loads++;
	}
	return first;
}

int kp_ui_send_user(KpUi *ui, int32_t control, uint32_t code, uint32_t param)
{
	KpUiControl *c = &ui->controls[control];
	KpUiEvent e;
	memset(&e, 0, sizeof e);
	e.type = KP_UI_USER;
	e.code = code;
	e.param = param;
	e.x = ui->mouse_x;
	e.y = ui->mouse_y;
	return c->interact ? c->interact(ui, c, &e) : 0;
}

int32_t kp_ui_find_control(KpUi *ui, int32_t dialog, int16_t id)
{
	int32_t c;
	for (c = ui->controls[dialog].child; c >= 0; c = ui->controls[c].next) {
		if ((ui->controls[c].id & CHILD_ID_MASK) == (id & CHILD_ID_MASK)) return c;
	}
	return -1;
}

void kp_ui_resolve_colours(KpUi *ui, const uint8_t *rgb, const uint8_t *cycled)
{
	/* ResolveUiColours 0x4cccb0 with FindNearestStaticColor 0x4666c0 over the table 0x50f0f8 (0x00BBGGRR) */
	static const uint32_t COLOURS[26] = { 0xffffff, 0xe0e0e0, 0xd0d0d0, 0xc0c0c0, 0xb0b0b0, 0xa0a0a0, 0x909090,
		0x808080, 0x707070, 0x606060, 0x505050, 0x404040, 0x303030, 0x202020, 0x101010, 0x0, 0xe34b00, 0x4fffff,
		0x2be300, 0x157100, 0xcf, 0x1892ff, 0xba185a, 0x243c5c, 0xae54dc, 0xffff00 };
	int k;
	for (k = 0; k < 26; k++) {
		ui->ui_colours[k] = kp_ui_nearest_colour(rgb, cycled, (int32_t)(COLOURS[k] & 0xff), (int32_t)(COLOURS[k] >> 8 & 0xff),
			(int32_t)(COLOURS[k] >> 16 & 0xff));
	}
}

uint8_t kp_ui_nearest_colour(const uint8_t *rgb, const uint8_t *cycled, int32_t r, int32_t g, int32_t b)
{
	uint32_t best = 0xffffffffu;
	uint8_t index = 0;
	int i;
	for (i = 1; i < 256; i++) {
		int32_t dr, dg, db;
		uint32_t d;
		if (cycled != NULL && cycled[i]) continue;
		dr = rgb[3 * i] - r;
		dg = rgb[3 * i + 1] - g;
		db = rgb[3 * i + 2] - b;
		d = (uint32_t)(dr * dr + dg * dg + db * db);
		if (d < best) {
			best = d;
			index = (uint8_t)i;
		}
	}
	return index;
}

static const char *const TBL_SECTIONS[KP_UI_TBLS] = { "STAT", "NETW", "GLUA", "GHST", "TIPS", "HELP" };

int kp_ui_set_strings(KpUi *ui, const uint8_t *kpd, size_t size)
{
	KpError e;
	int i;
	kp_kpf_free(&ui->strings_kpf); /* the section table of an earlier call */
	if (kp_kpf_parse(&ui->strings_kpf, kpd, size, "strings.kpd", 0, &e) != 0) {
		snprintf(ui->error, sizeof ui->error, "%s", e.msg);
		return -1;
	}
	for (i = 0; i < KP_UI_TBLS; i++) {
		const KpKpfSection *t = kp_kpf_find(&ui->strings_kpf, TBL_SECTIONS[i]);
		ui->tbl[i] = t ? t->data : NULL;
		ui->tbl_size[i] = t ? t->size : 0;
		ui->tbl_wide[i] = 0;
	}
	return 0;
}

/* --- a language's overlays (ADR 0016) --- */

static const char SPEECH_PACK[] = "speech"; /* the speech's pack: its refusal is the speech's, not the language's */

static void locale_refused(KpUi *ui, const char *name, const char *why)
{
	if (name == SPEECH_PACK) {
		if (ui->speech_problem[0] == 0) snprintf(ui->speech_problem, sizeof ui->speech_problem, "%s", why);
	} else if (ui->locale_problem[0] == 0) {
		snprintf(ui->locale_problem, sizeof ui->locale_problem, "%s: %s", name, why);
	}
}

/* An overlay pack's sections (their checksums checked, as nothing else reads them), or -1 with the problem. */
static int locale_pack(KpUi *ui, KpKpf *c, const uint8_t *data, size_t size, const char *name, const KpKpf *base)
{
	KpError e;
	if (kp_kpf_parse(c, data, size, name, 1, &e) != 0) {
		locale_refused(ui, name, e.msg);
		return -1;
	}
	if (memcmp(c->kind, base->kind, 4) != 0 || c->schema_version != base->schema_version) {
		locale_refused(ui, name, "another kind or schema than the base's");
		kp_kpf_free(c);
		return -1;
	}
	return 0;
}

static void locale_strings(KpUi *ui, const uint8_t *data, size_t size)
{
	KpKpf c;
	int i;
	if (locale_pack(ui, &c, data, size, "strings", &ui->strings_kpf) != 0) return;
	for (i = 0; i < KP_UI_TBLS; i++) {
		const KpKpfSection *t = kp_kpf_find(&c, TBL_SECTIONS[i]);
		uint32_t count;
		if (t == NULL) continue; /* the base's */
		count = t->size >= 4 ? kp_le_u32(t->data) : 0;
		if (t->schema_version != KP_UI_TBL_WIDE || ui->tbl[i] == NULL || ui->tbl_size[i] < 2 || t->size < 4
			|| count != kp_le_u16(ui->tbl[i]) || (t->size - 4) / 4 < count) {
			locale_refused(ui, "strings", "a table's schema or row count differs from the base's");
			continue;
		}
		ui->tbl[i] = t->data;
		ui->tbl_size[i] = t->size;
		ui->tbl_wide[i] = 1;
	}
	kp_kpf_free(&c);
}

/* The same dialogs in the same order, by their names and counts: the converter compares their records field by field
 * (kp_ui_set_locale takes a pack it wrote, under its CRCs). */
static int same_dialogs(const KpUi *ui, const KpUiBin *bins, int count)
{
	int i;
	if (count != ui->bin_count) return 0;
	for (i = 0; i < count; i++) {
		const KpUiBin *a = &ui->bins[i], *b = &bins[i];
		if (a->name_len != b->name_len || memcmp(a->name, b->name, a->name_len) != 0
			|| a->widget_count != b->widget_count || a->smk_count != b->smk_count) {
			return 0;
		}
	}
	return 1;
}

static void locale_ui(KpUi *ui, const uint8_t *data, size_t size)
{
	KpKpf c;
	const KpKpfSection *pool, *gfil;
	if (locale_pack(ui, &c, data, size, "ui", &ui->ui_kpf) != 0) return;
	pool = kp_kpf_find(&c, "POOL");
	if (pool != NULL || kp_kpf_find(&c, "DLGS") != NULL) {
		KpUiBin *base = ui->bins;
		int base_count = ui->bin_count;
		ui->bins = NULL;
		/* a control's text is read to its NUL: the last one must have it */
		if (pool == NULL || pool->size == 0 || pool->data[pool->size - 1] != 0 || parse_dialogs(ui, kp_kpf_find(&c, "DLGS")) != 0) {
			free(ui->bins);
			ui->bins = base;
			ui->bin_count = base_count;
			locale_refused(ui, "ui", "its dialogs are malformed");
		} else {
			KpUiBin *ours = ui->bins;
			int our_count = ui->bin_count;
			ui->bins = base;
			ui->bin_count = base_count;
			if (same_dialogs(ui, ours, our_count)) {
				free(base);
				ui->bins = ours;
				ui->pool = pool->data;
				ui->pool_size = pool->size;
			} else {
				free(ours);
				locale_refused(ui, "ui", "other dialogs than the base's");
			}
		}
	}
	gfil = kp_kpf_find(&c, "GFIL");
	if (gfil != NULL && parse_files(gfil, &ui->locale_files, &ui->locale_file_count) != 0) {
		free(ui->locale_files);
		ui->locale_files = NULL;
		ui->locale_file_count = 0;
		locale_refused(ui, "ui", "its files are malformed");
	}
	kp_kpf_free(&c);
}

static void locale_glyphs(KpUi *ui, const uint8_t *data, size_t size)
{
	static const char *const SECTIONS[KP_UI_FNT_FONTS] = { "U8  ", "U10 ", "U14 ", "U16 ", "U16X" };
	KpKpf c;
	KpUiFile *files = NULL;
	const KpKpfSection *vfnt;
	int i, count = 0;
	if (locale_pack(ui, &c, data, size, "glyphs", &ui->fonts_kpf) != 0) return;
	for (i = 0; i < KP_UI_FNT_FONTS; i++) {
		const KpKpfSection *s = kp_kpf_find(&c, SECTIONS[i]);
		int32_t n, k;
		if (s == NULL) continue;
		if (kp_ui_text_add_supplement(&ui->text, ui->font_files[i], s->data, s->size) != 0) {
			locale_refused(ui, "glyphs", "a font's supplement is malformed");
			continue;
		}
		if (ui->supplement_codes != NULL) continue; /* the converter gives every font the same code points */
		n = kp_ui_supplement_count(s->data, s->size);
		if (n > 0 && (ui->supplement_codes = (int32_t *)malloc((size_t)n * sizeof(int32_t))) != NULL) {
			for (k = 0; k < n; k++) ui->supplement_codes[k] = kp_ui_supplement_code(s->data, k);
			ui->supplement_code_count = n;
		}
	}
	vfnt = kp_kpf_find(&c, "VFNT");
	if (vfnt != NULL) {
		if (parse_files(vfnt, &files, &count) == 0 && count > 0) {
			for (i = 0; i < KP_UI_VECTOR_FONTS; i++) {
				ui->vector_fonts[i] = i < count ? files[i].data : NULL;
				ui->vector_font_sizes[i] = i < count ? files[i].size : 0;
			}
			ui->vector_font_count = count < KP_UI_VECTOR_FONTS ? count : KP_UI_VECTOR_FONTS;
		} else {
			locale_refused(ui, "glyphs", "its vector fonts are malformed");
		}
		free(files);
	}
	kp_kpf_free(&c);
}

/* The speech's sound records (stage L5): the durations of its voices, the base's sound count; a refusal goes to
 * ui->speech_problem. */
static void locale_speech(KpUi *ui, const uint8_t *data, size_t size)
{
	KpKpf c;
	const KpKpfSection *sfx;
	if (locale_pack(ui, &c, data, size, SPEECH_PACK, &ui->sounds_kpf) != 0) return;
	sfx = kp_kpf_find(&c, "SFX ");
	if (sfx == NULL || sfx->size < 2 || kp_le_u16(sfx->data) != (uint32_t)ui->sound_count
		|| 2 + KP_UI_SOUND_RECORD * (size_t)ui->sound_count > sfx->size) {
		locale_refused(ui, SPEECH_PACK, "no SFX of the base's sound count");
	} else {
		ui->sounds = sfx->data + 2;
	}
	kp_kpf_free(&c);
}

void kp_ui_set_locale(KpUi *ui, const KpUiLocale *locale)
{
	if (locale->speech != NULL) locale_speech(ui, locale->speech, locale->speech_size);
	if (locale->strings != NULL) locale_strings(ui, locale->strings, locale->strings_size);
	if (locale->ui != NULL) locale_ui(ui, locale->ui, locale->ui_size);
	if (locale->glyphs != NULL) locale_glyphs(ui, locale->glyphs, locale->glyphs_size);
}

const uint8_t *kp_ui_tbl(const KpUi *ui, int table, uint32_t id)
{
	static const uint8_t EMPTY[1] = { 0 };
	const uint8_t *t;
	uint32_t off;
	id &= 0xffff;
	if (id == 0) return NULL;
	if (table < 0 || table >= KP_UI_TBLS || (t = ui->tbl[table]) == NULL || ui->tbl_size[table] < 2) return EMPTY;
	if (ui->tbl_wide[table]) {
		if (id - 1 >= kp_le_u32(t) || 4 * (size_t)id + 4 > ui->tbl_size[table]) return EMPTY;
		off = kp_le_u32(t + 4 * id);
	} else {
		if (id - 1 >= kp_le_u16(t) || 2 * (size_t)id + 2 > ui->tbl_size[table]) return EMPTY;
		off = kp_le_u16(t + 2 * id);
	}
	if (off >= ui->tbl_size[table] || memchr(t + off, 0, ui->tbl_size[table] - off) == NULL) return EMPTY;
	return t + off;
}

/* AddDirtyRect 0x4c5490: outward to multiples of 16, clamped to the screen, ORed into the region. */
void kp_ui_add_dirty_rect(KpUi *ui, int32_t l, int32_t t, int32_t r, int32_t b)
{
	int16_t x0 = (int16_t)(l & ~15), y0 = (int16_t)(t & ~15), x1 = (int16_t)((r + 16) & ~15), y1 = (int16_t)((b + 16) & ~15);
	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 > KP_UI_SCREEN_W) x1 = KP_UI_SCREEN_W;
	if (y1 > KP_UI_SCREEN_H) y1 = KP_UI_SCREEN_H;
	if (x0 >= x1 || y0 >= y1) return;
	if (ui->region_count == KP_UI_REGION) { /* a full list: the whole screen */
		ui->region_count = 1;
		ui->region[0][0] = 0;
		ui->region[0][1] = 0;
		ui->region[0][2] = KP_UI_SCREEN_W;
		ui->region[0][3] = KP_UI_SCREEN_H;
		return;
	}
	ui->region[ui->region_count][0] = x0;
	ui->region[ui->region_count][1] = y0;
	ui->region[ui->region_count][2] = x1;
	ui->region[ui->region_count][3] = y1;
	ui->region_count++;
}

/* InvalidateControl 0x4c4ae0: the control's rectangle on the screen, within its dialog's. */
static void invalidate(KpUi *ui, int32_t k)
{
	const KpUiControl *c = &ui->controls[k], *d = c->type == 0 || c->parent < 0 ? c : &ui->controls[c->parent];
	int32_t l = c->left, t = c->top, r = c->right, b = c->bottom;
	if (c != d) {
		l += d->left;
		t += d->top;
		r += d->left;
		b += d->top;
	}
	if (l < d->left) l = d->left;
	if (t < d->top) t = d->top;
	if (r > d->right) r = d->right;
	if (b > d->bottom) b = d->bottom;
	if (l > r || t > b) return;
	/* The original's 16x16 dirty grid of the game view (0x6d06d0; 0x4cac50, 0x4c5530) only limits its partial redraw of
	 * the world and of the backdrop dialogs over it: Koprulu composes every frame whole, so it keeps none */
	kp_ui_add_dirty_rect(ui, l, t, r, b);
}

void kp_ui_mark_dirty(KpUi *ui, int32_t control)
{
	/* MarkControlDirty 0x4c2840 */
	if (ui->controls[control].flags & KP_UI_DIRTY) return;
	ui->controls[control].flags |= KP_UI_DIRTY;
	invalidate(ui, control);
}

void kp_ui_show_control(KpUi *ui, int32_t control)
{
	if (ui->controls[control].flags & KP_UI_VISIBLE) return;
	if (kp_ui_send_user(ui, control, KP_UI_SHOW, 0)) kp_ui_mark_dirty(ui, control);
}

void kp_ui_hide_control(KpUi *ui, int32_t control)
{
	if (!(ui->controls[control].flags & KP_UI_VISIBLE)) return;
	if (kp_ui_send_user(ui, control, KP_UI_HIDE, 0)) {
		(void)kp_ui_send_user(ui, control, KP_UI_FOCUS_LOST, 0);
		kp_ui_mark_dirty(ui, control);
	}
}

void kp_ui_set_visible(KpUi *ui, int32_t control, int show)
{
	if (show) kp_ui_show_control(ui, control);
	else kp_ui_hide_control(ui, control);
}

void kp_ui_bind_interacts(KpUi *ui, int32_t dialog, const KpUiInteract *table, int count)
{
	int32_t c;
	for (c = ui->controls[dialog].child; c >= 0; c = ui->controls[c].next) {
		int16_t id = ui->controls[c].id;
		if (id > 0 && id <= count && table[id - 1] != NULL) ui->controls[c].interact = table[id - 1];
	}
}

int kp_ui_callback(KpUi *ui, KpUiCallback fn)
{
	int k;
	for (k = 0; k < ui->callback_count; k++) {
		if (ui->callbacks[k] == fn) return k + 1;
	}
	if (ui->callback_count == KP_UI_CALLBACKS) return 0;
	ui->callbacks[ui->callback_count++] = fn;
	return ui->callback_count;
}

static void fire_timer(void *ctx, int64_t owner, uint16_t id, int callback, uint32_t period)
{
	KpUi *ui = (KpUi *)ctx;
	(void)period;
	if (owner < 0 || owner >= KP_UI_CONTROLS || !ui->controls[owner].used) return;
	if (callback > 0 && callback <= ui->callback_count) ui->callbacks[callback - 1](ui, (int32_t)owner);
	else (void)kp_ui_send_user(ui, (int32_t)owner, KP_UI_TIMER, id);
}

void kp_ui_resume_pass(KpUi *ui) { (void)kp_ui_resume_timers(&ui->timers, fire_timer, ui); }

int kp_ui_tick(KpUi *ui, uint32_t now)
{
	int fired;
	ui->now = now;
	fired = kp_ui_run_timers(&ui->timers, now, fire_timer, ui);
	kp_ui_music_tick(ui); /* the pump's Win32 timer and idle work of the music */
	return fired;
}

void kp_ui_init_dialog(KpUi *ui, int32_t dialog, KpUiInteract interact)
{
	KpUiControl *d = &ui->controls[dialog];
	size_t size;
	d->flags |= DIALOG_FLAGS;
	if (interact != NULL) d->interact = interact;
	d->bitmap.w = d->words36[0];
	d->bitmap.h = d->words36[1];
	size = (size_t)d->bitmap.w * (size_t)d->bitmap.h;
	kp_ui_surface_free(ui, d->bitmap.pixels);
	d->bitmap.pixels = size ? (uint8_t *)calloc(size, 1) : NULL;
	d->id = 0;
	kp_ui_send_user(ui, dialog, KP_UI_RECENTRE, 0);
	d->right = (int16_t)(d->right + d->left);
	d->bottom = (int16_t)(d->bottom + d->top);
	kp_ui_send_user(ui, dialog, KP_UI_POST_LOAD, 0);
	kp_ui_send_user(ui, dialog, KP_UI_CREATE, 0);
	(void)kp_ui_set_timer(&ui->timers, dialog, SHOW_TIMER, SHOW_PERIOD, 0, ui->now);
	d->flags &= ~KP_UI_INITIALIZING;
}

static void unlink_open(KpUi *ui, int32_t dialog)
{
	int32_t *link = &ui->open;
	while (*link >= 0 && *link != dialog) link = &ui->controls[*link].next;
	if (*link == dialog) *link = ui->controls[dialog].next;
	ui->controls[dialog].next = -1;
}

void kp_ui_close_dialog(KpUi *ui, int32_t dialog)
{
	/* EndDialog 0x4c2b50: the dialog leaves the open list; its result is ui->result. A dialog past its initialization
	 * is marked dirty (MarkControlDirty 0x4c2840): its rectangle is repainted from what lay under it */
	if (!(ui->controls[dialog].flags & KP_UI_INITIALIZING)) invalidate(ui, dialog);
	ui->controls[dialog].flags |= KP_UI_DIRTY;
	unlink_open(ui, dialog);
	ui->abort = 1;
}

void kp_ui_destroy_dialog(KpUi *ui, int32_t dialog)
{
	int32_t c, next;
	unlink_open(ui, dialog);
	kp_ui_send_user(ui, dialog, KP_UI_DESTROY, 0);
	for (c = ui->controls[dialog].child; c >= 0; c = next) {
		next = ui->controls[c].next;
		free_control(ui, c);
	}
	free_control(ui, dialog);
	kp_ui_modal_destroyed(ui, dialog); /* a modal run ends: its continuation */
}

void kp_ui_drop_control(KpUi *ui, int32_t dialog, int32_t control)
{
	int32_t *link = &ui->controls[dialog].child;
	while (*link >= 0 && *link != control) link = &ui->controls[*link].next;
	if (*link < 0) return;
	*link = ui->controls[control].next;
	free_control(ui, control);
}

/* The children of a dialog get the event (ForEachControl 0x4c18a0 from the first child with ForwardEvent). */
static void send_children(KpUi *ui, KpUiControl *d, KpUiEvent *e)
{
	int32_t c, next;
	for (c = d->child; c >= 0; c = next) {
		KpUiControl *k = &ui->controls[c];
		next = k->next;
		if (k->interact) (void)k->interact(ui, k, e);
	}
}

static int eligible(const KpUiControl *c)
{
	return (c->flags & KP_UI_VISIBLE) && !(c->flags & KP_UI_DISABLED) && (uint16_t)c->id != 0xffff
		&& ((c->id & 0x8000) || (c->flags & KP_UI_EVENTS));
}

/* --- the dialogs' input (dlg.cpp, ctrl.cpp: atlas P42 0x4c1470..0x4c3770, P43 0x4c7440..0x4c7660) --- */

#define PRESS_MS 30

static int control_default(KpUi *ui, KpUiControl *c, KpUiEvent *e);
#define ID_OK 0xfffe
#define ID_CANCEL 0xfffd
#define FLAG_DEFAULT 0x80000u   /* the control Enter activates */
#define FLAG_ESCAPE 0x40u       /* the control Escape activates */
#define FLAG_VK_HOTKEY 0x100u
#define FLAG_CHAR_HOTKEY 0x200u
#define FLAG_NO_FOCUS 0x20000000u
#define DIALOG_NESTED 0x8000000u    /* a dialog composited into another dialog's bitmap */
#define CHECK_BOX_FRAME 11          /* CheckBoxProc 0x4c74a0's wUser: the box's first theme frame */
#define KEY_SHIFT 0x03           /* the key event's flags +0x0a: either Shift (GetModifierKeyState 0x440060) */
#define KEY_UPPER_FLAG 0x08      /* the key event's flags +0x0a bit 3: the character hotkeys too */
#define VK_LEFT 0x25
#define VK_UP 0x26
#define VK_RIGHT 0x27
#define VK_DOWN 0x28
#define CHAR_NO_HOTKEY 0x100
#define NOT_ASCII (-1)          /* a character no hotkey or control key can be (ADR 0016) */

static int send_code(KpUi *ui, int32_t c, uint32_t code, uint32_t param, const KpUiEvent *at)
{
	KpUiControl *k = &ui->controls[c];
	KpUiEvent e;
	memset(&e, 0, sizeof e);
	e.type = KP_UI_USER;
	e.code = code;
	e.param = param;
	e.x = at ? at->x : ui->mouse_x;
	e.y = at ? at->y : ui->mouse_y;
	return k->interact ? k->interact(ui, k, &e) : 0;
}

static int32_t dialog_of(const KpUi *ui, int32_t c)
{
	return ui->controls[c].type == 0 ? c : ui->controls[c].parent;
}

/* IsCursorInControl 0x4c29a0 against a rectangle (s16, inclusive). */
static int inside(const KpUiControl *c, int32_t x, int32_t y)
{
	return x >= c->left && x <= c->right && y >= c->top && y <= c->bottom;
}

/* HitTestDialog 0x4c2090: the hit control, the dialog, or -1. */
int32_t kp_ui_hit_test(KpUi *ui, int32_t dialog, const KpUiEvent *e)
{
	KpUiControl *d = &ui->controls[dialog];
	KpUiEvent q;
	int32_t c, x, y;
	if (!inside(d, e->x, e->y)) return -1;
	x = (int16_t)(e->x - d->left);
	y = (int16_t)(e->y - d->top);
	q = *e;
	q.type = KP_UI_USER;
	q.code = KP_UI_HIT_TEST;
	for (c = d->child; c >= 0; c = ui->controls[c].next) {
		KpUiControl *k = &ui->controls[c];
		if (inside(k, x, y) && k->interact && k->interact(ui, k, &q)) return c;
	}
	return d->interact && d->interact(ui, d, &q) ? dialog : -1;
}

/* SetEventCapture 0x4c3370. */
void kp_ui_set_capture(KpUi *ui, int32_t c, int type)
{
	if (ui->capture[type] >= 0) (void)send_code(ui, ui->capture[type], KP_UI_FOCUS_LOST, 0, NULL);
	ui->capture[type] = c;
}

/* ReleaseControlReferences 0x4c3770. */
void kp_ui_release_refs(KpUi *ui, int32_t c)
{
	int k;
	for (k = 0; k < KP_UI_EVENT_TYPES; k++) {
		if (ui->capture[k] == c) ui->capture[k] = -1;
	}
	for (k = 0; k < 4; k++) {
		if (ui->press[k].control == c) ui->press[k].control = -1;
	}
}

static void dirty_text(KpUi *ui, int32_t c)
{
	KpUiControl *k = &ui->controls[c];
	if (k->text != NULL && !(k->flags & KP_UI_DIRTY)) kp_ui_mark_dirty(ui, c);
}

/* ClearHover 0x4c1c70. */
static void clear_hover(KpUi *ui, int32_t dialog)
{
	KpUiControl *d = &ui->controls[dialog];
	if (d->hover < 0) return;
	ui->controls[d->hover].flags &= ~KP_UI_HOVER;
	dirty_text(ui, d->hover);
	d->hover = -1;
}

/* SetHoverControl 0x4c2690. */
static void set_hover(KpUi *ui, int32_t dialog, int32_t c)
{
	clear_hover(ui, dialog);
	if (c == dialog) return;
	ui->controls[c].flags |= KP_UI_HOVER;
	(void)send_code(ui, c, KP_UI_MOUSE_ENTER, 0, NULL);
	ui->controls[dialog].hover = c;
	dirty_text(ui, c);
}

/* UpdateHover 0x4c1c20. */
static void update_hover(KpUi *ui, int32_t dialog, const KpUiEvent *e)
{
	int32_t hit = kp_ui_hit_test(ui, dialog, e);
	if (hit < 0) clear_hover(ui, dialog);
	else if (!(ui->controls[hit].flags & KP_UI_HOVER)) set_hover(ui, dialog, hit);
}

void kp_ui_update_hover(KpUi *ui, int32_t dialog, const KpUiEvent *e) { update_hover(ui, dialog, e); }

void kp_ui_ui_sound(KpUi *ui, int event)
{
	/* the controls' sound hook 0x6ce664 (SetControlSoundHook 0x4c3750): the glue's GlueUiSoundProc 0x458490, the
	 * game's GameDialogCallback 0x443f20 */
	if (ui->ui_sound != NULL) ui->ui_sound(ui, event);
}

/* RefreshDefaultButton 0x4c2940: unless the focus is a button, the first visible, enabled button (a group's own
 * children walked in place of its siblings) is drawn again. */
static void refresh_default_button(KpUi *ui, int32_t dialog)
{
	const KpUiControl *d = &ui->controls[dialog];
	int32_t c;
	if (d->active >= 0 && ui->controls[d->active].type == 1) return;
	for (c = d->child; c >= 0;) {
		const KpUiControl *k = &ui->controls[c];
		if (k->type == 1 && (k->flags & (KP_UI_VISIBLE | KP_UI_DISABLED)) == KP_UI_VISIBLE) {
			if (!(k->flags & KP_UI_DIRTY)) kp_ui_mark_dirty(ui, c);
			return;
		}
		c = k->type == 0 ? k->child : k->next;
	}
}

/* FocusControl 0x4c2730: the focus of the parent dialog (the grandparent when the parent is a group). */
void kp_ui_focus_control(KpUi *ui, int32_t c)
{
	int32_t parent = ui->controls[c].parent, owner = parent, old;
	if (parent < 0 || ui->controls[parent].active == c) return;
	if (!send_code(ui, c, KP_UI_FOCUS, 0, NULL)) return;
	if (ui->controls[owner].type != 0) owner = ui->controls[owner].parent;
	old = ui->controls[parent].active;
	ui->controls[owner].active = (ui->controls[owner].flags & FLAG_NO_FOCUS) ? -1 : c;
	if (old >= 0) {
		(void)send_code(ui, old, KP_UI_FOCUS_LOST, 0, NULL);
		kp_ui_mark_dirty(ui, old);
	}
	kp_ui_mark_dirty(ui, c);
	refresh_default_button(ui, parent);
}

/* SetDialogFocusPointer 0x4c1e70: the focus of a control's dialog (its parent; a dialog's own), none under
 * FLAG_NO_FOCUS. */
static void set_focus_pointer(KpUi *ui, int32_t c, int32_t focus)
{
	KpUiControl *d = &ui->controls[ui->controls[c].type != 0 ? ui->controls[c].parent : c];
	d->active = (d->flags & FLAG_NO_FOCUS) ? -1 : focus;
}

/* The focus walk's eligibility: not disabled, visible, taking events. */
static int focusable(const KpUiControl *k)
{
	return (k->flags & (KP_UI_DISABLED | KP_UI_VISIBLE | KP_UI_EVENTS)) == (KP_UI_VISIBLE | KP_UI_EVENTS);
}

/* 0x4c23d0: the next control in its parent's ring of children. */
static int32_t ring_next(KpUi *ui, int32_t c)
{
	int32_t n = ui->controls[c].next;
	return n >= 0 ? n : ui->controls[ui->controls[c].parent].child;
}

/* 0x4c23e0: the previous one. */
static int32_t ring_previous(KpUi *ui, int32_t c)
{
	int32_t first = ui->controls[ui->controls[c].parent].child, target = c == first ? -1 : c, k = first;
	while (k >= 0 && ui->controls[k].next != target) k = ui->controls[k].next;
	return k >= 0 ? k : c;
}

/* TabFocus 0x4c2220 (and the arrow keys' walk of DialogDefaultInteract 0x4c1470): from the step after the focus, or
 * the first child, the first focusable control in the ring takes the focus; 0 after a whole lap. */
static int walk_focus(KpUi *ui, int32_t dialog, int32_t (*step)(KpUi *, int32_t))
{
	const KpUiControl *d = &ui->controls[dialog];
	int32_t start = d->active >= 0 ? step(ui, d->active) : d->child, c = start;
	if (start < 0) return 0;
	do {
		if (focusable(&ui->controls[c])) {
			kp_ui_focus_control(ui, c);
			return 1;
		}
		c = step(ui, c);
	} while (c != start);
	return 0;
}

/* SchedulePressRelease 0x4c3570. */
static void schedule_press(KpUi *ui, int32_t c)
{
	kp_ui_schedule_press(ui, c);
}

void kp_ui_schedule_press(KpUi *ui, int32_t c)
{
	int k;
	for (k = 0; k < 4; k++) {
		if (ui->press[k].control >= 0 && ui->controls[ui->press[k].control].parent == ui->controls[c].parent) return;
	}
	for (k = 0; k < 4; k++) {
		if (ui->press[k].control < 0) {
			ui->press[k].control = c;
			ui->press[k].deadline = ui->now + PRESS_MS;
			return;
		}
	}
}

/* TrackPushButtonPress 0x4c7440. */
static void track_press(KpUi *ui, int32_t c, const KpUiEvent *e)
{
	KpUiControl *k = &ui->controls[c];
	int32_t d = dialog_of(ui, c);
	int in = inside(k, (int16_t)(e->x - ui->controls[d].left), (int16_t)(e->y - ui->controls[d].top));
	if (in && !(k->flags & KP_UI_PRESSED)) {
		kp_ui_ui_sound(ui, 0); /* PlayControlSound 0x4c3740: pressed under the cursor */
		k->flags |= KP_UI_PRESSED;
		kp_ui_mark_dirty(ui, c);
	} else if (!in && (k->flags & KP_UI_PRESSED)) {
		k->flags &= ~KP_UI_PRESSED;
		kp_ui_mark_dirty(ui, c);
	}
}

/* ReleasePushButton 0x4c7660. */
static void release_push(KpUi *ui, int32_t c)
{
	KpUiControl *k = &ui->controls[c];
	kp_ui_release_refs(ui, c);
	if (!(k->flags & KP_UI_PRESSED)) return;
	k->flags &= ~KP_UI_PRESSED;
	kp_ui_mark_dirty(ui, c);
	(void)send_code(ui, c, KP_UI_CLOSE, 0, NULL); /* user code 2: activate */
	kp_ui_ui_sound(ui, 2); /* PlayControlSound 0x4c3740: fired */
}

int kp_ui_press_slots(KpUi *ui)
{
	int k, n = 0;
	for (k = 0; k < 4; k++) {
		int32_t c = ui->press[k].control;
		if (c >= 0 && (int32_t)(ui->press[k].deadline - ui->now) < 0) {
			ui->press[k].control = -1;
			(void)send_code(ui, c, KP_UI_ACTIVATE, 0, NULL); /* user code 3 */
			n++;
		}
	}
	return n;
}

/* The hotkey matchers 0x4c1920 (character, upper case), 0x4c19c0 (character), 0x4c1a50 (virtual key). */
static int32_t match_hotkey(KpUi *ui, int32_t dialog, int key, uint32_t flag, int upper)
{
	int32_t c;
	for (c = ui->controls[dialog].child; c >= 0; c = ui->controls[c].next) {
		KpUiControl *k = &ui->controls[c];
		int first;
		if (!(k->flags & flag) || k->text == NULL || !eligible(k)) continue;
		first = upper ? toupper(k->text[0]) : k->text[0];
		if ((int8_t)first == (int8_t)key && send_code(ui, c, KP_UI_ACTIVATE, 0, NULL)) return c;
	}
	return -1;
}

/* ActivateControlWithFlag 0x4c2430 and the activation by ID 0x4c24c0. */
static int activate_where(KpUi *ui, int32_t dialog, uint32_t flag, uint16_t id)
{
	int32_t c;
	for (c = ui->controls[dialog].child; c >= 0; c = ui->controls[c].next) {
		KpUiControl *k = &ui->controls[c];
		if (flag ? (k->flags & flag) != 0 : (uint16_t)k->id == id) {
			if (eligible(k) && send_code(ui, c, KP_UI_ACTIVATE, 0, NULL)) return 1;
			if (flag) return 0;
		}
	}
	return 0;
}

static int keyboard_type(int type) { return type == 6 || type == 7 || type == 8 || type == 12 || type == 13; } /* 0x50ef28 */

static int input_ready(const KpUiControl *k)
{
	return keyboard_type(k->type) && (k->flags & (KP_UI_DISABLED | KP_UI_VISIBLE | KP_UI_EVENTS)) == (KP_UI_VISIBLE | KP_UI_EVENTS);
}

/* OfferKeyToInputControls 0x4c21b0. */
static int offer_to_inputs(KpUi *ui, int32_t dialog, KpUiEvent *e)
{
	int32_t c;
	for (c = ui->controls[dialog].child; c >= 0; c = ui->controls[c].next) {
		KpUiControl *k = &ui->controls[c];
		if (input_ready(k) && k->interact && k->interact(ui, k, e)) {
			kp_ui_focus_control(ui, c);
			return 1;
		}
	}
	return 0;
}

/* FindActiveEditControl 0x4c2160. */
static int has_edit(KpUi *ui, int32_t dialog)
{
	int32_t c;
	for (c = ui->controls[dialog].child; c >= 0; c = ui->controls[c].next) {
		const KpUiControl *k = &ui->controls[c];
		if (k->type == 8 && (k->flags & (KP_UI_DISABLED | KP_UI_VISIBLE | KP_UI_EVENTS)) == (KP_UI_VISIBLE | KP_UI_EVENTS)) return 1;
	}
	return 0;
}

/* DialogCharInteract 0x4c1cb0. */
static int dialog_char(KpUi *ui, int32_t dialog, KpUiEvent *e)
{
	KpUiControl *d = &ui->controls[dialog];
	int ch = e->key < KP_TEXT_ASCII_END ? e->key : NOT_ASCII; /* a hotkey is ASCII (ADR 0016) */
	if (ch == 9) {
		(void)walk_focus(ui, dialog, (e->mods & KEY_SHIFT) ? ring_previous : ring_next);
		return 1;
	}
	if (ch == 13) {
		if (d->active >= 0 && (ui->controls[d->active].type == 1 || ui->controls[d->active].type == 2)
			&& activate_where(ui, dialog, 0, (uint16_t)ui->controls[d->active].id)) {
			return 1;
		}
		if (activate_where(ui, dialog, 0, ID_OK) || activate_where(ui, dialog, FLAG_DEFAULT, 0)) return 1;
	} else if (ch == 27) {
		if (activate_where(ui, dialog, 0, ID_CANCEL) || activate_where(ui, dialog, FLAG_ESCAPE, 0)) return 1;
	} else if (ch == ' ') {
		if (d->active >= 0 && eligible(&ui->controls[d->active]) && send_code(ui, d->active, KP_UI_ACTIVATE, 0, NULL)) {
			return 1;
		}
	}
	if (ch != NOT_ASCII && !(e->mods & CHAR_NO_HOTKEY) && !has_edit(ui, dialog)
		&& match_hotkey(ui, dialog, tolower(ch), FLAG_CHAR_HOTKEY, 0) >= 0) {
		return 1;
	}
	if (d->active >= 0 && ui->controls[d->active].interact && ui->controls[d->active].interact(ui, &ui->controls[d->active], e)) {
		return 1;
	}
	if (offer_to_inputs(ui, dialog, e)) return 1;
	return (d->flags & KP_UI_CONSUMED) != 0;
}

/* DialogDefaultInteract 0x4c1470's input events. */
static int dialog_input(KpUi *ui, KpUiControl *d, KpUiEvent *e)
{
	int32_t self = (int32_t)(d - ui->controls), hit;
	int consumed = (d->flags & KP_UI_CONSUMED) != 0;
	switch (e->type) {
	case KP_UI_KEY_DOWN:
		if (match_hotkey(ui, self, e->key & 0xff, FLAG_VK_HOTKEY, 0) >= 0) return 1;
		if ((e->mods & KEY_UPPER_FLAG) && match_hotkey(ui, self, e->key & 0xff, FLAG_CHAR_HOTKEY, 1) >= 0) return 1;
		/* fall through */
	case KP_UI_KEY_REPEAT: case KP_UI_KEY_UP:
		if (d->active >= 0) {
			KpUiControl *f = &ui->controls[d->active];
			if (f->interact && f->interact(ui, f, e)) return 1;
		}
		/* the keys down and repeated: OfferKeyToInputControls 0x4c21b0, then the arrows walk the focus */
		if (e->type == KP_UI_KEY_UP) return consumed;
		if (offer_to_inputs(ui, self, e)) return 1;
		if (d->flags & FLAG_NO_FOCUS) return consumed;
		switch (e->key & 0xff) {
		case VK_LEFT: case VK_UP:
			return walk_focus(ui, self, ring_previous) || consumed;
		case VK_RIGHT: case VK_DOWN:
			return walk_focus(ui, self, ring_next) || consumed;
		default:
			return consumed;
		}
	case KP_UI_MOUSE_MOVE:
		if (inside(d, e->x, e->y)) update_hover(ui, self, e);
		else clear_hover(ui, self);
		return consumed;
	case KP_UI_LEFT_DOWN: case KP_UI_LEFT_DOUBLE: case KP_UI_RIGHT_DOWN: case KP_UI_RIGHT_DOUBLE:
		hit = kp_ui_hit_test(ui, self, e);
		if (hit < 0) return consumed;
		if (hit == self) return 1;
		kp_ui_focus_control(ui, hit);
		return ui->controls[hit].interact ? ui->controls[hit].interact(ui, &ui->controls[hit], e) : 0;
	case KP_UI_CHAR:
		return dialog_char(ui, self, e);
	default:
		return 0;
	}
}

/* ButtonBaseProc 0x4c7590. */
static int button_base(KpUi *ui, KpUiControl *c, KpUiEvent *e)
{
	int32_t self = (int32_t)(c - ui->controls);
	switch (e->type) {
	case KP_UI_MOUSE_MOVE:
		track_press(ui, self, e);
		return 1;
	case KP_UI_LEFT_DOWN: case KP_UI_LEFT_DOUBLE:
		kp_ui_set_capture(ui, self, KP_UI_LEFT_UP);
		kp_ui_set_capture(ui, self, KP_UI_MOUSE_MOVE);
		track_press(ui, self, e);
		return 1;
	case KP_UI_LEFT_UP:
		track_press(ui, self, e);
		update_hover(ui, dialog_of(ui, self), e);
		release_push(ui, self);
		return 1;
	case KP_UI_USER:
		break;
	default:
		return 0;
	}
	if (e->code == KP_UI_ACTIVATE) {
		kp_ui_mark_dirty(ui, self);
		if (!(c->flags & KP_UI_PRESSED)) {
			schedule_press(ui, self);
			c->flags |= KP_UI_PRESSED;
		} else {
			release_push(ui, self);
		}
		return 1;
	}
	if (e->code == KP_UI_FOCUS_LOST) c->flags &= ~KP_UI_PRESSED;
	return control_default(ui, c, e);
}

/* ControlDefaultInteract 0x4c1e90. */
static int control_default(KpUi *ui, KpUiControl *c, KpUiEvent *e)
{
	int32_t self = (int32_t)(c - ui->controls);
	if (e->type != KP_UI_USER) return 0;
	switch (e->code) {
	case KP_UI_DESTROY:
		kp_ui_delete_owner_timers(&ui->timers, self);
		kp_ui_release_refs(ui, self);
		if (!(c->flags & KP_UI_SHARED_BITMAP)) kp_ui_drop_image(ui, c);
		return 1;
	case KP_UI_HIT_TEST:
		return eligible(c);
	case KP_UI_FOCUS:
		return 1;
	case KP_UI_FOCUS_LOST:
		kp_ui_release_refs(ui, self);
		if (c->type == 0) {
			send_children(ui, c, e);
		} else if (ui->controls[c->parent].active == self) {
			/* the parent's focus passes on to the next focusable control in the ring (perhaps itself), or none */
			int32_t start = ring_next(ui, self), k = start;
			do {
				if (focusable(&ui->controls[k])) {
					kp_ui_focus_control(ui, k);
					return 1;
				}
				k = ring_next(ui, k);
			} while (k != start);
			set_focus_pointer(ui, self, -1);
		}
		return 1;
	case KP_UI_TIMER:
		if (c->type == 0) {
			send_children(ui, c, e);
		} else if (e->param == SHOW_TIMER && !(c->flags & KP_UI_VISIBLE) && c->delay != 0) {
			c->delay = (uint16_t)(c->delay - SHOW_PERIOD);
			if (c->delay == 0 && kp_ui_send_user(ui, self, KP_UI_SHOW, 0)) c->flags |= KP_UI_DIRTY;
		}
		return 1;
	case KP_UI_POST_LOAD:
		if (c->delay != 0) {
			if ((c->flags & KP_UI_VISIBLE) && kp_ui_send_user(ui, self, KP_UI_HIDE, 0)) c->flags |= KP_UI_DIRTY;
			c->delay = (uint16_t)((c->delay + 50) / 100 * 100);
		}
		return 1;
	case KP_UI_SHOW:
		if (c->delay == 0) {
			c->flags |= KP_UI_VISIBLE;
			if (c->parent >= 0 && c->type != 0) { /* UpdateHoverForControl 0x4c1ae0 at the cursor */
				const KpUiControl *d = &ui->controls[c->parent];
				int32_t x = e->x, y = e->y;
				if (inside(d, x, y) && inside(c, x - d->left, y - d->top) && !(c->flags & KP_UI_HOVER)
					&& send_code(ui, self, KP_UI_HIT_TEST, 0, e)) {
					set_hover(ui, c->parent, self);
				}
			}
		}
		return 1;
	case KP_UI_HIDE:
		c->flags &= ~KP_UI_VISIBLE;
		return 1;
	default:
		return 0;
	}
}

/* DialogDefaultInteract 0x4c1470. */
static int dialog_default(KpUi *ui, KpUiControl *d, KpUiEvent *e)
{
	int32_t self = (int32_t)(d - ui->controls), c;
	if (e->type != KP_UI_USER) return dialog_input(ui, d, e);
	switch (e->code) {
	case KP_UI_CREATE:
		kp_ui_mark_dirty(ui, self);
		if (d->active < 0) { /* the first eligible child gets the focus (FocusControl: none under 0x20000000) */
			for (c = d->child; c >= 0; c = ui->controls[c].next) {
				if (eligible(&ui->controls[c])) {
					kp_ui_focus_control(ui, c);
					break;
				}
			}
		}
		send_children(ui, d, e);
		{ /* the control under the cursor becomes the hover control (SetHoverControl 0x4c2690) */
			int32_t hit = kp_ui_hit_test(ui, self, e);
			if (hit >= 0 && hit != self) set_hover(ui, self, hit);
		}
		return control_default(ui, d, e);
	case KP_UI_DESTROY:
		send_children(ui, d, e);
		kp_ui_surface_free(ui, d->bitmap.pixels);
		d->bitmap.pixels = NULL;
		ui->open_count--;
		return control_default(ui, d, e);
	case KP_UI_CLOSE:
		d->flags |= KP_UI_DIRTY;
		unlink_open(ui, self);
		ui->abort = 1;
		return 1;
	case KP_UI_HIT_TEST:
		return 1;
	case KP_UI_POST_LOAD:
		ui->open_count++;
		send_children(ui, d, e);
		d->next = ui->open;
		ui->open = self;
		return control_default(ui, d, e);
	default:
		return control_default(ui, d, e);
	}
}

/* ButtonProc 0x4c7500 (the OK and Cancel IDs pass their activation to the dialog), the option button's 0x4c7540 and
 * the check box's 0x4c74a0. */
/* SelectOptionInGroup 0x4c76a0: the option buttons with the IDs next to this one's are cleared, this one set. */
static void select_option(KpUi *ui, KpUiControl *c)
{
	int32_t k = (int32_t)(c - ui->controls), n;
	while ((n = kp_ui_find_control(ui, c->parent, (int16_t)(ui->controls[k].id - 1))) >= 0 && ui->controls[n].type == 3) k = n;
	ui->controls[k].words36[0] = 0;
	kp_ui_mark_dirty(ui, k);
	while ((k = kp_ui_find_control(ui, c->parent, (int16_t)(ui->controls[k].id + 1))) >= 0 && ui->controls[k].type == 3) {
		ui->controls[k].words36[0] = 0;
		kp_ui_mark_dirty(ui, k);
	}
	c->words36[0] = 1;
}

static int button_interact(KpUi *ui, KpUiControl *c, KpUiEvent *e)
{
	if (e->type == KP_UI_USER && e->code == KP_UI_CLOSE) {
		if (c->type <= 2 && ((uint16_t)c->id == ID_OK || (uint16_t)c->id == ID_CANCEL)) {
			KpUiControl *d = &ui->controls[c->parent];
			ui->result = (int16_t)c->id;
			return d->interact ? d->interact(ui, d, e) : 0;
		}
		if (c->type == 4) { /* CheckBoxProc 0x4c74a0 */
			c->words36[0] ^= 1;
			return 1;
		}
		if (c->type == 3) { /* OptionButtonProc 0x4c7540 */
			select_option(ui, c);
			return 1;
		}
	}
	if (e->type == KP_UI_USER && e->code == KP_UI_POST_LOAD && (c->type == 3 || c->type == 4)) {
		int16_t h = (int16_t)(c->top + (c->type == 4 ? ui->theme.check_size : ui->theme.option_size));
		if (c->type == 4) c->wuser = CHECK_BOX_FRAME;
		if (c->bottom < h) c->bottom = h;
	}
	if (e->type == KP_UI_USER && e->code == KP_UI_VALUE_CHANGED && (c->type == 3 || c->type == 4)) {
		if (c->type == 4) c->words36[0] = e->param != 0;
		else if (e->param != 0) select_option(ui, c);
		return 1;
	}
	return button_base(ui, c, e);
}

/* ImageControlInteract 0x4c17e0: code 1 frees the bitmap without the defaults; 0, 6, 8, 10, 13, 14 the defaults. */
static int image_interact(KpUi *ui, KpUiControl *c, KpUiEvent *e)
{
	if (e->type != KP_UI_USER) return 0;
	if (e->code == KP_UI_DESTROY) {
		kp_ui_drop_image(ui, c);
		return 0;
	}
	if (e->code == KP_UI_CREATE || e->code == KP_UI_FOCUS_LOST || e->code == KP_UI_TIMER || e->code == KP_UI_POST_LOAD
		|| e->code == KP_UI_SHOW || e->code == KP_UI_HIDE) {
		return control_default(ui, c, e);
	}
	return 0;
}

/* StaticControlInteract 0x4c1860: 0, 6, 8, 10, 13, 14 the defaults, others 0. */
static int static_interact(KpUi *ui, KpUiControl *c, KpUiEvent *e)
{
	if (e->type != KP_UI_USER) return 0;
	if (e->code == KP_UI_CREATE || e->code == KP_UI_FOCUS_LOST || e->code == KP_UI_TIMER || e->code == KP_UI_POST_LOAD
		|| e->code == KP_UI_SHOW || e->code == KP_UI_HIDE) {
		return control_default(ui, c, e);
	}
	return 0;
}

void kp_ui_set_enabled(KpUi *ui, int32_t control, int enable)
{
	/* EnableControl 0x4c2910 (SetControlEnabled 0x4c2600's branch), DisableControl 0x4c2280 */
	KpUiControl *c = &ui->controls[control];
	if (enable) {
		if (!(c->flags & KP_UI_DISABLED)) return;
		c->flags &= ~KP_UI_DISABLED;
		if (c->parent >= 0) { /* RefreshHoverAtCursor 0x4c1b90 */
			KpUiEvent at;
			memset(&at, 0, sizeof at);
			at.type = KP_UI_USER;
			at.x = ui->mouse_x;
			at.y = ui->mouse_y;
			update_hover(ui, c->parent, &at);
		}
		kp_ui_mark_dirty(ui, control);
		return;
	}
	if (c->flags & KP_UI_DISABLED) return;
	c->flags = (c->flags & ~KP_UI_HOVER) | KP_UI_DISABLED;
	kp_ui_mark_dirty(ui, control);
	(void)kp_ui_send_user(ui, control, KP_UI_FOCUS_LOST, 0);
}

KpUiInteract kp_ui_type_interact(int type)
{
	switch (type) {
	case 0: return dialog_default;
	case 1: case 2: case 3: case 4: return button_interact;
	case 5: return image_interact;
	case 6: case 7: return kp_ui_scrollbar_proc;
	case 8: return kp_ui_edit_proc;
	case 9: case 10: case 11: return static_interact;
	case 12: return kp_ui_list_proc;
	default: return control_default; /* type 13, a combo box: not ported (see kp_ui_type_draw) */
	}
}

/* --- painting (PaintDialogLayer 0x4c4d90, CollectPaintTargets 0x4c4910, DrawControlClipped 0x4c51c0) --- */

static int intersect16(int16_t r[4], const int16_t o[4])
{
	if (o[0] > r[0]) r[0] = o[0];
	if (o[1] > r[1]) r[1] = o[1];
	if (o[2] < r[2]) r[2] = o[2];
	if (o[3] < r[3]) r[3] = o[3];
	return r[1] > r[3] || r[0] > r[2];
}

/* DrawControlClipped 0x4c51c0 with a dirty rectangle in screen coordinates. */
static void draw_clipped(KpUi *ui, int32_t dialog, KpUiControl *c, const int16_t rect[4])
{
	KpUiControl *d = &ui->controls[dialog];
	int16_t r[4], dr[4], screen[4];
	int32_t x, y;
	KpUiSurface *saved = ui->text.target;
	if (c->draw == NULL || d->bitmap.pixels == NULL) return;
	r[0] = rect[0];
	r[1] = rect[1];
	r[2] = rect[2];
	r[3] = rect[3];
	dr[0] = d->left;
	dr[1] = d->top;
	dr[2] = d->right;
	dr[3] = d->bottom;
	(void)intersect16(r, dr);
	r[0] = (int16_t)(r[0] - d->left);
	r[1] = (int16_t)(r[1] - d->top);
	r[2] = (int16_t)(r[2] - d->left);
	r[3] = (int16_t)(r[3] - d->top);
	screen[0] = 0;
	screen[1] = 0;
	screen[2] = 639;
	screen[3] = 479;
	(void)intersect16(r, screen);
	x = r[0];
	y = r[1];
	if (c != d) {
		int16_t cr[4];
		cr[0] = c->left;
		cr[1] = c->top;
		cr[2] = c->right;
		cr[3] = c->bottom;
		(void)intersect16(r, cr);
		x = x - c->left > 0 ? x - c->left : 0;
		y = y - c->top > 0 ? y - c->top : 0;
	}
	if (c->type == 5) ui->effects.level[0] = kp_ui_effect_level(ui->effects.image_percent); /* 0x4ca7e0 */
	else if (c->type == 13) ui->effects.level[0] = kp_ui_effect_level(ui->effects.combo_percent);
	ui->text.target = &d->bitmap;
	c->draw(ui, c, x, y, r);
	ui->text.target = saved;
}

void kp_ui_paint_dialog(KpUi *ui, int32_t dialog)
{
	KpUiControl *d = &ui->controls[dialog];
	int16_t rect[4];
	int32_t c;
	rect[0] = d->left;
	rect[1] = d->top;
	rect[2] = d->right;
	rect[3] = d->bottom;
	d->flags &= ~KP_UI_DIRTY;
	draw_clipped(ui, dialog, d, rect);
	for (c = d->child; c >= 0; c = ui->controls[c].next) {
		KpUiControl *k = &ui->controls[c];
		k->flags &= ~KP_UI_DIRTY;
		if (k->flags & KP_UI_VISIBLE) draw_clipped(ui, dialog, k, rect);
	}
}

/* Storm's region (storm.dll SRgnCombineRect 523 in mode 2, SRgnGetRectsf 529): the rectangles ORed in, in order, are
 * cut into fragments around the earlier fragments (0x15029990), one merge pass joins adjacent fragments
 * (0x15029450), and the fragments are sorted by bottom, then left (0x1502b290). Storm keeps floats; the console's
 * rectangles are whole multiples of 16. */
#define FRAG_DELETED INT32_MAX
typedef struct {
	int32_t l, t, r, b;
} Frag;
#define FRAG_INLINE 512 /* the fragments kept in the struct: a console's region is tens, so the heap is not touched */
typedef struct {
	Frag *v;
	int n, cap;
	Frag inline_v[FRAG_INLINE];
} Frags;

static int frag_push(Frags *f, Frag r)
{
	if (f->n == f->cap) {
		int cap = 2 * f->cap;
		Frag *v;
		if (f->v == f->inline_v) {
			v = (Frag *)malloc((size_t)cap * sizeof(Frag));
			if (v != NULL) memcpy(v, f->v, (size_t)f->n * sizeof(Frag));
		} else {
			v = (Frag *)realloc(f->v, (size_t)cap * sizeof(Frag));
		}
		if (v == NULL) return -1;
		f->v = v;
		f->cap = cap;
	}
	f->v[f->n++] = r;
	return 0;
}

static int frag_empty(Frag r) { return r.l >= r.r || r.t >= r.b; }

/* 0x15029990: r against the fragments [start, end); an overlapped one cuts r into the parts outside it. */
static void frag_add(Frags *f, int start, int end, Frag r)
{
	int i, k;
	for (i = start; i < end; i++) {
		Frag e = f->v[i], parts[4];
		int32_t top, bottom;
		if (!(e.r > r.l && e.b > r.t && r.r > e.l && r.b > e.t)) continue;
		top = r.t > e.t ? r.t : e.t;
		bottom = e.b > r.b ? r.b : e.b;
		parts[0].l = r.l, parts[0].t = r.t, parts[0].r = r.r, parts[0].b = e.t;
		parts[1].l = r.l, parts[1].t = e.b, parts[1].r = r.r, parts[1].b = r.b;
		parts[2].l = r.l, parts[2].t = top, parts[2].r = e.l, parts[2].b = bottom;
		parts[3].l = e.r, parts[3].t = top, parts[3].r = r.r, parts[3].b = bottom;
		for (k = 0; k < 4; k++) {
			if (!frag_empty(parts[k])) frag_add(f, i + 1, end, parts[k]);
		}
		return;
	}
	(void)frag_push(f, r);
}

static void frag_delete(Frag *r) { r->l = r->t = r->r = r->b = FRAG_DELETED; }

/* 0x15029450: each fragment against those before it; the first neighbour it joins ends its turn. */
static void frag_merge(Frags *f)
{
	int i, j, k;
	for (i = 1; i < f->n; i++) {
		for (j = 0; j < i; j++) {
			Frag *a = &f->v[i], *b = &f->v[j];
			if (a->l == b->l && a->r == b->r && (a->t == b->b || b->t == a->b)) { /* stacked */
				if (b->t < a->t) a->t = b->t;
				if (b->b > a->b) a->b = b->b;
				frag_delete(b);
				break;
			}
			if (a->l != b->r && b->l != a->r) continue;
			if (a->t == b->t && a->b == b->b) { /* side by side */
				if (b->l < a->l) a->l = b->l;
				if (b->r > a->r) a->r = b->r;
				frag_delete(b);
				break;
			}
			if (b->b > a->t && a->b > b->t) { /* side by side, partly overlapping rows: five parts */
				Frag x = *a, y = *b, parts[5];
				parts[0].l = x.l, parts[0].t = x.t, parts[0].r = x.r, parts[0].b = y.t;
				parts[1].l = y.l, parts[1].t = y.t, parts[1].r = y.r, parts[1].b = x.t;
				parts[2].l = x.l, parts[2].t = y.b, parts[2].r = x.r, parts[2].b = x.b;
				parts[3].l = y.l, parts[3].t = x.b, parts[3].r = y.r, parts[3].b = y.b;
				parts[4].l = y.l <= x.l ? y.l : x.l;
				parts[4].t = x.t > y.t ? x.t : y.t;
				parts[4].r = x.r > y.r ? x.r : y.r;
				parts[4].b = y.b <= x.b ? y.b : x.b;
				for (k = 0; k < 5; k++) {
					if (!frag_empty(parts[k])) (void)frag_push(f, parts[k]);
				}
				frag_delete(&f->v[i]);
				frag_delete(&f->v[j]);
				break;
			}
		}
	}
}

static int frag_compare(const void *pa, const void *pb)
{
	const Frag *a = (const Frag *)pa, *b = (const Frag *)pb;
	int64_t d = a->b != b->b ? (int64_t)a->b - b->b : (int64_t)a->l - b->l;
	return d > 0 ? 1 : d < 0 ? -1 : 0;
}

int kp_ui_region(const KpUi *ui, int16_t out[KP_UI_REGION][4])
{
	Frags f;
	int i, n;
	f.v = f.inline_v;
	f.n = 0;
	f.cap = FRAG_INLINE;
	for (i = 0; i < ui->region_count; i++) {
		Frag r;
		r.l = ui->region[i][0];
		r.t = ui->region[i][1];
		r.r = ui->region[i][2];
		r.b = ui->region[i][3];
		frag_add(&f, 0, f.n, r);
	}
	frag_merge(&f);
	if (f.n > 1) qsort(f.v, (size_t)f.n, sizeof(Frag), frag_compare);
	while (f.n > 0 && frag_empty(f.v[f.n - 1])) f.n--;
	n = f.n < KP_UI_REGION - 1 ? f.n : KP_UI_REGION - 1; /* PaintDialogLayer asks for at most 255 */
	for (i = 0; i < n; i++) {
		out[i][0] = (int16_t)f.v[i].l;
		out[i][1] = (int16_t)f.v[i].t;
		out[i][2] = (int16_t)(f.v[i].r - 1);
		out[i][3] = (int16_t)(f.v[i].b - 1);
	}
	if (f.v != f.inline_v) free(f.v);
	return n;
}

static int overlaps(const int16_t a[4], int32_t l, int32_t t, int32_t r, int32_t b)
{
	return a[0] <= r && a[2] >= l && a[1] <= b && a[3] >= t;
}

/* The dialog a shown nested dialog (a popup) is composited into, -1 with none. */
static int32_t popup_target(const KpUi *ui)
{
	int32_t d;
	int nested = 0;
	for (d = ui->open; d >= 0; d = ui->controls[d].next) {
		uint32_t flags = ui->controls[d].flags;
		if (!(flags & KP_UI_VISIBLE)) continue;
		if (flags & DIALOG_NESTED) nested = 1;
		else if (nested) return d;
	}
	return -1;
}

int kp_ui_present(KpUi *ui)
{
	int16_t rects[KP_UI_REGION][4];
	int32_t order[KP_UI_CONTROLS], d, under;
	int n, count = 0, i, k, draws = 0;
	kp_ui_tooltip_invalidate(ui);
	/* Not the original's (ADR 0011): a control's text is plotted whole when a rectangle touches the control, and the
	 * popup is composited over the rectangles alone. The original's screen takes only the rectangles from the bitmap;
	 * the canvas shows all of it, so text plotted past them would lie over the popup (the campaign's labels, whose
	 * large buttons the rectangle of the popup's Ok and Cancel touches). With a popup the whole screen is repainted. */
	under = popup_target(ui);
	if (under >= 0 && ui->region_count > 0) {
		const KpUiControl *t = &ui->controls[under];
		ui->region_count = 0;
		kp_ui_add_dirty_rect(ui, t->left, t->top, t->right, t->bottom);
	}
	n = kp_ui_region(ui, rects);
	for (d = ui->open; d >= 0 && count < KP_UI_CONTROLS; d = ui->controls[d].next) {
		if (ui->controls[d].flags & KP_UI_VISIBLE) order[count++] = d;
	}
	int32_t target = -1;
	ui->image_log.repaint = ui->image_log.serial + 1; /* what it draws lies under what it draws after */
	for (i = count - 1; i >= 0; i--) { /* from the bottom dialog up */
		KpUiControl *dl = &ui->controls[order[i]];
		if (!(dl->flags & DIALOG_NESTED)) target = order[i]; /* 0x6ce788: a nested dialog's blit target */
		for (k = 0; k < n; k++) {
			int32_t c;
			if (!overlaps(rects[k], dl->left, dl->top, dl->right, dl->bottom)) continue;
			dl->flags &= ~KP_UI_DIRTY; /* CollectPaintTargets clears the dialog's and every child's dirty flag */
			for (c = dl->child; c >= 0; c = ui->controls[c].next) ui->controls[c].flags &= ~KP_UI_DIRTY;
			draw_clipped(ui, order[i], dl, rects[k]);
			draws++;
			for (c = dl->child; c >= 0; c = ui->controls[c].next) {
				KpUiControl *ch = &ui->controls[c];
				if (!(ch->flags & KP_UI_VISIBLE)
					|| !overlaps(rects[k], dl->left + ch->left, dl->top + ch->top, dl->left + ch->right, dl->top + ch->bottom)) {
					continue;
				}
				draw_clipped(ui, order[i], ch, rects[k]);
				draws++;
			}
		}
	}
	/* the blit pass, from the top dialog down (BlitDialogToScreen 0x4c5630): a nested glue dialog (a popup) is
	 * composited into the target's bitmap with its flags, the effect level of composited dialogs (0x6ce780); the
	 * others reach the screen through the host's canvas */
	for (i = 0; i < count && target >= 0; i++) {
		KpUiControl *dl = &ui->controls[order[i]], *t = &ui->controls[target];
		KpUiSurface *saved = ui->text.target;
		if (!(dl->flags & DIALOG_NESTED) || order[i] == target || dl->bitmap.pixels == NULL) continue;
		ui->effects.level[0] = kp_ui_effect_level(ui->effects.dialog_percent);
		ui->text.target = &t->bitmap;
		for (k = 0; k < n; k++) {
			int16_t r[4], into[4];
			r[0] = (int16_t)(rects[k][0] > dl->left ? rects[k][0] : dl->left);
			r[1] = (int16_t)(rects[k][1] > dl->top ? rects[k][1] : dl->top);
			r[2] = (int16_t)(rects[k][2] < dl->right ? rects[k][2] : dl->right);
			r[3] = (int16_t)(rects[k][3] < dl->bottom ? rects[k][3] : dl->bottom);
			if (r[1] > r[3] || r[0] > r[2]) continue;
			into[0] = (int16_t)(r[0] - t->left);
			into[1] = (int16_t)(r[1] - t->top);
			into[2] = (int16_t)(r[2] - t->left);
			into[3] = (int16_t)(r[3] - t->top);
			kp_ui_blit_clipped(ui, dl->bitmap.pixels, dl->bitmap.w, dl->bitmap.h, r[0] - dl->left, r[1] - dl->top, into,
				dl->flags);
		}
		ui->text.target = saved;
	}
	ui->image_log.repaint = 0;
	ui->region_count = 0;
	return draws;
}

/* --- PCX --- */

int kp_ui_pcx(const uint8_t *data, size_t size, int32_t *w, int32_t *h, uint8_t *pixels, size_t capacity)
{
	size_t pos = 128, out = 0, total;
	int32_t bpl;
	if (size < 128 || data[0] != 0x0a || data[2] != 1 || data[3] != 8 || data[65] != 1) return -1;
	*w = (int32_t)(kp_le_u16(data + 8) - kp_le_u16(data + 4) + 1);
	*h = (int32_t)(kp_le_u16(data + 10) - kp_le_u16(data + 6) + 1);
	bpl = (int32_t)kp_le_u16(data + 66);
	if (*w <= 0 || *h <= 0 || bpl < *w) return -1;
	total = (size_t)*w * (size_t)*h;
	if (pixels == NULL) return 0;
	if (capacity < total) return -1;
	{
		int32_t row, col = 0;
		for (row = 0; row < *h && pos < size;) {
			uint8_t b = data[pos++];
			int count = 1;
			if ((b & 0xc0) == 0xc0) {
				count = b & 0x3f;
				if (pos >= size) break;
				b = data[pos++];
			}
			while (count-- > 0) {
				if (col < *w) pixels[out++] = b;
				if (++col == bpl) {
					col = 0;
					row++;
					if (row >= *h) break;
				}
			}
		}
	}
	return out == total ? 0 : -1;
}
