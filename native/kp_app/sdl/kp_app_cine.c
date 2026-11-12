/*
 * The cinematics (ADR 0015; ADR 0018 stage D7, ADR 0019 stage C4): the data's movies, played as they are. A repack's
 * or a Remastered install's WebM by Remastered's names (a repack keeps a 1.17 install's movies transcoded as the SD
 * set), its HD2 set first, else a 1.17 install's Smacker files, each checked against the reference install's copy by
 * its size; --cinematics picks the set. Each movie is loaded whole when it begins (kp_res), its sound played from
 * memory as the clock (the sound's position) asks for the pictures that kp_cine.c decodes, their planes converted and
 * drawn full screen by kp_gfx_video with black bars to keep the shape of the frames. The queue, the movies' order and
 * the skip rule are kp_cine.c's run (KpCineRun); this is its host: the movies' bytes, the soundtrack's track, the input
 * and the drawing. While a movie plays the flow and the world are not run (SDL_AppIterate); a key, a click or a tap
 * ends it once it has been on screen, as the original's PlayCinematic does (LEFT_UP, RIGHT_UP, Escape, Enter, Space:
 * 0x47ec60).
 *
 * libvpx decodes in one thread on the main thread, with NEON on arm64 (an HD2 frame 1.0 ms on the Fold7, 1.2 on the
 * Mac, ADR 0015) and SSE2..AVX2 on x86-64 when NASM was there at the build (third_party/vpx/kp_vpx.cmake), else from C
 * (the web's wasm: an HD2 frame 3 to 5 ms on the Mac).
 *
 * The web reads a movie of the repack's cinematics group, which lies beside the page, by the range the core's listing
 * gives (kp_res_raw_range): fetched whole before it begins, the screen black meanwhile (KP_CINE_LOAD_WAIT).
 *
 * Remastered's subtitles (locales/<locale>/Assets/Smk/<name>.srt: the language's in effect, else the install's text
 * locale's, else enUS's) are drawn over the bottom of the picture in ui.kpd's text font (kp_caption_render) when the
 * flow shows them (S toggles them, the profile keeps the choice); laid out again when the subtitle, the movie or the
 * picture's size changes.
 */
#include "kp_app.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#define CAPTION_LINE 18.0      /* a subtitle's line is the picture's height over this */
#define CAPTION_WIDTH 0.9      /* and its lines are at most this much of its width */
#define CAPTION_MARGIN 0.04    /* above the picture's bottom by this much of its height */
#define CAPTION_NONE (-2)      /* no subtitle laid out yet: the next frame lays out the one shown */
#define GPU_ERROR_LIMIT 4 /* --cine-check: the mean byte error of a GPU picture against the CPU's (the chroma sampled
                           * between its texels there, copied here; 0 for the sRGB movies) */
#define SMK_FORMAT "smk\\%s.smk" /* a 1.17 install's movie by the profile's name */

static const char *const VARIANT_NAME[2] = { "sd", "hd2" };

static uint64_t host_now_ns(void *ctx)
{
	(void)ctx;
	return SDL_GetTicksNS();
}

/* The movie on its way given up. */
static void fetch_drop(KpApp *a)
{
	kp_fetch_free(a->cine.fetch);
	a->cine.fetch = NULL;
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
	fetch_drop(a);
	kp_input_focus(a, a->window && (SDL_GetWindowFlags(a->window) & SDL_WINDOW_INPUT_FOCUS) != 0);
}

/* The stream of a movie's soundtrack (taken; NULL: none) begun at the music option's volume (the player's own choice,
 * silence included). */
static int play_sound(KpApp *a, SDL_IOStream *io, const char *name)
{
	float g = kp_audio_gain(a->flow.ui ? kp_ui_music_level(a->flow.ui) : 0);
	if (io && MIX_SetTrackIOStream(a->audio.cine, io, true) && MIX_SetTrackGain(a->audio.cine, g)
		&& MIX_PlayTrack(a->audio.cine, 0)) {
		return 1;
	}
	SDL_Log("cinematics: %s plays without sound: %s", name, SDL_GetError());
	return 0;
}

