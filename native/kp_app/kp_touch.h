/*
 * The touch layer (phase 10, ADR 0014): a phone's fingers become the interface's own input, the mouse events of the
 * logical screen's points, the view's moves and zoom; it makes no rule of its own. Headless: the host feeds the fingers
 * in logical coordinates with the clock, answers what a point lies on and takes what the gestures give.
 *
 *   panel (a dialog, a glue screen, the console)  touch: the pointer there and a press; move: a drag; lift: the release
 *                                                 (a button fires on a lift inside it, a slide off cancels it)
 *   game view, a tap                              the smart tap: the host says select (a left click) or order (a right
 *                                                 click); a quick second select tap is a double click
 *   game view, a long press                       the other one of the tap's two
 *   game view, a drag                             the selection box (a left press, the moves, the release)
 *   game view, aiming at a target                 the cursor follows the finger; the lift clicks there
 *   game view, a building's placement             the preview follows the finger and stays where it is lifted; a tap
 *                                                 on it places the building (a click there), a tap elsewhere moves it
 *                                                 there, a drag on it moves it along
 *   a host button (the touch pad's)               pressed at the touch, a tap at the lift, held: a long press
 *   two fingers                                   the view pans with their middle and zooms with their spread; the
 *                                                 second finger cancels a game-view gesture without its click
 */
#ifndef KP_TOUCH_H
#define KP_TOUCH_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { KP_TOUCH_DOWN, KP_TOUCH_MOVE, KP_TOUCH_UP, KP_TOUCH_CANCEL };

/* What a point lies on, and what a tap there does (the host's answer): a button is one of the host's own (the
 * touch pad's), pressed, tapped and held through the host's button callback; place is a building's placement, ghost
 * the point on its preview. */
enum { KP_TOUCH_PANEL, KP_TOUCH_SELECT, KP_TOUCH_ORDER, KP_TOUCH_AIM, KP_TOUCH_BUTTON, KP_TOUCH_PLACE, KP_TOUCH_GHOST };

/* A host button's moments: pressed at the touch, tapped at a lift (the host tells whether the lift is on it), held
 * long (at the long press's time), let go (after a long press, or a cancel). */
enum { KP_TOUCH_BUTTON_PRESS, KP_TOUCH_BUTTON_TAP, KP_TOUCH_BUTTON_LONG, KP_TOUCH_BUTTON_END };

/* The gestures' thresholds, in dp (a 160 dpi pixel) and milliseconds. */
#define KP_TOUCH_SLOP_DP 10.0
#define KP_TOUCH_DOUBLE_DP 24.0
#define KP_TOUCH_LONG_MS 450
#define KP_TOUCH_DOUBLE_MS 400

typedef struct {
	void *ud;
	/* what the logical point (x, y) lies on: KP_TOUCH_* */
	int (*target)(void *ud, int32_t x, int32_t y);
	/* an interface mouse event (KP_UI_MOUSE_MOVE, KP_UI_LEFT_DOWN, ...) at the logical point */
	void (*mouse)(void *ud, int event, int32_t x, int32_t y);
	/* the interface's capture ends: the buttons the gesture holds are let go without a release */
	void (*cancel)(void *ud);
	/* the aiming cursor at the logical point (shown) or hidden */
	void (*aim)(void *ud, int shown, int32_t x, int32_t y);
	/* the view's content moves by (dx, dy) logical pixels (the fingers' way) */
	void (*pan)(void *ud, int32_t dx, int32_t dy);
	/* the view zooms by factor around the logical point (NULL: no zoom) */
	void (*zoom)(void *ud, double factor, double x, double y);
	/* a host button's moment (KP_TOUCH_BUTTON_*) at the logical point (NULL: no buttons) */
	void (*button)(void *ud, int moment, int32_t x, int32_t y);
} KpTouchHost;

enum { KP_TOUCH_IDLE, KP_TOUCH_PRESS_PANEL, KP_TOUCH_PRESS_VIEW, KP_TOUCH_BOX, KP_TOUCH_AIMING, KP_TOUCH_LONG,
	KP_TOUCH_TWO, KP_TOUCH_SPENT, KP_TOUCH_PRESS_BUTTON, KP_TOUCH_BUTTON_HELD, KP_TOUCH_CONFIRM };

typedef struct {
	int64_t id;
	double x, y;
} KpTouchFinger;

typedef struct {
	KpTouchHost host;
	double px_per_dp; /* the logical screen's pixels per dp */
	int state;        /* KP_TOUCH_IDLE ... */
	int count;        /* the fingers down */
	KpTouchFinger finger[2];
	double start_x, start_y; /* the first finger's touch */
	uint32_t start_ms;
	int kind;                /* the first finger's target at its touch */
	/* the last tap, for a double one: a panel's press, a select tap of the game view */
	int last_kind;
	double last_x, last_y;
	uint32_t last_ms;
	int has_last;
	/* two fingers: their middle and spread before the move, the pan's fractions */
	double mid_x, mid_y, spread, rest_x, rest_y;
	/* a placement's preview: the aiming moves it (placing), it stays where it was lifted (parked) until a tap on it
	 * places the building; a drag that began on it keeps the finger's offset from it */
	int placing, parked;
	double park_x, park_y, off_x, off_y;
} KpTouch;

void kp_touch_init(KpTouch *t, const KpTouchHost *host);
/* The logical screen's pixels per dp (the window's scale and the display's density). */
void kp_touch_set_density(KpTouch *t, double px_per_dp);
/* A finger event (KP_TOUCH_DOWN ...) of finger id at the logical point, at now_ms. */
void kp_touch_event(KpTouch *t, int type, int64_t id, double x, double y, uint32_t now_ms);
/* Each frame: a press held long enough becomes a long press; a parked preview whose placement ended goes. */
void kp_touch_frame(KpTouch *t, uint32_t now_ms);
/* Every finger let go without its click (the window lost the focus, the screen changed). */
void kp_touch_cancel(KpTouch *t);
/* Whether a gesture is on (a finger down). */
int kp_touch_active(const KpTouch *t);

#ifdef __cplusplus
}
#endif

#endif
