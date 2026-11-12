/* The title screen (title.cpp, atlas P39/P40 0x4b5300..0x4b55d0): RunMainLoop 0x4679b0 opens rez\titledlg.bin
 * (CreateTitleScreen 0x4b5570) after the first start's cinematics, loads the images (0x40fef0) under it, and RunGlueLoop
 * 0x457510 sleeps 1500 ms before it destroys the title (DestroyTitleDialog 0x4b55b0) and opens the main menu. The
 * dialog: glue\title\title.pcx as its image with its palette, three copyright lines and the loading text (child -10;
 * -11 in the low-memory mode 0x66fd30, which kp_ui does not have) that blinks while the start loads. The fades in and
 * out (0x4cce50, 0x4cceb0 at speed 3) are the host's palette changes: the original's fade 0x4ccd60 shows the old
 * palette for 10 ms, then its remaining steps without waiting. */
#include "kp_ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEXT_ID (-10)
#define BLINK_TIMER 1
#define BLINK_PERIOD 500
#define TEXT_FONT_SLOT 2   /* 0x6d302c */
#define TEXT_FORMAT 0x22   /* 0x6d300e: centred both ways in the bitmap */
#define TEXT_COLOUR 2      /* SelectTextColor 0x4ce250's row */
#define PCX_PALETTE 769    /* 0x0c and 256 RGB entries at the file's end */

static KpUiTitle *ti(KpUi *ui) { return &ui->title; }

/* DrawTitleBackground 0x4b5550: the control's image opaque at its (left, top) (0x4cf4f0). */
static void draw_image(KpUi *ui, KpUiControl *c, int32_t x, int32_t y, const int16_t clip[4])
{
	int16_t r[4];
	(void)x;
	(void)y;
	(void)clip;
	if (c->image == NULL) return;
	r[0] = 0;
	r[1] = 0;
	r[2] = (int16_t)(c->image_w - 1);
	r[3] = (int16_t)(c->image_h - 1);
	kp_ui_blit_opaque(ui, c->left, c->top, c->image, c->image_w, c->image_h, r);
}

static void blink(KpUi *ui, int32_t owner)
{
	/* TitleBlinkTimer 0x4b55d0: only while the application is active, the text's image and the plain one by turns */
	KpUiTitle *t = ti(ui);
	KpUiControl *c = &ui->controls[owner];
	const KpUiSurface *s;
	if (ui->inactive) return;
	t->toggle = t->toggle == 0;
	s = t->toggle ? &t->shown : &t->blank;
	c->image = s->pixels;
	c->image_w = (uint16_t)s->w;
	c->image_h = (uint16_t)s->h;
	c->owns_image = 0;
	c->draw = draw_image;
	kp_ui_mark_dirty(ui, owner);
}

/* Surface s as w x h of the dialog's image under the rectangle from (left, top). */
static int copy_under(KpUi *ui, const KpUiControl *d, KpUiSurface *s, int32_t left, int32_t top, int32_t w, int32_t h)
{
	KpUiSurface *saved = ui->text.target;
	int16_t src[4];
	s->pixels = (uint8_t *)calloc((size_t)w * (size_t)h, 1);
	if (s->pixels == NULL) return -1;
	s->w = w;
	s->h = h;
	if (d->image == NULL) return 0;
	src[0] = (int16_t)left;
	src[1] = (int16_t)top;
	src[2] = (int16_t)(left + w - 1);
	src[3] = (int16_t)(top + h - 1);
	ui->text.target = s;
	kp_ui_blit_opaque(ui, 0, 0, d->image, d->image_w, d->image_h, src); /* SBltROP3 SRCCOPY */
	ui->text.target = saved;
	return 0;
}

static void loading_text(KpUi *ui, int32_t dialog)
{
	/* InitTitleLoadingText 0x4b5300 */
	KpUiTitle *t = ti(ui);
	const KpUiControl *d = &ui->controls[dialog];
	KpUiSurface *saved = ui->text.target;
	int32_t k = kp_ui_find_control(ui, dialog, TEXT_ID), w, h;
	KpUiControl *c;
	if (k < 0) return;
	c = &ui->controls[k];
	w = c->right - c->left + 1;
	h = c->bottom - c->top + 1;
	if (w <= 0 || h <= 0 || copy_under(ui, d, &t->blank, c->left, c->top, w, h) != 0
		|| copy_under(ui, d, &t->shown, c->left, c->top, w, h) != 0) {
		return;
	}
	ui->text.target = &t->shown;
	(void)kp_ui_text_select_font(&ui->text, ui->fonts[TEXT_FONT_SLOT], ui->font_sizes[TEXT_FONT_SLOT]);
	ui->text.flags = TEXT_FORMAT;
	kp_ui_select_color(&ui->text, TEXT_COLOUR);
	if (c->text != NULL) (void)kp_ui_draw_text_told(ui, c, 0, 0, c->text);
	ui->text.font = NULL; /* SelectFont(0) */
	ui->text.target = saved;
	kp_ui_show_control(ui, k);
	t->text = k;
	(void)kp_ui_set_timer(&ui->timers, k, BLINK_TIMER, BLINK_PERIOD, kp_ui_callback(ui, blink), ui->now);
}

