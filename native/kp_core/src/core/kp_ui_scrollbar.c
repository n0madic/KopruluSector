/* The scroll bars and sliders (scrlbar.cpp, atlas P44 0x4c8d10..0x4c9be0): control types 6 (horizontal) and 7
 * (vertical), flag 0x2000000 the slider style. The position in a range with its page and tick steps, the hit zones
 * (the arrows, the track before and after the thumb, the thumb), the actions a press starts (a step at once, then
 * repeated by the idle events and the mouse's moves; the thumb dragged), the keys, and the four draw procedures. A
 * list box's vertical bar (CreateScrollBar 0x4c9580) scrolls its rows through the change callback. */
#include "kp_ui.h"

#include <stdlib.h>

#define FLAG_SLIDER 0x2000000u
#define ACTION_DRAG 0x80000000u     /* the thumb dragged (+0x42) */
#define ACTION_LOW 0x40000000u      /* the left or up arrow pressed */
#define ACTION_HIGH 0x20000000u     /* the right or down arrow */
#define ACTION_PAGE_LOW 0x8000000u  /* the track before the thumb */
#define ACTION_PAGE_HIGH 0x4000000u /* the track after it */
#define ACTION_KEEP 0x13ffffffu     /* the flags a left button's release keeps */
#define ID_NO_SHOW 0x8000           /* an ID with bit 15: a list's bar, which shows and hides with the list */
#define SLIDER_THUMB_TICK 0x164     /* InitScrollBar 0x4c9930: thumb frame 100, tick step 1 */
#define TICKS_MAX 7
#define SOUND_PRESS 2               /* the controls' sound hook */
#define COLOUR_DISABLED 8           /* 0x6d2370 */
#define COLOUR_FOCUS 16             /* 0x6d2378 */
#define VK_PRIOR 0x21
#define VK_NEXT 0x22
#define VK_END 0x23
#define VK_HOME 0x24
#define VK_LEFT 0x25
#define VK_UP 0x26
#define VK_RIGHT 0x27
#define VK_DOWN 0x28

/* the zones' actions 0x4f11d0 and steps 0x50ef78 (the page steps set per press) */
static const uint32_t ZONE_ACTIONS[7] = { 0, ACTION_LOW, ACTION_HIGH, ACTION_DRAG, ACTION_PAGE_LOW, ACTION_PAGE_HIGH,
	0 };

static KpUiScrollBar *state(KpUiControl *c)
{
	if (c->bar == NULL) c->bar = (KpUiScrollBar *)calloc(1, sizeof *c->bar);
	return c->bar;
}

static int32_t self_of(const KpUi *ui, const KpUiControl *c) { return (int32_t)(c - ui->controls); }

static int slider(const KpUiControl *c) { return (c->flags & FLAG_SLIDER) != 0; }

void kp_ui_scrollbar_free(KpUiControl *c)
{
	free(c->bar);
	c->bar = NULL;
}

/* ScrollTrackTravel 0x4c9320 (16-bit arithmetic). */
static uint16_t travel(const KpUi *ui, const KpUiControl *c)
{
	const KpUiTheme *t = &ui->theme;
	if (c->type == 6) {
		return slider(c) ? (uint16_t)(c->right - c->left - t->slider_thumb_w + 1)
			: (uint16_t)(c->right - c->left - 2 * t->left_w - t->thumb_w - 1);
	}
	return slider(c) ? (uint16_t)(c->bottom - c->top - t->slider_thumb_h + 1)
		: (uint16_t)(c->bottom - c->top - 2 * t->up_h - t->thumb_h - 5);
}

/* ScrollThumbOffset 0x4c9290. */
static int16_t thumb_offset(const KpUi *ui, KpUiControl *c)
{
	KpUiScrollBar *s = state(c);
	int32_t r, t;
	if (c->flags & ACTION_DRAG) return s->drag;
	r = (int16_t)(uint16_t)(s->max - s->min);
	if (r == 0) return 0;
	t = (int16_t)travel(ui, c);
	if (s->pos == s->min) return 0;
	if (s->pos == s->max) return (int16_t)t;
	return (int16_t)((r / 2 + (int32_t)(int16_t)(s->pos - s->min) * t) / r);
}

int16_t kp_ui_scrollbar_pos(KpUiControl *c) { return state(c)->pos; } /* GetScrollPos 0x4c9920 */

