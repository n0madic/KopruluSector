/* The map's scrolling (plan phase 9C): scroll.cpp's UpdateMapScroll 0x49bf10 with GetScrollKeyDirection 0x49bcc0,
 * the Center View trigger's camera pan (StartCameraPan 0x49be10, EndCameraPan 0x49be50, LimitPanStep 0x49be70),
 * GetEdgeScrollCursorId 0x43e7b0, SetScreenPosition 0x40b450 and the incremental ScrollScreenDown/Left/Right/Up
 * 0x40b5e0..0x40b870 (atlas P34, P13, P03). The terrain's ring buffer and the parallax stars those routines also
 * move are the renderer's. */
#include "kp_ui.h"

#include <string.h>

#include "kp_game.h"

#define VIEW_TILES_H 12        /* InitScreenScroll (P03): 0x543220 = (W - 20) * 32, 0x543224 = (H - 12) * 32 + 8 */
#define VIEW_BOTTOM_EXTRA 8
#define TILE 32
#define EDGE 1                 /* GetEdgeScrollCursorId: x <= 1, x >= 638, y <= 1, y >= 478 */
#define SCREEN_LAST_X 639
#define SCREEN_LAST_Y 479
#define MAX_STEP 6
#define PERCENT 100
#define HALF 50
#define PAN_SPEED 6            /* LimitPanStep's speed row */
#define VK_PRIOR 0x21
#define VK_NEXT 0x22
#define VK_END 0x23
#define VK_HOME 0x24
#define VK_LEFT 0x25
#define VK_UP 0x26
#define VK_RIGHT 0x27
#define VK_DOWN 0x28
#define VK_NUMPAD1 0x61
#define VK_NUMPAD2 0x62
#define VK_NUMPAD3 0x63
#define VK_NUMPAD4 0x64
#define VK_NUMPAD6 0x66
#define VK_NUMPAD7 0x67
#define VK_NUMPAD8 0x68
#define VK_NUMPAD9 0x69

/* 0x509390[7][7]: pixels per call by speed (row) and step (column); row 6 (0x5093ba) is also LimitPanStep's. */
static const uint8_t SPEEDS[7][7] = {
	{ 0, 0, 8, 8, 16, 16, 16 },
	{ 0, 0, 8, 8, 16, 16, 24 },
	{ 0, 8, 8, 16, 16, 24, 24 },
	{ 0, 8, 8, 16, 24, 24, 32 },
	{ 0, 8, 16, 16, 24, 32, 40 },
	{ 0, 8, 16, 24, 32, 40, 48 },
	{ 0, 8, 24, 32, 40, 56, 64 },
};
/* 0x50320c: the edge cursors by dx + 3 * dy + 4 */
static const uint16_t EDGE_CURSORS[9] = { 18, 11, 12, 17, 0, 13, 16, 15, 14 };

static KpUiInput *in(KpUi *ui) { return &ui->input; }
static KpUiScroll *sc(KpUi *ui) { return &ui->input.scroll; }

/* A limit on the original's 8-pixel grid, which its limits are always on: the screen's positions and the pan's target
 * are (& ~7) and the pan's steps are 8 pixels or more, so a pan from a limit off the grid never arrived. */
static int32_t on_grid(int32_t v) { return v > 0 ? v & ~7 : v; }

/* InitScreenScroll's limits: (W - 20) * 32 and (H - 12) * 32 + 8 for the original's 640 x 400 view, which a wider
 * layout's view (kp_ui_set_view) replaces, down to the 8-pixel grid for a view's size of no multiple of 8. */
static int32_t max_x(KpUi *ui) { return ui->game ? on_grid((int32_t)ui->game->map_w * TILE - ui->view_w) : 0; }
static int32_t max_y(KpUi *ui)
{
	return ui->game ? on_grid((int32_t)ui->game->map_h * TILE - VIEW_TILES_H * TILE - (ui->view_h - KP_UI_VIEW_H)
		+ VIEW_BOTTOM_EXTRA) : 0;
}

void kp_ui_set_screen_position(KpUi *ui, int32_t x, int32_t y)
{
	KpUiInput *p = in(ui);
	if (sc(ui)->pan) return;
	if (x < 0) x = 0;
	else if ((uint32_t)x >= (uint32_t)max_x(ui)) x = max_x(ui);
	if (y < 0) y = 0;
	else if ((uint32_t)y >= (uint32_t)max_y(ui)) y = max_y(ui);
	x &= ~7;
	y &= ~7;
	if (x != p->screen_x || y != p->screen_y) p->camera_moved = 1;
	p->screen_x = x;
	p->screen_y = y;
}

