/*
 * The cinematics (ADR 0015; KP_CINEMATICS): generated/video/cinematics-<hd2|sd>.kpd chosen by --cinematics, its index
 * read at start, a movie played from the file as the clock of its Ogg Vorbis (the sound's position) asks for the
 * pictures that kp_cine.c decodes, their planes converted and drawn full screen by kp_gfx_video with black bars to
 * keep the shape of the frames. The queue, the movies' order and the skip rule are kp_cine.c's run (KpCineRun); this is
 * its host: the pack's file, the soundtrack's track, the input and the drawing. While a movie plays the flow and the
 * world are not run (SDL_AppIterate); a key, a click or a tap ends it once it has been on screen, as the original's
 * PlayCinematic does (LEFT_UP, RIGHT_UP, Escape, Enter, Space: 0x47ec60).
 *
 * libvpx decodes in one thread on the main thread, with NEON on arm64 (an HD2 frame 1.0 ms on the Fold7, 1.2 on the
 * Mac, ADR 0015). TODO: x86 (Windows, Linux) decodes from C: libvpx's SSE and AVX need nasm for a part of them.
 *
 * Without KP_CINEMATICS the pack is never there: has and start answer 0 and nothing else is called.
 *
 * TODO: Remastered's subtitles (locales/<locale>/Assets/Smk/<name>.srt, ADR 0015) are not shown.
 */
#include "kp_app.h"

#include <stdio.h>
#include <stdlib.h>
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

void kp_app_cine_frame(KpApp *a, int visible)
{
	(void)a;
	(void)visible;
}

void kp_app_cine_skip(KpApp *a, uint64_t when_ns)
{
	(void)a;
	(void)when_ns;
}

void kp_app_cine_stop(KpApp *a) { (void)a; }

void kp_app_cine_free(KpApp *a) { (void)a; }

#else

#define PACK_FORMAT "generated/video/cinematics-%s.kpd"
#define GPU_ERROR_LIMIT 4 /* --cine-check: the mean byte error of a GPU picture against the CPU's (the chroma sampled
                           * between its texels there, copied here; 0 for the sRGB movies) */

/* The index of the pack at path; 0, or -1 (logged unless the file is not there). */
static int index_file(KpApp *a, const char *path)
{
	uint8_t head[KP_CINE_HEAD];
	SDL_IOStream *io;
	Sint64 size;
	size_t n;
	KpError err;
	int rc;
	if (!(io = kp_io_open_head(path, head, sizeof head, &n, &size))) {
		if (size >= 0) SDL_Log("cinematics: cannot read %s: %s", path, SDL_GetError());
		return -1;
	}
	rc = kp_cine_index(&a->cine.run.index, head, n, (size_t)size, kp_io_read_into, io, &err);
	SDL_CloseIO(io);
	if (rc != 0) SDL_Log("%s (%s)", err.msg, path);
	return rc;
}

static uint64_t host_now_ns(void *ctx)
{
	(void)ctx;
	return SDL_GetTicksNS();
}

static void *host_open(void *ctx)
{
	KpApp *a = (KpApp *)ctx;
	SDL_IOStream *io = SDL_IOFromFile(a->cine.path, "rb");
	if (!io) SDL_Log("cinematics: cannot open %s: %s", a->cine.path, SDL_GetError());
	return io;
}

static void host_close(void *ctx, void *file)
{
	(void)ctx;
	SDL_CloseIO((SDL_IOStream *)file);
}

/* A run begins: PlayCinematic stops the music (the interface's track closed too, so the next screen starts its own
 * again), and the presses before the movie end here: their releases are the movie's. */
static void host_begin(void *ctx)
{
	KpApp *a = (KpApp *)ctx;
	if (a->flow.ui) kp_ui_music_stop(a->flow.ui);
	kp_audio_stop_all(&a->audio);
	kp_input_focus(a, 0);
	SDL_HideCursor();
}

/* The window's input as the original's movie loop leaves it: the presses before the movie (whose releases the movie
 * took) released, the interface active again when the window has the focus. */
static void host_end(void *ctx)
{
	KpApp *a = (KpApp *)ctx;
	kp_input_focus(a, a->window && (SDL_GetWindowFlags(a->window) & SDL_WINDOW_INPUT_FOCUS) != 0);
}

