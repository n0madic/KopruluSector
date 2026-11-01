/*
 * The touch layer's gestures (phase 10, ADR 0014; kp_touch.h). The thresholds follow the phones' own: a touch slop of
 * about 10 dp, a long press of 450 ms, a double tap within 400 ms and 24 dp of the first.
 */
#include "kp_touch.h"

#include <math.h>
#include <string.h>

#include "kp_ui.h"

static double dist(double ax, double ay, double bx, double by) { return hypot(ax - bx, ay - by); }

static void mouse(KpTouch *t, int event, double x, double y)
{
	t->host.mouse(t->host.ud, event, (int32_t)floor(x), (int32_t)floor(y));
}

/* A left click at the point (the pointer there, the press, the release): event is the press. */
static void click(KpTouch *t, int down, int up, double x, double y)
{
	mouse(t, KP_UI_MOUSE_MOVE, x, y);
	mouse(t, down, x, y);
	mouse(t, up, x, y);
}

static void aim(KpTouch *t, int shown, double x, double y)
{
	if (t->host.aim) t->host.aim(t->host.ud, shown, (int32_t)floor(x), (int32_t)floor(y));
}

/* The placement's preview parked (left where the finger was lifted) or gone. */
static void park(KpTouch *t, int on, double x, double y)
{
	t->parked = on;
	t->park_x = x;
	t->park_y = y;
	aim(t, on, x, y);
}

/* The aiming point of a finger at (x, y): a placement's preview keeps the offset of where the drag took it. */
static void aim_at(KpTouch *t, double x, double y)
{
	x += t->off_x;
	y += t->off_y;
	aim(t, 1, x, y);
	mouse(t, KP_UI_MOUSE_MOVE, x, y);
}

/* A press of kind at the point is the second of a double one. */
static int second(const KpTouch *t, int kind, double x, double y, uint32_t now_ms)
{
	return t->has_last && t->last_kind == kind && now_ms - t->last_ms <= KP_TOUCH_DOUBLE_MS
		&& dist(t->last_x, t->last_y, x, y) <= KP_TOUCH_DOUBLE_DP * t->px_per_dp;
}

/* The press remembered for a double one; the second of a pair is not (a third press is a press again). */
static void remember(KpTouch *t, int kind, int was_second, double x, double y, uint32_t now_ms)
{
	t->has_last = !was_second;
	t->last_kind = kind;
	t->last_x = x;
	t->last_y = y;
	t->last_ms = now_ms;
}

static void button(KpTouch *t, int moment, double x, double y)
{
	if (t->host.button) t->host.button(t->host.ud, moment, (int32_t)floor(x), (int32_t)floor(y));
}

static int finger_of(const KpTouch *t, int64_t id)
{
	int k;
	for (k = 0; k < t->count; k++) {
		if (t->finger[k].id == id) return k;
	}
	return -1;
}

static void two_measure(const KpTouch *t, double *mx, double *my, double *spread)
{
	*mx = (t->finger[0].x + t->finger[1].x) / 2;
	*my = (t->finger[0].y + t->finger[1].y) / 2;
	*spread = dist(t->finger[0].x, t->finger[0].y, t->finger[1].x, t->finger[1].y);
}

/* A game-view tap of kind at the point: a select tap is a left click (the second of a quick pair a double click), an
 * order a right click, an aim's tap a click there. */
static void tap(KpTouch *t, int kind, double x, double y, uint32_t now_ms)
{
	if (kind == KP_TOUCH_ORDER) {
		click(t, KP_UI_RIGHT_DOWN, KP_UI_RIGHT_UP, x, y);
		t->has_last = 0;
	} else if (kind == KP_TOUCH_AIM) {
		click(t, KP_UI_LEFT_DOWN, KP_UI_LEFT_UP, x, y);
		t->has_last = 0;
	} else {
		int pair = second(t, KP_TOUCH_SELECT, x, y, now_ms);
		click(t, pair ? KP_UI_LEFT_DOUBLE : KP_UI_LEFT_DOWN, KP_UI_LEFT_UP, x, y);
		remember(t, KP_TOUCH_SELECT, pair, x, y, now_ms);
	}
}

