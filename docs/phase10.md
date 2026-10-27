# Phase 10 progress

Plan: `IMPLEMENTATION_PLAN.md` §7 phase 10, items 1 and 2 (touch UX; safe areas, DPI, rotation and split-screen on the fly). Started 2026-10-28, after the user approved the plan ([ADR 0014](adr/0014-touch-input-and-zoom.md)):
- the touch layer translates gestures into the inputs the interface already takes and changes no rule (the oracles stay as they are);
- a tap is a smart tap (select one's own unit, else order when one's own units are selected, else select); a long press does the other one;
- buttons fire on the lift inside them, as in the original;
- the world zooms in only, from 1× to 2×, as presentation;
- the cursor is hidden in the touch mode except while a target or a placement is aimed; no edge scroll.

The optimisation items 3–6 of phase 10 are not part of this work.

## Work items

| # | Item | Status | Where |
|---|---|---|---|
| 10.0 | ADR 0014, this tracker | done | `docs/adr/0014-touch-input-and-zoom.md`, `docs/phase10.md` |
| 10A | The touch layer: finger events, input mode, smart tap, long press, box, aiming, two-finger pan, Android back | done on macOS ([10A](#10a-the-touch-layer)); the Fold7 run waits for the phone | `native/kp_app/kp_touch.{h,c}`, `native/kp_app/sdl/kp_app_input.c`, `kp_hud.c`, `kp_ui_input.c`, `tests/c/test_touch.c`, `test_hud.touch_taps_select_and_order` |
| 10B | The overlay: control-group buttons, sticky Shift | done on macOS ([10B](#10b-the-touch-pad)) | `native/kp_core/src/core/kp_ui_touch.c`, `native/kp_app/kp_hud.c` (`KpHudPad`), `kp_touch.c` (host buttons), `kp_present` (`KpCanvas.overlay`), `test_hud.touch_pad_assigns_and_recalls_groups`, `test_touch.host_button_taps_and_holds` |
| 10C | The world's zoom (1×–2×, pinch) | | `native/kp_gfx/`, `native/kp_app/kp_hud.c` |
| 10D | Touch layouts: console groups at a multiple of the scale by DPI, the large command card | | `native/kp_present/kp_layout.c`, `kp_canvas.c` |
| 10E | Safe areas (cutouts, gesture zones), immersive mode, the iOS plist, the view's focus kept on resize and rotation | | `native/kp_gfx/kp_gfx_passes.c`, `tools/native/android_app.sh`, `tools/product.sh` |
| 10F | Acceptance: `--touch-check DIR` on macOS, the web, the iOS Simulator and the Fold7; hand play on the Fold7 | | `native/kp_app/sdl/kp_app_dev.c` |

## 10A: the touch layer

- **Fingers, not a synthetic mouse.** `SDL_HINT_TOUCH_MOUSE_EVENTS` is off; `SDL_EVENT_FINGER_*` of a direct touch device (a screen: a trackpad's indirect touches stay the mouse's) go to `kp_touch` at the logical screen's point, kept below a pixel; mouse events with `SDL_TOUCH_MOUSEID` are dropped. The last real device sets the input mode (`a->touch_mode`).
- **The gestures** (`kp_touch.c`, no SDL): a panel's touch moves the interface's pointer there and presses, a move drags, the lift releases (the original's buttons fire on a lift inside them; a quick second press nearby is a double click, as `CS_DBLCLKS`); a game-view touch waits for its lift: within the slop (10 dp) a tap, held 450 ms a long press, past the slop the box (a left press at the touch, the moves, the release). The tap is the smart tap: `kp_ui_touch_target` (read-only) answers select (a unit of the local player's under the point, or none of the player's selected), order (the player's units selected) or aim (a target or a placement being chosen); a select tap is a left click (the second within 400 ms and 24 dp a double click: the kind on the screen), an order tap a right click, a long press the other one. Aiming, the cursor shows under the finger and follows it, the lift clicks there. A second finger ends a game-view gesture without its click (the box through the capture's loss) and pans the view with the fingers' middle; over a panel's press it does nothing.
- **No cursor, no edge scroll.** A touch frame updates no cursor (`kp_hud_frame`'s `KP_HUD_POINTER_NONE`), so the edge scroll never starts and the cursor is not drawn; aiming updates the pointer's cursor alone (`kp_ui_pointer_update`: the targeting's crosshair, no edge arrow, with which the game view would take no click). Entering the touch mode sets the cursor id back to the arrow.
- **Android's back** is Escape (`SDL_HINT_ANDROID_TRAP_BACK_BUTTON`, `SDL_SCANCODE_AC_BACK`): the game menu, a transmission's skip, the glue's back.
- **Cancelled gestures:** the focus lost, or the screen the gesture began on changed (the glue and the game, the logical size: a rotation, a fold), lets every finger go without its click (`kp_touch_cancel`).
- **Tests:** `test_touch` (11: a panel's press and double press, the smart tap, the long press, the double tap, the box and its slop, the aim, two fingers' cancel, pan and zoom, a panel's press under a second finger, the cancel, the density's thresholds); `test_hud.touch_taps_select_and_order` (terran01: a tap selects a Marine, a tap on the ground orders a right click, a long press on another Marine orders onto it, the console is a panel's).

## 10B: the touch pad

- **What it is.** A row of eleven buttons over the game view's top, centred, in the touch mode's games only: the control groups 1–9 and 0, then Shift. Ours, not the original's (ADR 0014 point 4): `kp_ui_touch_pad` draws them in the tooltip box's look (the UI colours 15 and 16, ShowTooltipText 0x418f60) with the game's font16, a group with units in it white, an empty one grey, the pressed button and an active Shift yellow with a double outline. The canvas lays the pad over the panels as a layer of its own (`KpCanvas.overlay`, index 0 transparent, under the tooltip); `kp_hud` lays it out (`KpHudPad`: a button of `KP_HUD_PAD_DP` = 44 dp, narrowed to the screen's width, its height four fifths of its width) and draws it again only when its state or the groups held change.
- **Its buttons** are the touch layer's host buttons (`KP_TOUCH_BUTTON`: pressed at the touch, a tap at the lift, a long press when held): a group's tap is the digit's character (the group recalled; the original's second recall within 500 ms centres the view on it), its long press Ctrl and the digit (the selection assigned, `ACC_ASSIGN_FIRST`); a lift off the button pressed does nothing. Shift's tap turns it on until it is tapped again (the queued orders of several waypoints need it held across taps, so it stays rather than dropping after one order, as the plan first said): the interface's Shift byte is set while it is on.
- **Tests:** `test_hud.touch_pad_assigns_and_recalls_groups` (the pad laid over the game view with its buttons of 44 dp, its hits and gaps, a long press assigning group 1, a lift off the pressed button, Shift on and off, the pad gone off the touch mode), `test_touch.host_button_taps_and_holds`.
