/*
 * The product's entry (plan phase S, stage S5; ADR 0012): SDL's main callbacks (SDL_MAIN_USE_CALLBACKS, which the web
 * and iOS need) over one KpApp: the window with its GL context (GLES 3.0 / WebGL2 on the phones and the web, GL 4.1
 * core on macOS, 3.3 core on Windows and Linux), the audio, the data, then each frame the flow, the drawing and the
 * development checks.
 *
 *   koprulu [--data DIR] [--mission NAME [--play]] [--scale-mode auto|integer|sharp] [--fog off|original|smooth]
 *           [--water on|off] [--world-only]
 *           [--autotest [N]] [--play-check N] [--screenshot PATH [--after SECONDS] [--camera X,Y]] [--bench-frames N]
 *           [--frame-cap HZ]
 *           [--no-vsync] [--window WxH] [--font original|vector|auto] [--vector-font PATH] [--vector-size F]
 *           [--graphics original|hd2|hd|auto] [--hd-budget MB] [--hd-decode] [--lighting on|off]
 *           [--video-check DIR] [--hd-check DIR]
 */
#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "kp_app.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#if defined(SDL_PLATFORM_ANDROID) || defined(SDL_PLATFORM_IOS)
#define KP_AUTOPAUSE 1 /* mobile devices pause without the focus */
#else
#define KP_AUTOPAUSE 0
#endif

#if defined(__EMSCRIPTEN__) || defined(SDL_PLATFORM_IOS)
#define KP_DISPLAY_LOOP 1 /* the display calls SDL_AppIterate */
#endif
#define KP_FRAME_SLACK_S 0.002 /* a display's beat a little early still draws: a timer's limit would lose a whole frame */

typedef struct {
	KpApp app;
	int ready;    /* the data loaded and the flow started (the web waits for its files first) */
	int failed;
	double next_frame; /* when the frame limit lets the next frame be shown */
	char error[512];
} Main;

