/* The glue's screens (glues.cpp, atlas P19 0x457510..0x458980): a screen's dialog with its theme's background,
 * palette, colour bank, effects and dialog frames (InitGlueDialog 0x457f00, ReleaseGlueDialog 0x458380,
 * ActivateGlueScreen 0x4579b0, EndGlueScreen 0x457b50), the entry and exit slides (StartGlueSlideIn 0x458100 with
 * GlueSlideInStep 0x4587a0, SlideOutGlueDialog 0x457a40 with GlueSlideOutStep 0x458980) and the controls' sounds
 * (LoadGlueSounds 0x458740, GlueUiSoundProc 0x458490). The palette is the host's to upload (kp_ui_glue_palette);
 * the fades to and from black are its too. */
#include "kp_ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kp_rng.h"

#define CURSOR_TIMER 0x18
#define CURSOR_PERIOD 50
#define ERROR_TIMER 0x2e
#define ERROR_PERIOD 100
#define SLIDE_TIMER 7
#define SLIDE_PERIOD 20
#define SLIDE_TICK_MS 20
#define SLIDE_IN_TO 0           /* 0 from the left, 1 from the top, 2 from the right, 3 from the bottom */
#define SLIDE_FROM_TOP 1
#define SLIDE_FROM_RIGHT 2
#define SLIDE_FROM_BOTTOM 3
#define SOUND_CLICK 0xa5        /* the glue sounds 0xa5 + 0..8 */
#define SOUND_OVER 0xa6
#define SOUND_SLID_IN 0xab
#define SOUND_SLIDE_IN 0xac
#define SOUND_SLIDE_OUT 0xad
#define GLUE_SOUNDS 9
#define OVER_GAP_MS 200
#define SLIDE_DONE 0x405
#define DIALOG_GLUE 0x20000000u
#define DIALOG_NESTED 0x8000000u
#define DIALOG_BACKDROP 0x20u
#define SLIDING 0x20000u
#define THEME_DIRS 12
#define EFFECT_PERCENT 80      /* InitGlueDialog's single-colour effect */
#define EFFECT_INDEX 1         /* the theme records' +0x108 (1 in every record) */

/* The twelve themes' directories (the theme records 0x503e9c.., atlas P19) */
static const char *const THEMES[THEME_DIRS] = { "PalMm", "PalCs", "PalRt", "PalRz", "PalRp", "PalPd", "PalPv", "PalZd",
	"PalZv", "PalTd", "PalTv", "PalNl" };

const char *kp_ui_glue_theme_dir(int theme) { return theme >= 0 && theme < THEME_DIRS ? THEMES[theme] : NULL; }

static KpUiGlue *gl(KpUi *ui) { return &ui->glue; }

void kp_ui_glue_init(KpUi *ui)
{
	KpUiGlue *g = gl(ui);
	memset(g, 0, sizeof *g);
	g->theme = -1;
	g->background_theme = -1;
	g->dialog = -1;
	g->slide_sound = 1;
	kp_rng_crt_init(&g->crt, 1);
}

void kp_ui_glue_free(KpUi *ui)
{
	free(gl(ui)->background.pixels);
	gl(ui)->background.pixels = NULL;
}

/* GlueUiSoundProc 0x458490: the mouse-over sound at most every 200 ms, the click. */
static void ui_sound(KpUi *ui, int event)
{
	KpUiGlue *g = gl(ui);
	if (event == 0) {
		if (ui->now - g->last_over < OVER_GAP_MS) return;
		(void)kp_ui_play_sound(ui, SOUND_OVER);
		g->last_over = ui->now;
	} else if (event == 1) {
		(void)kp_ui_play_sound(ui, SOUND_CLICK);
	}
}

