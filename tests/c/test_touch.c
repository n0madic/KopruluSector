/*
 * The touch layer's gestures (phase 10, ADR 0014; native/kp_app/kp_touch.c) over a host that records what they give:
 * a panel's press, the smart tap and its long press, the double tap, the box, the aim, a placement's confirmation, two
 * fingers.
 */
#include "kp_test.h"

#include <math.h>

#include "kp_touch.h"
#include "kp_ui.h"

#define LOG 64

typedef struct {
	int kind; /* what every point lies on (KP_TOUCH_*) */
	int events[LOG], x[LOG], y[LOG], count;
	int cancels, aim_shown, aims;
	int moments[LOG], moment_count;
	int32_t pan_x, pan_y;
	double zoom;
	int ghost_x, ghost_y; /* a placement's preview: 64 x 64 pixels at the last move's point */
} Host;

static int target(void *ud, int32_t x, int32_t y)
{
	Host *h = (Host *)ud;
	if (h->kind == KP_TOUCH_PLACE && x >= h->ghost_x && y >= h->ghost_y && x < h->ghost_x + 64 && y < h->ghost_y + 64) {
		return KP_TOUCH_GHOST;
	}
	return h->kind;
}

static void mouse(void *ud, int event, int32_t x, int32_t y)
{
	Host *h = (Host *)ud;
	if (h->count == LOG) return;
	if (event == KP_UI_MOUSE_MOVE) {
		h->ghost_x = x;
		h->ghost_y = y;
	}
	h->events[h->count] = event;
	h->x[h->count] = x;
	h->y[h->count] = y;
	h->count++;
}

static void cancel(void *ud) { ((Host *)ud)->cancels++; }

static void aim(void *ud, int shown, int32_t x, int32_t y)
{
	Host *h = (Host *)ud;
	(void)x;
	(void)y;
	h->aim_shown = shown;
	h->aims++;
}

static void pan(void *ud, int32_t dx, int32_t dy)
{
	Host *h = (Host *)ud;
	h->pan_x += dx;
	h->pan_y += dy;
}

static void zoom(void *ud, double factor, double x, double y)
{
	(void)x;
	(void)y;
	((Host *)ud)->zoom *= factor;
}

static void button(void *ud, int moment, int32_t x, int32_t y)
{
	Host *h = (Host *)ud;
	(void)x;
	(void)y;
	if (h->moment_count < LOG) h->moments[h->moment_count++] = moment;
}

/* A touch layer over host h at 2 logical pixels per dp (a slop of 20 pixels). */
static void start(KpTouch *t, Host *h, int kind)
{
	KpTouchHost host;
	memset(h, 0, sizeof *h);
	h->kind = kind;
	h->zoom = 1;
	h->ghost_x = h->ghost_y = -1000;
	memset(&host, 0, sizeof host);
	host.ud = h;
	host.target = target;
	host.mouse = mouse;
	host.cancel = cancel;
	host.aim = aim;
	host.pan = pan;
	host.zoom = zoom;
	host.button = button;
	kp_touch_init(t, &host);
	kp_touch_set_density(t, 2);
}

/* The events from first on are exactly the n given. */
static int events_are(const Host *h, int first, int n, const int *want)
{
	int k;
	if (h->count - first != n) return 0;
	for (k = 0; k < n; k++) {
		if (h->events[first + k] != want[k]) return 0;
	}
	return 1;
}

static void panel_press_follows_the_finger(KpT *T)
{
	KpTouch t;
	Host h;
	static const int WANT[] = { KP_UI_MOUSE_MOVE, KP_UI_LEFT_DOWN, KP_UI_MOUSE_MOVE, KP_UI_LEFT_UP };
	start(&t, &h, KP_TOUCH_PANEL);
	kp_touch_event(&t, KP_TOUCH_DOWN, 1, 100, 200, 1000);
	kp_t_ok(T, events_are(&h, 0, 2, WANT), "the touch moves the pointer there and presses");
	kp_touch_event(&t, KP_TOUCH_MOVE, 1, 140, 210, 1050);
	kp_touch_frame(&t, 2000); /* a panel's long press is a held press (its tooltip), nothing more */
	kp_touch_event(&t, KP_TOUCH_UP, 1, 140, 210, 2100);
	kp_t_ok(T, events_are(&h, 0, 4, WANT), "the drag moves, the lift releases");
	kp_t_eq(T, h.x[3], 140, "released where the finger left");
	kp_t_ok(T, !kp_touch_active(&t), "no finger down");
}