/* The movie's soundtrack from memory (the movie's) */
static int host_sound(void *ctx, const KpCineClip *clip, const uint8_t *data, size_t size)
{
	KpApp *a = (KpApp *)ctx;
	if (!a->audio.cine) return 0;
	return play_sound(a, SDL_IOFromConstMem(data, size), clip->name);
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
	if (t) {
		MIX_StopTrack(t, 0);
		MIX_SetTrackAudio(t, NULL); /* closes the sound's stream */
	}
}

static void host_log(void *ctx, const char *line)
{
	(void)ctx;
	SDL_Log("%s", line);
}

/* --- the movies: WebM by Remastered's names (a repack's, a Remastered install's) or a 1.17 install's Smacker files --- */

/* The data's file of catalog movie k in set variant */
static void movie_name(const KpApp *a, int k, int variant, char *out, size_t size)
{
	const char *name = a->cine.catalog.movies[k].name;
	if (a->cine.webm) {
		char lower[KP_CINE_NAME];
		size_t i;
		for (i = 0; name[i] && i + 1 < sizeof lower; i++) lower[i] = (char)(name[i] >= 'A' && name[i] <= 'Z' ? name[i] - 'A' + 'a' : name[i]);
		lower[i] = 0;
		snprintf(out, size, KP_CINE_WEBM_FORMAT, KP_CINE_WEBM_FOLDER[variant], lower);
	} else {
		snprintf(out, size, SMK_FORMAT, name);
	}
}

typedef struct {
	KpApp *app;
	int variant;
} HasMovie;

/* Whether the data has catalog movie k of the set: a 1.17 install's file, or a WebM an open group holds or the install;
 * one of a repack's group read by ranges that is not open (a computer's and a phone's groups open with the core, so
 * there it is not there) where that group lies beside the web's page. */
static int movie_has(void *ctx, int k)
{
	HasMovie *h = (HasMovie *)ctx;
	KpApp *a = h->app;
	char name[128], file[128];
	uint64_t offset;
	uint32_t size;
	movie_name(a, k, h->variant, name, sizeof name);
	if (!a->cine.webm) return kp_res_exists(a->res, name);
	if (!kp_res_raw_held(a->res, name)) return 0;
	if (!kp_res_raw_range(a->res, name, file, sizeof file, &offset, &size)) return 1;
#ifdef __EMSCRIPTEN__
	{
		char path[1200];
		snprintf(path, sizeof path, "%s/%s", kp_res_repack_dir(a->res), file);
		return kp_app_web_beside(a, path);
	}
#else
	return 0;
#endif
}

/* A movie's subtitles: the .srt of the language in effect (the flow's: --locale, the main menu's choice) when it is not
 * the base's, else of a Remastered install's text locale (the language whose files it downloads), else the base's; the
 * first the data holds. None when no such file is there, or one that does not read (logged). */
static void movie_subtitles(KpApp *a, KpCineMovie *m, const char *movie)
{
	const char *locales[3];
	int count = 0, k, j;
	if (a->flow.text.locale[0] && strcmp(a->flow.text.locale, KP_LOCALE_BASE) != 0) locales[count++] = a->flow.text.locale;
	if (kp_res_text_locale(a->res)[0]) locales[count++] = kp_res_text_locale(a->res);
	locales[count++] = KP_LOCALE_BASE;
	for (k = 0; k < count; k++) {
		char name[160], why[256];
		size_t size;
		uint8_t *srt;
		KpError err;
		for (j = 0; j < k && strcmp(locales[j], locales[k]) != 0; j++) {}
		if (j < k) continue; /* tried */
		snprintf(name, sizeof name, KP_LOCALE_SUBTITLES, locales[k], movie);
		if (!kp_res_raw_held(a->res, name)) continue;
		if (!(srt = kp_res_raw_read(a->res, name, &size, why, sizeof why))) {
			SDL_Log("cinematics: %s: %s", name, why);
			continue;
		}
		if (kp_cine_movie_subtitles(m, srt, size, &err) != 0) SDL_Log("%s (%s): the movie plays without them", err.msg, name);
		free(srt);
		return;
	}
}

/* A WebM's bytes as the clip's movie with its subtitles: 0, or -1 with err. */
static int webm_movie(KpApp *a, const KpCineClip *clip, const uint8_t *data, size_t size, KpCineMovie **movie, KpError *err)
{
	if (!(*movie = kp_cine_movie_webm(data, size, clip, err))) return -1;
	movie_subtitles(a, *movie, clip->name + 4);
	return 0;
}