void kp_ui_glue_sounds(KpUi *ui)
{
	/* LoadGlueSounds 0x458740: once, the preload of 0xa5..0xad and the hook */
	uint16_t list[GLUE_SOUNDS];
	int k;
	if (!gl(ui)->sounds_loaded) {
		for (k = 0; k < GLUE_SOUNDS; k++) list[k] = (uint16_t)(SOUND_CLICK + k);
		kp_ui_preload(ui, list, GLUE_SOUNDS);
		gl(ui)->sounds_loaded = 1;
	}
	ui->ui_sound = ui_sound;
}

/* The theme's Backgnd.pcx: the pixels and the palette after them. */
static int load_background(KpUi *ui, int theme)
{
	KpUiGlue *g = gl(ui);
	char path[64];
	if (theme < 0 || theme >= THEME_DIRS) return -1;
	snprintf(path, sizeof path, "glue\\%s\\Backgnd.pcx", THEMES[theme]);
	g->background_theme = -1; /* the cached one goes either way */
	if (kp_ui_load_picture(ui, kp_ui_glue_file(ui, path), &g->background, g->background_palette) != 0) return -1;
	g->background_theme = theme;
	return 0;
}

void kp_ui_glue_init_dialog(KpUi *ui, int32_t dialog, const KpUiInteract *procs, int count, int theme)
{
	/* InitGlueDialog 0x457f00 */
	KpUiGlue *g = gl(ui);
	KpUiControl *d = &ui->controls[dialog];
	char path[64];
	uint8_t rgb[768];
	int k;
	kp_ui_flc_start_timers(ui, dialog);
	d->flags |= DIALOG_GLUE;
	if (procs != NULL) kp_ui_bind_interacts(ui, dialog, procs, count);
	if (g->nested++ != 0) {
		if (g->dialog >= 0) (void)kp_ui_send_user(ui, g->dialog, KP_UI_FOCUS_LOST, 0);
		d->flags |= DIALOG_NESTED;
		return;
	}
	if (g->background_theme != theme) (void)load_background(ui, theme);
	if (g->background.pixels != NULL) { /* the dialog's image is the cached background (not the dialog's to free) */
		kp_ui_drop_image(ui, d);
		d->image = g->background.pixels;
		d->image_w = (uint16_t)g->background.w;
		d->image_h = (uint16_t)g->background.h;
		d->owns_image = 0;
	}
	memcpy(g->palette, g->background_palette, sizeof g->palette);
	d->flags |= DIALOG_BACKDROP;
	for (k = 0; k < 256; k++) {
		rgb[3 * k] = g->palette[4 * k];
		rgb[3 * k + 1] = g->palette[4 * k + 1];
		rgb[3 * k + 2] = g->palette[4 * k + 2];
	}
	kp_ui_resolve_colours(ui, rgb, NULL);
	(void)kp_ui_effects_palette(ui, g->palette);
	{ /* SetDialogEffectColors 0x4c4d50 (theme +0x10a) and SetSingleColourEffect 0x4ca820(80 %, tEffect[1], A) */
		static const uint8_t PERCENTS[THEME_DIRS][3] = { { 50, 75, 75 }, { 50, 75, 75 }, { 50, 75, 75 }, { 50, 75, 75 },
			{ 50, 75, 75 }, { 50, 75, 75 }, { 50, 75, 75 }, { 50, 75, 75 }, { 50, 75, 75 }, { 50, 75, 75 }, { 50, 75, 75 },
			{ 75, 75, 75 } };
		ui->effects.image_percent = PERCENTS[theme][0];
		ui->effects.combo_percent = PERCENTS[theme][1];
		ui->effects.dialog_percent = PERCENTS[theme][2];
		ui->effects.level[0] = kp_ui_effect_level(EFFECT_PERCENT);
		if (ui->theme_effects != NULL && ui->theme_effect_size > EFFECT_INDEX) {
			ui->effects.colour[0] = ui->theme_effects[ui->theme_effect_size * (size_t)theme + EFFECT_INDEX];
		}
	}
	kp_ui_font_set(ui, 0);
	kp_ui_text_set_rows(&ui->text, ui->theme_fonts + 48 * (size_t)theme, 48);
	snprintf(path, sizeof path, "glue\\%s\\Dlg.grp", THEMES[theme]);
	(void)kp_ui_apply_theme(ui, path);
	kp_ui_cursor_set_theme(ui, theme);
	(void)kp_ui_set_timer(&ui->timers, dialog, CURSOR_TIMER, CURSOR_PERIOD, kp_ui_callback(ui, kp_ui_glue_cursor_tick),
		ui->now);
	g->dialog = dialog;
	g->dialog_theme = theme;
}

