/* The helpers of the C suites that drive the interface kp_ui (test_kp_ui.c, test_hud.c, test_ui_data.c,
 * test_locale.c): an interface over the original data or over the converter's fixtures, the window procedure's events,
 * the queued commands and a dialog's BIN name. */
#ifndef KP_TEST_UI_H
#define KP_TEST_UI_H

#include "kp_test.h"
#include "kp_oracle_ui.h"
#include "kp_ui_data.h"

#ifdef __cplusplus
extern "C" {
#endif

/* An interface over the original data (kp_oracle_ui_new) with the parts of flags (KP_ORACLE_UI_*), released when the
 * test ends; fails without them. */
KpOracleUi *kp_t_ui(KpT *T, int flags);
/* The interface's data from the fixtures' (KP_T_FIXTURES) ui.kpd and fonts.kpd, tiny containers of the interface's
 * kinds, the languages' and the speech's overlay formats (kp_ui.h kp_ui_set_locale): from their DLGS, POOL, TFNT, TEFF, DGRP, PCXS, GFIL, GHST,
 * VFNT; TFGM, F8..F16X, its tables the profile's; d and the bytes it points into live until the test ends. */
KpUiData *kp_t_fixture_ui_data(KpT *T);
/* An interface over kp_t_fixture_ui_data, released when the test ends. */
KpUi *kp_t_fixture_ui(KpT *T);
/* A fixture container's bytes (KP_T_FIXTURES name) parsed, live until the test ends; *section its section of that
 * name and *n its size (fails without it). */
const uint8_t *kp_t_fixture_section(KpT *T, const char *name, const char *fourcc, size_t *n);
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
