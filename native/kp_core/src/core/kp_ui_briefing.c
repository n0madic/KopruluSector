/* The ready rooms and their briefing (glurdy.cpp, glubrfport.cpp; atlas P17 0x44fcd0..0x4505a0, P21 0x460a20..0x461840):
 * the room's dialog (rez\glurdy<t|z|p>.bin with its procedure, slides and controls' procedures), the briefing list of
 * the game (kp_briefing_record: ExpandBriefingOwners 0x4607e0) run by RunBriefingTrigger 0x44fee0 every 100 ms on
 * timer 10 (BriefingTick 0x460ea0), the ten actions of 0x503908 (the waits on timer 11, the text of control -10 with
 * its scroll timers 1-3, the objectives of -11, the portrait slots 15-18, Skip Tutorial), and Start, Cancel, Replay.
 * The menus' CRT generator draws the portraits' variants (kp_ui.glue.crt, the process's before the game's seed). */
#include "kp_ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kp_game.h"
#include "kp_ids.h"
#include "kp_rng.h"

#define RECORD_ACTIONS 0x140
#define ACTION_SIZE 32
#define ACTIONS 64
#define EXEC_FLAGS 0x940
#define EXEC_RUNNING 0x01u
#define ACTION_INDEX 0x95f
#define CONDITION_TYPE 0x0f
#define MISSION_BRIEFING 13
#define A_STRING 0x04
#define A_WAV 0x08
#define A_TIME 0x0c
#define A_SLOT 0x10
#define A_NUMBER 0x14
#define A_UNIT 0x18
#define A_ID 0x1a
#define A_MODIFIER 0x1b
#define A_FLAGS 0x1c
#define A_WAITED 0x01u
#define A_DISABLED 0x02u
#define SET_TO 7
#define ADD 8
#define SUBTRACT 9
#define TICK_TIMER 10
#define TICK_PERIOD 100
#define WAIT_TIMER 11
#define TEXT_HIDE 1
#define TEXT_STEP 2
#define TEXT_DELAY 3
#define TEXT_DELAY_MS 4000
#define TEXT_HEIGHTS 20
#define TEXT_FORMAT 0x11
#define TEXT_FONT 1            /* 0x6d3028 */
#define ID_TEXT (-10)
#define ID_OBJECTIVES (-11)
#define ID_SKIP_IMAGE (-13)
#define ID_NO_BRIEFING (-14)
#define ID_START 13
#define ID_CANCEL 14
#define ID_SLOT_FIRST 15
#define ID_SKIP 19
#define ID_REPLAY 20
#define ID_ANIMATION 1
#define RESULT_START 101
#define RESULT_EXIT 100
#define SLIDES 7
#define SLIDE_DELAY 80
#define PROCS 20
#define SLOTS 4
#define PORTRAIT_W 120
#define PORTRAIT_H 112
#define SLOT_TIMER_FRAMES 1
#define SLOT_TIMER_TALK 2
#define SLOT_PERIOD 50
#define SLOT_TALKING 0x01u
#define SLOT_PENDING 0x02u
#define VARIANTS 4
#define IDLE_RANGE 101
#define TALK_VARIANTS 3
#define TURN_MS 250
#define MUSIC_TITLE 22
#define SLIDE_DONE 0x405
#define CHEAT_NOGLUES 0x20000000

/* The rooms of the races (zerg 0, terran 1, protoss 2): the BIN, the theme (PalRz, PalRt, PalRp), the music. */
static const char *const BINS[3] = { "rez\\glurdyz.bin", "rez\\glurdyt.bin", "rez\\glurdyp.bin" };
static const int THEMES[3] = { 3, 2, 4 };
static const uint16_t MUSIC[3] = { 10, 11, 12 };
static const char LETTERS[3] = { 'Z', 'T', 'P' };
/* 0x508370 (the same for the three rooms): {ID, direction} */
static const uint16_t SLIDE_TABLE[2 * SLIDES] = { 5, 3, 6, 0, 7, 3, 9, 0, 10, 0, 11, 2, 12, 2 };

static KpUiBriefing *br(KpUi *ui) { return &ui->briefing; }

static int32_t control_id(KpUi *ui, int16_t id) { return kp_ui_find_control(ui, br(ui)->dialog, id); }

static const uint8_t *map_string(KpUi *ui, uint32_t id)
{
	return ui->game != NULL ? (const uint8_t *)kp_map_string(ui->game, (int64_t)(id & 0xffff)) : NULL;
}

/* --- the portrait slots (glubrfport.cpp) --- */

static KpUiBriefSlot *slot_of(KpUi *ui, int32_t c)
{
	int k = ui->controls[c].id - ID_SLOT_FIRST;
	return k >= 0 && k < SLOTS ? &br(ui)->slots[k] : NULL;
}