static void usage_flags(KpAppFlags *f, int argc, char **argv)
{
	int k;
	memset(f, 0, sizeof *f);
	f->fog = 1;
	f->vsync = 1;
	f->scale_mode = KP_SCALE_AUTO;
	f->font_mode = f->graphics = f->fog_style = f->water = f->lighting = KP_VIDEO_AUTO;
	f->vector_size = 1.0;
	f->win_w = KP_APP_WINDOW_W;
	f->win_h = KP_APP_WINDOW_H;
	for (k = 1; k < argc; k++) {
		const char *s = argv[k], *next = k + 1 < argc ? argv[k + 1] : NULL;
		if (strcmp(s, "--data") == 0 && next) snprintf(f->data, sizeof f->data, "%s", argv[++k]);
		else if (strcmp(s, "--mission") == 0 && next) snprintf(f->mission, sizeof f->mission, "%s", argv[++k]);
		else if (strcmp(s, "--play") == 0) f->play = 1;
		else if (strcmp(s, "--fog") == 0 && next) { /* off, or the fog's style */
			const char *m = argv[++k];
			f->fog = strcmp(m, "off") != 0;
			if (strcmp(m, "original") == 0) f->fog_style = KP_FOG_ORIGINAL;
			else if (strcmp(m, "smooth") == 0) f->fog_style = KP_FOG_SMOOTH;
			if (f->fog_style != KP_VIDEO_AUTO) f->video_given |= KP_VIDEO_GIVEN_FOG;
		} else if (strcmp(s, "--water") == 0 && next) {
			f->water = strcmp(argv[++k], "off") != 0 ? KP_WATER_ANIMATED : KP_WATER_STILL;
			f->video_given |= KP_VIDEO_GIVEN_WATER;
		}
		else if (strcmp(s, "--world-only") == 0) f->world_only = 1;
		else if (strcmp(s, "--no-vsync") == 0) f->vsync = 0;
		else if (strcmp(s, "--autotest") == 0) {
			f->autotest = 1;
			if (next && next[0] != '-') f->autotest_count = atoi(argv[++k]);
		} else if (strcmp(s, "--play-check") == 0 && next) f->play_check = atoi(argv[++k]);
		else if (strcmp(s, "--screenshot") == 0 && next) snprintf(f->screenshot, sizeof f->screenshot, "%s", argv[++k]);
		else if (strcmp(s, "--video-check") == 0 && next) snprintf(f->video_check, sizeof f->video_check, "%s", argv[++k]);
		else if (strcmp(s, "--hd-check") == 0 && next) snprintf(f->hd_check, sizeof f->hd_check, "%s", argv[++k]);
		else if (strcmp(s, "--save-check") == 0 && next) snprintf(f->save_check, sizeof f->save_check, "%s", argv[++k]);
		else if (strcmp(s, "--touch-check") == 0 && next) snprintf(f->touch_check, sizeof f->touch_check, "%s", argv[++k]);
		else if (strcmp(s, "--cinematics") == 0 && next) snprintf(f->cinematics, sizeof f->cinematics, "%s", argv[++k]);
		else if (strcmp(s, "--subtitles") == 0 && next) snprintf(f->subtitles, sizeof f->subtitles, "%s", argv[++k]);
		else if (strcmp(s, "--cine-check") == 0 && next) snprintf(f->cine_check, sizeof f->cine_check, "%s", argv[++k]);
		else if (strcmp(s, "--cine-play") == 0 && next) snprintf(f->cine_play, sizeof f->cine_play, "%s", argv[++k]);
		else if (strcmp(s, "--after") == 0 && next) f->screenshot_after = atof(argv[++k]);
		else if (strcmp(s, "--camera") == 0 && next) f->camera = sscanf(argv[++k], "%d,%d", &f->camera_x, &f->camera_y) == 2;
		else if (strcmp(s, "--bench-frames") == 0 && next) f->bench_frames = atoi(argv[++k]);
		else if (strcmp(s, "--frame-cap") == 0 && next) f->frame_cap = atoi(argv[++k]);
		else if (strcmp(s, "--window") == 0 && next) sscanf(argv[++k], "%dx%d", &f->win_w, &f->win_h);
		else if (strcmp(s, "--vector-font") == 0 && next) snprintf(f->vector_font, sizeof f->vector_font, "%s", argv[++k]);
		else if (strcmp(s, "--vector-size") == 0 && next) f->vector_size = atof(argv[++k]);
		else if (strcmp(s, "--font") == 0 && next) {
			const char *m = argv[++k];
			f->font_mode = strcmp(m, "original") == 0 ? KP_VTEXT_ORIGINAL : strcmp(m, "vector") == 0 ? KP_VTEXT_VECTOR
				: KP_VIDEO_AUTO;
			f->video_given |= KP_VIDEO_GIVEN_FONT;
		} else if (strcmp(s, "--graphics") == 0 && next) {
			const char *m = argv[++k];
			f->graphics = strcmp(m, "original") == 0 ? KP_GRAPHICS_ORIGINAL : strcmp(m, "hd2") == 0 ? KP_GRAPHICS_HD2
				: strcmp(m, "hd") == 0 ? KP_GRAPHICS_HD : KP_VIDEO_AUTO;
			f->video_given |= KP_VIDEO_GIVEN_GRAPHICS;
		} else if (strcmp(s, "--hd-budget") == 0 && next) {
			f->hd_budget = atoi(argv[++k]);
		} else if (strcmp(s, "--hd-decode") == 0) {
			f->hd_decode = 1;
		} else if (strcmp(s, "--lighting") == 0 && next) {
			const char *m = argv[++k];
			f->lighting = strcmp(m, "on") == 0 ? KP_LIGHTING_ON : strcmp(m, "off") == 0 ? KP_LIGHTING_OFF : KP_VIDEO_AUTO;
			f->video_given |= KP_VIDEO_GIVEN_LIGHTING;
		} else if (strcmp(s, "--scale-mode") == 0 && next) {
			const char *m = argv[++k];
			f->scale_mode = strcmp(m, "integer") == 0 ? KP_SCALE_INTEGER : strcmp(m, "sharp") == 0 ? KP_SCALE_SHARP
				: KP_SCALE_AUTO;
		}
	}
	/* The checks compare pixels, not pacing; with vsync Cocoa's swap waits on the display link, which stops while the
	 * display sleeps, and an unattended run would block in SDL_GL_SwapWindow forever. */
	if (kp_flow_dev_checks(f)) f->vsync = 0;
	/* the checks compare the canvas's and the window's pixels with the original's indices: the bitmap text, the world's
	 * indexed art, its fog and still water (the profile's video options are not theirs: video_from_profile) */
	if (f->autotest || f->play_check) {
		if (!(f->video_given & KP_VIDEO_GIVEN_FONT)) f->font_mode = KP_VTEXT_ORIGINAL;
		if (!(f->video_given & KP_VIDEO_GIVEN_GRAPHICS)) f->graphics = KP_GRAPHICS_ORIGINAL;
		f->fog_style = KP_FOG_ORIGINAL;
		f->water = KP_WATER_STILL;
	}
}

