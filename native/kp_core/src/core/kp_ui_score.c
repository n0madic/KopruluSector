/* The score screen (gluscore.cpp, atlas P21 0x461940..0x463350): rez\gluScore.bin over the score directory
 * glue\score{Z,T,P}{d,v}\ of the local race and result, the four tabs (Overview, Units, Structures, Resources), the rows
 * of the roster sorted by their totals, the count-up animation every 20 ms and the OK that ends it (ui->result 7). The
 * host gives the finished game's data (kp_ui_score_data from a KpGame, or the oracle's from the emulator's log). */
#include "kp_ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kp_game.h"
#include "kp_ids.h"

#define DIRECTORIES 6
#define ID_IMAGE 1
#define ID_TITLE 2
#define ID_TAB_FIRST 3
#define TABS 4
#define ID_OK 7
#define ID_TOTAL_LABEL 8
#define ID_ELAPSED 9
#define ID_HEADER_FIRST 10
#define ID_ROW_FIRST 13         /* row r: 13 + 6r inset, 14 + 6r name, 15..17 + 6r columns, 18 + 6r total */
#define ROW_CONTROLS 6
#define ID_SAVE_REPLAY 61
#define PROCS 61
#define COLUMNS 3
#define TOTAL 3                 /* the rows' value index of the total */
#define COUNT_TIMER 0x22
#define COUNT_PERIOD 20
#define SLIDE_DELAY 500
#define SOUND_FILL 0xa8         /* glue\scorefill.wav */
#define SOUND_FILL_END 0xa9     /* glue\scorefillend.wav */
#define RESULT_VICTORY 1
#define FLAG_SELECTED KP_UI_CONSUMED /* a tab's bit 31 */
#define FLAG_TITLE KP_UI_PRESSED     /* colour table 6 */
#define FLAG_TILED 0x20000000u
#define TYPE_COMPUTER 1
#define TYPE_HUMAN 2
#define TYPE_HUMAN_NETWORK 10
#define TYPE_COMPUTER_NETWORK 11
#define NAME_CLIP_RIGHT 184
#define ICON_X 8
#define ICON_Y 20
#define FORCE_GAP 40
#define FORCE_COLOUR 6
#define WIN_BONUS 0x3fffffff
#define INVALID_KEY ((int32_t)0x80000001u)
#define UNTAB_Y 28
#define FORMAT_NAME 0x11
#define FORMAT_VALUE 0x12
#define FORMAT_TAB 0x22
#define STR_ELAPSED 0x10        /* gluAll: "Elapsed Time: %s" */
#define STR_REPLAY 0xb2
#define ORIGINAL_NAMES 0x1d   /* network.tbl: the computers' names, terran, zerg, protoss by colour */
#define EXPANSION_NAMES 0x91
#define NO_STORM_PLAYER 0xffffffffu

/* 0x508454, 0x50846c, 0x508484: the directories, glue states and music of race * 2 + victory (a replay: 1) */
static const char *const DIRS[DIRECTORIES] = { "glue\\scoreZd\\", "glue\\scoreZv\\", "glue\\scoreTd\\", "glue\\scoreTv\\",
	"glue\\scorePd\\", "glue\\scorePv\\" };
static const uint8_t STATES[DIRECTORIES] = { 14, 15, 16, 17, 18, 19 };
static const uint8_t THEMES[DIRECTORIES] = { 7, 8, 9, 10, 5, 6 }; /* the glue state table 0x4ed820: PalZd .. PalPv */
static const uint8_t MUSIC[DIRECTORIES] = { 13, 16, 14, 17, 15, 18 };
/* 0x50849c: the column headers of each tab (gluAll) */
static const uint8_t HEADERS[TABS][COLUMNS] = { { 4, 5, 6 }, { 7, 8, 9 }, { 10, 11, 12 }, { 13, 14, 15 } };
/* the title by the local result 0..4 (0x462270): Draw, Victory, Defeat, Draw, Disconnect */
static const uint8_t TITLES[5] = { 0x13, 0x12, 0x11, 0x13, 0x14 };
static const uint16_t SLIDE_TABLE[2] = { ID_IMAGE, 0 }; /* 0x5085c4: control 1 from the left */

static KpUiScore *sc(KpUi *ui) { return &ui->score; }

static int32_t control(KpUi *ui, int id) { return kp_ui_find_control(ui, sc(ui)->dialog, (int16_t)id); }

static int listed(uint32_t type)
{
	return type == TYPE_COMPUTER || type == TYPE_HUMAN || type == TYPE_HUMAN_NETWORK || type == TYPE_COMPUTER_NETWORK;
}