static void open_variant(KpUi *ui, KpUiBriefSlot *s, int talking, int variant)
{
	uint32_t row = s->portrait;
	const KpUiFile *f = kp_ui_portrait_movie(ui, row, talking, variant);
	if (f == NULL && ui->video_trace) { /* SVidPlayBegin of a missing file fails, after the oracle saw its name */
		char name[KP_UI_VIDEO_NAME];
		if (kp_ui_portrait_movie_name(ui, row, talking, variant, name, sizeof name) == 0) {
			kp_ui_video_trace(ui, name, strlen(name));
		}
	}
	(void)kp_ui_video_begin_zoomed(ui, &s->video, f, s->bitmap.pixels, s->bitmap.w, s->bitmap.h);
}

static void open_idle(KpUi *ui, int32_t c, KpUiBriefSlot *s)
{
	/* OpenBriefingIdleVideo 0x4503d0 */
	KpGame *g = ui->game;
	uint8_t v = (uint8_t)(((int32_t)kp_ui_glue_rand(ui) >> 8) % IDLE_RANGE), sum;
	int i = 0;
	uint32_t p = s->portrait;
	sum = (uint8_t)KP_DAT(g, portdata, idle_smk_change, p);
	while (v > sum) {
		if (++i >= VARIANTS) {
			i = 0;
			break;
		}
		switch (i) {
		case 1: sum = (uint8_t)(sum + KP_DAT(g, portdata, talking_smk_change, p)); break;
		case 2: sum = (uint8_t)(sum + KP_DAT(g, portdata, idle_unknown, p)); break;
		default: sum = (uint8_t)(sum + KP_DAT(g, portdata, talking_unknown, p)); break;
		}
	}
	for (; i >= 0 && !s->video.open; i--) open_variant(ui, s, 0, i);
	kp_ui_mark_dirty(ui, c);
}

static void open_talking(KpUi *ui, int32_t c, KpUiBriefSlot *s)
{
	/* OpenBriefingTalkingVideo 0x4504c0 */
	uint8_t v = (uint8_t)(((int32_t)kp_ui_glue_rand(ui) >> 8) % TALK_VARIANTS);
	int8_t i;
	if (v == s->previous) v = (uint8_t)((v + 2) % TALK_VARIANTS);
	i = (int8_t)v;
	while (i >= 0 && !s->video.open) { /* +0x10 = the loop variable: v, the opened variant - 1, or 0xff */
		open_variant(ui, s, 1, i);
		i = (int8_t)(i - 1);
	}
	s->previous = (uint8_t)i;
	kp_ui_mark_dirty(ui, c);
}

static void slot_advance(KpUi *ui, int32_t c)
{
	/* AdvanceBriefingPortraitVideo 0x4502e0 */
	KpUiBriefSlot *s = slot_of(ui, c);
	int updated = 0, result = 0;
	if (s == NULL) return;
	if (s->video.open) {
		result = kp_ui_video_continue(&s->video, 0, &updated, kp_ui_video_clock(ui));
		if (ui->video_trace && ui->bpc_log_count < KP_UI_FLC_LOG) {
			ui->bpc_log[ui->bpc_log_count].id = ui->controls[c].id;
			ui->bpc_log[ui->bpc_log_count].result = result;
			ui->bpc_log[ui->bpc_log_count].shown = updated;
			ui->bpc_log[ui->bpc_log_count].frame = s->video.open ? (int32_t)s->video.smk.frame : -1;
			ui->bpc_log_count++;
		}
	}
	if (result) {
		if (!updated) return;
		if (s->flags & SLOT_PENDING) {
			kp_ui_show_control(ui, c);
			s->flags &= (uint8_t)~SLOT_PENDING;
		}
		kp_ui_mark_dirty(ui, c);
		return;
	}
	kp_ui_video_end(&s->video);
	if (s->flags & SLOT_TALKING) open_talking(ui, c, s);
	else open_idle(ui, c, s);
}

static void slot_timer(KpUi *ui, int32_t c, uint16_t id)
{
	/* BriefingPortraitTimer 0x4505a0 */
	KpUiBriefSlot *s = slot_of(ui, c);
	if (s == NULL || (!(ui->controls[c].flags & KP_UI_VISIBLE) && !(s->flags & SLOT_PENDING))) return;
	if (id == SLOT_TIMER_FRAMES) {
		slot_advance(ui, c);
	} else if (id == SLOT_TIMER_TALK) {
		s->flags &= (uint8_t)~SLOT_TALKING;
		kp_ui_delete_timer(&ui->timers, c, SLOT_TIMER_TALK);
		kp_ui_video_end(&s->video);
		slot_advance(ui, c);
	}
}

static void slot_frames(KpUi *ui, int32_t owner) { slot_timer(ui, owner, SLOT_TIMER_FRAMES); }
static void slot_talk(KpUi *ui, int32_t owner) { slot_timer(ui, owner, SLOT_TIMER_TALK); }