static void panel_double_tap_is_a_double_click(KpT *T)
{
	KpTouch t;
	Host h;
	start(&t, &h, KP_TOUCH_PANEL);
	kp_touch_event(&t, KP_TOUCH_DOWN, 1, 100, 100, 1000);
	kp_touch_event(&t, KP_TOUCH_UP, 1, 100, 100, 1060);
	kp_touch_event(&t, KP_TOUCH_DOWN, 2, 104, 102, 1200);
	kp_t_eq(T, h.events[h.count - 1], KP_UI_LEFT_DOUBLE, "a quick second press nearby");
	kp_touch_event(&t, KP_TOUCH_UP, 2, 104, 102, 1260);
	kp_touch_event(&t, KP_TOUCH_DOWN, 3, 104, 102, 1400);
	kp_t_eq(T, h.events[h.count - 1], KP_UI_LEFT_DOWN, "a third press is a press again");
	kp_touch_event(&t, KP_TOUCH_UP, 3, 104, 102, 1460);
	kp_touch_event(&t, KP_TOUCH_DOWN, 4, 104, 102, 2400);
	kp_t_eq(T, h.events[h.count - 1], KP_UI_LEFT_DOWN, "a slow second press is a press");
	kp_touch_event(&t, KP_TOUCH_UP, 4, 104, 102, 2460);
	kp_touch_event(&t, KP_TOUCH_DOWN, 5, 200, 102, 2600);
	kp_t_eq(T, h.events[h.count - 1], KP_UI_LEFT_DOWN, "a press far from the first is a press");
}

static void tap_selects_or_orders(KpT *T)
{
	KpTouch t;
	Host h;
	static const int SELECT[] = { KP_UI_MOUSE_MOVE, KP_UI_LEFT_DOWN, KP_UI_LEFT_UP };
	static const int ORDER[] = { KP_UI_MOUSE_MOVE, KP_UI_RIGHT_DOWN, KP_UI_RIGHT_UP };
	start(&t, &h, KP_TOUCH_SELECT);
	kp_touch_event(&t, KP_TOUCH_DOWN, 1, 300, 150, 1000);
	kp_t_eq(T, h.count, 0, "a game-view touch waits for its lift");
	kp_touch_event(&t, KP_TOUCH_MOVE, 1, 310, 155, 1050); /* within the slop */
	kp_touch_event(&t, KP_TOUCH_UP, 1, 310, 155, 1100);
	kp_t_ok(T, events_are(&h, 0, 3, SELECT), "a select tap is a left click");
	kp_t_eq(T, h.x[1], 310, "at the lift's point");
	h.kind = KP_TOUCH_ORDER;
	h.count = 0;
	kp_touch_event(&t, KP_TOUCH_DOWN, 2, 500, 150, 3000);
	kp_touch_event(&t, KP_TOUCH_UP, 2, 500, 150, 3100);
	kp_t_ok(T, events_are(&h, 0, 3, ORDER), "an order tap is a right click");
}