void kp_ui_glue_cursor_tick(KpUi *ui, int32_t owner)
{
	/* the glue's 50 ms timer 0x4574f0: the cursor's animation */
	(void)owner;
	kp_ui_cursor_animate(ui);
}

void kp_ui_glue_release_dialog(KpUi *ui, int32_t dialog)
{
	/* ReleaseGlueDialog 0x458380 */
	KpUiGlue *g = gl(ui);
	kp_ui_flc_stop_timers(ui, dialog);
	if (g->nested-- == 1) {
		kp_ui_delete_timer(&ui->timers, dialog, CURSOR_TIMER);
		g->dialog = -1;
	}
}

static void error_tick(KpUi *ui, int32_t owner)
{
	/* 0x458790: the deferred network error (none offline) */
	kp_ui_delete_timer(&ui->timers, owner, ERROR_TIMER);
}

void kp_ui_glue_activate(KpUi *ui, int32_t dialog, int theme)
{
	/* ActivateGlueScreen 0x4579b0 */
	KpUiGlue *g = gl(ui);
	ui->cursor_hidden = 0;
	if (theme != g->theme) {
		g->palette_serial++; /* the palette upload and the fade from black are the host's */
		kp_ui_mark_dirty(ui, dialog);
		g->theme = theme;
	}
	(void)kp_ui_set_timer(&ui->timers, dialog, ERROR_TIMER, ERROR_PERIOD, kp_ui_callback(ui, error_tick), ui->now);
}

void kp_ui_glue_end(KpUi *ui, int next_theme)
{
	/* EndGlueScreen 0x457b50 */
	KpUiGlue *g = gl(ui);
	g->hidden_count = 0;
	if (next_theme != g->theme) {
		kp_ui_surface_free(ui, g->background.pixels);
		g->background.pixels = NULL;
		g->background_theme = -1;
		ui->cursor_hidden = 1; /* the fade to black is the host's */
	}
	g->nested = 0;
	g->desc_count = 0;
}

/* A slide's step of s pixels in the direction (sign 1: the entry's, -1: the exit's): the leading edge moves first and
 * the union of the old and the new rectangle is invalidated, then the trailing edge follows (0x4587a0, 0x458980). */
static void move(KpUi *ui, int32_t ci, int dir, int32_t s, int sign)
{
	KpUiControl *c = &ui->controls[ci];
	int32_t dx = 0, dy = 0;
	switch (dir) {
	case SLIDE_IN_TO: dx = s; break;
	case SLIDE_FROM_TOP: dy = s; break;
	case SLIDE_FROM_RIGHT: dx = -s; break;
	default: dy = -s; break;
	}
	dx *= sign;
	dy *= sign;
	if (dx > 0) c->right = (int16_t)(c->right + dx);
	else c->left = (int16_t)(c->left + dx);
	if (dy > 0) c->bottom = (int16_t)(c->bottom + dy);
	else c->top = (int16_t)(c->top + dy);
	kp_ui_mark_dirty(ui, ci);
	if (dx > 0) c->left = (int16_t)(c->left + dx);
	else c->right = (int16_t)(c->right + dx);
	if (dy > 0) c->top = (int16_t)(c->top + dy);
	else c->bottom = (int16_t)(c->bottom + dy);
}