/* The roster entry's colour index k (MapPlayerColor 0x41d560, then the score screen's table 0x536722). */
static int colour_of(const KpUiScore *s, const KpUiScoreRow *r)
{
	uint32_t i = s->data.roster[r->roster].index;
	int k = i < 8 ? s->data.player_colours[i] : (int)i;
	if (k >= 0 && k < 12 && s->data.colours[k] != 0) k = s->data.colours[k];
	return k;
}

/* A file of the score directory as a surface (0 or -1). */
static int load_pcx(KpUi *ui, const char *name, KpUiSurface *out)
{
	char path[64];
	const KpUiFile *f;
	int32_t w, h;
	snprintf(path, sizeof path, "%s%s", DIRS[sc(ui)->index], name);
	if (out->pixels != NULL) return 0; /* loaded once */
	if ((f = kp_ui_glue_file(ui, path)) == NULL || kp_ui_pcx(f->data, f->size, &w, &h, NULL, 0) != 0) return -1;
	out->pixels = (uint8_t *)malloc((size_t)w * (size_t)h);
	if (out->pixels == NULL || kp_ui_pcx(f->data, f->size, &w, &h, out->pixels, (size_t)w * (size_t)h) != 0) {
		free(out->pixels);
		out->pixels = NULL;
		return -1;
	}
	out->w = w;
	out->h = h;
	return 0;
}

static void free_surface(KpUiSurface *s)
{
	free(s->pixels);
	s->pixels = NULL;
}

static void set_image(KpUiControl *c, const KpUiSurface *s)
{
	c->image = s->pixels;
	c->image_w = (uint16_t)s->w;
	c->image_h = (uint16_t)s->h;
	c->owns_image = 0;
	c->flags |= KP_UI_SHARED_BITMAP;
}

/* --- the rows --- */

static int64_t value_of(const KpUiScore *s, int a, uint32_t p) { return p < 12 ? s->data.arrays[a][p] : 0; }

/* The tab callbacks 0x462b60, 0x462c10, 0x462b20, 0x462bb0: the three columns and the total of score index p. */
static void tab_values(const KpUiScore *s, int tab, uint32_t p, int32_t v[4])
{
	int64_t gas = value_of(s, KP_UI_SCORE_CUMULATIVE_GAS, p), minerals = value_of(s, KP_UI_SCORE_CUMULATIVE_MINERALS, p);
	switch (tab) {
	case 0:
		v[0] = (int32_t)(value_of(s, KP_UI_SCORE_UNIT_SCORE, p) + value_of(s, KP_UI_SCORE_KILL_SCORE, p));
		v[1] = (int32_t)(value_of(s, KP_UI_SCORE_STRUCTURE_SCORE, p) + value_of(s, KP_UI_SCORE_RAZING_SCORE, p));
		v[2] = (int32_t)(gas + minerals);
		v[3] = (int32_t)((int64_t)v[0] + v[1] + v[2]);
		break;
	case 1:
		v[0] = (int32_t)value_of(s, KP_UI_SCORE_MEN_PRODUCED, p);
		v[1] = (int32_t)value_of(s, KP_UI_SCORE_MEN_KILLED, p);
		v[2] = (int32_t)value_of(s, KP_UI_SCORE_MEN_LOST, p);
		v[3] = (int32_t)(value_of(s, KP_UI_SCORE_UNIT_SCORE, p) + value_of(s, KP_UI_SCORE_KILL_SCORE, p));
		break;
	case 2:
		v[0] = (int32_t)value_of(s, KP_UI_SCORE_BUILDINGS_CONSTRUCTED, p);
		v[1] = (int32_t)value_of(s, KP_UI_SCORE_BUILDINGS_RAZED, p);
		v[2] = (int32_t)value_of(s, KP_UI_SCORE_BUILDINGS_LOST, p);
		v[3] = (int32_t)(value_of(s, KP_UI_SCORE_STRUCTURE_SCORE, p) + value_of(s, KP_UI_SCORE_RAZING_SCORE, p));
		break;
	default: {
		int64_t spent = gas + minerals - value_of(s, KP_UI_SCORE_GAS, p) - value_of(s, KP_UI_SCORE_MINERALS, p);
		v[0] = (int32_t)gas;
		v[1] = (int32_t)minerals;
		v[2] = (int32_t)(spent > 0 ? spent : 0);
		v[3] = (int32_t)(gas + minerals);
		break;
	}
	}
}

static void compute_rows(KpUiScore *s)
{
	/* ComputeScoreRows 0x4629f0: the counters start at 0, the steps make the count-up's length */
	int i, k;
	for (i = 0; i < KP_UI_SCORE_SLOTS; i++) {
		KpUiScoreRow *r = &s->rows[i];
		if (!listed(s->data.roster[i].type)) continue;
		r->valid = 1;
		r->roster = i;
		tab_values(s, s->tab, s->data.roster[i].index, r->value);
		for (k = 0; k < 4; k++) {
			int32_t v = r->value[k], q = v != 0 ? 256 / v : 0;
			int32_t step = q == 0 ? 1 : q > 8 ? 16 : q >= 2 ? 8 : q;
			r->step[k] = step;
			r->target[k] = (int32_t)((uint32_t)v * (uint32_t)step);
		}
	}
}

