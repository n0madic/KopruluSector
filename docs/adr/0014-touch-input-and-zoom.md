# ADR 0014: Touch input and the world's zoom as presentation

Status: accepted (phase 10, 2026-10-28). The user approved the plan (plan file and inline answers, 2026-10-28): items 1 and 2 of phase 10 (`IMPLEMENTATION_PLAN.md` §7), the touch UX and the safe areas, DPI, rotation and split-screen; the optimisation items 3–6 are not part of it. Tracker: [phase 10](../phase10.md).

## Context

On phones the product is played through SDL's synthetic mouse (touch to mouse): there is no right click, so no order can be given; the last tap stays as the mouse, a tap near an edge starts the console's edge scroll for good (and the game view ignores clicks while the cursor is a scroll arrow); the console's buttons are 36 logical pixels, about 4 mm on the Galaxy Z Fold7's cover screen; the layout reads the window's pixels, not its DPI; a rotation clamps the view's top-left instead of keeping its focus (plan §2.1). The interface's rules are the original's and are checked by the behaviour oracles in the original's dialog coordinates (ADR 0010, ADR 0011 point 3).

## Decision

1. **A translator, not new rules.** The touch layer (`native/kp_app/kp_touch.c`, no SDL) turns finger events into the inputs the interface already takes: mouse events in logical coordinates (`kp_input_point`: down, move, up and double clicks of the left and right buttons), keys and characters (`kp_ui_window_key`, `kp_ui_window_char`), and the view's position (`a->cam_x/y`, clamped by `kp_flow_clamp_camera`). The interface gains only read-only queries (what lies under a point, whether the player's own units are selected, whether a target or a placement is being chosen). So the behaviour oracles, the panel oracle and the mouse path on the desktops are untouched.
2. **Input mode.** The last real device chooses the mode (`MOUSE` or `TOUCH`): a finger event, or a mouse event that is not SDL's synthetic one (`SDL_TOUCH_MOUSEID`). SDL's touch-to-mouse synthesis is off (`SDL_HINT_TOUCH_MOUSE_EVENTS`).
3. **Gestures** (the user's choices, 2026-10-28). Thresholds are in dp, not pixels.
   - Panels and glue screens: a touch moves the interface's pointer to the finger and presses, a move drags, a lift releases, so a button fires on a lift inside it and a finger slid off cancels it, as in the original; holding shows the tooltip.
   - The game view: a tap on one of the player's own units selects (a left click); a tap elsewhere while the player's own units are selected gives the smart order (a right click); otherwise it selects. A long press does the other one. A double tap is a double click (the kind on the screen). A drag is the selection box.
   - A tap near a ping on the minimap centres the view on the ping's event (the Space's last event for the fingers); a touch elsewhere on the minimap is the minimap's own.
   - Choosing a target: the touch shows the cursor under the finger, a move moves it, the lift clicks there. Choosing a placement: the building's outline follows the finger and stays where it is lifted (no click); a tap on the outline places the building there, a tap elsewhere moves the outline there, a drag on it moves it along (the user's request after the Fold7's hand play, 2026-10-28: a lift alone placed buildings too early). The command card's Cancel, or Back, cancels.
   - Two fingers: the second finger cancels a one-finger gesture without an order; moving them pans the view, pinching zooms it.
   - The cursor is hidden and the edge scroll is off in the touch mode, except while a target or a placement is being aimed.
4. **An overlay of our own.** The control groups and a sticky Shift get buttons of their own (a presentation panel, not an original dialog), drawn with the original's font and the tooltip's outline over the translucent dialogs' blended fill; they send the keys the original binds (recall, assign, add, centre). After the first Fold7 play they form a folding column along the game view's left edge under the resource line: Shift on top, then the groups' toggle; under it, folded, the groups with units in them, opened, all ten.
5. **Zoom is presentation.** The world zooms in from 1× to 2× (not out: the view never shows more than the layout's). At zoom z the interface's game view is the view divided by z in map pixels, and points in the game view are divided by z before the interface sees them, so the scroll's limits, the selection, the box, the sounds' distance, the units on screen and the screen rectangle in the game's state stay one consistent rectangle. Zoom is neither saved nor kept between missions.
6. **Touch layouts.** A panel variant (ADR 0011 point 5): in the touch mode a console group may be drawn at a whole multiple of the layout's scale, chosen from the display's DPI so that a command card button is at least 48 dp; the panel's surface stays the original's. The interactive panels lie inside the safe area, which on Android includes the mandatory system gesture zones; the world may be drawn under cutouts and rounded corners.

All of this is `approximation` (plan §8): the original has no touch input; its rules and its panels' content stay its own.

## Consequences

- Touch play is checked by its own tests (`test_touch`, the touch layouts' geometric test) and the touch check of the product (`--touch-check`), not by the original; the campaigns' logic stays covered by the behaviour oracles because the touch layer only produces inputs the oracles already replay.
- The phone-only paths (finger events, the safe area with gesture insets, immersive mode, the iOS launch screen) need device runs (phase S [Device runs](../phaseS.md#device-runs)).