/* The fetch of the clip's movie (the web: its bytes by their range in the repack's group, kp_res_raw_range) begun:
 * KP_CINE_LOAD_WAIT, or -1 with err when the listings place it nowhere or the fetch does not begin. */
static int fetch_begin(KpApp *a, const KpCineClip *clip, const char *name, KpError *err)
{
	char file[128], path[1200];
	uint64_t offset;
	uint32_t size;
	const char *dir = kp_res_repack_dir(a->res);
	if (!kp_res_raw_range(a->res, name, file, sizeof file, &offset, &size) || !dir[0]) {
		snprintf(err->msg, sizeof err->msg, "cinematics: %s is not in the data", name);
		return -1;
	}
	if (offset > UINT32_MAX - size) {
		snprintf(err->msg, sizeof err->msg, "cinematics: %s lies past 4 GB of %s", name, file);
		return -1;
	}
	snprintf(path, sizeof path, "%s/%s", dir, file);
	if (!(a->cine.fetch = kp_fetch_start(path, (uint32_t)offset, size))) {
		snprintf(err->msg, sizeof err->msg, "cinematics: %s: out of memory", name);
		return -1;
	}
	a->cine.fetch_clip = clip->id;
	a->cine.fetch_size = size;
	return KP_CINE_LOAD_WAIT;
}

/* The movie on its way: KP_CINE_LOAD_WAIT while it comes, else as host_load. */
static int fetch_poll(KpApp *a, const KpCineClip *clip, const char *name, KpCineMovie **movie, KpError *err)
{
	size_t size = 0;
	uint8_t *data;
	int rc, state = kp_fetch_state(a->cine.fetch);
	if (state == KP_FETCH_PENDING) return KP_CINE_LOAD_WAIT;
	if (state != KP_FETCH_DONE || kp_fetch_whole_file(a->cine.fetch)) { /* a server without ranges: not the whole group */
		snprintf(err->msg, sizeof err->msg, "cinematics: %s: %s", name,
			state == KP_FETCH_DONE ? "the server answers no range requests" : "its bytes did not come");
		fetch_drop(a);
		return -1;
	}
	data = kp_fetch_take(a->cine.fetch, &size);
	fetch_drop(a);
	if (!data || size != a->cine.fetch_size) {
		free(data);
		snprintf(err->msg, sizeof err->msg, "cinematics: %s: %lu bytes came, not %u", name, (unsigned long)size,
			(unsigned)a->cine.fetch_size);
		return -1;
	}
	rc = webm_movie(a, clip, data, size, movie, err);
	free(data);
	return rc;
}

/* The run's load: the movie of the clip read whole from the data (a 1.17 copy taken when its size is the reference
 * install's: a movie only shows and plays, it is not hashed), with its subtitles; a WebM of a group the web has not
 * fetched, fetched by its range (KP_CINE_LOAD_WAIT until it has come; another clip asked for gives it up). The
 * longest movie (79 MB of SMK) loads in 0.14 s on the Mac. */
static int host_load(void *ctx, const KpCineClip *clip, KpCineMovie **movie, KpError *err)
{
	KpApp *a = (KpApp *)ctx;
	char name[128], why[256];
	size_t size;
	uint8_t *data;
	int rc;
	*movie = NULL;
	movie_name(a, clip->id, a->cine.run.index.variant, name, sizeof name);
	if (a->cine.fetch && a->cine.fetch_clip != clip->id) fetch_drop(a);
	if (a->cine.fetch) return fetch_poll(a, clip, name, movie, err);
	if (a->cine.webm) {
		if (!(data = kp_res_raw_read(a->res, name, &size, why, sizeof why)))
			return why[0] ? (snprintf(err->msg, sizeof err->msg, "cinematics: %s: %s", name, why), -1)
				: fetch_begin(a, clip, name, err);
		rc = webm_movie(a, clip, data, size, movie, err);
		free(data);
		return rc;
	}
	{
		KpResStatus st;
		data = kp_res_read(a->res, name, &size, &st, why, sizeof why);
	}
	if (!data) {
		snprintf(err->msg, sizeof err->msg, "cinematics: %s: %s", name, why[0] ? why : "not in the install");
		return -1;
	}
	if (kp_cine_catalog_check(&a->cine.catalog, clip->id, size) != 0) {
		free(data);
		snprintf(err->msg, sizeof err->msg, "cinematics: %s differs from the reference install's copy (%u bytes)", name,
			(unsigned)a->cine.catalog.movies[clip->id].size);
		return -1;
	}
	return (*movie = kp_cine_movie_smk(data, size, clip, err)) != NULL ? 0 : -1;
}

