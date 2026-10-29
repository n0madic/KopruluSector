/*
 * The window's input for the interface (plan phase S, stage S5): SDL's keys become the original's virtual-key codes for
 * kp_ui's window_key (the accelerators, the scroll keys, the message line), its text the window's characters, its mouse
 * every event at the logical screen's point (no press is lost between frames). The modifiers' two keys share one
 * virtual key: a release goes to the window only when the other side is up too.
 */
#include "kp_app.h"

#include <math.h>
#include <string.h>

enum { SIDE_LSHIFT, SIDE_RSHIFT, SIDE_LCTRL, SIDE_RCTRL, SIDE_LALT, SIDE_RALT };

/* The virtual-key code of a scancode, 0 for a key the window does not get. */
static uint16_t vk_of(SDL_Scancode s)
{
	if (s >= SDL_SCANCODE_A && s <= SDL_SCANCODE_Z) return (uint16_t)('A' + (s - SDL_SCANCODE_A));
	if (s >= SDL_SCANCODE_1 && s <= SDL_SCANCODE_9) return (uint16_t)('1' + (s - SDL_SCANCODE_1));
	if (s == SDL_SCANCODE_0) return '0';
	if (s >= SDL_SCANCODE_F1 && s <= SDL_SCANCODE_F12) return (uint16_t)(0x70 + (s - SDL_SCANCODE_F1));
	if (s >= SDL_SCANCODE_KP_1 && s <= SDL_SCANCODE_KP_9) return (uint16_t)(0x61 + (s - SDL_SCANCODE_KP_1));
	switch (s) {
	case SDL_SCANCODE_KP_0: return 0x60;
	case SDL_SCANCODE_TAB: return 0x09;
	case SDL_SCANCODE_BACKSPACE: return 0x08;
	case SDL_SCANCODE_INSERT: return 0x2d;
	case SDL_SCANCODE_DELETE: return 0x2e;
	case SDL_SCANCODE_LSHIFT: case SDL_SCANCODE_RSHIFT: return 0x10;
	case SDL_SCANCODE_LCTRL: case SDL_SCANCODE_RCTRL: return 0x11;
	case SDL_SCANCODE_LALT: case SDL_SCANCODE_RALT: return 0x12;
	case SDL_SCANCODE_PAGEUP: return 0x21;
	case SDL_SCANCODE_PAGEDOWN: return 0x22;
	case SDL_SCANCODE_END: return 0x23;
	case SDL_SCANCODE_HOME: return 0x24;
	case SDL_SCANCODE_ESCAPE: case SDL_SCANCODE_AC_BACK: return 0x1b; /* Android's back: the glue's back (a game's: key()) */
	case SDL_SCANCODE_LEFT: return 0x25;
	case SDL_SCANCODE_UP: return 0x26;
	case SDL_SCANCODE_RIGHT: return 0x27;
	case SDL_SCANCODE_DOWN: return 0x28;
	case SDL_SCANCODE_SPACE: return 0x20;
	case SDL_SCANCODE_RETURN: case SDL_SCANCODE_KP_ENTER: return 0x0d;
	default: return 0;
	}
}

static int side_of(SDL_Scancode s)
{
	switch (s) {
	case SDL_SCANCODE_LSHIFT: return SIDE_LSHIFT;
	case SDL_SCANCODE_RSHIFT: return SIDE_RSHIFT;
	case SDL_SCANCODE_LCTRL: return SIDE_LCTRL;
	case SDL_SCANCODE_RCTRL: return SIDE_RCTRL;
	case SDL_SCANCODE_LALT: return SIDE_LALT;
	case SDL_SCANCODE_RALT: return SIDE_RALT;
	default: return -1;
	}
}

static int32_t clamp(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }

/* The logical point of a window point (the window's rows count from the bottom in the drawing), on the logical screen:
 * a drag the window captured past its edge stays at the edge, as the original's cursor clipped to the screen. */
static void logical(KpApp *a, float wx, float wy, int32_t *lx, int32_t *ly)
{
	double px = (double)wx * a->density - a->safe_x;
	double py_up = (double)a->full_h - (double)wy * a->density - a->safe_y;
	*lx = clamp((int32_t)floor(px / a->scale), 0, a->world_w - 1);
	*ly = clamp(a->world_h - 1 - (int32_t)floor(py_up / a->scale), 0, a->world_h - 1);
}

/* An input of the game's console (key, text or mouse), run under the game's failure handler: the commands it made
 * queued, the camera it moved taken (a text's too: the portrait's hotkey, the space, is a character; the next frame
 * gives the interface the host's camera again). */
typedef struct {
	KpApp *a;
	enum { CONSOLE_KEY, CONSOLE_TEXT, CONSOLE_MOUSE, CONSOLE_TARGET, CONSOLE_PAD } kind;
	int down, repeat, event;
	uint16_t vk;
	const char *text;
	int32_t lx, ly;
	int target; /* CONSOLE_TARGET's answer */
} ConsoleInput;

