/*
 * A scripted run of the product (koprulu --script FILE --script-out DIR; kp_script.h, ADR 0017): the script parsed at
 * the start (its header sets the flags), then run after each drawn frame. Its inputs go through the window's event
 * handler as SDL's events (kp_input_event), its files go to --script-out, its profile is its own (DIR/pref/), the
 * player's is never opened. On the desktops the exit code says how it went and a watchdog thread ends a run whose
 * frames stopped (a thread of its own, joined before the script is freed: SDL_RemoveTimer would not wait for a timer's
 * callback that is running). With --script-live FIFO (macOS, Linux) a reader thread queues the FIFO's lines and each
 * frame parses them onto the script before its steps (kp_script_live_line).
 */
#if defined(__linux__) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L /* --script-live's mkfifo, poll and O_CLOEXEC under -std=c99 (glibc hides them) */
#endif
#include "kp_app.h"
#include "kp_script.h"
#include "kp_script_flow.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(SDL_PLATFORM_ANDROID) && !defined(SDL_PLATFORM_IOS) && !defined(__EMSCRIPTEN__)
#define KP_SCRIPT_WATCHDOG 1 /* the desktops: a thread ends a run whose frames stopped (the web has no threads) */
#endif
/* --script-live: a POSIX FIFO read by a thread of its own. TODO: Windows' named pipes; the phones and the web (no FIFO
 * a tool on the computer could write: a socket or adb's forwarding would be theirs). */
#if defined(SDL_PLATFORM_MACOS) || defined(SDL_PLATFORM_LINUX)
#define KP_SCRIPT_LIVE 1
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
#define LIVE_QUEUE 256   /* the lines read and not yet parsed */
#define LIVE_POLL_MS 100 /* the reader's wait for bytes, its stop's latency */
#define LIVE_FULL_MS 10  /* the reader's wait while the queue is full */
#define LIVE_IDLE_MS 10  /* a frame's sleep while the run waits for its next line (it would spin uncapped) */
#define WATCHDOG_TICK_MS 1000
#define SCRIPT_PATH 1100
#define PREF_DIR "pref"
#define FAILURE_NAME "failure" /* an abort's failure.png, failure.ui, failure.state (a script's names never are) */
#define KEY_RACE_BUILD SDL_SCANCODE_COUNT /* key race-build: the local player's race's building key */
#define VIDEO_HD2_SCALE 2 /* the HD variants' art scales (the world's hd_scale) */
#define VIDEO_HD_SCALE 4

/* The host's checks (check NAME): */
enum {
	CHECK_VIDEO_CHOICE, /* the last choose took (its option and the world's art), or a disabled one changed nothing */
	CHECK_TERRAIN,      /* ["LABEL"] the view's terrain, byte for byte (the world's images off) */
	CHECK_FOG_PLAIN,    /* the view without the fog kept for check fog */
	CHECK_FOG,          /* the view with the fog against the kept one shaded on the CPU */
	CHECK_PALETTE,      /* the window's colours against the palette's of the index target */
	CHECK_HD,           /* ["LABEL"] no SD pixel over HD art, no draw outside the image log (after audit hd) */
	CHECK_SAVE_FILE,    /* "NAME": the save's file is there, and goes */
	CHECK_COUNT
};
static const struct {
	const char *name;
	KpScriptArg arg;
} CHECKS[CHECK_COUNT] = {
	{ "video-choice", KP_SCRIPT_ARG_NONE }, { "terrain", KP_SCRIPT_ARG_OPT_TEXT }, { "fog-plain", KP_SCRIPT_ARG_NONE },
	{ "fog", KP_SCRIPT_ARG_NONE }, { "palette", KP_SCRIPT_ARG_NONE }, { "hd", KP_SCRIPT_ARG_OPT_TEXT },
	{ "save-file", KP_SCRIPT_ARG_TEXT },
};

#ifdef KP_SCRIPT_LIVE
/* A line from the FIFO. One byte past the longest line with its \r: a longer line, its bytes past it dropped, stays too
 * long once kp_script_live_line drops a last \r. */
typedef struct {
	size_t len;
	char text[KP_SCRIPT_LINE + 1];
} LiveLine;
#endif

