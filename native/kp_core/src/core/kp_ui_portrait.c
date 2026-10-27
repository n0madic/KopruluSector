/* The console's portrait (plan phase 9B; statport.cpp; atlas P36 0x4a65d0..0x4a7420, docs/research/native-audiovisual.md):
 * the status unit's idle and talking movies (SMK, played as SVid does through Smackw32: SVidPlayBegin 454,
 * SVidPlayContinueSingle 457 with SmackWait's timing, SVidPlayEnd 458), the variants the portrait RNG 0x4a6e00 draws,
 * the fade-in static, the console's overlay (game\<race>conover.pcx) and the timers 1 (50 ms, the frames), 2 (30 ms,
 * the fade) and 3 (the end of a talk). */
#include "kp_ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "kp_game.h"
#include "kp_ids.h"

#define TIMER_FRAMES 1
#define TIMER_FADE 2
#define TIMER_TALK 3
#define TIMER_TRIGGER 4
#define FRAMES_PERIOD 50
#define FADE_PERIOD 30
#define FADE_DONE 80
#define FADE_FRESH 50
#define FADE_STEP 10
#define PORTRAIT_NONE 0xffff
#define PORTRAIT_OVERMIND 38
#define PORTRAIT_ADVISOR 60
#define PORTRAIT_PLAYER_COLOUR 82  /* the players' colour portraits 82..89, then 98.. by the colour rows */
#define PLAYER_COLOUR_ROWS 0x62
#define MODE_ANIMATED 2
#define IDLE_DRAW_HIGH 100
#define IDLE_MASK 0x80
#define TALK_DRAW_HIGH 2
#define TALK_MASK 4
#define STATIC_COLOURS 16
#define BLACK 15               /* 0x6d2377 = 0x6d2368[15] */
#define C_HOTKEYS -10
#define VARIANTS 4
#define SMK_NONE 0xffff
#define SOUNDS 1144
#define SECOND 1000
#define SOUND_RECORD 22 /* sounds.kpd: u16 flags, format, channels, u32 rate, average bytes per second, u16 block, bits, u32 length */
#define SOUND_PARSED 0x02
#define EVENT_HALF_W 320 /* the last transmission's screen: the sprite at the centre of the original's 640 x 400 view */
#define EVENT_HALF_H 200
#define TALK_BLINK 8     /* the minimap blink's draws */

static const uint16_t RACE_PORTRAITS[3] = { 38, 17, 60 }; /* 0x50d7c0 */
static const char RACE_LETTERS[3] = { 'z', 't', 'p' };

static uint32_t rd16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }

/* --- SVid --- */

void kp_ui_video_end(KpUiVideo *v)
{
	if (v->open) kp_smk_close(&v->smk);
	v->open = 0;
	free(v->frame);
	v->frame = NULL;
	v->zoom = NULL;
}

uint32_t kp_ui_video_clock(KpUi *ui)
{
	if (ui->video_ticks != NULL && ui->video_tick_next < ui->video_tick_count) return ui->video_ticks[ui->video_tick_next++];
	return ui->now;
}

int kp_ui_video_begin_zoomed(KpUi *ui, KpUiVideo *v, const KpUiFile *file, uint8_t *target)
{
	uint8_t *frame;
	if (kp_ui_video_begin(ui, v, file, NULL) != 0) return -1;
	frame = (uint8_t *)calloc((size_t)v->smk.width * v->smk.height != 0 ? (size_t)v->smk.width * v->smk.height : 1, 1);
	if (frame == NULL) {
		kp_ui_video_end(v);
		return -1;
	}
	v->frame = frame;
	v->zoom = target;
	kp_smk_set_target(&v->smk, frame);
	return 0;
}

/* The scanline zoom of SVid (the SCode scalers without full lines or horizontal blend): each pixel twice across,
 * into the even rows. */
static void zoom_frame(KpUiVideo *v)
{
	uint32_t w = v->smk.width, h = v->smk.height, x, y;
	for (y = 0; y < h; y++) {
		const uint8_t *s = v->frame + (size_t)y * w;
		uint8_t *d = v->zoom + (size_t)(2 * y) * (2 * w);
		for (x = 0; x < w; x++) d[2 * x] = d[2 * x + 1] = s[x];
	}
}

