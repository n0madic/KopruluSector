/*
 * The product's entry (plan phase S, stage S5; ADR 0012): SDL's main callbacks (SDL_MAIN_USE_CALLBACKS, which the web
 * and iOS need) over one KpApp: the window with its GL context (GLES 3.0 / WebGL2 on the phones and the web, GL 4.1
 * core on macOS, 3.3 core on Windows and Linux), the audio, the data, then each frame the flow, the drawing and the
 * development checks.
 *
 *   koprulu [--data DIR] [--mission NAME [--play]] [--scale-mode auto|integer|sharp] [--fog off] [--world-only]
 *           [--autotest [N]] [--play-check N] [--screenshot PATH [--after SECONDS]] [--bench-frames N] [--no-vsync]
 *           [--window WxH] [--font original|vector|auto]
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

typedef struct {
	KpApp app;
	int ready;    /* the data loaded and the flow started (the web waits for its files first) */
	int failed;
	char error[512];
} Main;

static void usage_flags(KpAppFlags *f, int argc, char **argv)
{
	int k;
	memset(f, 0, sizeof *f);
	f->fog = 1;
	f->vsync = 1;
	f->scale_mode = KP_SCALE_AUTO;
	f->font_mode = -1;
	f->win_w = 1280;
	f->win_h = 960;
	for (k = 1; k < argc; k++) {
		const char *s = argv[k], *next = k + 1 < argc ? argv[k + 1] : NULL;
		if (strcmp(s, "--data") == 0 && next) snprintf(f->data, sizeof f->data, "%s", argv[++k]);
		else if (strcmp(s, "--mission") == 0 && next) snprintf(f->mission, sizeof f->mission, "%s", argv[++k]);
		else if (strcmp(s, "--play") == 0) f->play = 1;
		else if (strcmp(s, "--fog") == 0 && next) f->fog = strcmp(argv[++k], "off") != 0;
		else if (strcmp(s, "--world-only") == 0) f->world_only = 1;
		else if (strcmp(s, "--no-vsync") == 0) f->vsync = 0;
		else if (strcmp(s, "--autotest") == 0) {
			f->autotest = 1;
			if (next && next[0] != '-') f->autotest_count = atoi(argv[++k]);
		} else if (strcmp(s, "--play-check") == 0 && next) f->play_check = atoi(argv[++k]);
		else if (strcmp(s, "--screenshot") == 0 && next) snprintf(f->screenshot, sizeof f->screenshot, "%s", argv[++k]);
		else if (strcmp(s, "--after") == 0 && next) f->screenshot_after = atof(argv[++k]);
		else if (strcmp(s, "--bench-frames") == 0 && next) f->bench_frames = atoi(argv[++k]);
		else if (strcmp(s, "--window") == 0 && next) sscanf(argv[++k], "%dx%d", &f->win_w, &f->win_h);
		else if (strcmp(s, "--font") == 0 && next) {
			const char *m = argv[++k];
			f->font_mode = strcmp(m, "original") == 0 ? KP_VTEXT_ORIGINAL : strcmp(m, "vector") == 0 ? KP_VTEXT_VECTOR
				: KP_VTEXT_AUTO;
		} else if (strcmp(s, "--scale-mode") == 0 && next) {
			const char *m = argv[++k];
			f->scale_mode = strcmp(m, "integer") == 0 ? KP_SCALE_INTEGER : strcmp(m, "sharp") == 0 ? KP_SCALE_SHARP
				: KP_SCALE_AUTO;
		}
	}
	/* The checks compare pixels, not pacing; with vsync Cocoa's swap waits on the display link, which stops while the
	 * display sleeps, and an unattended run would block in SDL_GL_SwapWindow forever. */
	if (f->autotest || f->play_check) f->vsync = 0;
	/* the checks compare the canvas's and the window's pixels with the original's indices: the bitmap text */
	if (f->font_mode < 0) f->font_mode = f->autotest || f->play_check ? KP_VTEXT_ORIGINAL : KP_VTEXT_AUTO;
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

static void window_size(KpApp *a)
{
	int w, h, pw, ph;
	SDL_GetWindowSize(a->window, &w, &h);
	SDL_GetWindowSizeInPixels(a->window, &pw, &ph);
	a->win_w = pw > 0 ? pw : 1;
	a->win_h = ph > 0 ? ph : 1;
	a->density = w > 0 ? (float)pw / (float)w : 1.0f;
}

static int start(Main *m)
{
	KpApp *a = &m->app;
	if (kp_app_load_data(a, m->error, sizeof m->error) != 0) return -1;
	kp_audio_init(&a->audio, a->sounds_bytes, a->sounds_size);
	if (kp_flow_init(a, m->error, sizeof m->error) != 0) return -1;
	if (kp_dev_init(a) != 0) return -1;
	return 0;
}

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
	SDL_SetAppMetadata(KP_APP_NAME, "0.1", "com.koprulusector.game");
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
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
	api.glsl_version = 410;
#else
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
	api.glsl_version = 330;
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
	a->gl = SDL_GL_CreateContext(a->window);
#if !defined(SDL_PLATFORM_ANDROID) && !defined(SDL_PLATFORM_IOS) && !defined(__EMSCRIPTEN__) && !defined(SDL_PLATFORM_MACOS)
	if (!a->gl) { /* a driver without 3.3 core (Wine over macOS's GL): 4.1 core, forward compatible */
		SDL_Log("GL 3.3 core: %s; trying 4.1 core", SDL_GetError());
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
		SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
		api.glsl_version = 410;
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
	SDL_StartTextInput(a->window); /* TODO: the phones' soft keyboard (plan phase 10) */
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
	window_size(a);
	if (context_lost(a)) {
		kp_flow_frame(a);
		return a->quit ? SDL_APP_SUCCESS : SDL_APP_CONTINUE;
	}
	{
		double t0 = kp_app_now(), t1, t2;
		kp_flow_frame(a);
		t1 = kp_app_now();
		kp_world_draw(a);
		t2 = kp_app_now();
		kp_dev_timing(a, (t1 - t0) * 1000, (t2 - t1) * 1000);
	}
	kp_dev_frame(a);
	SDL_GL_SwapWindow(a->window);
	return a->quit ? SDL_APP_SUCCESS : SDL_APP_CONTINUE;
}

static void window_event(KpApp *a, int event)
{
	if (a->session) kp_clock_on_window(&a->session->clock, event, KP_AUTOPAUSE);
	if (event == KP_WINDOW_FOCUS_LOST || event == KP_WINDOW_FOCUS_GAINED) kp_input_focus(a, event == KP_WINDOW_FOCUS_GAINED);
	/* without a pause menu of its own the product ends the autopause when the focus returns */
	if (KP_AUTOPAUSE && event == KP_WINDOW_FOCUS_GAINED && a->session && a->hud_on) kp_session_resume(a->session);
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
		if (m->ready && !m->failed && !a->flags.autotest && !a->flags.play_check) kp_input_event(a, e);
		break;
	}
	return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result)
{
	Main *m = (Main *)appstate;
	(void)result;
	if (!m) return;
	kp_dev_free(&m->app);
	kp_flow_free(&m->app);
	kp_audio_free(&m->app.audio);
	kp_gfx_free(m->app.gfx);
	if (m->app.gl) SDL_GL_DestroyContext(m->app.gl);
	if (m->app.window) SDL_DestroyWindow(m->app.window);
	free(m);
}
