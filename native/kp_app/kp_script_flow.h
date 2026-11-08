/*
 * A script's names over the product's flow (kp_script.h, ADR 0017), without SDL: the values, the named conditions, the
 * dialogs and the controls a script may name, their meaning over a KpFlow, the points of the logical screen and the
 * flow's own actions (the camera, the pause). The product's host (sdl/kp_app_script.c) adds the keys and runs the
 * inputs; the tests run these over a real flow (tests/c/test_flow.c).
 */
#ifndef KP_SCRIPT_FLOW_H
#define KP_SCRIPT_FLOW_H

#include "kp_flow.h"
#include "kp_script.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KP_SCRIPT_FLOW_NONE 2 /* kp_script_flow_act: the step is not the flow's */

/* Resolves word as a value, a condition, a control, a dialog or a screen of the flow's; 0, or -1 when it is none. */
int kp_script_flow_name(KpScriptKind kind, const char *word, KpScriptName *out);
/* A value now: 0, or -1 with why (no game for it). */
int kp_script_flow_value(const KpFlow *f, const KpScriptRef *ref, int64_t *out, char *why, size_t size);
/* A named condition now: 1 holds, 0 not (why says what there is instead). */
int kp_script_flow_cond(const KpFlow *f, const KpScriptRef *ref, char *why, size_t size);
/* A point as a logical screen's pixel: 0, or -1 with why (no game, no such control open, off the screen). */
int kp_script_flow_point(const KpFlow *f, const KpScriptPoint *p, int32_t *x, int32_t *y, char *why, size_t size);
/* The flow's actions (camera, pause, resume): 0, -1 with why, or KP_SCRIPT_FLOW_NONE for another step. */
int kp_script_flow_act(KpFlow *f, const KpScriptStep *step, char *why, size_t size);
/* The open dialogs' names, topmost first (the modeless one before them), into out: a failure's why. */
void kp_script_flow_dialogs(const KpFlow *f, char *out, size_t size);

#ifdef __cplusplus
}
#endif

#endif