void kp_ui_scrollbar_set_pos(KpUi *ui, int32_t bar, int32_t value)
{
	/* SetScrollPos 0x4c9ae0: clamped; the change callback when it moves; always invalidated */
	KpUiControl *c = &ui->controls[bar];
	KpUiScrollBar *s = state(c);
	int16_t v = (int16_t)value;
	if (v < s->min) v = s->min;
	else if (v > s->max) v = s->max;
	if (v != s->pos) {
		s->pos = v;
		if (s->changed != NULL) s->changed(ui, c->user, v);
	}
	kp_ui_mark_dirty(ui, bar);
}

void kp_ui_scrollbar_set_range(KpUi *ui, int32_t bar, int32_t min, int32_t max)
{
	/* SetScrollRange 0x4c9b20: the callback gets the old position */
	KpUiControl *c = &ui->controls[bar];
	KpUiScrollBar *s = state(c);
	int16_t v, old;
	if (s->min == (int16_t)min && s->max == (int16_t)max) return;
	s->min = (int16_t)min;
	s->max = (int16_t)max;
	v = old = s->pos;
	if (v < s->min) v = s->min;
	else if (v > s->max) v = s->max;
	if (v != old) {
		s->pos = v;
		if (s->changed != NULL) s->changed(ui, c->user, (uint16_t)old);
	}
	kp_ui_mark_dirty(ui, bar);
}

void kp_ui_scrollbar_set_steps(KpUi *ui, int32_t bar, uint8_t page, uint8_t tick)
{
	/* SetScrollSteps 0x4c9ad0 */
	KpUiScrollBar *s = state(&ui->controls[bar]);
	s->page = page;
	s->tick = tick;
}

void kp_ui_scrollbar_set_callback(KpUi *ui, int32_t bar, KpUiScrollChanged changed)
{
	state(&ui->controls[bar])->changed = changed;
}

/* ScrollHitZone 0x4c9790: 0 outside, 1/2 the arrows (a slider's ends), 3 the thumb, 4/5 the track before and after. */
static int hit_zone(const KpUi *ui, KpUiControl *c, const KpUiEvent *e)
{
	const KpUiControl *p = &ui->controls[c->parent];
	const KpUiTheme *t = &ui->theme;
	int32_t x = (int16_t)(e->x - p->left), y = (int16_t)(e->y - p->top), at, s, n, a, k, th;
	if (x < c->left || x > c->right || y < c->top || y > c->bottom) return 0;
	if (c->type == 6) {
		at = x;
		s = c->left;
		n = (uint16_t)(c->right - c->left + 1);
		a = (slider(c) ? 0 : t->left_w) + 2;
		k = slider(c) ? t->slider_thumb_w : t->thumb_w;
	} else {
		at = y;
		s = c->top;
		n = (uint16_t)(c->bottom - c->top + 1);
		a = (slider(c) ? 0 : t->up_h) + 2;
		k = slider(c) ? t->slider_thumb_w : t->thumb_h;
	}
	if (at <= s + a) return 1;
	if (at >= s + n - a) return 2;
	th = (int16_t)(s + a + thumb_offset(ui, c));
	if (at < th) return 4;
	if (at >= th + k) return 5;
	return 3;
}

static int send_value(KpUi *ui, KpUiControl *c, int32_t value)
{
	return kp_ui_send_user(ui, self_of(ui, c), KP_UI_VALUE_CHANGED, (uint32_t)(int32_t)(int16_t)value);
}

/* UpdateScrollAction 0x4c93b0: the dragged thumb follows the cursor; a pressed zone shows while the cursor is on it. */
static void update_action(KpUi *ui, KpUiControl *c, const KpUiEvent *e)
{
	KpUiScrollBar *s = state(c);
	const KpUiTheme *t = &ui->theme;
	uint32_t f = ZONE_ACTIONS[hit_zone(ui, c, e)], a = s->action;
	if (a == ACTION_DRAG) {
		const KpUiControl *p = &ui->controls[c->parent];
		int32_t x = (int16_t)(e->x - p->left), y = (int16_t)(e->y - p->top), limit, o, start, half;
		if (c->type == 6) {
			limit = slider(c) ? c->right - (t->slider_thumb_w >> 1) : c->right - (t->thumb_w >> 1) - t->left_h - 3;
			o = (x < limit ? x : limit) - c->left;
			start = slider(c) ? 0 : t->left_w;
			half = (slider(c) ? t->slider_thumb_w : t->thumb_w) >> 1;
		} else { /* a vertical slider's limit is the original's right edge */
			limit = slider(c) ? c->right - (t->slider_thumb_h >> 1) : c->bottom - (t->thumb_h >> 1) - t->up_h - 3;
			o = (y < limit ? y : limit) - c->top;
			start = slider(c) ? 0 : t->up_h + 2;
			half = (slider(c) ? t->slider_thumb_h : t->thumb_h) >> 1;
		}
		o -= half + start;
		if (o < 0) o = 0;
		s->drag = (int16_t)o;
		{
			int32_t tr = (int16_t)travel(ui, c);
			s->step = (int16_t)(tr != 0 ? s->min + ((s->max - s->min + 1) * o) / tr - s->pos : 0);
		}
		return;
	}
	if (a & c->flags) {
		if (f != a) {
			c->flags ^= a;
			kp_ui_mark_dirty(ui, self_of(ui, c));
		}
	} else if (f == a) {
		c->flags ^= a;
		kp_ui_mark_dirty(ui, self_of(ui, c));
		kp_ui_play_click(ui); /* PlayUiClickSound 0x49e460 */
	}
}