void kp_ui_start_screen(KpUi *ui)
{
	/* HandleStartLocationRecord 0x46a280's camera: the local player's start location's tile less (10, 6), at least 0
	 * (half the original's 20 x 12 tile view: half the layout's view here); then InitScreenPosition 0x445810 */
	KpGame *g = ui->game;
	int32_t tx, ty;
	if (g == NULL) return;
	tx = (int32_t)((uint16_t)g->players.start_x[g->local_player] >> 5) - ui->view_w / TILE / 2;
	ty = (int32_t)((uint16_t)g->players.start_y[g->local_player] >> 5) - ui->view_h / TILE / 2;
	kp_ui_set_screen_position(ui, (tx > 0 ? tx : 0) * TILE, (ty > 0 ? ty : 0) * TILE);
	in(ui)->camera_moved = 1;
}

/* ScrollScreenDown 0x40b5e0 / ScrollScreenUp 0x40b870 / ScrollScreenRight 0x40b790 / ScrollScreenLeft 0x40b6c0:
 * by n pixels, clamped to the map; 1 when the view moved. */
static int scroll_by(KpUi *ui, int32_t *at, int32_t max, int sign, uint32_t n)
{
	int32_t v;
	if (sign > 0) {
		v = *at + (int32_t)n;
		if ((uint32_t)v > (uint32_t)max) v = max;
	} else {
		v = *at - (int32_t)n;
		if (v < 0) v = 0;
	}
	if (v == *at) return 0;
	*at = v;
	in(ui)->camera_moved = 1;
	return 1;
}

uint16_t kp_ui_edge_scroll(KpUi *ui, int32_t cursor_x, int32_t cursor_y, int *dx, int *dy)
{
	KpUiScroll *s = sc(ui);
	KpUiInput *p = in(ui);
	int x = 0, y = 0;
	if (s->suppressed) {
		if (cursor_x == s->cursor_x && cursor_y == s->cursor_y) return 0;
		s->suppressed = 0;
	}
	s->cursor_x = cursor_x;
	s->cursor_y = cursor_y;
	if (cursor_x <= EDGE && p->screen_x != 0) x = -1;
	else if (cursor_x >= SCREEN_LAST_X - EDGE && (uint32_t)p->screen_x < (uint32_t)max_x(ui)) x = 1;
	if (cursor_y <= EDGE && p->screen_y != 0) y = -1;
	else if (cursor_y >= SCREEN_LAST_Y - EDGE && (uint32_t)p->screen_y < (uint32_t)max_y(ui)) y = 1;
	if (dx) *dx = x;
	if (dy) *dy = y;
	return EDGE_CURSORS[x + 3 * y + 4];
}

static int held(KpUi *ui, int a, int b) { return in(ui)->vk[a] || in(ui)->vk[b]; }

/* GetScrollKeyDirection 0x49bcc0. */
static void key_direction(KpUi *ui, int *dx, int *dy)
{
	KpUiScroll *s = sc(ui);
	KpUiInput *p = in(ui);
	*dx = *dy = 0;
	if (s->pan) { /* toward the pan's target (unsigned compares) */
		if ((uint32_t)s->pan_x < (uint32_t)p->screen_x) *dx = -1;
		else if ((uint32_t)p->screen_x < (uint32_t)s->pan_x) *dx = 1;
		if ((uint32_t)s->pan_y < (uint32_t)p->screen_y) *dy = -1;
		else if ((uint32_t)p->screen_y < (uint32_t)s->pan_y) *dy = 1;
		return;
	}
	if (held(ui, VK_NUMPAD3, VK_NEXT)) { *dx = 1; *dy = 1; return; }
	if (held(ui, VK_NUMPAD1, VK_END)) { *dx = -1; *dy = 1; return; }
	if (held(ui, VK_NUMPAD7, VK_HOME)) { *dx = -1; *dy = -1; return; }
	if (held(ui, VK_NUMPAD9, VK_PRIOR)) { *dx = 1; *dy = -1; return; }
	if (held(ui, VK_DOWN, VK_NUMPAD2)) *dy = 1;
	else if (held(ui, VK_UP, VK_NUMPAD8)) *dy = -1;
	if (held(ui, VK_RIGHT, VK_NUMPAD6)) *dx = 1;
	else if (held(ui, VK_LEFT, VK_NUMPAD4)) *dx = -1;
}