static void long_press_does_the_other_one(KpT *T)
{
	KpTouch t;
	Host h;
	static const int ORDER[] = { KP_UI_MOUSE_MOVE, KP_UI_RIGHT_DOWN, KP_UI_RIGHT_UP };
	static const int SELECT[] = { KP_UI_MOUSE_MOVE, KP_UI_LEFT_DOWN, KP_UI_LEFT_UP };
	start(&t, &h, KP_TOUCH_SELECT);
	kp_touch_event(&t, KP_TOUCH_DOWN, 1, 300, 150, 1000);
	kp_touch_frame(&t, 1000 + KP_TOUCH_LONG_MS - 1);
	kp_t_eq(T, h.count, 0, "not yet long");
	kp_touch_frame(&t, 1000 + KP_TOUCH_LONG_MS);
	kp_t_ok(T, events_are(&h, 0, 3, ORDER), "held where a tap selects: an order, at once");
	kp_touch_event(&t, KP_TOUCH_UP, 1, 300, 150, 2000);
	kp_t_eq(T, h.count, 3, "the lift adds nothing");
	h.kind = KP_TOUCH_ORDER;
	h.count = 0;
	kp_touch_event(&t, KP_TOUCH_DOWN, 2, 300, 150, 3000);
	kp_touch_frame(&t, 3000 + KP_TOUCH_LONG_MS);
	kp_t_ok(T, events_are(&h, 0, 3, SELECT), "held where a tap orders: a selection");
	kp_touch_event(&t, KP_TOUCH_UP, 2, 300, 150, 4000);
}

static void double_tap_selects_the_kind(KpT *T)
{
	KpTouch t;
	Host h;
	start(&t, &h, KP_TOUCH_SELECT);
	kp_touch_event(&t, KP_TOUCH_DOWN, 1, 300, 150, 1000);
	kp_touch_event(&t, KP_TOUCH_UP, 1, 300, 150, 1080);
	kp_touch_event(&t, KP_TOUCH_DOWN, 2, 306, 150, 1250);
	kp_touch_event(&t, KP_TOUCH_UP, 2, 306, 150, 1320);
	kp_t_eq(T, h.events[4], KP_UI_LEFT_DOUBLE, "the second select tap is the double click");
	h.kind = KP_TOUCH_ORDER;
	h.count = 0;
	kp_touch_event(&t, KP_TOUCH_DOWN, 3, 306, 150, 1400);
	kp_touch_event(&t, KP_TOUCH_UP, 3, 306, 150, 1450);
	kp_touch_event(&t, KP_TOUCH_DOWN, 4, 306, 150, 1500);
	kp_touch_event(&t, KP_TOUCH_UP, 4, 306, 150, 1550);
	kp_t_eq(T, h.events[4], KP_UI_RIGHT_DOWN, "two order taps are two orders");
}

static void drag_is_the_box(KpT *T)
{
	KpTouch t;
	Host h;
	static const int BOX[] = { KP_UI_MOUSE_MOVE, KP_UI_LEFT_DOWN, KP_UI_MOUSE_MOVE, KP_UI_MOUSE_MOVE, KP_UI_MOUSE_MOVE,
		KP_UI_LEFT_UP };
	start(&t, &h, KP_TOUCH_ORDER);
	kp_touch_event(&t, KP_TOUCH_DOWN, 1, 100, 100, 1000);
	kp_touch_event(&t, KP_TOUCH_MOVE, 1, 115, 105, 1020); /* 15.8 pixels: inside the 20 of the slop */
	kp_t_eq(T, h.count, 0, "inside the slop: still a tap");
	kp_touch_event(&t, KP_TOUCH_MOVE, 1, 130, 120, 1040);
	kp_t_eq(T, h.x[1], 100, "the box starts at the touch");
	kp_touch_event(&t, KP_TOUCH_MOVE, 1, 200, 180, 1060);
	kp_touch_frame(&t, 3000); /* a drag never becomes a long press */
	kp_touch_event(&t, KP_TOUCH_UP, 1, 210, 190, 3100);
	kp_t_ok(T, events_are(&h, 0, 6, BOX), "a left press at the touch, the moves, the release at the lift");
	kp_t_eq(T, h.x[5], 210, NULL);
}