int kp_ui_video_begin(KpUi *ui, KpUiVideo *v, const KpUiFile *file, uint8_t *target)
{
	kp_ui_video_end(v);
	v->path = NULL;
	v->path_len = 0;
	v->shown = -1;
	if (file != NULL) kp_ui_video_trace(ui, file->path, file->path_len);
	if (file == NULL || kp_smk_open(&v->smk, file->data, file->size) != 0) return -1;
	v->path = file->path;
	v->path_len = file->path_len;
	kp_smk_set_target(&v->smk, target);
	v->next = 0xffffffffu; /* SmackOpen */
	v->period = kp_smk_frame_time(&v->smk);
	v->ready = 0;
	v->loop = 0;
	v->zoom = v->frame = NULL;
	v->open = 1;
	return 0;
}

/* SmackWait (Smackw32 0x10007720) without a sound track: nonzero while the next frame is not due. */
static int smack_wait(KpUiVideo *v, uint32_t now_ms)
{
	uint32_t now = now_ms * 100u, next;
	if (v->ready) return 0;
	if (v->next == 0xffffffffu) {
		v->next = v->period + now;
		return 1;
	}
	if (v->next > now) return 1;
	next = v->next + v->period;
	v->next = next;
	if (now - next > v->period) v->next = now + v->period;
	if (v->period != 0) v->ready = 1;
	return 0;
}

int kp_ui_video_continue(KpUiVideo *v, int force, int *updated, uint32_t now)
{
	uint32_t frame;
	if (updated != NULL) *updated = 0;
	if (!v->open) return 0;
	if (!force && smack_wait(v, now)) return 1;
	frame = v->smk.frame;
	if (kp_smk_decode(&v->smk) == 0) { /* SmackDoFrame into the SmackToBuffer bitmap */
		if (updated != NULL) *updated = 1;
		v->shown = (int32_t)frame;
		if (v->zoom != NULL) zoom_frame(v);
	}
	if (frame + 1 >= v->smk.frames) {
		if (!v->loop) { /* the last frame: SVidPlayEnd */
			kp_ui_video_end(v);
			return 0;
		}
	}
	v->ready = 0; /* SmackNextFrame */
	return 1;
}

/* --- portraits.kpd --- */

int kp_ui_set_portraits(KpUi *ui, const uint8_t *kpd, size_t size)
{
	KpError e;
	const KpKpfSection *rows, *files;
	const uint8_t *p;
	int i, n;
	kp_kpf_free(&ui->portraits_kpf); /* the section table of an earlier call */
	if (kp_kpf_parse(&ui->portraits_kpf, kpd, size, "portraits.kpd", 0, &e) != 0) {
		snprintf(ui->error, sizeof ui->error, "%s", e.msg);
		return -1;
	}
	rows = kp_kpf_find(&ui->portraits_kpf, "ROWS");
	files = kp_kpf_find(&ui->portraits_kpf, "FILE");
	if (rows == NULL || files == NULL || rows->size < 2 || files->size < 2) {
		snprintf(ui->error, sizeof ui->error, "portraits.kpd lacks ROWS or FILE");
		return -1;
	}
	ui->portrait_row_count = (int)rd16(rows->data);
	ui->portrait_rows = rows->data + 2;
	n = (int)rd16(files->data);
	free(ui->portrait_files);
	ui->portrait_files = (KpUiFile *)calloc((size_t)n + 1, sizeof(KpUiFile));
	if (ui->portrait_files == NULL) return -1;
	for (i = 0, p = files->data + 2; i < n; i++) {
		size_t len = p[0];
		uint32_t bytes = (uint32_t)p[1 + len] | (uint32_t)p[2 + len] << 8 | (uint32_t)p[3 + len] << 16
			| (uint32_t)p[4 + len] << 24;
		ui->portrait_files[i].path = (const char *)p + 1;
		ui->portrait_files[i].path_len = len;
		ui->portrait_files[i].data = p + 5 + len;
		ui->portrait_files[i].size = bytes;
		p += 5 + len + bytes;
	}
	ui->portrait_file_count = n;
	return 0;
}

/* The movie "portrait\<stem><variant>.smk" of a portdata row (talking or idle), or NULL when the file does not exist
 * (portraits.kpd ROWS: the first SMKS entry and the mask of the existing variant digits). */
const KpUiFile *kp_ui_portrait_movie(KpUi *ui, uint32_t row, int talking, int variant)
{
	const uint8_t *r;
	uint32_t first, mask, index;
	int d;
	if (row >= (uint32_t)ui->portrait_row_count || variant < 0 || variant > 15) return NULL;
	r = ui->portrait_rows + 8 * row + (talking ? 4 : 0);
	first = rd16(r);
	mask = rd16(r + 2);
	if (first == SMK_NONE || !(mask & (1u << variant))) return NULL;
	index = first;
	for (d = 0; d < variant; d++) {
		if (mask & (1u << d)) index++;
	}
	return index < (uint32_t)ui->portrait_file_count ? &ui->portrait_files[index] : NULL;
}