/* The converted data's root: --data, else the working directory when it holds generated/, else the program's own
 * directory's data/ (a bundle). Android reads the APK's assets and the web its preloaded /data. */
static void data_root(KpAppFlags *f)
{
#if defined(SDL_PLATFORM_ANDROID)
	(void)f;
#elif defined(__EMSCRIPTEN__)
	if (!f->data[0]) snprintf(f->data, sizeof f->data, "/data");
#else
	SDL_PathInfo info;
	const char *base;
	if (f->data[0]) return;
	if (SDL_GetPathInfo("generated/data/dat.kpd", &info)) {
		snprintf(f->data, sizeof f->data, ".");
		return;
	}
	base = SDL_GetBasePath();
	if (base) snprintf(f->data, sizeof f->data, "%sdata", base);
#endif
}

/* The drawable and its safe area: a phone's window lies under its status and navigation bars (Android's edge to edge),
 * whose pixels a game leaves black; the picture's rectangle for the screen drawn last. */
static void window_size(KpApp *a)
{
	int w, h, pw, ph;
	SDL_Rect safe;
	SDL_GetWindowSize(a->window, &w, &h);
	SDL_GetWindowSizeInPixels(a->window, &pw, &ph);
	a->flow.full_w = pw > 0 ? pw : 1;
	a->flow.full_h = ph > 0 ? ph : 1;
	a->density = w > 0 ? (float)pw / (float)w : 1.0f;
	a->flow.safe_w = a->flow.full_w;
	a->flow.safe_h = a->flow.full_h;
	a->flow.safe_x = a->flow.safe_y = 0;
	if (SDL_GetWindowSafeArea(a->window, &safe) && safe.w > 0 && safe.h > 0) {
		int32_t x0 = (int32_t)((float)safe.x * a->density + 0.5f), y0 = (int32_t)((float)safe.y * a->density + 0.5f);
		int32_t x1 = (int32_t)((float)(safe.x + safe.w) * a->density + 0.5f);
		int32_t y1 = (int32_t)((float)(safe.y + safe.h) * a->density + 0.5f);
		if (x0 < 0) x0 = 0;
		if (y0 < 0) y0 = 0;
		if (x1 > a->flow.full_w) x1 = a->flow.full_w;
		if (y1 > a->flow.full_h) y1 = a->flow.full_h;
		if (x1 > x0 && y1 > y0) {
			a->flow.safe_w = x1 - x0;
			a->flow.safe_h = y1 - y0;
			a->flow.safe_x = x0;
			a->flow.safe_y = a->flow.full_h - y1;
		}
	}
	kp_flow_picture(&a->flow);
}