static void console_input(void *ud)
{
	ConsoleInput *in = (ConsoleInput *)ud;
	KpApp *a = in->a;
	int64_t x, y;
	switch (in->kind) {
	case CONSOLE_KEY: kp_hud_key(a->ui, in->down, in->vk, in->repeat); break;
	case CONSOLE_TEXT: kp_hud_text(a->ui, in->text); break;
	case CONSOLE_TARGET: in->target = kp_hud_touch_target(&a->hud, in->lx, in->ly, a->cam_x, a->cam_y); return;
	case CONSOLE_PAD: kp_hud_pad_button(&a->hud, in->event, in->lx, in->ly); break;
	default: kp_hud_input(&a->hud, in->event, in->lx, in->ly, a->cam_x, a->cam_y); break;
	}
	kp_hud_take_commands(a->ui, a->session);
	if (kp_hud_take_camera(a->ui, &x, &y)) {
		a->cam_x = x;
		a->cam_y = y;
		kp_flow_clamp_camera(a);
	}
}

/* Whether the console of a game in play takes the input. */
static int console_on(const KpApp *a) { return a->playing && a->hud_on && a->session && a->session->g; }

static void console(KpApp *a, ConsoleInput *in)
{
	in->a = a;
	/* a failure in the console's input stops the game as a step's does (the world is left partly changed) */
	if (kp_session_guard(a->session, console_input, in) != 0) {
		SDL_Log("play: stopped in the console's input: %s", a->session->error);
	}
}

static void key(KpApp *a, const SDL_KeyboardEvent *k)
{
	uint16_t vk = vk_of(k->scancode);
	int side = side_of(k->scancode);
	int down = k->type == SDL_EVENT_KEY_DOWN;
	if (side >= 0) {
		int other = side ^ 1;
		a->held_sides[side] = down;
		if (!down && a->held_sides[other]) return; /* the other side still holds the virtual key */
	}
	if (!vk) return;
	if (k->scancode == SDL_SCANCODE_AC_BACK) { /* in a game the menu, or Escape where it closes or cancels */
		if (down && !k->repeat) a->back_vk = console_on(a) && !kp_flow_glue_active(a) ? kp_hud_back_key(a->ui) : vk;
		if (a->back_vk) vk = a->back_vk;
	}
	if (kp_flow_glue_active(a)) {
		kp_hud_key(a->ui, down, vk, k->repeat);
	} else if (console_on(a)) {
		ConsoleInput in;
		memset(&in, 0, sizeof in);
		in.kind = CONSOLE_KEY;
		in.down = down;
		in.vk = vk;
		in.repeat = k->repeat;
		console(a, &in);
	}
}

static void text(KpApp *a, const char *utf8)
{
	if (kp_flow_glue_active(a)) {
		kp_hud_text(a->ui, utf8);
	} else if (console_on(a)) {
		ConsoleInput in;
		memset(&in, 0, sizeof in);
		in.kind = CONSOLE_TEXT;
		in.text = utf8;
		console(a, &in);
	}
}

static void mouse(KpApp *a, int event, float wx, float wy)
{
	int32_t lx, ly;
	logical(a, wx, wy, &lx, &ly);
	kp_input_mouse(a, event, lx, ly);
}

/* An interface mouse event at the logical point, the glue's or the console's. */
static void point(KpApp *a, int event, int32_t lx, int32_t ly)
{
	if (kp_flow_glue_active(a)) {
		kp_glue_input(&a->glue, event, lx, ly);
	} else if (console_on(a)) {
		ConsoleInput in;
		memset(&in, 0, sizeof in);
		in.kind = CONSOLE_MOUSE;
		in.event = event;
		in.lx = lx;
		in.ly = ly;
		console(a, &in);
	}
}

void kp_input_mouse(KpApp *a, int event, int32_t lx, int32_t ly)
{
	a->mouse_lx = lx;
	a->mouse_ly = ly;
	a->has_mouse = 1;
	point(a, event, lx, ly);
}

/* --- the touch layer's host (kp_touch.h) --- */

static int touch_target(void *ud, int32_t x, int32_t y)
{
	KpApp *a = (KpApp *)ud;
	ConsoleInput in;
	if (kp_flow_glue_active(a) || !console_on(a)) return KP_TOUCH_PANEL;
	memset(&in, 0, sizeof in);
	in.kind = CONSOLE_TARGET;
	in.lx = x;
	in.ly = y;
	console(a, &in);
	return in.target;
}

static int touch_placing(void *ud)
{
	KpApp *a = (KpApp *)ud;
	return !kp_flow_glue_active(a) && console_on(a) && kp_hud_placing(a->ui);
}