/* ScoreSortKey 0x4623c0. */
static int32_t sort_key(const KpUiScore *s, int i, int j)
{
	const KpUiScoreRow *r = &s->rows[i];
	const KpUiRosterEntry *e;
	uint32_t key;
	if (!r->valid) return INVALID_KEY;
	key = (uint32_t)r->value[TOTAL] * 8u + 8u - (uint32_t)j;
	e = &s->data.roster[r->roster];
	if (e->type != TYPE_COMPUTER && e->type != TYPE_COMPUTER_NETWORK && e->storm == s->data.local_storm) {
		/* TODO: the other Storm players' results (multiplayer) */
		if (s->data.result == RESULT_VICTORY) key += WIN_BONUS;
		else if (s->data.result == 2) key -= WIN_BONUS;
	}
	return (int32_t)key;
}

static void sort_rows(KpUiScore *s)
{
	/* SortScoreRows 0x4630c0: a selection sort, descending, then the column maxima */
	int i, j, c;
	for (i = 0; i < s->data.count && i < KP_UI_SCORE_SLOTS; i++) {
		int best = i;
		int32_t best_key = sort_key(s, i, i);
		for (j = i + 1; j < KP_UI_SCORE_SLOTS; j++) {
			int32_t key = sort_key(s, j, j);
			if (key > best_key) {
				best = j;
				best_key = key;
			}
		}
		if (best > i) {
			KpUiScoreRow t = s->rows[i];
			s->rows[i] = s->rows[best];
			s->rows[best] = t;
		}
		if (s->rows[i].valid) {
			for (c = 0; c < COLUMNS; c++) {
				if (s->rows[i].value[c] > s->maxima[c]) s->maxima[c] = s->rows[i].value[c];
			}
		}
	}
}

/* --- the drawing --- */

static KpUiScoreRow *row_of(KpUi *ui, const KpUiControl *c)
{
	return c->user != 0 && c->user <= KP_UI_SCORE_SLOTS ? &sc(ui)->rows[c->user - 1] : NULL;
}

static void draw_column(KpUi *ui, KpUiControl *c, int32_t x, int32_t y, const int16_t clip[4])
{
	/* DrawScoreColumn 0x4628c0: the bar of iScore.grp's frames in the player's colour, then the value */
	KpUiScore *s = sc(ui);
	KpUiScoreRow *r = row_of(ui, c);
	const uint8_t *saved_grp = ui->theme.grp;
	size_t saved_size = ui->theme.grp_size;
	int16_t rect[4];
	int col = c->wuser % COLUMNS, k;
	int32_t v, max, w;
	uint32_t fill;
	(void)x;
	(void)y;
	(void)clip;
	if (r == NULL || !r->valid || s->icons == NULL) return;
	v = r->step[col] != 0 ? r->counter[col] / r->step[col] : 0;
	max = s->maxima[col];
	w = c->right - c->left + 1;
	k = colour_of(s, r);
	fill = max == 0 ? 0 : (uint32_t)w * (uint32_t)v / (uint32_t)max;
	if (max != 0 && fill > (uint32_t)w) fill = (uint32_t)w;
	if (v != 0 && fill == 0) fill = 1;
	rect[0] = c->left;
	rect[1] = c->top;
	rect[2] = c->right;
	rect[3] = c->bottom;
	ui->theme.grp = s->icons->data;
	ui->theme.grp_size = s->icons->size;
	if (w != 0) kp_ui_three_part_bar(ui, c->left, c->top, w, ui->theme.bar_piece, k * 6 + 9, rect, KP_UI_TRANSPARENT);
	if (fill != 0) kp_ui_three_part_bar(ui, c->left, c->top, (int32_t)fill, ui->theme.bar_piece, k * 6 + 6, rect,
		KP_UI_TRANSPARENT);
	ui->theme.grp = saved_grp;
	ui->theme.grp_size = saved_size;
	if (c->text != NULL) kp_ui_draw_control_text(ui, c, 0, 2, NULL, FORMAT_VALUE);
}

static void outline(KpUi *ui, int32_t x, int32_t y, int32_t w, int32_t h, uint8_t colour)
{
	/* DrawRectOutline 0x4ce7d0 */
	kp_ui_fill_rect(ui, x, y, w, 1, colour);
	kp_ui_fill_rect(ui, x, y + h - 1, w, 1, colour);
	kp_ui_fill_rect(ui, x, y, 1, h, colour);
	kp_ui_fill_rect(ui, x + w - 1, y, 1, h, colour);
}