int kp_ui_portrait_movie_name(KpUi *ui, uint32_t row, int talking, int variant, char *out, size_t size)
{
	const uint8_t *r;
	const KpUiFile *f;
	uint32_t first;
	if (row >= (uint32_t)ui->portrait_row_count || size == 0) return -1;
	r = ui->portrait_rows + 8 * row + (talking ? 4 : 0);
	first = rd16(r);
	if (first == SMK_NONE || first >= (uint32_t)ui->portrait_file_count) return -1;
	f = &ui->portrait_files[first];
	if (f->path_len < 5 || f->path_len >= size) return -1;
	memcpy(out, f->path, f->path_len);
	out[f->path_len] = 0;
	out[f->path_len - 5] = (char)('0' + variant % 10); /* "...<digit>.smk" */
	return 0;
}

void kp_ui_video_trace(KpUi *ui, const char *path, size_t len)
{
	if (!ui->video_trace || ui->video_count >= KP_UI_VIDEO_TRACE) return;
	if (len >= KP_UI_VIDEO_NAME) len = KP_UI_VIDEO_NAME - 1;
	memcpy(ui->videos[ui->video_count], path, len);
	ui->videos[ui->video_count][len] = 0;
	ui->video_count++;
}

int kp_ui_set_sounds(KpUi *ui, const uint8_t *kpd, size_t size)
{
	KpError e;
	const KpKpfSection *sfx;
	kp_kpf_free(&ui->sounds_kpf); /* the section table of an earlier call */
	if (kp_kpf_parse(&ui->sounds_kpf, kpd, size, "sounds.kpd", 0, &e) != 0) {
		snprintf(ui->error, sizeof ui->error, "%s", e.msg);
		return -1;
	}
	sfx = kp_kpf_find(&ui->sounds_kpf, "SFX ");
	if (sfx == NULL || sfx->size < 2 || 2 + SOUND_RECORD * (size_t)rd16(sfx->data) > sfx->size) {
		snprintf(ui->error, sizeof ui->error, "sounds.kpd lacks SFX");
		return -1;
	}
	ui->sound_count = (int)rd16(sfx->data);
	ui->sounds = sfx->data + 2;
	{
		const KpKpfSection *musl = kp_kpf_find(&ui->sounds_kpf, "MUSL");
		if (musl != NULL) kp_ui_music_lengths(ui, musl->data, musl->size);
	}
	return 0;
}

uint32_t kp_ui_sound_duration(const KpUi *ui, uint32_t sound)
{
	const uint8_t *r;
	uint32_t avg, length;
	if (ui->sounds == NULL || sound >= (uint32_t)ui->sound_count) return 0;
	r = ui->sounds + SOUND_RECORD * (size_t)sound;
	if (!(rd16(r) & SOUND_PARSED)) return 0;
	avg = rd16(r + 10) | rd16(r + 12) << 16;
	length = rd16(r + 18) | rd16(r + 20) << 16;
	return avg ? (uint32_t)(length * 1000u) / avg : 0;
}

/* --- the portrait --- */

static KpUiControl *control(KpUi *ui) { return &ui->controls[ui->portrait.control]; }

static int has_control(const KpUi *ui) { return ui->portrait.control >= 0; }

/* ResolveUnitPortrait 0x4a65d0. */
uint16_t kp_ui_resolve_portrait(KpUi *ui, int64_t type)
{
	KpGame *g = ui->game;
	uint16_t p = (uint16_t)KP_DAT(g, units, portrait, type);
	uint16_t mission = (uint16_t)kp_scalar(g, KP_MISSION_INDEX);
	if (p == PORTRAIT_OVERMIND && mission > 31) return (uint16_t)KP_DAT(g, units, portrait, KP_TYPE_DAGGOTH);
	if (p == PORTRAIT_ADVISOR && mission >= 28) return (uint16_t)KP_DAT(g, units, portrait, KP_TYPE_FENIX_DRAGOON);
	return p;
}

static int64_t displayed_type(KpGame *g, int64_t n)
{
	/* GetDisplayedUnitType 0x428b20 */
	if (kp_is_morphing_zerg_building(g, n)) {
		const KpUnits *u = &g->u;
		switch (u->queue_slot[n] % 5) {
		case 0: return u->queue0[n];
		case 1: return u->queue1[n];
		case 2: return u->queue2[n];
		case 3: return u->queue3[n];
		default: return u->queue4[n];
		}
	}
	return g->u.type[n];
}