static void touch_mouse(void *ud, int event, int32_t x, int32_t y) { point((KpApp *)ud, event, x, y); }

static void touch_cancel(void *ud)
{
	KpApp *a = (KpApp *)ud;
	if (a->hud_on) kp_hud_capture_lost(&a->hud);
	if (a->glue_on) kp_glue_capture_lost(&a->glue);
}

static void touch_aim(void *ud, int shown, int32_t x, int32_t y)
{
	KpApp *a = (KpApp *)ud;
	a->touch_aim = shown;
	a->mouse_lx = x;
	a->mouse_ly = y;
}

static void touch_pan(void *ud, int32_t dx, int32_t dy)
{
	KpApp *a = (KpApp *)ud;
	if (kp_flow_glue_active(a) || !console_on(a)) return;
	a->cam_x -= dx;
	a->cam_y -= dy;
	kp_flow_clamp_camera(a);
}

static void touch_button(void *ud, int moment, int32_t x, int32_t y)
{
	KpApp *a = (KpApp *)ud;
	ConsoleInput in;
	if (kp_flow_glue_active(a) || !console_on(a)) return;
	memset(&in, 0, sizeof in);
	in.kind = CONSOLE_PAD;
	in.event = moment;
	in.lx = x;
	in.ly = y;
	console(a, &in);
}

void kp_input_init(KpApp *a)
{
	KpTouchHost host;
	memset(&host, 0, sizeof host);
	host.ud = a;
	host.target = touch_target;
	host.placing = touch_placing;
	host.mouse = touch_mouse;
	host.cancel = touch_cancel;
	host.aim = touch_aim;
	host.pan = touch_pan;
	host.button = touch_button;
	kp_touch_init(&a->touch, &host);
}

/* The touch mode (a finger) or the mouse's (a real mouse); a finger's mode hides the cursor, whose id may still be an
 * edge scroll's arrow (with which the game view takes no click). */
static void input_mode(KpApp *a, int touch)
{
	if (a->touch_mode == touch) return;
	a->touch_mode = touch;
	a->touch_aim = 0;
	if (touch && a->ui && console_on(a)) kp_ui_cursor_set(a->ui, 0);
	if (!touch) kp_touch_cancel(&a->touch);
}

static uint32_t clock_ms(void) { return (uint32_t)(kp_app_now() * 1000); }

static void screen_key(const KpApp *a, int32_t key[3])
{
	key[0] = kp_flow_glue_active(a);
	key[1] = a->world_w;
	key[2] = a->world_h;
}

void kp_input_finger(KpApp *a, int type, int64_t id, double lx, double ly)
{
	if (type == KP_TOUCH_DOWN && !kp_touch_active(&a->touch)) screen_key(a, a->touch_screen);
	input_mode(a, 1);
	kp_touch_event(&a->touch, type, id, lx, ly, clock_ms());
}

void kp_input_frame(KpApp *a)
{
	int32_t key[3];
	/* the display's pixels per dp (Android's density, iOS's points) in the logical screen's pixels */
	float display = a->window ? SDL_GetWindowDisplayScale(a->window) : 0.0f;
	double px_per_dp;
	if (display <= 0) display = 1;
	px_per_dp = a->scale > 0 ? display / a->scale : display;
	kp_touch_set_density(&a->touch, px_per_dp);
	if (a->hud_on) kp_hud_pad(&a->hud, a->touch_mode, px_per_dp); /* the touch pad in the touch mode's games */
	screen_key(a, key);
	if (kp_touch_active(&a->touch) && memcmp(key, a->touch_screen, sizeof key) != 0) kp_touch_cancel(&a->touch);
	kp_touch_frame(&a->touch, clock_ms());
}

int kp_input_cursor(const KpApp *a)
{
	if (a->touch_mode) return a->touch_aim ? KP_HUD_POINTER_AIM : KP_HUD_POINTER_NONE;
	return a->has_mouse ? KP_HUD_POINTER_MOUSE : KP_HUD_POINTER_NONE;
}

/* A finger's point (SDL's fraction of the window) on the logical screen, as logical() maps the mouse's but kept below
 * a pixel (the gestures' moves). */
static void finger(KpApp *a, int type, const SDL_TouchFingerEvent *f)
{
	/* a screen's touches only: a trackpad's (macOS's indirect touches) drive the mouse */
	if (SDL_GetTouchDeviceType(f->touchID) != SDL_TOUCH_DEVICE_DIRECT) return;
	double px = (double)f->x * a->full_w - a->safe_x, py_up = (double)a->full_h * (1 - (double)f->y) - a->safe_y;
	double lx = px / a->scale, ly = a->world_h - py_up / a->scale;
	if (lx < 0) lx = 0;
	if (ly < 0) ly = 0;
	if (lx > a->world_w - 1) lx = a->world_w - 1;
	if (ly > a->world_h - 1) ly = a->world_h - 1;
	if (type == KP_TOUCH_DOWN) kp_input_text_sync(a, 1);
	kp_input_finger(a, type, (int64_t)f->fingerID, lx, ly);
}