static int start(Main *m)
{
	KpApp *a = &m->app;
	if (kp_app_load_data(a, m->error, sizeof m->error) != 0) return -1;
	kp_audio_init(&a->audio, a->sounds_bytes, a->sounds_size);
	kp_app_cine_open(a);
	if (kp_app_flow_init(a, m->error, sizeof m->error) != 0) return -1;
	if (kp_dev_init(a) != 0) return -1;
	if (a->flags.cine_play[0]) { /* the numbers of --cine-play, played before the first screen */
		int ids[KP_CINE_RUN_QUEUE], n = 0;
		const char *p = a->flags.cine_play;
		while (*p) {
			char *end;
			long v = strtol(p, &end, 10);
			if (end == p) break;
			if (n == KP_CINE_RUN_QUEUE) {
				SDL_Log("cinematics: --cine-play %s: only the first %d", a->flags.cine_play, KP_CINE_RUN_QUEUE);
				break;
			}
			ids[n++] = (int)v;
			p = *end == ',' ? end + 1 : end;
		}
		if (!kp_flow_cinematics(&a->flow, ids, n)) SDL_Log("cinematics: --cine-play %s: nothing to play", a->flags.cine_play);
	}
	return 0;
}

#if defined(SDL_PLATFORM_LINUX) && !defined(SDL_PLATFORM_ANDROID)
/* The window's icon from the staged data (tools/product.sh linux; the other platforms take their bundle's or
 * executable's): without the file the desktop's default stays. */
static void window_icon(KpApp *a)
{
	size_t size;
	uint8_t *png = kp_app_read(a, "generated/icon/icon-256.png", &size);
	SDL_Surface *icon = png ? SDL_LoadPNG_IO(SDL_IOFromConstMem(png, size), true) : NULL;
	if (icon) {
		if (!SDL_SetWindowIcon(a->window, icon)) SDL_Log("SDL_SetWindowIcon: %s", SDL_GetError());
		SDL_DestroySurface(icon);
	} else if (png) {
		SDL_Log("window icon: %s", SDL_GetError());
	}
	free(png);
}
#endif

#if !defined(SDL_PLATFORM_ANDROID) && !defined(SDL_PLATFORM_IOS) && !defined(__EMSCRIPTEN__)
/* The desktop GL core context of GLSL version glsl (KP_GFX_GLSL_*) asked of SDL: 4.1 forward compatible, as macOS
 * gives it. */
static void gl_core(KpGfxApi *api, int glsl)
{
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, glsl / 100);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, glsl / 10 % 10);
	if (glsl >= KP_GFX_GLSL_41) SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
	api->glsl_version = glsl;
}
#endif

SDL_AppResult SDL_AppInit(void **appstate, int argc, char **argv)
{
	Main *m = (Main *)calloc(1, sizeof *m);
	KpApp *a;
	KpGfxApi api;
	char err[512];
	if (!m) return SDL_APP_FAILURE;
	*appstate = m;
	a = &m->app;
	usage_flags(&a->flags, argc, argv);
	data_root(&a->flags);
	SDL_SetAppMetadata(KP_APP_NAME, KP_APP_VERSION, KP_APP_ID);
#if defined(SDL_PLATFORM_ANDROID) || defined(SDL_PLATFORM_IOS)
	/* "auto" shows no soft keyboard once the system lists any keyboard device (a phone's virtual or paired one) */
	SDL_SetHint(SDL_HINT_ENABLE_SCREEN_KEYBOARD, "1");
	SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1"); /* the back gesture is Escape's (kp_app_input.c) */
#endif
	/* the fingers go to the touch layer (ADR 0014), not as a synthetic mouse */
	SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
	if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS)) {
		SDL_Log("SDL_Init: %s", SDL_GetError());
		return SDL_APP_FAILURE;
	}
	memset(&api, 0, sizeof api);
#if defined(SDL_PLATFORM_ANDROID) || defined(SDL_PLATFORM_IOS) || defined(__EMSCRIPTEN__)
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
	api.gles = 1;
#elif defined(SDL_PLATFORM_MACOS)
	gl_core(&api, KP_GFX_GLSL_41);
#else
	gl_core(&api, KP_GFX_GLSL_33);
#endif
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
	SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);
	{
		SDL_WindowFlags flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
#ifdef __EMSCRIPTEN__
		flags |= SDL_WINDOW_FILL_DOCUMENT; /* the page's whole window */
#endif
		a->window = SDL_CreateWindow(KP_APP_NAME, a->flags.win_w, a->flags.win_h, flags);
	}
	if (!a->window) {
		SDL_Log("SDL_CreateWindow: %s", SDL_GetError());
		return SDL_APP_FAILURE;
	}