static void aim_follows_and_clicks_at_the_lift(KpT *T)
{
	KpTouch t;
	Host h;
	static const int AIM[] = { KP_UI_MOUSE_MOVE, KP_UI_MOUSE_MOVE, KP_UI_MOUSE_MOVE, KP_UI_LEFT_DOWN, KP_UI_LEFT_UP };
	start(&t, &h, KP_TOUCH_AIM);
	kp_touch_event(&t, KP_TOUCH_DOWN, 1, 100, 100, 1000);
	kp_t_ok(T, h.aim_shown, "the cursor shows under the finger");
	kp_touch_event(&t, KP_TOUCH_MOVE, 1, 300, 200, 1100);
	kp_touch_frame(&t, 3000);
	kp_touch_event(&t, KP_TOUCH_UP, 1, 300, 200, 3100);
	kp_t_ok(T, events_are(&h, 0, 5, AIM), "the pointer follows, the lift clicks there");
	kp_t_eq(T, h.x[3], 300, NULL);
	kp_t_ok(T, !h.aim_shown, "the cursor hides after");
}

/* A building's placement: the preview follows the finger and stays where it was lifted, no click; a tap elsewhere
 * moves it there; a drag on it moves it along with the finger's offset; a tap on it places it there (a click at the
 * preview's point, not the tap's); the placement's end hides the parked preview. */
static void placement_waits_for_a_tap_on_it(KpT *T)
{
	KpTouch t;
	Host h;
	int n;
	static const int CLICK[] = { KP_UI_MOUSE_MOVE, KP_UI_LEFT_DOWN, KP_UI_LEFT_UP };
	start(&t, &h, KP_TOUCH_PLACE);
	kp_touch_event(&t, KP_TOUCH_DOWN, 1, 100, 100, 1000);
	kp_touch_event(&t, KP_TOUCH_MOVE, 1, 300, 200, 1100);
	kp_touch_event(&t, KP_TOUCH_UP, 1, 300, 200, 1200);
	for (n = 0; n < h.count; n++) kp_t_ok(T, h.events[n] == KP_UI_MOUSE_MOVE, "the drag only moves the pointer");
	kp_t_ok(T, h.aim_shown && t.parked, "the preview parked where the finger was lifted");
	kp_t_eq(T, h.ghost_x, 300, NULL);
	kp_touch_frame(&t, 1300);
	kp_t_ok(T, t.parked, "still parked while the placement goes on");
	n = h.count;
	kp_touch_event(&t, KP_TOUCH_DOWN, 1, 500, 400, 2000); /* a tap elsewhere moves it there */
	kp_touch_event(&t, KP_TOUCH_UP, 1, 500, 400, 2060);
	for (; n < h.count; n++) kp_t_ok(T, h.events[n] == KP_UI_MOUSE_MOVE, "a tap elsewhere only moves it");
	kp_t_ok(T, t.parked && h.ghost_x == 500 && h.ghost_y == 400, "parked at the tap");
	kp_touch_event(&t, KP_TOUCH_DOWN, 1, 520, 420, 3000); /* a drag on it keeps the offset */
	kp_touch_event(&t, KP_TOUCH_MOVE, 1, 620, 420, 3100);
	kp_touch_event(&t, KP_TOUCH_UP, 1, 620, 420, 3200);
	kp_t_ok(T, t.parked && h.ghost_x == 600 && h.ghost_y == 400, "dragged along: %d,%d", h.ghost_x, h.ghost_y);
	n = h.count;
	kp_touch_event(&t, KP_TOUCH_DOWN, 1, 630, 450, 4000); /* a tap on it places it */
	kp_touch_event(&t, KP_TOUCH_UP, 1, 632, 451, 4060);
	kp_t_ok(T, events_are(&h, n, 3, CLICK), "the tap on it clicks");
	kp_t_ok(T, h.x[n + 1] == 600 && h.y[n + 1] == 400, "at the preview's point: %d,%d", h.x[n + 1], h.y[n + 1]);
	kp_t_ok(T, !t.parked && !h.aim_shown, "the preview no longer parked");
	/* a parked preview goes when the placement ends (placed or cancelled elsewhere) */
	kp_touch_event(&t, KP_TOUCH_DOWN, 1, 100, 100, 5000);
	kp_touch_event(&t, KP_TOUCH_UP, 1, 100, 100, 5060);
	kp_t_ok(T, t.parked, NULL);
	h.kind = KP_TOUCH_SELECT;
	kp_touch_frame(&t, 5100);
	kp_t_ok(T, !t.parked && !h.aim_shown, "the ended placement's preview hidden");
}