/* ScrollRepeatTick 0x4c9b90. */
static void repeat_tick(KpUi *ui, KpUiControl *c, const KpUiEvent *e)
{
	KpUiScrollBar *s = state(c);
	if (--s->countdown != 0) return;
	s->countdown = (s->action & (ACTION_LOW | ACTION_HIGH)) ? 1 : 3;
	update_action(ui, c, e);
	if (s->action & c->flags) (void)send_value(ui, c, s->pos + s->step);
}

/* BeginScrollAction 0x4c9be0: the button's release, the moves and the idle events come here; the first step now. */
static void begin_action(KpUi *ui, KpUiControl *c, const KpUiEvent *e, int16_t step, uint32_t action)
{
	KpUiScrollBar *s = state(c);
	int32_t self = self_of(ui, c);
	kp_ui_set_capture(ui, self, KP_UI_LEFT_UP);
	kp_ui_set_capture(ui, self, KP_UI_MOUSE_MOVE);
	kp_ui_set_capture(ui, self, KP_UI_IDLE);
	s->step = step;
	c->flags |= action;
	s->countdown = 1;
	s->action = action;
	kp_ui_mark_dirty(ui, self);
	repeat_tick(ui, c, e);
}

/* ScrollMouseDown 0x4c9a70. */
static int mouse_down(KpUi *ui, KpUiControl *c, const KpUiEvent *e)
{
	KpUiScrollBar *s = state(c);
	int16_t steps[6];
	int z = hit_zone(ui, c, e);
	steps[0] = 0;
	steps[1] = -1;
	steps[2] = 1;
	steps[3] = 0;
	steps[4] = (int16_t)-s->page;
	steps[5] = (int16_t)s->page;
	if (z == 0) return 0;
	if (z != 6) {
		begin_action(ui, c, e, steps[z], ZONE_ACTIONS[z]);
		kp_ui_ui_sound(ui, SOUND_PRESS);
	}
	return 1;
}

/* ScrollBarKey 0x4c99b0. */
static int key(KpUi *ui, KpUiControl *c, const KpUiEvent *e)
{
	KpUiScrollBar *s = state(c);
	int32_t v;
	switch (e->key & 0xff) {
	case VK_PRIOR: v = s->pos - s->page; break;
	case VK_NEXT: v = s->pos + s->page; break;
	case VK_END: v = s->max; break;
	case VK_HOME: v = s->min; break;
	case VK_LEFT: if (c->type != 6) return 0; v = s->pos - 1; break;
	case VK_UP: if (c->type == 6) return 0; v = s->pos - 1; break;
	case VK_RIGHT: if (c->type != 6) return 0; v = s->pos + 1; break;
	case VK_DOWN: if (c->type == 6) return 0; v = s->pos + 1; break;
	default: return 0;
	}
	(void)send_value(ui, c, v);
	return 1;
}

/* InitScrollBar 0x4c9930 (user code 10). */
static void init(KpUi *ui, KpUiControl *c)
{
	KpUiScrollBar *s = state(c);
	const KpUiTheme *t = &ui->theme;
	if (slider(c)) {
		s->thumb = (uint8_t)(SLIDER_THUMB_TICK & 0xff);
		s->tick = (uint8_t)(SLIDER_THUMB_TICK >> 8);
	}
	if (c->type == 6) {
		int16_t h = (int16_t)(c->top + (slider(c) ? t->slider_thumb_h : t->left_h));
		if (c->bottom < h) c->bottom = h;
	} else if (!slider(c)) {
		int16_t w = (int16_t)(c->left + t->up_w);
		if (c->right < w) c->right = w;
	}
}