void kp_input_focus(KpApp *a, int active)
{
	/* WM_ACTIVATEAPP: no scroll, the key bytes cleared; the modifiers' sides released outside the window send no
	 * release here */
	memset(a->held_sides, 0, sizeof a->held_sides);
	kp_touch_cancel(&a->touch);
	if (!a->ui) return;
	kp_ui_set_active(a->ui, active);
	if (a->hud_on) kp_hud_keys_cleared(&a->hud); /* the pad's Shift with the Shift byte */
	if (!active) { /* a button let go elsewhere never comes back */
		if (a->hud_on) kp_hud_capture_lost(&a->hud);
		if (a->glue_on) kp_glue_capture_lost(&a->glue);
	}
}

void kp_input_text_sync(KpApp *a, int tapped)
{
#if defined(SDL_PLATFORM_ANDROID) || defined(SDL_PLATFORM_IOS)
	int want = a->ui != NULL && kp_ui_edit_focused(a->ui);
	int active = SDL_TextInputActive(a->window);
	if (want && (!active || (tapped && !SDL_ScreenKeyboardShown(a->window)))) {
		/* a name or a message: no capitals and no corrections of the soft keyboard's own */
		SDL_PropertiesID props = SDL_CreateProperties();
		SDL_SetNumberProperty(props, SDL_PROP_TEXTINPUT_TYPE_NUMBER, SDL_TEXTINPUT_TYPE_TEXT);
		SDL_SetNumberProperty(props, SDL_PROP_TEXTINPUT_CAPITALIZATION_NUMBER, SDL_CAPITALIZE_NONE);
		SDL_SetBooleanProperty(props, SDL_PROP_TEXTINPUT_AUTOCORRECT_BOOLEAN, false);
		SDL_SetBooleanProperty(props, SDL_PROP_TEXTINPUT_MULTILINE_BOOLEAN, false);
		if (!SDL_StartTextInputWithProperties(a->window, props)) SDL_Log("SDL_StartTextInput: %s", SDL_GetError());
		SDL_DestroyProperties(props);
	} else if (!want && active) {
		SDL_StopTextInput(a->window);
	}
#else
	(void)a;
	(void)tapped;
#endif
}

void kp_input_event(KpApp *a, const SDL_Event *e)
{
	switch (e->type) {
	case SDL_EVENT_KEY_DOWN:
	case SDL_EVENT_KEY_UP:
		key(a, &e->key);
		break;
	case SDL_EVENT_TEXT_INPUT:
		text(a, e->text.text);
		break;
	case SDL_EVENT_FINGER_DOWN:
		finger(a, KP_TOUCH_DOWN, &e->tfinger);
		break;
	case SDL_EVENT_FINGER_MOTION:
		finger(a, KP_TOUCH_MOVE, &e->tfinger);
		break;
	case SDL_EVENT_FINGER_UP:
		finger(a, KP_TOUCH_UP, &e->tfinger);
		break;
	case SDL_EVENT_FINGER_CANCELED:
		finger(a, KP_TOUCH_CANCEL, &e->tfinger);
		break;
	case SDL_EVENT_MOUSE_MOTION:
		if (e->motion.which == SDL_TOUCH_MOUSEID) break; /* a finger's synthetic mouse (the touch layer has the finger) */
		input_mode(a, 0);
		mouse(a, KP_UI_MOUSE_MOVE, e->motion.x, e->motion.y);
		break;
	case SDL_EVENT_MOUSE_BUTTON_DOWN:
	case SDL_EVENT_MOUSE_BUTTON_UP: {
		int down = e->type == SDL_EVENT_MOUSE_BUTTON_DOWN;
		if (e->button.which == SDL_TOUCH_MOUSEID) break;
		input_mode(a, 0);
		if (down) kp_input_text_sync(a, 1);
		/* CS_DBLCLKS: the second press of a quick pair arrives as the double click, the third as a press again */
		int twice = down && e->button.clicks % 2 == 0;
		if (e->button.button == SDL_BUTTON_LEFT) {
			mouse(a, !down ? KP_UI_LEFT_UP : twice ? KP_UI_LEFT_DOUBLE : KP_UI_LEFT_DOWN, e->button.x, e->button.y);
		} else if (e->button.button == SDL_BUTTON_RIGHT) {
			mouse(a, !down ? KP_UI_RIGHT_UP : twice ? KP_UI_RIGHT_DOUBLE : KP_UI_RIGHT_DOWN, e->button.x, e->button.y);
		}
		break;
	}
	default:
		break;
	}
}