void kp_ui_view_target(const KpUi *ui, int32_t *x, int32_t *y)
{
	*x -= (ui->view_w - KP_UI_VIEW_W) / 2;
	*y -= (ui->view_h - KP_UI_VIEW_H) / 2;
}

/* The pan's target for the layout's view: the trigger's, made for the original's 640 x 400 view (kp_ui_view_target),
 * within the view's scroll limits (a wider view's are smaller: a target past them was never reached, and the camera
 * and the player's triggers waited for it forever). The original's view keeps the trigger's target. */
static void pan_target(KpUi *ui)
{
	KpUiScroll *s = sc(ui);
	int32_t x = s->target_x, y = s->target_y, mx = max_x(ui) > 0 ? max_x(ui) : 0, my = max_y(ui) > 0 ? max_y(ui) : 0;
	kp_ui_view_target(ui, &x, &y);
	s->pan_x = (x < 0 ? 0 : x > mx ? mx : x) & ~7;
	s->pan_y = (y < 0 ? 0 : y > my ? my : y) & ~7;
}

/* LimitPanStep 0x49be70: the step whose speed does not pass the target. */
static uint32_t limit_pan_step(KpUi *ui)
{
	KpUiScroll *s = sc(ui);
	KpUiInput *p = in(ui);
	uint32_t ax = (uint32_t)s->pan_x - (uint32_t)p->screen_x, ay = (uint32_t)s->pan_y - (uint32_t)p->screen_y;
	if ((int32_t)ax < 0) ax = (uint32_t)-(int32_t)ax;
	if ((int32_t)ay < 0) ay = (uint32_t)-(int32_t)ay;
	if (ax != 0) {
		while (SPEEDS[PAN_SPEED][s->step] > ax) s->step--;
	}
	if (ay != 0) {
		while (SPEEDS[PAN_SPEED][s->step] > ay) s->step--;
	}
	return PAN_SPEED;
}

/* The original's view is always on its 8-pixel grid, the pan's steps and target too, so LimitPanStep never limits the
 * step to 0 short of the target there; a host's view off the grid (a two-finger pan, a rotation's kept centre) is less
 * than a step from it on an axis, and the pan would hold the view and its trigger's wait for good: that axis moves the
 * rest of the way (toward the target, unlike a snap down onto the grid). 1 when the view moved. */
static int pan_remainder(KpUi *ui)
{
	KpUiScroll *s = sc(ui);
	KpUiInput *p = in(ui);
	int32_t d[2];
	int32_t *at[2];
	int32_t max[2];
	int k, moved = 0;
	d[0] = s->pan_x - p->screen_x;
	d[1] = s->pan_y - p->screen_y;
	at[0] = &p->screen_x;
	at[1] = &p->screen_y;
	max[0] = max_x(ui);
	max[1] = max_y(ui);
	for (k = 0; k < 2; k++) {
		uint32_t n = (uint32_t)(d[k] < 0 ? -d[k] : d[k]);
		if (n != 0 && n < SPEEDS[PAN_SPEED][1]) moved |= scroll_by(ui, at[k], max[k], d[k] < 0 ? -1 : 1, n);
	}
	return moved;
}

int kp_ui_update_scroll(KpUi *ui, int32_t cursor_x, int32_t cursor_y)
{
	KpUiScroll *s = sc(ui);
	KpUiInput *p = in(ui);
	int dx, dy, moving, moved = 0;
	uint32_t speed;
	if (ui->inactive) return 0; /* the application's activity 0x66fd24 */
	if (ui->game != NULL && kp_scalar(ui->game, KP_PAUSED) != 0) return 0; /* IsSinglePlayerPaused 0x47cb70 */
	if (s->pan) {
		pan_target(ui); /* for the view as it is now (the window may have been resized) */
		p->box = 0; /* CancelDragBox 0x4bccc0 (its dirty cells are the view's) */
	} else if (p->box || ui->cursor_locked) {
		return 0;
	}
	key_direction(ui, &dx, &dy);
	if (dx != 0 || dy != 0) {
		speed = ui->key_scroll_speed; /* TODO: the multiplayer speeds 0x6d3150, 0x6d314c */
		s->suppressed = 1;
		moving = 1;
	} else {
		kp_ui_edge_scroll(ui, cursor_x, cursor_y, &dx, &dy);
		speed = ui->mouse_scroll_speed;
		moving = dx != 0 || dy != 0;
		if (!moving) s->step = 0;
	}
	if (moving && s->step < MAX_STEP) s->step++;
	if (s->pan) speed = limit_pan_step(ui);
	if (moving) {
		s->dx = dx;
		s->dy = dy;
	}
	if (speed > MAX_STEP) speed = MAX_STEP;
	if (s->step != 0) {
		uint32_t n = SPEEDS[speed][s->step];
		if (s->dy != 0) moved |= scroll_by(ui, &p->screen_y, max_y(ui), s->dy, n);
		if (s->dx != 0) moved |= scroll_by(ui, &p->screen_x, max_x(ui), s->dx, n);
	} else if (s->pan) {
		moved |= pan_remainder(ui);
	}
	/* InvalidateGameLayer 0x445880 when it moved only marks the view's grid dirty (see kp_ui_core.c): no state */
	if (s->pan && p->screen_x == s->pan_x && p->screen_y == s->pan_y) {
		if (s->pan_release) s->release = 1;
		s->pan_release = 0;
	}
	return moved;
}