typedef struct {
	KpApp *a;
	KpScript s;
	KpScriptRun run;
	KpScriptHost h;
	char dir[SCRIPT_PATH]; /* the script's directory with its separator ("" the working directory): its includes */
	double wall_ms;        /* the wall clock at the last frame (0 before the first) */
	double movie_ms;       /* under the fixed clock: the wall clock's time in movies, added to the run's clock */
	/* the last choose: its button, whether the dialog offered it, the option's value before the click */
	int16_t choice;
	int chosen, choice_offered, choice_before;
	int audit;             /* audit hd: the canvases count the draws outside the image log */
	uint32_t hd_dropped;   /* the image log's drops at the last check hd (or audit hd) */
#ifdef KP_SCRIPT_WATCHDOG
	SDL_Thread *watchdog;
	SDL_Mutex *watch_lock;
	SDL_Condition *watch_wake;
	int watch_stop;        /* under watch_lock: the run is over, the thread returns */
	SDL_AtomicInt last_ms; /* SDL_GetTicks() of the last frame, 0 before the first */
#endif
#ifdef KP_SCRIPT_LIVE
	/* --script-live: the FIFO opened for reading and writing (its writers may come and go, no end of file comes), the
	 * reader's thread, the lines it read for the frames to parse (LIVE_QUEUE of them, made by live_start) */
	int live_fd;
	SDL_Thread *reader;
	SDL_Mutex *live_lock;
	SDL_AtomicInt live_stop;
	SDL_AtomicInt live_broken;   /* the reader could not read the FIFO: no line will come */
	int queue_head, queue_count; /* under live_lock */
	LiveLine *queue;
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

/* The host's own values (the world's art, the HD interface, the cursor, the mouse, the sound), ids from
 * KP_SCRIPT_FLOW_HOST. */
enum { H_ART_SCALE, H_ART_LIT, H_ART_LIT_PAGES, H_ART_FX, H_HD_UI, H_CURSOR_SHOWN, H_MOUSE_X, H_MOUSE_Y, H_MUSIC_PLAYING,
	H_MUSIC_TRACK, H_SOUNDS_STARTED, H_SOUNDS_STREAMED, H_COUNT };
static const char *const HOST_VALUES[H_COUNT] = {
	"art.scale",        /* the world's HD pixels a logical pixel (2 HD2, 4 HD), 0 the original's art */
	"art.lit",          /* the HD sprites drawn lit */
	"art.lit.pages",    /* the HD packs' lit pages */
	"art.fx",           /* the original's art through the RGBA path (the smooth fog, the water) */
	"hd.ui",            /* the HD interface's art on */
	"cursor.shown",
	"mouse.x",          /* the mouse's last logical point */
	"mouse.y",
	"music.playing",
	"music.track",
	"sounds.started",   /* the sounds the mixer started */
	"sounds.streamed",
};

static int host_name(void *ud, KpScriptKind kind, const char *word, KpScriptName *out)
{
	int k;
	(void)ud;
	out->arg = KP_SCRIPT_ARG_NONE;
	out->arg_kind = KP_SCRIPT_NAME_KEY;
	if (kind == KP_SCRIPT_NAME_KEY) {
		SDL_Scancode s = SDL_strcasecmp(word, "race-build") == 0 ? (SDL_Scancode)KEY_RACE_BUILD : key_scancode(word);
		if (s == SDL_SCANCODE_UNKNOWN) return -1;
		out->id = (int32_t)s;
		return 0;
	}
	if (kind == KP_SCRIPT_NAME_VALUE) {
		for (k = 0; k < H_COUNT; k++) {
			if (SDL_strcasecmp(word, HOST_VALUES[k]) == 0) {
				out->id = KP_SCRIPT_FLOW_HOST + k;
				return 0;
			}
		}
	}
	if (kind == KP_SCRIPT_NAME_CHECK) {
		for (k = 0; k < CHECK_COUNT; k++) {
			if (SDL_strcasecmp(word, CHECKS[k].name) == 0) {
				out->id = k;
				out->arg = CHECKS[k].arg;
				return 0;
			}
		}
		return -1;
	}
	return kp_script_flow_name(kind, word, out);
}

/* A file's bytes (malloc'd): from the file system, else the data's (a debug bundle's generated/scripts/, which
 * tools/product.sh stages from tests/scripts/: a phone's assets, the web's preloaded /data). */
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
	const KpApp *a = ((Script *)ud)->a;
	int k = ref->id - KP_SCRIPT_FLOW_HOST;
	if (k < 0) return kp_script_flow_value(&a->flow, run, ref, out, why, size);
	if (k <= H_ART_FX && !a->world.active) {
		snprintf(why, size, "no game");
		return -1;
	}
	if ((k == H_MOUSE_X || k == H_MOUSE_Y) && !a->has_mouse) {
		snprintf(why, size, "no mouse yet");
		return -1;
	}
	switch (k) {
	case H_ART_SCALE: *out = a->world.hd_scale; break;
	case H_ART_LIT: *out = a->world.hd_lit != 0; break;
	case H_ART_LIT_PAGES: *out = kp_world_hd_lit_pages(a); break;
	case H_ART_FX: *out = a->world.fx != 0; break;
	case H_HD_UI: *out = a->hd_ui.open; break;
	case H_CURSOR_SHOWN: *out = a->cursor.shown != 0; break;
	case H_MOUSE_X: *out = a->mouse_lx; break;
	case H_MOUSE_Y: *out = a->mouse_ly; break;
	case H_MUSIC_PLAYING: *out = a->audio.has_track != 0; break;
	case H_MUSIC_TRACK: *out = a->audio.track; break;
	case H_SOUNDS_STARTED: *out = a->audio.started; break;
	case H_SOUNDS_STREAMED: *out = a->audio.streamed; break;
	default:
		snprintf(why, size, "no value %d", (int)ref->id);
		return -1;
	}
	return 0;
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
static int key_step(KpApp *a, const KpScriptStep *st, char *why, size_t size)
{
	SDL_Scancode s = (SDL_Scancode)st->id;
	SDL_Keymod mod = SDL_KMOD_NONE;
	char text[2] = { 0, 0 };
	int k;
	if (st->id == KEY_RACE_BUILD) {
		char c = kp_script_flow_race_key(&a->flow);
		if (!c) {
			snprintf(why, size, "no worker of the player's to tell its race by");
			return -1;
		}
		s = (SDL_Scancode)(SDL_SCANCODE_A + (c - 'A'));
	}
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
	return 0;
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

/* The mouse at a logical point: moved there, then its button pressed (down 1), released (0) or neither (-1). */
static void mouse_at(KpApp *a, Uint8 button, int down, int32_t lx, int32_t ly)
{
	float x, y;
	kp_input_window_point(a, lx, ly, &x, &y);
	mouse_motion(a, x, y);
	if (down >= 0) mouse_button(a, button, down, x, y);
}

/* The mouse's steps: moved to the point, then its button's press, release or both. */
static int mouse_step(KpApp *a, const KpScriptRun *run, const KpScriptStep *st, char *why, size_t size)
{
	int32_t lx, ly;
	Uint8 button = st->op == KP_SCRIPT_RCLICK ? SDL_BUTTON_RIGHT : SDL_BUTTON_LEFT;
	if (kp_script_flow_point(&a->flow, run, &st->p, &lx, &ly, why, size) != 0) return -1;
	mouse_at(a, button, -1, lx, ly);
	if (st->op != KP_SCRIPT_MOVE && st->op != KP_SCRIPT_RELEASE) mouse_at(a, button, 1, lx, ly);
	if (st->op != KP_SCRIPT_MOVE && st->op != KP_SCRIPT_PRESS) mouse_at(a, button, 0, lx, ly);
	return 0;
}

/* A finger at the point through the touch layer (kp_input_finger: the touch layer's way in; SDL's finger events carry
 * the touch device's normalised coordinates, which a hidden window has no mapping for). */
static int finger_step(KpApp *a, const KpScriptRun *run, const KpScriptStep *st, char *why, size_t size)
{
	static const int TYPES[] = { KP_TOUCH_DOWN, KP_TOUCH_MOVE, KP_TOUCH_UP };
	int32_t lx, ly;
	if (kp_script_flow_point(&a->flow, run, &st->p, &lx, &ly, why, size) != 0) return -1;
	if (st->op == KP_SCRIPT_TAP) {
		kp_input_finger(a, KP_TOUCH_DOWN, st->id, lx, ly);
		kp_input_finger(a, KP_TOUCH_UP, st->id, lx, ly);
	} else {
		kp_input_finger(a, TYPES[st->flag], st->id, lx, ly);
	}
	return 0;
}

/* select NAME RADIUS: the camera on the unit (the flow's), the box drawn over it in one frame. */
static int select_step(KpApp *a, const KpScriptStep *st, char *why, size_t size)
{
	int32_t box[4];
	if (kp_script_flow_select(&a->flow, st, box, why, size) != 0) return -1;
	mouse_at(a, SDL_BUTTON_LEFT, -1, box[0], box[1]);
	mouse_at(a, SDL_BUTTON_LEFT, 1, box[0], box[1]);
	mouse_at(a, SDL_BUTTON_LEFT, -1, box[2], box[3]);
	mouse_at(a, SDL_BUTTON_LEFT, 0, box[2], box[3]);
	return 0;
}

/* --- the video options' choices (choose, check video-choice) --- */

/* An option button's group (its first ID, KP_VIDEO_ID_*). */
static int choice_group(int16_t id) { return id / KP_VIDEO_GROUP_IDS * KP_VIDEO_GROUP_IDS; }

/* The option a button's group sets now (the portraits' mode for its group). */
static int choice_value(const KpApp *a, int16_t id)
{
	const KpUiVideoOptions *o = &a->flow.ui->video_options;
	switch (choice_group(id)) {
	case KP_VIDEO_ID_GRAPHICS: return o->graphics;
	case KP_VIDEO_ID_FOG: return o->fog;
	case KP_VIDEO_ID_WATER: return o->water;
	case KP_VIDEO_ID_FONT: return o->font;
	case KP_VIDEO_ID_LIGHTING: return o->lighting;
	default: return (int)a->flow.ui->portrait.mode;
	}
}

/* The value a button sets (KP_VIDEO_CHOICE_ID's, the portraits' mode). */
static int choice_expected(int16_t id)
{
	return choice_group(id) == KP_VIDEO_ID_PORTRAITS ? id - KP_VIDEO_ID_PORTRAITS : id - choice_group(id) - 1;
}

/* choose: the video dialog's button clicked at its centre; whether it was offered (shown, enabled) and the option's
 * value before it are kept for check video-choice. */
static int choose_step(Script *S, const KpScriptRun *run, const KpScriptStep *st, char *why, size_t size)
{
	KpApp *a = S->a;
	KpScriptPoint p;
	int32_t d = a->flow.ui ? a->flow.ui->modeless : -1, c, lx, ly;
	memset(&p, 0, sizeof p);
	p.kind = KP_SCRIPT_AT_CONTROL;
	p.name = kp_script_flow_control("video", (int16_t)st->id);
	if (kp_script_flow_point(&a->flow, run, &p, &lx, &ly, why, size) != 0) return -1;
	c = d >= 0 ? kp_ui_find_control(a->flow.ui, d, (int16_t)st->id) : -1;
	S->choice = (int16_t)st->id;
	S->chosen = 1;
	S->choice_offered = c >= 0 && !(a->flow.ui->controls[c].flags & KP_UI_DISABLED);
	S->choice_before = choice_value(a, S->choice);
	mouse_at(a, SDL_BUTTON_LEFT, -1, lx, ly);
	mouse_at(a, SDL_BUTTON_LEFT, 1, lx, ly);
	mouse_at(a, SDL_BUTTON_LEFT, 0, lx, ly);
	return 0;
}

/* Appends a failure to why ("; " between them). */
static void add_why(char *why, size_t size, const char *what, int got, int want)
{
	size_t n = strlen(why);
	snprintf(why + n, size - n, "%s%s %d, wanted %d", n ? "; " : "", what, got, want);
}

static int check_video_choice(Script *S, char *why, size_t size)
{
	KpApp *a = S->a;
	int16_t id = S->choice;
	int want;
	why[0] = '\0';
	if (!S->chosen || !a->flow.ui) {
		snprintf(why, size, "no choose before it");
		return -1;
	}
	want = S->choice_offered ? choice_expected(id) : S->choice_before;
	if (choice_value(a, id) != want) add_why(why, size, S->choice_offered ? "the choice" : "a disabled choice", choice_value(a, id), want);
	if (choice_group(id) == KP_VIDEO_ID_GRAPHICS && S->choice_offered && choice_expected(id) != KP_VIDEO_AUTO) {
		int scale = choice_expected(id) == KP_GRAPHICS_HD2 ? VIDEO_HD2_SCALE : choice_expected(id) == KP_GRAPHICS_HD ? VIDEO_HD_SCALE : 0;
		if (a->world.hd_scale != scale) add_why(why, size, "the world's art scale", (int)a->world.hd_scale, scale);
	}
	if (choice_group(id) == KP_VIDEO_ID_LIGHTING && S->choice_offered && a->world.hd_scale) { /* lit when its packs have lit pages */
		int lit = choice_expected(id) != KP_LIGHTING_OFF && kp_world_hd_lit_pages(a) > 0;
		if (a->world.hd_lit != lit) add_why(why, size, "the HD sprites lit", a->world.hd_lit, lit);
	}
	kp_dev_line("script check video-choice: button %d %s, the option %d (before %d)", (int)id,
		S->choice_offered ? "offered" : "disabled", choice_value(a, id), S->choice_before);
	return why[0] ? KP_SCRIPT_CHECK_FAILED : 0;
}

/* --- the checks --- */

/* The canvases' audit of the draws outside the image log, kept on across a new game's canvas. */
static void apply_audit(Script *S)
{
	KpApp *a = S->a;
	if (!S->audit) return;
	a->flow.glue.canvas.canvas.hd_audit = 1;
	if (a->flow.hud.canvas) a->flow.hud.canvas->canvas.hd_audit = 1;
}

/* The save's file is there; then it goes the player's way, the save dialogs' delete (DeleteSaveFile: the file, the
 * profile's name, the profile written, the dialogs' list). */
static int check_save_file(KpApp *a, const char *name, char *why, size_t size)
{
	char file[KP_SAVE_FILE_NAME], path[SCRIPT_PATH];
	SDL_PathInfo info;
	kp_save_file_name(kp_profile_save_owner(&a->profile), name, file, sizeof file);
	kp_app_pref_path(a, file, path, sizeof path);
	if (!SDL_GetPathInfo(path, &info)) {
		snprintf(why, size, "no file %s", path);
		return KP_SCRIPT_CHECK_FAILED;
	}
	if (!a->flow.ui || !kp_ui_delete_save(a->flow.ui, name) || SDL_GetPathInfo(path, &info)) {
		snprintf(why, size, "%s stays (the log says why)", path);
		return KP_SCRIPT_CHECK_FAILED;
	}
	kp_dev_line("script check save-file: %s deleted", path);
	return 0;
}

/* A comparison's result: -1 its frame unreadable (logged), none compared, or some mismatches failed (counted). */
static int compared(int r, int64_t checked, int64_t bad, const char *what, char *why, size_t size)
{
	if (r != 0) snprintf(why, size, "%s: the frame could not be read", what);
	else if (checked == 0) snprintf(why, size, "%s: nothing compared", what);
	else if (bad != 0) snprintf(why, size, "%s: %lld mismatches", what, (long long)bad);
	else return 0;
	return KP_SCRIPT_CHECK_FAILED;
}

static int check_step(Script *S, const KpScriptStep *st, char *why, size_t size)
{
	KpApp *a = S->a;
	int64_t checked = 0, bad = 0;
	int r;
	if (st->id != CHECK_VIDEO_CHOICE && st->id != CHECK_SAVE_FILE && st->id != CHECK_HD && !a->world.active) {
		snprintf(why, size, "no game");
		return -1;
	}
	switch (st->id) {
	case CHECK_VIDEO_CHOICE: return check_video_choice(S, why, size);
	case CHECK_TERRAIN:
		r = kp_dev_check_terrain(a, st->text[0] ? st->text : "the view", &checked, &bad);
		return compared(r, checked, bad, "the terrain", why, size);
	case CHECK_FOG_PLAIN:
		if (kp_dev_fog_plain(a) == 0) return 0;
		snprintf(why, size, "the view without the fog could not be read");
		return KP_SCRIPT_CHECK_FAILED;
	case CHECK_FOG:
		r = kp_dev_check_fog(a, &checked, &bad);
		return compared(r, checked, bad, "the fog", why, size);
	case CHECK_PALETTE:
		r = kp_dev_check_palette(a, NULL, &checked, &bad);
		return compared(r, checked, bad, "the palette", why, size);
	case CHECK_HD:
		if (!a->flow.ui) {
			snprintf(why, size, "no interface");
			return -1;
		}
		r = kp_dev_check_hd(a, st->text[0] ? st->text : "screen", S->hd_dropped);
		S->hd_dropped = a->flow.ui->image_log.dropped;
		if (r < 0) snprintf(why, size, "no HD interface");
		else if (r > 0) snprintf(why, size, "SD over HD art, or the image log's drops");
		return r != 0 ? KP_SCRIPT_CHECK_FAILED : 0;
	case CHECK_SAVE_FILE: return check_save_file(a, st->text, why, size);
	default:
		snprintf(why, size, "no check %d", (int)st->id);
		return -1;
	}
}

#define OUT_PATH (sizeof ((KpAppFlags *)0)->script_out + KP_SCRIPT_STRING + 8) /* DIR/NAME.ext */

static int shot(Script *S, const char *name)
{
	char path[OUT_PATH];
	snprintf(path, sizeof path, "%s/%s.png", S->a->flags.script_out, name);
	return kp_dev_screenshot(S->a, path);
}

/* An observation's lines written to a file, opened at its first line: an observation refused (no game) leaves none. */
typedef struct {
	const char *path;
	SDL_IOStream *io;
	int lines, failed;
	char error[KP_SCRIPT_WHY];
} Lines;

static void put_line(void *ud, const char *line, size_t length)
{
	Lines *o = (Lines *)ud;
	if (o->failed) return;
	if (!o->io && !(o->io = SDL_IOFromFile(o->path, "wb"))) {
		snprintf(o->error, sizeof o->error, "%s", SDL_GetError());
		o->failed = 1;
		return;
	}
	if (SDL_WriteIO(o->io, line, length) != length || SDL_WriteIO(o->io, "\n", 1) != 1) {
		snprintf(o->error, sizeof o->error, "%s", SDL_GetError());
		o->failed = 1;
		return;
	}
	o->lines++;
}

typedef int (*Observe)(const KpFlow *f, KpDumpLine emit, void *ud, char *why, size_t size);

/* DIR/NAME.ext of what observe gives (the ui, the game's state); its line beside the shots' ("ui PATH: N lines"). */
static int observe(Script *S, const char *name, const char *ext, Observe what, char *why, size_t size)
{
	char path[OUT_PATH];
	Lines o;
	int r;
	snprintf(path, sizeof path, "%s/%s.%s", S->a->flags.script_out, name, ext);
	memset(&o, 0, sizeof o);
	o.path = path;
	r = what(&S->a->flow, put_line, &o, why, size);
	if (o.io && !SDL_CloseIO(o.io) && !o.failed) {
		snprintf(o.error, sizeof o.error, "%s", SDL_GetError());
		o.failed = 1;
	}
	if (r == 0 && (o.failed || !o.io)) { /* the observations give a line at least: their header, the dump's first */
		snprintf(why, size, "cannot write %s: %s", path, o.failed ? o.error : "no lines");
		r = -1;
	}
	if (r == 0) kp_dev_line("%s %s: %d lines", ext, path, o.lines);
	return r;
}

/* An abort's files, logged before the run's last line: the frame it failed in, the interface's tree, the game's
 * state (a file not written logged with the run's name: "script: " is a refusal's, the end of a phone's log). */
static void host_aborted(void *ud, const KpScriptRun *run)
{
	Script *S = (Script *)ud;
	KpApp *a = S->a;
	char why[KP_SCRIPT_WHY];
	(void)shot(S, FAILURE_NAME);
	if (a->flow.ui && observe(S, FAILURE_NAME, "ui", kp_script_flow_ui, why, sizeof why) != 0) {
		kp_dev_line("script %s: %s", run->script->name, why);
	}
	if (a->flow.session && a->flow.session->g && observe(S, FAILURE_NAME, "state", kp_script_flow_state, why, sizeof why) != 0) {
		kp_dev_line("script %s: %s", run->script->name, why);
	}
}

static int host_exec(void *ud, KpScriptRun *run, const KpScriptStep *st, char *why, size_t size)
{
	Script *S = (Script *)ud;
	KpApp *a = S->a;
	int r;
	switch (st->op) {
	case KP_SCRIPT_KEY: return key_step(a, st, why, size);
	case KP_SCRIPT_TEXT:
		a->text_vk = 0; /* no key stands for it (kp_hud_text_key) */
		text_event(a, st->text);
		return 0;
	case KP_SCRIPT_MOVE:
	case KP_SCRIPT_CLICK:
	case KP_SCRIPT_RCLICK:
	case KP_SCRIPT_PRESS:
	case KP_SCRIPT_RELEASE: return mouse_step(a, run, st, why, size);
	case KP_SCRIPT_FINGER:
	case KP_SCRIPT_TAP: return finger_step(a, run, st, why, size);
	case KP_SCRIPT_TOUCH: /* the touch layer's mode, its pad laid out next frame (a cancel is its way in) */
		kp_input_finger(a, KP_TOUCH_CANCEL, 0, 0, 0);
		return 0;
	case KP_SCRIPT_CHOOSE: return choose_step(S, run, st, why, size);
	case KP_SCRIPT_SELECT: return select_step(a, st, why, size);
	case KP_SCRIPT_WORLD: /* the world's images drawn, or the terrain alone */
		if (!a->world.active) break;
		kp_gfx_world(a->gfx, st->flag);
		return 0;
	case KP_SCRIPT_FOG:
		if (!a->world.active) break;
		a->flags.fog = st->flag;
		return 0;
	case KP_SCRIPT_AUDIT:
		S->audit = 1;
		apply_audit(S);
		S->hd_dropped = a->flow.ui ? a->flow.ui->image_log.dropped : 0;
		return 0;
	case KP_SCRIPT_CHECK: return check_step(S, st, why, size);
	case KP_SCRIPT_HANG:
		SDL_Delay((Uint32)st->n);
		return 0;
	case KP_SCRIPT_SHOT:
		if (shot(S, st->text) == 0) return 0;
		snprintf(why, size, "the frame could not be saved");
		return -1;
	case KP_SCRIPT_STATE: return observe(S, st->text, "state", kp_script_flow_state, why, size);
	case KP_SCRIPT_UI: return observe(S, st->text, "ui", kp_script_flow_ui, why, size);
	default:
		r = kp_script_flow_act(&a->flow, run, st, why, size);
		if (r != KP_SCRIPT_FLOW_NONE) return r;
		snprintf(why, size, "the step is not the host's");
		return -1;
	}
	snprintf(why, size, "no game");
	return -1;
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

#ifdef KP_SCRIPT_LIVE
/* One read's bytes split into lines onto cur, each line ended queued: all under one lock, so a frame takes a write's
 * lines together (a write of up to PIPE_BUF bytes is read whole), unless the queue fills and the reader waits for the
 * frames. 0, or -1 the reader stops. */
static int live_push(Script *S, LiveLine *cur, const char *buf, ssize_t n)
{
	ssize_t k = 0;
	SDL_LockMutex(S->live_lock);
	while (k < n) {
		LiveLine *q;
		if (buf[k] != '\n') {
			if (cur->len < sizeof cur->text) cur->text[cur->len++] = buf[k];
			k++;
			continue;
		}
		if (S->queue_count == LIVE_QUEUE) {
			SDL_UnlockMutex(S->live_lock);
			if (SDL_GetAtomicInt(&S->live_stop)) return -1;
			SDL_Delay(LIVE_FULL_MS);
			SDL_LockMutex(S->live_lock);
			continue;
		}
		q = &S->queue[(S->queue_head + S->queue_count) % LIVE_QUEUE];
		memcpy(q->text, cur->text, cur->len);
		q->len = cur->len;
		S->queue_count++;
		cur->len = 0;
		k++;
	}
	SDL_UnlockMutex(S->live_lock);
	return 0;
}

/* The reader's thread: the FIFO's bytes split into lines (a line's bytes past LiveLine's dropped, the parse refusing
 * it as too long) until live_stop. A failed poll or read ends it with live_broken: the frame ends the run. */
static int SDLCALL live_reader(void *ud)
{
	Script *S = (Script *)ud;
	LiveLine cur;
	char buf[1024];
	cur.len = 0;
	while (!SDL_GetAtomicInt(&S->live_stop)) {
		struct pollfd p;
		ssize_t n;
		int r;
		p.fd = S->live_fd;
		p.events = POLLIN;
		p.revents = 0;
		r = poll(&p, 1, LIVE_POLL_MS);
		if (r == 0 || (r < 0 && errno == EINTR)) continue;
		n = r < 0 ? -1 : read(S->live_fd, buf, sizeof buf);
		if (n < 0 && errno != EINTR && errno != EAGAIN) {
			kp_dev_line("script %s: the live lines cannot be read: %s", S->s.name, strerror(errno));
			SDL_SetAtomicInt(&S->live_broken, 1);
			return 0;
		}
		if (n > 0 && live_push(S, &cur, buf, n) != 0) return 0;
	}
	return 0;
}

/* The FIFO (made when there is none) opened and its reader started: 0, or -1 with err. */
static int live_start(Script *S, const char *path, char *err, size_t err_size)
{
	struct stat st;
	if (stat(path, &st) != 0 && (errno != ENOENT || mkfifo(path, 0600) != 0)) {
		snprintf(err, err_size, "cannot make the FIFO %s: %s", path, strerror(errno));
		return -1;
	}
	if (stat(path, &st) != 0 || !S_ISFIFO(st.st_mode)) {
		snprintf(err, err_size, "%s is no FIFO", path);
		return -1;
	}
	/* read and write: a writer's close is no end of file, and the open waits for no writer */
	if ((S->live_fd = open(path, O_RDWR | O_CLOEXEC)) < 0) {
		snprintf(err, err_size, "cannot open %s: %s", path, strerror(errno));
		return -1;
	}
	SDL_SetAtomicInt(&S->live_stop, 0);
	SDL_SetAtomicInt(&S->live_broken, 0);
	if (!(S->queue = (LiveLine *)malloc(LIVE_QUEUE * sizeof *S->queue))) {
		snprintf(err, err_size, "out of memory");
		return -1;
	}
	if (!(S->live_lock = SDL_CreateMutex()) || !(S->reader = SDL_CreateThread(live_reader, "kp_live", S))) {
		snprintf(err, err_size, "no reader for %s: %s", path, SDL_GetError());
		return -1;
	}
	return 0;
}

/* The lines read since the last frame parsed onto the script; a refused one answered in the log. */
static void live_lines(Script *S)
{
	LiveLine line;
	KpError e;
	for (;;) {
		SDL_LockMutex(S->live_lock);
		if (S->queue_count == 0) {
			SDL_UnlockMutex(S->live_lock);
			return;
		}
		line = S->queue[S->queue_head];
		S->queue_head = (S->queue_head + 1) % LIVE_QUEUE;
		S->queue_count--;
		SDL_UnlockMutex(S->live_lock);
		if (kp_script_live_line(&S->s, line.text, line.len, &S->h, &e) < 0) kp_dev_line("script %s: refused", e.msg);
	}
}

/* The reader stopped and joined (at most LIVE_POLL_MS), the FIFO closed. */
static void live_stop(Script *S)
{
	SDL_SetAtomicInt(&S->live_stop, 1);
	if (S->reader) SDL_WaitThread(S->reader, NULL);
	S->reader = NULL;
	if (S->live_lock) SDL_DestroyMutex(S->live_lock);
	S->live_lock = NULL;
	free(S->queue);
	S->queue = NULL;
	if (S->live_fd >= 0) close(S->live_fd);
	S->live_fd = -1;
}
#endif

/* The flag a script cannot run beside (the checks drive the game themselves, a script's shots and camera are its own),
 * NULL none. */
static const char *refused_flag(const KpAppFlags *f)
{
	if (f->autotest) return "--autotest";
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
	if (!f->script_out[0]) {
		snprintf(err, err_size, "%s needs --script-out DIR", f->script[0] ? "--script" : "--script-live");
		return -1;
	}
	if (!kp_flow_scripted(f)) {
		snprintf(err, err_size, "--script-out needs --script FILE or --script-live FIFO");
		return -1;
	}
#ifndef KP_SCRIPT_LIVE
	if (f->script_live[0]) {
		snprintf(err, err_size, "--script-live runs on macOS and Linux only");
		return -1;
	}
#endif
	if (flag) {
		snprintf(err, err_size, "%s cannot run with %s", flag, f->script[0] ? "--script" : "--script-live");
		return -1;
	}
	if (!(S = (Script *)calloc(1, sizeof *S))) {
		snprintf(err, err_size, "out of memory");
		return -1;
	}
	S->a = a;
#ifdef KP_SCRIPT_LIVE
	S->live_fd = -1;
#endif
	/* the includes beside the script, a live run's alone beside the working directory */
	for (slash = f->script + strlen(f->script); slash > f->script && slash[-1] != '/' && slash[-1] != '\\'; slash--) {}
	snprintf(S->dir, sizeof S->dir, "%.*s", (int)(slash - f->script), f->script);
	S->h.ud = S;
	S->h.name = host_name;
	S->h.read = host_read;
	S->h.value = host_value;
	S->h.cond = host_cond;
	S->h.exec = host_exec;
	S->h.log = host_log;
	S->h.aborted = host_aborted;
	if (!f->script[0]) { /* a live run's alone: its header the defaults, its name live */
		text = NULL;
	} else if (!(text = read_file(a, f->script, &size))) {
		snprintf(err, err_size, "cannot read %s", f->script);
		free(S);
		return -1;
	}
	if (kp_script_parse(&S->s, f->script[0] ? f->script : KP_SCRIPT_LIVE_NAME ".kps", text ? text : "", size, &S->h, &e) != 0
		|| (f->script_live[0] && kp_script_live_begin(&S->s, &e) != 0)) {
		snprintf(err, err_size, "%s", e.msg);
		kp_script_free(&S->s);
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
	a->clock_fixed = !S->s.clock_real;
#ifdef KP_SCRIPT_LIVE
	if (S->s.live && live_start(S, a->flags.script_live, err, err_size) != 0) return -1;
#endif
	kp_dev_line("script %s: %d steps, files in %s, profile %s, clock %s%s%s", S->s.name, S->s.count, a->flags.script_out, a->pref,
		S->s.clock_real ? "real" : "fixed", S->s.live ? ", live lines from " : "", S->s.live ? a->flags.script_live : "");
#ifdef KP_SCRIPT_WATCHDOG
	watchdog_start(S);
#endif
	return 0;
}

void kp_app_script_frame(KpApp *a)
{
	Script *S = (Script *)a->script;
	if (!S || S->run.done) return;
#ifdef KP_SCRIPT_WATCHDOG
	SDL_SetAtomicInt(&S->last_ms, (int)SDL_GetTicks() | 1); /* 0 is "no frame yet" */
#endif
	/* the run's own clock: the fixed one stands in a movie, whose frames depend on the wall clock, so a movie's time is
	 * the wall's and a wait's limit still ends a movie that never does */
	{
		double wall = kp_app_wall();
		if (a->clock_fixed && kp_app_cine_active(a) && S->wall_ms > 0) S->movie_ms += wall * 1000 - S->wall_ms;
		S->wall_ms = wall * 1000;
	}
	apply_audit(S);
	/* a game that stopped (its step failed) fails the run: no wait would hold again */
	if (a->flow.session && a->flow.session->failed) {
		char why[KP_SCRIPT_WHY + 32];
		snprintf(why, sizeof why, "the game stopped: %s", a->flow.session->error);
		host_aborted(S, &S->run);
		kp_script_abort(&S->run, &S->h, why);
		a->quit = 1;
		return;
	}
#ifdef KP_SCRIPT_LIVE
	if (S->s.live && SDL_GetAtomicInt(&S->live_broken)) { /* no line will come: the run would wait for ever */
		kp_script_abort(&S->run, &S->h, "the live lines cannot be read");
		a->quit = 1;
		return;
	}
	if (S->s.live) live_lines(S);
#endif
	/* an abort's files are written by host_aborted, in this frame, before the run's last line */
	if (!kp_script_frame(&S->run, &S->h, (int64_t)(kp_app_now(a) * 1000 + S->movie_ms))) {
		a->quit = 1;
		return;
	}
	/* a live run waiting for its next line: the fixed clock stands (the game waits for it too), the frame sleeps */
	a->clock_hold = kp_script_idle(&S->run);
	if (a->clock_hold) SDL_Delay(LIVE_IDLE_MS);
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
#ifdef KP_SCRIPT_LIVE
	live_stop(S);
#endif
	if (!refused) kp_app_script_stop(a, "the program ended before the script did");
	code = S->run.failures > 0 || S->run.aborted ? KP_SCRIPT_EXIT_FAILED : KP_SCRIPT_EXIT_PASSED;
	kp_script_free(&S->s);
	free(S);
	a->script = NULL;
	return code;
}