/* The HD interface's images (ADR 0013 R4d) lose the pixels of b drawn at x, y within clip (keyed: index 0 not). */
static void erase_images(KpUi *ui, KpUiSurface *t, const KpUiSurface *b, int32_t x, int32_t y, const int16_t clip[4],
	int keyed)
{
	int32_t rect[4];
	rect[0] = x > clip[0] ? x : clip[0];
	rect[1] = y > clip[1] ? y : clip[1];
	rect[2] = x + b->w < clip[2] + 1 ? x + b->w : clip[2] + 1;
	rect[3] = y + b->h < clip[3] + 1 ? y + b->h : clip[3] + 1;
	if (rect[0] < 0) rect[0] = 0;
	if (rect[1] < 0) rect[1] = 0;
	if (rect[0] < rect[2] && rect[1] < rect[3]) kp_ui_image_erase(ui, t, rect, keyed ? b : NULL, rect[0] - x, rect[1] - y);
}

static void blit_keyed(KpUi *ui, KpUiSurface *t, const KpUiSurface *b, int32_t x, int32_t y, const int16_t clip[4],
	int keyed)
{
	int32_t i, j;
	if (t == NULL || b->pixels == NULL) return;
	for (j = 0; j < b->h; j++) {
		int32_t ty = y + j;
		if (ty < clip[1] || ty > clip[3] || ty < 0 || ty >= t->h) continue;
		for (i = 0; i < b->w; i++) {
			int32_t tx = x + i;
			uint8_t v = b->pixels[(size_t)j * b->w + i];
			if (tx < clip[0] || tx > clip[2] || tx < 0 || tx >= t->w || (keyed && v == 0)) continue;
			t->pixels[(size_t)ty * t->w + tx] = v;
		}
	}
	erase_images(ui, t, b, x, y, clip, keyed);
}

static void slot_draw(KpUi *ui, KpUiControl *c, int32_t x, int32_t y, const int16_t clip[4])
{
	/* DrawBriefingPortrait 0x450370 */
	KpUiBriefSlot *s = slot_of(ui, (int32_t)(c - ui->controls));
	const KpUiSurface *frame;
	int k;
	(void)x;
	(void)y;
	if (s == NULL) return;
	k = c->id - ID_SLOT_FIRST;
	kp_ui_video_image(ui, &s->video, &s->bitmap);
	blit_keyed(ui, ui->text.target, &s->bitmap, (int16_t)(c->left + s->off_x), (int16_t)(c->top + s->off_y), clip, 0);
	if (ui->hd && ui->text.target) { /* the HD interface: the movie's image carried (ADR 0013 R4e) */
		int32_t bx = c->left + s->off_x, by = c->top + s->off_y, rect[4];
		rect[0] = bx > clip[0] ? bx : clip[0];
		rect[1] = by > clip[1] ? by : clip[1];
		rect[2] = bx + s->bitmap.w < clip[2] + 1 ? bx + s->bitmap.w : clip[2] + 1;
		rect[3] = by + s->bitmap.h < clip[3] + 1 ? by + s->bitmap.h : clip[3] + 1;
		if (rect[0] < rect[2] && rect[1] < rect[3]) {
			kp_ui_image_blit(ui, &s->bitmap, ui->text.target, rect[0] - bx, rect[1] - by, rect, 0);
		}
	}
	frame = (s->flags & SLOT_TALKING) ? &br(ui)->talking_frames[k] : &br(ui)->frames[k];
	blit_keyed(ui, ui->text.target, frame, c->left, c->top, clip, 1);
}

static void slot_create(KpUi *ui, KpUiControl *c)
{
	/* CreateBriefingPortrait 0x450080 */
	int32_t self = (int32_t)(c - ui->controls);
	KpUiBriefSlot *s = slot_of(ui, self);
	if (s == NULL) return;
	kp_ui_video_end(&s->video);
	kp_ui_image_forget(ui, s->bitmap.pixels);
	free(s->bitmap.pixels);
	s->bitmap.w = PORTRAIT_W;
	s->bitmap.h = PORTRAIT_H;
	s->bitmap.pixels = (uint8_t *)calloc(PORTRAIT_W * PORTRAIT_H, 1);
	s->flags = 0;
	s->portrait = 0;
	s->previous = 0; /* +0x10 is left uninitialized natively (atlas P17) */
	s->off_x = (uint8_t)(((c->right - c->left) - (PORTRAIT_W - 1)) / 2);
	s->off_y = (uint8_t)(((c->bottom - c->top) - (PORTRAIT_H - 1)) / 2);
	c->draw = slot_draw;
	(void)kp_ui_set_timer(&ui->timers, self, SLOT_TIMER_FRAMES, SLOT_PERIOD, kp_ui_callback(ui, slot_frames), ui->now);
	kp_ui_hide_control(ui, self);
}

static void slot_destroy(KpUi *ui, KpUiControl *c)
{
	/* DestroyBriefingPortrait 0x44ff70 */
	int32_t self = (int32_t)(c - ui->controls);
	KpUiBriefSlot *s = slot_of(ui, self);
	if (s != NULL) {
		kp_ui_video_end(&s->video);
		kp_ui_image_forget(ui, s->bitmap.pixels);
		free(s->bitmap.pixels);
		s->bitmap.pixels = NULL;
	}
	kp_ui_delete_timer(&ui->timers, self, SLOT_TIMER_FRAMES);
	kp_ui_delete_timer(&ui->timers, self, SLOT_TIMER_TALK);
}