void kp_touch_init(KpTouch *t, const KpTouchHost *host)
{
	memset(t, 0, sizeof *t);
	t->host = *host;
	t->px_per_dp = 1;
}

void kp_touch_set_density(KpTouch *t, double px_per_dp) { t->px_per_dp = px_per_dp > 0 ? px_per_dp : 1; }

int kp_touch_active(const KpTouch *t) { return t->count > 0; }

/* The second finger: a game-view gesture ends without its click and the two fingers move the view (a parked preview
 * back where it was parked, the pointer's with it); a panel's press goes on (the second finger does nothing). */
static void second_finger(KpTouch *t)
{
	switch (t->state) {
	case KP_TOUCH_BOX:
		t->host.cancel(t->host.ud);
		break;
	case KP_TOUCH_AIMING: case KP_TOUCH_CONFIRM:
		if (t->parked) {
			aim(t, 1, t->park_x, t->park_y);
			mouse(t, KP_UI_MOUSE_MOVE, t->park_x, t->park_y);
		} else {
			park(t, 0, 0, 0);
		}
		break;
	case KP_TOUCH_PRESS_VIEW: case KP_TOUCH_LONG:
		break;
	default:
		return;
	}
	t->state = KP_TOUCH_TWO;
	t->has_last = 0;
	two_measure(t, &t->mid_x, &t->mid_y, &t->spread);
	t->rest_x = t->rest_y = 0;
}

static void down(KpTouch *t, int64_t id, double x, double y, uint32_t now_ms)
{
	if (t->count >= 2 || finger_of(t, id) >= 0) return;
	t->finger[t->count].id = id;
	t->finger[t->count].x = x;
	t->finger[t->count].y = y;
	if (++t->count == 2) {
		second_finger(t);
		return;
	}
	t->start_x = x;
	t->start_y = y;
	t->start_ms = now_ms;
	t->kind = t->host.target(t->host.ud, (int32_t)floor(x), (int32_t)floor(y));
	t->off_x = t->off_y = 0;
	t->placing = t->kind == KP_TOUCH_PLACE || t->kind == KP_TOUCH_GHOST;
	/* a panel's touch: the preview is the pointer's again (a host button's moves no pointer) */
	if (t->parked && !t->placing && t->kind != KP_TOUCH_BUTTON) park(t, 0, 0, 0);
	switch (t->kind) {
	case KP_TOUCH_PANEL: {
		int pair = second(t, KP_TOUCH_PANEL, x, y, now_ms);
		t->state = KP_TOUCH_PRESS_PANEL;
		mouse(t, KP_UI_MOUSE_MOVE, x, y);
		mouse(t, pair ? KP_UI_LEFT_DOUBLE : KP_UI_LEFT_DOWN, x, y);
		remember(t, KP_TOUCH_PANEL, pair, x, y, now_ms);
		break;
	}
	case KP_TOUCH_GHOST:
		if (t->parked) { /* on the parked preview: a tap places it, a drag moves it along */
			t->state = KP_TOUCH_CONFIRM;
			t->off_x = t->park_x - x;
			t->off_y = t->park_y - y;
			break;
		}
		/* fall through - a preview the finger did not park follows the finger (GCC reads only a dash after the words) */
	case KP_TOUCH_AIM: case KP_TOUCH_PLACE:
		t->state = KP_TOUCH_AIMING;
		aim_at(t, x, y);
		break;
	case KP_TOUCH_BUTTON: case KP_TOUCH_PANEL_BUTTON:
		t->state = KP_TOUCH_PRESS_BUTTON;
		button(t, KP_TOUCH_BUTTON_PRESS, x, y);
		break;
	default:
		t->state = KP_TOUCH_PRESS_VIEW;
		break;
	}
}