static void fill(KpUi *ui, KpUiSurface *s, uint8_t v)
{
	int32_t all[4];
	if (s->pixels == NULL) return;
	memset(s->pixels, v, (size_t)s->w * (size_t)s->h);
	all[0] = all[1] = 0;
	all[2] = s->w;
	all[3] = s->h;
	kp_ui_image_erase(ui, s, all, NULL, 0, 0); /* the HD interface's images there go (ADR 0013 R4d) */
}

static void open_movie(KpUi *ui, uint32_t row, int talking, int variant)
{
	KpUiPortrait *p = &ui->portrait;
	if (p->mode == 0) return;
	p->first = 1;
	(void)kp_ui_video_begin(ui, &p->video, kp_ui_portrait_movie(ui, row, talking, variant), p->bitmap.pixels);
}

/* OpenIdlePortrait 0x4a6b40. */
static void open_idle(KpUi *ui, uint32_t row)
{
	KpGame *g = ui->game;
	KpUiPortrait *p = &ui->portrait;
	int k = 0;
	if (p->mode == MODE_ANIMATED) {
		uint8_t v = (uint8_t)kp_rng_portrait_draw(&p->rng, 0, IDLE_DRAW_HIGH, IDLE_MASK);
		uint8_t sum = (uint8_t)KP_DAT(g, portdata, idle_smk_change, row);
		for (;;) {
			if (v <= sum) break;
			if (++k >= VARIANTS) {
				k = 0;
				break;
			}
			switch (k) { /* the four weight arrays from 0x51c0b0 */
			case 1: sum = (uint8_t)(sum + KP_DAT(g, portdata, talking_smk_change, row)); break;
			case 2: sum = (uint8_t)(sum + KP_DAT(g, portdata, idle_unknown, row)); break;
			default: sum = (uint8_t)(sum + KP_DAT(g, portdata, talking_unknown, row)); break;
			}
		}
	}
	for (; !p->video.open && k >= 0; k--) open_movie(ui, row, 0, k);
	kp_ui_mark_dirty(ui, p->control);
}

/* OpenTalkingPortrait 0x4a6c50. */
static void open_talking(KpUi *ui, uint32_t row)
{
	KpUiPortrait *p = &ui->portrait;
	int8_t k = 0;
	if (p->mode == MODE_ANIMATED) {
		uint8_t d = (uint8_t)kp_rng_portrait_draw(&p->rng, 0, TALK_DRAW_HIGH, TALK_MASK);
		k = (int8_t)(d == p->previous_talking ? (uint8_t)((d + 2) % 3) : d);
	}
	while (!p->video.open) {
		open_movie(ui, row, 1, k);
		k = (int8_t)(k - 1);
		if (k < 0) break;
	}
	p->previous_talking = (uint8_t)k;
	kp_ui_mark_dirty(ui, p->control);
}

static void show_idle(KpUi *ui, int64_t unit);

void kp_ui_portrait_set_mode(KpUi *ui, uint32_t mode)
{
	/* SetPortraitMode 0x4a6e90: with a console and a status unit its idle portrait again in the new mode
	 * (ShowPortrait 0x4a6f10(1)) */
	ui->portrait.mode = mode > MODE_ANIMATED ? MODE_ANIMATED : mode;
	if (has_control(ui) && ui->game != NULL && ui->hud.status_unit != 0) show_idle(ui, ui->hud.status_unit);
}

/* ShowPortrait 0x4a6f10. */
static void show_portrait(KpUi *ui, int64_t unit, uint16_t portrait, int talking)
{
	KpUiPortrait *p = &ui->portrait;
	KpUiControl *c = control(ui);
	kp_ui_video_end(&p->video);
	if (portrait == PORTRAIT_NONE) {
		c->user = 0;
		c->slot.unit = 0;
		c->wuser = PORTRAIT_NONE;
		kp_ui_hide_control(ui, p->control);
		return;
	}
	if (portrait == PORTRAIT_PLAYER_COLOUR && unit != 0) {
		/* the speaker's sprite owner's colour (MapPlayerColor 0x41d560: the slots as the players in a single game's
		 * 0x5264fc) and the row the map's colours moved it to (0x536722): 0x62 on, else 0x52 on */
		int32_t colour = (int32_t)ui->game->w->spr.owner[kp_unit_sprite(ui->game, unit)];
		if (colour >= 8) portrait = (uint16_t)(colour + PLAYER_COLOUR_ROWS);
		else if (ui->minimap.colour_table[colour] != 0) portrait = (uint16_t)(ui->minimap.colour_table[colour] + PLAYER_COLOUR_ROWS);
		else portrait = (uint16_t)(colour + PORTRAIT_PLAYER_COLOUR);
	}
	if (c->slot.unit != unit || c->wuser != portrait) {
		fill(ui, &p->bitmap, ui->ui_colours[BLACK]);
		kp_ui_show_control(ui, p->control);
		p->fade = 0;
		p->fresh = 1;
		c->user = (uint32_t)unit;
		c->slot.unit = unit;
		c->wuser = portrait;
	}
	if (talking) {
		open_talking(ui, portrait);
		p->talking = 1;
	} else {
		open_idle(ui, portrait);
		p->talking = 0;
	}
}