#if defined(SDL_PLATFORM_LINUX) && !defined(SDL_PLATFORM_ANDROID)
	window_icon(a);
#endif
#if defined(SDL_PLATFORM_ANDROID) || defined(SDL_PLATFORM_IOS)
	/* the phones' immersive mode: no status and navigation bars (a swipe from the edge shows them for a while), the
	 * picture up to the cutouts (the safe area keeps the panels off them) */
	if (!SDL_SetWindowFullscreen(a->window, true)) SDL_Log("SDL_SetWindowFullscreen: %s", SDL_GetError());
#endif
	a->gl = SDL_GL_CreateContext(a->window);
#if !defined(SDL_PLATFORM_ANDROID) && !defined(SDL_PLATFORM_IOS) && !defined(__EMSCRIPTEN__) && !defined(SDL_PLATFORM_MACOS)
	if (!a->gl) { /* a driver without 3.3 core (Wine over macOS's GL): 4.1 core, forward compatible */
		SDL_Log("GL 3.3 core: %s; trying 4.1 core", SDL_GetError());
		gl_core(&api, KP_GFX_GLSL_41);
		a->gl = SDL_GL_CreateContext(a->window);
	}
#endif
	if (!a->gl) {
		SDL_Log("SDL_GL_CreateContext: %s", SDL_GetError());
		return SDL_APP_FAILURE;
	}
	SDL_GL_SetSwapInterval(a->flags.vsync ? 1 : 0);
	SDL_Log("GL %s, %s", kp_gfx_version(), SDL_GetPlatform());
	if (!(a->gfx = kp_gfx_new(&api, err, sizeof err))) {
		SDL_Log("%s", err);
		return SDL_APP_FAILURE;
	}
#if !defined(SDL_PLATFORM_ANDROID) && !defined(SDL_PLATFORM_IOS)
	SDL_StartTextInput(a->window); /* the phones' soft keyboard follows the focused edit control (kp_input_text_sync) */
#endif
#ifdef __EMSCRIPTEN__
	/* the preferences live in IndexedDB: mounted, then read back before the profile loads */
	EM_ASM(
		FS.mkdir('/prefs');
		FS.mount(IDBFS, {}, '/prefs');
		Module.kp_synced = 0;
		FS.syncfs(true, function(err) { Module.kp_synced = 1; if (err) console.error('syncfs: ' + err); });
		/* a lost WebGL context (the tab in the background, the GPU process restarted): restorable once its default
		   is prevented; kp_context 1 lost, 2 restored */
		Module.kp_context = 0;
		Module.canvas.addEventListener('webglcontextlost', function(e) { e.preventDefault(); Module.kp_context = 1; }, false);
		Module.canvas.addEventListener('webglcontextrestored', function() { Module.kp_context = 2; }, false);
	);
	snprintf(a->pref, sizeof a->pref, "/prefs/");
#else
	{
		char *pref = SDL_GetPrefPath(KP_APP_NAME, KP_APP_NAME);
		snprintf(a->pref, sizeof a->pref, "%s", pref ? pref : "./");
		SDL_free(pref);
	}
#endif
	a->started = kp_app_now();
	window_size(a);
	kp_input_init(a);
	return SDL_APP_CONTINUE;
}

/* kp_gfx's resources made again in a restored context and the world's CPU copies uploaded (the canvas, cursor, fog and
 * palette go up on their own). A reset that fails leaves kp_gfx without a backend: nothing is drawn any more. */
static int restore_context(KpApp *a)
{
	char err[256];
	if (kp_gfx_reset(a->gfx, err, sizeof err) != 0) {
		SDL_Log("%s (nothing is drawn any more)", err);
		return -1;
	}
	kp_world_upload(a);
	a->flow.video_available = a->flow.host.video_available(a, a->flow.ui); /* a new context may take other blocks */
	kp_hd_ui_lost(a);
	kp_app_cine_lost(a);
	SDL_Log("gfx: the context was restored");
	return 0;
}

#ifdef __EMSCRIPTEN__
/* While WebGL's context is lost nothing is drawn (the game goes on); once the browser restores it, the resources are
 * made again (kp_context 3: the restore failed, nothing more is drawn). */