static void draw_name(KpUi *ui, KpUiControl *c, int32_t x, int32_t y, const int16_t clip[4])
{
	/* DrawScoreName 0x462c50: the race's icon, the colour's box, the name and the force's name */
	KpUiScore *s = sc(ui);
	KpUiScoreRow *r = row_of(ui, c);
	const KpUiRosterEntry *e;
	const uint8_t *pixels;
	int32_t fw = 0, fh = 0;
	int16_t rect[4];
	int saved_wrap;
	(void)x;
	(void)y;
	(void)clip;
	if (r == NULL || s->icons == NULL) return;
	e = &s->data.roster[r->roster];
	rect[0] = (int16_t)(c->left + ICON_X);
	rect[1] = (int16_t)(c->top + ICON_Y);
	rect[2] = c->right;
	rect[3] = c->bottom;
	if (kp_ui_grp_bitmap(s->icons->data, s->icons->size, (uint8_t)(e->race + 3), &fw, &fh, &pixels) == 0) {
		kp_ui_blit_clipped(ui, pixels, fw, fh, 0, 0, rect, KP_UI_TRANSPARENT);
		outline(ui, (uint16_t)(c->left + ICON_X), (uint16_t)(c->top + ICON_Y), fw, fh, s->minimap[colour_of(s, r) % 12]);
	}
	saved_wrap = ui->text.wrap;
	if (c->text != NULL) {
		int16_t name_clip[4];
		name_clip[0] = c->left;
		name_clip[1] = c->top;
		name_clip[2] = NAME_CLIP_RIGHT;
		name_clip[3] = c->bottom;
		ui->text.wrap = 0;
		kp_ui_draw_control_text(ui, c, 0, 0, name_clip, FORMAT_NAME);
		ui->text.wrap = saved_wrap;
	}
	if (e->force[0] != 0) {
		uint8_t force[sizeof e->force + 1];
		size_t i, n = 0;
		for (i = 0; i < sizeof e->force && e->force[i] != 0; i++) { /* 0x4a8320: the control codes stripped */
			if ((uint8_t)e->force[i] >= 0x20) force[n++] = (uint8_t)e->force[i];
		}
		force[n] = 0;
		ui->text.wrap = 0;
		(void)kp_ui_text_select_font(&ui->text, ui->fonts[0], ui->font_sizes[0]);
		kp_ui_select_color(&ui->text, FORCE_COLOUR);
		ui->text.clip_left = c->left;
		ui->text.clip_top = c->top;
		ui->text.clip_right = c->right;
		ui->text.clip_bottom = c->bottom;
		ui->text.flags = FORMAT_NAME;
		(void)kp_ui_draw_text(&ui->text, c->left + (uint16_t)fw + FORCE_GAP, c->top + ICON_Y, force);
		ui->text.font = NULL;
		ui->text.wrap = saved_wrap;
	}
}

static void draw_total(KpUi *ui, KpUiControl *c, int32_t x, int32_t y, const int16_t clip[4])
{
	/* DrawScoreTotal 0x462e00 */
	int16_t rect[4];
	(void)x;
	(void)y;
	(void)clip;
	rect[0] = c->left;
	rect[1] = c->top;
	rect[2] = c->right;
	rect[3] = c->bottom;
	if (c->image != NULL) kp_ui_blit_clipped(ui, c->image, c->image_w, c->image_h, 0, 0, rect, KP_UI_TRANSPARENT);
	if (c->text != NULL) kp_ui_draw_control_text(ui, c, 4, 2, NULL, FORMAT_VALUE);
}

static void draw_tab(KpUi *ui, KpUiControl *c, int32_t x, int32_t y, const int16_t clip[4])
{
	/* DrawScoreTab 0x4632d0: a selected tab has the highlight colour and no image */
	uint32_t flags = c->flags;
	int16_t rect[4];
	(void)x;
	(void)y;
	c->flags &= ~KP_UI_HOVER;
	if (flags & FLAG_SELECTED) {
		c->flags |= KP_UI_HOVER;
	} else if (c->image != NULL) {
		rect[0] = c->left;
		rect[1] = (int16_t)(c->top + UNTAB_Y);
		rect[2] = clip[2];
		rect[3] = clip[3];
		kp_ui_blit_clipped(ui, c->image, c->image_w, c->image_h, 0, 0, rect, flags);
	}
	kp_ui_draw_control_text(ui, c, 0, 0, NULL, FORMAT_TAB);
	c->flags = flags;
}

/* --- the tabs and the count-up --- */