int kp_ui_scrollbar_proc(KpUi *ui, KpUiControl *c, KpUiEvent *e)
{
	/* ScrollBarProc 0x4c9630 */
	KpUiScrollBar *s = state(c);
	KpUiInteract fallback = kp_ui_type_interact(-1);
	switch (e->type) {
	case KP_UI_KEY_DOWN: case KP_UI_KEY_REPEAT: return key(ui, c, e);
	case KP_UI_KEY_UP: return 0;
	case KP_UI_MOUSE_MOVE:
		update_action(ui, c, e);
		if (c->flags & ACTION_DRAG) {
			s->countdown = 1;
			repeat_tick(ui, c, e);
		}
		return 1;
	case KP_UI_LEFT_DOWN: case KP_UI_LEFT_DOUBLE: return mouse_down(ui, c, e);
	case KP_UI_LEFT_UP:
		update_action(ui, c, e);
		c->flags &= ACTION_KEEP;
		kp_ui_mark_dirty(ui, self_of(ui, c));
		kp_ui_release_refs(ui, self_of(ui, c));
		return 0;
	case KP_UI_IDLE:
		repeat_tick(ui, c, e);
		return 0;
	case KP_UI_USER:
		switch (e->code) {
		case KP_UI_FOCUS_LOST: c->flags &= ACTION_KEEP; break;
		case KP_UI_POST_LOAD: init(ui, c); break;
		case KP_UI_VALUE_CHANGED:
			kp_ui_scrollbar_set_pos(ui, self_of(ui, c), (int16_t)(e->param & 0xffff));
			return 1;
		case KP_UI_SHOW: case KP_UI_HIDE:
			if ((uint16_t)c->id & ID_NO_SHOW) return 1;
			break;
		case KP_UI_SHOW + 2: kp_ui_scrollbar_set_pos(ui, self_of(ui, c), s->pos - 1); break; /* 15 */
		case KP_UI_SHOW + 3: kp_ui_scrollbar_set_pos(ui, self_of(ui, c), s->pos + 1); break; /* 16 */
		default: break;
		}
		return fallback(ui, c, e);
	default: return 0;
	}
}

/* --- the draw procedures --- */

static void focus_outline(KpUi *ui, KpUiControl *c, uint8_t colour)
{
	/* DrawFocusOutline 0x4c4200 */
	int32_t w = (uint16_t)(c->right - c->left + 1), h = (uint16_t)(c->bottom - c->top + 1);
	if (!(c->flags & KP_UI_FOCUS_LINE)) return;
	kp_ui_fill_rect(ui, c->left, c->top, w, 1, colour);
	kp_ui_fill_rect(ui, c->left, c->top + h - 1, w, 1, colour);
	kp_ui_fill_rect(ui, c->left, c->top, 1, h, colour);
	kp_ui_fill_rect(ui, c->left + w - 1, c->top, 1, h, colour);
}

static void frame(KpUi *ui, int f, int32_t x, int32_t y, const int16_t clip[4])
{
	kp_ui_draw_theme_frame(ui, f, x, y, clip, KP_UI_TRANSPARENT); /* DrawThemeFrameTransparent 0x4c46d0 */
}

static void draw_hslider(KpUi *ui, KpUiControl *c, const int16_t clip[4])
{
	/* DrawHSlider 0x4c8ed0 */
	KpUiScrollBar *s = state(c);
	const KpUiTheme *t = &ui->theme;
	int d = (c->flags & KP_UI_DISABLED) != 0, k = d ? 97 : 98, step = s->tick ? s->tick : 1, count, i;
	int32_t l = c->left, top = c->top, half = t->slider_thumb_w >> 1, tick = (t->tick_w + 1) / 2;
	kp_ui_update_focus(ui, c);
	kp_ui_three_part_bar(ui, l, top, c->right - l + 1, t->slider_piece, d ? 91 : 94, clip, KP_UI_TRANSPARENT);
	frame(ui, k, l + half, top, clip);
	count = (s->max - s->min) / step;
	if (count > TICKS_MAX) count = TICKS_MAX;
	for (i = 1; i < count; i++) {
		frame(ui, k, l + half + ((c->right - l - t->slider_thumb_w + 1) * i) / count - tick, top, clip);
	}
	frame(ui, k, (int16_t)(c->right - tick - half), top, clip);
	frame(ui, d ? 99 : s->thumb, l + thumb_offset(ui, c), top, clip);
	focus_outline(ui, c, ui->ui_colours[COLOUR_FOCUS]);
}