/* The data's movies (--data's) as the run's index: WebM by Remastered's names, its HD2 set first (--cinematics picks a
 * set), else a 1.17 install's Smacker files (the SD set); 0 when it has any. */
static int movies_open(KpApp *a, const char *mode)
{
	const KpDefsTable *table = a->res ? kp_defs_table(kp_res_profile(a->res), "CINM") : NULL;
	int variants[2], count = 0, k;
	HasMovie has;
	if (!table) return -1;
	if (kp_cine_catalog(&a->cine.catalog, table->data, table->size) != 0) {
		SDL_Log("cinematics: the profile's CINM does not read");
		return -1;
	}
	if (strcmp(mode, "sd") != 0) variants[count++] = 1;
	if (strcmp(mode, "hd2") != 0) variants[count++] = 0;
	has.app = a;
	for (k = 0; k < count + 1; k++) { /* the WebM sets, then the Smacker files */
		a->cine.webm = k < count;
		has.variant = a->cine.webm ? variants[k] : 0;
		if (!a->cine.webm && variants[count - 1] != 0) break; /* hd2 alone: no Smacker set */
		kp_cine_index_movies(&a->cine.run.index, has.variant, &a->cine.catalog, movie_has, &has);
		if (a->cine.run.index.count > 0) {
			a->cine.run.have = 1;
			SDL_Log("cinematics: %s %s, %d movies", VARIANT_NAME[has.variant], a->cine.webm ? "WebM" : "Smacker (1.17 install)",
				a->cine.run.index.count);
			return 0;
		}
	}
	memset(&a->cine.run.index, 0, sizeof a->cine.run.index);
	return -1;
}

int kp_app_cine_open(KpApp *a)
{
	const char *mode = a->flags.cinematics[0] ? a->flags.cinematics : "auto";
	KpCineHost *h = &a->cine.run.host;
	memset(&a->cine, 0, sizeof a->cine);
	a->cine.caption_cue = CAPTION_NONE;
	h->ctx = a;
	h->now_ns = host_now_ns;
	h->begin = host_begin;
	h->end = host_end;
	h->load = host_load;
	h->sound = host_sound;
	h->sound_ms = host_sound_ms;
	h->sound_stop = host_sound_stop;
	h->log = host_log;
	if (strcmp(mode, "off") == 0) return 0;
	if (strcmp(mode, "sd") != 0 && strcmp(mode, "hd2") != 0 && strcmp(mode, "auto") != 0) {
		SDL_Log("cinematics: --cinematics %s is not auto, sd, hd2 or off; auto", mode);
		mode = "auto";
	}
	if (movies_open(a, mode) != 0) SDL_Log("cinematics: none in the data (%s): the game plays without them", mode);
	return 0;
}

int kp_app_cine_has(const KpApp *a, int id) { return kp_cine_run_has(&a->cine.run, id); }

int kp_app_cine_active(const KpApp *a) { return a->cine.run.active; }

#define CAPTION_WHITE 230 /* --cine-check: a subtitle's text pixel, every channel at least this */

/* Subtitle cue of subs laid out for a picture of rw x rh window pixels (on whole pixels) and given to kp_gfx_video;
 * when out_img is not NULL its image kept there (the caller's to free) and its top-left pixel in the picture in
 * out_x, out_y: 0, or -1 (no font, nothing to lay out; logged) with none. */