static void move(KpTouch *t, int64_t id, double x, double y)
{
	int k = finger_of(t, id);
	if (k < 0) return;
	t->finger[k].x = x;
	t->finger[k].y = y;
	switch (t->state) {
	case KP_TOUCH_PRESS_PANEL:
		if (k == 0) mouse(t, KP_UI_MOUSE_MOVE, x, y);
		break;
	case KP_TOUCH_PRESS_VIEW:
		if (dist(t->start_x, t->start_y, x, y) > KP_TOUCH_SLOP_DP * t->px_per_dp) {
			t->state = KP_TOUCH_BOX;
			t->has_last = 0;
			mouse(t, KP_UI_MOUSE_MOVE, t->start_x, t->start_y);
			mouse(t, KP_UI_LEFT_DOWN, t->start_x, t->start_y);
			mouse(t, KP_UI_MOUSE_MOVE, x, y);
		}
		break;
	case KP_TOUCH_BOX:
		mouse(t, KP_UI_MOUSE_MOVE, x, y);
		break;
	case KP_TOUCH_PRESS_BUTTON: case KP_TOUCH_BUTTON_HELD:
		/* a panel's button slid off: the panel's press from the touch and its drag */
		if (t->kind != KP_TOUCH_PANEL_BUTTON || k != 0
			|| dist(t->start_x, t->start_y, x, y) <= KP_TOUCH_SLOP_DP * t->px_per_dp) {
			break;
		}
		button(t, KP_TOUCH_BUTTON_END, x, y);
		t->state = KP_TOUCH_PRESS_PANEL;
		mouse(t, KP_UI_MOUSE_MOVE, t->start_x, t->start_y);
		mouse(t, KP_UI_LEFT_DOWN, t->start_x, t->start_y);
		mouse(t, KP_UI_MOUSE_MOVE, x, y);
		break;
	case KP_TOUCH_CONFIRM:
		if (dist(t->start_x, t->start_y, x, y) <= KP_TOUCH_SLOP_DP * t->px_per_dp) break;
		t->state = KP_TOUCH_AIMING;
		/* fall through */
	case KP_TOUCH_AIMING:
		aim_at(t, x, y);
		break;
	case KP_TOUCH_TWO: {
		double mx, my, spread, dx, dy;
		int32_t ix, iy;
		if (t->count != 2) break;
		two_measure(t, &mx, &my, &spread);
		dx = mx - t->mid_x + t->rest_x;
		dy = my - t->mid_y + t->rest_y;
		ix = (int32_t)trunc(dx);
		iy = (int32_t)trunc(dy);
		t->rest_x = dx - ix;
		t->rest_y = dy - iy;
		if ((ix != 0 || iy != 0) && t->host.pan) t->host.pan(t->host.ud, ix, iy);
		if (t->host.zoom && t->spread > 0 && spread > 0 && spread != t->spread) {
			t->host.zoom(t->host.ud, spread / t->spread, mx, my);
		}
		t->mid_x = mx;
		t->mid_y = my;
		t->spread = spread;
		break;
	}
	default:
		break;
	}
}