static int context_lost(KpApp *a)
{
	int state = EM_ASM_INT(return Module.kp_context | 0;);
	if (state == 1 || state == 3) return 1;
	if (state == 2) {
		if (restore_context(a) != 0) {
			EM_ASM(Module.kp_context = 3;);
			return 1;
		}
		EM_ASM(Module.kp_context = 0;);
	}
	return 0;
}
#else
/* Back from the background (the phones), the context is checked: one that lost kp_gfx's objects is made current
 * again, or made anew when it is gone, and the resources come back as on the web. While no context can be made,
 * nothing is drawn and the next frame tries again. */
static int context_lost(KpApp *a)
{
	if (!a->recheck_context) return 0;
	if (a->gl && !kp_gfx_lost(a->gfx)) {
		a->recheck_context = 0;
		return 0;
	}
	if (!a->gl || !SDL_GL_MakeCurrent(a->window, a->gl)) {
		int first = a->gl != NULL;
		if (first) {
			SDL_Log("gfx: the context is gone after the background");
			SDL_GL_DestroyContext(a->gl);
		}
		if (!(a->gl = SDL_GL_CreateContext(a->window))) {
			if (first) SDL_Log("SDL_GL_CreateContext: %s (tried again each frame)", SDL_GetError());
			return 1;
		}
		SDL_GL_SetSwapInterval(a->flags.vsync ? 1 : 0);
	} else {
		SDL_Log("gfx: the context lost its objects in the background");
	}
	a->recheck_context = 0;
	restore_context(a);
	return 0;
}
#endif

/* The textures kp_gfx could not make in the frames before, logged (each pass without its texture shows nothing). */
static void gfx_failures(KpApp *a)
{
	char msg[256];
	int n = a->gfx ? kp_gfx_take_failures(a->gfx, msg, sizeof msg) : 0;
	if (n > 1) SDL_Log("%s (and %d more)", msg, n - 1);
	else if (n == 1) SDL_Log("%s", msg);
}

/* The frame limit (kp_flow_frame_cap) on the loops the display drives (the web's requestAnimationFrame, iOS's display
 * link): their extra iterations return at once, 1 here, and a frame's work ends before the display's next beat. */
static int frame_skipped(Main *m)
{
#ifdef KP_DISPLAY_LOOP
	int cap = kp_flow_frame_cap(&m->app.flow);
	double now = kp_app_now(), period;
	if (cap <= 0) return 0;
	if (now + KP_FRAME_SLACK_S < m->next_frame) return 1;
	period = 1.0 / cap;
	m->next_frame = m->next_frame + period > now ? m->next_frame + period : now + period;
#else
	(void)m;
#endif
	return 0;
}

/* The frame shown. SDL's own loop (Android, the desktops) runs as the swap lets it: under a limit it sleeps here, so a
 * tick's longer frame and the next short one are shown a period apart (sleeping before the frame's work would show them
 * unevenly). A frame late starts the grid again. */
static void present(Main *m)
{
#ifndef KP_DISPLAY_LOOP
	int cap = kp_flow_frame_cap(&m->app.flow);
	if (cap > 0) {
		double now = kp_app_now(), period = 1.0 / cap;
		if (now < m->next_frame) SDL_DelayNS((Uint64)((m->next_frame - now) * 1e9));
		m->next_frame = m->next_frame + period > now ? m->next_frame + period : now + period;
	}
#endif
	SDL_GL_SwapWindow(m->app.window);
}