/* The movie's soundtrack at the music option's volume (the player's own choice, silence included). */
static int host_sound(void *ctx, const KpCineClip *clip)
{
	KpApp *a = (KpApp *)ctx;
	float g;
	SDL_IOStream *io;
	if (!a->audio.cine) return 0;
	g = kp_audio_gain(a->flow.ui ? kp_ui_music_level(a->flow.ui) : 0);
	io = kp_app_range_io(a->cine.path, (Sint64)clip->audio_offset, (Sint64)clip->audio_size);
	if (io && MIX_SetTrackIOStream(a->audio.cine, io, true) && MIX_SetTrackGain(a->audio.cine, g)
		&& MIX_PlayTrack(a->audio.cine, 0)) {
		return 1;
	}
	SDL_Log("cinematics: %s plays without sound: %s", clip->name, SDL_GetError());
	return 0;
}

static int host_sound_ms(void *ctx, int64_t *ms)
{
	MIX_Track *t = ((KpApp *)ctx)->audio.cine;
	Sint64 pos;
	*ms = -1;
	if (!t || !MIX_TrackPlaying(t)) return 0;
	pos = MIX_GetTrackPlaybackPosition(t);
	*ms = pos > 0 ? MIX_TrackFramesToMS(t, pos) : -1;
	return 1;
}

static void host_sound_stop(void *ctx)
{
	MIX_Track *t = ((KpApp *)ctx)->audio.cine;
	if (!t) return;
	MIX_StopTrack(t, 0);
	MIX_SetTrackAudio(t, NULL); /* closes the sound's stream: the pack's second handle */
}

static void host_log(void *ctx, const char *line)
{
	(void)ctx;
	SDL_Log("%s", line);
}

int kp_app_cine_open(KpApp *a)
{
	static const char *const AUTO[] = { "hd2", "sd" };
	const char *mode = a->flags.cinematics[0] ? a->flags.cinematics : "auto";
	const char *const *variants = AUTO;
	const char *given[1];
	KpCineHost *h = &a->cine.run.host;
	int count = 2, k;
	memset(&a->cine, 0, sizeof a->cine);
	h->ctx = a;
	h->now_ns = host_now_ns;
	h->read = kp_io_read_into;
	h->open = host_open;
	h->close = host_close;
	h->begin = host_begin;
	h->end = host_end;
	h->sound = host_sound;
	h->sound_ms = host_sound_ms;
	h->sound_stop = host_sound_stop;
	h->log = host_log;
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
			a->cine.run.have = 1;
			SDL_Log("cinematics: %s, %d movies", variants[k], a->cine.run.index.count);
			return 0;
		}
	}
	memset(&a->cine.run.index, 0, sizeof a->cine.run.index);
	a->cine.path[0] = 0;
	SDL_Log("cinematics: no pack (%s): the game plays without them", mode);
	return 0;
}

int kp_app_cine_has(const KpApp *a, int id) { return kp_cine_run_has(&a->cine.run, id); }

int kp_app_cine_active(const KpApp *a) { return a->cine.run.active; }

/* The picture over the safe area, the shape of the movie kept: pic the new picture, NULL the movie's one drawn last
 * (black while the movie has drawn none: the textures may hold the movie's before it). */
static void draw(KpApp *a, const KpVideoPicture *pic)
{
	const KpCineClip *clip = a->cine.run.player ? kp_cine_clip(a->cine.run.player) : NULL;
	int last = pic || a->cine.run.drawn_ns != 0;
	KpGfxFrame f;
	double sx, sy, s;
	int32_t rw, rh;
	if (!clip) return;
	sx = (double)a->flow.win_w / clip->shown_w;
	sy = (double)a->flow.win_h / clip->shown_h;
	s = sx < sy ? sx : sy;
	rw = (int32_t)((double)clip->shown_w * s + 0.5);
	rh = (int32_t)((double)clip->shown_h * s + 0.5);
	if (rw < 1) rw = 1;
	if (rh < 1) rh = 1;
	memset(&f, 0, sizeof f);
	f.win_w = rw;
	f.win_h = rh;
	f.win_x = a->flow.win_x + (a->flow.win_w - rw) / 2;
	f.win_y = a->flow.win_y + (a->flow.win_h - rh) / 2;
	f.full_w = a->flow.full_w;
	f.full_h = a->flow.full_h;
	kp_gfx_video(a->gfx, pic, last ? clip->width : 0, last ? clip->height : 0, &f);
}

