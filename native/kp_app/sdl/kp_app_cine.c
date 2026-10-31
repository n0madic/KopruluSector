/*
 * The cinematics (ADR 0015; KP_CINEMATICS): generated/video/cinematics-<hd2|sd>.kpd chosen by --cinematics, its index
 * read at start, a movie played from the file as the clock of its Ogg Vorbis (the sound's position) asks for the
 * pictures that kp_cine.c decodes, drawn full screen by kp_gfx_video with black bars to keep the shape of the frames.
 * While a movie plays the flow and the world are not run (SDL_AppIterate); a key, a click or a tap ends it, as the
 * original's PlayCinematic does (LEFT_UP, RIGHT_UP, Escape, Enter, Space: 0x47ec60).
 *
 * Without KP_CINEMATICS the pack is never there: has and start answer 0 and nothing else is called.
 *
 * TODO: Remastered's subtitles (locales/<locale>/Assets/Smk/<name>.srt, ADR 0015) are not shown.
 */
#include "kp_app.h"

#include <stdio.h>
#include <string.h>

#ifndef KP_CINEMATICS

int kp_app_cine_open(KpApp *a)
{
	(void)a;
	return 0;
}

int kp_app_cine_has(const KpApp *a, int id)
{
	(void)a;
	(void)id;
	return 0;
}

int kp_app_cine_start(KpApp *a, const int *ids, int n, int preludes)
{
	(void)a;
	(void)ids;
	(void)n;
	(void)preludes;
	return 0;
}

int kp_app_cine_active(const KpApp *a)
{
	(void)a;
	return 0;
}

int kp_app_cine_check(KpApp *a, const char *dir)
{
	(void)a;
	(void)dir;
	kp_dev_line("cine check: built without KP_CINEMATICS");
	return -1;
}

void kp_app_cine_frame(KpApp *a) { (void)a; }

void kp_app_cine_skip(KpApp *a, int all)
{
	(void)a;
	(void)all;
}

void kp_app_cine_free(KpApp *a) { (void)a; }

#else

#define PACK_FORMAT "generated/video/cinematics-%s.kpd"
#define AUDIO_WAIT_MS 300 /* a sound that has not begun by then leaves the movie to the wall clock until it does */
#define CLOCK_GAP_MS 250  /* a longer pause between two frames (the app in the background) is not the movie's time */

/* The index of the pack at path; 0, or -1 with the reason logged. */
static int index_file(KpApp *a, const char *path)
{
	uint8_t head[KP_CINE_HEAD];
	SDL_IOStream *io = SDL_IOFromFile(path, "rb");
	Sint64 size;
	size_t n;
	KpError err;
	int rc;
	if (!io) return -1;
	size = SDL_GetIOSize(io);
	n = size > 0 && (uint64_t)size < sizeof head ? (size_t)size : sizeof head;
	if (size <= 0 || SDL_ReadIO(io, head, n) != n) {
		SDL_Log("cinematics: cannot read %s: %s", path, SDL_GetError());
		SDL_CloseIO(io);
		return -1;
	}
	rc = kp_cine_index(&a->cine.index, head, n, (size_t)size, kp_io_read_into, io, &err);
	SDL_CloseIO(io);
	if (rc != 0) SDL_Log("%s (%s)", err.msg, path);
	return rc;
}

int kp_app_cine_open(KpApp *a)
{
	static const char *const AUTO[] = { "hd2", "sd" };
	const char *mode = a->flags.cinematics[0] ? a->flags.cinematics : "auto";
	const char *const *variants = AUTO;
	const char *given[1];
	int count = 2, k;
	memset(&a->cine, 0, sizeof a->cine);
	if (strcmp(mode, "off") == 0) return 0;
	if (strcmp(mode, "sd") == 0 || strcmp(mode, "hd2") == 0) {
		given[0] = mode;
		variants = given;
		count = 1;
	} else if (strcmp(mode, "auto") != 0) {
		SDL_Log("cinematics: --cinematics %s is not auto, sd, hd2 or off; auto", mode);
	}
	for (k = 0; k < count; k++) {
		char rel[96];
		snprintf(rel, sizeof rel, PACK_FORMAT, variants[k]);
		kp_app_path(a, rel, a->cine.path, sizeof a->cine.path);
		if (index_file(a, a->cine.path) == 0) {
			a->cine.have = 1;
			SDL_Log("cinematics: %s, %d movies", variants[k], a->cine.index.count);
			return 0;
		}
	}
	memset(&a->cine, 0, sizeof a->cine);
	SDL_Log("cinematics: no pack (%s): the game plays without them", mode);
	return 0;
}