SDL_AppResult SDL_AppIterate(void *appstate)
{
	Main *m = (Main *)appstate;
	KpApp *a = &m->app;
	if (!m->ready && !m->failed) {
#ifdef __EMSCRIPTEN__
		if (!EM_ASM_INT(return Module.kp_synced;)) return SDL_APP_CONTINUE;
#endif
		m->ready = 1;
		if (start(m) != 0) {
			m->failed = 1;
			SDL_Log("%s", m->error);
			SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, KP_APP_NAME, m->error, a->window);
			return SDL_APP_FAILURE;
		}
	}
	if (frame_skipped(m)) return SDL_APP_CONTINUE;
	window_size(a);
	kp_input_frame(a);
	gfx_failures(a);
	if (kp_app_cine_active(a)) { /* a movie: the flow and the world wait */
		if (context_lost(a)) { /* the movie goes on unseen (its sound and its decoder) until a context draws it */
			kp_app_cine_frame(a, 0);
			return a->quit ? SDL_APP_SUCCESS : SDL_APP_CONTINUE;
		}
		kp_app_cine_frame(a, 1);
		kp_dev_frame(a);
		present(m);
		return a->quit ? SDL_APP_SUCCESS : SDL_APP_CONTINUE;
	}
	if (context_lost(a)) {
		kp_app_flow_frame(a);
		return a->quit ? SDL_APP_SUCCESS : SDL_APP_CONTINUE;
	}
	{
		double t0 = kp_app_now(), t1, t2;
		kp_app_flow_frame(a);
		if (a->quit) return SDL_APP_SUCCESS; /* the program ends: the scene the flow left is not shown */
		t1 = kp_app_now();
		if (kp_app_cine_active(a)) kp_app_cine_frame(a, 1); /* a movie the flow began: its frame, not the scene it left */
		else kp_world_draw(a);
		t2 = kp_app_now();
		kp_dev_timing(a, (t1 - t0) * 1000, (t2 - t1) * 1000);
	}
	kp_dev_frame(a);
	kp_input_text_sync(a, 0);
	present(m);
	return a->quit ? SDL_APP_SUCCESS : SDL_APP_CONTINUE;
}

static void window_event(KpApp *a, int event)
{
	if (a->flow.session) kp_clock_on_window(&a->flow.session->clock, event, KP_AUTOPAUSE);
	if (event == KP_WINDOW_FOCUS_LOST || event == KP_WINDOW_FOCUS_GAINED) kp_input_focus(a, event == KP_WINDOW_FOCUS_GAINED);
	/* without a pause menu of its own the product ends the autopause when the focus returns */
	if (KP_AUTOPAUSE && event == KP_WINDOW_FOCUS_GAINED && a->flow.session && a->flow.hud_on) kp_session_resume(a->flow.session);
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *e)
{
	Main *m = (Main *)appstate;
	KpApp *a = &m->app;
	switch (e->type) {
	case SDL_EVENT_QUIT:
		return SDL_APP_SUCCESS;
	case SDL_EVENT_WINDOW_FOCUS_LOST:
		window_event(a, KP_WINDOW_FOCUS_LOST);
		break;
	case SDL_EVENT_WINDOW_FOCUS_GAINED:
		window_event(a, KP_WINDOW_FOCUS_GAINED);
		break;
	case SDL_EVENT_WINDOW_MINIMIZED:
	case SDL_EVENT_DID_ENTER_BACKGROUND:
		window_event(a, KP_WINDOW_ICONIFIED);
		break;
	case SDL_EVENT_WINDOW_RESTORED:
		window_event(a, KP_WINDOW_DEICONIFIED);
		break;
	case SDL_EVENT_DID_ENTER_FOREGROUND:
		a->recheck_context = 1;
		window_event(a, KP_WINDOW_DEICONIFIED);
		break;
	default:
		/* the checks give their own input: the desk's mouse over the window would scroll their views */
		if (m->ready && !m->failed && !a->flags.autotest && !a->flags.play_check && !a->flags.touch_check[0]) {
			kp_input_event(a, e);
		}
		break;
	}
	return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result)
{
	Main *m = (Main *)appstate;
	(void)result;
	if (!m) return;
	kp_app_cine_free(&m->app);
	kp_dev_free(&m->app);
	kp_app_flow_free(&m->app);
	kp_hd_ui_free(&m->app);
	free(m->app.web_sizes);
	kp_audio_free(&m->app.audio);
	kp_gfx_free(m->app.gfx);
	if (m->app.gl) SDL_GL_DestroyContext(m->app.gl);
	if (m->app.window) SDL_DestroyWindow(m->app.window);
	free(m);
}
