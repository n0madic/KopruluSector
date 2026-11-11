/* The AI script binaries: scripts\aiscript.bin and scripts\bwscript.bin as the
 * loader 0x493800 keeps them (0x6a2e74, 0x6a2e78), each with its own directory. The VM is kp_aiscript.c's. */
#ifndef KP_AISCRIPT_H
#define KP_AISCRIPT_H

#include <stddef.h>
#include <stdint.h>

#include "kp_files.h"
#include "kp_kpf.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KP_AI_BINS 2 /* 0 aiscript.bin, 1 bwscript.bin (the script flags' bit 0) */

typedef struct {
	uint8_t *blob; /* owned: aiscript.bin, then bwscript.bin */
	size_t size;
	const uint8_t *bin[KP_AI_BINS];
	uint32_t bin_size[KP_AI_BINS];
} KpAiBins;

/* aiscript.bin and bwscript.bin read directly (ADR 0018, stage D3), their directories checked as the converter does. */
int kp_ai_bins_from_files(KpAiBins *b, const KpFiles *files, KpError *err);
void kp_ai_bins_free(KpAiBins *b);

/* FindAiScriptOffset 0x494d20: the offset of script `id` (its four bytes little-endian) in aiscript.bin's directory
 * (16-byte records) when it has a nonzero one, else in bwscript.bin's (8-byte records, *bin = 1); 0 when neither. */
uint32_t kp_ai_bins_find(const KpAiBins *b, uint32_t id, int *bin);

#ifdef __cplusplus
}
#endif

#endif