static int slot_proc(KpUi *ui, KpUiControl *c, KpUiEvent *e)
{
	/* BriefingPortraitInteract 0x44ffd0 */
	if (e->type == KP_UI_USER && e->code == KP_UI_POST_LOAD) {
		slot_create(ui, c);
		return 1;
	}
	if (e->type == KP_UI_USER && e->code == KP_UI_DESTROY) slot_destroy(ui, c);
	return kp_ui_type_interact(c->type)(ui, c, e);
}

static void slot_hide(KpUi *ui, int32_t c)
{
	/* HideBriefingPortrait 0x450250 */
	KpUiBriefSlot *s;
	if (c < 0 || (s = slot_of(ui, c)) == NULL) return;
	kp_ui_video_end(&s->video);
	s->flags = 0;
	kp_ui_hide_control(ui, c);
}

static void slot_show(KpUi *ui, int32_t c, uint16_t portrait)
{
	/* ShowBriefingPortrait 0x450280 */
	KpUiBriefSlot *s;
	if (c < 0 || (s = slot_of(ui, c)) == NULL) return;
	kp_ui_video_end(&s->video);
	s->portrait = portrait;
	s->flags = SLOT_PENDING;
	slot_advance(ui, c);
}

static void slot_talk_for(KpUi *ui, int32_t c, uint32_t ms)
{
	/* StartBriefingTalking 0x4502c0 */
	KpUiBriefSlot *s;
	if (c < 0 || (s = slot_of(ui, c)) == NULL) return;
	s->flags |= SLOT_TALKING;
	(void)kp_ui_set_timer(&ui->timers, c, SLOT_TIMER_TALK, ms, kp_ui_callback(ui, slot_talk), ui->now);
}

/* --- the text of control -10 --- */

static void text_hide(KpUi *ui, int32_t owner)
{
	/* HideBriefingText 0x4610f0 (timer 1) */
	kp_ui_hide_control(ui, owner);
	kp_ui_delete_timer(&ui->timers, owner, TEXT_HIDE);
	kp_ui_delete_timer(&ui->timers, owner, TEXT_STEP);
	kp_ui_delete_timer(&ui->timers, owner, TEXT_DELAY);
}

static void text_step(KpUi *ui, int32_t owner)
{
	/* ScrollBriefingTextStep 0x4610b0 (timer 2) */
	ui->controls[owner].wuser = (uint16_t)(ui->controls[owner].wuser + 1);
	kp_ui_mark_dirty(ui, owner);
}

static void text_delay(KpUi *ui, int32_t owner)
{
	/* StartBriefingTextScroll 0x4610c0 (timer 3) */
	(void)kp_ui_set_timer(&ui->timers, owner, TEXT_STEP, ui->controls[owner].user, kp_ui_callback(ui, text_step), ui->now);
	kp_ui_delete_timer(&ui->timers, owner, TEXT_DELAY);
}

static void text_draw(KpUi *ui, KpUiControl *c, int32_t x, int32_t y, const int16_t clip[4])
{
	/* DrawBriefingText 0x460cc0: rows wUser.. of the text bitmap at (left, top), colour 0 transparent */
	KpUiSurface *t = ui->text.target, *b = &br(ui)->text;
	int32_t h = c->bottom - c->top + 1, i, j;
	(void)x;
	(void)y;
	if (t == NULL || b->pixels == NULL) return;
	for (j = 0; j < h; j++) {
		int32_t sy = c->wuser + j, ty = c->top + j;
		if (sy >= b->h || ty < clip[1] || ty > clip[3] || ty < 0 || ty >= t->h) continue;
		for (i = 0; i < b->w; i++) {
			int32_t tx = c->left + i;
			uint8_t v = b->pixels[(size_t)sy * b->w + i];
			if (v == 0 || tx < clip[0] || tx > clip[2] || tx < 0 || tx >= t->w) continue;
			t->pixels[(size_t)ty * t->w + tx] = v;
		}
	}
	if (c->wuser < b->h) { /* the rows drawn */
		KpUiSurface rows = *b;
		rows.pixels = b->pixels + (size_t)c->wuser * (size_t)b->w;
		rows.h = b->h - c->wuser < h ? b->h - c->wuser : h;
		erase_images(ui, t, &rows, c->left, c->top, clip, 1);
	}
}

