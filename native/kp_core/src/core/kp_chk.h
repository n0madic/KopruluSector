/*
 * A scenario (CHK) read by the original's loader tables (ADR 0018, stage D4; converter/formats/chk.py): the chunks as
 * 0x4c1170 enumerates them, the version's descriptor (VER), the sections of the campaign's groups (header, briefing,
 * use-map-settings) checked by their handlers' payload rules, in the dispatch order of 0x4c12e0 (table order outer,
 * chunk order inner). The tables are the profile's compiled CHKL (tools/defs_c.py from defs/chk). A chunk neither read
 * nor allowlisted, a required section missing or a read one repeated is refused, as the converter refuses them.
 */
#ifndef KP_CHK_H
#define KP_CHK_H

#include <stddef.h>
#include <stdint.h>

#include "kp_defs.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KP_CHK_SECTIONS 40

typedef struct {
	char tag[5];
	const uint8_t *data; /* into the CHK's bytes */
	uint32_t size;
} KpChkSection;

typedef struct {
	uint16_t version;
	KpChkSection sections[KP_CHK_SECTIONS]; /* every read tag once, in dispatch order */
	int count;
} KpChk;

/* 0, or -1 with the reason in err. The sections point into data. */
int kp_chk_read(KpChk *c, const uint8_t *data, size_t size, const KpDefsProfile *profile, int expansion, char *err,
	size_t err_size);
/* A read section, or NULL. */
const KpChkSection *kp_chk_section(const KpChk *c, const char *tag);

#ifdef __cplusplus
}
#endif

#endif
