/* The flic buttons of the glue (type 14; flc.cpp, atlas P43 0x4c8150..0x4c8760, P44 0x4c8860..0x4c8c00): the SMK
 * overlays of a button (OpenFlicClip 0x4c8860 into private buffers, AdvanceFlicButton 0x4c89b0 every 30 ms on timer
 * 0x48, the hover fades UpdateFlcButtonHover 0x4c8760), the stock procedure FlcButtonProc 0x4c82f0 and the text
 * button's 0x4c83b0 (ButtonUserEvent 0x4c8c00), the press tracking 0x4c8a40 and ReleaseButton 0x4c8bc0, the draws
 * DrawFlcButton 0x4c8150, DrawFlcOverlay 0x4c8520, DrawFlcButtonText 0x4c8650 and DrawTextButton 0x4c8730, and
 * FlcAnimationProc 0x4c82b0 (the Zerg ready room's animation). */
#include "kp_ui.h"

#include <stdlib.h>
#include <string.h>

#define SMK_RECORD 28
#define CLIP_FADES 0x01u       /* the overlay fades with the hover */
#define CLIP_DIMS 0x02u        /* dims to 30 when not hovered */
#define CLIP_LOOPS 0x04u       /* SVid's loop flag 0x40000 */
#define CLIP_HOVER_ONLY 0x08u  /* drawn only while hovered */
#define CLIP_KEYED 0x10u       /* drawn with colour 0 transparent */
#define DIM_LEVEL 30
#define FULL_LEVEL 100
#define FADE_STEP 15
#define FLC_TIMER 0x48
#define FLC_PERIOD 30
#define RESPONSIVE 0x10u       /* ButtonHitTest's answer: the flag bit 0x10 */
#define SOUND_OVER 0           /* the UI sound hook 0x4c3770: 0 mouse over, 1 click */
#define SOUND_CLICK 1
#define TEXT_ROW 1             /* DrawFlcButton's glowing text row */
#define BASE_ROW 2
#define DISABLED_COLOUR 5
#define CENTRE_X 320
#define PAN_RANGE 1000

static int32_t self_of(KpUi *ui, KpUiControl *c) { return (int32_t)(c - ui->controls); }

static int16_t *response(KpUiControl *c) { return (int16_t *)c->words36; }

static int32_t alloc_clip(KpUi *ui)
{
	int32_t k;
	for (k = 0; k < KP_UI_FLC_CLIPS; k++) {
		if (!ui->flc[k].used) {
			memset(&ui->flc[k], 0, sizeof ui->flc[k]);
			ui->flc[k].used = 1;
			ui->flc[k].next = -1;
			ui->flc[k].order = -1;
			return k;
		}
	}
	return -1;
}

static void free_clip(KpUi *ui, int32_t k)
{
	KpUiFlcClip *f = &ui->flc[k];
	kp_ui_video_end(&f->video);
	free(f->pixels);
	f->pixels = NULL;
	f->used = 0;
}

/* OpenFlicClip 0x4c8860: the clip's size (the BIN's, else the movie's), its own cleared buffer, the first frame. */
static void open_clip(KpUi *ui, KpUiFlcClip *f)
{
	const KpUiFile *file = kp_ui_glue_file(ui, f->name);
	if (file == NULL) return; /* the original stops with "Could not load" */
	if (f->w == 0 || f->h == 0) { /* SVidPlayBegin, SVidGetSize, SVidPlayEnd */
		if (kp_ui_video_begin(ui, &f->video, file, NULL, 0, 0) != 0) return;
		f->w = (uint16_t)f->video.smk.width;
		f->h = (uint16_t)f->video.smk.height;
		kp_ui_video_end(&f->video);
	}
	f->pixels = (uint8_t *)calloc((size_t)f->w * f->h != 0 ? (size_t)f->w * f->h : 1, 1);
	/* a BIN size that is not the movie's leaves the clip closed (drawn as nothing) */
	if (f->pixels == NULL || kp_ui_video_begin(ui, &f->video, file, f->pixels, f->w, f->h) != 0) return;
	f->video.loop = (f->flags & CLIP_LOOPS) != 0;
	(void)kp_ui_video_continue(&f->video, 1, NULL, ui->now);
}