static void bind_rows(KpUi *ui)
{
	/* BindScoreRows 0x4626e0 */
	KpUiScore *s = sc(ui);
	int i, k;
	for (i = 0; i < KP_UI_SCORE_SLOTS; i++) {
		int32_t ids[ROW_CONTROLS];
		for (k = 0; k < ROW_CONTROLS; k++) ids[k] = control(ui, ID_ROW_FIRST + ROW_CONTROLS * i + k);
		if (i >= s->data.count) {
			for (k = 0; k < ROW_CONTROLS; k++) {
				if (ids[k] >= 0) kp_ui_hide_control(ui, ids[k]);
			}
			continue;
		}
		if (ids[0] >= 0) {
			ui->controls[ids[0]].user = (uint32_t)i + 1;
			kp_ui_show_control(ui, ids[0]);
		}
		if (ids[1] >= 0) {
			KpUiControl *c = &ui->controls[ids[1]];
			const KpUiRosterEntry *e = &s->data.roster[s->rows[i].roster];
			c->user = (uint32_t)i + 1;
			c->draw = draw_name;
			c->text = (const uint8_t *)e->name;
			kp_ui_show_control(ui, ids[1]);
			if (e->storm == s->data.local_storm && e->type != TYPE_COMPUTER && e->type != TYPE_COMPUTER_NETWORK) {
				c->flags |= KP_UI_PRESSED;
			} else {
				c->flags &= ~KP_UI_PRESSED;
			}
		}
		for (k = 0; k < COLUMNS; k++) {
			if (ids[2 + k] < 0) continue;
			ui->controls[ids[2 + k]].wuser = (uint16_t)k;
			ui->controls[ids[2 + k]].draw = draw_column;
			ui->controls[ids[2 + k]].user = (uint32_t)i + 1;
			ui->controls[ids[2 + k]].text = (const uint8_t *)s->rows[i].text[k];
			kp_ui_show_control(ui, ids[2 + k]);
		}
		if (ids[5] >= 0) {
			ui->controls[ids[5]].user = (uint32_t)i + 1;
			ui->controls[ids[5]].draw = draw_total;
			ui->controls[ids[5]].text = (const uint8_t *)s->rows[i].text[TOTAL];
			kp_ui_show_control(ui, ids[5]);
		}
	}
}

static void count_up(KpUi *ui, int32_t owner)
{
	/* ScoreCountUpTick 0x462f70 */
	KpUiScore *s = sc(ui);
	int i, k, changed = 0;
	static const int ORDER[4] = { TOTAL, 0, 1, 2 };
	for (i = 0; i < s->data.count && i < KP_UI_SCORE_SLOTS; i++) {
		KpUiScoreRow *r = &s->rows[i];
		for (k = 0; k < 4; k++) {
			int v = ORDER[k];
			int32_t id = control(ui, ID_ROW_FIRST + ROW_CONTROLS * i + (v == TOTAL ? 5 : 2 + v));
			if (r->counter[v] != r->target[v]) {
				int32_t inc = (r->target[v] - r->counter[v]) / 8;
				if (inc < 4) inc = 4;
				r->counter[v] = r->counter[v] + inc < r->target[v] ? r->counter[v] + inc : r->target[v];
				changed = 1;
			}
			snprintf(r->text[v], sizeof r->text[v], "%u", (unsigned)(r->step[v] != 0 ? r->counter[v] / r->step[v] : 0));
			if (id >= 0) kp_ui_mark_dirty(ui, id);
		}
	}
	if (!changed) {
		kp_ui_delete_timer(&ui->timers, owner, COUNT_TIMER);
		kp_ui_stop_sounds(ui);
		(void)kp_ui_play_sound(ui, SOUND_FILL_END);
	}
}

static void select_tab(KpUi *ui, int32_t tab)
{
	/* SelectScoreTab 0x462460 */
	KpUiScore *s = sc(ui);
	int32_t was = -1, id;
	int k;
	for (k = 0; k < TABS; k++) {
		if ((id = control(ui, ID_TAB_FIRST + k)) < 0) continue;
		if (ui->controls[id].words36[0]) was = id;
		ui->controls[id].words36[0] = 0;
		ui->controls[id].flags = (ui->controls[id].flags & ~FLAG_SELECTED) | FLAG_TILED;
		kp_ui_mark_dirty(ui, id);
	}
	s->tab = ui->controls[tab].id - ID_TAB_FIRST;
	ui->controls[tab].flags |= FLAG_SELECTED;
	ui->controls[tab].words36[0] = 1;
	if (was == tab) return;
	memset(s->maxima, 0, sizeof s->maxima);
	memset(s->rows, 0, sizeof s->rows);
	kp_ui_stop_sounds(ui);
	kp_ui_delete_timer(&ui->timers, s->dialog, COUNT_TIMER);
	for (k = 0; k < COLUMNS; k++) {
		if ((id = control(ui, ID_HEADER_FIRST + k)) >= 0) {
			ui->controls[id].text = kp_ui_tbl(ui, KP_UI_GLUALL, HEADERS[s->tab & 3][k]);
			kp_ui_mark_dirty(ui, id);
		}
	}
	compute_rows(s);
	sort_rows(s);
	bind_rows(ui);
	(void)kp_ui_set_timer(&ui->timers, s->dialog, COUNT_TIMER, COUNT_PERIOD, kp_ui_callback(ui, count_up), ui->now);
	if ((int64_t)s->maxima[0] + s->maxima[1] + s->maxima[2] > 0) (void)kp_ui_play_sound(ui, SOUND_FILL);
}