int kp_app_cine_start(KpApp *a, const int *ids, int n, int preludes)
{
	return kp_cine_run_start(&a->cine.run, ids, n, preludes);
}

void kp_app_cine_frame(KpApp *a, int visible)
{
	KpCineRun *r = &a->cine.run;
	KpVideoPicture pic;
	int fresh;
	if (!r->active) return;
	fresh = kp_cine_run_frame(r, &pic);
	/* a movie that began as the one before ended has no picture yet: black, not the one before it */
	if (!visible || !r->active) return;
	draw(a, fresh ? &pic : NULL);
	if (fresh) kp_cine_run_shown(r);
}

/* A picture as dir/<name>_<which>.png. */
static void save_picture(const KpCineClip *clip, const uint8_t *rgba, const char *dir, const char *which)
{
	char path[1400];
	const char *name = strrchr(clip->name, '\\');
	SDL_Surface *s;
	if (!rgba) return;
	s = SDL_CreateSurfaceFrom(clip->width, clip->height, SDL_PIXELFORMAT_RGBA32, (void *)rgba, clip->width * 4);
	snprintf(path, sizeof path, "%s/%s_%s.png", dir, name ? name + 1 : clip->name, which);
	if (s) {
		if (!SDL_SavePNG(s, path)) kp_dev_line("cine check: %s: %s", path, SDL_GetError());
		SDL_DestroySurface(s);
	}
}

/* The player's picture drawn by the GPU (kp_gfx_video) 1:1 at the window's top-left and read back: the mean of its
 * bytes' (RGB) difference from kp_cine_rgba's over the part the window holds; -1 when it cannot be drawn or read. */
static double gpu_error(KpApp *a, KpCinePlayer *p)
{
	const KpCineClip *clip = kp_cine_clip(p);
	const uint8_t *ref = kp_cine_rgba(p);
	int32_t w = clip->width < a->flow.full_w ? clip->width : a->flow.full_w, h = clip->height < a->flow.full_h ? clip->height : a->flow.full_h;
	KpVideoPicture pic;
	KpGfxFrame f;
	uint8_t *got;
	uint64_t sum = 0;
	int32_t x, y;
	int k;
	if (!ref || !a->gfx || w <= 0 || h <= 0 || kp_cine_picture(p, &pic) != 0) return -1;
	if (!(got = (uint8_t *)malloc((size_t)w * (size_t)h * 4))) return -1;
	memset(&f, 0, sizeof f);
	f.win_x = 0;
	f.win_y = a->flow.full_h - clip->height; /* the picture's row r at the window's GL row full_h - 1 - r */
	f.win_w = clip->width;
	f.win_h = clip->height;
	f.full_w = a->flow.full_w;
	f.full_h = a->flow.full_h;
	kp_gfx_video(a->gfx, &pic, clip->width, clip->height, &f);
	if (kp_gfx_read_window(a->gfx, 0, a->flow.full_h - h, w, h, got) != 0) {
		free(got);
		return -1;
	}
	for (y = 0; y < h; y++) { /* the read's rows bottom-up: its row y is the picture's row h - 1 - y */
		const uint8_t *g = got + (size_t)y * (size_t)w * 4, *r = ref + (size_t)(h - 1 - y) * clip->width * 4;
		for (x = 0; x < w; x++) {
			for (k = 0; k < 3; k++) {
				int d = g[4 * x + k] - r[4 * x + k];
				sum += (uint64_t)(d < 0 ? -d : d);
			}
		}
	}
	free(got);
	return (double)sum / ((double)w * (double)h * 3);
}

/* The CRC-32 of a picture's three planes, row by row (the strides' padding left out), continued from crc. */
static uint32_t picture_crc(uint32_t crc, const KpVideoPicture *pic)
{
	int i, y;
	for (i = 0; i < 3; i++) {
		int32_t w = i ? pic->chroma_w : pic->width, h = i ? pic->chroma_h : pic->height;
		for (y = 0; y < h; y++) crc = kp_crc32_update(crc, pic->planes[i] + (size_t)y * (size_t)pic->strides[i], (size_t)w);
	}
	return crc;
}

/* One movie decoded to its end at the times of its frames, its middle picture also drawn by the GPU: 0, or -1 with
 * the reason logged. The line logged gives the mean and longest kp_cine_advance a frame (the pack's reads and the
 * decoding) and the CRC-32 of every picture's planes in turn (the same in any build of libvpx: its SIMD is bit-exact
 * with its C). */