static void clear_portrait(KpUi *ui)
{
	KpUiControl *c = control(ui);
	c->user = 0;
	c->slot.unit = 0;
	c->wuser = PORTRAIT_NONE;
	kp_ui_hide_control(ui, ui->portrait.control);
}

static void show_idle(KpUi *ui, int64_t unit)
{
	show_portrait(ui, unit, kp_ui_resolve_portrait(ui, displayed_type(ui->game, unit)), 0);
}

/* ContinuePortraitVideo 0x4a6700. */
static int continue_video(KpUi *ui, int *updated)
{
	KpUiPortrait *p = &ui->portrait;
	int r;
	if (p->mode == 0) return 0;
	if (p->mode != MODE_ANIMATED && p->first == 0) return 1;
	r = kp_ui_video_continue(&p->video, 0, updated, ui->now);
	if (r == 0) return 0;
	if (p->first && *updated) p->first--;
	return 1;
}

/* PortraitTimerCallback 0x4a7030 (the timer ID from the control's timer: the callback runs for each of 1, 2, 3). */
static void portrait_timer(KpUi *ui, int32_t owner, uint16_t id)
{
	KpUiPortrait *p = &ui->portrait;
	KpUiControl *c = &ui->controls[owner];
	int updated = 0;
	if (id == TIMER_FRAMES) {
		if (!(c->flags & KP_UI_VISIBLE) || !p->video.open) return;
		if (continue_video(ui, &updated) == 0) {
			if (!p->talking) show_idle(ui, ui->hud.status_unit);
			else show_portrait(ui, c->slot.unit, c->wuser, 1);
		}
		if (p->fresh) {
			if (!updated) return;
			p->fresh = 0;
			p->fade = FADE_FRESH;
		}
	} else if (id == TIMER_FADE) {
		if (!(c->flags & KP_UI_VISIBLE) || !p->video.open) return;
		if (!p->fresh) {
			if (p->fade >= FADE_DONE) return;
			p->fade = (uint8_t)(p->fade + FADE_STEP);
		}
		updated = 1;
	} else if (id == TIMER_TALK) {
		p->talking = 0;
		kp_ui_delete_timer(&ui->timers, owner, TIMER_TALK);
		kp_ui_video_end(&p->video);
		if (ui->hud.status_unit == 0) clear_portrait(ui);
		else show_idle(ui, ui->hud.status_unit);
	} else {
		return;
	}
	if (updated) kp_ui_mark_dirty(ui, owner);
}

/* TalkingDurationPending 0x4a7190: the sound's whole seconds once it is loaded. */
static void timer_talk_pending(KpUi *ui, int32_t owner);

static void timer_frames(KpUi *ui, int32_t owner) { portrait_timer(ui, owner, TIMER_FRAMES); }
static void timer_fade(KpUi *ui, int32_t owner) { portrait_timer(ui, owner, TIMER_FADE); }
static void timer_talk(KpUi *ui, int32_t owner) { portrait_timer(ui, owner, TIMER_TALK); }

static void timer_talk_pending(KpUi *ui, int32_t owner)
{
	uint32_t d = kp_ui_sound_duration(ui, ui->portrait.talk_sound) / SECOND * SECOND; /* 0x446740 */
	if (d > SECOND) {
		(void)kp_ui_set_timer(&ui->timers, owner, TIMER_TALK, d - SECOND, kp_ui_callback(ui, timer_talk), ui->now);
		return;
	}
	ui->portrait.talking = 0;
	kp_ui_delete_timer(&ui->timers, owner, TIMER_TALK);
	if (ui->hud.status_unit == 0) clear_portrait(ui);
}