static int caption_upload(KpApp *a, const KpCineSubs *subs, int cue, int32_t rw, int32_t rh, KpCaption *out_img,
	int32_t *out_x, int32_t *out_y)
{
	const uint8_t *const *files;
	const size_t *sizes;
	KpCaption img;
	float rect[4];
	int32_t margin, top;
	kp_gfx_video_caption(a->gfx, NULL, 0, 0, NULL);
	if (!a->flow.ui || kp_ui_vector_fonts(a->flow.ui, &files, &sizes) < 1) {
		if (!a->cine.caption_no_font) SDL_Log("cinematics: no font for the subtitles (ui.kpd's VFNT)");
		a->cine.caption_no_font = 1;
		return -1;
	}
	memset(&img, 0, sizeof img);
	if (kp_caption_render(&img, files[0], sizes[0], subs->text + subs->cues[cue].at, subs->cues[cue].size,
		(float)(rh / CAPTION_LINE), (int32_t)(rw * CAPTION_WIDTH)) != 0) {
		SDL_Log("cinematics: subtitle %d cannot be laid out", cue);
		return -1;
	}
	margin = (int32_t)(rh * CAPTION_MARGIN);
	top = rh - margin - img.h;
	if (top < 0) top = 0;
	rect[0] = (float)((rw - img.w) / 2) / (float)rw;
	rect[2] = rect[0] + (float)img.w / (float)rw;
	rect[1] = (float)top / (float)rh;
	rect[3] = rect[1] + (float)img.h / (float)rh;
	kp_gfx_video_caption(a->gfx, img.rgba, img.w, img.h, rect);
	if (out_img) {
		*out_img = img;
		*out_x = (rw - img.w) / 2;
		*out_y = top;
	} else {
		kp_caption_free(&img);
	}
	return 0;
}

/* The subtitle shown now over a picture of rw x rh window pixels: laid out and uploaded when it, the movie or that size
 * changed; none while the flow hides them, between subtitles or without a font. */
static void caption(KpApp *a, int32_t rw, int32_t rh)
{
	KpAppCine *c = &a->cine;
	int cue = kp_flow_subtitles(&a->flow) ? kp_cine_run_cue(&c->run) : -1;
	if (cue == c->caption_cue && c->run.movies == c->caption_movie && rw == c->caption_w && rh == c->caption_h) return;
	c->caption_cue = cue;
	c->caption_movie = c->run.movies;
	c->caption_w = rw;
	c->caption_h = rh;
	if (cue < 0) kp_gfx_video_caption(a->gfx, NULL, 0, 0, NULL);
	else caption_upload(a, &c->run.subs, cue, rw, rh, NULL, NULL, NULL);
}

void kp_app_cine_lost(KpApp *a) { a->cine.caption_cue = CAPTION_NONE; }

/* The picture over the safe area, the shape of the movie kept: pic the new picture, NULL the movie's one drawn last
 * (black while the movie has drawn none: the textures may hold the movie's before it), the subtitle over it. */
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
	caption(a, rw, rh);
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

/* The player's picture drawn by the GPU (kp_gfx_video, with the subtitle given to it) 1:1 at the window's top-left and
 * read back into a buffer of *w x *h (the part the window holds, rows bottom-up); NULL when it cannot be drawn or read. */
static uint8_t *gpu_picture(KpApp *a, KpCinePlayer *p, int32_t *w, int32_t *h)
{
	const KpCineClip *clip = kp_cine_clip(p);
	KpVideoPicture pic;
	KpGfxFrame f;
	uint8_t *got;
	*w = clip->width < a->flow.full_w ? clip->width : a->flow.full_w;
	*h = clip->height < a->flow.full_h ? clip->height : a->flow.full_h;
	if (!a->gfx || *w <= 0 || *h <= 0 || kp_cine_picture(p, &pic) != 0) return NULL;
	if (!(got = (uint8_t *)malloc((size_t)*w * (size_t)*h * 4))) return NULL;
	memset(&f, 0, sizeof f);
	f.win_x = 0;
	f.win_y = a->flow.full_h - clip->height; /* the picture's row r at the window's GL row full_h - 1 - r */
	f.win_w = clip->width;
	f.win_h = clip->height;
	f.full_w = a->flow.full_w;
	f.full_h = a->flow.full_h;
	kp_gfx_video(a->gfx, &pic, clip->width, clip->height, &f);
	if (kp_gfx_read_window(a->gfx, 0, a->flow.full_h - *h, *w, *h, got) != 0) {
		free(got);
		return NULL;
	}
	return got;
}

