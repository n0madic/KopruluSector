/* kp_res's rebuilds of the profile's files from StarCraft: Remastered's own formats (ADR 0018, stage D6): the C of
 * converter/casc/source.py ScrSource._remake over kp_scr.h, by the profile's compiled tables. Internal to kp_res. */
#ifndef KP_RES_SCR_H
#define KP_RES_SCR_H

#include "kp_casc.h"
#include "kp_res.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KP_RES_SOURCE_NAME 512 /* a status's source as kp_res keeps it */

typedef struct KpResScr KpResScr;

/* The tables of a profile (NULL when it has none, or out of memory). */
KpResScr *kp_res_scr_new(const KpDefsProfile *profile);
void kp_res_scr_free(KpResScr *s);

/* A file rebuilt from Remastered's own formats (key normalized, in its manifest entry) after no CASC copy reached the
 * manifest's digest: a malloc'd copy (NUL terminated past *size), its status (derived, equivalent, colour-equal or
 * approximation) and its source; NULL when no rule rebuilds it. */
uint8_t *kp_res_scr_remake(KpRes *r, KpResScr *s, KpCasc *casc, const char *key, const KpDefsInput *in, size_t *size,
	KpResStatus *status, char *source);

/* kp_res.c's: a CASC copy of a listed name equal to its manifest entry (exact, or by kp_res_casc_rebuild), without
 * rebuilding it from other formats; NULL when none. */
uint8_t *kp_res_casc_exact(KpRes *r, const char *key, size_t *size);

/* The KPW1 a movie only Remastered's WebM has stands in for its SMK as (kp_smk.h) */
#define KP_RES_KPW_MAGIC "KPW1"

#ifdef __cplusplus
}
#endif

#endif