/* The dialog's image and the glue's palette from title.pcx: 0 or -1. */
static int load_background(KpUi *ui, KpUiControl *d)
{
	KpUiTitle *t = ti(ui);
	const KpUiFile *f = kp_ui_glue_file(ui, "glue\\title\\title.pcx");
	int32_t w, h;
	size_t k;
	if (f == NULL || f->size < 128 + PCX_PALETTE || kp_ui_pcx(f->data, f->size, &w, &h, NULL, 0) != 0) return -1;
	t->background.pixels = (uint8_t *)malloc((size_t)w * (size_t)h);
	if (t->background.pixels == NULL
		|| kp_ui_pcx(f->data, f->size, &w, &h, t->background.pixels, (size_t)w * (size_t)h) != 0) {
		return -1;
	}
	t->background.w = w;
	t->background.h = h;
	kp_ui_picture(ui, t->background.pixels, w, h, f); /* the HD interface's (ADR 0013 R4d) */
	kp_ui_drop_image(ui, d);
	d->image = t->background.pixels;
	d->image_w = (uint16_t)w;
	d->image_h = (uint16_t)h;
	d->owns_image = 0;
	for (k = 0; k < 256; k++) {
		const uint8_t *p = f->data + f->size - 768 + 3 * k;
		ui->glue.palette[4 * k] = p[0];
		ui->glue.palette[4 * k + 1] = p[1];
		ui->glue.palette[4 * k + 2] = p[2];
		ui->glue.palette[4 * k + 3] = 0;
	}
	return 0;
}

static void init(KpUi *ui, int32_t dialog)
{
	/* InitTitleScreen 0x4b5450 */
	if (load_background(ui, &ui->controls[dialog]) != 0) return;
	kp_ui_text_set_rows(&ui->text, ui->title_rows, ui->title_rows_size); /* LoadFontColorTable 0x47ca80 */
	kp_ui_music_play(ui, KP_UI_TITLE_MUSIC);                            /* not in the low-memory mode */
	kp_ui_mark_dirty(ui, dialog);
	ui->cursor_hidden = 1;     /* HideCursorLayer 0x43e590 */
	ui->glue.palette_serial++; /* 0x4cd040's 256 entries, the fade in 0x4cce50 */
	loading_text(ui, dialog);
}

static void destroy(KpUi *ui)
{
	/* DestroyTitleScreen 0x4b54c0 (the fade out 0x4cceb0 is the host's) */
	KpUiTitle *t = ti(ui);
	if (t->text >= 0) kp_ui_delete_timer(&ui->timers, t->text, BLINK_TIMER);
	t->text = -1;
	kp_ui_surface_free(ui, t->shown.pixels);
	kp_ui_surface_free(ui, t->blank.pixels);
	t->shown.pixels = t->blank.pixels = NULL;
}

/* destroy() and the picture: what the title owns. */
static void release(KpUi *ui)
{
	KpUiTitle *t = ti(ui);
	destroy(ui);
	kp_ui_surface_free(ui, t->background.pixels);
	t->background.pixels = NULL;
}

static int title_proc(KpUi *ui, KpUiControl *d, KpUiEvent *e)
{
	/* TitleDialogProc 0x4b5500 */
	if (e->type == KP_UI_USER) {
		if (e->code == KP_UI_CREATE) init(ui, (int32_t)(d - ui->controls));
		else if (e->code == KP_UI_DESTROY) destroy(ui);
	}
	return kp_ui_type_interact(0)(ui, d, e);
}

int32_t kp_ui_title_open(KpUi *ui)
{
	/* CreateTitleScreen 0x4b5570 (RunMainLoop 0x4679b0's font set: the glue's) */
	KpUiTitle *t = ti(ui);
	int32_t d;
	kp_ui_title_close(ui);
	if (kp_ui_glue_file(ui, "glue\\title\\title.pcx") == NULL) {
		snprintf(ui->error, sizeof ui->error, "ui: no glue\\title\\title.pcx");
		return -1;
	}
	kp_ui_font_set(ui, 0);
	t->toggle = 0;
	d = kp_ui_load_dialog(ui, "rez\\titledlg.bin");
	if (d < 0) return -1;
	t->dialog = d;
	kp_ui_init_dialog(ui, d, title_proc);
	return d;
}

void kp_ui_title_close(KpUi *ui)
{
	/* DestroyTitleDialog 0x4b55b0, then RunGlueLoop 0x457510's theme -1 (the next screen's palette comes with it) */
	KpUiTitle *t = ti(ui);
	if (t->dialog >= 0) {
		if (ui->controls[t->dialog].used && ui->controls[t->dialog].interact == title_proc) {
			kp_ui_destroy_dialog(ui, t->dialog);
		}
		ui->glue.theme = -1;
	}
	t->dialog = -1;
	release(ui);
}

int kp_ui_title_is_open(const KpUi *ui) { return ui->title.dialog >= 0; }

void kp_ui_title_free(KpUi *ui)
{
	/* kp_ui_free has freed the controls by now: the dialog is not destroyed again */
	ui->title.dialog = -1;
	release(ui);
}
