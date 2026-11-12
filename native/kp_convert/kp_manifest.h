/*
 * The input manifest of a profile (defs/scr/inputs-<profile>.json; ADR 0018, ADR 0019): the files the game reads from the
 * profile's reference 1.17 install, as kp_res records the runtime's own loaders reading them (KpResConfig.record), so a
 * file the runtime starts to read is in it without a twin elsewhere. tools/defs_c.py compiles it: kp_res checks a
 * source's copies against it and kp_res_scr.c rebuilds Remastered's own formats by what it keeps.
 *
 * A file read is a row [name, size, SHA-256] as the game names it, sorted by the name in lower case, and what is kept
 * of an image or a movie besides (the research's scdata/casc/rebuild.py reads the same):
 *
 *   PCX   {"pixels", "rgb": the SHA-256 of u16 width, u16 height and the indices or their colours, "palette": of its 768
 *         bytes, "size": [w, h], "header": its bytes 12-15 and 68-69 in hex}
 *   GRP   a GRP of plain rows (the dialogs'; no frame decodes as rows of packets): {"box": [w, h], "frames":
 *         [[x, y, w, h, offset], ...], "pixels": the SHA-256 of the bytes after the frame table}; a GRP in a folder of
 *         PCX files: "palette", the one most of them have (the smallest SHA-256 of the most)
 *   SMK   {"smk": [width, height, frames, ring (0 or 1), the header's frame rate]}; a movie of one palette whose colours
 *         at the indices it draws are a PCX's within SCREEN_PALETTE_TOLERANCE: "palette", that PCX's (the first by
 *         SHA-256 of the nearest)
 *
 * then "absent", the names looked for and not found, and "palettes", the PCX files of each palette. A copy the install
 * holds but cannot give whole (a disc copied with a bad read, issue #2) keeps the committed manifest's row: the profile's
 * data is the release's, not one copy's.
 */
#ifndef KP_MANIFEST_H
#define KP_MANIFEST_H

#include <stddef.h>
#include <stdint.h>

#include "kp_res.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KP_MANIFEST_SCREEN_PALETTE_TOLERANCE 4 /* a glue movie's palette is its screen's but for rounding (6-bit entries) */

typedef struct KpManifest KpManifest;

typedef struct {
	int files, damaged, absent, palettes;
} KpManifestCounts;

/* log: a damaged copy's line (NULL: none). */
KpManifest *kp_manifest_new(void (*log)(void *ctx, const char *line), void *log_ctx);
void kp_manifest_free(KpManifest *m);
/* The recorder (KpResConfig.record, ctx a KpManifest): a file read whole is described as it is read; the first
 * reason a description failed is kp_manifest_text's error. */
void kp_manifest_record(void *ctx, const char *name, KpResRecord what, const uint8_t *data, size_t size);
/* The files only opened (a head, a stream) read whole through res, which records them: 0, or -1 with err. */
int kp_manifest_read_opened(KpManifest *m, KpRes *res, char *err, size_t err_size);
/* The manifest's text of what was recorded (malloc'd), committed the manifest file's text now (NULL: none), whose
 * rows of the damaged copies are kept; NULL with err (a damaged copy it has no row of, a file that could not be
 * described). */
char *kp_manifest_text(KpManifest *m, const char *profile_id, const char *committed, size_t committed_size,
	KpManifestCounts *counts, char *err, size_t err_size);

/* The manifest of the 1.17 install in dir (kp_res_open's rules; the reference: read as it is), its files read through
 * the runtime's loaders: the data (kp_data_build: the DAT, the scripts, grpmeta's GRP and LO files, the palettes and
 * the tilesets' remapping PCX files, the campaign, the interface: its dialogs, pictures, fonts, texts, cursors, sounds,
 * music and portraits), each campaign mission (kp_data_mission: its map, its speech, its tileset's walk) and every
 * tileset (kp_tileset_from_files). The test scenarios (kp_convert scenarios) read their base missions' maps, the
 * terrain checks the tilesets: both among these. log: kp_res's lines and the damaged copies'. As kp_manifest_text. */
char *kp_manifest_build(const char *dir, const char *committed, size_t committed_size, KpManifestCounts *counts,
	void (*log)(void *ctx, const char *line), void *log_ctx, char *err, size_t err_size);

#ifdef __cplusplus
}
#endif

#endif