/* --- the procedures --- */

static int tab_proc(KpUi *ui, KpUiControl *c, KpUiEvent *e)
{
	/* ScoreTabProc 0x463350 */
	KpUiInteract base = kp_ui_type_interact(3);
	int32_t self = (int32_t)(c - ui->controls);
	if (e->type == KP_UI_USER) {
		switch (e->code) {
		case KP_UI_POST_LOAD:
			if (load_pcx(ui, "untab.pcx", &sc(ui)->untab) == 0) set_image(c, &sc(ui)->untab);
			c->draw = draw_tab;
			kp_ui_mark_dirty(ui, self);
			break;
		case KP_UI_DESTROY:
			free_surface(&sc(ui)->untab);
			c->image = NULL;
			break;
		case KP_UI_CLOSE:
			select_tab(ui, self);
			return 1;
		case KP_UI_VALUE_CHANGED:
			if (e->param != 0) select_tab(ui, self);
			return 1;
		case KP_UI_SHOW:
			if (c->id == ID_TAB_FIRST) select_tab(ui, self);
			break;
		default: break;
		}
	}
	return base(ui, c, e);
}

/* ScoreInsetProc 0x461f70 (pInset.pcx, the rule of the show) and ScoreTotalBoxProc 0x462e40 (scorebox.pcx). */
static int box_proc(KpUi *ui, KpUiControl *c, KpUiEvent *e, const char *file, KpUiSurface *s, int show_rule)
{
	KpUiInteract base = kp_ui_type_interact(c->type);
	if (e->type != KP_UI_USER) return base(ui, c, e);
	switch (e->code) {
	case KP_UI_POST_LOAD:
		if (load_pcx(ui, file, s) == 0) set_image(c, s);
		kp_ui_mark_dirty(ui, (int32_t)(c - ui->controls));
		break;
	case KP_UI_DESTROY:
		free_surface(s);
		c->image = NULL;
		break;
	case KP_UI_SHOW:
		if (show_rule) {
			KpUiScoreRow *r = row_of(ui, c);
			if (r == NULL || !r->valid) return 1;
		}
		break;
	default: break;
	}
	return base(ui, c, e);
}

static int inset_proc(KpUi *ui, KpUiControl *c, KpUiEvent *e)
{
	return box_proc(ui, c, e, "pInset.pcx", &sc(ui)->inset, 1);
}

static int total_proc(KpUi *ui, KpUiControl *c, KpUiEvent *e)
{
	return box_proc(ui, c, e, "scorebox.pcx", &sc(ui)->box, 0);
}

static int save_replay_proc(KpUi *ui, KpUiControl *c, KpUiEvent *e)
{
	/* SaveReplayButtonProc 0x4620c0: disabled after a replay, a loaded game or a campaign mission. TODO: the
	 * replay's save (0x4809d0) */
	if (e->type == KP_UI_USER && e->code == KP_UI_CREATE) {
		if (sc(ui)->data.replay || ui->game == NULL || kp_scalar(ui->game, KP_MISSION_INDEX) != KP_NIL) {
			c->flags |= KP_UI_DISABLED;
		}
	}
	return kp_ui_text_button_proc(ui, c, e);
}

static void elapsed_time(KpUi *ui)
{
	/* SetScoreElapsedTime 0x4625f0 */
	KpUiScore *s = sc(ui);
	char time[24];
	const uint8_t *label = kp_ui_tbl(ui, KP_UI_GLUALL, STR_ELAPSED);
	uint32_t t = s->data.elapsed;
	int32_t id = control(ui, ID_ELAPSED);
	size_t at = 0;
	const uint8_t *f;
	if (t >= 3600) snprintf(time, sizeof time, "%d:%02d:%02d", (int)(t / 3600), (int)(t / 60 % 60), (int)(t % 60));
	else snprintf(time, sizeof time, "%02d:%02d", (int)(t / 60 % 60), (int)(t % 60));
	for (f = label; f != NULL && *f != 0 && at + 1 < sizeof s->elapsed_text; f++) {
		if (f[0] == '%' && f[1] == 's') {
			size_t k;
			for (k = 0; time[k] != 0 && at + 1 < sizeof s->elapsed_text; k++) s->elapsed_text[at++] = time[k];
			f++;
		} else {
			s->elapsed_text[at++] = (char)*f;
		}
	}
	s->elapsed_text[at] = 0;
	if (id >= 0) ui->controls[id].text = (const uint8_t *)s->elapsed_text;
}

