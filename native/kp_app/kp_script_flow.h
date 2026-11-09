/*
 * A script's names over the product's flow (kp_script.h, ADR 0017), without SDL: the values, the named conditions, the
 * dialogs and the controls a script may name, their meaning over a KpFlow, the points of the logical screen, the flow's
 * own actions (the camera, the pause) and the observations of the interface (ui) and of the game (state). The product's
 * host (sdl/kp_app_script.c) adds the keys and its own values (the world's art, the sound, the cursor) and runs the
 * inputs; the tests run these over a real flow (tests/c/test_flow.c).
 */
#ifndef KP_SCRIPT_FLOW_H
#define KP_SCRIPT_FLOW_H

#include "kp_flow.h"
#include "kp_script.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KP_SCRIPT_FLOW_NONE 2         /* kp_script_flow_act: the step is not the flow's */
#define KP_SCRIPT_FLOW_HOST 0x100000  /* a name's id from here up is the host's own (its values) */

/* Resolves word as a value, a condition, a control, a dialog or a screen of the flow's; 0, or -1 when it is none. A
 * control is a name of the table or DIALOG.ID (the dialog's control by its ID, as `ui` lists it). */
int kp_script_flow_name(KpScriptKind kind, const char *word, KpScriptName *out);
/* A value now (run: the picks', NULL none): 0, or -1 with why (no game for it). */
int kp_script_flow_value(const KpFlow *f, const KpScriptRun *run, const KpScriptRef *ref, int64_t *out, char *why,
	size_t size);
/* A named condition now: 1 holds, 0 not (why says what there is instead). */
int kp_script_flow_cond(const KpFlow *f, const KpScriptRef *ref, char *why, size_t size);
/* A point as a logical screen's pixel (run: the picks', NULL none): 0, or -1 with why (no game, no such control open,
 * off the screen). */
int kp_script_flow_point(const KpFlow *f, const KpScriptRun *run, const KpScriptPoint *p, int32_t *x, int32_t *y,
	char *why, size_t size);
/* The flow's actions (camera, pause, resume, the saves and loads, the menus, a control activated, the placement and
 * the targeting ended) and its pick: 0, -1 with why, or KP_SCRIPT_FLOW_NONE for another step. */
int kp_script_flow_act(KpFlow *f, KpScriptRun *run, const KpScriptStep *step, char *why, size_t size);
/* select NAME RADIUS: the camera on the local player's unit of the kind, the box's corners (logical pixels) over it.
 * 0, or -1 with why (no game, the player has no such unit). */
int kp_script_flow_select(KpFlow *f, const KpScriptStep *step, int32_t box[4], char *why, size_t size);
/* The key of a building the local player's race places from its worker's basic build menu ('H', 'S', 'P'), 0 when
 * the player has no worker to tell its race by. */
char kp_script_flow_race_key(const KpFlow *f);
/* The control name (KP_SCRIPT_NAME_CONTROL's id) of the dialog's control ID, -1 no such dialog. */
int32_t kp_script_flow_control(const char *dialog, int16_t id);
/* The open dialogs' names, topmost first (the modeless one before them, the console's left out), into out: a
 * failure's why. */
void kp_script_flow_dialogs(const KpFlow *f, char *out, size_t size);
/* Whether the glue's panels stand still: none on its way in or out, the controls a slide-in hid shown again. */
int kp_script_flow_still(const KpUi *ui);
/* The interface as lines for a script's author (`ui NAME`): the open dialogs topmost first, each with its place and
 * role, and its controls with their address (DIALOG.ID, as `control` takes it), kind, centre and box in logical
 * pixels, text, state and the table's names for them. 0, or -1 with why (no interface). */
int kp_script_flow_ui(const KpFlow *f, KpDumpLine emit, void *ud, char *why, size_t size);
/* The game's state lines (`state NAME`: kp_dump's, without the fields the drawing refreshes). 0, or -1 with why. */
int kp_script_flow_state(const KpFlow *f, KpDumpLine emit, void *ud, char *why, size_t size);

#ifdef __cplusplus
}
#endif

#endif
