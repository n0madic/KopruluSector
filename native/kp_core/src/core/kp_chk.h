/*
 * A scenario (CHK) read by the original's loader tables (ADR 0018, stage D4; scdata/formats/chk.py): the chunks as
 * 0x4c1170 enumerates them, the version's descriptor (VER), the sections of a load's groups checked by their handlers'
 * payload rules, in the dispatch order of 0x4c12e0 (table order outer, chunk order inner). The tables are the
 * profile's compiled CHKL (tools/defs_c.py from defs/chk).
 *
 * A campaign load reads the header, briefing and use-map-settings groups; a chunk neither read nor allowlisted, a
 * required section missing or a read one repeated is refused, as the converter refuses them. A custom map's load is
 * the original's for a game (ADR 0024): the header and melee groups, or the use-map-settings ones; an unread chunk is
 * skipped, and a read tag may repeat where its handler's repeat rule says what the repeat does: the last chunk, or
 * the chunks laid over each other (MTXM, MASK).
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

typedef enum {
	KP_CHK_CAMPAIGN, /* header, briefing, use map settings; strict */
	KP_CHK_MELEE, /* header, melee; a game's */
	KP_CHK_UMS, /* header, briefing, use map settings; a game's */
	KP_CHK_HEADER /* the header alone, a game's: a map's for the lists (LoadMapHeader 0x46b5c0) */
} KpChkLoad;

typedef struct {
	char tag[5];
	const uint8_t *data; /* into the CHK's bytes, or owned: the chunks laid over each other */
	uint32_t size;
	uint8_t *owned; /* data when the section owns it (kp_chk_free) */
} KpChkSection;

typedef struct {
	uint16_t version;
	KpChkSection sections[KP_CHK_SECTIONS]; /* every read tag once, in dispatch order */
	int count;
} KpChk;

/* A campaign load: kp_chk_load with KP_CHK_CAMPAIGN. */
int kp_chk_read(KpChk *c, const uint8_t *data, size_t size, const KpDefsProfile *profile, int expansion, char *err,
	size_t err_size);
/* 0, or -1 with the reason in err (nothing to free then; c->version keeps the VER read, 0 without one: LoadMapHeader
 * 0x46b5c0's failure tells a later version by it). The sections point into data unless repeats were laid over each
 * other; kp_chk_free releases those (a campaign load owns none). */
int kp_chk_load(KpChk *c, const uint8_t *data, size_t size, const KpDefsProfile *profile, int expansion,
	KpChkLoad load, char *err, size_t err_size);
void kp_chk_free(KpChk *c);
/* A read section, or NULL. */
const KpChkSection *kp_chk_section(const KpChk *c, const char *tag);

#ifdef __cplusplus
}
#endif

#endif