static void second_finger_cancels_and_pans(KpT *T)
{
	KpTouch t;
	Host h;
	start(&t, &h, KP_TOUCH_SELECT);
	kp_touch_event(&t, KP_TOUCH_DOWN, 1, 100, 100, 1000);
	kp_touch_event(&t, KP_TOUCH_MOVE, 1, 150, 150, 1020); /* a box */
	kp_touch_event(&t, KP_TOUCH_DOWN, 2, 200, 100, 1040);
	kp_t_eq(T, h.cancels, 1, "the box ends without its release");
	h.count = 0;
	kp_touch_event(&t, KP_TOUCH_MOVE, 1, 160, 160, 1060);
	kp_touch_event(&t, KP_TOUCH_MOVE, 2, 210, 110, 1060);
	kp_t_eq(T, h.pan_x, 10, "the middle's way");
	kp_t_eq(T, h.pan_y, 10, NULL);
	kp_t_eq(T, h.count, 0, "no mouse events");
	kp_touch_event(&t, KP_TOUCH_MOVE, 2, 260, 110, 1080); /* the spread from (50, 50) at the touch to (100, 50) */
	kp_t_ok(T, fabs(h.zoom - hypot(100, 50) / hypot(50, 50)) < 1e-9, "zoomed by the spread: %f", h.zoom);
	kp_touch_event(&t, KP_TOUCH_UP, 2, 260, 110, 1100);
	kp_touch_event(&t, KP_TOUCH_MOVE, 1, 300, 300, 1120);
	kp_touch_frame(&t, 4000);
	kp_touch_event(&t, KP_TOUCH_UP, 1, 300, 300, 4100);
	kp_t_eq(T, h.count, 0, "the finger left over does nothing");
	kp_t_ok(T, !kp_touch_active(&t), NULL);
	/* a game-view touch not yet a gesture: the second finger takes it without a cancel */
	kp_touch_event(&t, KP_TOUCH_DOWN, 3, 100, 100, 5000);
	kp_touch_event(&t, KP_TOUCH_DOWN, 4, 200, 100, 5010);
	kp_touch_event(&t, KP_TOUCH_UP, 3, 100, 100, 5020);
	kp_touch_event(&t, KP_TOUCH_UP, 4, 200, 100, 5030);
	kp_t_eq(T, h.count, 0, "no tap");
	kp_t_eq(T, h.cancels, 1, NULL);
}

static void second_finger_leaves_a_panel_press(KpT *T)
{
	KpTouch t;
	Host h;
	start(&t, &h, KP_TOUCH_PANEL);
	kp_touch_event(&t, KP_TOUCH_DOWN, 1, 100, 100, 1000);
	kp_touch_event(&t, KP_TOUCH_DOWN, 2, 300, 100, 1010);
	kp_touch_event(&t, KP_TOUCH_UP, 2, 300, 100, 1020);
	kp_touch_event(&t, KP_TOUCH_UP, 1, 100, 100, 1030);
	kp_t_eq(T, h.count, 3, "the press and its release, the second finger's nothing");
	kp_t_eq(T, h.events[2], KP_UI_LEFT_UP, NULL);
	kp_t_eq(T, h.x[2], 100, NULL);
	kp_t_eq(T, h.pan_x, 0, "no pan over a panel's press");
}

static void cancel_lets_go_without_the_click(KpT *T)
{
	KpTouch t;
	Host h;
	start(&t, &h, KP_TOUCH_AIM);
	kp_touch_event(&t, KP_TOUCH_DOWN, 1, 100, 100, 1000);
	kp_touch_event(&t, KP_TOUCH_CANCEL, 1, 100, 100, 1010);
	kp_t_ok(T, !h.aim_shown && !kp_touch_active(&t), "the aim ends");
	kp_touch_event(&t, KP_TOUCH_UP, 1, 100, 100, 1020);
	kp_t_eq(T, h.events[h.count - 1], KP_UI_MOUSE_MOVE, "no click after the cancel");
	h.kind = KP_TOUCH_PANEL;
	kp_touch_event(&t, KP_TOUCH_DOWN, 2, 100, 100, 2000);
	kp_touch_cancel(&t);
	kp_t_eq(T, h.cancels, 1, "a panel's press is let go by the host");
}