static void up(KpTouch *t, int64_t id, double x, double y, uint32_t now_ms)
{
	int k = finger_of(t, id);
	if (k < 0) return;
	t->finger[k].x = x;
	t->finger[k].y = y;
	if (t->count == 2) { /* one of two let go: the other does nothing more until it is let go too */
		int pressing = t->state == KP_TOUCH_PRESS_PANEL || t->state == KP_TOUCH_PRESS_BUTTON
			|| t->state == KP_TOUCH_BUTTON_HELD;
		t->count = 1;
		if (pressing && k == 1) return; /* the panel's or the button's press goes on */
		if (t->state == KP_TOUCH_PRESS_PANEL) mouse(t, KP_UI_LEFT_UP, x, y);
		else if (pressing) button(t, KP_TOUCH_BUTTON_END, x, y);
		if (k == 0) t->finger[0] = t->finger[1];
		t->state = KP_TOUCH_SPENT;
		return;
	}
	switch (t->state) {
	case KP_TOUCH_PRESS_PANEL:
		mouse(t, KP_UI_LEFT_UP, x, y);
		break;
	case KP_TOUCH_PRESS_VIEW:
		tap(t, t->host.target(t->host.ud, (int32_t)floor(x), (int32_t)floor(y)), x, y, now_ms);
		break;
	case KP_TOUCH_BOX:
		mouse(t, KP_UI_MOUSE_MOVE, x, y);
		mouse(t, KP_UI_LEFT_UP, x, y);
		break;
	case KP_TOUCH_AIMING:
		if (t->placing) { /* the preview stays there for a tap on it */
			mouse(t, KP_UI_MOUSE_MOVE, x + t->off_x, y + t->off_y);
			park(t, 1, x + t->off_x, y + t->off_y);
		} else {
			click(t, KP_UI_LEFT_DOWN, KP_UI_LEFT_UP, x, y);
			aim(t, 0, x, y);
		}
		break;
	case KP_TOUCH_CONFIRM:
		click(t, KP_UI_LEFT_DOWN, KP_UI_LEFT_UP, t->park_x, t->park_y);
		park(t, 0, 0, 0);
		break;
	case KP_TOUCH_PRESS_BUTTON:
		button(t, KP_TOUCH_BUTTON_TAP, x, y);
		break;
	case KP_TOUCH_BUTTON_HELD:
		button(t, KP_TOUCH_BUTTON_END, x, y);
		break;
	default:
		break;
	}
	t->count = 0;
	t->state = KP_TOUCH_IDLE;
}

void kp_touch_frame(KpTouch *t, uint32_t now_ms)
{
	int kind;
	if (t->parked && t->state == KP_TOUCH_IDLE && !t->host.placing(t->host.ud)) park(t, 0, 0, 0); /* placed or cancelled */
	if (!t->parked && t->state == KP_TOUCH_IDLE && t->host.anchor && t->host.placing(t->host.ud)) {
		int32_t x, y;
		if (t->host.anchor(t->host.ud, &x, &y)) park(t, 1, x, y); /* an add-on's preview beside its parent waits for a tap */
	}
	if (t->state == KP_TOUCH_PRESS_BUTTON && now_ms - t->start_ms >= KP_TOUCH_LONG_MS) {
		t->state = KP_TOUCH_BUTTON_HELD;
		button(t, KP_TOUCH_BUTTON_LONG, t->start_x, t->start_y);
		return;
	}
	if (t->state != KP_TOUCH_PRESS_VIEW || now_ms - t->start_ms < KP_TOUCH_LONG_MS) return;
	/* the long press: the other one of the tap's two (an order where a tap selects, a selection where it orders) */
	kind = t->host.target(t->host.ud, (int32_t)floor(t->start_x), (int32_t)floor(t->start_y));
	t->state = KP_TOUCH_LONG;
	t->has_last = 0;
	if (kind == KP_TOUCH_SELECT) click(t, KP_UI_RIGHT_DOWN, KP_UI_RIGHT_UP, t->start_x, t->start_y);
	else if (kind == KP_TOUCH_ORDER) click(t, KP_UI_LEFT_DOWN, KP_UI_LEFT_UP, t->start_x, t->start_y);
}

void kp_touch_cancel(KpTouch *t)
{
	if (t->state == KP_TOUCH_BOX || t->state == KP_TOUCH_PRESS_PANEL) t->host.cancel(t->host.ud);
	if (t->state == KP_TOUCH_AIMING || t->state == KP_TOUCH_CONFIRM || t->parked) park(t, 0, 0, 0);
	if (t->state == KP_TOUCH_PRESS_BUTTON || t->state == KP_TOUCH_BUTTON_HELD) button(t, KP_TOUCH_BUTTON_END, 0, 0);
	t->count = 0;
	t->state = KP_TOUCH_IDLE;
	t->has_last = 0;
}

void kp_touch_event(KpTouch *t, int type, int64_t id, double x, double y, uint32_t now_ms)
{
	switch (type) {
	case KP_TOUCH_DOWN: down(t, id, x, y, now_ms); break;
	case KP_TOUCH_MOVE: move(t, id, x, y); break;
	case KP_TOUCH_UP: up(t, id, x, y, now_ms); break;
	default: kp_touch_cancel(t); break;
	}
}