static void text_show(KpUi *ui, const uint8_t *text, uint32_t duration)
{
	/* ShowBriefingText 0x460fa0 */
	KpUiBriefing *b = br(ui);
	KpUiText *t = &ui->text;
	KpUiSurface *saved = t->target;
	int32_t c = control_id(ui, ID_TEXT), h, height;
	if (text == NULL || c < 0 || b->text.pixels == NULL) return;
	memset(b->text.pixels, 0, (size_t)b->text.w * (size_t)b->text.h);
	t->target = &b->text;
	(void)kp_ui_text_select_font(t, ui->fonts[TEXT_FONT], ui->font_sizes[TEXT_FONT]);
	t->flags = TEXT_FORMAT;
	t->clip_left = 0;
	t->clip_top = 0;
	t->clip_right = (int16_t)(b->text.w - 1);
	t->clip_bottom = (int16_t)(b->text.h - 1);
	(void)kp_ui_draw_text(t, 0, 0, text);
	height = t->ext_bottom;
	(void)kp_ui_text_select_font(t, NULL, 0);
	t->target = saved;
	ui->controls[c].wuser = 0;
	kp_ui_show_control(ui, c);
	kp_ui_mark_dirty(ui, c);
	(void)kp_ui_set_timer(&ui->timers, c, TEXT_HIDE, duration, kp_ui_callback(ui, text_hide), ui->now);
	kp_ui_delete_timer(&ui->timers, c, TEXT_STEP);
	h = ui->controls[c].bottom - ui->controls[c].top;
	if ((int16_t)height > h) {
		int32_t half = (h + 1) / 2;
		uint32_t lines = (uint32_t)half > (uint32_t)(height - half) ? (uint32_t)half : (uint32_t)(height - half);
		if (lines == 0) lines = 1;
		if (duration > TEXT_DELAY_MS) {
			ui->controls[c].user = (duration - TEXT_DELAY_MS) / lines;
			(void)kp_ui_set_timer(&ui->timers, c, TEXT_DELAY, TEXT_DELAY_MS, kp_ui_callback(ui, text_delay), ui->now);
		} else {
			ui->controls[c].user = duration / lines;
			(void)kp_ui_set_timer(&ui->timers, c, TEXT_STEP, ui->controls[c].user, kp_ui_callback(ui, text_step),
				ui->now);
		}
	}
}

/* --- the actions (0x503908) --- */

static void wait_expired(KpUi *ui, int32_t owner)
{
	/* BriefingWaitTimerExpired 0x4611c0 */
	br(ui)->wait = 0;
	kp_ui_delete_timer(&ui->timers, owner, WAIT_TIMER);
}

static void wait_timer(KpUi *ui, uint32_t ms)
{
	/* StartBriefingWaitTimer 0x4611a0 */
	(void)kp_ui_set_timer(&ui->timers, br(ui)->dialog, WAIT_TIMER, ms, kp_ui_callback(ui, wait_expired), ui->now);
}

static void play_wav(KpUi *ui, const uint8_t *a)
{
	/* BriefingPlayWav 0x44fd10: the mission's WAV by its string (the host finds the file) */
	uint32_t wav = kp_le_u32(a + A_WAV);
	if (wav != 0) kp_ui_stream(ui, (uint16_t)(wav & 0xffff));
}

static int action(KpUi *ui, uint8_t *a)
{
	KpUiBriefing *b = br(ui);
	switch (a[A_ID]) {
	case 1: /* BriefingWait 0x44fcd0 */
		if (b->wait) return 0;
		if (a[A_FLAGS] & A_WAITED) {
			a[A_FLAGS] &= (uint8_t)~A_WAITED;
			return 1;
		}
		wait_timer(ui, kp_le_u32(a + A_TIME));
		a[A_FLAGS] |= A_WAITED;
		b->wait = 1;
		return 0;
	case 2: play_wav(ui, a); return 1;
	case 3: text_show(ui, map_string(ui, kp_le_u32(a + A_STRING)), kp_le_u32(a + A_TIME)); return 1;
	case 4: { /* BriefingSetObjectives 0x44fdc0 */
		int32_t c = control_id(ui, ID_OBJECTIVES);
		if (ui->game != NULL) kp_briefing_objectives(ui->game, kp_le_u32(a + A_STRING));
		if (c >= 0) {
			ui->controls[c].text = map_string(ui, kp_le_u32(a + A_STRING));
			kp_ui_show_control(ui, c);
			kp_ui_mark_dirty(ui, c);
		}
		return 1;
	}
	case 5: /* BriefingShowPortrait 0x44fde0 */
		slot_show(ui, control_id(ui, (int16_t)(ID_SLOT_FIRST + a[A_SLOT])),
			kp_ui_resolve_portrait(ui, (int64_t)(a[A_UNIT] | a[A_UNIT + 1] << 8)));
		return 1;
	case 6: slot_hide(ui, control_id(ui, (int16_t)(ID_SLOT_FIRST + a[A_SLOT]))); return 1;
	case 7: slot_talk_for(ui, control_id(ui, (int16_t)(ID_SLOT_FIRST + a[A_SLOT])), kp_le_u32(a + A_TIME)); return 1;
	case 8: { /* BriefingTransmission 0x44fe30 */
		uint32_t d = kp_le_u32(a + A_TIME), n = kp_le_u32(a + A_NUMBER);
		if (b->wait) return 0;
		if (a[A_FLAGS] & A_WAITED) {
			a[A_FLAGS] &= (uint8_t)~A_WAITED;
			return 1;
		}
		if (a[A_MODIFIER] == SET_TO) d = n;
		else if (a[A_MODIFIER] == ADD) d += n;
		else if (a[A_MODIFIER] == SUBTRACT) d = n > d ? 0 : d - n;
		play_wav(ui, a);
		text_show(ui, map_string(ui, kp_le_u32(a + A_STRING)), kp_le_u32(a + A_TIME));
		slot_talk_for(ui, control_id(ui, (int16_t)(ID_SLOT_FIRST + a[A_SLOT])), d);
		wait_timer(ui, d);
		a[A_FLAGS] |= A_WAITED;
		b->wait = 1;
		return 0;
	}
	case 9: /* BriefingEnableSkipTutorial 0x44fec0 */
		if (ui->game != NULL && kp_scalar(ui->game, KP_MISSION_INDEX) != 0) {
			int32_t c = control_id(ui, ID_SKIP), i = control_id(ui, ID_SKIP_IMAGE);
			if (c >= 0) kp_ui_show_control(ui, c);
			if (i >= 0) kp_ui_show_control(ui, i);
		}
		return 1;
	default: return 1; /* None 0x4b8e70 */
	}
}

