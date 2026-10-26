/*
 * The window's input for the interface (plan phase S, stage S5): SDL's keys become the original's virtual-key codes for
 * kp_ui's window_key (the accelerators, the scroll keys, the message line), its text the window's characters, its mouse
 * every event at the logical screen's point (no press is lost between frames). The modifiers' two keys share one
 * virtual key: a release goes to the window only when the other side is up too.
 */
#include "kp_app.h"

#include <math.h>
#include <setjmp.h>
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
	case SDL_SCANCODE_ESCAPE: return 0x1b;
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
	double px = (double)wx * a->density, py_up = (double)a->win_h - (double)wy * a->density;
	*lx = clamp((int32_t)floor(px / a->scale), 0, a->world_w - 1);
	*ly = clamp(a->world_h - 1 - (int32_t)floor(py_up / a->scale), 0, a->world_h - 1);
}

static void key(KpApp *a, const SDL_KeyboardEvent *k)
{
	uint16_t vk = vk_of(k->scancode);
	int side = side_of(k->scancode);
	volatile int down = k->type == SDL_EVENT_KEY_DOWN; /* read after the setjmp below */
	if (side >= 0) {
		int other = side ^ 1;
		a->held_sides[side] = down;
		if (!down && a->held_sides[other]) return; /* the other side still holds the virtual key */
	}
	if (!vk) return;
	if (kp_flow_glue_active(a)) {
		kp_hud_key(a->ui, down, vk, k->repeat);
	} else if (a->playing && a->hud_on && a->session && a->session->g) {
		KpTry t;
		KpGame *g = a->session->g;
		int64_t x, y;
		kp_images_begin(g->w, &t);
		if (setjmp(t.buf)) {
			kp_images_end(g->w, &t);
			return;
		}
		kp_hud_key(a->ui, down, vk, k->repeat);
		kp_hud_take_commands(a->ui, a->session);
		if (kp_hud_take_camera(a->ui, &x, &y)) {
			a->cam_x = x;
			a->cam_y = y;
			kp_flow_clamp_camera(a);
		}
		kp_images_end(g->w, &t);
	}
}

static void text(KpApp *a, const char *utf8)
{
	if (kp_flow_glue_active(a)) {
		kp_hud_text(a->ui, utf8);
	} else if (a->playing && a->hud_on && a->session && a->session->g) {
		KpTry t;
		KpGame *g = a->session->g;
		kp_images_begin(g->w, &t);
		if (setjmp(t.buf)) {
			kp_images_end(g->w, &t);
			return;
		}
		kp_hud_text(a->ui, utf8);
		kp_hud_take_commands(a->ui, a->session);
		kp_images_end(g->w, &t);
	}
}

static void mouse(KpApp *a, int event, float wx, float wy)
{
	int32_t lx, ly;
	logical(a, wx, wy, &lx, &ly);
	kp_input_mouse(a, event, lx, ly);
}

void kp_input_mouse(KpApp *a, int event, int32_t lx, int32_t ly)
{
	a->mouse_lx = lx;
	a->mouse_ly = ly;
	a->has_mouse = 1;
	if (kp_flow_glue_active(a)) {
		kp_glue_input(&a->glue, event, lx, ly);
	} else if (a->playing && a->hud_on && a->session && a->session->g) {
		KpTry t;
		KpGame *g = a->session->g;
		int64_t x, y;
		kp_images_begin(g->w, &t);
		if (setjmp(t.buf)) {
			kp_images_end(g->w, &t);
			return;
		}
		kp_hud_input(&a->hud, event, lx, ly, a->cam_x, a->cam_y);
		kp_hud_take_commands(a->ui, a->session);
		if (kp_hud_take_camera(a->ui, &x, &y)) {
			a->cam_x = x;
			a->cam_y = y;
			kp_flow_clamp_camera(a);
		}
		kp_images_end(g->w, &t);
	}
}

void kp_input_focus(KpApp *a, int active)
{
	/* WM_ACTIVATEAPP: no scroll, the key bytes cleared; the modifiers' sides released outside the window send no
	 * release here */
	memset(a->held_sides, 0, sizeof a->held_sides);
	if (!a->ui) return;
	kp_ui_set_active(a->ui, active);
	if (!active) { /* a button let go elsewhere never comes back */
		if (a->hud_on) kp_hud_capture_lost(&a->hud);
		if (a->glue_on) kp_glue_capture_lost(&a->glue);
	}
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
	case SDL_EVENT_MOUSE_MOTION:
		mouse(a, KP_HUD_MOUSE_MOVE, e->motion.x, e->motion.y);
		break;
	case SDL_EVENT_MOUSE_BUTTON_DOWN:
	case SDL_EVENT_MOUSE_BUTTON_UP: {
		int down = e->type == SDL_EVENT_MOUSE_BUTTON_DOWN;
		if (e->button.button == SDL_BUTTON_LEFT) {
			mouse(a, down ? KP_HUD_LEFT_DOWN : KP_HUD_LEFT_UP, e->button.x, e->button.y);
		} else if (e->button.button == SDL_BUTTON_RIGHT) {
			mouse(a, down ? KP_HUD_RIGHT_DOWN : KP_HUD_RIGHT_UP, e->button.x, e->button.y);
		}
		break;
	}
	default:
		break;
	}
}
