/*
 * A scripted run of the product (koprulu --script FILE --script-out DIR; kp_script.h, ADR 0017): the script parsed at
 * the start (its header sets the flags), then run after each drawn frame. Its inputs go through the window's event
 * handler as SDL's events (kp_input_event), its files go to --script-out, its profile is its own (DIR/pref/), the
 * player's is never opened. On the desktops the exit code says how it went and a watchdog thread ends a run whose
 * frames stopped (a thread of its own, joined before the script is freed: SDL_RemoveTimer would not wait for a timer's
 * callback that is running).
 */
#include "kp_app.h"
#include "kp_script.h"
#include "kp_script_flow.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(SDL_PLATFORM_ANDROID) && !defined(SDL_PLATFORM_IOS) && !defined(__EMSCRIPTEN__)
#define KP_SCRIPT_WATCHDOG 1 /* the desktops: a thread ends a run whose frames stopped (the web has no threads) */
#endif
#define WATCHDOG_TICK_MS 1000
#define SCRIPT_PATH 1100
#define PREF_DIR "pref"
#define FAILURE_SHOT "failure.png"

typedef struct {
	KpApp *a;
	KpScript s;
	KpScriptRun run;
	KpScriptHost h;
	char dir[SCRIPT_PATH]; /* the script's directory with its separator ("" the working directory): its includes */
#ifdef KP_SCRIPT_WATCHDOG
	SDL_Thread *watchdog;
	SDL_Mutex *watch_lock;
	SDL_Condition *watch_wake;
	int watch_stop;        /* under watch_lock: the run is over, the thread returns */
	SDL_AtomicInt last_ms; /* SDL_GetTicks() of the last frame, 0 before the first */
#endif
} Script;

/* --- the names --- */

static const struct {
	const char *name;
	SDL_Scancode scancode;
} KEYS[] = {
	{ "Escape", SDL_SCANCODE_ESCAPE }, { "Return", SDL_SCANCODE_RETURN }, { "Enter", SDL_SCANCODE_KP_ENTER },
	{ "Space", SDL_SCANCODE_SPACE }, { "Tab", SDL_SCANCODE_TAB }, { "Backspace", SDL_SCANCODE_BACKSPACE },
	{ "Delete", SDL_SCANCODE_DELETE }, { "Insert", SDL_SCANCODE_INSERT }, { "Home", SDL_SCANCODE_HOME },
	{ "End", SDL_SCANCODE_END }, { "PageUp", SDL_SCANCODE_PAGEUP }, { "PageDown", SDL_SCANCODE_PAGEDOWN },
	{ "Left", SDL_SCANCODE_LEFT }, { "Right", SDL_SCANCODE_RIGHT }, { "Up", SDL_SCANCODE_UP },
	{ "Down", SDL_SCANCODE_DOWN }, { "LShift", SDL_SCANCODE_LSHIFT }, { "RShift", SDL_SCANCODE_RSHIFT },
	{ "LCtrl", SDL_SCANCODE_LCTRL }, { "RCtrl", SDL_SCANCODE_RCTRL }, { "LAlt", SDL_SCANCODE_LALT },
	{ "RAlt", SDL_SCANCODE_RALT }, { "Back", SDL_SCANCODE_AC_BACK }, { "Semicolon", SDL_SCANCODE_SEMICOLON },
	{ "Comma", SDL_SCANCODE_COMMA }, { "Period", SDL_SCANCODE_PERIOD }, { "Slash", SDL_SCANCODE_SLASH },
	{ "Minus", SDL_SCANCODE_MINUS }, { "Equals", SDL_SCANCODE_EQUALS }, { "Grave", SDL_SCANCODE_GRAVE },
};

/* A key's scancode by its name (any case): a letter, a digit, F1..F12 or KEYS'; SDL_SCANCODE_UNKNOWN none. */
static SDL_Scancode key_scancode(const char *w)
{
	size_t k;
	if (w[0] && !w[1]) {
		int c = SDL_toupper((unsigned char)w[0]);
		if (c >= 'A' && c <= 'Z') return (SDL_Scancode)(SDL_SCANCODE_A + (c - 'A'));
		if (c >= '1' && c <= '9') return (SDL_Scancode)(SDL_SCANCODE_1 + (c - '1'));
		if (c == '0') return SDL_SCANCODE_0;
	}
	if (SDL_toupper((unsigned char)w[0]) == 'F' && w[1] >= '1' && w[1] <= '9') {
		int n = atoi(w + 1);
		if (n >= 1 && n <= 12 && (w[2] == 0 || (n >= 10 && w[3] == 0))) return (SDL_Scancode)(SDL_SCANCODE_F1 + (n - 1));
	}
	for (k = 0; k < sizeof KEYS / sizeof KEYS[0]; k++) {
		if (SDL_strcasecmp(w, KEYS[k].name) == 0) return KEYS[k].scancode;
	}
	return SDL_SCANCODE_UNKNOWN;
}