static void slide_in_step(KpUi *ui, int32_t owner)
{
	/* GlueSlideInStep 0x4587a0 */
	KpUiGlue *g = gl(ui);
	int k, done = 1;
	if (g->slide_delay > 0) {
		g->slide_delay--;
		return;
	}
	if (g->slide_sound) {
		(void)kp_ui_play_sound(ui, SOUND_SLIDE_IN);
		g->slide_sound = 0;
	}
	for (k = 0; k < g->desc_count; k++) {
		KpUiControl *c;
		uint16_t r, s;
		if (!g->slide_active[k] || g->slide_control[k] < 0) continue;
		c = &ui->controls[g->slide_control[k]];
		r = c->wuser;
		if (r != 0) { /* a control already in place is invalidated still */
			done = 0;
			s = (uint16_t)(r < 4 ? r : r >> 2);
			c->wuser = (uint16_t)(r - s);
			move(ui, g->slide_control[k], g->desc[k].dir, s, 1);
		}
		kp_ui_mark_dirty(ui, g->slide_control[k]);
	}
	if (!done) return;
	(void)kp_ui_play_sound(ui, SOUND_SLID_IN);
	for (k = 0; k < g->hidden_count; k++) {
		(void)kp_ui_send_user(ui, g->hidden[k], SLIDE_DONE, 0);
		kp_ui_show_control(ui, g->hidden[k]);
	}
	g->hidden_count = 0;
	for (k = 0; k < g->desc_count; k++) g->slide_active[k] = 0;
	kp_ui_delete_timer(&ui->timers, owner, SLIDE_TIMER);
	g->slide_sound = 1;
	(void)kp_ui_send_user(ui, owner, SLIDE_DONE, 0);
}

void kp_ui_glue_slide_in(KpUi *ui, int32_t dialog, const uint16_t *table, int count, uint16_t delay)
{
	/* StartGlueSlideIn 0x458100: table = {ID, direction} pairs */
	KpUiGlue *g = gl(ui);
	int k, best = -1;
	int32_t c, last = -1;
	g->desc_count = count < KP_UI_SLIDES ? count : KP_UI_SLIDES;
	g->closing = 0;
	g->slide_delay = (int)(delay / SLIDE_TICK_MS);
	g->hidden_count = 0;
	for (k = 0; k < g->desc_count; k++) {
		KpUiControl *x;
		int32_t d;
		g->desc[k].id = (int16_t)table[2 * k];
		g->desc[k].dir = (uint8_t)table[2 * k + 1];
		g->slide_control[k] = kp_ui_find_control(ui, dialog, g->desc[k].id);
		g->slide_active[k] = 0;
		if (g->slide_control[k] < 0) continue;
		x = &ui->controls[g->slide_control[k]];
		x->flags |= SLIDING;
		switch (g->desc[k].dir) {
		case SLIDE_IN_TO: d = x->right + 1; x->left = (int16_t)(x->left - d); x->right = -1; break;
		case SLIDE_FROM_TOP: d = x->bottom + 1; x->top = (int16_t)(x->top - d); x->bottom = -1; break;
		case SLIDE_FROM_RIGHT: d = KP_UI_SCREEN_W - x->left; x->left = KP_UI_SCREEN_W; x->right = (int16_t)(x->right + d); break;
		default: d = KP_UI_SCREEN_H - x->top; x->top = KP_UI_SCREEN_H; x->bottom = (int16_t)(x->bottom + d); break;
		}
		g->slide_distance[k] = (uint16_t)d;
		x->wuser = (uint16_t)d;
		g->slide_active[k] = 1;
		if (best < 0 || g->desc[k].id > g->desc[best].id) best = k;
	}
	if (best >= 0) last = g->slide_control[best];
	g->hidden_from = last >= 0 ? ui->controls[last].next : -1;
	(void)kp_ui_set_timer(&ui->timers, dialog, SLIDE_TIMER, SLIDE_PERIOD, kp_ui_callback(ui, slide_in_step), ui->now);
	for (c = g->hidden_from; c >= 0 && last >= 0; c = ui->controls[c].next) {
		if (!(ui->controls[c].flags & KP_UI_VISIBLE) || g->hidden_count >= KP_UI_CONTROLS_HIDDEN) continue;
		g->hidden[g->hidden_count++] = c;
		kp_ui_hide_control(ui, c);
	}
}