/* DrawPortraitStatic 0x4a6610: the noise where the RNG's 0..100 is at least the fade. */
static void draw_static(KpUi *ui, uint8_t threshold, const KpUiControl *c)
{
	KpUiSurface *t = ui->text.target;
	int32_t x, y;
	ui->portrait.static_draws++;
	for (y = c->top; y <= c->bottom; y++) {
		for (x = c->left; x <= c->right; x++) {
			uint8_t v = (uint8_t)kp_rng_portrait_draw(&ui->portrait.rng, 0, IDLE_DRAW_HIGH, 0x80);
			if (v >= threshold) {
				uint8_t k = (uint8_t)kp_rng_portrait_draw(&ui->portrait.rng, 0, STATIC_COLOURS - 1, STATIC_COLOURS);
				if (t && x >= 0 && x < t->w && y >= 0 && y < t->h) {
					int32_t at[4];
					t->pixels[(int64_t)y * t->w + x] = ui->ui_colours[k];
					at[0] = x;
					at[1] = y;
					at[2] = x + 1;
					at[3] = y + 1;
					kp_ui_image_erase(ui, t, at, NULL, 0, 0); /* the HD portrait loses the noise's pixel */
				}
			}
		}
	}
}

/* DrawPortrait 0x4a6970. */
static void draw_portrait(KpUi *ui, KpUiControl *c, int32_t x, int32_t y, const int16_t clip[4])
{
	KpUiPortrait *p = &ui->portrait;
	int16_t r[4];
	(void)x;
	(void)y;
	(void)clip;
	if (p->mode != 0 && p->bitmap.pixels) {
		r[0] = 0;
		r[1] = 0;
		r[2] = (int16_t)(p->bitmap.w - 1);
		r[3] = (int16_t)(p->bitmap.h - 1);
		kp_ui_video_image(ui, &p->video, &p->bitmap);
		kp_ui_blit_opaque(ui, c->left, c->top, p->bitmap.pixels, p->bitmap.w, p->bitmap.h, r);
		if (p->fade < FADE_DONE) draw_static(ui, p->fade, c);
	}
	if (p->overlay.pixels) {
		r[0] = 0;
		r[1] = 0;
		r[2] = (int16_t)(p->overlay.w - 1);
		r[3] = (int16_t)(p->overlay.h - 1);
		kp_ui_blit_transparent(ui, c->left, c->top, p->overlay.pixels, p->overlay.w, p->overlay.h, r);
	}
}

/* PortraitControlInteract 0x4a6d30. */
static int portrait_control_interact(KpUi *ui, KpUiControl *c, KpUiEvent *e)
{
	int32_t self = (int32_t)(c - ui->controls);
	if (e->type == KP_UI_USER && e->code == KP_UI_CREATE) {
		int race = (int)ui->game->players.race[ui->game->local_player];
		c->user = 0;
		c->slot.unit = 0;
		c->draw = draw_portrait;
		c->wuser = RACE_PORTRAITS[race >= 0 && race < 3 ? race : 1];
		ui->portrait.control = self;
		(void)kp_ui_set_timer(&ui->timers, self, TIMER_FRAMES, FRAMES_PERIOD, kp_ui_callback(ui, timer_frames), ui->now);
		(void)kp_ui_set_timer(&ui->timers, self, TIMER_FADE, FADE_PERIOD, kp_ui_callback(ui, timer_fade), ui->now);
		return 1;
	}
	if (e->type == KP_UI_USER && e->code == KP_UI_DESTROY) {
		int id;
		for (id = TIMER_FRAMES; id <= TIMER_TRIGGER; id++) kp_ui_delete_timer(&ui->timers, self, (uint16_t)id);
		ui->portrait.control = -1;
	}
	if (e->type == KP_UI_USER && e->code == KP_UI_CLOSE && c->slot.unit != 0) {
		kp_ui_centre_on_unit(ui, c->slot.unit); /* user code 2, a click with a speaker: CenterScreenOnUnit 0x422b10 */
	}
	return kp_ui_type_interact(c->type)(ui, c, e);
}

static int load_pcx(KpUi *ui, const char *path, KpUiSurface *s)
{
	const KpUiFile *f = kp_ui_file(ui->pcx, ui->pcx_count, path);
	int32_t w, h;
	if (f == NULL || kp_ui_pcx(f->data, f->size, &w, &h, NULL, 0) != 0) return -1;
	free(s->pixels);
	s->pixels = (uint8_t *)malloc((size_t)w * (size_t)h);
	if (s->pixels == NULL) return -1;
	s->w = w;
	s->h = h;
	return kp_ui_pcx(f->data, f->size, &w, &h, s->pixels, (size_t)w * (size_t)h);
}