/* The control's SMK nodes of its BIN record (ui.kpd DLGS: 28-byte records after the widgets). */
static void load_clips(KpUi *ui, KpUiControl *c)
{
	const KpUiBin *b = &ui->bins[c->bin];
	const uint8_t *w = b->widgets + (size_t)c->record * KP_UI_WIDGET_SIZE;
	uint32_t first = kp_le_u16(w + 28), count = kp_le_u16(w + 30), n;
	int32_t *link = &c->clips;
	if (first == 0xffff) return;
	for (n = 0; n < count && first + n < (uint32_t)b->smk_count; n++) {
		const uint8_t *r = b->smks + (size_t)(first + n) * SMK_RECORD;
		uint32_t name = kp_le_u32(r + 2), extra = kp_le_u32(r + 12), size = kp_le_u32(r + 20);
		int32_t k = alloc_clip(ui);
		KpUiFlcClip *f;
		if (k < 0) return;
		f = &ui->flc[k];
		f->flags = kp_le_u16(r) | (extra & 0xffff) << 16;
		f->level = (uint16_t)(extra >> 16);
		f->name = name < ui->pool_size ? (const char *)ui->pool + name : "";
		f->x = (uint16_t)kp_le_u16(r + 8);
		f->y = (uint16_t)kp_le_u16(r + 10);
		f->w = (uint16_t)(size & 0xffff);
		f->h = (uint16_t)(size >> 16);
		*link = k;
		link = &f->next;
	}
}

static void close_clips(KpUi *ui, KpUiControl *c)
{
	/* CloseFlcButtonVideos 0x4c8470 (the response rectangle back to control-relative) */
	int16_t *r = response(c);
	int32_t k = c->clips, next;
	for (; k >= 0; k = next) {
		next = ui->flc[k].next;
		free_clip(ui, k);
	}
	c->clips = -1;
	r[0] = (int16_t)(r[0] - c->left);
	r[1] = (int16_t)(r[1] - c->top);
	r[2] = (int16_t)(r[2] - c->left);
	r[3] = (int16_t)(r[3] - c->top);
}

static int hovered_on_screen(KpUi *ui, KpUiControl *c)
{
	int16_t *r = response(c);
	return (c->flags & KP_UI_HOVER) && ui->mouse_x >= r[0] && ui->mouse_x <= r[2] && ui->mouse_y >= r[1]
		&& ui->mouse_y <= r[3];
}

static void draw_overlay(KpUi *ui, KpUiControl *c, KpUiFlcClip *f, int32_t x, int32_t y, const int16_t clip[4])
{
	/* DrawFlcOverlay 0x4c8520 */
	KpUiSurface *t = ui->text.target;
	int32_t l, tp, r, b, sx, sy, sr, sb, mode;
	if (!f->video.open || f->pixels == NULL || t == NULL) return;
	l = clip[0] > c->left + f->x ? clip[0] : c->left + f->x;
	tp = clip[1] > c->top + f->y ? clip[1] : c->top + f->y;
	r = clip[2] + 1 < l + f->w ? clip[2] + 1 : l + f->w;
	b = clip[3] + 1 < tp + f->h ? clip[3] + 1 : tp + f->h;
	sx = x - f->x > 0 ? x - f->x : 0;
	sy = y - f->y > 0 ? y - f->y : 0;
	sr = f->w < r - l + sx + 1 ? f->w : r - l + sx + 1;
	sb = f->h < b - tp + sy + 1 ? f->h : b - tp + sy + 1;
	if (sb <= sy || sr <= sx) return;
	if (f->flags & (CLIP_FADES | CLIP_DIMS)) mode = KP_UI_BLIT_OPACITY;
	else mode = (f->flags & CLIP_KEYED) ? KP_UI_BLIT_KEYED : KP_UI_BLIT_COPY;
	{
		/* BlitWithEffect 0x4ca110: the smaller of the source and destination rectangles */
		int32_t w = sr - sx < r - l ? sr - sx : r - l, h = sb - sy < b - tp ? sb - sy : b - tp;
		if (l < 0 || tp < 0 || w <= 0 || h <= 0) return;
		if (l + w > t->w) w = t->w - l;
		if (tp + h > t->h) h = t->h - tp;
		if (w <= 0 || h <= 0) return;
		kp_ui_blit_effect(ui, t->pixels + (size_t)tp * (size_t)t->w + (size_t)l, t->w,
			f->pixels + (size_t)sy * f->w + (size_t)sx, f->w, w, h, mode, f->level & 0xff);
	}
}