static void slide_out_step(KpUi *ui, int32_t owner)
{
	/* GlueSlideOutStep 0x458980 */
	KpUiGlue *g = gl(ui);
	int k, done = 1;
	for (k = 0; k < g->desc_count; k++) {
		KpUiControl *c;
		uint16_t s;
		if (!g->slide_active[k] || g->slide_control[k] < 0) continue;
		c = &ui->controls[g->slide_control[k]];
		s = c->wuser;
		switch (g->desc[k].dir) { /* still on the screen before the step */
		case SLIDE_IN_TO: if (c->right > 0) done = 0; break;
		case SLIDE_FROM_TOP: if (c->bottom > 0) done = 0; break;
		case SLIDE_FROM_RIGHT: if (c->left < KP_UI_SCREEN_W) done = 0; break;
		default: if (c->top < KP_UI_SCREEN_H) done = 0; break;
		}
		move(ui, g->slide_control[k], g->desc[k].dir, s, -1);
		c->wuser = (uint16_t)(2 * s);
	}
	if (!done) return;
	kp_ui_delete_timer(&ui->timers, owner, SLIDE_TIMER);
	if (g->closing) { /* the modal loop ends: the dialog closes and is destroyed (code 1) */
		kp_ui_glue_release_dialog(ui, owner);
		kp_ui_close_dialog(ui, owner);
		kp_ui_destroy_dialog(ui, owner);
	}
}

void kp_ui_glue_slide_out(KpUi *ui, int32_t dialog)
{
	/* SlideOutGlueDialog 0x457a40 (the result is already in ui->result) */
	KpUiGlue *g = gl(ui);
	int k, any = 0;
	int32_t c;
	ui->cursor_hidden = 1;
	g->closing = 1;
	g->hidden_count = 0;
	for (k = 0; k < g->desc_count; k++) {
		if (g->slide_control[k] < 0) continue;
		ui->controls[g->slide_control[k]].wuser = 1;
		g->slide_active[k] = 1;
		any = 1;
	}
	if (any) (void)kp_ui_play_sound(ui, SOUND_SLIDE_OUT);
	(void)kp_ui_set_timer(&ui->timers, dialog, SLIDE_TIMER, SLIDE_PERIOD, kp_ui_callback(ui, slide_out_step), ui->now);
	for (c = g->hidden_from; c >= 0; c = ui->controls[c].next) kp_ui_hide_control(ui, c);
	/* ForEachControl 0x4c18a0 from the dialog itself (a group's children in place of its siblings) with
	 * ClearControlFlag10 0x457500: nothing on the screen takes events while it slides out */
	for (c = dialog; c >= 0; c = ui->controls[c].type == 0 ? ui->controls[c].child : ui->controls[c].next) {
		ui->controls[c].flags &= ~KP_UI_EVENTS;
	}
}

const uint8_t *kp_ui_glue_palette(KpUi *ui, uint32_t *serial)
{
	if (serial != NULL) *serial = gl(ui)->palette_serial;
	return gl(ui)->palette;
}

uint32_t kp_ui_glue_rand(KpUi *ui)
{
	KpUiGlue *g = &ui->glue;
	if (ui->video_trace && g->rand_count < KP_UI_RAND_LOG) g->rand_log[g->rand_count++] = g->crt.holdrand;
	return kp_rng_crt_rand(&g->crt);
}
