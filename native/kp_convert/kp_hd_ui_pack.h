/*
 * The HD interface's container (ui.kpd, HDUI; ADR 0019, stage C6) of a variant's layout from the install
 * (kp_hd_ui_scr.h): what kp_convert repack writes into a variant's ui group, the Python converter's hd_ui.py
 * generate's byte for byte (until stage C6; tests/golden/hdui/converter.json) (its frames' images in IMGS: BC blocks as the DDS holds them, an RGBA image as a PNG).
 */
#ifndef KP_HD_UI_PACK_H
#define KP_HD_UI_PACK_H

#include <stddef.h>
#include <stdint.h>

#include "kp_hd_ui_scr.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ui.kpd of the layout, its frames' images read through h: malloc'd (*size bytes), or NULL with err. */
uint8_t *kp_hd_ui_pack(const KpHdUiScr *s, KpResReader *h, const char *profile_id, size_t *size, KpError *err);

#ifdef __cplusplus
}
#endif

#endif