static int host_name(void *ud, KpScriptKind kind, const char *word, KpScriptName *out)
{
	(void)ud;
	if (kind == KP_SCRIPT_NAME_KEY) {
		SDL_Scancode s = key_scancode(word);
		if (s == SDL_SCANCODE_UNKNOWN) return -1;
		out->id = (int32_t)s;
		out->arg = KP_SCRIPT_ARG_NONE;
		out->arg_kind = KP_SCRIPT_NAME_KEY;
		return 0;
	}
	return kp_script_flow_name(kind, word, out);
}

/* A file's bytes (malloc'd): from the file system, else the data's (a phone's assets: generated/scripts/). */
static char *read_file(KpApp *a, const char *path, size_t *size)
{
	size_t n = 0;
	void *bytes = SDL_LoadFile(path, &n);
	char *copy;
	if (!bytes) return (char *)kp_app_read(a, path, size);
	copy = (char *)malloc(n ? n : 1);
	if (copy) memcpy(copy, bytes, n);
	SDL_free(bytes);
	*size = n;
	return copy;
}

static char *host_read(void *ud, const char *name, size_t *size)
{
	Script *S = (Script *)ud;
	char path[SCRIPT_PATH + KP_SCRIPT_NAME];
	snprintf(path, sizeof path, "%s%s", S->dir, name);
	return read_file(S->a, path, size);
}

/* --- the run's frame --- */

static int host_value(void *ud, const KpScriptRun *run, const KpScriptRef *ref, int64_t *out, char *why, size_t size)
{
	(void)run;
	return kp_script_flow_value(&((Script *)ud)->a->flow, ref, out, why, size);
}

static int host_cond(void *ud, const KpScriptRun *run, const KpScriptRef *ref, char *why, size_t size)
{
	(void)run;
	return kp_script_flow_cond(&((Script *)ud)->a->flow, ref, why, size);
}

static void host_log(void *ud, const char *line)
{
	(void)ud;
	kp_dev_line("%s", line);
}

static void key_event(KpApp *a, SDL_EventType type, SDL_Scancode scancode, SDL_Keymod mod)
{
	SDL_Event e;
	memset(&e, 0, sizeof e);
	e.key.type = type;
	e.key.timestamp = SDL_GetTicksNS(); /* kp_app_cine_skip compares a press's time with the movie's first picture */
	e.key.windowID = SDL_GetWindowID(a->window);
	e.key.scancode = scancode;
	e.key.key = SDL_GetKeyFromScancode(scancode, mod, false);
	e.key.mod = mod;
	e.key.down = type == SDL_EVENT_KEY_DOWN;
	kp_input_event(a, &e);
}

static void text_event(KpApp *a, const char *text)
{
	SDL_Event e;
	memset(&e, 0, sizeof e);
	e.text.type = SDL_EVENT_TEXT_INPUT;
	e.text.timestamp = SDL_GetTicksNS();
	e.text.windowID = SDL_GetWindowID(a->window);
	e.text.text = text;
	kp_input_event(a, &e);
}

static const struct {
	int bit;
	SDL_Scancode scancode;
	SDL_Keymod mod;
} MODS[] = {
	{ KP_SCRIPT_MOD_SHIFT, SDL_SCANCODE_LSHIFT, SDL_KMOD_LSHIFT },
	{ KP_SCRIPT_MOD_CTRL, SDL_SCANCODE_LCTRL, SDL_KMOD_LCTRL },
	{ KP_SCRIPT_MOD_ALT, SDL_SCANCODE_LALT, SDL_KMOD_LALT },
	{ KP_SCRIPT_MOD_GUI, SDL_SCANCODE_LGUI, SDL_KMOD_LGUI },
};
#define MOD_COUNT ((int)(sizeof MODS / sizeof MODS[0]))

/* The character a US layout gives the key without ctrl, alt or gui: a letter (shifted, its capital), a digit unshifted,
 * the space; 0 none. */