/* InitPortraitDialog 0x4a67a0. */
static void init_portrait_dialog(KpUi *ui, int32_t dialog)
{
	static const KpUiInteract CHILDREN[1] = { portrait_control_interact };
	KpUiPortrait *p = &ui->portrait;
	int32_t k = kp_ui_find_control(ui, dialog, 1), hot;
	int race = (int)ui->game->players.race[ui->game->local_player];
	char path[32];
	if (k >= 0) {
		free(p->bitmap.pixels);
		p->bitmap.w = ui->controls[k].right - ui->controls[k].left + 1;
		p->bitmap.h = ui->controls[k].bottom - ui->controls[k].top + 1;
		p->bitmap.pixels = (uint8_t *)malloc((size_t)p->bitmap.w * (size_t)p->bitmap.h);
		fill(ui, &p->bitmap, ui->ui_colours[BLACK]);
	}
	kp_ui_bind_interacts(ui, dialog, CHILDREN, 1);
	kp_ui_capture_console(ui, dialog);
	snprintf(path, sizeof path, "game\\%cconover.pcx", RACE_LETTERS[race >= 0 && race < 3 ? race : 1]);
	(void)load_pcx(ui, path, &p->overlay);
	hot = kp_ui_find_control(ui, dialog, C_HOTKEYS);
	p->hotkeys = hot >= 0 ? ui->controls[hot].text : NULL;
}

/* PortraitDialogInteract 0x4a68e0. */
static int portrait_dialog_interact(KpUi *ui, KpUiControl *d, KpUiEvent *e)
{
	int32_t self = (int32_t)(d - ui->controls);
	KpUiInteract stock = kp_ui_type_interact(0);
	switch (e->type) {
	case KP_UI_KEY_DOWN: case KP_UI_KEY_REPEAT: case KP_UI_MOUSE_MOVE:
		return 0;
	case KP_UI_CHAR: { /* PortraitHotkey 0x4a6760: a hotkey moves the view to the last transmission (0x40b450) */
		const uint8_t *k;
		for (k = ui->portrait.hotkeys; k != NULL && *k != 0; k++) {
			if (e->key == *k) {
				int32_t x = (int32_t)ui->portrait.camera_x, y = (int32_t)ui->portrait.camera_y;
				kp_ui_view_target(ui, &x, &y);
				kp_ui_set_screen_position(ui, x, y);
				return 1;
			}
		}
		return 0;
	}
	case KP_UI_USER:
		if (e->code == KP_UI_CREATE) init_portrait_dialog(ui, self);
		else if (e->code == KP_UI_HIT_TEST) return 1;
		return stock(ui, d, e);
	default:
		return stock(ui, d, e);
	}
}

int kp_ui_portrait_create(KpUi *ui)
{
	/* LoadPortraitData 0x4a6a40 */
	KpUiPortrait *p = &ui->portrait;
	int32_t d;
	p->control = -1;
	p->talking = 0;
	p->trigger = 0;
	p->camera_x = kp_scalar(ui->game, KP_SCREEN_X) == KP_NIL ? 0 : kp_scalar(ui->game, KP_SCREEN_X) * 32; /* 0x543218 */
	p->camera_y = kp_scalar(ui->game, KP_SCREEN_Y) == KP_NIL ? 0 : kp_scalar(ui->game, KP_SCREEN_Y) * 32;
	ui->hud.portrait_refresh = 1;
	d = kp_ui_load_dialog(ui, "rez\\statport.bin");
	if (d < 0) return -1;
	p->dialog = d;
	kp_ui_init_dialog(ui, d, portrait_dialog_interact);
	return p->control >= 0 ? 0 : -1;
}

void kp_ui_portrait_frame(KpUi *ui)
{
	/* UpdatePortraitForSelection 0x4a7420 */
	KpUiPortrait *p = &ui->portrait;
	int64_t n = ui->hud.status_unit;
	if (!ui->hud.portrait_refresh || !has_control(ui)) return;
	ui->hud.portrait_refresh = 0;
	if (p->talking) return;
	if (n == 0) {
		clear_portrait(ui);
		return;
	}
	if (control(ui)->slot.unit == n) return;
	show_idle(ui, n);
}

