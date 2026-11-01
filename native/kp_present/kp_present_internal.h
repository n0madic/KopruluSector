/*
 * kp_present's helpers shared by its files, not part of its interface (kp_present.h).
 */
#ifndef KP_PRESENT_INTERNAL_H
#define KP_PRESENT_INTERNAL_H

#include "kp_present.h"

/* "name: msg" into err; returns -1 (kp_packs.c). */
int kp_present_fail(KpError *err, const char *name, const char *msg);
/* The end of a container's sections names[0..count-1] from its first bytes; 0 when one is missing or the bytes are not
 * a container's (kp_hd_ui.c). */
size_t kp_present_head_size(const uint8_t *first, size_t n, size_t file_size, const char *const *names, int count);

#endif