static char key_char(SDL_Scancode s, int mods)
{
	int shift = (mods & KP_SCRIPT_MOD_SHIFT) != 0;
	if (mods & (KP_SCRIPT_MOD_CTRL | KP_SCRIPT_MOD_ALT | KP_SCRIPT_MOD_GUI)) return 0;
	if (s >= SDL_SCANCODE_A && s <= SDL_SCANCODE_Z) return (char)((shift ? 'A' : 'a') + (s - SDL_SCANCODE_A));
	if (shift) return s == SDL_SCANCODE_SPACE ? ' ' : 0;
	if (s >= SDL_SCANCODE_1 && s <= SDL_SCANCODE_9) return (char)('1' + (s - SDL_SCANCODE_1));
	if (s == SDL_SCANCODE_0) return '0';
	return s == SDL_SCANCODE_SPACE ? ' ' : 0;
}

/* A key as a keyboard gives it: its modifiers pressed first, its press, the text a layout gives (key_char's unless the
 * script says), its release, the modifiers released. */
static void key_step(KpApp *a, const KpScriptStep *st)
{
	SDL_Scancode s = (SDL_Scancode)st->id;
	SDL_Keymod mod = SDL_KMOD_NONE;
	char text[2] = { 0, 0 };
	int k;
	for (k = 0; k < MOD_COUNT; k++) {
		if (!(st->mods & MODS[k].bit)) continue;
		mod = (SDL_Keymod)(mod | MODS[k].mod);
		key_event(a, SDL_EVENT_KEY_DOWN, MODS[k].scancode, mod);
	}
	key_event(a, SDL_EVENT_KEY_DOWN, s, mod);
	if (st->flag == KP_SCRIPT_KEY_TEXT) {
		text_event(a, st->text);
	} else if (st->flag == KP_SCRIPT_KEY_AUTO && (text[0] = key_char(s, st->mods)) != 0) {
		text_event(a, text);
	}
	key_event(a, SDL_EVENT_KEY_UP, s, mod);
	for (k = MOD_COUNT - 1; k >= 0; k--) {
		if (!(st->mods & MODS[k].bit)) continue;
		mod = (SDL_Keymod)(mod & ~MODS[k].mod);
		key_event(a, SDL_EVENT_KEY_UP, MODS[k].scancode, mod);
	}
}

static void mouse_motion(KpApp *a, float x, float y)
{
	SDL_Event e;
	memset(&e, 0, sizeof e);
	e.motion.type = SDL_EVENT_MOUSE_MOTION;
	e.motion.timestamp = SDL_GetTicksNS();
	e.motion.windowID = SDL_GetWindowID(a->window);
	e.motion.x = x;
	e.motion.y = y;
	kp_input_event(a, &e);
}

static void mouse_button(KpApp *a, Uint8 button, int down, float x, float y)
{
	SDL_Event e;
	memset(&e, 0, sizeof e);
	e.button.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
	e.button.timestamp = SDL_GetTicksNS();
	e.button.windowID = SDL_GetWindowID(a->window);
	e.button.button = button;
	e.button.down = down != 0;
	e.button.clicks = 1;
	e.button.x = x;
	e.button.y = y;
	kp_input_event(a, &e);
}

/* The mouse's steps: moved to the point, then its button's press, release or both. */
static int mouse_step(KpApp *a, const KpScriptStep *st, char *why, size_t size)
{
	int32_t lx, ly;
	float x, y;
	Uint8 button = st->op == KP_SCRIPT_RCLICK ? SDL_BUTTON_RIGHT : SDL_BUTTON_LEFT;
	if (kp_script_flow_point(&a->flow, &st->p, &lx, &ly, why, size) != 0) return -1;
	kp_input_window_point(a, lx, ly, &x, &y);
	mouse_motion(a, x, y);
	if (st->op != KP_SCRIPT_MOVE && st->op != KP_SCRIPT_RELEASE) mouse_button(a, button, 1, x, y);
	if (st->op != KP_SCRIPT_MOVE && st->op != KP_SCRIPT_PRESS) mouse_button(a, button, 0, x, y);
	return 0;
}

static int shot(Script *S, const char *name)
{
	char path[sizeof S->a->flags.script_out + KP_SCRIPT_STRING + 8];
	snprintf(path, sizeof path, "%s/%s.png", S->a->flags.script_out, name);
	return kp_dev_screenshot(S->a, path);
}