void kp_ui_start_pan(KpUi *ui, int32_t x, int32_t y)
{
	KpUiScroll *s = sc(ui);
	if (s->pan_release) s->release = 1; /* the previous pan's callback runs, without being cleared first */
	s->target_x = x;
	s->target_y = y;
	pan_target(ui);
	s->pan = 1;
	s->pan_release = 1;
}

void kp_ui_end_pan(KpUi *ui)
{
	KpUiScroll *s = sc(ui);
	if (!s->pan) return;
	s->pan = 0;
	kp_ui_warp_cursor(ui);
}

void kp_ui_warp_cursor(KpUi *ui)
{
	/* 0x4ce8c0(320, 200): SetCursorPos at the original's view's centre, whose WM_MOUSEMOVE the window procedure
	 * takes into 0x6d3154 at the next pump, before the game loop's next UpdateCursor: the layout's view's centre here
	 * (the system cursor is the caller's) */
	sc(ui)->warp_cursor = 1;
	ui->mouse_x = (int16_t)(ui->view_w / 2);
	ui->mouse_y = (int16_t)(ui->view_h / 2);
}

/* The scrollable range in pixels (0x543230 - 640, 0x543234 - 400: the map's pixel size less the view's). */
static int32_t range_x(KpUi *ui) { return ui->game ? (int32_t)(uint16_t)ui->game->map_w_px - ui->view_w : 0; }
static int32_t range_y(KpUi *ui) { return ui->game ? (int32_t)(uint16_t)ui->game->map_h_px - ui->view_h : 0; }

/* The mouse move while the middle button scrolls (0x448d20): a pixel is 1 % of the scrollable range. */
static void middle_move(KpUi *ui, KpUiEvent *e)
{
	KpUiScroll *s = sc(ui);
	int32_t px = ui->mouse_x - s->middle_x + HALF, py = ui->mouse_y - s->middle_y + HALF;
	(void)e;
	px = px < 0 ? 0 : px > PERCENT ? PERCENT : px;
	py = py < 0 ? 0 : py > PERCENT ? PERCENT : py;
	kp_ui_set_screen_position(ui, range_x(ui) * px / PERCENT, range_y(ui) * py / PERCENT);
}

/* BeginMiddleButtonScroll 0x448cb0: the anchor at the view's percentage of the range. */
void kp_ui_middle_down(KpUi *ui, KpUiEvent *e)
{
	KpUiScroll *s = sc(ui);
	KpUiInput *p = in(ui);
	uint32_t rx = (uint32_t)range_x(ui), ry = (uint32_t)range_y(ui);
	uint32_t qx = rx ? (uint32_t)p->screen_x * PERCENT / rx : 0, qy = ry ? (uint32_t)p->screen_y * PERCENT / ry : 0;
	s->middle_x = e->x - (int32_t)qx + HALF;
	s->middle_y = e->y - (int32_t)qy + HALF;
	ui->cursor_hidden = 1;
	p->proc[KP_UI_MOUSE_MOVE] = middle_move;
}

/* The middle-up procedure 0x448dc0. */
void kp_ui_middle_up(KpUi *ui, KpUiEvent *e)
{
	(void)e;
	ui->cursor_hidden = 0;
	in(ui)->proc[KP_UI_MOUSE_MOVE] = NULL;
}