static int run_record(KpUi *ui, uint8_t *r)
{
	/* RunBriefingTrigger 0x44fee0 */
	if (r[CONDITION_TYPE] != MISSION_BRIEFING) return 1;
	r[EXEC_FLAGS] |= EXEC_RUNNING;
	while (r[ACTION_INDEX] < ACTIONS) {
		uint8_t *a = r + RECORD_ACTIONS + ACTION_SIZE * r[ACTION_INDEX];
		int done;
		if (a[A_FLAGS] & A_DISABLED) {
			r[ACTION_INDEX]++;
			continue;
		}
		if (a[A_ID] == 0) {
			r[ACTION_INDEX] = ACTIONS;
			break;
		}
		done = action(ui, a);
		kp_ui_brief_log(ui, br(ui)->current, r[ACTION_INDEX], a[A_ID], done);
		if (!done) break;
		r[ACTION_INDEX]++;
	}
	if (r[ACTION_INDEX] < ACTIONS) return 0;
	r[EXEC_FLAGS] &= (uint8_t)~EXEC_RUNNING;
	r[ACTION_INDEX] = 0;
	return 1;
}

static void tick(KpUi *ui, int32_t owner)
{
	/* BriefingTick 0x460ea0 */
	KpUiBriefing *b = br(ui);
	(void)owner;
	if (b->current < 0 || ui->game == NULL) return;
	if (run_record(ui, kp_briefing_record(ui->game, b->current))) {
		b->current = b->current + 1 < kp_briefing_count(ui->game) ? b->current + 1 : -1;
	}
}

static void stop_streams_and_slots(KpUi *ui)
{
	int k;
	kp_ui_stop_streams(ui);
	for (k = 0; k < SLOTS; k++) slot_hide(ui, control_id(ui, (int16_t)(ID_SLOT_FIRST + k)));
}

static void restart(KpUi *ui)
{
	/* RestartBriefing 0x460dd0 */
	KpUiBriefing *b = br(ui);
	int32_t c = control_id(ui, ID_TEXT), o = control_id(ui, ID_OBJECTIVES);
	stop_streams_and_slots(ui);
	if (c >= 0) {
		ui->controls[c].text = NULL;
		kp_ui_hide_control(ui, c);
		kp_ui_delete_timer(&ui->timers, c, TEXT_HIDE);
		kp_ui_delete_timer(&ui->timers, c, TEXT_STEP);
		kp_ui_delete_timer(&ui->timers, c, TEXT_DELAY);
	}
	if (o >= 0) kp_ui_hide_control(ui, o);
	b->wait = 0;
	kp_ui_delete_timer(&ui->timers, b->dialog, WAIT_TIMER);
	if (b->current >= 0 && ui->game != NULL) {
		uint8_t *r = kp_briefing_record(ui->game, b->current);
		if (r[ACTION_INDEX] < ACTIONS) r[RECORD_ACTIONS + ACTION_SIZE * r[ACTION_INDEX] + A_FLAGS] &= (uint8_t)~A_WAITED;
		r[EXEC_FLAGS] &= (uint8_t)~EXEC_RUNNING;
		r[ACTION_INDEX] = 0;
	}
	b->current = ui->game != NULL && kp_briefing_count(ui->game) > 0 ? 0 : -1;
}

static void stop(KpUi *ui)
{
	/* StopBriefing 0x460f00 */
	KpUiBriefing *b = br(ui);
	int32_t c = control_id(ui, ID_TEXT), o = control_id(ui, ID_OBJECTIVES);
	if (c >= 0) {
		kp_ui_delete_timer(&ui->timers, c, TEXT_HIDE);
		kp_ui_delete_timer(&ui->timers, c, TEXT_STEP);
		kp_ui_delete_timer(&ui->timers, c, TEXT_DELAY);
	}
	kp_ui_delete_timer(&ui->timers, b->dialog, TICK_TIMER);
	kp_ui_delete_timer(&ui->timers, b->dialog, WAIT_TIMER);
	kp_ui_stop_streams(ui);
	if (c >= 0) kp_ui_hide_control(ui, c);
	if (o >= 0) kp_ui_hide_control(ui, o);
	{
		int k;
		for (k = 0; k < SLOTS; k++) slot_hide(ui, control_id(ui, (int16_t)(ID_SLOT_FIRST + k)));
	}
}