static int host_exec(void *ud, KpScriptRun *run, const KpScriptStep *st, char *why, size_t size)
{
	Script *S = (Script *)ud;
	KpApp *a = S->a;
	int r;
	(void)run;
	switch (st->op) {
	case KP_SCRIPT_KEY: key_step(a, st); return 0;
	case KP_SCRIPT_TEXT:
		a->text_vk = 0; /* no key stands for it (kp_hud_text_key) */
		text_event(a, st->text);
		return 0;
	case KP_SCRIPT_MOVE:
	case KP_SCRIPT_CLICK:
	case KP_SCRIPT_RCLICK:
	case KP_SCRIPT_PRESS:
	case KP_SCRIPT_RELEASE: return mouse_step(a, st, why, size);
	case KP_SCRIPT_HANG:
		SDL_Delay((Uint32)st->n);
		return 0;
	case KP_SCRIPT_SHOT:
		if (shot(S, st->text) == 0) return 0;
		snprintf(why, size, "the frame could not be saved");
		return -1;
	default:
		r = kp_script_flow_act(&a->flow, st, why, size);
		if (r != KP_SCRIPT_FLOW_NONE) return r;
		/* TODO: state and ui (stage SC3), the fingers, the video options, saves, menus, picks and checks (stage SC5) */
		snprintf(why, size, "not in this build yet (docs/phase-script.md)");
		return -1;
	}
}

/* --- the run's start and end --- */

#ifdef KP_SCRIPT_WATCHDOG
/* The watchdog's thread: each tick, the frames stopped for longer than the script's watchdog end the program with
 * code 3; watch_stop (signalled) ends it. */
static int SDLCALL watchdog(void *ud)
{
	Script *S = (Script *)ud;
	SDL_LockMutex(S->watch_lock);
	while (!S->watch_stop) {
		Uint32 last, now;
		int32_t since;
		SDL_WaitConditionTimeout(S->watch_wake, S->watch_lock, WATCHDOG_TICK_MS);
		if (S->watch_stop) break;
		last = (Uint32)SDL_GetAtomicInt(&S->last_ms);
		now = (Uint32)SDL_GetTicks();
		/* signed: the frame's mark may be 1 ms ahead of now (its | 1), and the ticks wrap in 32 bits */
		since = (int32_t)(now - last);
		if (last != 0 && since > S->s.watchdog_ms) {
			kp_dev_line("script %s: no frame in %d ms: the watchdog ends the program FAILED", S->s.name, (int)since);
			_Exit(KP_SCRIPT_EXIT_WATCHDOG);
		}
	}
	SDL_UnlockMutex(S->watch_lock);
	return 0;
}

static void watchdog_start(Script *S)
{
	SDL_SetAtomicInt(&S->last_ms, 0);
	if (!(S->watch_lock = SDL_CreateMutex()) || !(S->watch_wake = SDL_CreateCondition())
		|| !(S->watchdog = SDL_CreateThread(watchdog, "kp_watchdog", S))) {
		kp_dev_line("script %s: no watchdog: %s", S->s.name, SDL_GetError());
	}
}

/* The thread woken and joined: nothing reads the script after this. */
static void watchdog_stop(Script *S)
{
	if (S->watchdog) {
		SDL_LockMutex(S->watch_lock);
		S->watch_stop = 1;
		SDL_SignalCondition(S->watch_wake);
		SDL_UnlockMutex(S->watch_lock);
		SDL_WaitThread(S->watchdog, NULL);
		S->watchdog = NULL;
	}
	if (S->watch_wake) SDL_DestroyCondition(S->watch_wake);
	if (S->watch_lock) SDL_DestroyMutex(S->watch_lock);
	S->watch_wake = NULL;
	S->watch_lock = NULL;
}
#endif

/* The flag a script cannot run beside (the checks drive the game themselves, a script's shots and camera are its own),
 * NULL none. */
static const char *refused_flag(const KpAppFlags *f)
{
	if (f->autotest) return "--autotest";
	if (f->play_check) return "--play-check";
	if (f->video_check[0]) return "--video-check";
	if (f->hd_check[0]) return "--hd-check";
	if (f->save_check[0]) return "--save-check";
	if (f->touch_check[0]) return "--touch-check";
	if (f->cine_check[0]) return "--cine-check";
	if (f->cine_play[0]) return "--cine-play";
	if (f->bench_frames) return "--bench-frames";
	if (f->screenshot[0]) return "--screenshot";
	if (f->camera) return "--camera";
	return NULL;
}