static void thresholds_follow_the_density(KpT *T)
{
	KpTouch t;
	Host h;
	start(&t, &h, KP_TOUCH_SELECT);
	kp_touch_set_density(&t, 0.5); /* a slop of 5 pixels */
	kp_touch_event(&t, KP_TOUCH_DOWN, 1, 100, 100, 1000);
	kp_touch_event(&t, KP_TOUCH_MOVE, 1, 106, 100, 1010);
	kp_t_eq(T, h.events[1], KP_UI_LEFT_DOWN, "6 pixels is a drag at half a pixel per dp");
	kp_touch_event(&t, KP_TOUCH_UP, 1, 106, 100, 1020);
}

static void host_button_taps_and_holds(KpT *T)
{
	KpTouch t;
	Host h;
	start(&t, &h, KP_TOUCH_BUTTON);
	kp_touch_event(&t, KP_TOUCH_DOWN, 1, 100, 10, 1000);
	kp_touch_event(&t, KP_TOUCH_UP, 1, 102, 10, 1100);
	kp_t_eq(T, h.moment_count, 2, "pressed, tapped");
	kp_t_eq(T, h.moments[0], KP_TOUCH_BUTTON_PRESS, NULL);
	kp_t_eq(T, h.moments[1], KP_TOUCH_BUTTON_TAP, NULL);
	kp_touch_event(&t, KP_TOUCH_DOWN, 2, 100, 10, 2000);
	kp_touch_frame(&t, 2000 + KP_TOUCH_LONG_MS);
	kp_touch_event(&t, KP_TOUCH_DOWN, 3, 300, 300, 2500); /* a second finger: no pan over a button's press */
	kp_touch_event(&t, KP_TOUCH_MOVE, 3, 350, 300, 2510);
	kp_touch_event(&t, KP_TOUCH_UP, 3, 350, 300, 2520);
	kp_touch_event(&t, KP_TOUCH_UP, 2, 100, 10, 2600);
	kp_t_eq(T, h.moment_count, 5, "pressed, held long, let go");
	kp_t_eq(T, h.moments[3], KP_TOUCH_BUTTON_LONG, NULL);
	kp_t_eq(T, h.moments[4], KP_TOUCH_BUTTON_END, NULL);
	kp_t_eq(T, h.pan_x, 0, NULL);
	kp_t_eq(T, h.count, 0, "no mouse events");
}

static const KpTest TESTS[] = {
	{ "panel_press_follows_the_finger", panel_press_follows_the_finger, 0 },
	{ "panel_double_tap_is_a_double_click", panel_double_tap_is_a_double_click, 0 },
	{ "tap_selects_or_orders", tap_selects_or_orders, 0 },
	{ "long_press_does_the_other_one", long_press_does_the_other_one, 0 },
	{ "double_tap_selects_the_kind", double_tap_selects_the_kind, 0 },
	{ "drag_is_the_box", drag_is_the_box, 0 },
	{ "aim_follows_and_clicks_at_the_lift", aim_follows_and_clicks_at_the_lift, 0 },
	{ "placement_waits_for_a_tap_on_it", placement_waits_for_a_tap_on_it, 0 },
	{ "second_finger_cancels_and_pans", second_finger_cancels_and_pans, 0 },
	{ "second_finger_leaves_a_panel_press", second_finger_leaves_a_panel_press, 0 },
	{ "cancel_lets_go_without_the_click", cancel_lets_go_without_the_click, 0 },
	{ "thresholds_follow_the_density", thresholds_follow_the_density, 0 },
	{ "host_button_taps_and_holds", host_button_taps_and_holds, 0 },
};

KP_SUITE(kp_suite_touch, "test_touch", TESTS);