static int check_movie(KpApp *a, SDL_IOStream *file, int k, const char *dir, uint64_t *frames_total)
{
	const KpCineClip *clip = &a->cine.run.index.clips[k];
	KpCinePlayer *p;
	KpError err;
	uint32_t n, pictures = 0;
	uint64_t t0 = SDL_GetTicks(), advance = 0, advance_max = 0;
	double gpu = -1, tick_ms = 1000.0 / (double)SDL_GetPerformanceFrequency();
	uint32_t crc = 0;
	KpVideoPicture pic;
	int rc = 0;
	if (!(p = kp_cine_open(&a->cine.run.index, k, kp_io_read_into, file, &err))) {
		kp_dev_line("cine check %s: %s", clip->name, err.msg);
		return -1;
	}
	for (n = 0; n < clip->frames && rc == 0; n++) {
		uint64_t d = SDL_GetPerformanceCounter();
		int st = kp_cine_advance(p, kp_cine_frame_ms(clip, n), &err);
		d = SDL_GetPerformanceCounter() - d;
		advance += d;
		if (d > advance_max) advance_max = d;
		if (st < 0) {
			kp_dev_line("cine check %s: frame %u: %s", clip->name, (unsigned)n, err.msg);
			rc = -1;
		} else if (st == KP_CINE_FRAME) {
			pictures++;
			if (kp_cine_picture(p, &pic) == 0) {
				crc = picture_crc(crc, &pic);
			} else {
				kp_dev_line("cine check %s: frame %u: no planes", clip->name, (unsigned)n);
				rc = -1;
			}
			if (n == 0) save_picture(clip, kp_cine_rgba(p), dir, "first");
			if (n == clip->frames / 2) { /* the middle: the first pictures fade in from black */
				gpu = gpu_error(a, p);
				if (gpu < 0 || gpu > GPU_ERROR_LIMIT) {
					kp_dev_line("cine check %s: the GPU's picture %u: %s %.2f", clip->name, (unsigned)n,
						gpu < 0 ? "not drawn" : "mean error", gpu);
					rc = -1;
				}
			}
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
	kp_dev_line("cine check %s: %dx%d, %u frames, %u pictures, GPU error %.2f, advance %.2f ms a frame (max %.1f), "
		"planes crc %08x, %llu ms%s", clip->name, clip->width, clip->height, (unsigned)clip->frames, (unsigned)pictures,
		gpu, n ? (double)advance * tick_ms / (double)n : 0.0, (double)advance_max * tick_ms, (unsigned)crc,
		(unsigned long long)(SDL_GetTicks() - t0), rc ? ", FAILED" : "");
	*frames_total += kp_cine_decoded(p);
	kp_cine_free(p);
	return rc;
}

int kp_app_cine_check(KpApp *a, const char *dir)
{
	const KpCineIndex *ix = &a->cine.run.index;
	SDL_IOStream *file;
	int k, failures = 0;
	uint64_t frames = 0;
	if (!a->cine.run.have) {
		kp_dev_line("cine check: no pack (--cinematics %s)", a->flags.cinematics[0] ? a->flags.cinematics : "auto");
		return -1;
	}
	kp_app_cine_stop(a); /* a movie of --cine-play keeps the pack's file and its own player */
	if (!SDL_CreateDirectory(dir)) kp_dev_line("cine check: %s: %s", dir, SDL_GetError());
	if (!(file = SDL_IOFromFile(a->cine.path, "rb"))) {
		kp_dev_line("cine check: cannot open %s: %s", a->cine.path, SDL_GetError());
		return -1;
	}
	for (k = 0; k < ix->count; k++) failures += check_movie(a, file, k, dir, &frames) != 0;
	SDL_CloseIO(file);
	kp_dev_line("cine check: %d movies, %llu frames, %d failures", ix->count, (unsigned long long)frames, failures);
	return failures;
}

void kp_app_cine_skip(KpApp *a, uint64_t when_ns) { kp_cine_run_skip(&a->cine.run, when_ns); }

void kp_app_cine_stop(KpApp *a) { kp_cine_run_stop(&a->cine.run); }

void kp_app_cine_free(KpApp *a)
{
	kp_app_cine_stop(a);
}

#endif