void kp_ui_portrait_talk(KpUi *ui, int64_t unit, uint32_t duration, int ping, uint8_t colour, uint32_t sound)
{
	/* RequestUnitTalking 0x4a7200 */
	KpUiPortrait *p = &ui->portrait;
	KpUiSound *sd = &ui->sound;
	if (sd->talk_count < KP_UI_SOUND_CALLS) {
		sd->talks[sd->talk_count].unit = unit;
		sd->talks[sd->talk_count].duration = duration;
		sd->talks[sd->talk_count].sound = sound;
		sd->talks[sd->talk_count].ping = (uint8_t)(ping != 0);
		sd->talks[sd->talk_count].colour = colour;
		sd->talk_count++;
	}
	if (p->trigger || !has_control(ui) || unit == 0) return;
	show_portrait(ui, unit, kp_ui_resolve_portrait(ui, displayed_type(ui->game, unit)), 1);
	if (duration == 0 && sound < SOUNDS) {
		p->talk_sound = sound;
		(void)kp_ui_set_timer(&ui->timers, p->control, TIMER_TALK, SECOND, kp_ui_callback(ui, timer_talk_pending),
			ui->now);
	} else {
		(void)kp_ui_set_timer(&ui->timers, p->control, TIMER_TALK, duration, kp_ui_callback(ui, timer_talk), ui->now);
	}
	if (ping) {
		KpGame *g = ui->game;
		int32_t s = kp_unit_sprite(g, unit), x = (int16_t)g->w->spr.x[s], y = (int16_t)g->w->spr.y[s];
		ui->portrait.camera_x = x - EVENT_HALF_W < 0 ? 0 : x - EVENT_HALF_W;
		ui->portrait.camera_y = y - EVENT_HALF_H < 0 ? 0 : y - EVENT_HALF_H;
		/* the minimap's blink counter and colour are the unit's (+0x8a, +0x8b): the interface's, which nothing of the
		 * simulation reads (ADR 0010) */
		g->u.last_event_timer[unit] = TALK_BLINK;
		g->u.last_event_color[unit] = colour;
		kp_ui_minimap_ping(ui, (uint32_t)x, (uint32_t)y, colour);
	}
}

/* The trigger portrait's end 0x4a7300 (timer 4). */
static void timer_trigger(KpUi *ui, int32_t owner)
{
	kp_ui_delete_timer(&ui->timers, owner, TIMER_TRIGGER);
	ui->portrait.trigger = 0;
	ui->portrait.release = 1; /* single player: 0x4be3a0 */
}

void kp_ui_portrait_trigger(KpUi *ui, int64_t type, int32_t x, int32_t y, uint32_t duration)
{
	KpUiPortrait *p = &ui->portrait;
	if (!has_control(ui)) return;
	p->trigger = 1;
	(void)kp_ui_set_timer(&ui->timers, p->control, TIMER_TRIGGER, duration, kp_ui_callback(ui, timer_trigger), ui->now);
	show_portrait(ui, 0, kp_ui_resolve_portrait(ui, type), 1);
	(void)kp_ui_set_timer(&ui->timers, p->control, TIMER_TALK, duration, kp_ui_callback(ui, timer_talk), ui->now);
	if (x != -1) {
		ui->portrait.camera_x = x - EVENT_HALF_W < 0 ? 0 : x - EVENT_HALF_W;
		ui->portrait.camera_y = y - EVENT_HALF_H < 0 ? 0 : y - EVENT_HALF_H;
	}
}

void kp_ui_portrait_trigger_stop(KpUi *ui)
{
	KpUiPortrait *p = &ui->portrait;
	if (!p->trigger) return;
	p->trigger = 0;
	kp_ui_delete_timer(&ui->timers, p->control, TIMER_TRIGGER);
	portrait_timer(ui, p->control, TIMER_TALK);
}

int kp_ui_pending_releases(const KpUi *ui, uint32_t *portrait_ms, int *camera)
{
	const KpUiPortrait *p = &ui->portrait;
	*camera = ui->input.scroll.pan && ui->input.scroll.pan_release;
	return p->trigger && kp_ui_timer_remaining(&ui->timers, p->control, TIMER_TRIGGER, ui->now, portrait_ms);
}

void kp_ui_game_loaded(KpUi *ui)
{
	if (ui->game != NULL && kp_scalar(ui->game, KP_PLACEMENT_ACTIVE) == 1) kp_placement_close(ui->game);
	kp_ui_cursor_modes_ended(ui, 1);
	kp_ui_portrait_trigger_stop(ui);
	ui->input.scroll.pan = 0;
	ui->input.scroll.pan_release = 0;
	ui->portrait.release = 0;
	ui->input.scroll.release = 0;
}

int kp_ui_take_release(KpUi *ui)
{
	int r = ui->portrait.release + ui->input.scroll.release; /* the portrait's end, the camera pan's arrival */
	ui->portrait.release = 0;
	ui->input.scroll.release = 0;
	return r;
}
