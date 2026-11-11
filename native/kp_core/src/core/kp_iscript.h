/*
 * The iscript: the unchanged iscript.bin, the script headers by IScript id and the animation slot counts by header
 * type (the profile's SLOT). `code` points into the owned copy of the file.
 */
#ifndef KP_ISCRIPT_H
#define KP_ISCRIPT_H

#include <stddef.h>
#include <stdint.h>

#include "kp_files.h"
#include "kp_kpf.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KP_ISCRIPT_NO_HEADER (-1)

typedef struct {
	uint8_t *blob; /* owned copy of iscript.bin */
	size_t size;
	const uint8_t *code;
	uint32_t code_size;
	uint32_t id_count; /* headers has id_count entries */
	int32_t *headers; /* [IScript id] -> header offset, KP_ISCRIPT_NO_HEADER for none */
	int16_t slots[256]; /* [header type] -> animation slots, -1 for none */
} KpIScript;

void kp_iscript_free(KpIScript *s);

/* A script from a bare iscript.bin (test harnesses' synthetic programs): the code is copied,
 * every one of the id_count headers starts as KP_ISCRIPT_NO_HEADER for the caller to fill and
 * no slot counts are known. Returns -1 with err set when out of memory. */
int kp_iscript_from_code(KpIScript *s, const uint8_t *code, size_t size, uint32_t id_count, KpError *err);
/* iscript.bin read directly (ADR 0018, stage D3): its id index as the converter's HDRS (an id's first header, each one
 * starting with SCPE), the profile's compiled SLOT. */
int kp_iscript_from_files(KpIScript *s, const KpFiles *files, KpError *err);

static inline int32_t kp_iscript_header(const KpIScript *s, uint32_t id)
{
	return id < s->id_count ? s->headers[id] : KP_ISCRIPT_NO_HEADER;
}

#ifdef __cplusplus
}
#endif

#endif