static int load_frame(KpUi *ui, const char *path, KpUiSurface *s)
{
	const KpUiFile *f = kp_ui_glue_file(ui, path);
	int32_t w, h;
	free(s->pixels);
	s->pixels = NULL;
	if (f == NULL || kp_ui_pcx(f->data, f->size, &w, &h, NULL, 0) != 0) return -1;
	s->pixels = (uint8_t *)malloc((size_t)w * (size_t)h);
	if (s->pixels == NULL || kp_ui_pcx(f->data, f->size, &w, &h, s->pixels, (size_t)w * (size_t)h) != 0) return -1;
	s->w = w;
	s->h = h;
	kp_ui_picture(ui, s->pixels, w, h, f); /* the HD interface's (ADR 0013 R4d) */
	return 0;
}

static void init_briefing(KpUi *ui, int32_t dialog)
{
	/* InitReadyRoomBriefing 0x460a70 and LoadBriefingPortraitFrames 0x4501f0 */
	KpUiBriefing *b = br(ui);
	int32_t c, k;
	char path[64];
	b->dialog = dialog;
	c = control_id(ui, ID_TEXT);
	if (c >= 0) {
		KpUiControl *t = &ui->controls[c];
		kp_ui_image_forget(ui, b->text.pixels);
		free(b->text.pixels);
		b->text.w = (uint16_t)(t->right - t->left + 1);
		b->text.h = (uint16_t)((t->bottom - t->top + 1) * TEXT_HEIGHTS);
		b->text.pixels = (uint8_t *)calloc((size_t)b->text.w * (size_t)b->text.h != 0 ? (size_t)b->text.w * b->text.h : 1, 1);
		t->draw = text_draw;
	}
	(void)kp_ui_set_timer(&ui->timers, dialog, TICK_TIMER, TICK_PERIOD, kp_ui_callback(ui, tick), ui->now);
	restart(ui);
	if (ui->game == NULL || kp_briefing_count(ui->game) == 0) {
		int32_t none = control_id(ui, ID_NO_BRIEFING), replay = control_id(ui, ID_REPLAY);
		if (none >= 0) kp_ui_show_control(ui, none);
		if (replay >= 0) {
			ui->controls[replay].flags |= KP_UI_DISABLED;
			kp_ui_mark_dirty(ui, replay);
		}
	}
	for (k = 0; k < SLOTS; k++) {
		snprintf(path, sizeof path, "glue\\Ready%c\\%cFrame%d.pcx", LETTERS[b->race], LETTERS[b->race], (int)k + 1);
		(void)load_frame(ui, path, &b->frames[k]);
		snprintf(path, sizeof path, "glue\\Ready%c\\%cFrameH%d.pcx", LETTERS[b->race], LETTERS[b->race], (int)k + 1);
		(void)load_frame(ui, path, &b->talking_frames[k]);
	}
}

static void destroy(KpUi *ui)
{
	/* DestroyReadyRoom 0x460c30 */
	KpUiBriefing *b = br(ui);
	stop(ui);
	kp_ui_image_forget(ui, b->text.pixels);
	free(b->text.pixels);
	b->text.pixels = NULL;
	b->wait = 0;
	b->dialog = -1;
}

static int button(KpUi *ui, KpUiControl *d)
{
	/* ReadyRoomButton 0x460b70 */
	KpUiBriefing *b = br(ui);
	int32_t start = control_id(ui, ID_START);
	switch (ui->result) {
	case ID_START:
	case ID_CANCEL:
		b->current = -1;
		if (start >= 0) {
			ui->controls[start].flags |= KP_UI_DISABLED;
			kp_ui_mark_dirty(ui, start);
		}
		if (ui->result == ID_START) {
			b->ready = 1; /* 0x475cc0(0): the command 0x54 the next turn step sends */
			return 1;
		}
		b->polling = 0;
		/* fall through - as the other results */
	default:
		stop(ui);
		kp_ui_glue_slide_out(ui, (int32_t)(d - ui->controls));
		return 1;
	case ID_REPLAY:
		restart(ui);
		return 1;
	}
}

static int room_proc(KpUi *ui, KpUiControl *d, KpUiEvent *e)
{
	/* the rooms' procedures 0x461320, 0x461560, 0x4617a0 */
	KpUiBriefing *b = br(ui);
	int32_t self = (int32_t)(d - ui->controls);
	KpUiInteract base = kp_ui_type_interact(0);
	if (e->type == KP_UI_USER) {
		switch (e->code) {
		case KP_UI_CREATE: kp_ui_glue_activate(ui, self, THEMES[b->race]); break;
		case KP_UI_DESTROY: destroy(ui); break;
		case KP_UI_CLOSE: return button(ui, d);
		case KP_UI_POST_LOAD: {
			KpUiInteract procs[PROCS];
			int k;
			b->polling = 1; /* ReadyRoomPostLoad 0x460c80: the idle callback PollBriefingReady 0x461120 */
			b->ready_tick = ui->now - TURN_MS; /* 0x6737ec: the first idle event steps */
			b->sent = 0;
			kp_ui_glue_slide_in(ui, self, SLIDE_TABLE, SLIDES, SLIDE_DELAY);
			memset(procs, 0, sizeof procs);
			if (b->race == 0) procs[ID_ANIMATION - 1] = kp_ui_flc_animation_proc;
			procs[ID_START - 1] = kp_ui_flc_button_proc;
			procs[ID_CANCEL - 1] = kp_ui_text_button_proc;
			for (k = 0; k < SLOTS; k++) procs[ID_SLOT_FIRST - 1 + k] = slot_proc;
			procs[ID_SKIP - 1] = kp_ui_text_button_proc;
			procs[ID_REPLAY - 1] = kp_ui_text_button_proc;
			kp_ui_glue_init_dialog(ui, self, procs, PROCS, THEMES[b->race]);
			break;
		}
		case SLIDE_DONE: init_briefing(ui, self); break;
		default: break;
		}
	}
	return base(ui, d, e);
}