/* DrawFlcButtonText 0x4c8650 with the format byte in the text state. */
static void draw_text(KpUi *ui, KpUiControl *c, uint32_t glow)
{
	KpUiText *t = &ui->text;
	const uint8_t *text = c->text;
	uint8_t format = t->flags;
	if (text == NULL) return;
	kp_ui_select_control_font(ui, c);
	t->flags = format;
	if (c->flags & KP_UI_NO_SPACING) t->spacing = 0;
	if ((c->flags & KP_UI_HOTKEY_PREFIX) && text[0] != 0) text++;
	t->clip_left = (int16_t)c->extra[0];
	t->clip_top = (int16_t)c->extra[1];
	t->clip_right = (int16_t)c->extra[2];
	t->clip_bottom = (int16_t)c->extra[3];
	if (c->flags & KP_UI_DISABLED) {
		kp_ui_select_color(t, DISABLED_COLOUR);
		(void)kp_ui_draw_text(t, (int16_t)c->extra[0], (int16_t)c->extra[1], text);
	} else {
		int saved = t->row;
		kp_ui_text_opacity_row(ui, t->rows[KP_UI_EFFECT_ROW], t->rows[TEXT_ROW], t->rows[BASE_ROW], glow);
		t->row = KP_UI_EFFECT_ROW; /* DrawTextWithOpacity 0x4ca290: 0x6d3010 = 0x6d06c8 while drawing */
		(void)kp_ui_draw_text(t, (int16_t)c->extra[0], (int16_t)c->extra[1], text);
		t->row = saved;
	}
	(void)kp_ui_text_select_font(t, NULL, 0);
}

static void draw_button(KpUi *ui, KpUiControl *c, int32_t x, int32_t y, const int16_t clip[4])
{
	/* DrawFlcButton 0x4c8150 */
	int hover = hovered_on_screen(ui, c);
	int32_t k;
	for (k = c->clips; k >= 0; k = ui->flc[k].next) {
		if (hover || !(ui->flc[k].flags & CLIP_HOVER_ONLY)) draw_overlay(ui, c, &ui->flc[k], x, y, clip);
	}
	if (c->text != NULL) {
		ui->text.flags = c->text_format;
		draw_text(ui, c, (uint32_t)c->glow);
	}
}

static void draw_text_button(KpUi *ui, KpUiControl *c, int32_t x, int32_t y, const int16_t clip[4])
{
	/* DrawTextButton 0x4c8730 */
	(void)x;
	(void)y;
	(void)clip;
	kp_ui_update_focus(ui, c);
	if (c->text != NULL) {
		ui->text.flags = c->text_format;
		draw_text(ui, c, (uint32_t)c->glow);
	}
}

static void absolute_response(KpUiControl *c)
{
	int16_t *r = response(c);
	r[0] = (int16_t)(r[0] + c->left);
	r[1] = (int16_t)(r[1] + c->top);
	r[2] = (int16_t)(r[2] + c->left);
	r[3] = (int16_t)(r[3] + c->top);
}