static void init_header(KpUi *ui, int32_t dialog)
{
	/* InitScoreHeader 0x462160 */
	KpUiScore *s = sc(ui);
	int32_t id;
	int result = s->data.result;
	(void)dialog;
	if ((id = control(ui, ID_TITLE)) >= 0) {
		uint32_t str = s->data.replay ? STR_REPLAY : result >= 0 && result < 5 ? TITLES[result] : 0;
		if (str != 0) {
			ui->controls[id].text = kp_ui_tbl(ui, KP_UI_GLUALL, str);
			ui->controls[id].flags |= FLAG_TITLE;
		}
	}
	if ((id = control(ui, ID_TOTAL_LABEL)) >= 0) ui->controls[id].flags |= FLAG_TITLE;
	if ((id = control(ui, ID_IMAGE)) >= 0 && load_pcx(ui, "pMain.pcx", &s->main) == 0) {
		set_image(&ui->controls[id], &s->main);
		kp_ui_mark_dirty(ui, id);
	}
	elapsed_time(ui);
}

static int score_proc(KpUi *ui, KpUiControl *d, KpUiEvent *e)
{
	/* ScoreDialogProc 0x4622f0 */
	KpUiScore *s = sc(ui);
	int32_t self = (int32_t)(d - ui->controls);
	KpUiInteract base = kp_ui_type_interact(0);
	if (e->type == KP_UI_USER) {
		switch (e->code) {
		case KP_UI_CREATE:
			init_header(ui, self);
			kp_ui_glue_activate(ui, self, s->theme);
			break;
		case KP_UI_DESTROY: /* FreeScoreScreen 0x462290 (Sleep(1000) before: the host's) */
			free_surface(&s->main);
			s->dialog = -1;
			break;
		case KP_UI_CLOSE:
			kp_ui_stop_sounds(ui);
			s->answered = 1;
			kp_ui_glue_slide_out(ui, self);
			return 1;
		case KP_UI_POST_LOAD: {
			KpUiInteract procs[PROCS];
			int k, r;
			memset(procs, 0, sizeof procs);
			procs[ID_IMAGE - 1] = kp_ui_type_interact(5);
			procs[ID_TITLE - 1] = kp_ui_type_interact(10);
			for (k = 0; k < TABS; k++) procs[ID_TAB_FIRST - 1 + k] = tab_proc;
			procs[ID_OK - 1] = kp_ui_text_button_proc;
			for (k = ID_TOTAL_LABEL; k < ID_ROW_FIRST; k++) procs[k - 1] = kp_ui_type_interact(10);
			for (r = 0; r < KP_UI_SCORE_SLOTS; r++) {
				int first = ID_ROW_FIRST + ROW_CONTROLS * r;
				procs[first - 1] = inset_proc;
				for (k = 1; k < ROW_CONTROLS - 1; k++) procs[first - 1 + k] = kp_ui_type_interact(10);
				procs[first - 1 + ROW_CONTROLS - 1] = total_proc;
			}
			procs[ID_SAVE_REPLAY - 1] = save_replay_proc;
			kp_ui_glue_slide_in(ui, self, SLIDE_TABLE, 1, SLIDE_DELAY);
			kp_ui_glue_init_dialog(ui, self, procs, PROCS, s->theme);
			break;
		}
		default: break;
		}
	}
	return base(ui, d, e);
}

int32_t kp_ui_score_open(KpUi *ui, const KpUiScoreData *data)
{
	/* RunScoreScreen 0x461940 */
	KpUiScore *s = sc(ui);
	char path[64];
	const KpUiFile *f;
	int32_t d;
	kp_ui_score_free(ui);
	memset(s, 0, sizeof *s);
	s->data = *data;
	s->dialog = -1;
	s->index = s->data.replay ? 1 : (s->data.race % 3) * 2 + (s->data.result == RESULT_VICTORY);
	s->theme = THEMES[s->index];
	kp_ui_music_play(ui, MUSIC[s->index]);
	snprintf(path, sizeof path, "%siScore.grp", DIRS[s->index]);
	s->icons = kp_ui_glue_file(ui, path);
	snprintf(path, sizeof path, "%stminimap.pcx", DIRS[s->index]);
	if ((f = kp_ui_glue_file(ui, path)) != NULL) {
		int32_t w, h;
		uint8_t pixels[256];
		/* the first 12 pixels (SBmpLoadImage into a 12-byte buffer: the first row's) */
		if (kp_ui_pcx(f->data, f->size, &w, &h, NULL, 0) == 0 && (size_t)w * (size_t)h <= sizeof pixels
			&& kp_ui_pcx(f->data, f->size, &w, &h, pixels, sizeof pixels) == 0) {
			memcpy(s->minimap, pixels, sizeof s->minimap);
		}
	}
	{
		uint16_t fill = SOUND_FILL;
		kp_ui_preload(ui, &fill, 1);
	}
	kp_ui_glue_sounds(ui);
	d = kp_ui_load_dialog(ui, "rez\\gluScore.bin");
	if (d < 0) return -1;
	s->dialog = d;
	kp_ui_init_dialog(ui, d, score_proc);
	return d;
}