int kp_app_script_load(KpApp *a, char *err, size_t err_size)
{
	KpAppFlags *f = &a->flags;
	const char *flag = refused_flag(f), *slash;
	Script *S;
	char *text;
	size_t size = 0;
	KpError e;
	if (!f->script[0] || !f->script_out[0]) {
		snprintf(err, err_size, "%s", f->script[0] ? "--script needs --script-out DIR" : "--script-out needs --script FILE");
		return -1;
	}
	if (flag) {
		snprintf(err, err_size, "%s cannot run with --script", flag);
		return -1;
	}
	if (!(S = (Script *)calloc(1, sizeof *S))) {
		snprintf(err, err_size, "out of memory");
		return -1;
	}
	S->a = a;
	for (slash = f->script + strlen(f->script); slash > f->script && slash[-1] != '/' && slash[-1] != '\\'; slash--) {}
	snprintf(S->dir, sizeof S->dir, "%.*s", (int)(slash - f->script), f->script);
	S->h.ud = S;
	S->h.name = host_name;
	S->h.read = host_read;
	S->h.value = host_value;
	S->h.cond = host_cond;
	S->h.exec = host_exec;
	S->h.log = host_log;
	if (!(text = read_file(a, f->script, &size))) {
		snprintf(err, err_size, "cannot read %s", f->script);
		free(S);
		return -1;
	}
	if (kp_script_parse(&S->s, f->script, text, size, &S->h, &e) != 0) {
		snprintf(err, err_size, "%s", e.msg);
		free(text);
		free(S);
		return -1;
	}
	free(text);
	kp_script_start(&S->run, &S->s);
	/* the header: the command line's --mission wins (one script, several missions); video originals implies the
	 * options' (kp_script_parse) */
	if (S->s.mission[0] && !f->mission[0]) snprintf(f->mission, sizeof f->mission, "%s", S->s.mission);
	f->originals = S->s.options_originals;
	f->index_video = S->s.video_originals;
	a->script = S;
	return 0;
}

int kp_app_script_begin(KpApp *a, char *err, size_t err_size)
{
	Script *S = (Script *)a->script;
	char pref[sizeof a->pref];
	if (!S) return 0;
	snprintf(pref, sizeof pref, "%s/" PREF_DIR, a->flags.script_out);
	/* SDL_CreateDirectory makes the parents too; the player's directory (SDL_GetPrefPath) is never asked for */
	if (!SDL_CreateDirectory(a->flags.script_out) || !SDL_CreateDirectory(pref)) {
		snprintf(err, err_size, "cannot make %s: %s", pref, SDL_GetError());
		return -1;
	}
	snprintf(a->pref, sizeof a->pref, "%s/", pref);
	kp_dev_line("script %s: %d steps, files in %s, profile %s, clock %s", S->s.name, S->s.count, a->flags.script_out, a->pref,
		/* TODO: the fixed clock (stage SC4) */
		S->s.clock_real ? "real" : "real (fixed comes with stage SC4)");
#ifdef KP_SCRIPT_WATCHDOG
	watchdog_start(S);
#endif
	return 0;
}

void kp_app_script_frame(KpApp *a)
{
	Script *S = (Script *)a->script;
	char path[sizeof a->flags.script_out + sizeof FAILURE_SHOT + 1];
	if (!S || S->run.done) return;
#ifdef KP_SCRIPT_WATCHDOG
	SDL_SetAtomicInt(&S->last_ms, (int)SDL_GetTicks() | 1); /* 0 is "no frame yet" */
#endif
	if (kp_script_frame(&S->run, &S->h, (int64_t)(kp_app_now(a) * 1000))) return;
	if (S->run.aborted) {
		/* TODO: failure.ui and failure.state beside it (stage SC3) */
		snprintf(path, sizeof path, "%s/" FAILURE_SHOT, a->flags.script_out);
		(void)kp_dev_screenshot(a, path);
	}
	a->quit = 1;
}

void kp_app_script_stop(KpApp *a, const char *why)
{
	Script *S = (Script *)a->script;
	if (!S || S->run.done) return;
	kp_script_abort(&S->run, &S->h, why);
}

int kp_app_script_end(KpApp *a, int refused)
{
	Script *S = (Script *)a->script;
	int code;
	if (!S) return KP_SCRIPT_EXIT_PASSED;
#ifdef KP_SCRIPT_WATCHDOG
	watchdog_stop(S);
#endif
	if (!refused) kp_app_script_stop(a, "the program ended before the script did");
	code = S->run.failures > 0 || S->run.aborted ? KP_SCRIPT_EXIT_FAILED : KP_SCRIPT_EXIT_PASSED;
	kp_script_free(&S->s);
	free(S);
	a->script = NULL;
	return code;
}