static void init_button(KpUi *ui, KpUiControl *c)
{
	/* InitFlcButton 0x4c8200 */
	int32_t k;
	int first = 1;
	absolute_response(c);
	c->pan = (int16_t)((c->left + (c->right - c->left) / 2 - CENTRE_X) * PAN_RANGE / CENTRE_X);
	load_clips(ui, c);
	for (k = c->clips; k >= 0; k = ui->flc[k].next) {
		KpUiFlcClip *f = &ui->flc[k];
		if (first) {
			if (c->flags & KP_UI_TRANSPARENT) f->flags |= CLIP_KEYED;
		} else {
			f->flags |= CLIP_KEYED;
			if (f->flags & CLIP_DIMS) f->level = DIM_LEVEL;
		}
		first = 0;
		if (f->name[0] != 0) open_clip(ui, f);
	}
	c->text_format = (uint8_t)(c->flags >> 20);
	c->draw = draw_button;
	kp_ui_mark_dirty(ui, self_of(ui, c));
}

static void hover_fade(KpUi *ui, KpUiControl *c)
{
	/* UpdateFlcButtonHover 0x4c8760 */
	KpUiControl *d = &ui->controls[c->parent];
	int16_t *r = response(c);
	int32_t mx = ui->mouse_x - d->left, my = ui->mouse_y - d->top, k;
	int hover = ui->open == c->parent && (c->flags & KP_UI_HOVER) && mx + d->left >= r[0] && mx + d->left <= r[2]
		&& my + d->top >= r[1] && my + d->top <= r[3];
	if (hover) {
		if (c->glow < FULL_LEVEL) c->glow = (int16_t)(c->glow + FADE_STEP < FULL_LEVEL ? c->glow + FADE_STEP : FULL_LEVEL);
	} else if (c->glow > 0) {
		c->glow = (int16_t)(c->glow - FADE_STEP > 0 ? c->glow - FADE_STEP : 0);
	}
	for (k = c->clips; k >= 0; k = ui->flc[k].next) {
		KpUiFlcClip *f = &ui->flc[k];
		uint16_t target;
		if (!(f->flags & CLIP_FADES)) continue;
		target = hover ? FULL_LEVEL : (f->flags & CLIP_DIMS) ? DIM_LEVEL : FULL_LEVEL;
		if (f->level < target) f->level = (uint16_t)(f->level + FADE_STEP < target ? f->level + FADE_STEP : target);
		else if (f->level > target) f->level = (uint16_t)(f->level - FADE_STEP > target ? f->level - FADE_STEP : target);
	}
	kp_ui_mark_dirty(ui, self_of(ui, c));
}

void kp_ui_flc_advance(KpUi *ui, int32_t owner)
{
	/* AdvanceFlicButton 0x4c89b0 */
	KpUiControl *c = &ui->controls[owner];
	int32_t *link = &c->clips, s;
	int changed = 0;
	if (!(c->flags & KP_UI_VISIBLE)) return;
	while (*link >= 0) {
		KpUiFlcClip *f = &ui->flc[*link];
		int shown = 0, result = kp_ui_video_continue(&f->video, 0, &shown, kp_ui_video_clock(ui));
		if (ui->video_trace && ui->flc_log_count < KP_UI_FLC_LOG) {
			if (f->order < 0) f->order = ui->flc_orders++;
			ui->flc_log[ui->flc_log_count].clip = f->order;
			ui->flc_log[ui->flc_log_count].result = result;
			ui->flc_log[ui->flc_log_count].shown = shown;
			ui->flc_log[ui->flc_log_count].frame = f->video.open ? (int32_t)f->video.smk.frame : -1;
			ui->flc_log_count++;
		}
		if (!result) {
			int32_t k = *link;
			free(f->pixels);
			f->pixels = NULL;
			changed = 1;
			*link = f->next;
			free_clip(ui, k);
			continue;
		}
		changed |= shown;
		link = &f->next;
	}
	if (changed) {
		for (s = ui->controls[c->parent].child; s >= 0; s = ui->controls[s].next) {
			if (ui->controls[s].type == 14 && ui->controls[s].clips >= 0) kp_ui_mark_dirty(ui, s);
		}
	}
	hover_fade(ui, c);
}