static void draw_vslider(KpUi *ui, KpUiControl *c)
{
	/* DrawVSlider 0x4c9210 */
	uint8_t colour = ui->ui_colours[(c->flags & KP_UI_DISABLED) ? COLOUR_DISABLED : COLOUR_FOCUS];
	int32_t w = (uint16_t)(c->right - c->left + 1), h = (uint16_t)(c->bottom - c->top + 1), y;
	kp_ui_update_focus(ui, c);
	kp_ui_fill_rect(ui, c->left, c->top, w, 1, colour);
	kp_ui_fill_rect(ui, c->left, c->top + h - 1, w, 1, colour);
	kp_ui_fill_rect(ui, c->left, c->top, 1, h, colour);
	kp_ui_fill_rect(ui, c->left + w - 1, c->top, 1, h, colour);
	if (c->flags & KP_UI_DISABLED) return;
	y = c->top + thumb_offset(ui, c);
	kp_ui_fill_rect(ui, c->left, y, w, 1, colour);
	kp_ui_fill_rect(ui, c->left, y + 5, w, 1, colour);
	kp_ui_fill_rect(ui, c->left, y, 1, 6, colour);
	kp_ui_fill_rect(ui, c->left + w - 1, y, 1, 6, colour);
}

static void draw_hbar(KpUi *ui, KpUiControl *c, const int16_t clip[4])
{
	/* DrawHScrollBar 0x4c8d10 */
	const KpUiTheme *t = &ui->theme;
	int e = !(c->flags & KP_UI_DISABLED), n, k;
	int32_t l = c->left, top = c->top, x;
	if (slider(c)) {
		draw_hslider(ui, c, clip);
		return;
	}
	if (c->right - c->left < 2 * (t->left_w + t->piece_w)) return;
	kp_ui_update_focus(ui, c);
	kp_ui_draw_theme_frame(ui, 22 + e + ((c->flags & ACTION_LOW) ? 1 : 0), l, top, clip, c->flags);
	kp_ui_draw_theme_frame(ui, 32, l + t->left_w, top, clip, c->flags);
	x = t->left_w;
	n = t->piece_w != 0 ? (c->right - 2 * t->left_w - c->left) / t->piece_w - 2 : 0;
	for (k = 0; k < n; k++) {
		x += t->piece_w;
		kp_ui_draw_theme_frame(ui, 33, l + x, top, clip, c->flags);
	}
	x += t->piece_w;
	kp_ui_draw_theme_frame(ui, 34, l + x, top, clip, c->flags);
	x += t->left_w;
	kp_ui_draw_theme_frame(ui, 25 + e + ((c->flags & ACTION_HIGH) ? 1 : 0), l + x, top, clip, c->flags);
	if (e) frame(ui, 28, l + t->left_w + thumb_offset(ui, c), top + (t->left_h - t->thumb_h) / 2, clip);
	if (!(c->flags & KP_UI_PLAIN)) focus_outline(ui, c, ui->ui_colours[COLOUR_FOCUS]);
}

static void draw_vbar(KpUi *ui, KpUiControl *c, const int16_t clip[4])
{
	/* DrawVScrollBar 0x4c9060 */
	const KpUiTheme *t = &ui->theme;
	int e = !(c->flags & KP_UI_DISABLED), n, k;
	int32_t l = c->left, top = c->top, y;
	if (slider(c)) {
		draw_vslider(ui, c);
		return;
	}
	if (c->bottom - c->top < 2 * (t->up_w + t->track_h)) return;
	kp_ui_update_focus(ui, c);
	frame(ui, 16 + e + ((c->flags & ACTION_LOW) ? 1 : 0), l, top, clip);
	frame(ui, 29, l, top + t->up_h + 2, clip);
	y = t->up_h + 2;
	n = t->track_h != 0 ? ((int16_t)travel(ui, c) + t->thumb_h) / t->track_h - 1 : 0;
	for (k = 0; k < n; k++) {
		y += t->track_h;
		frame(ui, 30, l, top + y, clip);
	}
	y += t->track_h;
	frame(ui, 31, l, top + y, clip);
	frame(ui, 19 + e + ((c->flags & ACTION_HIGH) ? 1 : 0), l, top + y + t->track_h + 2, clip);
	if (e) frame(ui, 28, l - 1 + ((t->up_w - t->thumb_w) >> 1), top + thumb_offset(ui, c) + 2 + t->up_h, clip);
	if (!(c->flags & KP_UI_PLAIN)) focus_outline(ui, c, ui->ui_colours[COLOUR_FOCUS]);
}

void kp_ui_scrollbar_draw(KpUi *ui, KpUiControl *c, int32_t x, int32_t y, const int16_t clip[4])
{
	(void)x;
	(void)y;
	if (c->type == 6) draw_hbar(ui, c, clip);
	else draw_vbar(ui, c, clip);
}