int kp_app_cine_has(const KpApp *a, int id) { return a->cine.have && kp_cine_find(&a->cine.index, id) >= 0; }

int kp_app_cine_active(const KpApp *a) { return a->cine.active; }

/* The picture over the safe area, the shape of the movie kept: rgba the new picture, NULL the one drawn last (black
 * while there is none of the movie's size yet). */
static void draw(KpApp *a, const uint8_t *rgba)
{
	const KpCineClip *clip = a->cine.player ? kp_cine_clip(a->cine.player) : NULL;
	KpGfxFrame f;
	double sx, sy, s;
	int32_t rw, rh;
	if (!clip) return;
	sx = (double)a->win_w / clip->shown_w;
	sy = (double)a->win_h / clip->shown_h;
	s = sx < sy ? sx : sy;
	rw = (int32_t)((double)clip->shown_w * s + 0.5);
	rh = (int32_t)((double)clip->shown_h * s + 0.5);
	if (rw < 1) rw = 1;
	if (rh < 1) rh = 1;
	memset(&f, 0, sizeof f);
	f.win_w = rw;
	f.win_h = rh;
	f.win_x = a->safe_x + (a->win_w - rw) / 2;
	f.win_y = a->safe_y + (a->win_h - rh) / 2;
	f.full_w = a->full_w;
	f.full_h = a->full_h;
	kp_gfx_video(a->gfx, rgba, clip->width, clip->height, &f);
}

/* The movie's time: the sound's position while it plays (also when it began late: the pictures wait for it), else the
 * wall clock from where the sound left the movie or from its begin; a pause between two frames longer than
 * CLOCK_GAP_MS (the app in the background) does not count. */
static uint64_t movie_ms(KpApp *a)
{
	KpAppCine *c = &a->cine;
	uint64_t now = SDL_GetTicks(), wall;
	if (now - c->last_ticks > CLOCK_GAP_MS) c->wall_base_ticks += now - c->last_ticks;
	c->last_ticks = now;
	if (c->audio) {
		MIX_Track *t = a->audio.cine;
		bool playing = MIX_TrackPlaying(t);
		Sint64 pos = playing ? MIX_GetTrackPlaybackPosition(t) : -1;
		Sint64 ms = pos > 0 ? MIX_TrackFramesToMS(t, pos) : -1;
		if (ms > 0) {
			if ((uint64_t)ms > c->last_ms) c->last_ms = (uint64_t)ms;
			c->wall_base_ms = c->last_ms;
			c->wall_base_ticks = now;
			return c->last_ms;
		}
		if (!playing) {
			c->audio = 0; /* it ended, or never began */
		} else if (now - c->t0 < AUDIO_WAIT_MS) {
			c->wall_base_ticks = now; /* its start awaited: the wall clock begins when the wait ends */
			return c->last_ms;
		}
	}
	wall = c->wall_base_ms + (now - c->wall_base_ticks);
	if (wall > c->last_ms) c->last_ms = wall;
	return c->last_ms;
}

/* The window's input as the original's movie loop leaves it: the presses before the movie (whose releases the movie
 * took) released, the interface active again when the window has the focus. */
static void finish(KpApp *a)
{
	KpAppCine *c = &a->cine;
	if (c->file) SDL_CloseIO(c->file);
	c->file = NULL;
	c->active = 0;
	kp_input_focus(a, a->window && (SDL_GetWindowFlags(a->window) & SDL_WINDOW_INPUT_FOCUS) != 0);
}

