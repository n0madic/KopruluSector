/*
 * kp_convert hd-ui-table (ADR 0019, stage C6): the HD interface's placement table (defs/scr/hd-ui-<profile>.json,
 * compiled by tools/defs_c.py as SHUI) from a StarCraft: Remastered install, by the heuristics of the Python
 * converter's hd_ui.py (until stage C6), which live here alone (kp_hd_ui_table.c).
 */
#ifndef KP_HD_UI_TABLE_H
#define KP_HD_UI_TABLE_H

#include <stddef.h>

#include "kp_files.h"
#include "kp_res.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The table of the install r (its SD files through sd) as JSON (malloc'd, *size bytes and a NUL), a line of counts
 * per variant on stdout; NULL with err (an HD file unreadable, a console piece matching no part of its SD console). */
char *kp_hd_ui_table_json(KpRes *r, const KpFiles *sd, size_t *size, KpError *err);

#ifdef __cplusplus
}
#endif

#endif