static int32_t open_room(KpUi *ui)
{
	KpUiBriefing *b = br(ui);
	int32_t d;
	b->stage = KP_UI_ROOM_OPEN;
	kp_ui_glue_sounds(ui);
	d = kp_ui_load_dialog(ui, BINS[b->race]);
	if (d < 0) return -1;
	b->dialog = d;
	kp_ui_init_dialog(ui, d, room_proc);
	return d;
}

int kp_ui_ready_room_open(KpUi *ui, int race)
{
	/* RunTerranReadyRoom 0x461420 and its twins: the music, the mission's establishing text (0x414f70: offline, without
	 * the noglues cheat, a campaign mission's), then the room (kp_ui_ready_room_continue) */
	KpUiBriefing *b = br(ui);
	const char *text = NULL;
	if (race < 0 || race > 2) return -1;
	b->race = race;
	b->dialog = -1;
	b->current = -1;
	b->ready = 0;
	b->polling = 0;
	b->result = 0;
	b->log_count = 0;
	kp_ui_music_stop(ui);
	kp_ui_music_play(ui, MUSIC[race]);
	if (ui->game != NULL && !(kp_scalar(ui->game, KP_CHEATS) & CHEAT_NOGLUES)) {
		int64_t key = kp_scalar(ui->game, KP_MISSION_INDEX);
		if (key != KP_NIL) text = kp_ui_establishing_text(ui, (uint16_t)key, 0);
	}
	if (text != NULL && kp_ui_credits_open(ui, text) >= 0) {
		b->stage = KP_UI_ROOM_TEXT;
		return 0;
	}
	return open_room(ui) >= 0 ? 0 : -1;
}

int32_t kp_ui_ready_room_continue(KpUi *ui)
{
	KpUiBriefing *b = br(ui);
	if (b->stage != KP_UI_ROOM_TEXT || !ui->credits.done) return b->stage == KP_UI_ROOM_OPEN ? b->dialog : -1;
	kp_ui_destroy_dialog(ui, ui->credits.dialog);
	ui->credits.dialog = -1;
	return open_room(ui);
}

void kp_ui_ready_room_idle(KpUi *ui)
{
	/* PollBriefingReady 0x461120 offline: a network turn step every 250 ms of idle events (0x476b20; the turns come
	 * back at once, refreshing 0x6737ec); Start's command 0x54 goes out with a step and its ready byte arrives with the
	 * next, which answers 101 */
	KpUiBriefing *b = br(ui);
	if (!b->polling || b->dialog < 0 || ui->now - b->ready_tick < TURN_MS) return;
	kp_ui_ready_room_turn(ui);
}

void kp_ui_ready_room_turn(KpUi *ui)
{
	KpUiBriefing *b = br(ui);
	if (!b->polling || b->dialog < 0) return;
	b->ready_tick = ui->now;
	if (b->sent) {
		b->polling = 0;
		b->sent = 0;
		ui->result = RESULT_START;
		(void)kp_ui_send_user(ui, b->dialog, KP_UI_CLOSE, 0);
		return;
	}
	if (b->ready) {
		b->ready = 0;
		b->sent = 1;
	}
}

void kp_ui_brief_log(KpUi *ui, int64_t record, int index, int type, int result)
{
	KpUiBriefing *b = br(ui);
	if (b->log_count >= KP_UI_BRIEF_LOG) return;
	b->log[b->log_count].record = (int32_t)record;
	b->log[b->log_count].index = (uint8_t)index;
	b->log[b->log_count].type = (uint8_t)type;
	b->log[b->log_count].result = (uint8_t)result;
	b->log[b->log_count].tick = ui->now;
	b->log_count++;
}

void kp_ui_ready_room_free(KpUi *ui)
{
	KpUiBriefing *b = br(ui);
	int k;
	free(b->text.pixels);
	b->text.pixels = NULL;
	for (k = 0; k < SLOTS; k++) {
		kp_ui_video_end(&b->slots[k].video);
		free(b->slots[k].bitmap.pixels);
		free(b->frames[k].pixels);
		free(b->talking_frames[k].pixels);
		b->slots[k].bitmap.pixels = b->frames[k].pixels = b->talking_frames[k].pixels = NULL;
	}
}