/* Cinematic id begins; -1 (logged) when it cannot. */
static int begin_movie(KpApp *a, int id)
{
	KpAppCine *c = &a->cine;
	const KpCineClip *clip;
	KpError err;
	int k = kp_cine_find(&c->index, id);
	if (k < 0 || !(c->player = kp_cine_open(&c->index, k, kp_io_read_into, c->file, &err))) {
		SDL_Log("cinematics: %d: %s", id, k < 0 ? "not in the pack" : err.msg);
		return -1;
	}
	clip = kp_cine_clip(c->player);
	c->t0 = c->last_ticks = c->wall_base_ticks = SDL_GetTicks();
	c->last_ms = 0;
	c->wall_base_ms = 0;
	c->audio = 0;
	if (clip->audio_size > 0 && a->audio.cine) {
		/* the soundtrack at the music option's volume (the player's own choice, silence included) */
		float g = kp_audio_gain(a->ui ? kp_ui_music_level(a->ui) : 0);
		SDL_IOStream *io = kp_app_range_io(c->path, (Sint64)clip->audio_offset, (Sint64)clip->audio_size);
		if (io && MIX_SetTrackIOStream(a->audio.cine, io, true) && MIX_SetTrackGain(a->audio.cine, g)
			&& MIX_PlayTrack(a->audio.cine, 0)) {
			c->audio = 1;
		} else {
			SDL_Log("cinematics: %s plays without sound: %s", clip->name, SDL_GetError());
		}
	}
	SDL_Log("cinematics: %s %dx%d, %u frames", clip->name, clip->width, clip->height, (unsigned)clip->frames);
	return 0;
}

/* The movie playing ended: the next of the queue begins, or the cinematics end. */
static void end_movie(KpApp *a)
{
	KpAppCine *c = &a->cine;
	if (c->player) {
		SDL_Log("cinematics: %s ended after %llu ms of the wall, %llu of its own; %u of %u frames decoded, %u pictures shown",
			kp_cine_clip(c->player)->name, (unsigned long long)(SDL_GetTicks() - c->t0), (unsigned long long)c->last_ms,
			(unsigned)kp_cine_decoded(c->player), (unsigned)kp_cine_clip(c->player)->frames, (unsigned)c->shown);
		kp_cine_free(c->player);
	}
	c->player = NULL;
	c->shown = 0;
	if (a->audio.cine) MIX_StopTrack(a->audio.cine, 0);
	for (c->queue_pos++; c->queue_pos < c->queue_len; c->queue_pos++) {
		if (begin_movie(a, c->queue[c->queue_pos]) == 0) return;
	}
	finish(a);
}

int kp_app_cine_start(KpApp *a, const int *ids, int n, int preludes)
{
	KpAppCine *c = &a->cine;
	int k;
	if (!c->have) return 0;
	if (c->active) return 1;
	c->queue_len = 0;
	for (k = 0; k < n; k++) {
		int prelude = preludes ? kp_cine_prelude(&c->index, ids[k]) : -1;
		if (prelude >= 0 && kp_cine_find(&c->index, prelude) >= 0 && c->queue_len < KP_APP_CINE_QUEUE) {
			c->queue[c->queue_len++] = prelude;
		}
		if (kp_cine_find(&c->index, ids[k]) >= 0 && c->queue_len < KP_APP_CINE_QUEUE) c->queue[c->queue_len++] = ids[k];
	}
	if (c->queue_len == 0) return 0;
	if (!(c->file = SDL_IOFromFile(c->path, "rb"))) {
		SDL_Log("cinematics: cannot open %s: %s", c->path, SDL_GetError());
		return 0;
	}
	/* PlayCinematic stops the music: the interface's track closed too, so the next screen starts its own again */
	if (a->ui) kp_ui_music_stop(a->ui);
	kp_audio_stop_all(&a->audio);
	kp_input_focus(a, 0); /* the presses before the movie end here: their releases are the movie's */
	SDL_HideCursor();
	c->active = 1;
	c->shown = 0;
	c->queue_pos = -1;
	end_movie(a);
	return c->active;
}

void kp_app_cine_frame(KpApp *a)
{
	KpAppCine *c = &a->cine;
	const uint8_t *picture = NULL;
	KpError err;
	int rc;
	if (!c->active) return;
	if (c->player) {
		rc = kp_cine_advance(c->player, movie_ms(a), &err);
		if (rc < 0) {
			SDL_Log("%s: the movie ends", err.msg);
			end_movie(a);
		} else if (rc == KP_CINE_END) {
			end_movie(a);
		} else if (rc == KP_CINE_FRAME) {
			picture = kp_cine_rgba(c->player);
			c->shown++;
		}
	}
	draw(a, picture);
}