void kp_ui_flc_start_timers(KpUi *ui, int32_t dialog)
{
	/* StartDialogFlcTimers 0x4c84f0 */
	int32_t c;
	for (c = ui->controls[dialog].child; c >= 0; c = ui->controls[c].next) {
		if (ui->controls[c].type == 14) {
			(void)kp_ui_set_timer(&ui->timers, c, FLC_TIMER, FLC_PERIOD, kp_ui_callback(ui, kp_ui_flc_advance), ui->now);
		}
	}
}

void kp_ui_flc_stop_timers(KpUi *ui, int32_t dialog)
{
	/* StopDialogFlcTimers 0x4c84c0 */
	int32_t c;
	for (c = ui->controls[dialog].child; c >= 0; c = ui->controls[c].next) {
		if (ui->controls[c].type == 14) kp_ui_delete_timer(&ui->timers, c, FLC_TIMER);
	}
}

static int hit(KpUi *ui, KpUiControl *c, const KpUiEvent *e)
{
	/* ButtonHitTest 0x4c8970: the cursor relative to the parent (0x4c29d0) in the response rectangle */
	const KpUiControl *d = &ui->controls[c->parent];
	int16_t *r = response(c);
	int32_t x = (int16_t)(e->x - d->left), y = (int16_t)(e->y - d->top);
	if (x < r[0] || x > r[2] || y < r[1] || y > r[3]) return 0;
	if (!(c->flags & KP_UI_VISIBLE) || (c->flags & KP_UI_DISABLED)) return 0;
	return (c->flags & RESPONSIVE) != 0;
}

static void release(KpUi *ui, KpUiControl *c)
{
	/* ReleaseButton 0x4c8bc0 */
	kp_ui_release_refs(ui, self_of(ui, c));
	if (!(c->flags & KP_UI_PRESSED)) return;
	c->flags &= ~KP_UI_PRESSED;
	kp_ui_mark_dirty(ui, self_of(ui, c));
	(void)kp_ui_send_user(ui, self_of(ui, c), KP_UI_CLOSE, 0);
}

static void track(KpUi *ui, KpUiControl *c, const KpUiEvent *e, const int16_t r[4])
{
	/* TrackButtonPress 0x4c8a40 (the rectangle in the dialog's coordinates) */
	const KpUiControl *d = &ui->controls[c->parent];
	int32_t x = e->x - d->left, y = e->y - d->top, self = self_of(ui, c);
	int in = x >= r[0] && x <= r[2] && y >= r[1] && y <= r[3];
	if (in) {
		if (c->flags & KP_UI_PRESSED) return;
		kp_ui_ui_sound(ui, SOUND_OVER);
		c->flags ^= KP_UI_PRESSED;
		kp_ui_mark_dirty(ui, self);
		kp_ui_ui_sound(ui, SOUND_CLICK);
	} else if (c->flags & KP_UI_PRESSED) {
		c->flags ^= KP_UI_PRESSED;
		kp_ui_mark_dirty(ui, self);
	}
}

