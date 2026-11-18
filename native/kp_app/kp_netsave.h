/*
 * StarCraft 1.17's multiplayer saves (.mlt; Brood War's .mlx) as a network lobby reads them (atlas P33, P34; phase N,
 * stage N3): the ASCII banner up to its 0x1a, the tag (a raw DWORD whose high WORD is 0x69), the compressed header of
 * KP_NETSAVE_HEADER bytes (+0 the version 105, +2 the sub-version, +4 the RNG state, +0xc the game header, +0x99 the
 * display name), the compressed player table (0x1b0 bytes), the compressed core block 0x5263d0 (0x17700 bytes) and the
 * raw DWORD of the local player. The world after it is not read: Koprulu resumes a network game from its own saves
 * only (the plan leaves a mixed save out), so these readers serve the lobbies of the original's recordings.
 *
 * A file is untrusted input: every read is bounded (the blocks by kp_rep_block_read), a short or malformed file reads
 * as unreadable.
 */
#ifndef KP_NETSAVE_H
#define KP_NETSAVE_H

#include <stddef.h>
#include <stdint.h>

#include "kp_netgame.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KP_NETSAVE_HEADER 0xb5   /* the save header */
#define KP_NETSAVE_VERSION 105   /* its +0 */
#define KP_NETSAVE_NAME 28       /* its display name +0x99, the NUL included */
#define KP_NETSAVE_PLAYERS 0x1b0 /* the player table 0x6726c8 */
#define KP_NETSAVE_CORE 0x17700  /* the core block 0x5263d0 */
#define KP_NETSAVE_FILE_MAX (16u << 20) /* Koprulu's bound of a save file it reads (the original's stay below 1 MB) */
#define KP_NETSAVE_EXT ".mlt"    /* the save extensions (0x4995a0): the original's, Brood War's */
#define KP_NETSAVE_EXT_BW ".mlx"

/* ValidateSaveFile 0x499750's results */
enum { KP_NETSAVE_VALID, KP_NETSAVE_UNREADABLE, KP_NETSAVE_OTHER_VERSION };

/* ValidateSaveFile 0x499750 over a save's bytes and its file's name: KP_NETSAVE_VALID with the game header (h) and the
 * display name (name, KP_NETSAVE_NAME bytes: the name after its last '/' or '\\', at most 31 bytes, cut at its last
 * '.'; it must be shorter than 28 bytes), KP_NETSAVE_UNREADABLE (no banner's end, a wrong tag, a bad block, a long
 * name) or KP_NETSAVE_OTHER_VERSION (the header's version not 105). h and name may be NULL. */
int kp_netsave_probe(const uint8_t *data, size_t size, const char *file, KpNetGameHeader *h, char *name);
/* ReadSaveHeaderAndGameBlock 0x499c50 over a save's bytes: 1 with out filled, 0 when it does not read or its Brood War
 * byte (0x536720) is not `expansion` (0 or 1). The header's version is not checked here (the probe's). */
int kp_netsave_read(const uint8_t *data, size_t size, int expansion, KpNetGameSave *out);

#ifdef __cplusplus
}
#endif

#endif