int kp_ui_score_glue_state(const KpUi *ui) { return STATES[ui->score.index % DIRECTORIES]; }

void kp_ui_score_free(KpUi *ui)
{
	KpUiScore *s = sc(ui);
	free_surface(&s->main);
	free_surface(&s->inset);
	free_surface(&s->box);
	free_surface(&s->untab);
}

void kp_ui_score_data(KpUi *ui, struct KpGame *g, int result, const char *player_name, uint32_t elapsed,
	KpUiScoreData *out)
{
	/* CaptureScoreRoster 0x461e00 offline (the names of AssignDefaultComputerPlayerNames 0x4074a0; the forces' of the
	 * map, Use Map Settings) and the arrays the screen reads. TODO: the team games' force names and score
	 * indexes */
	int p, a;
	static const int PSTATS[KP_UI_SCORE_ARRAYS] = { -1, -1, -1, -1, 0x529124, 0x529154, 0x529184, 0x5291b4, 0x5291e4,
		0x529274, 0x5292a4, 0x5292d4, 0x529304, 0x529334 };
	memset(out, 0, sizeof *out);
	out->result = result == 3 ? 1 : result == 2 ? 2 : result == 5 ? 3 : 0; /* KP_EVENT_GAME_RESULT's a */
	out->race = (int)g->players.race[g->local_player];
	out->local_storm = 0; /* offline: the human is Storm player 0, the computers have none */
	/* GetElapsedSeconds 0x47a780: the clock's seconds less those UnpauseGame 0x47cc10 took off (0x47a620) */
	out->elapsed = elapsed > ui->paused_seconds ? elapsed - ui->paused_seconds : 0;
	for (p = 0; p < 8; p++) {
		KpUiRosterEntry *e = &out->roster[p];
		int64_t c = g->players.controller[p];
		out->player_colours[p] = (uint8_t)p;
		if (c == KP_CTRL_HUMAN) {
			snprintf(e->name, sizeof e->name, "%s", player_name != NULL ? player_name : "");
			e->type = TYPE_HUMAN;
		} else if (c == KP_CTRL_COMPUTER) {
			int base = g->expansion ? EXPANSION_NAMES : ORIGINAL_NAMES;
			int race = (int)g->players.race[p], offset = race == 0 ? 8 : race == 2 ? 16 : 0;
			const uint8_t *name = kp_ui_tbl(ui, KP_UI_NETWORK, (uint8_t)(base + offset + p));
			snprintf(e->name, sizeof e->name, "%s", name != NULL ? (const char *)name : "");
			if (e->name[0] == 0) continue; /* offline: a computer without a name is not listed */
			e->type = TYPE_COMPUTER;
		} else {
			continue;
		}
		e->race = (uint8_t)g->players.race[p];
		e->index = (uint32_t)p;
		e->storm = c == KP_CTRL_HUMAN ? 0 : NO_STORM_PLAYER;
		if (g->players.force[p] >= 1 && g->players.force[p] <= 4) {
			const char *force = kp_map_string(g, g->players.force_name[g->players.force[p] - 1]);
			if (force != NULL) snprintf(e->force, sizeof e->force, "%s", force);
		}
		out->count++;
	}
	for (p = 0; p < 12; p++) {
		out->arrays[KP_UI_SCORE_MINERALS][p] = (int32_t)g->players.minerals[p];
		out->arrays[KP_UI_SCORE_GAS][p] = (int32_t)g->players.gas[p];
		out->arrays[KP_UI_SCORE_CUMULATIVE_GAS][p] = (int32_t)g->players.cumulative_gas[p];
		out->arrays[KP_UI_SCORE_CUMULATIVE_MINERALS][p] = (int32_t)g->players.cumulative_minerals[p];
		for (a = KP_UI_SCORE_MEN_PRODUCED; a < KP_UI_SCORE_ARRAYS; a++) {
			out->arrays[a][p] = (int32_t)g->pstat[(PSTATS[a] - 0x5290c4) / 0x30][p];
		}
	}
	for (p = 0; p < 8; p++) out->colours[p] = ui->minimap.colour_table[p];
}