/* The mean of the GPU's picture's bytes' (RGB) difference from kp_cine_rgba's (no subtitle over it); -1 when it cannot
 * be drawn or read. */
static double gpu_error(KpApp *a, KpCinePlayer *p)
{
	const KpCineClip *clip = kp_cine_clip(p);
	const uint8_t *ref = kp_cine_rgba(p);
	uint8_t *got;
	uint64_t sum = 0;
	int32_t x, y, w, h;
	int k;
	kp_gfx_video_caption(a->gfx, NULL, 0, 0, NULL);
	if (!ref || !(got = gpu_picture(a, p, &w, &h))) return -1;
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

/* The player's picture with subtitle cue over it, drawn by the GPU and saved as dir/<name>_subtitle.png: 0 when three
 * quarters of the subtitle's white pixels (drawn 1:1 on whole pixels, they stay white) are white in that picture, and
 * more of them than in the picture alone; -1 (logged) when not. */
static int check_caption(KpApp *a, KpCinePlayer *p, const KpCineSubs *subs, int cue, const char *dir)
{
	const KpCineClip *clip = kp_cine_clip(p);
	const uint8_t *ref = kp_cine_rgba(p);
	uint8_t *got = NULL, *top;
	KpCaption img;
	int32_t x, y, x0 = 0, y0 = 0, w = 0, h = 0, white = 0, seen = 0, before = 0;
	memset(&img, 0, sizeof img);
	if (caption_upload(a, subs, cue, clip->width, clip->height, &img, &x0, &y0) != 0 || !ref
		|| !(got = gpu_picture(a, p, &w, &h))) {
		kp_dev_line("cine check %s: subtitle %d not drawn", clip->name, cue);
		kp_gfx_video_caption(a->gfx, NULL, 0, 0, NULL);
		kp_caption_free(&img);
		return -1;
	}
	kp_gfx_video_caption(a->gfx, NULL, 0, 0, NULL);
	if (!(top = (uint8_t *)malloc((size_t)w * (size_t)h * 4))) {
		free(got);
		kp_caption_free(&img);
		return -1;
	}
	for (y = 0; y < h; y++) { /* rows top first, as the reference */
		memcpy(top + (size_t)y * (size_t)w * 4, got + (size_t)(h - 1 - y) * (size_t)w * 4, (size_t)w * 4);
	}
	free(got);
	for (y = 0; y < img.h; y++) {
		for (x = 0; x < img.w; x++) {
			int32_t px = x0 + x, py = y0 + y;
			const uint8_t *g, *r;
			if (img.rgba[4 * ((size_t)y * (size_t)img.w + (size_t)x)] < CAPTION_WHITE) continue;
			white++;
			if (px < 0 || py < 0 || px >= w || py >= h) continue;
			g = top + ((size_t)py * (size_t)w + (size_t)px) * 4;
			r = ref + ((size_t)py * clip->width + (size_t)px) * 4;
			seen += g[0] >= CAPTION_WHITE && g[1] >= CAPTION_WHITE && g[2] >= CAPTION_WHITE;
			before += r[0] >= CAPTION_WHITE && r[1] >= CAPTION_WHITE && r[2] >= CAPTION_WHITE;
		}
	}
	kp_caption_free(&img);
	if (w == clip->width && h == clip->height) save_picture(clip, top, dir, "subtitle");
	free(top);
	kp_dev_line("cine check %s: subtitle %d at %u ms, %d of its %d white pixels seen (%d white without it)", clip->name,
		cue, (unsigned)subs->cues[cue].start_ms, (int)seen, (int)white, (int)before);
	if (white == 0 || seen * 4 < white * 3 || seen <= before) {
		kp_dev_line("cine check %s: the subtitle is not seen", clip->name);
		return -1;
	}
	return 0;
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

/* Movie k of the index loaded for the check (*movie, the caller's to free) and opened, its subtitles into subs. NULL
 * with the reason in err. */
static KpCinePlayer *check_open(KpApp *a, int k, KpCineMovie **movie, KpCineSubs *subs, KpError *err)
{
	KpCinePlayer *p;
	int rc = host_load(a, &a->cine.run.index.clips[k], movie, err);
	subs->count = 0;
	if (rc == KP_CINE_LOAD_WAIT) { /* the check plays nothing: it reads the movies at once */
		fetch_drop(a);
		snprintf(err->msg, sizeof err->msg, "fetched by ranges as the movies play (--cine-play plays one)");
		return NULL;
	}
	if (rc != 0) return NULL;
	if (!(p = kp_cine_open_movie(*movie, err))) {
		kp_cine_movie_free(*movie);
		*movie = NULL;
		return NULL;
	}
	*subs = *kp_cine_movie_subs(*movie);
	return p;
}

/* One movie decoded to its end at the times of its frames, its middle picture also drawn by the GPU: 0, or -1 with
 * the reason logged. The line logged gives the mean and longest kp_cine_advance a frame (the decoding) and the CRC-32
 * of every picture's planes in turn (the same in any build of libvpx: its SIMD is bit-exact with its C), and the time
 * the movie's loading took (its read, its check, its sound decoded). */
static int check_movie(KpApp *a, int k, const char *dir, uint64_t *frames_total)
{
	const KpCineClip *clip = &a->cine.run.index.clips[k];
	KpCinePlayer *p;
	KpCineMovie *movie;
	uint64_t load_ms;
	KpError err;
	uint32_t n, pictures = 0;
	uint64_t t0 = SDL_GetTicks(), advance = 0, advance_max = 0;
	double gpu = -1, tick_ms = 1000.0 / (double)SDL_GetPerformanceFrequency();
	uint32_t crc = 0;
	KpVideoPicture pic;
	KpCineSubs *subs = &a->cine.run.subs; /* the run's: --cine-check plays no run */
	int rc = 0, captioned = 0;
	if (!(p = check_open(a, k, &movie, subs, &err))) {
		kp_dev_line("cine check %s: %s", clip->name, err.msg);
		return -1;
	}
	load_ms = SDL_GetTicks() - t0;
	clip = kp_cine_clip(p);
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
			if (!captioned && subs->count > 0 && kp_cine_frame_ms(clip, n + 1) > subs->cues[0].start_ms) {
				captioned = 1; /* the first subtitle over the picture shown when it begins (it may end within it) */
				if (check_caption(a, p, subs, 0, dir) != 0) rc = -1;
			}
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
	if (rc == 0 && subs->count > 0 && !captioned) {
		kp_dev_line("cine check %s: its first subtitle is over no picture", clip->name);
		rc = -1;
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
		"planes crc %08x, %llu ms (loaded in %llu)%s", clip->name, clip->width, clip->height, (unsigned)clip->frames,
		(unsigned)pictures, gpu, n ? (double)advance * tick_ms / (double)n : 0.0, (double)advance_max * tick_ms,
		(unsigned)crc, (unsigned long long)(SDL_GetTicks() - t0), (unsigned long long)load_ms, rc ? ", FAILED" : "");
	*frames_total += kp_cine_decoded(p);
	kp_cine_free(p);
	kp_cine_movie_free(movie);
	subs->count = 0;
	a->cine.caption_cue = CAPTION_NONE;
	return rc;
}

int kp_app_cine_check(KpApp *a, const char *dir)
{
	const KpCineIndex *ix = &a->cine.run.index;
	int k, failures = 0;
	uint64_t frames = 0;
	if (!a->cine.run.have) {
		kp_dev_line("cine check: no movies (--cinematics %s)", a->flags.cinematics[0] ? a->flags.cinematics : "auto");
		return -1;
	}
	kp_app_cine_stop(a); /* a movie of --cine-play keeps its own player */
	if (!SDL_CreateDirectory(dir)) kp_dev_line("cine check: %s: %s", dir, SDL_GetError());
	for (k = 0; k < ix->count; k++) failures += check_movie(a, k, dir, &frames) != 0;
	kp_dev_line("cine check: %d movies, %llu frames, %d failures", ix->count, (unsigned long long)frames, failures);
	return failures;
}

void kp_app_cine_skip(KpApp *a, uint64_t when_ns) { kp_cine_run_skip(&a->cine.run, when_ns); }

void kp_app_cine_stop(KpApp *a) { kp_cine_run_stop(&a->cine.run); }

void kp_app_cine_free(KpApp *a)
{
	kp_app_cine_stop(a);
	fetch_drop(a);
}
