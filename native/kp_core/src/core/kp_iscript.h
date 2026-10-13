/*
 * iscript.kpd (kind ISCR; docs/formats/kpd.md), the C counterpart of parse_iscript in
 * game/data/scripts.lua: the unchanged iscript.bin, the script headers by IScript id and the
 * animation slot counts by header type. The loader copies the file; `code` points into it.
 */
#ifndef KP_ISCRIPT_H
#define KP_ISCRIPT_H

#include <stddef.h>
#include <stdint.h>

#include "kp_kpf.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KP_ISCRIPT_KIND "ISCR"
#define KP_ISCRIPT_SCHEMA 2
#define KP_ISCRIPT_NO_HEADER (-1)

typedef struct {
	uint8_t *blob; /* owned copy of the file */
	size_t size;
	const uint8_t *code;
	uint32_t code_size;
	uint32_t id_count; /* headers has id_count entries */
	int32_t *headers; /* [IScript id] -> header offset, KP_ISCRIPT_NO_HEADER for none */
	int16_t slots[256]; /* [header type] -> animation slots, -1 for none */
} KpIScript;

int kp_iscript_load(KpIScript *s, const uint8_t *data, size_t size, const char *name, const KpKpfExpect *expect,
	KpError *err);
void kp_iscript_free(KpIScript *s);

/* A script from a bare iscript.bin (test harnesses' synthetic programs): the code is copied,
 * every one of the id_count headers starts as KP_ISCRIPT_NO_HEADER for the caller to fill and
 * no slot counts are known. Returns -1 with err set when out of memory. */
int kp_iscript_from_code(KpIScript *s, const uint8_t *code, size_t size, uint32_t id_count, KpError *err);

static inline int32_t kp_iscript_header(const KpIScript *s, uint32_t id)
{
	return id < s->id_count ? s->headers[id] : KP_ISCRIPT_NO_HEADER;
}

#ifdef __cplusplus
}
#endif

#endif
