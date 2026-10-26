/* The helpers of the C suites that drive the interface kp_ui (test_kp_ui.c, test_hud.c): an interface over the
 * generated data, the window procedure's events, the queued commands and a dialog's BIN name. */
#ifndef KP_TEST_UI_H
#define KP_TEST_UI_H

#include "kp_test.h"
#include "kp_oracle_ui.h"

#ifdef __cplusplus
extern "C" {
#endif

/* An interface over generated/data/ui.kpd and fonts.kpd with the files of flags (KP_ORACLE_UI_*), released when the
 * test ends; fails without them. */
KpOracleUi *kp_t_ui(KpT *T, int flags);
/* An event of type: a key, character or command event (a the key, b the modifiers) at the mouse, a wheel event (a
 * its parameter), else a mouse event at (a, b). */
void kp_t_ui_input(KpUi *ui, int type, int64_t a, int64_t b);
/* The pump's idle event at the mouse (IDLE with no position). */
void kp_t_ui_idle(KpUi *ui);
/* The commands an interface queued since the last take: their count, the first's bytes in first (NULL: none kept). */
int kp_t_ui_take_commands(KpUi *ui, uint8_t *first, size_t *first_size);
/* The BIN name of control d's dialog in lower case ("" for a control without a BIN). */
const char *kp_t_ui_bin(const KpUi *ui, int32_t d, char *out, size_t size);

#ifdef __cplusplus
}
#endif

#endif