/* The type-14 user events of FlicButtonUserEvent 0x4c8ab0 (text = 0) and ButtonUserEvent 0x4c8c00 (text = 1). */
static int user_event(KpUi *ui, KpUiControl *c, KpUiEvent *e, int text)
{
	int32_t self = self_of(ui, c);
	KpUiInteract fallback = kp_ui_type_interact(-1);
	switch (e->code) {
	case KP_UI_DESTROY:
		if (!text) close_clips(ui, c);
		return fallback(ui, c, e);
	case KP_UI_CLOSE: {
		KpUiControl *d = &ui->controls[c->parent];
		ui->result = c->id;
		return d->interact ? d->interact(ui, d, e) : 0;
	}
	case KP_UI_ACTIVATE:
		kp_ui_mark_dirty(ui, self);
		if (c->flags & KP_UI_PRESSED) {
			release(ui, c);
		} else {
			kp_ui_schedule_press(ui, self);
			c->flags ^= KP_UI_PRESSED;
			if (c->flags & KP_UI_PRESSED) kp_ui_ui_sound(ui, SOUND_CLICK);
		}
		return 1;
	case KP_UI_HIT_TEST: return hit(ui, c, e);
	case KP_UI_FOCUS: return 0;
	case KP_UI_FOCUS_LOST:
		c->flags &= ~(KP_UI_PRESSED | KP_UI_HOVER);
		return fallback(ui, c, e);
	case KP_UI_MOUSE_ENTER:
		kp_ui_ui_sound(ui, SOUND_OVER);
		return 1;
	case KP_UI_POST_LOAD:
		if (text) {
			absolute_response(c);
			c->draw = draw_text_button;
			c->text_format = (uint8_t)((c->flags >> 20) & 0x77);
			c->glow = 0;
		} else {
			init_button(ui, c);
		}
		return fallback(ui, c, e);
	case KP_UI_HIDE:
		c->flags &= ~KP_UI_PRESSED;
		return fallback(ui, c, e);
	default: return fallback(ui, c, e);
	}
}

static int button_proc(KpUi *ui, KpUiControl *c, KpUiEvent *e, int text)
{
	/* FlcButtonProc 0x4c82f0 / the text button 0x4c83b0 (the control's rectangle instead of the response one) */
	int16_t r[4];
	const int16_t *resp = response(c);
	int32_t self = self_of(ui, c);
	const KpUiControl *d = &ui->controls[c->parent];
	if (text) {
		r[0] = c->left;
		r[1] = c->top;
		r[2] = c->right;
		r[3] = c->bottom;
	} else { /* the response rectangle in the dialog's coordinates */
		r[0] = (int16_t)(resp[0] - d->left);
		r[1] = (int16_t)(resp[1] - d->top);
		r[2] = (int16_t)(resp[2] - d->left);
		r[3] = (int16_t)(resp[3] - d->top);
	}
	switch (e->type) {
	case KP_UI_MOUSE_MOVE:
		track(ui, c, e, r);
		return 1;
	case KP_UI_LEFT_DOWN: case KP_UI_LEFT_DOUBLE:
		kp_ui_set_capture(ui, self, KP_UI_LEFT_UP);
		kp_ui_set_capture(ui, self, KP_UI_MOUSE_MOVE);
		track(ui, c, e, r);
		return 1;
	case KP_UI_LEFT_UP:
		kp_ui_release_refs(ui, self);
		track(ui, c, e, r);
		kp_ui_update_hover(ui, c->parent, e);
		release(ui, c);
		return 1;
	case KP_UI_USER: return user_event(ui, c, e, text);
	default: return 0;
	}
}

int kp_ui_flc_button_proc(KpUi *ui, KpUiControl *c, KpUiEvent *e) { return button_proc(ui, c, e, 0); }

int kp_ui_text_button_proc(KpUi *ui, KpUiControl *c, KpUiEvent *e) { return button_proc(ui, c, e, 1); }

int kp_ui_flc_animation_proc(KpUi *ui, KpUiControl *c, KpUiEvent *e)
{
	/* FlcAnimationProc 0x4c82b0 */
	KpUiInteract fallback = kp_ui_type_interact(-1);
	if (e->type != KP_UI_USER) return 0;
	if (e->code == KP_UI_DESTROY) {
		close_clips(ui, c);
		return fallback(ui, c, e);
	}
	if (e->code == KP_UI_POST_LOAD) {
		init_button(ui, c);
		return fallback(ui, c, e);
	}
	return 0;
}