/* A picture as dir/<name>_<which>.png. */
static void save_picture(const KpCineClip *clip, const uint8_t *rgba, const char *dir, const char *which)
{
	char path[1400];
	const char *name = strrchr(clip->name, '\\');
	SDL_Surface *s = SDL_CreateSurfaceFrom(clip->width, clip->height, SDL_PIXELFORMAT_RGBA32, (void *)rgba,
		clip->width * 4);
	snprintf(path, sizeof path, "%s/%s_%s.png", dir, name ? name + 1 : clip->name, which);
	if (s) {
		if (!SDL_SavePNG(s, path)) kp_dev_line("cine check: %s: %s", path, SDL_GetError());
		SDL_DestroySurface(s);
	}
}

/* One movie decoded to its end at the times of its frames: 0, or -1 with the reason logged. */
static int check_movie(KpApp *a, int k, const char *dir, uint64_t *frames_total)
{
	KpAppCine *c = &a->cine;
	const KpCineClip *clip = &c->index.clips[k];
	KpCinePlayer *p;
	KpError err;
	uint32_t n, pictures = 0;
	uint64_t t0 = SDL_GetTicks();
	int rc = 0;
	if (!(p = kp_cine_open(&c->index, k, kp_io_read_into, c->file, &err))) {
		kp_dev_line("cine check %s: %s", clip->name, err.msg);
		return -1;
	}
	for (n = 0; n < clip->frames && rc == 0; n++) {
		int st = kp_cine_advance(p, kp_cine_frame_ms(clip, n), &err);
		if (st < 0) {
			kp_dev_line("cine check %s: frame %u: %s", clip->name, (unsigned)n, err.msg);
			rc = -1;
		} else if (st == KP_CINE_FRAME) {
			pictures++;
			if (n == 0) save_picture(clip, kp_cine_rgba(p), dir, "first");
		}
	}
	if (rc == 0) {
		save_picture(clip, kp_cine_rgba(p), dir, "last");
		if (kp_cine_advance(p, kp_cine_frame_ms(clip, clip->frames), &err) != KP_CINE_END || kp_cine_decoded(p) != clip->frames
			|| pictures != clip->frames) {
			kp_dev_line("cine check %s: %u of %u frames decoded, %u pictures, no end", clip->name,
				(unsigned)kp_cine_decoded(p), (unsigned)clip->frames, (unsigned)pictures);
			rc = -1;
		}
	}
	kp_dev_line("cine check %s: %dx%d, %u frames, %u pictures, %llu ms%s", clip->name, clip->width, clip->height,
		(unsigned)clip->frames, (unsigned)pictures, (unsigned long long)(SDL_GetTicks() - t0), rc ? ", FAILED" : "");
	*frames_total += kp_cine_decoded(p);
	kp_cine_free(p);
	return rc;
}

int kp_app_cine_check(KpApp *a, const char *dir)
{
	KpAppCine *c = &a->cine;
	int k, failures = 0;
	uint64_t frames = 0;
	if (!c->have) {
		kp_dev_line("cine check: no pack (--cinematics %s)", a->flags.cinematics[0] ? a->flags.cinematics : "auto");
		return -1;
	}
	kp_app_cine_skip(a, 1); /* a movie of --cine-play keeps the pack's file and its own player */
	if (!SDL_CreateDirectory(dir)) kp_dev_line("cine check: %s: %s", dir, SDL_GetError());
	if (!(c->file = SDL_IOFromFile(c->path, "rb"))) {
		kp_dev_line("cine check: cannot open %s: %s", c->path, SDL_GetError());
		return -1;
	}
	for (k = 0; k < c->index.count; k++) failures += check_movie(a, k, dir, &frames) != 0;
	SDL_CloseIO(c->file);
	c->file = NULL;
	kp_dev_line("cine check: %d movies, %llu frames, %d failures", c->index.count, (unsigned long long)frames, failures);
	return failures;
}

void kp_app_cine_skip(KpApp *a, int all)
{
	KpAppCine *c = &a->cine;
	if (!c->active) return;
	if (all) c->queue_pos = c->queue_len - 1;
	end_movie(a);
}

void kp_app_cine_free(KpApp *a)
{
	kp_app_cine_skip(a, 1);
}

#endif
